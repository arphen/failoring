// Scenes of a relaxed crochet model (docs/CROCHET_PLAN.md section 7.2, ticket T6.1) and the plan's section 7.4
// test 3 for the pouch: relax, hang it by a pin and from a peg, settle under gravity. Reports Newton cap hits
// (plan 1 rule 6), self intersections and the clearance to the peg. Reads only the committed exports.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <memory>
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

    // --- 7.4 test 1: strap stretch, analytic. A 5 sc x 60 row strip, top held, a 1 kg weight on the bottom row
    {
        StitchGraph sg;
        auto addRow = [&](bool turned) {
            Row r;
            r.id = static_cast<int>(sg.rows.size());
            r.piece = "main";
            r.kind = "row";
            r.turned = turned;
            sg.rows.push_back(r);
            return r.id;
        };
        auto addSt = [&](const char* type, int row, std::vector<int> targets) {
            Stitch st;
            st.id = static_cast<int>(sg.st.size());
            st.type = type;
            st.piece = "main";
            st.row = row;
            for (const Stitch& q : sg.st) st.index += q.row == row;
            st.index += 1;
            st.loop = std::string(type) == "ch" ? "space" : "both";
            st.loopRs = st.loop;
            st.targets = std::move(targets);
            st.prev = sg.st.empty() ? -1 : static_cast<int>(sg.st.size()) - 1;
            sg.st.push_back(st);
            return st.id;
        };
        const int cols = 5, rows = 60;
        const int r0 = addRow(true);
        for (int i = 0; i <= cols; ++i) addSt("ch", r0, {});
        std::vector<int> prev;
        for (int c = cols - 1; c >= 0; --c) prev.push_back(c);
        for (int r = 1; r <= rows; ++r) {
            const int row = addRow(true);
            std::vector<int> cur;
            if (r > 1) addSt("ch", row, {});
            for (int c = 0; c < cols; ++c) cur.push_back(addSt("sc", row, {r == 1 ? prev[c] : prev[cols - 1 - c]}));
            prev = cur;
        }
        ModelOptions mo2;
        CrochetModel sm = buildCrochetModel(sg, *acrylic, mo2);
        layoutModel(sm, sg);
        // the weight: 1 kg shared by the 5 stitch tops of the bottom row (each vertex is a stitch cell)
        const Real boxKg = 1.0;
        int bottomRow = rows;
        std::vector<int> bottom, top;
        for (const Stitch& st : sg.st) {
            const int v = sm.topo.stitchVertex[st.id];
            if (v < 0) continue;
            if (st.row == bottomRow) bottom.push_back(v);
            if (st.row == 0) top.push_back(v);
        }
        for (const int v : bottom) sm.mesh.extraMass[v] += boxKg / bottom.size();
        SolverParams sp;
        sp.newtonIterations = 30;
        std::unique_ptr<ClothSolver> solver = makeSolver(sm, sp);
        for (const int v : top) solver->pinVertex(v);
        int calm = 0, frames = 0;
        while (frames < 2000 && calm < 30) {
            solver->step();
            ++frames;
            calm = solver->kineticEnergy() < 1e-10 ? calm + 1 : 0;
        }
        Real yTop = 0, yBottom = 0;
        for (const int v : top) yTop += solver->positions()[v].y / top.size();
        for (const int v : bottom) yBottom += solver->positions()[v].y / bottom.size();
        const Real simulated = (yTop - yBottom) - rows * sm.dims[sg.st[6].id].h;
        // analytic: row r carries the box plus the yarn below it; strain from the J-curve (plan 2.3)
        const StitchMech mech = stitchMech(*acrylic, mo2.hook_m, 1.0, StitchType::Sc, Insertion::Both, nullptr);
        // The mesh has one vertex per stitch top, so a strip of 5 stitches is 4 cells wide: the width that carries the load is
        // (cols - 1) w. (The half stitch at each free edge carries nothing in this model: the same load on the 5 w of the
        // fabric would give a 4 % smaller elongation in the stiff branch, up to 25 % in the soft one; see reports/T6.2.md.)
        const Real wStrip = (cols - 1) * sm.dims[sg.st[6].id].w, hRow = sm.dims[sg.st[6].id].h;
        Real rowMass = 0;  // yarn of one row: 5 sc and the turning chain
        for (const Stitch& st : sg.st)
            if (st.row == 2) rowMass += sm.dims[st.id].yarnLen * acrylic->lambda() * 1e-3;
        Real analytic = 0;
        for (int r = 1; r <= rows; ++r) {
            const Real below = (rows - r) * rowMass + 0.5 * rowMass;  // rows below this row plus half of its own
            const Real T = (boxKg + below) * 9.81 / wStrip;           // N/m
            const Real kLock = mech.kWale * mech.lockWale;
            const Real eps = T <= kLock ? T / mech.kWale : mech.lockWale + (T - kLock) / (mech.lockRatio * mech.kWale);
            analytic += eps * hRow;
        }
        std::printf("   strap: %d frames, KE %.1e J; T at the bottom %.1f N/m over the %.1f mm of mesh width (lock at %.1f N/m), strain there %.3f\n", frames, solver->kineticEnergy(), boxKg * 9.81 / wStrip, wStrip * 1e3,
                    mech.kWale * mech.lockWale, mech.lockWale + (boxKg * 9.81 / wStrip - mech.kWale * mech.lockWale) / (mech.lockRatio * mech.kWale));
        std::printf("   strap: simulated elongation %.3f mm, analytic %.3f mm (%.2f %%), calm %d\n", simulated * 1e3, analytic * 1e3, 100 * (simulated / analytic - 1), calm);
        CHECKP(calm >= 30 && std::fabs(simulated / analytic - 1) < 0.03, "strap 5 x 60 under 1 kg: elongation %.2f mm vs analytic %.2f mm within 3 %% (%.2f %%)", simulated * 1e3, analytic * 1e3,
               100 * (simulated / analytic - 1));
    }

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

    std::unique_ptr<CrochetScene> pinScene;  // the settled hang_pin pouch, kept for the load test
    for (const SceneKind kind : {SceneKind::HangPin, SceneKind::HangPeg}) {
        const bool pin = kind == SceneKind::HangPin;
        SceneOptions so;
        so.kind = kind;
        auto scenePtr = std::make_unique<CrochetScene>(cm, g, layout, so);
        CrochetScene& scene = *scenePtr;
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
        if (pin) pinScene = std::move(scenePtr);
    }

    // --- 7.4 test 2: force balance. A 0.3 kg phone-sized box in the pouch hung by the pin: the pins carry the weight of the
    // cloth plus the box (the pin reactions are the gradient of the potential energy of the constraints; the box rests on
    // the cloth through contact forces, which are external to it)
    {
        const LoadSpec phone = loadPreset("box:70x140x9_mm:0.3");
        const FitResult fit = fitCheck(cm, g, layout, phone);
        CHECKP(fit.known && fit.fits && fit.tight, "pouch + 70 x 140 x 9 mm phone: %s", fit.message.c_str());
        RigidLoad load(*pinScene, phone);
        const bool inserted = load.insert();
        CHECKP(inserted, "phone inserted in %d frames (%s)", load.stats().insertFrames, load.stats().stopReason.c_str());
        const bool released = load.release();
        const LoadStats& ls = load.stats();
        CHECKP(released && ls.netForceFraction < 0.02, "phone released: settled in %d frames, |F_net| = %.3f of its weight, speed %.2f mm/s", ls.releaseFrames, ls.netForceFraction, ls.speed * 1e3);
        const std::vector<Vec3> reactions = pinScene->pinReactions();
        Real sumY = 0;
        for (const Vec3& r : reactions) sumY += r.y;
        Real mCloth = 0;
        for (int v = 0; v < pinScene->solver().vertexCount(); ++v) mCloth += pinScene->solver().vertexMass(v);
        const Real expected = (mCloth + phone.mass) * 9.81;
        CHECKP(std::fabs(sumY / expected - 1) < 0.02, "force balance: pin reactions %.4f N = (cloth %.4f kg + box %.3f kg) g = %.4f N within 2 %% (%.2f %%)", sumY, mCloth, phone.mass, expected,
               100 * (sumY / expected - 1));
        const ClothSolver& sv = pinScene->solver();
        Real rim = 0;
        for (const int v : pinScene->mouth()) rim += sv.positions()[v].y / pinScene->mouth().size();
        const Real top = load.center().y + 0.5 * phone.height;
        CHECKP(std::fabs(load.center().x) < 0.02 && std::fabs(load.center().z) < 0.02 && top < rim + 0.01,
               "phone sits in the pouch: centre (%.1f, %.1f) mm off axis, its top %.1f mm below the rim", load.center().x * 1e3, load.center().z * 1e3, (rim - top) * 1e3);
        CHECKP(pinScene->solver().selfIntersections() == 0 && pinScene->solver().contactDiag().iterCapHits * 100 < pinScene->solver().contactDiag().substeps,
               "loaded pouch: %d self intersections, Newton cap in %lld of %lld substeps", pinScene->solver().selfIntersections(), pinScene->solver().contactDiag().iterCapHits,
               pinScene->solver().contactDiag().substeps);
        if (shotDir != nullptr) writeSnapshotPng(std::string(shotDir) + "/pouch_phone.png", pinScene->solver().positions(), cm.mesh.tris);
    }

    // --- 7.4 test 4: the fit check. The pouch cannot take a MacBook Air 13; the bag of section 11.2 can
    {
        const FitResult pouchFit = fitCheck(cm, g, layout, loadPreset("mba13"));
        CHECKP(pouchFit.known && !pouchFit.fits && pouchFit.shortfall > 0.15, "pouch + MBA13: fits = false, %s", pouchFit.message.c_str());
        StitchGraph bagG = loadFixture("bag_round_plain");
        ModelOptions bo;
        bo.hook_m = 4.5e-3;
        CrochetModel bagCm = buildCrochetModel(bagG, *book.find("cotton_worsted"), bo);
        const LayoutReport bagLayout = layoutModel(bagCm, bagG);
        const FitResult bagFit = fitCheck(bagCm, bagG, bagLayout, loadPreset("mba13"));
        CHECKP(bagFit.known && bagFit.fits && !bagFit.tight, "bag + MBA13: fits = true, %s", bagFit.message.c_str());
        const FitResult mbp16 = fitCheck(bagCm, bagG, bagLayout, loadPreset("mbp16"));
        std::printf("   bag + MBP16: %s\n", mbp16.message.c_str());
        // rest inside of the bag: half the 114-stitch circumference, and 41 wall rounds + the base
        const StitchDims scd = bagCm.dims[bagG.st[60].id];
        CHECKP(std::fabs(bagFit.innerWidth / (57 * scd.w) - 1) < 1e-6 && bagFit.innerDepth > 0.20 && bagFit.innerDepth < 0.27,
               "bag inside: %.1f cm wide (57 stitch widths), %.1f cm deep", bagFit.innerWidth * 100, bagFit.innerDepth * 100);
        bool threw = false;
        try { loadPreset("laptop"); } catch (const std::invalid_argument&) { threw = true; }
        bool threw2 = false;
        try { loadPreset("box:12x34_mm:1"); } catch (const std::invalid_argument&) { threw2 = true; }
        const LoadSpec custom = loadPreset("box:300x200x10_mm:1.5");
        CHECKP(threw && threw2 && std::fabs(custom.width - 0.3) < 1e-12 && std::fabs(custom.mass - 1.5) < 1e-12 && custom.cornerRadius <= 0.5 * custom.thickness,
               "load presets: unknown names and malformed boxes throw, box:300x200x10_mm:1.5 parses");
    }

    FINISH();
}
