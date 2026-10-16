// Crochet plan §2.7 test 1: an IntrinsicMesh whose triRest corners are copied from the vertex UVs must behave
// exactly like the equivalent normal panel. Both solvers get the same mesh, placement, material and params; the
// acceptance is max |x_A - x_B| < 1e-9 m, plus a non-vacuousness check that the sheet really settled onto the body.
// The numbers go to docs/CROCHET_STATUS.md.
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

#include "check.hpp"
#include "failoring/cloth.hpp"
#include "failoring/collider.hpp"
#include "failoring/intrinsic_mesh.hpp"
#include "failoring/material.hpp"
#include "failoring/mesher.hpp"

using namespace fl;

int main() {
    std::printf("[intrinsic panel equivalence (crochet plan 2.7 test 1)]\n");
    auto mats = loadMaterialsFromFile(FL_MATERIALS_PATH);
    const FabricMaterial* fab = findMaterial(mats, "cotton_poplin");
    CHECK(fab != nullptr);

    // A flat 30 x 30 cm sheet, 2 cm triangles, held 30 cm above a 15 cm sphere at the origin; it drapes over it.
    PanelMesh mesh = meshRect(0.30, 0.30, 0.02);
    PanelPlacement place;
    place.origin = Vec3(-0.15, 0.30, -0.15);
    place.ex = Vec3(1, 0, 0);
    place.ey = Vec3(0, 0, 1);
    auto sphere = std::make_shared<SphereCollider>(Vec3(0, 0, 0), 0.15);

    SolverParams sp;  // identical for both solvers

    // A: the mesh as a normal flat panel.
    ClothSolver a(sp);
    a.setCollider(sphere);
    a.addPanel(mesh, *fab, place, 0, "flat");

    // B: the same mesh as an intrinsic panel: x0 = the placed 3D positions, triRest = the vertex UVs.
    IntrinsicMesh im;
    std::vector<Vec3> x0;
    x0.reserve(mesh.verts.size());
    for (const Vec2& uv : mesh.verts) x0.push_back(place.apply(uv));
    im.x0 = x0;
    im.layoutUV = mesh.verts;
    im.tris = mesh.tris;
    im.triRest.reserve(mesh.tris.size());
    im.triMaterial.assign(mesh.tris.size(), 0);
    for (const auto& t : mesh.tris) im.triRest.push_back({mesh.verts[t[0]], mesh.verts[t[1]], mesh.verts[t[2]]});
    ClothSolver b(sp);
    b.setCollider(sphere);
    const int pid = b.addIntrinsicPanel(im, {*fab}, "intrinsic");
    CHECK(pid == 0);

    const int frames = 60;
    for (int f = 0; f < frames; ++f) {
        a.step();
        b.step();
    }

    CHECK(a.vertexCount() == b.vertexCount());
    CHECK(a.triangleCount() == b.triangleCount());
    CHECK(a.vertexCount() == static_cast<int>(mesh.verts.size()));
    CHECK(a.triangleCount() == static_cast<int>(mesh.tris.size()));

    Real massDiff = 0, maxDiff = 0, dispA = 0, dispB = 0;
    for (int i = 0; i < a.vertexCount(); ++i) {
        massDiff = std::max(massDiff, std::fabs(a.vertexMass(i) - b.vertexMass(i)));
        maxDiff = std::max(maxDiff, length(a.positions()[i] - b.positions()[i]));
        dispA = std::max(dispA, length(a.positions()[i] - x0[i]));
        dispB = std::max(dispB, length(b.positions()[i] - x0[i]));
    }

    std::printf("  %d verts, %d tris, %d frames each; %d panels\n", a.vertexCount(), a.triangleCount(), frames, a.panelCount());
    std::printf("  max per-vertex mass difference: %.3e kg\n", massDiff);
    std::printf("  max |x_A - x_B|: %.3e m (PASS if < 1e-9 m)\n", maxDiff);
    std::printf("  settled displacement from x0: %.3e m (A) / %.3e m (B) (PASS if > 1e-3 m)\n", dispA, dispB);

    CHECK_MSG(massDiff == 0, "per-vertex masses differ by %.3e kg", massDiff);
    CHECK_MSG(maxDiff < 1e-9, "intrinsic panel differs from the equivalent flat panel by %.3e m", maxDiff);
    CHECK_MSG(dispA > 1e-3, "sheet barely moved: %.3e m", dispA);
    CHECK_MSG(dispB > 1e-3, "sheet barely moved: %.3e m", dispB);

    std::printf("[signed hinge: analytic gradient vs central differences (crochet plan 2.7 test 3)]\n");
    {
        std::mt19937 rng(20261016u);
        std::uniform_real_distribution<double> U(-0.5, 0.5);
        const int cases = 20;
        const Real eps = 1e-6;  // central-difference step, as in test_basics
        Real worstUnsig = 0, worstRel = 0, worstAbs = 0, worstSum = 0, maxGrad = 0;
        for (int c = 0; c < cases; ++c) {
            // deterministic pseudo-random configurations with well-separated vertices: redraw until the shared edge
            // and both triangle normals are far from degenerate (a fixed seed, so this is reproducible)
            Vec3 q[4];
            bool wellSeparated = false;
            for (int tries = 0; tries < 1000 && !wellSeparated; ++tries) {
                for (int i = 0; i < 4; ++i) q[i] = Vec3(U(rng), U(rng), U(rng));
                wellSeparated = length(q[1] - q[0]) > 0.3 && length(cross(q[1] - q[0], q[2] - q[0])) > 0.05 &&
                                length(cross(q[1] - q[0], q[3] - q[0])) > 0.05;
            }
            CHECK(wellSeparated);  // must not happen (fixed seed); guards against a silent infinite loop
            Real ths = 0;
            Vec3 g[4];
            CHECK(detail::hingeAngleSigned(q[0], q[1], q[2], q[3], ths, g));
            const Real thu = detail::hingeAngle(q[0], q[1], q[2], q[3], nullptr);
            worstUnsig = std::max(worstUnsig, std::fabs(std::fabs(ths) - thu));   // invariant (a)
            worstSum = std::max(worstSum, length(g[0] + g[1] + g[2] + g[3]));     // gradients sum to zero
            // E(theta_s) = 0.5 (theta_s - 0.4)^2, analytic dE/dx = (theta_s - 0.4) dtheta_s/dx: compare every
            // component with a central difference of E (step 1e-6)
            for (int p = 0; p < 4; ++p)
                for (int k = 0; k < 3; ++k) {
                    Vec3 a[4] = {q[0], q[1], q[2], q[3]}, b[4] = {q[0], q[1], q[2], q[3]};
                    Real* pa = (k == 0) ? &a[p].x : (k == 1) ? &a[p].y : &a[p].z;
                    Real* pb = (k == 0) ? &b[p].x : (k == 1) ? &b[p].y : &b[p].z;
                    *pa += eps;
                    *pb -= eps;
                    Real sa = 0, sb = 0;
                    detail::hingeAngleSigned(a[0], a[1], a[2], a[3], sa, nullptr);
                    detail::hingeAngleSigned(b[0], b[1], b[2], b[3], sb, nullptr);
                    const Real fd = (0.5 * (sa - 0.4) * (sa - 0.4) - 0.5 * (sb - 0.4) * (sb - 0.4)) / (2 * eps);
                    const Real an = (ths - 0.4) * ((k == 0) ? g[p].x : (k == 1) ? g[p].y : g[p].z);
                    worstAbs = std::max(worstAbs, std::fabs(an - fd));
                    maxGrad = std::max(maxGrad, std::fabs(fd));
                    worstRel = std::max(worstRel, std::fabs(an - fd) / std::max(std::fabs(fd), Real(1e-9)));
                }
        }
        std::printf("  %d random hinge configurations, 4x3 gradient components each\n", cases);
        std::printf("  max | |theta_s| - theta_unsigned |: %.3e rad (PASS if < 1e-12)\n", worstUnsig);
        std::printf("  worst E-gradient relative error vs central FD: %.3e (PASS if < 1e-6), %.3e absolute, max |dE/dx| %.3f\n",
                    worstRel, worstAbs, maxGrad);
        std::printf("  max |g[0]+g[1]+g[2]+g[3]|: %.3e (PASS if < 1e-12)\n", worstSum);
        CHECK_MSG(worstUnsig <= 1e-12, "signed and unsigned hinge angles differ by %.3e rad", worstUnsig);
        CHECK_MSG(worstRel < 1e-6, "signed hinge gradient mismatches the finite difference by %.3e (relative)", worstRel);
        CHECK_MSG(worstSum < 1e-12, "signed hinge gradients do not sum to zero: %.3e", worstSum);

        // fixed checks of the orientation convention: flat -> 0, and the folded pair test_basics uses (x3 = (0.3,1,0),
        // x4 = (0.5,0,1), i.e. lifted to +z where triangle 1's normal points) -> +pi/2; the mirrored fold -> -pi/2
        Real t = 0;
        CHECK(detail::hingeAngleSigned(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0.3, 1, 0), Vec3(0.5, -1, 0), t, nullptr));
        std::printf("  flat, consistently wound pair: theta_s = %.3e rad (PASS if 0)\n", t);
        CHECK_MSG(std::fabs(t) < 1e-12, "flat pair has signed angle %.3e", t);
        CHECK(detail::hingeAngleSigned(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0.3, 1, 0), Vec3(0.5, 0, 1), t, nullptr));
        std::printf("  test_basics folded pair, x4 lifted to +z: theta_s = %.6f rad (PASS if +pi/2)\n", t);
        CHECK_MSG(std::fabs(t - kPi / 2) < 1e-12, "folded pair has signed angle %.6f, expected +pi/2", t);
        const Real tu = detail::hingeAngle(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0.3, 1, 0), Vec3(0.5, 0, 1), nullptr);
        CHECK_MSG(std::fabs(std::fabs(t) - tu) < 1e-12, "|theta_s| %.12f differs from the unsigned %.12f", std::fabs(t), tu);
        CHECK(detail::hingeAngleSigned(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0.3, 1, 0), Vec3(0.5, 0, -1), t, nullptr));
        std::printf("  the mirrored fold, x4 lifted to -z: theta_s = %.6f rad (PASS if -pi/2)\n", t);
        CHECK_MSG(std::fabs(t + kPi / 2) < 1e-12, "mirrored fold has signed angle %.6f, expected -pi/2", t);
    }

    std::printf("[signed hinge rest angle: 2-triangle strip, rest = +0.5 rad (crochet plan 2.7 test 4)]\n");
    {
        // A flat strip of two triangles sharing edge (0,1), both wound counter-clockwise around +z:
        // triangle 1 = (0,1,2) is pinned, triangle 2 = (1,0,3) has the free vertex 3. finalize() turns the shared
        // edge into one hinge with b.i = {0,1,2,3}, so vertex 3 is the second triangle's opposite vertex i[3].
        const Real w = 0.04, h = 0.02;
        IntrinsicMesh strip;
        strip.x0 = {Vec3(0, 0, 0), Vec3(w, 0, 0), Vec3(w / 2, h, 0), Vec3(w / 2, -h, 0)};
        strip.layoutUV = {{0, 0}, {w, 0}, {w / 2, h}, {w / 2, -h}};
        strip.tris = {{0, 1, 2}, {1, 0, 3}};
        strip.triRest.reserve(strip.tris.size());
        for (const auto& t : strip.tris) strip.triRest.push_back({strip.layoutUV[t[0]], strip.layoutUV[t[1]], strip.layoutUV[t[2]]});
        strip.triMaterial = {0, 0};
        HingeMod hm;
        hm.a = 0;
        hm.b = 1;           // the shared edge
        hm.restAngle = 0.5; // rad, bendScale stays 1
        strip.hingeMods = {hm};

        SolverParams sp;
        sp.gravity = Vec3(0, 0, 0);
        ClothSolver s(sp);
        s.addIntrinsicPanel(strip, {*fab}, "strip");
        s.pinVertex(0);
        s.pinVertex(1);
        s.pinVertex(2);  // the three vertices of triangle 1
        const StaticResult r = s.solveStatic();
        CHECK_MSG(r.converged, "static solve did not converge (res %.3e, step %.3e)", r.residual, r.maxStep);

        const std::vector<Vec3>& x = s.positions();
        Real ths = 0;
        const bool ok = detail::hingeAngleSigned(x[0], x[1], x[2], x[3], ths, nullptr);
        CHECK(ok);
        // triangle 1's oriented normal from its winding (tris[0]) and the free vertex's side of its plane
        const Vec3 n1 = normalized(cross(x[1] - x[0], x[2] - x[0]));
        const Real side = dot(x[3] - x[0], n1);
        std::printf("  settled in %d Newton iterations, residual %.2e\n", r.iterations, r.residual);
        std::printf("  measured signed angle on the settled positions: %.4f rad (PASS if 0.5 +- 0.005)\n", ths);
        std::printf("  free vertex signed distance to triangle 1's plane: %+.4f mm on its +normal side (PASS if > 0)\n",
                    1000 * side);
        CHECK_MSG(std::fabs(ths - 0.5) < 0.005, "settled signed angle %.4f rad differs from the +0.5 rad rest angle", ths);
        CHECK_MSG(side > 0, "free vertex settled on the -normal side of triangle 1 (signed distance %.3e m)", side);
    }
    FINISH();
}
