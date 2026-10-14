// End-to-end garment test: two pattern panels, sewn at both sides into a tube,
// dropped from above onto an ellipsoid mannequin. The fabric cannot stretch, so the
// tube's lower hem must lodge where the body girth equals the tube circumference -
// a physical result we can predict independently of the simulator. (The tube starts
// on the narrow upper dome: started around the belly it would have to stretch ~10% to close,
// and started as flat panels beside the body it collapses into a pillow and slides off.)
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "check.hpp"
#include "failoring/cloth.hpp"
#include "failoring/collider.hpp"
#include "failoring/material.hpp"
#include "failoring/mesher.hpp"
#include "failoring/metrics.hpp"

using namespace fl;

static Real ellipsePerimeter(Real a, Real b) {  // Ramanujan II
    const Real hh = (a - b) * (a - b) / ((a + b) * (a + b));
    return kPi * (a + b) * (1 + 3 * hh / (10 + std::sqrt(4 - 3 * hh)));
}

int main() {
    auto mats = loadMaterialsFromFile(FL_MATERIALS_PATH);
    const FabricMaterial* fab = findMaterial(mats, "cotton_poplin");
    CHECK(fab != nullptr);

    // mannequin: vertical ellipsoid
    const Vec3 radii(0.17, 0.35, 0.11);
    auto body = std::make_shared<EllipsoidCollider>(Vec3(0, 0, 0), radii);

    const Real W = 0.40, H = 0.30, h = 0.02, y0 = 0.25;  // hem starts on the upper dome, where the girth is 0.62 m
    PanelMesh mesh = meshRect(W, H, h);

    SolverParams sp;
    sp.substeps = 4;  // contact projection over-stretches wedged rings at larger steps
    sp.seamRampFrames = 0;  // panels start pre-wrapped: seams are already closed
    // K5 option (off unless FL_BARRIER is set in the environment): IPC-style barrier self contact instead of the
    // vertex-triangle projection. The default run is unchanged.
    if (std::getenv("FL_BARRIER")) { sp.barrierContact = true; sp.selfCollision = false; }
    // K2 (docs/handoff/OPUS_SESSION_1.md): the tension part of the stretch Hessian, off unless FL_TENSION is set.
    if (std::getenv("FL_TENSION")) sp.tensionStiffness = true;
    ClothSolver cs(sp);
    cs.setCollider(body);

    PanelPlacement front, back;
    front.origin = Vec3(-W / 2, y0, 0.145); front.ex = Vec3(1, 0, 0);  front.ey = Vec3(0, 1, 0);
    back.origin  = Vec3(+W / 2, y0, -0.145); back.ex  = Vec3(-1, 0, 0); back.ey  = Vec3(0, 1, 0);
    const int pf = cs.addPanel(mesh, *fab, front, kPi / 2, "front");
    const int pb = cs.addPanel(mesh, *fab, back, kPi / 2, "back");
    cs.addSeam(pf, 1, pb, 3, true);  // front right side  <->  back left side
    cs.addSeam(pf, 3, pb, 1, true);  // front left side   <->  back right side

    // Pre-wrap each panel onto a circular tube of circumference 2W (isometric: arc length = panel x),
    // so the seams start closed. Front panel spans the +z half, back panel the -z half.
    {
        const Real Rc = W / kPi;
        for (int p : {pf, pb})
            for (size_t i = 0; i < mesh.verts.size(); ++i) {
                const Real th = kPi * mesh.verts[i].x / W, y = y0 + mesh.verts[i].y;
                const Vec3 q = (p == pf) ? Vec3(-Rc * std::cos(th), y, Rc * std::sin(th)) : Vec3(Rc * std::cos(th), y, -Rc * std::sin(th));
                cs.setVertexPosition(cs.firstVertex(p) + static_cast<int>(i), q);
            }
    }

    auto t0 = std::chrono::steady_clock::now();
    int f = 0;
    for (; f < 300; ++f) {
        cs.step();
        if (f > 150 && cs.kineticEnergy() < 1e-6) break;
    }
    const Real secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    // measurements
    Real hemY = 0;
    int rimN = 0;
    // lower hem of both panels = outline edge 0
    for (int p : {pf, pb})
        for (int v : cs.edgeChain(p, 0)) { hemY += cs.positions()[v].y; ++rimN; }
    hemY /= rimN;

    // predicted lodging height: tube circumference = body girth (inflated by the contact offset)
    const Real circ = 2 * W;
    const Real off = sp.contactOffset + 0.5 * fab->thickness;
    Real lo = 0, hi = radii.y - 1e-6;
    for (int it = 0; it < 60; ++it) {  // girth decreases with |y|; find y where girth == circ
        const Real y = 0.5 * (lo + hi);
        const Real s = std::sqrt(1 - (y / radii.y) * (y / radii.y));
        const Real girth = ellipsePerimeter(radii.x * s + off, radii.z * s + off);
        if (girth > circ) lo = y; else hi = y;
    }
    const Real predicted = 0.5 * (lo + hi);

    const Real gap = cs.maxSeamGap();
    const Real clr = cs.minBodyClearance();
    bool finite = true;
    for (auto& p : cs.positions()) finite = finite && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    std::printf("garment: %d verts, %d tris, %d frames (%.1f s wall)\n", cs.vertexCount(), cs.triangleCount(), f, secs);
    std::printf("  mass %.0f g, KE %.1e J, seam gap max %.2f mm, body clearance min %.2f mm\n", cs.totalMass() * 1000, cs.kineticEnergy(), gap * 1000, clr * 1000);
    std::printf("  hem height %.1f mm, predicted lodging height (girth = circumference) %.1f mm\n", hemY * 1000, predicted * 1000);

    CHECK(finite);
    CHECK_MSG(cs.kineticEnergy() < 5e-4,  // a ring wedged on the body keeps jittering at ~1e-4 J (known limitation, see docs)
               "not settled, KE %.2e", cs.kineticEnergy());
    CHECK_MSG(gap < 0.004, "seams not closed: %.2f mm", gap * 1000);
    CHECK_MSG(clr > -0.001, "penetrates the body by %.2f mm", -clr * 1000);
    CHECK_MSG(std::fabs(hemY - predicted) < 0.04, "hem at %.1f mm vs predicted %.1f mm", hemY * 1000, predicted * 1000);

    writeObj(std::string(FL_OUT_DIR) + "/garment_tube_dress.obj", cs.positions(), cs.triangles());

    // body mesh for viewing next to the garment
    {
        const int nu = 48, nv = 24;
        std::vector<Vec3> X;
        std::vector<std::array<int, 3>> T;
        for (int j = 0; j <= nv; ++j)
            for (int i = 0; i < nu; ++i) {
                const Real th = kPi * j / nv, ph = 2 * kPi * i / nu;
                X.push_back({radii.x * std::sin(th) * std::cos(ph), radii.y * std::cos(th), radii.z * std::sin(th) * std::sin(ph)});
            }
        for (int j = 0; j < nv; ++j)
            for (int i = 0; i < nu; ++i) {
                const int a = j * nu + i, b = j * nu + (i + 1) % nu, c = (j + 1) * nu + i, d = (j + 1) * nu + (i + 1) % nu;
                T.push_back({a, c, b});
                T.push_back({b, c, d});
            }
        writeObj(std::string(FL_OUT_DIR) + "/garment_body.obj", X, T);
    }
    FINISH();
}
