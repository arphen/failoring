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

    std::printf("[J-curve: helper C1 continuity and gradient vs central differences (crochet plan 2.7 test 5a)]\n");
    {
        // detail::jCurveStretch describes E(C) above the lock: E = 0.5 k lock^2 + k lock (C-lock) + 0.5 r k (C-lock)^2,
        // dE/dC = k lock + r k (C-lock) (the plan's example numbers: lock 0.15, ratio 30, k 1).
        const Real lock = 0.15, ratio = 30.0, k = 1.0;
        auto E = [&](Real C) { return C > lock ? detail::jCurveStretch(C, k, lock, ratio).e : 0.5 * k * C * C; };
        auto dEdC = [&](Real C) { return C > lock ? detail::jCurveStretch(C, k, lock, ratio).dedc : k * C; };

        // C0 at the lock: at machine-scale h the raw jump is only the slope * h, and the helper's constant term
        // matches 0.5 k lock^2 (the below-lock parabola at C = lock).
        const Real hTiny = 1e-12;
        const Real rawJump = E(lock + hTiny) - E(lock - hTiny);
        const Real lockOffset = std::fabs(detail::jCurveStretch(lock, k, lock, ratio).e - 0.5 * k * lock * lock);
        std::printf("  energy continuity at C = lock: |E(lock+h) - E(lock-h)| = %.3e J at h = %.0e (PASS if < 1e-12)\n",
                    std::fabs(rawJump), hTiny);
        std::printf("  |E_jcurve(lock) - 0.5 k lock^2| = %.3e J (PASS if < 1e-12)\n", lockOffset);
        CHECK_MSG(std::fabs(rawJump) < 1e-12, "energy jumps by %.3e J across the lock", std::fabs(rawJump));
        CHECK_MSG(lockOffset < 1e-12, "the helper is offset by %.3e J from 0.5 k lock^2 at the lock", lockOffset);

        // C1: the left limit of dE/dC (k*C -> k*lock) equals the helper's right limit at C = lock exactly.
        const Real leftSlope = k * lock;
        const Real rightSlope = detail::jCurveStretch(lock, k, lock, ratio).dedc;
        std::printf("  gradient continuity at C = lock: left limit k*lock = %.17g, right limit dedc = %.17g (PASS if equal)\n",
                    leftSlope, rightSlope);
        CHECK_MSG(leftSlope == rightSlope, "dE/dC jumps at the lock: %.17g (left) vs %.17g (right)", leftSlope, rightSlope);

        // dE/dC from central differences (step 1e-6) at strains on both sides of the lock.
        const Real h = 1e-6;
        Real worstRel = 0;
        for (Real C : {0.05, 0.10, 0.145, 0.155, 0.20, 0.30}) {
            const Real fd = (E(C + h) - E(C - h)) / (2 * h);
            const Real an = dEdC(C);
            worstRel = std::max(worstRel, std::fabs(fd - an) / std::max(std::fabs(an), Real(1e-12)));
        }
        std::printf("  central-difference dE/dC (step %.0e) at 6 strains: worst relative error %.3e (PASS if < 1e-6)\n", h, worstRel);
        CHECK_MSG(worstRel < 1e-6, "dE/dC mismatches the central difference by %.3e (relative)", worstRel);

        // Above the lock the slope grows monotonically with C; the closed form holds bit for bit at C = 0.20.
        const Real C20 = 0.20;
        const Real an20 = detail::jCurveStretch(C20, k, lock, ratio).dedc;
        const Real expect20 = k * lock + ratio * k * (C20 - lock);
        std::printf("  above-lock slope dedc(%.2f) = %.9f == k*lock + ratio*k*(C-lock) exactly (PASS if equal)\n", C20, an20);
        CHECK_MSG(an20 == expect20, "dedc(0.20) = %.17g, expected %.17g", an20, expect20);
        Real prevRatio = 0;
        bool grows = true, first = true;
        for (Real C : {0.151, 0.16, 0.18, 0.20, 0.25, 0.30}) {
            const Real dc = detail::jCurveStretch(C, k, lock, ratio).dedc;
            const Real rr = dc / C;
            if (!first) grows = grows && rr > prevRatio;
            first = false;
            prevRatio = rr;
            std::printf("    C %.3f: dedc %.6f, dedc/C %.6f\n", C, dc, rr);
        }
        CHECK_MSG(grows, "dedc/C does not grow with C above the lock");
    }

    std::printf("[J-curve: static equilibrium strain vs the analytic piecewise-linear answer (crochet plan 2.7 test 5b)]\n");
    {
        // The lock keys parse (and bad values throw): the loader half of the ticket.
        const std::vector<FabricMaterial> jm = loadMaterialsFromString(
            R"({"fabrics":[{"id":"jc_test","name":"J-curve test","category":"knit","notes":"",
                 "params":{"density_kg_m2":0.15,"thickness_m":0.0005,
                           "tensile_warp_N_m":300.0,"tensile_weft_N_m":300.0,"shear_N_m_rad":40.0,
                           "bending_warp_Nm":1e-05,"bending_weft_Nm":1e-05,"compression_ratio":0.3,
                           "friction_static":0.5,"friction_kinetic":0.35,"damping_1_s":0.6,
                           "lock_strain_warp":0.15,"lock_strain_weft":0.12,"lock_stiffness_ratio":30.0}}]})");
        CHECK(jm.size() == 1);
        const FabricMaterial& fb = jm[0];
        CHECK_MSG(fb.lockStrainWarp == 0.15 && fb.lockStrainWeft == 0.12 && fb.lockStiffnessRatio == 30.0,
                  "lock keys parsed as %.17g / %.17g / %.17g", fb.lockStrainWarp, fb.lockStrainWeft, fb.lockStiffnessRatio);
        std::printf("  parsed lock keys: warp %.4f, weft %.4f, ratio %.2f\n", fb.lockStrainWarp, fb.lockStrainWeft,
                    fb.lockStiffnessRatio);
        // Without the keys the defaults are exactly the old behaviour (the shipped materials/fabrics.json has none).
        const std::vector<FabricMaterial> old = loadMaterialsFromString(
            R"({"fabrics":[{"id":"plain","params":{"tensile_warp_N_m":100.0,"tensile_weft_N_m":100.0,"shear_N_m_rad":10.0,"bending_warp_Nm":1e-05,"bending_weft_Nm":1e-05}}]})");
        CHECK(old.size() == 1);
        CHECK_MSG(old[0].lockStrainWarp == 0 && old[0].lockStrainWeft == 0 && old[0].lockStiffnessRatio == 1,
                  "lock defaults are not 0 / 0 / 1");
        bool threwRatio = false, threwLock = false;
        try { loadMaterialsFromString(R"({"fabrics":[{"id":"bad","params":{"lock_stiffness_ratio":0.5}}]})"); }
        catch (const std::runtime_error&) { threwRatio = true; }
        try { loadMaterialsFromString(R"({"fabrics":[{"id":"bad","params":{"lock_strain_warp":-0.1}}]})"); }
        catch (const std::runtime_error&) { threwLock = true; }
        std::printf("  validation: lock_stiffness_ratio 0.5 throws: %s, negative lock_strain_warp throws: %s (PASS if yes/yes)\n",
                    threwRatio ? "yes" : "no", threwLock ? "yes" : "no");
        CHECK_MSG(threwRatio, "lock_stiffness_ratio 0.5 did not throw");
        CHECK_MSG(threwLock, "negative lock_strain_warp did not throw");

        // One triangle pulled along u by a pinned weight: v0 = (0, h, 0), v1 = (0, -h, 0) (the pinned pair),
        // v2 = (L, 0, 0); layoutUV = the same 2D coordinates, so the rest frame's u axis is x. v2 carries the
        // extra mass M and gravity pulls it along +x with F = m2 * g.
        const Real hh = 0.03, LL = 0.06, M = 1.5;
        IntrinsicMesh tri;
        tri.x0 = {Vec3(0, hh, 0), Vec3(0, -hh, 0), Vec3(LL, 0, 0)};
        tri.layoutUV = {{0, hh}, {0, -hh}, {LL, 0}};
        tri.tris = {{0, 1, 2}};
        tri.triRest.push_back({Vec2(0, hh), Vec2(0, -hh), Vec2(LL, 0)});
        tri.triMaterial = {0};
        tri.extraMass = {0, 0, M};

        SolverParams sp;
        sp.gravity = Vec3(9.81, 0, 0);
        sp.hessianAuto = false;
        sp.tensionStiffness = true;  // exercises the locked addTension branch (Hessian model only, not the equilibrium)
        ClothSolver s(sp);
        s.addIntrinsicPanel(tri, {fb}, "jcurve");
        s.pinVertex(0);
        s.pinVertex(1);

        StaticParams st;
        st.residualTolerance = 1e-8;      // tighter than the 1e-3 default so the 0.5 % strain check is not
        st.displacementTolerance = 1e-9;  // residual-limited
        st.maxIterations = 2000;
        const StaticResult r = s.solveStatic(st);
        CHECK_MSG(r.converged, "static solve did not converge (res %.3e, step %.3e, iters %d)", r.residual, r.maxStep,
                  r.iterations);

        // Replicate finalize()'s 2x2 rest-frame inverse from triRest (a = dv2/det, b = -du2/det, c = -dv1/det,
        // d = du1/det). wu = d1*a + d2*c is the solver's deformed u column and C = |wu| - 1 its u strain.
        const std::vector<Vec3>& x = s.positions();
        const Vec2 u0 = tri.triRest[0][0], u1 = tri.triRest[0][1], u2 = tri.triRest[0][2];
        const Real du1 = u1.x - u0.x, dv1 = u1.y - u0.y, du2 = u2.x - u0.x, dv2 = u2.y - u0.y;
        const Real det = du1 * dv2 - du2 * dv1;
        const Real ar = dv2 / det, br = -du2 / det, cr = -dv1 / det, dr = du1 / det;
        const Real area = 0.5 * std::fabs(det);
        // k = 1/alphaU exactly as the solver's locked branch sees it: finalize() stores alphaU = 1/(tensile*area)
        // (stretchStiffnessCap = 0) and the branch computes 1.0 / c.alphaU.
        const Real k = 1.0 / (1.0 / (fb.tensileWarp * area));

        const Vec3 d1 = x[1] - x[0], d2 = x[2] - x[0];
        const Vec3 wu = d1 * ar + d2 * cr, wv = d1 * br + d2 * dr;
        const Real lu = length(wu);
        const Real Cu = lu - 1, Cv = length(wv) - 1, Cs = dot(wu, wv);
        const Vec3 nu = wu / lu;
        const Real lock = fb.lockStrainWarp;
        const Real F = s.vertexMass(2) * sp.gravity.x;  // weight pulling v2 along +x (N)
        const Real dedc = detail::jCurveStretch(Cu, k, lock, fb.lockStiffnessRatio).dedc;
        // The solver's force balance on v2: grad(energy)_x = dedc * (d2 coefficient of wu) * nu = the weight.
        const Real balX = dedc * cr * nu.x - F, balY = dedc * cr * nu.y, balZ = dedc * cr * nu.z;

        // Independent bisection of the same scalar balance on the deformed geometry, with the piecewise
        // T(C) = k*C below the lock and k*lock + ratio*k*(C-lock) above; x2(d) = (L + d, 0, 0).
        auto deformedW = [&](Real dsp) { return d1 * ar + (Vec3(LL + dsp, 0, 0) - x[0]) * cr; };
        auto residual = [&](Real dsp) {
            const Vec3 w = deformedW(dsp);
            const Real Cb = length(w) - 1;
            const Real Tb = Cb > lock ? detail::jCurveStretch(Cb, k, lock, fb.lockStiffnessRatio).dedc : k * Cb;
            const Vec3 nn = w / length(w);
            return Tb * cr * nn.x - F;
        };
        Real lo = 0, hi = LL;  // residual(0) < 0 < residual(L)
        CHECK_MSG(residual(lo) < 0 && residual(hi) > 0, "bisection bracket does not contain the root");
        for (int it = 0; it < 100; ++it) {
            const Real mid = 0.5 * (lo + hi);
            if (residual(mid) < 0) lo = mid; else hi = mid;
        }
        const Real CBis = length(deformedW(0.5 * (lo + hi))) - 1;

        // Analytic piecewise-linear answer: dedc = F*L/k above the lock inverts to C = lock + (F*L/k - lock)/ratio.
        const Real Cana = lock + (F * LL / k - lock) / fb.lockStiffnessRatio;
        const Real relBalX = std::fabs(balX) / F, relBalY = std::fabs(balY) / F;
        const Real relAna = std::fabs(Cana - Cu) / Cu, relBis = std::fabs(CBis - Cu) / Cu;

        std::printf("  rig: h %.3f m, L %.3f m, tensile_warp %.1f N/m, density %.3f kg/m2, extraMass %.3f kg, g %.3f m/s2\n",
                    hh, LL, fb.tensileWarp, fb.density, M, sp.gravity.x);
        std::printf("  v2 mass %.6f kg -> weight F = %.6f N; k = 1/alphaU = %.6f N\n", s.vertexMass(2), F, k);
        std::printf("  settled strain: C_u = %.6f (PASS if > lock %.2f: the stiff branch is exercised), C_v = %.2e, shear = %.2e (~0)\n",
                    Cu, lock, Cv, Cs);
        std::printf("  force balance on v2: |dedc*c*nu.x - F| / F = %.3e (PASS if < 5e-3), |y| / F = %.3e (PASS if < 1e-9)\n",
                    relBalX, relBalY);
        std::printf("  strain: measured %.6f, analytic piecewise-linear %.6f, bisection %.6f\n", Cu, Cana, CBis);
        std::printf("  |measured - analytic| / measured = %.3e (PASS if < 5e-3)\n", relAna);
        std::printf("  |measured - bisection| / measured = %.3e (PASS if < 5e-3)\n", relBis);
        std::printf("  force-balance residual: x %.3e N, y %.3e N, z %.3e N (printed, no threshold)\n", balX, balY, balZ);

        CHECK_MSG(Cu > lock, "settled strain %.6f is not above the lock %.2f", Cu, lock);
        CHECK_MSG(std::fabs(Cv) < 1e-9 && std::fabs(Cs) < 1e-9, "the v axis is loaded: C_v %.3e, shear %.3e", Cv, Cs);
        CHECK_MSG(relBalX < 5e-3, "the v2 force balance is off by %.3e (relative)", relBalX);
        CHECK_MSG(relBalY < 1e-9, "the lateral (y) force balance is %.3e of the weight", relBalY);
        CHECK_MSG(relAna < 5e-3, "measured strain %.6f vs analytic %.6f", Cu, Cana);
        CHECK_MSG(relBis < 5e-3, "measured strain %.6f vs bisection %.6f", Cu, CBis);

        // potentialEnergy (the solveStatic result) must equal the analytic J-curve energy minus the gravity work:
        // E = jc.e(C_u) - m2 g x2.x (the v stretch and the shear are zero, and there are no hinges or seams). This
        // pins variationalEnergy's locked branch to the same formula assembleEnergy's gradient uses.
        const Real jcE = detail::jCurveStretch(Cu, k, lock, fb.lockStiffnessRatio).e;
        const Real Eexp = jcE - F * x[2].x;
        std::printf("  energy: StaticResult %.9f J vs analytic jc.e - F*x2.x = %.9f J, |diff| = %.3e J (PASS if < 1e-9)\n",
                    r.energy, Eexp, std::fabs(r.energy - Eexp));
        CHECK_MSG(std::fabs(r.energy - Eexp) < 1e-9, "potentialEnergy %.9f J != analytic %.9f J", r.energy, Eexp);

        // The same check on the weft (v) axis exercises TriC::lockV: the rest frame maps its v axis onto world +y, so
        // the mirrored triangle (pinned pair along x, free vertex at (0, L, 0)) is pulled by gravity along +y.
        {
            IntrinsicMesh tv;
            tv.x0 = {Vec3(hh, 0, 0), Vec3(-hh, 0, 0), Vec3(0, LL, 0)};
            tv.layoutUV = {{hh, 0}, {-hh, 0}, {0, LL}};
            tv.tris = {{0, 1, 2}};
            tv.triRest.push_back({Vec2(hh, 0), Vec2(-hh, 0), Vec2(0, LL)});
            tv.triMaterial = {0};
            tv.extraMass = {0, 0, M};
            SolverParams spv;
            spv.gravity = Vec3(0, 9.81, 0);
            spv.hessianAuto = false;
            spv.tensionStiffness = true;
            ClothSolver sv(spv);
            sv.addIntrinsicPanel(tv, {fb}, "jcurve_v");
            sv.pinVertex(0);
            sv.pinVertex(1);
            const StaticResult rv = sv.solveStatic(st);
            CHECK_MSG(rv.converged, "v-axis static solve did not converge (res %.3e, step %.3e)", rv.residual, rv.maxStep);

            const std::vector<Vec3>& xv = sv.positions();
            const Vec2 w0 = tv.triRest[0][0], w1 = tv.triRest[0][1], w2 = tv.triRest[0][2];
            const Real gu1 = w1.x - w0.x, gv1 = w1.y - w0.y, gu2 = w2.x - w0.x, gv2 = w2.y - w0.y;
            const Real gdet = gu1 * gv2 - gu2 * gv1;
            const Real av = gv2 / gdet, bv = -gu2 / gdet, cv = -gv1 / gdet, dv = gu1 / gdet;
            const Real areav = 0.5 * std::fabs(gdet);
            const Real kv = 1.0 / (1.0 / (fb.tensileWeft * areav));
            const Vec3 e1 = xv[1] - xv[0], e2 = xv[2] - xv[0];
            const Vec3 wuv = e1 * av + e2 * cv, wvv = e1 * bv + e2 * dv;
            const Real Cvu = length(wuv) - 1, Cvv = length(wvv) - 1;
            const Vec3 nvv = wvv / length(wvv);
            const Real lockV = fb.lockStrainWeft;
            const Real Fv = sv.vertexMass(2) * spv.gravity.y;
            const Real dedcv = detail::jCurveStretch(Cvv, kv, lockV, fb.lockStiffnessRatio).dedc;
            const Real relBalV = std::fabs(dedcv * dv * nvv.y - Fv) / Fv;
            const Real relAnaV = std::fabs(lockV + (Fv * LL / kv - lockV) / fb.lockStiffnessRatio - Cvv) / Cvv;
            std::printf("  v axis (lockV %.2f): settled C_v = %.6f, analytic %.6f, force balance / F = %.3e, unloaded C_u = %.2e\n",
                        lockV, Cvv, lockV + (Fv * LL / kv - lockV) / fb.lockStiffnessRatio, relBalV, Cvu);
            CHECK_MSG(Cvv > lockV, "settled v strain %.6f is not above the weft lock %.2f", Cvv, lockV);
            CHECK_MSG(std::fabs(Cvu) < 1e-9, "the u axis of the v rig is loaded: C_u %.3e", Cvu);
            CHECK_MSG(relBalV < 5e-3, "the v2 force balance along y is off by %.3e (relative)", relBalV);
            CHECK_MSG(relAnaV < 5e-3, "measured v strain %.6f vs analytic %.6f", Cvv, lockV + (Fv * LL / kv - lockV) / fb.lockStiffnessRatio);
            const Real jcEv = detail::jCurveStretch(Cvv, kv, lockV, fb.lockStiffnessRatio).e;
            const Real EexpV = jcEv - Fv * xv[2].y;
            std::printf("  v axis energy: StaticResult %.9f J vs analytic jc.e - F*x2.y = %.9f J, |diff| = %.3e J (PASS if < 1e-9)\n",
                        rv.energy, EexpV, std::fabs(rv.energy - EexpV));
            CHECK_MSG(std::fabs(rv.energy - EexpV) < 1e-9, "potentialEnergy %.9f J != analytic %.9f J on the v axis", rv.energy, EexpV);
        }
    }

    std::printf("[point seam: two squares joined at 3 vertex pairs 5 cm apart (crochet plan 2.7 test 6)]\n");
    {
        // Two separate 0.10 x 0.10 m squares, cell 0.025 m (meshRect ordering id(i,j) = j*5 + i; 25 verts each).
        // Panel A spans x in [0, 0.10] (default placement); panel B is offset by width + 5 cm, so x in [0.15, 0.25].
        // The seams link 3 consecutive pairs on the facing edges (A's right edge i = 4, B's left edge i = 0, same y
        // row j). Global ids (A 0..24, B 25..49): (9, 30), (14, 35), (19, 40) for rows j = 1, 2, 3; each pair starts
        // at exactly 0.05 m (y = j * 0.025 m on both sides, 5 cm apart in x). share = 0.025 m, the vertex spacing
        // one link stands for.
        PanelMesh sq = meshRect(0.10, 0.10, 0.025);
        PanelPlacement placeA, placeB;
        placeA.origin = Vec3(0, 0, 0);
        placeA.ex = Vec3(1, 0, 0);
        placeA.ey = Vec3(0, 1, 0);
        placeB.origin = Vec3(0.15, 0, 0);
        placeB.ex = Vec3(1, 0, 0);
        placeB.ey = Vec3(0, 1, 0);

        SolverParams sp;
        sp.gravity = Vec3(0, 0, 0);
        sp.seamRampFrames = 30;  // all other params keep their defaults; the plan's rig
        auto build = [&](const SolverParams& ps) {
            auto s = std::make_unique<ClothSolver>(ps);
            s->addPanel(sq, *fab, placeA, 0, "A");
            s->addPanel(sq, *fab, placeB, 0, "B");
            s->addPointSeam(9, 30, 0.025);
            s->addPointSeam(14, 35, 0.025);
            s->addPointSeam(19, 40, 0.025);
            return s;
        };

        auto s = build(sp);
        CHECK(s->vertexCount() == 50);
        CHECK(s->panelCount() == 2);
        // call-time validation of addPointSeam (before stepping: the solver is not finalized yet)
        bool threwShare = false, threwRange = false, threwSame = false;
        try { s->addPointSeam(9, 30, 0.0); } catch (const std::invalid_argument&) { threwShare = true; }
        try { s->addPointSeam(50, 30, 0.025); } catch (const std::out_of_range&) { threwRange = true; }
        try { s->addPointSeam(9, 9, 0.025); } catch (const std::invalid_argument&) { threwSame = true; }
        std::printf("  validation: share 0 throws: %s, vertex 50 throws: %s, a == b throws: %s (PASS if yes/yes/yes)\n",
                    threwShare ? "yes" : "no", threwRange ? "yes" : "no", threwSame ? "yes" : "no");
        CHECK_MSG(threwShare, "addPointSeam: share 0 did not throw std::invalid_argument");
        CHECK_MSG(threwRange, "addPointSeam: vertex 50 did not throw std::out_of_range");
        CHECK_MSG(threwSame, "addPointSeam: a == b did not throw std::invalid_argument");

        const int pairs[3][2] = {{9, 30}, {14, 35}, {19, 40}};
        for (int f = 0; f < 120; ++f) s->step();
        Real dist[3] = {0, 0, 0}, dmax = 0;
        for (int k = 0; k < 3; ++k) {
            dist[k] = length(s->positions()[pairs[k][0]] - s->positions()[pairs[k][1]]);
            dmax = std::max(dmax, dist[k]);
        }
        std::printf("  pair distances after 120 frames: %.3e, %.3e, %.3e m; max %.3e (PASS if < 5e-4 m)\n",
                    dist[0], dist[1], dist[2], dmax);
        CHECK_MSG(dmax < 5e-4, "point seam max pair distance %.3e m is not below 5e-4 m (0.5 mm)", dmax);

        // addPointSeam after the first step has finalized the solver must throw (same gate as addSeam).
        bool threwLate = false;
        try { s->addPointSeam(0, 49, 0.025); } catch (const std::logic_error&) { threwLate = true; }
        CHECK_MSG(threwLate, "addPointSeam after the simulation started did not throw std::logic_error");

        // dampingScale (CROCHET_PLAN 6.9): the same rig, fresh solvers, dampingScale 1 vs 5. Ten frames in, the
        // ramped seams have pumped the same motion into both, so the x5 run must carry strictly less KE.
        SolverParams sp5 = sp;
        sp5.dampingScale = 5.0;
        auto e1 = build(sp), e5 = build(sp5);
        for (int f = 0; f < 10; ++f) {
            e1->step();
            e5->step();
        }
        const Real ke1 = e1->kineticEnergy(), ke5 = e5->kineticEnergy();
        std::printf("  after 10 frames: KE dampingScale 1 = %.6e J, dampingScale 5 = %.6e J (PASS if 5 < 1)\n", ke1, ke5);
        CHECK_MSG(ke5 < ke1, "dampingScale 5 KE %.6e J is not strictly below dampingScale 1 KE %.6e J", ke5, ke1);
    }
    FINISH();
}
