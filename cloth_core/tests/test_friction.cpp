// Body friction (opus session 1): a cloth patch resting on an inclined plane must stick when tan(theta) < mu and slide
// with acceleration a = g (sin theta - mu cos theta) when tan(theta) > mu (Coulomb). The material's velocity damping c
// (dv/dt = a - c v, from FabricMaterial::damping) is part of the prediction:
//     x(t) = a/c * (t - (1 - exp(-c t)) / c)
// Modes: bodyFrictionMode 0 (old: position correction after the solve), 2 (smoothed Coulomb potential inside Newton).
// Usage: test_friction [mode]   no argument: both modes, checks on mode 2 (the old mode is reported only).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "check.hpp"
#include "failoring/cloth.hpp"
#include "failoring/collider.hpp"
#include "failoring/material.hpp"
#include "failoring/mesher.hpp"

using namespace fl;

namespace {

struct Slide { Real dist = 0, predicted = 0, frictionless = 0, ke = 0; int lsFail = 0; long long cap = 0; Real clr = 0; Real lamRatio = 0; };

Slide run(Real thetaDeg, int mode, const FabricMaterial& fab, Real T) {
    const Real th = thetaDeg * kPi / 180;
    const Vec3 n(std::sin(th), std::cos(th), 0);   // plane normal
    const Vec3 t(std::cos(th), -std::sin(th), 0);  // downhill direction in the plane
    SolverParams sp;
    sp.substeps = 4;
    sp.bodyFrictionMode = mode == 3 ? 2 : mode;
    sp.contactAL = mode == 3;  // mode 3: K2 contact multipliers with friction mode 2
    sp.contactDiagnostics = true;
    ClothSolver cs(sp);
    cs.setCollider(std::make_shared<PlaneCollider>(Vec3(0, 0, 0), n));
    const Real L = 0.10, h = 0.01;
    const Real lift = sp.contactOffset + 0.5 * fab.thickness + 1e-5;  // resting on the plane (contact radius + 10 um)
    PanelPlacement pl;
    pl.origin = t * (-L / 2) + Vec3(0, 0, -L / 2) + n * lift;
    pl.ex = t;
    pl.ey = Vec3(0, 0, 1);
    cs.addPanel(meshRect(L, L, h), fab, pl, 0.0);
    std::vector<Vec3> x0 = cs.positions();  // at rest on the plane at t = 0
    const int frames = static_cast<int>(std::lround(T / sp.dt));
    for (int f = 0; f < frames; ++f) cs.step();
    Slide r;
    for (int i = 0; i < cs.vertexCount(); ++i) r.dist += dot(cs.positions()[i] - x0[i], t) / cs.vertexCount();
    const Real c = fab.damping, tt = frames * sp.dt;
    auto travel = [&](Real a) { return a <= 0 ? 0.0 : a / c * (tt - (1 - std::exp(-c * tt)) / c); };
    const Real mu = fab.frictionKinetic, g = 9.81;
    r.predicted = std::tan(th) <= mu ? 0.0 : travel(g * (std::sin(th) - mu * std::cos(th)));
    r.frictionless = travel(g * std::sin(th));
    r.ke = cs.kineticEnergy();
    r.lsFail = cs.lineSearchFailures();
    r.cap = cs.contactDiag().iterCapHits;
    r.clr = cs.minBodyClearance();
    if (sp.contactAL) {  // the multipliers are the normal forces: their sum must carry the weight's normal component
        Real lam = 0;
        for (Real l : cs.contactMultipliers()) lam += l;
        r.lamRatio = lam / (cs.totalMass() * 9.81 * std::cos(th));
    }
    std::printf("  mode %d, %4.1f deg (tan %.3f vs mu %.2f): slid %8.2f mm | Coulomb prediction %8.2f mm | frictionless %8.2f mm | "
                "ls-fail %d, cap hits %lld, clearance %.2f mm\n",
                mode, thetaDeg, std::tan(th), mu, r.dist * 1e3, r.predicted * 1e3, r.frictionless * 1e3, r.lsFail, r.cap, r.clr * 1e3);
    if (sp.contactAL) std::printf("           contact multipliers: sum / (m g cos theta) = %.5f\n", r.lamRatio);
    std::fflush(stdout);
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    auto mats = loadMaterialsFromFile(FL_MATERIALS_PATH);
    const FabricMaterial* fab = findMaterial(mats, "cotton_poplin");
    CHECK(fab != nullptr);
    if (!fab) FINISH();
    const Real T = 0.5;
    std::printf("[cotton patch on an inclined plane, %.1f s; mu_kinetic %.2f, damping %.2f 1/s]\n", T, fab->frictionKinetic, fab->damping);
    if (argc > 1) {
        const int mode = std::atoi(argv[1]);
        for (Real deg : {10.0, 15.0, 25.0, 30.0, 40.0}) run(deg, mode, *fab, T);
        FINISH();
    }
    for (Real deg : {15.0, 30.0}) run(deg, 0, *fab, T);  // old mode: reported only
    const Slide stick = run(15.0, 2, *fab, T);
    const Slide slide = run(30.0, 2, *fab, T);
    CHECK_MSG(std::fabs(stick.dist) < 0.001, "15 deg: slid %.2f mm, must stick (tan 0.268 < mu)", stick.dist * 1e3);
    CHECK_MSG(std::fabs(slide.dist - slide.predicted) < 0.05 * slide.predicted, "30 deg: slid %.1f mm vs Coulomb %.1f mm (> 5 %%)",
              slide.dist * 1e3, slide.predicted * 1e3);
    CHECK_MSG(stick.clr > -0.001 && slide.clr > -0.001, "penetrates the plane");
    CHECK(stick.lsFail == 0 && slide.lsFail == 0);
    // mode 3: the same friction with K2 contact multipliers as the normal force; the multipliers equal the normal load
    const Slide stickAL = run(15.0, 3, *fab, T);
    const Slide slideAL = run(30.0, 3, *fab, T);
    CHECK_MSG(std::fabs(stickAL.dist) < 0.001, "AL 15 deg: slid %.2f mm, must stick", stickAL.dist * 1e3);
    CHECK_MSG(std::fabs(slideAL.dist - slideAL.predicted) < 0.05 * slideAL.predicted, "AL 30 deg: slid %.1f mm vs Coulomb %.1f mm",
              slideAL.dist * 1e3, slideAL.predicted * 1e3);
    CHECK_MSG(std::fabs(stickAL.lamRatio - 1) < 0.01 && std::fabs(slideAL.lamRatio - 1) < 0.01,
              "multipliers vs normal load: %.4f (15 deg), %.4f (30 deg)", stickAL.lamRatio, slideAL.lamRatio);
    CHECK_MSG(stickAL.clr > -0.001 && slideAL.clr > -0.001, "AL: penetrates the plane");
    CHECK(stickAL.lsFail == 0 && slideAL.lsFail == 0);
    FINISH();
}
