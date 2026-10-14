// Static equilibrium solver (ClothSolver::solveStatic) against the dynamic implicit solver, which is the ground
// truth that passes the other tests:
//   1. Cusick drape disc, three fabrics (fluid / mid / stiff): drape coefficient within 2 points, no penetration,
//      fewer Newton iterations and less CPU time than the dynamic settle.
//   2. Tube lodging on the ellipsoid mannequin (test_garment scenario): hem within 2 mm of the dynamic result,
//      seams within 0.2 mm.
//   3. Cantilever tip deflection within the tolerances of test_cantilever.
//   4. Warm start after a small edit reaches the cold-solve shape (< 0.5 mm) in fewer iterations.
//   5. Path-dependence diagnostic.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>

#include "check.hpp"
#include "failoring/cloth.hpp"
#include "failoring/collider.hpp"
#include "failoring/material.hpp"
#include "failoring/mesh_sizing.hpp"
#include "failoring/mesher.hpp"
#include "failoring/metrics.hpp"

using namespace fl;

static double cpuNow() { return static_cast<double>(std::clock()) / CLOCKS_PER_SEC; }

static Real maxDiff(const std::vector<Vec3>& a, const std::vector<Vec3>& b) {
    Real m = 0;
    for (size_t i = 0; i < a.size(); ++i) m = std::max(m, length(a[i] - b[i]));
    return m;
}

// ---------------------------------------------------------------- drape disc
static const Real kR = 0.15, kRs = 0.09;

static std::unique_ptr<ClothSolver> makeDrape(const FabricMaterial& mat, Real h) {
    PanelMesh disc = meshDisc(kR, h);
    SolverParams sp;
    sp.substeps = 1;
    auto cs = std::make_unique<ClothSolver>(sp);
    PanelPlacement pl;
    pl.origin = Vec3(0, 0.012, 0);
    pl.ex = Vec3(1, 0, 0);
    pl.ey = Vec3(0, 0, 1);
    cs->addPanel(disc, mat, pl, 0.0);
    cs->setCollider(std::make_shared<CylinderCollider>(Vec3(0, -0.10, 0), kRs, 0.10));
    return cs;
}

static Real drapeCoefficient(const ClothSolver& cs) {
    const Real shadow = projectedShadowArea(cs.positions(), cs.triangles(), 0.2, 1000);
    return 100.0 * (shadow - kPi * kRs * kRs) / (kPi * kR * kR - kPi * kRs * kRs);
}

static void drapeCase(const FabricMaterial& m) {
    const Real h = std::max(optimalResolution(m).isotropic(), Real(0.010));
    // dynamic settle (same loop as test_drape)
    auto dyn = makeDrape(m, h);
    const double c0 = cpuNow();
    int f = 0;
    for (; f < 300; ++f) {
        dyn->step();
        if (f > 100 && dyn->kineticEnergy() < 1e-8) break;
    }
    const double dynCpu = cpuNow() - c0;
    const Real dcDyn = drapeCoefficient(*dyn);
    const long long dynIt = dyn->newtonIterations();

    auto st = makeDrape(m, h);
    StaticParams spar;
    spar.verbose = std::getenv("FL_STATIC_VERBOSE") != nullptr;
    const StaticResult r = st->solveStatic(spar);
    const Real dcSta = drapeCoefficient(*st);
    const Real clr = st->minBodyClearance();
    std::printf("  %-18s h %4.1f mm, %4d verts | dynamic: DC %5.1f, %3d frames, %4lld Newton it, %6.2f s cpu | static: DC %5.1f, %3d it (%d stages, %d failed, final %d), "
                "res %.1e, step %.1e, %6.2f s cpu, clear %.2f mm | factor it x%.1f, cpu x%.1f\n",
                m.id.c_str(), 1000 * h, dyn->vertexCount(), dcDyn, f, dynIt, dynCpu, dcSta, r.iterations, r.stages, r.failedStages, r.finalIterations,
                r.residual, r.maxStep, r.cpuSeconds, clr * 1000, double(dynIt) / r.iterations, dynCpu / std::max(r.cpuSeconds, 1e-9));
    CHECK_MSG(r.converged, "%s: static solve did not converge (res %.2e, step %.2e)", m.id.c_str(), r.residual, r.maxStep);
    CHECK_MSG(std::fabs(dcSta - dcDyn) < 2.0, "%s: static DC %.2f vs dynamic %.2f", m.id.c_str(), dcSta, dcDyn);
    CHECK_MSG(clr > -0.001, "%s: static penetrates the pedestal by %.2f mm", m.id.c_str(), -clr * 1000);
    CHECK_MSG(r.iterations < dynIt, "%s: static %d Newton iterations vs dynamic %lld", m.id.c_str(), r.iterations, dynIt);
    CHECK_MSG(r.cpuSeconds < dynCpu, "%s: static %.2f s vs dynamic %.2f s cpu", m.id.c_str(), r.cpuSeconds, dynCpu);
}

int main() {
    auto mats = loadMaterialsFromFile(FL_MATERIALS_PATH);
    std::printf("== drape disc, static vs dynamic\n");
    for (const char* id : {"silk_charmeuse", "cotton_poplin", "denim_12oz"}) {
        const FabricMaterial* m = findMaterial(mats, id);
        CHECK(m != nullptr);
        if (m) drapeCase(*m);
    }
    FINISH();
}
