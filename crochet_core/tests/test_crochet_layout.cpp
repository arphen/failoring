// The initial layout of every fixture (docs/CROCHET_PLAN.md section 6.8, ticket T5.3): regions, the
// fold, the strap arc, orientation, and how far the guess is from the rest shapes (the relaxation,
// T5.4, closes that gap). Reads only the committed exports and the yarn database. When the
// environment variable FL_LAYOUT_SHOTS names a directory, the PNG snapshots of every layout are
// written there (docs/crochet-shots/layout/ holds the committed set).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "check.hpp"
#include "failoring/crochet/layout.hpp"
#include "failoring/crochet/pattern.hpp"
#include "failoring/crochet/snapshot.hpp"

using namespace fl;

#define CHECKP(cond, ...)                                 \
    do {                                                  \
        CHECK(cond);                                      \
        if (cond) {                                       \
            std::printf("PASS ");                         \
            std::printf(__VA_ARGS__);                     \
            std::printf("\n");                            \
        }                                                 \
    } while (0)

static StitchGraph loadFixture(const std::string& name) {
    return importPatternFile(std::string(FL_PATTERNS_DIR) + "/cp/exported/" + name + ".json", std::string(FL_PATTERNS_DIR) + "/cp/" + name + ".txt");
}

static std::uint64_t ekey(int a, int b) {
    if (a > b) std::swap(a, b);
    return (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint32_t>(b);
}

struct LayoutStats {
    int nonfinite = 0;
    Real minRatio = 1e300, p5 = 0, median = 0, p95 = 0, maxRatio = 0;  // 3D edge length / rest length
    int interior = 0, disagreeing = 0;                                 // neighbouring winding normals
    Real maxSeamGap = 0;
    Vec3 lo, hi;
};

static Vec3 triNormal(const CrochetModel& cm, int t) {
    const auto& v = cm.mesh.tris[t];
    const auto& x = cm.mesh.x0;
    return normalized(cross(x[v[1]] - x[v[0]], x[v[2]] - x[v[0]]));
}

static LayoutStats measure(const CrochetModel& cm) {
    LayoutStats s;
    const auto& x = cm.mesh.x0;
    const auto& tris = cm.mesh.tris;
    std::vector<Real> ratios;
    for (size_t t = 0; t < tris.size(); ++t)
        for (int k = 0; k < 3; ++k) {
            const Real rest = length(cm.mesh.triRest[t][(k + 1) % 3] - cm.mesh.triRest[t][k]);
            const Real cur = length(x[tris[t][(k + 1) % 3]] - x[tris[t][k]]);
            if (!std::isfinite(cur)) ++s.nonfinite;
            if (rest > 1e-9) ratios.push_back(cur / rest);
        }
    std::sort(ratios.begin(), ratios.end());
    auto pc = [&](double q) { return ratios[static_cast<size_t>(q * (ratios.size() - 1))]; };
    s.minRatio = pc(0);
    s.p5 = pc(0.05);
    s.median = pc(0.5);
    s.p95 = pc(0.95);
    s.maxRatio = pc(1);
    std::map<std::uint64_t, std::vector<int>> et;
    for (size_t t = 0; t < tris.size(); ++t)
        for (int k = 0; k < 3; ++k) et[ekey(tris[t][k], tris[t][(k + 1) % 3])].push_back(static_cast<int>(t));
    for (const auto& kv : et)
        if (kv.second.size() == 2) {
            ++s.interior;
            if (dot(triNormal(cm, kv.second[0]), triNormal(cm, kv.second[1])) < 0) ++s.disagreeing;
        }
    for (const SeamRequest& sr : cm.pointSeams) s.maxSeamGap = std::max(s.maxSeamGap, length(x[sr.a] - x[sr.b]));
    s.lo = s.hi = x[0];
    for (const Vec3& p : x) {
        s.lo = Vec3(std::min(s.lo.x, p.x), std::min(s.lo.y, p.y), std::min(s.lo.z, p.z));
        s.hi = Vec3(std::max(s.hi.x, p.x), std::max(s.hi.y, p.y), std::max(s.hi.z, p.z));
    }
    return s;
}


// --- hand-built graphs (same helpers as test_crochet_mesh) ------------------

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
    s.index += 1;
    s.loop = std::string(type) == "ch" ? "space" : "both";
    s.loopRs = s.loop;
    s.targets = std::move(targets);
    s.prev = g.st.empty() ? -1 : static_cast<int>(g.st.size()) - 1;
    g.st.push_back(s);
    return s.id;
}

struct Fixture {
    const char* name;
    bool bag;
};

int main() {
    std::printf("[crochet layout] %s\n", FL_PATTERNS_DIR);
    const YarnBook book = loadYarnsFromFile(FL_YARNS_PATH);
    const Yarn* acrylic = book.find("acrylic_worsted");
    const Yarn* cotton = book.find("cotton_worsted");
    CHECK(acrylic != nullptr && cotton != nullptr);
    if (acrylic == nullptr || cotton == nullptr) FINISH();
    const char* shotDir = std::getenv("FL_LAYOUT_SHOTS");

    const Fixture fixtures[] = {{"pouch", false},        {"bag_round_plain", true}, {"bag_round_blo", true},  {"bag_round_flo", true},
                                {"bag_round_slst", true}, {"bag_round_surface", true}, {"circle_plus5", false}, {"circle_plus6", false},
                                {"circle_plus8", false}, {"tube40", false},         {"facing", false}};
    std::map<std::string, CrochetModel> models;
    std::map<std::string, LayoutReport> reports;
    std::map<std::string, StitchGraph> graphs;
    for (const Fixture& fx : fixtures) {
        graphs[fx.name] = loadFixture(fx.name);
        const StitchGraph& g = graphs[fx.name];
        ModelOptions opt;
        opt.hook_m = fx.bag ? 4.5e-3 : 5.0e-3;
        CrochetModel cm = buildCrochetModel(g, fx.bag ? *cotton : *acrylic, opt);
        const LayoutReport rep = layoutModel(cm, g);
        const LayoutStats st = measure(cm);
        std::printf("== %s: %zu vertices, bbox x [%.3f %.3f] y [%.3f %.3f] z [%.3f %.3f] m; edge/rest min %.2f median %.3f p95 %.2f max %.2f; "
                    "%d of %d neighbouring normals disagree; seam gap max %.1f mm\n",
                    fx.name, cm.mesh.x0.size(), st.lo.x, st.hi.x, st.lo.y, st.hi.y, st.lo.z, st.hi.z, st.minRatio, st.median, st.p95, st.maxRatio,
                    st.disagreeing, st.interior, st.maxSeamGap * 1e3);
        for (const std::string& w : rep.warnings) std::printf("   warning: %s\n", w.c_str());
        CHECKP(cm.mesh.x0.size() == cm.topo.vertexStitch.size() && cm.mesh.layoutUV.size() == cm.mesh.x0.size() && st.nonfinite == 0,
               "%s: one finite position per vertex", fx.name);
        CHECKP(rep.warnings.empty(), "%s: no layout warnings (%zu)", fx.name, rep.warnings.size());
        // the guess is near the rest shapes: no collapsed edge, no wild stretch, median edge within 30 %
        CHECKP(st.minRatio > 0.3 && st.maxRatio < 3.0 && st.median > 0.95 && st.median < 1.3,
               "%s: edge length / rest length in [%.2f, %.2f], median %.3f", fx.name, st.minRatio, st.maxRatio, st.median);
        models[fx.name] = std::move(cm);
        reports[fx.name] = rep;
        if (shotDir != nullptr) {
            const CrochetModel& m = models[fx.name];
            std::vector<SnapshotColour> colours(m.mesh.tris.size());
            for (size_t t = 0; t < colours.size(); ++t) {
                const int row = graphs[fx.name].st[m.topo.triSource[t]].row;
                const bool alt = row % 2 == 1;
                colours[t] = alt ? SnapshotColour{205, 125, 60} : SnapshotColour{225, 150, 80};
            }
            CHECKP(writeSnapshotPng(std::string(shotDir) + "/" + fx.name + ".png", m.mesh.x0, m.mesh.tris, colours), "%s: snapshot written", fx.name);
        }
    }

    // --- regions of every fixture
    auto regionsOf = [&](const char* name) {
        std::string s;
        for (const LayoutRegion& r : reports[name].regions) {
            static const char* tn[] = {"F", "Flat", "Rev", "Rim", "Strip"};
            s += std::string(tn[r.type]) + "[" + std::to_string(r.firstRow) + "-" + std::to_string(r.lastRow) + "] ";
        }
        return s;
    };
    CHECKP(regionsOf("pouch") == "F[0-0] Flat[1-50] Rim[51-51] Strip[52-131] Rim[132-132] ", "pouch regions: %s", regionsOf("pouch").c_str());
    CHECKP(regionsOf("bag_round_plain") == "F[0-0] Rev[1-44] Strip[45-144] Rim[145-145] ", "bag regions: %s", regionsOf("bag_round_plain").c_str());
    CHECKP(regionsOf("bag_round_surface") == "F[0-0] Rev[1-44] Strip[46-145] Rim[146-146] ", "surface bag regions (the surface sl st row 45 has none): %s",
           regionsOf("bag_round_surface").c_str());
    CHECKP(regionsOf("circle_plus6") == "F[0-0] Rev[1-8] " && regionsOf("tube40") == "F[0-0] Rev[1-12] " && regionsOf("facing") == "F[0-0] Flat[1-4] ",
           "circle, tube, facing regions: %s| %s| %s", regionsOf("circle_plus6").c_str(), regionsOf("tube40").c_str(), regionsOf("facing").c_str());

    // --- the pouch: folded between rows 25 and 26, the back layer behind, the strap an arc of the right length
    {
        const CrochetModel& cm = models["pouch"];
        const StitchGraph& g = graphs["pouch"];
        const LayoutReport& rep = reports["pouch"];
        CHECKP(rep.foldAfterRow == 25, "pouch folds between rows 25 and 26 (%d)", rep.foldAfterRow);
        int front = 0, back = 0, other = 0;
        Real zBack = 0;
        for (const Stitch& s : g.st) {
            const int v = cm.topo.stitchVertex[s.id];
            if (v < 0 || s.row < 1 || s.row > 50) continue;
            const Real z = cm.mesh.x0[v].z;
            if (std::fabs(z) < 1e-12) {
                ++front;
                if (s.row > 25) ++other;
            } else {
                ++back;
                zBack = z;
                if (s.row <= 25) ++other;
            }
        }
        CHECKP(front == 375 && back == 375 && other == 0, "pouch: rows 1-25 in front (%d), rows 26-50 behind (%d), none misplaced (%d); back layer at z = %.2f mm", front, back,
               other, zBack * 1e3);
        // the mouth is on top: rows 1 and 50 are the highest stitches of the body
        Real yRow1 = 0, yRow50 = 0, yRow25 = 0;
        for (const Stitch& s : g.st) {
            const int v = cm.topo.stitchVertex[s.id];
            if (v < 0) continue;
            if (s.row == 1) yRow1 += cm.mesh.x0[v].y / 15;
            if (s.row == 50) yRow50 += cm.mesh.x0[v].y / 15;
            if (s.row == 25) yRow25 += cm.mesh.x0[v].y / 15;
        }
        CHECKP(yRow1 > yRow25 && yRow50 > yRow25 && std::fabs(yRow1 - yRow50) < 0.01, "pouch: mouth on top: row 1 at y = %.1f mm, row 50 at %.1f mm, the fold (row 25) at %.1f mm", yRow1 * 1e3,
               yRow50 * 1e3, yRow25 * 1e3);
        // the strap: 80 rows x h
        const StitchDims sc = cm.dims[g.st[16].id];
        CHECKP(std::fabs(rep.stripArcLength / (80 * sc.h) - 1) < 0.02, "pouch strap: arc length %.1f mm = 80 rows x %.2f mm within 2 %%", rep.stripArcLength * 1e3, sc.h * 1e3);
        {  // the embedded strap itself: the centre line through the row centroids is 80 rows long
            Real path = 0;
            Vec3 prev(0, 0, 0);
            for (int row = 52; row <= 131; ++row) {
                Vec3 c(0, 0, 0);
                int n = 0;
                for (const Stitch& s : g.st)
                    if (s.row == row && cm.topo.stitchVertex[s.id] >= 0) {
                        c += cm.mesh.x0[cm.topo.stitchVertex[s.id]];
                        ++n;
                    }
                c = c / n;
                if (row > 52) path += length(c - prev);
                prev = c;
            }
            CHECKP(std::fabs(path / (79 * sc.h) - 1) < 0.02, "pouch strap: row centroids are %.1f mm of path for 79 row steps of %.2f mm (%.2f %%)", path * 1e3, sc.h * 1e3,
                   100 * (path / (79 * sc.h) - 1));
        }
        CHECKP(cm.pointSeams.size() == 55, "pouch: 55 point seams, largest gap in the layout %.2f mm", measure(cm).maxSeamGap * 1e3);
        const LayoutStats st = measure(cm);
        CHECKP(st.maxSeamGap < 0.02, "pouch: every seam starts within 2 cm (%.1f mm)", st.maxSeamGap * 1e3);
        CHECKP(st.disagreeing >= 10 && st.disagreeing <= 20, "pouch: the fold is the only crease (%d of %d neighbouring normals disagree)", st.disagreeing, st.interior);
    }
    // --- bags: base in the xz plane, walls rise, strap loop above the opening
    for (const char* name : {"bag_round_plain", "bag_round_blo", "bag_round_flo", "bag_round_slst", "bag_round_surface"}) {
        const CrochetModel& cm = models[name];
        const StitchGraph& g = graphs[name];
        const LayoutStats st = measure(cm);
        // wall: the ring of round 44 is level and has a perimeter of 114 stitch widths
        const Stitch* top = nullptr;
        std::vector<int> ring;
        for (const Stitch& s : g.st)
            if (s.row == 44 && cm.topo.stitchVertex[s.id] >= 0) ring.push_back(cm.topo.stitchVertex[s.id]);
        Real ymin = 1e300, ymax = -1e300, perim = 0;
        for (size_t i = 0; i < ring.size(); ++i) {
            ymin = std::min(ymin, cm.mesh.x0[ring[i]].y);
            ymax = std::max(ymax, cm.mesh.x0[ring[i]].y);
            perim += length(cm.mesh.x0[ring[(i + 1) % ring.size()]] - cm.mesh.x0[ring[i]]);
        }
        (void)top;
        Real ringRest = 0;
        for (const Stitch& s : g.st)
            if (s.row == 44 && cm.topo.stitchVertex[s.id] >= 0) ringRest += cm.dims[s.id].w;
        CHECKP(ring.size() == 114 && ymax - ymin < 1e-9 && std::fabs(perim / ringRest - 1) < 0.01,
               "%s: round 44 is a level ring of 114 stitches, perimeter %.1f mm vs %.1f mm of stitch width (%.2f %%)", name, perim * 1e3, ringRest * 1e3, 100 * (perim / ringRest - 1));
        CHECKP(st.maxSeamGap < 0.02 && st.disagreeing <= 3, "%s: strap-end seams start within 2 cm (%.1f mm), %d neighbouring normals disagree", name, st.maxSeamGap * 1e3, st.disagreeing);
        // the strap loop reaches above the mouth by about its radius
        CHECKP(reports[name].stripArcLength > 0.4 && st.hi.y > ymax + 0.05, "%s: strap arc %.0f mm, its top %.0f mm above the mouth", name, reports[name].stripArcLength * 1e3, (st.hi.y - ymax) * 1e3);
    }
    // --- circles: flat for +6, a cup for +5, flat start for +8; the base normal looks up
    {
        const LayoutStats s6 = measure(models["circle_plus6"]), s5 = measure(models["circle_plus5"]), s8 = measure(models["circle_plus8"]);
        CHECKP(s6.hi.y - s6.lo.y < 1e-9 && s8.hi.y - s8.lo.y < 1e-9, "circles +6 and +8 start flat (rise %.3f / %.3f mm)", (s6.hi.y - s6.lo.y) * 1e3, (s8.hi.y - s8.lo.y) * 1e3);
        CHECKP(s5.hi.y - s5.lo.y > 0.4 * 0.0215 && s5.disagreeing == 0, "circle +5 starts as a cup, %.1f mm deep, normals continuous", (s5.hi.y - s5.lo.y) * 1e3);
        const CrochetModel& c6 = models["circle_plus6"];
        int up = 0;
        for (size_t t = 0; t < c6.mesh.tris.size(); ++t) up += triNormal(c6, static_cast<int>(t)).y > 0.99;
        CHECKP(up == static_cast<int>(c6.mesh.tris.size()), "circle +6: the right side looks up (+y) on all %zu faces (%d)", c6.mesh.tris.size(), up);
        // ring radii follow the plan rule: round k of the +6 circle has k * 6 stitches -> radius r_k = N_k w / 2 pi
        const StitchGraph& g = graphs["circle_plus6"];
        Real worst = 0;
        for (int row = 2; row <= 8; ++row) {
            Real rmean = 0;
            int cnt = 0;
            for (const Stitch& s : g.st)
                if (s.row == row && c6.topo.stitchVertex[s.id] >= 0) {
                    const Vec3& p = c6.mesh.x0[c6.topo.stitchVertex[s.id]];
                    rmean += std::sqrt(p.x * p.x + p.z * p.z);
                    ++cnt;
                }
            rmean /= cnt;
            const Real expect = cnt * c6.dims[1].w / (2 * kPi);
            worst = std::max(worst, std::fabs(rmean / expect - 1));
        }
        CHECKP(worst < 0.02, "circle +6: ring radii equal N w / 2 pi within 2 %% (worst %.2f %%)", worst * 100);
    }
    // --- tube: the oval ring of the last round has the circumference of 40 stitches
    {
        const CrochetModel& cm = models["tube40"];
        const StitchGraph& g = graphs["tube40"];
        std::vector<int> ring;
        Real rest = 0;
        for (const Stitch& s : g.st)
            if (s.row == 12 && cm.topo.stitchVertex[s.id] >= 0) {
                ring.push_back(cm.topo.stitchVertex[s.id]);
                rest += cm.dims[s.id].w;
            }
        Real perim = 0;
        for (size_t i = 0; i < ring.size(); ++i) perim += length(cm.mesh.x0[ring[(i + 1) % ring.size()]] - cm.mesh.x0[ring[i]]);
        CHECKP(ring.size() == 40 && std::fabs(perim / rest - 1) < 0.01, "tube40: round 12 has 40 stitches on a ring of %.1f mm (stitch widths %.1f mm)", perim * 1e3, rest * 1e3);
    }
    // --- a flat swatch is laid out at its rest size (the layout of section 6.8 "flat")
    {
        const CrochetModel& cm = models["facing"];
        const StitchGraph& g = graphs["facing"];
        Real xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
        for (const Stitch& s : g.st) {
            if (s.row < 1 || cm.topo.stitchVertex[s.id] < 0) continue;
            const Vec3& p = cm.mesh.x0[cm.topo.stitchVertex[s.id]];
            xmin = std::min(xmin, p.x);
            xmax = std::max(xmax, p.x);
            ymin = std::min(ymin, p.y);
            ymax = std::max(ymax, p.y);
        }
        const Real w = cm.dims[6].w, h = cm.dims[6].h;
        CHECKP(std::fabs((xmax - xmin) / (4 * w) - 1) < 0.03 && std::fabs((ymax - ymin) / (3 * h) - 1) < 0.03,
               "facing swatch 5 x 4: stitch tops span %.2f mm x %.2f mm = 4 w x 3 h (%.2f x %.2f mm) within 3 %%", (xmax - xmin) * 1e3, (ymax - ymin) * 1e3, 4 * w * 1e3, 3 * h * 1e3);
    }

    // --- hand-built: the row smoothing spreads an increase (flat), the orientation fix turns a reversed oval
    {
        // 6 ch, 5 sc back along the chain, then a row of 6 sc whose first two stitches share a target, then 6 plain
        StitchGraph g;
        const int r0 = addRow(g, "row", true);
        for (int i = 0; i < 6; ++i) addSt(g, "ch", r0, {});
        std::vector<int> prev;
        const int r1 = addRow(g, "row", true);
        for (int c = 4; c >= 0; --c) prev.push_back(addSt(g, "sc", r1, {c}));
        const int r2 = addRow(g, "row", true);
        addSt(g, "ch", r2, {});
        std::vector<int> row2;
        const int tg[6] = {prev[4], prev[4], prev[3], prev[2], prev[1], prev[0]};
        for (int i = 0; i < 6; ++i) row2.push_back(addSt(g, "sc", r2, {tg[i]}));
        const int r3 = addRow(g, "row", true);
        addSt(g, "ch", r3, {});
        for (int i = 0; i < 6; ++i) addSt(g, "sc", r3, {row2[5 - i]});
        CrochetModel cm = buildCrochetModel(g, *acrylic, ModelOptions());
        const LayoutReport rep = layoutModel(cm, g);
        const Real w = cm.dims[row2[0]].w;
        Real minGap = 1e300, xmin = 1e300, xmax = -1e300;
        for (int i = 0; i < 6; ++i) {
            const Vec3& p = cm.mesh.x0[cm.topo.stitchVertex[row2[i]]];
            xmin = std::min(xmin, p.x);
            xmax = std::max(xmax, p.x);
            if (i > 0) minGap = std::min(minGap, length(p - cm.mesh.x0[cm.topo.stitchVertex[row2[i - 1]]]));
        }
        // the plan's smoothing (spacing w, target weight 0.1) trades a little span for even stitches: 4.8 w of 5 w
        CHECKP(rep.warnings.empty() && minGap > 0.9 * w && (xmax - xmin) > 0.94 * 5 * w && (xmax - xmin) <= 5 * w + 1e-12,
               "increase row: stitches at least %.2f mm apart (w = %.2f mm), the row spans %.2f mm of 5 w = %.2f mm", minGap * 1e3, w * 1e3, (xmax - xmin) * 1e3, 5 * w * 1e3);
    }
    {
        // an inside-out mesh (every winding reversed) must still end with the right side up: the placement is mirrored
        for (const char* name : {"circle_plus6", "facing"}) {
            CrochetModel cm = models[name];
            for (auto& t : cm.topo.tris) std::swap(t[1], t[2]);
            for (auto& t : cm.mesh.tris) std::swap(t[1], t[2]);
            const LayoutReport rep = layoutModel(cm, graphs[name]);
            const bool flat = std::string(name) == "facing";
            int right = 0;
            for (size_t t = 0; t < cm.mesh.tris.size(); ++t) {
                const Vec3 n = triNormal(cm, static_cast<int>(t));
                right += (flat ? n.z : n.y) > 0.99;
            }
            bool mirrored = false;
            for (const LayoutRegion& r : rep.regions) mirrored = mirrored || r.mirrored;
            CHECKP(rep.warnings.empty() && mirrored && right == static_cast<int>(cm.mesh.tris.size()),
                   "%s with every winding reversed: placed mirrored, all %zu faces have the right side %s (%d)", name, cm.mesh.tris.size(), flat ? "toward +z" : "up", right);
        }
    }
    // --- deterministic
    {
        CrochetModel again = buildCrochetModel(graphs["pouch"], *acrylic, ModelOptions());
        layoutModel(again, graphs["pouch"]);
        bool same = again.mesh.x0.size() == models["pouch"].mesh.x0.size();
        for (size_t i = 0; same && i < again.mesh.x0.size(); ++i) same = length(again.mesh.x0[i] - models["pouch"].mesh.x0[i]) == 0;
        CHECKP(same, "pouch: the layout is deterministic");
    }
    FINISH();
}
