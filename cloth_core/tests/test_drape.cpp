// Cusick-style drape test: a 30 cm disc of fabric falls onto an 18 cm pedestal.
// The Drape Coefficient (percentage of the overhanging annulus that is NOT in
// shadow when lit from above) is a standard textile measure: ~30-50% for fluid
// silks/rayons, ~50-70% for cotton/wool, 70-100% for stiff cloth.
//
// We can't validate absolute numbers without lab data for the exact cloth, but we
// can check the physics is monotonic in the right direction and finish stable.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "check.hpp"
#include "failoring/cloth.hpp"
#include "failoring/collider.hpp"
#include "failoring/material.hpp"
#include "failoring/mesh_sizing.hpp"
#include "failoring/mesher.hpp"
#include "failoring/metrics.hpp"

using namespace fl;

struct Drape { Real dc; Real ke; Real clearance; int frames; Real seconds; Real minY; };

static Drape runDrape(const FabricMaterial& mat, Real h, const std::string& objPath) {
    const Real R = 0.15, Rs = 0.09;
    PanelMesh disc = meshDisc(R, h);
    SolverParams sp;
    sp.substeps = 1;
    ClothSolver cs(sp);
    PanelPlacement pl;
    pl.origin = Vec3(0, 0.012, 0);
    pl.ex = Vec3(1, 0, 0);
    pl.ey = Vec3(0, 0, 1);  // disc lies in the x-z plane, 12 mm above the pedestal top
    cs.addPanel(disc, mat, pl, 0.0);
    auto ped = std::make_shared<CylinderCollider>(Vec3(0, -0.10, 0), Rs, 0.10);  // top face at y = 0
    cs.setCollider(ped);

    auto t0 = std::chrono::steady_clock::now();
    int f = 0;
    for (; f < 300; ++f) {
        cs.step();
        if (f > 100 && cs.kineticEnergy() < 1e-8) break;
    }
    Drape d{};
    d.frames = f;
    d.ke = cs.kineticEnergy();
    d.clearance = cs.minBodyClearance();
    d.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    d.minY = 1e9;
    for (auto& p : cs.positions()) d.minY = std::min(d.minY, p.y);
    const Real shadow = projectedShadowArea(cs.positions(), cs.triangles(), 0.2, 1000);
    const Real aDisc = kPi * R * R, aSupport = kPi * Rs * Rs;
    d.dc = 100.0 * (shadow - aSupport) / (aDisc - aSupport);
    if (!objPath.empty()) writeObj(objPath, cs.positions(), cs.triangles());
    return d;
}

int main() {
    auto mats = loadMaterialsFromFile(FL_MATERIALS_PATH);
    std::map<std::string, Drape> res;
    std::printf("%-18s %7s %8s %8s %9s %8s %7s\n", "fabric", "h mm", "DC %", "frames", "KE (J)", "clear mm", "sec");
    for (const auto& m : mats) {
        // Mesh at the physics-based recommendation (Zhang et al. 2025), floored at 10 mm to bound CPU time here.
        const Real h = std::max(optimalResolution(m).isotropic(), Real(0.010));
        Drape d = runDrape(m, h, std::string(FL_OUT_DIR) + "/drape_" + m.id + ".obj");
        res[m.id] = d;
        std::printf("%-18s %7.1f %8.1f %8d %9.1e %8.2f %7.1f\n", m.id.c_str(), 1000 * h, d.dc, d.frames, d.ke, d.clearance * 1000, d.seconds);
        CHECK_MSG(std::isfinite(d.dc), "%s: non-finite result", m.id.c_str());
        CHECK_MSG(d.dc > 15 && d.dc < 100.5, "%s: DC %.1f outside plausible range", m.id.c_str(), d.dc);
        CHECK_MSG(d.ke < 1e-4, "%s: did not settle (KE %.2e)", m.id.c_str(), d.ke);
        CHECK_MSG(d.clearance > -0.001, "%s: penetrated pedestal by %.2f mm", m.id.c_str(), -d.clearance * 1000);
        CHECK_MSG(d.minY > -0.6, "%s: cloth fell off (min y %.2f)", m.id.c_str(), d.minY);
    }
    // Ordering by bending length c = (B/(rho g))^(1/3): fluid < crisp < stiff.
    auto dc = [&](const char* id) { return res.at(id).dc; };
    CHECK_MSG(dc("silk_charmeuse") < dc("cotton_poplin"), "silk %.1f !< poplin %.1f", dc("silk_charmeuse"), dc("cotton_poplin"));
    CHECK_MSG(dc("polyester_chiffon") < dc("cotton_poplin"), "chiffon %.1f !< poplin %.1f", dc("polyester_chiffon"), dc("cotton_poplin"));
    CHECK_MSG(dc("cotton_poplin") < dc("denim_12oz"), "poplin %.1f !< denim %.1f", dc("cotton_poplin"), dc("denim_12oz"));
    CHECK_MSG(dc("denim_12oz") < dc("canvas_heavy"), "denim %.1f !< canvas %.1f", dc("denim_12oz"), dc("canvas_heavy"));
    CHECK_MSG(dc("canvas_heavy") <= dc("lambskin_leather") + 5, "canvas %.1f vs leather %.1f", dc("canvas_heavy"), dc("lambskin_leather"));
    FINISH();
}
