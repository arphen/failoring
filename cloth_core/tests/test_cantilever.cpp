// Calibration test: does the bending model reproduce Euler-Bernoulli beam theory?
// A strip clamped at one end sags under its own weight:
//     tip deflection  delta = rho * g * L^4 / (8 * B)     (B in N*m, rho in kg/m^2)
// This is the physics of the textile cantilever test (ASTM D1388 / Peirce), so the
// material 'bending_*_Nm' numbers mean what textile labs mean by them.
#include <cmath>
#include <cstdio>

#include "check.hpp"
#include "failoring/cloth.hpp"
#include "failoring/mesher.hpp"

using namespace fl;

struct Result { Real tipDrop, analytic; int frames; int lsFail; };

static Result runCantilever(Real L, Real W, Real h, Real Bwarp, Real Bweft, Real rho, Real grain, bool useRect) {
    FabricMaterial m;
    m.density = rho;
    m.tensileWarp = m.tensileWeft = 5.0e4;
    m.shear = 500;
    m.bendWarp = Bwarp;
    m.bendWeft = Bweft;
    m.compressionRatio = 1.0;
    m.damping = 3.0;

    PanelMesh mesh = useRect ? meshRect(L, W, h) : meshPolygon({{0, 0}, {L, 0}, {L, W}, {0, W}}, h);
    SolverParams sp;
    sp.substeps = 1;
    sp.selfCollision = false;
    ClothSolver cs(sp);
    PanelPlacement pl;
    pl.ex = Vec3(1, 0, 0);
    pl.ey = Vec3(0, 0, 1);  // strip lies flat in the x-z plane, length along +x
    cs.addPanel(mesh, m, pl, grain);
    // Clamp: fix the first two columns (a single pinned column would be a hinge, not a clamp);
    // the effective beam starts at the last pinned column.
    Real xClamp = 0;
    const Real pinLimit = useRect ? 1.05 * h : 0.8 * h;
    for (int v = 0; v < cs.vertexCount(); ++v)
        if (mesh.verts[v].x < pinLimit) { cs.pinVertex(v); xClamp = std::max(xClamp, mesh.verts[v].x); }
    const Real Leff = L - xClamp;
    Result r{};
    r.analytic = rho * 9.81 * Leff * Leff * Leff * Leff / (8 * Bwarp);
    int f = 0;
    for (; f < 600; ++f) {
        cs.step();
        if (f > 60 && cs.kineticEnergy() < 1e-16) break;
    }
    r.frames = f;
    r.lsFail = cs.lineSearchFailures();
    Real tip = 0;
    int cnt = 0;
    for (int v = 0; v < cs.vertexCount(); ++v)
        if (std::fabs(mesh.verts[v].x - L) < 1e-9) { tip += cs.positions()[v].y; ++cnt; }
    r.tipDrop = -tip / cnt;
    return r;
}

int main() {
    const Real L = 0.07, W = 0.035, B = 1.0e-3, rho = 0.2;
    std::printf("beam theory: delta = rho*g*L^4/(8B), L measured from the clamp; B = %.1e N*m, rho = %.2f kg/m2\n", B, rho);

    struct Case { const char* name; Real h; bool rect; Real tol; };
    const Case cases[] = {{"hex mesh   h=7.0mm", 0.007, false, 0.30}, {"hex mesh   h=3.5mm", 0.0035, false, 0.08},
                          {"hex mesh   h=2.5mm", 0.0025, false, 0.08}, {"grid mesh  h=3.5mm", 0.0035, true, 0.20}};
    for (const Case& c : cases) {
        Result r = runCantilever(L, W, c.h, B, B, rho, 0.0, c.rect);
        const Real err = (r.tipDrop - r.analytic) / r.analytic;
        std::printf("  %-20s drop %.3f mm  vs analytic %.3f mm  error %+6.1f%%  (%d frames, line-search failures %d)\n", c.name, 1000 * r.tipDrop,
                    1000 * r.analytic, 100 * err, r.frames, r.lsFail);
        CHECK_MSG(std::fabs(err) < c.tol, "%s error %.1f%% (tolerance %.0f%%)", c.name, 100 * err, 100 * c.tol);
        CHECK(r.lsFail == 0);
    }

    // Orthotropy: warp bending B, weft bending 4*B (a realistic woven contrast). The same strip cut
    // along the warp vs across it must sag differently, in the right direction and by a sensible amount.
    // Known limitation: the hinge model mixes both directions on diagonal hinges and through the
    // sideways-curl energy, so the mesh-level contrast is smaller than the nominal 4x and strongly
    // orthotropic bending (>~1:5) is under-represented. A dedicated anisotropic bending model is on the roadmap.
    Result along = runCantilever(L, W, 0.0035, B, 4 * B, rho, 0.0, false);
    Result across = runCantilever(L, W, 0.0035, B, 4 * B, rho, kPi / 2, false);
    const Real ratio = along.tipDrop / across.tipDrop;
    std::printf("  grain test: along-warp strip %.3f mm, across-warp strip %.3f mm, ratio %.2f (nominal stiffness ratio 4)\n", 1000 * along.tipDrop,
                1000 * across.tipDrop, ratio);
    CHECK_MSG(ratio > 1.5 && ratio < 5.0, "grain ratio %.2f", ratio);
    FINISH();
}
