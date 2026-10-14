// K1 + K5 together (opus session 1, phase 0). Both options were merged into main separately and never run together.
// Scene: the test_garment tube (two cotton panels sewn into a ring, pre-wrapped on the narrow upper dome of the
// ellipsoid mannequin) on a coarser 3 cm mesh so it stays quick. Four runs:
//   rigid      : both options off (the default solver)
//   skin       : K1 compliant skin only
//   barrier    : K5 barrier self contact only
//   skin+barr. : both
// Reported per run: frames, kinetic energy, line-search failures, Newton iterations per substep, factorisations,
// body clearance (signed distance of the cloth to the offset surface), frames with exact self intersections, seam gap,
// hem height. Pass limits are the ones test_garment uses (KE < 5e-4 J, seam gap < 4 mm, penetration < 1 mm); in the
// skin modes the cloth may sink into the soft layer, so the penetration limit is skinMaxDepth + 1 mm. Barrier modes
// must never self-intersect.
// Usage: test_skin_barrier [frames] [mode]   mode = rigid | skin | barrier | both (default: all four)
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "check.hpp"
#include "failoring/cloth.hpp"
#include "failoring/collider.hpp"
#include "failoring/material.hpp"
#include "failoring/mesher.hpp"

using namespace fl;

namespace {

const Real kSkinK = 2.0e5;     // Pa/m, the value test_soft_skin's standing part uses (a test value, not tissue data)
const Real kSkinDepth = 0.008; // m

struct Run {
    std::string name;
    int frames = 0;
    double secs = 0;
    Real ke = 0, minClr = 1e9, endClr = 0, gap = 0, hemY = 0;
    int lsFail = 0, framesIntersecting = 0, maxIntersecting = 0;
    long long newton = 0, substeps = 0, factor = 0;
    bool finite = true;
    Real minDist = 0;
};

Run runScene(const std::string& name, bool skin, bool barrier, int frames) {
    auto mats = loadMaterialsFromFile(FL_MATERIALS_PATH);
    const FabricMaterial* fab = findMaterial(mats, "cotton_poplin");
    CHECK(fab != nullptr);
    auto body = std::make_shared<EllipsoidCollider>(Vec3(0, 0, 0), Vec3(0.17, 0.35, 0.11));

    SolverParams sp;
    sp.substeps = 4;
    sp.seamRampFrames = 0;
    if (skin) { sp.skinStiffness = kSkinK; sp.skinMaxDepth = kSkinDepth; }
    if (barrier) { sp.barrierContact = true; sp.selfCollision = false; }  // as test_garment's FL_BARRIER switch
    if (const char* e = std::getenv("FL_TENSION")) { sp.hessianAuto = false; sp.tensionStiffness = std::atoi(e) != 0; }  // K2: FL_TENSION=0 Gauss-Newton, =1 tension term (default: automatic)
    ClothSolver cs(sp);
    cs.setCollider(body);

    const Real W = 0.40, H = 0.30, h = 0.03, y0 = 0.25;
    PanelMesh mesh = meshRect(W, H, h);
    PanelPlacement a, b;
    a.origin = Vec3(-W / 2, y0, 0.145); a.ex = Vec3(1, 0, 0);  a.ey = Vec3(0, 1, 0);
    b.origin = Vec3(+W / 2, y0, -0.145); b.ex = Vec3(-1, 0, 0); b.ey = Vec3(0, 1, 0);
    const int pf = cs.addPanel(mesh, *fab, a, kPi / 2, "front"), pb = cs.addPanel(mesh, *fab, b, kPi / 2, "back");
    cs.addSeam(pf, 1, pb, 3, true);
    cs.addSeam(pf, 3, pb, 1, true);
    const Real Rc = W / kPi;
    for (int p : {pf, pb})
        for (size_t i = 0; i < mesh.verts.size(); ++i) {
            const Real th = kPi * mesh.verts[i].x / W, y = y0 + mesh.verts[i].y;
            cs.setVertexPosition(cs.firstVertex(p) + static_cast<int>(i),
                                 (p == pf) ? Vec3(-Rc * std::cos(th), y, Rc * std::sin(th)) : Vec3(Rc * std::cos(th), y, -Rc * std::sin(th)));
        }

    Run r;
    r.name = name;
    auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < frames; ++f) {
        cs.step();
        ++r.frames;
        const int n = cs.selfIntersections();
        if (n > 0) ++r.framesIntersecting;
        r.maxIntersecting = std::max(r.maxIntersecting, n);
        r.minClr = std::min(r.minClr, cs.minBodyClearance());
        for (const auto& p : cs.positions()) r.finite = r.finite && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        if (!r.finite) break;
    }
    r.secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    r.ke = cs.kineticEnergy();
    r.endClr = cs.minBodyClearance();
    r.gap = cs.maxSeamGap();
    r.lsFail = cs.lineSearchFailures();
    r.newton = cs.newtonIterations();
    r.substeps = cs.substepsDone();
    r.factor = cs.factorizations();
    r.minDist = cs.minSelfDistance(0.01);
    Real hy = 0; int c = 0;
    for (int p : {pf, pb}) for (int v : cs.edgeChain(p, 0)) { hy += cs.positions()[v].y; ++c; }
    r.hemY = hy / c;
    std::printf("  %-11s %d verts, %d frames, %.1f s | KE %.2e J | ls-fail %d | Newton/substep %.2f | factorisations %lld\n"
                "              clearance min %.2f mm (end %.2f) | self-intersecting frames %d (max pairs %d) | seam gap %.2f mm"
                " | hem %.1f mm | finite %s\n",
                name.c_str(), cs.vertexCount(), r.frames, r.secs, r.ke, r.lsFail,
                r.substeps ? double(r.newton) / r.substeps : 0.0, r.factor, r.minClr * 1e3, r.endClr * 1e3,
                r.framesIntersecting, r.maxIntersecting, r.gap * 1e3, r.hemY * 1e3, r.finite ? "yes" : "NO");
    std::fflush(stdout);

    CHECK_MSG(r.finite, "%s: NaN/inf positions", name.c_str());
    CHECK_MSG(r.ke < 5e-4, "%s: not settled, KE %.2e", name.c_str(), r.ke);
    CHECK_MSG(r.gap < 0.004, "%s: seams not closed: %.2f mm", name.c_str(), r.gap * 1e3);
    const Real allowed = (skin ? kSkinDepth : 0) + 0.001;
    CHECK_MSG(r.minClr > -allowed, "%s: penetrates by %.2f mm (allowed %.2f)", name.c_str(), -r.minClr * 1e3, allowed * 1e3);
    if (barrier) CHECK_MSG(r.framesIntersecting == 0, "%s: %d frames self-intersect", name.c_str(), r.framesIntersecting);
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    const int frames = argc > 1 ? std::atoi(argv[1]) : 200;
    const std::string only = argc > 2 ? argv[2] : "";
    std::printf("[K1 skin + K5 barrier: tube on the ellipsoid, 3 cm mesh, %d frames, skin k %.0e Pa/m, depth %.0f mm]\n",
                frames, kSkinK, kSkinDepth * 1e3);
    if (only.empty() || only == "rigid") runScene("rigid", false, false, frames);
    if (only.empty() || only == "skin") runScene("skin", true, false, frames);
    if (only.empty() || only == "barrier") runScene("barrier", false, true, frames);
    if (only.empty() || only == "both") runScene("skin+barr.", true, true, frames);
    FINISH();
}
