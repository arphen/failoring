// The stitch graph -> mesh topology stage (docs/CROCHET_PLAN.md sections 6.1-6.4,
// ticket T5.1) and its hygiene test (section 6.10, test 4) on every committed
// fixture: faces, per-triangle rest shapes and the orientation. Reads only the
// committed exports under FL_PATTERNS_DIR and the yarn database: no node, no
// CrochetPARADE checkout at test time.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "check.hpp"
#include "failoring/crochet/mesh.hpp"
#include "failoring/crochet/pattern.hpp"

using namespace fl;

// CHECK plus a PASS line carrying the measured number, so ctest output is
// readable when the test fails on one of the bounds.
#define CHECKP(cond, ...)                                 \
    do {                                                  \
        CHECK(cond);                                      \
        if (cond) {                                       \
            std::printf("PASS ");                         \
            std::printf(__VA_ARGS__);                     \
            std::printf("\n");                            \
        }                                                 \
    } while (0)

static std::string fixturePath(const std::string& name) { return std::string(FL_PATTERNS_DIR) + "/cp/" + name + ".txt"; }
static std::string exportPath(const std::string& name) { return std::string(FL_PATTERNS_DIR) + "/cp/exported/" + name + ".json"; }
static StitchGraph loadFixture(const std::string& name) { return importPatternFile(exportPath(name), fixturePath(name)); }

static std::uint64_t ekey(int a, int b) {
    if (a > b) std::swap(a, b);
    return (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint32_t>(b);
}

// --- reusable checks on a built mesh -----------------------------------

struct MeshStats {
    int vertices = 0, edges = 0, faces = 0, euler = 0;
    int maxFacesPerEdge = 0, boundaryEdges = 0;
    int inconsistentEdges = 0;  // manifold edges two faces traverse the same way
    int mixedHandedness = 0;    // triangles whose rest winding differs from the first triangle's
    int badIndices = 0;
    Real minArea = 1e300, maxArea = 0;
    int degenerate = 0;         // rest area below the threshold
};

static MeshStats analyse(const CrochetMesh& m, Real minAreaAllowed) {
    MeshStats s;
    s.vertices = static_cast<int>(m.vertexStitch.size());
    s.faces = static_cast<int>(m.tris.size());
    std::map<std::uint64_t, std::vector<std::pair<int, int>>> dir;  // edge -> directed (a, b) of each face
    Real firstDet = 0;
    for (size_t t = 0; t < m.tris.size(); ++t) {
        const auto& tr = m.tris[t];
        for (int k = 0; k < 3; ++k) {
            if (tr[k] < 0 || tr[k] >= s.vertices) ++s.badIndices;
            dir[ekey(tr[k], tr[(k + 1) % 3])].push_back({tr[k], tr[(k + 1) % 3]});
        }
        const auto& r = m.triRest[t];
        const Real det = cross(r[1] - r[0], r[2] - r[0]);
        const Real area = 0.5 * std::fabs(det);
        s.minArea = std::min(s.minArea, area);
        s.maxArea = std::max(s.maxArea, area);
        if (area < minAreaAllowed) ++s.degenerate;
        if (t == 0) firstDet = det;
        else if ((det > 0) != (firstDet > 0)) ++s.mixedHandedness;
    }
    s.edges = static_cast<int>(dir.size());
    for (const auto& kv : dir) {
        s.maxFacesPerEdge = std::max(s.maxFacesPerEdge, static_cast<int>(kv.second.size()));
        if (kv.second.size() == 1) ++s.boundaryEdges;
        if (kv.second.size() == 2 && kv.second[0] == kv.second[1]) ++s.inconsistentEdges;
    }
    s.euler = s.vertices - s.edges + s.faces;
    return s;
}

static int componentsOfPiece(const StitchGraph& g, const CrochetMesh& m, const std::string& piece) {
    std::set<int> comps;
    for (size_t t = 0; t < m.tris.size(); ++t)
        if (g.st[m.triSource[t]].piece == piece) comps.insert(m.triComponent[t]);
    return static_cast<int>(comps.size());
}

// Expected triangle count straight from the graph, without the builder's path
// search: a pair of consecutive anchors whose last/first targets are the same
// node makes one triangle, neighbouring targets make two (neighbours = adjacent
// anchors of one row, or the first and last anchor of a closed round). A round
// closes when those two target nodes are equal or neighbours. Pairs the mesh
// reports as jumps make none.
static int expectedTris(const StitchGraph& g, const CrochetMesh& m, const std::set<int>& surfaceRows = {}) {
    std::set<std::pair<int, int>> jumpPairs;
    for (const MeshJump& j : m.jumps) jumpPairs.insert({j.a, j.b});
    std::vector<std::vector<int>> anchors(g.rows.size());
    for (const Row& row : g.rows) {
        int last = -1;
        for (const Stitch& q : g.st)
            if (q.row == row.id) last = q.id;
        for (const Stitch& s : g.st) {
            if (s.row != row.id || s.targets.empty()) continue;
            const bool closing = s.type == "slst" && row.kind == "round" && s.targets.size() == 1 &&
                                 g.st[s.targets[0]].row == s.row && s.id == last;
            if (!closing) anchors[row.id].push_back(s.id);
        }
    }
    std::vector<char> closed(g.rows.size(), 0);
    std::map<int, std::pair<int, int>> pos;  // stitch id -> (row, position among that row's anchors)
    for (size_t r = 0; r < anchors.size(); ++r)
        for (size_t i = 0; i < anchors[r].size(); ++i) pos[anchors[r][i]] = {static_cast<int>(r), static_cast<int>(i)};
    auto neighbours = [&](int u, int v) {
        if (u == v) return true;
        const auto pu = pos.find(u), pv = pos.find(v);
        if (pu == pos.end() || pv == pos.end() || pu->second.first != pv->second.first) return false;
        const int r = pu->second.first, d = std::abs(pu->second.second - pv->second.second);
        const int n = static_cast<int>(anchors[r].size());
        return d == 1 || (closed[r] && d == n - 1 && n > 2);
    };
    int total = 0;
    for (size_t r = 0; r < anchors.size(); ++r) {
        if (surfaceRows.count(static_cast<int>(r))) continue;  // a surface sl st row lies on the fabric: no faces
        const std::vector<int>& a = anchors[r];
        for (size_t i = 1; i < a.size(); ++i) {
            if (jumpPairs.count({a[i - 1], a[i]})) continue;
            const int u = g.st[a[i - 1]].targets.back(), v = g.st[a[i]].targets.front();
            total += 1 + (u != v);
        }
        if (g.rows[r].kind == "round" && a.size() > 1 && neighbours(g.st[a.back()].targets.back(), g.st[a.front()].targets.front())) {
            closed[r] = 1;
            const int u = g.st[a.back()].targets.back(), v = g.st[a.front()].targets.front();
            total += 1 + (u != v);
        }
    }
    return total;
}


// --- hand-built graphs --------------------------------------------------

// Append a stitch to g in made order: prev is the previous stitch, index counts
// within the row. Rows must be added in increasing order via addRow.
static int addRow(StitchGraph& g, const char* kind, bool turned) {
    Row r;
    r.id = static_cast<int>(g.rows.size());
    r.piece = "main";
    r.kind = kind;
    r.turned = turned;
    g.rows.push_back(r);
    return r.id;
}
static int addSt(StitchGraph& g, const char* type, int row, std::vector<int> targets) {
    Stitch s;
    s.id = static_cast<int>(g.st.size());
    s.type = type;
    s.piece = "main";
    s.row = row;
    for (const Stitch& q : g.st) s.index += q.row == row;
    s.index += 1;  // -1 + count
    s.loop = std::string(type) == "ch" ? "space" : "both";
    s.loopRs = s.loop;
    s.targets = std::move(targets);
    s.prev = g.st.empty() ? -1 : static_cast<int>(g.st.size()) - 1;
    g.st.push_back(s);
    return s.id;
}

static std::vector<StitchDims> uniformDims(const StitchGraph& g, Real w, Real h) {
    StitchDims d;
    d.w = w;
    d.h = h;
    d.yarnLen = 0.01;
    d.thickness = 0.002;
    return std::vector<StitchDims>(g.st.size(), d);
}

// A swatch of `cols` sc over `rows` turned rows on a foundation of cols + 1 ch.
static StitchGraph makeSwatch(int cols, int rows) {
    StitchGraph g;
    const int r0 = addRow(g, "row", true);
    for (int i = 0; i <= cols; ++i) addSt(g, "ch", r0, {});
    std::vector<int> prevRow;  // ids of the previous row's sc in made order
    for (int c = cols - 1; c >= 0; --c) prevRow.push_back(c);  // row 1 works back along the chain, leaving the last ch unused
    for (int r = 1; r <= rows; ++r) {
        const int row = addRow(g, "row", true);
        std::vector<int> cur;
        if (r > 1) addSt(g, "ch", row, {});  // turning chain
        for (int c = 0; c < cols; ++c) {
            const int target = r == 1 ? prevRow[c] : prevRow[cols - 1 - c];
            cur.push_back(addSt(g, "sc", row, {target}));
        }
        prevRow = cur;
    }
    return g;
}

static bool anyWarningContains(const CrochetMesh& m, const char* text) {
    for (const std::string& w : m.warnings)
        if (w.find(text) != std::string::npos) return true;
    return false;
}

static void handBuilt() {
    const Real w = 6.5e-3, h = 6.0e-3, minA = 0.01 * w * h;

    // A: 4 x 3 swatch -> a 4 x 4 grid of vertices, 18 triangles, 12 boundary edges.
    {
        const StitchGraph g = makeSwatch(4, 3);
        const CrochetMesh m = buildMeshTopology(g, uniformDims(g, w, h));
        const MeshStats st = analyse(m, minA);
        CHECKP(m.warnings.empty() && st.vertices == 16 && st.faces == 18 && st.boundaryEdges == 12 && st.euler == 1 &&
                   st.maxFacesPerEdge == 2,
               "swatch 4x3: 16 vertices, 18 faces, 12 boundary edges, euler 1 (%d, %d, %d, %d, %zu warnings)", st.vertices, st.faces,
               st.boundaryEdges, st.euler, m.warnings.size());
        CHECKP(st.inconsistentEdges == 0 && st.mixedHandedness == 0 && m.components == 1,
               "swatch 4x3: oriented consistently across the turned rows (%d bad edges, %d mixed, %d component)", st.inconsistentEdges,
               st.mixedHandedness, m.components);
        // the unused last chain and the two turning chains drop out and hand their yarn to a kept neighbour
        int dropped = 0, dropOk = 0;
        for (size_t i = 0; i < g.st.size(); ++i)
            if (m.stitchVertex[i] < 0) {
                ++dropped;
                dropOk += m.dropTo[i] >= 0 && m.dropTo[i] < st.vertices;
            }
        CHECKP(dropped == 3 && dropOk == 3, "swatch 4x3: last chain + 2 turning chains dropped, each with a kept vertex for its mass (%d/%d)", dropOk, dropped);
        // total rest area: 9 quads (3 gaps x 3 rows) of w x h
        Real area = 0;
        for (const auto& t : m.triRest) area += 0.5 * std::fabs(cross(t[1] - t[0], t[2] - t[0]));
        CHECKP(std::fabs(area / (9 * w * h) - 1) < 0.02, "swatch 4x3: total rest area %.2f mm^2 = 9 w h within 2 %% (%.2f)", area * 1e6, 9 * w * h * 1e6);
    }

    // B: three sc into one ring chain: three increase triangles around the centre.
    {
        StitchGraph g;
        const int r0 = addRow(g, "round", false);
        addSt(g, "ch", r0, {});
        const int r1 = addRow(g, "round", false);
        for (int i = 0; i < 3; ++i) addSt(g, "sc", r1, {0});
        const std::vector<StitchDims> d = uniformDims(g, w, h);
        const CrochetMesh m = buildMeshTopology(g, d);
        const MeshStats st = analyse(m, minA);
        CHECKP(st.vertices == 4 && st.faces == 3 && st.euler == 1 && m.warnings.empty(), "ring: 4 vertices, 3 triangles, euler 1 (%d, %d, %d)", st.vertices,
               st.faces, st.euler);
        // plan 6.3: base is one point, top is one stitch width, Hf = sqrt(h^2 - (w/2)^2); corners are (u, v) = (y, x)
        const Real hf = std::sqrt(h * h - 0.25 * w * w);
        const auto& r = m.triRest[0];
        int base = 0, top = 0;
        Real vspread = 0;
        for (const Vec2& c : r) {
            if (std::fabs(c.x) < 1e-12) ++base;
            else if (std::fabs(c.x - hf) < 1e-12) ++top;
            vspread = std::max(vspread, std::fabs(c.y));
        }
        CHECKP(base == 1 && top == 2 && std::fabs(vspread - 0.5 * w) < 1e-12,
               "ring: increase triangle has the base at u = 0, two top corners at u = Hf = %.4f mm, course +-w/2 = %.4f mm", hf * 1e3, vspread * 1e3);
    }

    // C: a decrease (one sc worked into two stitches): a single triangle on the base edge.
    {
        StitchGraph g;
        const int r0 = addRow(g, "row", false);
        for (int i = 0; i < 3; ++i) addSt(g, "ch", r0, {});
        const int r1 = addRow(g, "row", false);
        addSt(g, "sc", r1, {1, 0});
        const std::vector<StitchDims> d = uniformDims(g, w, h);
        const CrochetMesh m = buildMeshTopology(g, d);
        CHECKP(m.tris.size() == 1 && m.warnings.empty() && m.stitchVertex[3] >= 0, "decrease: one triangle on the two targets (%zu faces, %zu warnings)", m.tris.size(),
               m.warnings.size());
        if (!m.tris.empty()) {
            const Real hf = std::sqrt(std::max(h * h - 0.25 * w * w, 0.09 * h * h));  // base = one chain edge of length w
            Real umax = 0;
            for (const Vec2& c : m.triRest[0]) umax = std::max(umax, c.x);
            CHECKP(std::fabs(umax - hf) < 1e-12, "decrease: apex at u = Hf = %.4f mm", umax * 1e3);
        }
    }

    // D: a chain between two sc is part of the top polyline: top 3 nodes over a 4-node base = 5 triangles.
    {
        StitchGraph g;
        const int r0 = addRow(g, "row", false);
        for (int i = 0; i < 4; ++i) addSt(g, "ch", r0, {});
        const int r1 = addRow(g, "row", false);
        addSt(g, "sc", r1, {0});
        addSt(g, "ch", r1, {});
        addSt(g, "sc", r1, {3});
        const CrochetMesh m = buildMeshTopology(g, uniformDims(g, w, h));
        const MeshStats st = analyse(m, minA);
        CHECKP(m.tris.size() == 5 && m.warnings.empty() && m.stitchVertex[5] >= 0 && st.maxFacesPerEdge <= 2,
               "chain space: 3 top nodes over 4 base nodes make 5 triangles, the chain is a vertex (%zu faces)", m.tris.size());
    }

    // E: targets too far apart: a hole warning, no face; a link turns it into a recorded jump.
    {
        StitchGraph g;
        const int r0 = addRow(g, "row", false);
        for (int i = 0; i < 12; ++i) addSt(g, "ch", r0, {});
        const int r1 = addRow(g, "row", false);
        addSt(g, "sc", r1, {0});
        addSt(g, "sc", r1, {11});
        const CrochetMesh m = buildMeshTopology(g, uniformDims(g, w, h));
        CHECKP(m.tris.empty() && anyWarningContains(m, "hole between stitch 12 and 13") && m.jumps.empty(),
               "far targets: no face and a 'hole between stitch 12 and 13' warning (%zu warnings, %zu faces)", m.warnings.size(), m.tris.size());
        g.links.push_back({12, 13, "join"});
        const CrochetMesh j = buildMeshTopology(g, uniformDims(g, w, h));
        CHECKP(j.tris.empty() && j.jumps.size() == 1 && j.jumps[0].a == 12 && j.jumps[0].b == 13 && !anyWarningContains(j, "hole"),
               "far targets with a link: a recorded jump, no hole warning (%zu jumps)", j.jumps.size());
    }

    // F: a sl st row on the fabric is no hole (surface); an sc row on the same edges is.
    for (int variant = 0; variant < 2; ++variant) {
        StitchGraph g = makeSwatch(4, 3);
        const int top = static_cast<int>(g.rows.size()) - 1;
        // the middle row's stitches (row 2): their course edges are in two faces
        std::vector<int> row2;
        for (const Stitch& s : g.st)
            if (s.row == 2 && s.type == "sc") row2.push_back(s.id);
        const int surfaceRow = addRow(g, "row", false);
        (void)top;
        const char* type = variant == 0 ? "slst" : "sc";
        for (int i = 0; i < 3; ++i) addSt(g, type, surfaceRow, {row2[i]});
        const CrochetMesh m = buildMeshTopology(g, uniformDims(g, w, h));
        if (variant == 0) {
            CHECKP(m.warnings.empty() && m.tris.size() == 18 && m.stitchVertex[g.st.size() - 1] < 0 && m.dropTo[g.st.size() - 1] >= 0,
                   "surface sl st row: no faces, no warning, the sl sts drop onto the fabric (%zu faces, %zu warnings)", m.tris.size(), m.warnings.size());
        } else {
            CHECKP(anyWarningContains(m, "hole between stitch") && m.tris.size() == 18,
                   "an sc row on full edges is a hole, not a surface row (%zu warnings)", m.warnings.size());
        }
    }

    // G: input validation.
    {
        const StitchGraph g = makeSwatch(3, 2);
        std::vector<StitchDims> d = uniformDims(g, w, h);
        d.pop_back();
        bool threw = false;
        try { buildMeshTopology(g, d); } catch (const std::runtime_error&) { threw = true; }
        CHECKP(threw, "wrong number of stitch sizes throws");
        d = uniformDims(g, w, h);
        d[3].h = 0;
        threw = false;
        try { buildMeshTopology(g, d); } catch (const std::runtime_error& e) { threw = std::string(e.what()).find("stitch 3") != std::string::npos; }
        CHECKP(threw, "a zero stitch height throws, naming the stitch");
    }
}

int main() {
    std::printf("[crochet mesh] %s\n", FL_PATTERNS_DIR);
    const YarnBook book = loadYarnsFromFile(FL_YARNS_PATH);
    const Yarn* yarn = book.find("acrylic_worsted");
    CHECK(yarn != nullptr);
    if (yarn == nullptr) FINISH();
    const Real hook = 5.0e-3;
    const StitchDims sc = stitchDims(*yarn, hook, 1.0, StitchType::Sc, Insertion::Both, nullptr);
    const Real minArea = 0.01 * sc.w * sc.h;  // section 6.10 test 4: rest area > 1 % of w x h

    handBuilt();

    const char* names[] = {"pouch",          "bag_round_plain", "bag_round_blo",   "bag_round_flo", "bag_round_slst",
                           "bag_round_surface", "circle_plus5", "circle_plus6", "circle_plus8", "tube40", "facing"};
    for (const char* name : names) {
        const StitchGraph g = loadFixture(name);
        const std::vector<StitchDims> dims = graphDims(g, *yarn, hook, 1.0, nullptr);
        const CrochetMesh m = buildMeshTopology(g, dims);
        const MeshStats st = analyse(m, minArea);
        int dropped = 0;
        for (const int v : m.stitchVertex) dropped += v < 0;
        std::printf("== %s: %zu stitches -> %d vertices (%d dropped), %d faces, %d edges, %d boundary edges, euler %d, "
                    "%d components, %zu jumps, %zu warnings\n",
                    name, g.st.size(), st.vertices, dropped, st.faces, st.edges, st.boundaryEdges, st.euler, m.components,
                    m.jumps.size(), m.warnings.size());
        for (size_t i = 0; i < m.warnings.size() && i < 8; ++i) std::printf("   warning: %s\n", m.warnings[i].c_str());
        for (const MeshJump& j : m.jumps)
            std::printf("   jump: stitch %d [%d,%d] -> stitch %d [%d,%d]\n", j.a, g.st[j.a].row, g.st[j.a].index, j.b, g.st[j.b].row,
                        g.st[j.b].index);
        CHECKP(m.warnings.empty(), "%s: no warnings (%zu)", name, m.warnings.size());
        CHECKP(st.badIndices == 0, "%s: all triangle vertex ids valid (%d bad)", name, st.badIndices);
        CHECKP(st.maxFacesPerEdge <= 2, "%s: every edge in at most 2 faces (max %d)", name, st.maxFacesPerEdge);
        CHECKP(st.inconsistentEdges == 0, "%s: orientation consistent across every manifold edge (%d bad)", name, st.inconsistentEdges);
        CHECKP(st.mixedHandedness == 0, "%s: all rest shapes wind like their vertices (%d mixed)", name, st.mixedHandedness);
        CHECKP(st.degenerate == 0, "%s: no degenerate triangle: min rest area %.3f mm^2 > %.3f mm^2 (%d bad)", name,
               st.minArea * 1e6, minArea * 1e6, st.degenerate);
        CHECKP(componentsOfPiece(g, m, "main") == 1, "%s: piece main is one connected component (%d)", name, componentsOfPiece(g, m, "main"));
        std::set<int> surfaceRows;
        if (std::string(name) == "bag_round_surface") surfaceRows.insert(45);  // the 114-slst surface round
        const int expected = expectedTris(g, m, surfaceRows);
        CHECKP(st.faces == expected, "%s: %d faces == %d from the pair count", name, st.faces, expected);
        // every stitch has a mass home; kept stitches are their own
        int homeless = 0, keptWrong = 0;
        for (size_t i = 0; i < g.st.size(); ++i) {
            if (m.dropTo[i] < 0 || m.dropTo[i] >= st.vertices) ++homeless;
            else if (m.stitchVertex[i] >= 0 && m.dropTo[i] != m.stitchVertex[i]) ++keptWrong;
        }
        CHECKP(homeless == 0 && keptWrong == 0, "%s: every stitch has a vertex for its mass (%d without, %d kept stitches misrouted)", name, homeless, keptWrong);
    }
    FINISH();
}
