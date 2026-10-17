// The zero-gravity relaxation of a crochet layout and the geometry tests of docs/CROCHET_PLAN.md
// section 6.10 (ticket T5.4): test 1 swatch size, test 2 flat / cup / ruffle circle (the key one),
// test 3 tube, test 5 hinge sign (BLO turns toward the WS, FLO the other way), and the relaxation of
// the pouch (seam gap, convergence). Newton convergence is reported (iterCapHits, plan 1 rule 6).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "check.hpp"
#include "failoring/crochet/layout.hpp"
#include "failoring/crochet/pattern.hpp"
#include "failoring/crochet/relax.hpp"
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

static int addRow(StitchGraph& g, const char* kind, bool turned) {
    Row r;
    r.id = static_cast<int>(g.rows.size());
    r.piece = "main";
    r.kind = kind;
    r.turned = turned;
    g.rows.push_back(r);
    return r.id;
}
static int addSt(StitchGraph& g, const char* type, int row, std::vector<int> targets, const char* loop = nullptr) {
    Stitch s;
    s.id = static_cast<int>(g.st.size());
    s.type = type;
    s.piece = "main";
    s.row = row;
    for (const Stitch& q : g.st) s.index += q.row == row;
    s.index += 1;
    s.loop = loop != nullptr ? loop : (std::string(type) == "ch" ? "space" : "both");
    s.loopRs = s.loop;
    s.targets = std::move(targets);
    s.prev = g.st.empty() ? -1 : static_cast<int>(g.st.size()) - 1;
    g.st.push_back(s);
    return s.id;
}

// cols sc x rows turned rows on a foundation of cols + 1 ch (the last ch is unused, like `sk`); the row bloRow (0 = none)
// is worked through the back loop (loop = "blo") or front loop ("flo"). Facing is resolved, so row 11 is on the RS.
static StitchGraph makeSwatch(int cols, int rows, int loopRow = 0, const char* loop = "both") {
    StitchGraph g;
    const int r0 = addRow(g, "row", true);
    for (int i = 0; i <= cols; ++i) addSt(g, "ch", r0, {});
    std::vector<int> prevRow;
    for (int c = cols - 1; c >= 0; --c) prevRow.push_back(c);
    for (int r = 1; r <= rows; ++r) {
        const int row = addRow(g, "row", true);
        std::vector<int> cur;
        if (r > 1) addSt(g, "ch", row, {});
        for (int c = 0; c < cols; ++c) {
            const int target = r == 1 ? prevRow[c] : prevRow[cols - 1 - c];
            cur.push_back(addSt(g, "sc", row, {target}, r == loopRow ? loop : "both"));
        }
        prevRow = cur;
    }
    resolveFacing(g);
    return g;
}

// Smallest eigenvector of the covariance (Jacobi): the normal of the best-fit plane through x.
static Vec3 planeNormal(const std::vector<Vec3>& x, Vec3& centroid) {
    centroid = Vec3(0, 0, 0);
    for (const Vec3& p : x) centroid += p;
    centroid = centroid / static_cast<Real>(x.size());
    Real a[3][3] = {};
    for (const Vec3& p : x) {
        const Vec3 d = p - centroid;
        const Real v[3] = {d.x, d.y, d.z};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) a[i][j] += v[i] * v[j];
    }
    Real V[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int sweep = 0; sweep < 60; ++sweep)
        for (int p = 0; p < 2; ++p)
            for (int q = p + 1; q < 3; ++q) {
                if (std::fabs(a[p][q]) < 1e-30) continue;
                const Real th = 0.5 * std::atan2(2 * a[p][q], a[q][q] - a[p][p]), c = std::cos(th), s = std::sin(th);
                for (int k = 0; k < 3; ++k) {
                    const Real akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 3; ++k) {
                    const Real apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 3; ++k) {
                    const Real vkp = V[k][p], vkq = V[k][q];
                    V[k][p] = c * vkp - s * vkq;
                    V[k][q] = s * vkp + c * vkq;
                }
            }
    int m = 0;
    for (int i = 1; i < 3; ++i)
        if (a[i][i] < a[m][m]) m = i;
    return normalized(Vec3(V[0][m], V[1][m], V[2][m]));
}

// With FL_RELAX_SHOTS=<dir> every relaxed shape is written as a PNG (docs/crochet-shots/relax/ holds the committed set).
static void shot(const std::string& name, const std::vector<Vec3>& x, const std::vector<std::array<int, 3>>& tris) {
    const char* dir = std::getenv("FL_RELAX_SHOTS");
    if (dir != nullptr) writeSnapshotPng(std::string(dir) + "/" + name + ".png", x, tris);
}

struct Prepared {
    StitchGraph g;
    CrochetModel cm;
};

static Prepared prepare(StitchGraph g, const Yarn& yarn, Real hook) {
    Prepared p;
    p.g = std::move(g);
    ModelOptions opt;
    opt.hook_m = hook;
    p.cm = buildCrochetModel(p.g, yarn, opt);
    layoutModel(p.cm, p.g);
    return p;
}

static std::vector<int> vertsOfRows(const Prepared& p, int rowA, int rowB) {
    std::vector<int> out;
    for (const Stitch& s : p.g.st)
        if (s.row >= rowA && s.row <= rowB && p.cm.topo.stitchVertex[s.id] >= 0) out.push_back(p.cm.topo.stitchVertex[s.id]);
    return out;
}

static std::vector<Vec3> pointsOf(const Prepared& p, const std::vector<int>& vs) {
    std::vector<Vec3> out;
    for (const int v : vs) out.push_back(p.cm.mesh.x0[v]);
    return out;
}

// Circle measures (plan 6.10 test 2). The rim is the last round.
struct CircleMeasure {
    Real radius = 0, maxDev = 0, depth = 0, oneSided = 0;
    int signChanges = 0;
};

static CircleMeasure measureCircle(const Prepared& p, int lastRow, bool relativeToRim) {
    CircleMeasure m;
    const std::vector<int> all = vertsOfRows(p, 0, lastRow), rim = vertsOfRows(p, lastRow, lastRow);
    Vec3 c, n;
    n = planeNormal(relativeToRim ? pointsOf(p, rim) : pointsOf(p, all), c);
    Vec3 e1 = normalized(cross(n, Vec3(1, 0, 0)));
    if (length(e1) < 0.5) e1 = normalized(cross(n, Vec3(0, 0, 1)));
    const Vec3 e2 = cross(n, e1);
    int pos = 0, neg = 0;
    for (const int v : all) {
        const Vec3 d = p.cm.mesh.x0[v] - c;
        const Real h = dot(d, n);
        m.radius = std::max(m.radius, length(d - n * h));
        m.maxDev = std::max(m.maxDev, std::fabs(h));
    }
    for (const int v : all) {
        const Real h = dot(p.cm.mesh.x0[v] - c, n);
        if (h > 0.01 * m.radius) ++pos;
        if (h < -0.01 * m.radius) ++neg;
    }
    m.oneSided = pos + neg > 0 ? static_cast<Real>(std::max(pos, neg)) / (pos + neg) : 1;
    m.depth = m.maxDev / m.radius;
    std::vector<std::pair<Real, Real>> ring;  // (angle, height)
    Real mean = 0;
    for (const int v : rim) {
        const Vec3 d = p.cm.mesh.x0[v] - c;
        ring.push_back({std::atan2(dot(d, e2), dot(d, e1)), dot(d, n)});
        mean += ring.back().second;
    }
    std::sort(ring.begin(), ring.end());
    mean /= ring.size();
    for (size_t k = 0; k < ring.size(); ++k) {
        const Real a = ring[k].second - mean, b = ring[(k + 1) % ring.size()].second - mean;
        if (a * b < 0) ++m.signChanges;
    }
    return m;
}

int main() {
    std::printf("[crochet relax] %s\n", FL_PATTERNS_DIR);
    const YarnBook book = loadYarnsFromFile(FL_YARNS_PATH);
    const Yarn* acrylic = book.find("acrylic_worsted");
    CHECK(acrylic != nullptr);
    if (acrylic == nullptr) FINISH();
    const Real hook = 5.0e-3;
    const StitchDims sc = stitchDims(*acrylic, hook, 1.0, StitchType::Sc, Insertion::Both, nullptr);

    // --- 6.10 test 1: a 20 x 20 swatch relaxed with zero g is 20 w x 20 h within 2 %
    {
        Prepared p = prepare(makeSwatch(20, 20), *acrylic, hook);
        RelaxOptions ro;
        const RelaxResult r = relaxModel(p.cm, ro);
        const std::vector<int> row10 = vertsOfRows(p, 10, 10);
        Real xmin = 1e300, xmax = -1e300;
        for (const int v : row10) {
            xmin = std::min(xmin, p.cm.mesh.x0[v].x);
            xmax = std::max(xmax, p.cm.mesh.x0[v].x);
        }
        // one vertex per stitch top: the extent of 20 tops is 19 stitch widths, the fabric is 20
        const Real width = (xmax - xmin) + sc.w;
        Real yTop = 0, yBottom = 0;
        for (const int v : vertsOfRows(p, 20, 20)) yTop += p.cm.mesh.x0[v].y / 20;
        for (const int v : vertsOfRows(p, 0, 0)) yBottom += p.cm.mesh.x0[v].y / 20;
        const Real height = yBottom - yTop;  // the work hangs: the foundation on top
        std::printf("   swatch relaxed in %d frames, KE %.1e J, %lld of %lld substeps at the Newton cap\n", r.frames, r.kineticEnergy, r.iterCapHits, r.substeps);
        CHECKP(r.converged && r.iterCapHits * 20 < r.substeps, "swatch: relaxation converged (%d frames), Newton cap in %lld of %lld substeps", r.frames, r.iterCapHits, r.substeps);
        CHECKP(std::fabs(width / (20 * sc.w) - 1) < 0.02 && std::fabs(height / (20 * sc.h) - 1) < 0.02,
               "swatch 20 x 20: width %.2f mm = 20 w (%.2f mm), height %.2f mm = 20 h (%.2f mm), within 2 %% (%.2f %% / %.2f %%)", width * 1e3, 20 * sc.w * 1e3,
               height * 1e3, 20 * sc.h * 1e3, 100 * (width / (20 * sc.w) - 1), 100 * (height / (20 * sc.h) - 1));
    }

    // --- 6.10 test 2: the circle, flat / cup / ruffle
    {
        struct Case {
            const char* name;
            int rounds;
        };
        for (const Case c : {Case{"circle_plus6", 8}, Case{"circle_plus5", 8}, Case{"circle_plus8", 8}}) {
            Prepared p = prepare(loadFixture(c.name), *acrylic, hook);
            RelaxOptions ro;
            ro.noise = 0.3e-3;  // symmetry breaking (plan 6.9 note in relax.hpp)
            const RelaxResult r = relaxModel(p.cm, ro);
            const std::string n = c.name;
            shot(c.name, p.cm.mesh.x0, p.cm.mesh.tris);
            const CircleMeasure flat = measureCircle(p, c.rounds, false), cup = measureCircle(p, c.rounds, true);
            std::printf("   %s: %d frames, KE %.1e J, Newton cap %lld/%lld; radius %.1f mm, deviation from the best plane %.1f %%, from the rim plane %.1f %% (%.0f %% on one side), "
                        "rim sign changes %d\n",
                        c.name, r.frames, r.kineticEnergy, r.iterCapHits, r.substeps, flat.radius * 1e3, 100 * flat.depth, 100 * cup.depth, 100 * cup.oneSided, flat.signChanges);
            CHECKP(r.converged && r.iterCapHits * 20 < r.substeps, "%s: relaxation converged (%d frames, KE %.1e J), Newton cap in %lld of %lld substeps", c.name, r.frames,
                   r.kineticEnergy, r.iterCapHits, r.substeps);
            if (n == "circle_plus6")
                CHECKP(flat.depth < 0.05, "circle +6 sc per round is flat: out-of-plane deviation %.2f %% of the radius (%.1f mm) < 5 %%", 100 * flat.depth, flat.radius * 1e3);
            if (n == "circle_plus5")
                CHECKP(cup.depth > 0.25 && cup.oneSided > 0.9, "circle +5 is a cup: %.1f %% of the radius deep, %.1f %% of the vertices (beyond 1 %% of the radius) on one side of the rim plane", 100 * cup.depth, 100 * cup.oneSided);
            if (n == "circle_plus8")
                CHECKP(flat.signChanges >= 4 && flat.depth > 0.25, "circle +8 ruffles: the rim height changes sign %d times, deviation %.0f %% of the radius", flat.signChanges, 100 * flat.depth);
        }
    }

    // --- 6.10 test 3: the tube keeps its circumference
    {
        Prepared p = prepare(loadFixture("tube40"), *acrylic, hook);
        RelaxOptions ro;
        const RelaxResult r = relaxModel(p.cm, ro);
        shot("tube40", p.cm.mesh.x0, p.cm.mesh.tris);
        CHECKP(r.converged && r.iterCapHits * 20 < r.substeps, "tube40: relaxation converged (%d frames), Newton cap in %lld of %lld substeps", r.frames, r.iterCapHits, r.substeps);
        for (const int round : {4, 8, 12}) {
            std::vector<int> ring;
            for (const Stitch& s : p.g.st)
                if (s.row == round && p.cm.topo.stitchVertex[s.id] >= 0) ring.push_back(p.cm.topo.stitchVertex[s.id]);
            Real perim = 0;
            for (size_t i = 0; i < ring.size(); ++i) perim += length(p.cm.mesh.x0[ring[(i + 1) % ring.size()]] - p.cm.mesh.x0[ring[i]]);
            CHECKP(ring.size() == 40 && std::fabs(perim / (40 * sc.w) - 1) < 0.03, "tube40 round %d: circumference %.1f mm = 40 w (%.1f mm) within 3 %% (%.2f %%)", round, perim * 1e3,
                   40 * sc.w * 1e3, 100 * (perim / (40 * sc.w) - 1));
        }
    }

    // --- 6.10 test 5: hinge sign. 10 sc x 20 rows, row 11 worked BLO on a RS row, the bottom 10 rows pinned flat
    for (const char* loop : {"blo", "flo"}) {
        Prepared p = prepare(makeSwatch(10, 20, 11, loop), *acrylic, hook);
        bool rsRow11 = false;
        for (const Stitch& st : p.g.st)
            if (st.row == 11 && st.type == "sc") rsRow11 = st.rs && st.loopRs == loop;
        CHECKP(rsRow11, "hinge swatch: row 11 is a RS row, %s as written and RS-relative", loop);
        RelaxOptions ro;
        for (const int v : vertsOfRows(p, 0, 10)) ro.pin.push_back(v);
        const RelaxResult r = relaxModel(p.cm, ro);
        shot(std::string("hinge_") + loop, p.cm.mesh.x0, p.cm.mesh.tris);
        // the free part (rows 11..20) rotates about the hinge line (row 10) by the rest angle
        Real y10 = 0, ysum = 0, zsum = 0;
        int n10 = 0, nTop = 0;
        for (const int v : vertsOfRows(p, 10, 10)) {
            y10 += p.cm.mesh.x0[v].y;
            ++n10;
        }
        y10 /= n10;
        for (const int v : vertsOfRows(p, 20, 20)) {
            ysum += p.cm.mesh.x0[v].y;
            zsum += p.cm.mesh.x0[v].z;
            ++nTop;
        }
        const Real dy = ysum / nTop - y10, dz = zsum / nTop;
        // RS is +z, so "toward the WS" is -z; the work hangs, the free part starts straight down (dy < 0)
        const Real towardWs = std::atan2(-dz, -dy) * 180 / kPi;
        const Real want = std::string(loop) == "blo" ? 25.0 : -25.0;
        CHECKP(r.converged && std::fabs(towardWs - want) < 3.0,
               "%s on a RS row: the top part turns %.1f deg toward the WS (want %+.0f +- 3), pinned rows 0-10, %d frames, Newton cap %lld/%lld", loop, towardWs, want, r.frames, r.iterCapHits,
               r.substeps);
    }

    // --- the pouch: seams close, the strap loops, nothing blows up (plan 6.9: record frames and the seam gap)
    {
        Prepared p = prepare(loadFixture("pouch"), *acrylic, hook);
        RelaxOptions ro;
        const RelaxResult r = relaxModel(p.cm, ro);
        shot("pouch", p.cm.mesh.x0, p.cm.mesh.tris);
        bool finite = true;
        for (const Vec3& x : p.cm.mesh.x0) finite = finite && std::isfinite(x.x) && std::isfinite(x.y) && std::isfinite(x.z);
        CHECKP(finite && r.converged && r.maxSeamGap < 1e-3 && r.iterCapHits * 20 < r.substeps,
               "pouch relaxed in %d frames (KE %.1e J), largest seam gap %.3f mm < 1 mm, Newton cap in %lld of %lld substeps", r.frames, r.kineticEnergy, r.maxSeamGap * 1e3,
               r.iterCapHits, r.substeps);
    }
    FINISH();
}
