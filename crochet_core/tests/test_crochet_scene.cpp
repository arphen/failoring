// Scenes of a relaxed crochet model (docs/CROCHET_PLAN.md section 7.2, ticket T6.1) and the plan's section 7.4
// test 3 for the pouch: relax, hang it by a pin and from a peg, settle under gravity. Reports Newton cap hits
// (plan 1 rule 6), self intersections and the clearance to the peg. Reads only the committed exports.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "check.hpp"
#include "failoring/crochet/pattern.hpp"
#include "failoring/crochet/scene.hpp"
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

struct Shape {
    Real lowest = 0, highest = 0;
    Vec3 centroid;
};

static Shape shapeOf(const std::vector<Vec3>& x) {
    Shape s;
    s.lowest = 1e300;
    s.highest = -1e300;
    for (const Vec3& p : x) {
        s.lowest = std::min(s.lowest, p.y);
        s.highest = std::max(s.highest, p.y);
        s.centroid += p;
    }
    s.centroid = s.centroid / static_cast<Real>(x.size());
    return s;
}

int main() {
    std::printf("[crochet scenes] %s\n", FL_PATTERNS_DIR);
    const YarnBook book = loadYarnsFromFile(FL_YARNS_PATH);
    const Yarn* acrylic = book.find("acrylic_worsted");
    CHECK(acrylic != nullptr);
    if (acrylic == nullptr) FINISH();
    const char* shotDir = std::getenv("FL_SCENE_SHOTS");

    // the relaxed pouch, shared by the scenes
    StitchGraph g = loadFixture("pouch");
    ModelOptions mo;
    mo.hook_m = 5e-3;
    CrochetModel cm = buildCrochetModel(g, *acrylic, mo);
    const LayoutReport layout = layoutModel(cm, g);
    const RelaxResult relax = relaxModel(cm, RelaxOptions());
    CHECKP(relax.converged && relax.maxSeamGap < 1e-3, "pouch relaxed: %d frames, seam gap %.3f mm", relax.frames, relax.maxSeamGap * 1e3);

    // orientation: whatever way the relaxed object points, the scene hangs it by the strap, body below
    {
        CrochetModel tilted = cm;
        const Real a = 0.9, b = -0.6;  // rotate about z, then about x
        for (Vec3& p : tilted.mesh.x0) {
            const Vec3 q(std::cos(a) * p.x - std::sin(a) * p.y, std::sin(a) * p.x + std::cos(a) * p.y, p.z);
            p = Vec3(q.x, std::cos(b) * q.y - std::sin(b) * q.z, std::sin(b) * q.y + std::cos(b) * q.z);
        }
        CrochetScene scene(tilted, g, layout, SceneOptions());
        const std::vector<Vec3>& x = scene.solver().positions();
        Vec3 pin(0, 0, 0), body(0, 0, 0);
        int nb = 0;
        std::vector<char> isPin(x.size(), 0);
        for (const int v : scene.pinned()) {
            isPin[v] = 1;
            pin += x[v];
        }
        pin = pin / static_cast<Real>(scene.pinned().size());
        for (size_t v = 0; v < x.size(); ++v)
            if (!isPin[v] && g.st[tilted.topo.vertexStitch[v]].row <= 51) {
                body += x[v];
                ++nb;
            }
        body = body / nb;
        CHECKP(!scene.pinned().empty() && std::fabs(pin.x) < 1e-9 && std::fabs(pin.z) < 1e-9 && body.y < pin.y - 0.10,
               "tilted model: hung by the strap (pins at x = %.1e, z = %.1e), the body %.1f cm below the pins", pin.x, pin.z, (pin.y - body.y) * 100);
    }

    for (const SceneKind kind : {SceneKind::HangPin, SceneKind::HangPeg}) {
        const bool pin = kind == SceneKind::HangPin;
        SceneOptions so;
        so.kind = kind;
        CrochetScene scene(cm, g, layout, so);
        const std::vector<Vec3> start = scene.solver().positions();
        const SceneStats st = scene.settle();
        const Shape sh = shapeOf(scene.solver().positions());
        std::printf("   %s: %d frames, KE %.1e J, Newton %lld iterations in %lld substeps (%lld at the cap), self intersections %d, min clearance %.2f mm; lowest point %.1f cm, centroid (%.1f, %.1f, %.1f) cm\n",
                    pin ? "hang_pin" : "hang_peg", st.frames, st.kineticEnergy, st.newtonIterations, st.substeps, st.iterCapHits, st.selfIntersections, st.minClearance * 1e3, sh.lowest * 100,
                    sh.centroid.x * 100, sh.centroid.y * 100, sh.centroid.z * 100);
        const char* name = pin ? "hang_pin" : "hang_peg";
        // section 7.4 test 3: settles within the cap, Newton cap hits < 1 %, no self intersections, clearance > -0.5 mm
        CHECKP(st.settled && st.frames <= so.maxFrames, "pouch %s: settled in %d frames (cap %d), KE %.1e J", name, st.frames, so.maxFrames, st.kineticEnergy);
        CHECKP(st.iterCapHits * 100 < st.substeps, "pouch %s: Newton cap in %lld of %lld substeps (< 1 %%)", name, st.iterCapHits, st.substeps);
        CHECKP(st.selfIntersections == 0, "pouch %s: %d self intersections", name, st.selfIntersections);
        if (!pin) CHECKP(st.minClearance > -0.5e-3, "pouch hang_peg: clearance to the peg %.2f mm > -0.5 mm", st.minClearance * 1e3);
        // it hangs: the lowest point is far below the hang point and the strap is taut
        // the strap's apex is the highest point; a 48 cm strap hung by its middle drops 24 cm to the mouth and the body adds 15 cm
        CHECKP(sh.highest - sh.lowest > 0.33 && sh.highest - sh.lowest < 0.45, "pouch %s: hangs %.1f cm from its highest point to its lowest (strap 24 cm + body 15 cm)", name,
               (sh.highest - sh.lowest) * 100);
        if (pin) {
            Real moved = 0;
            for (const int v : scene.pinned()) moved = std::max(moved, length(scene.solver().positions()[v] - start[v]));
            CHECKP(moved < 1e-9, "pouch hang_pin: the pinned rows did not move (%.1e m)", moved);
        }
        if (shotDir != nullptr) writeSnapshotPng(std::string(shotDir) + "/pouch_" + name + ".png", scene.solver().positions(), cm.mesh.tris);
    }
    FINISH();
}
