// K1 compliant skin layer: analytic ring-on-cylinder equilibrium (Laplace relation), the same on a baked grid
// collider, per-region stiffness lookups, pressure accessors, a "too small tube on a convex body" settling
// comparison (rigid vs soft), and the guarantee that the disabled layer changes nothing.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include "check.hpp"
#include "failoring/cloth.hpp"
#include "failoring/collider.hpp"
#include "failoring/material.hpp"
#include "failoring/mesher.hpp"
#include "failoring/sdf_grid.hpp"
#include "failoring/skin.hpp"

using namespace fl;
using Tris = std::vector<std::array<int, 3>>;

// ---------------------------------------------------------------------------------------------------------------
// Part 1: ring of cloth around a rigid-core cylinder.
// Two panels (each W wide, circumference C = 2W) sewn into a tube, pre-wrapped so the seams start closed.
// The hoop direction is the panel u axis, grain = pi/2 puts the weft along u (tensileWeft). Gravity off.
// Model: skin surface (offset-corrected) at Reff = R + radius_, cloth radius r = Reff - delta, polygon with n sides:
//   strain eps(r) = n * 2 r sin(pi/n) / C - 1,  T = Ks * eps,  p = T / r  (Laplace),  p = k * delta.
// Each vertex carries chord-length tributary width, so the polygon obeys p = T/r exactly (see notes).
// ---------------------------------------------------------------------------------------------------------------
struct RingSetup {
    Real W = 0.18, H = 0.12, h = 0.01, R = 0.060, Ks = 1000.0;
    Real k = 1.5e5, dmax = 0.010;
    bool useGrid = false;
    Real gridH = 0.003;
};

struct RingResult {
    Real delta = 0, pSoft = 0, pRigid = 0, pTotal = 0, ke = 0, gap = 0;
    int n = 0;
    Real Reff = 0;
};

static FabricMaterial ringMaterial(Real Ks) {
    FabricMaterial m;
    m.id = "ring_test";
    m.density = 0.15;
    m.thickness = 0.0005;
    m.tensileWarp = Ks;
    m.tensileWeft = Ks;
    m.shear = 40;
    m.bendWarp = m.bendWeft = 1e-5;
    return m;
}

// Solves p(delta) = k delta for the scalar equilibrium; returns delta.
static Real expectedDelta(const RingSetup& s, int n, Real Reff) {
    const Real C = 2 * s.W;
    auto f = [&](Real d) {
        const Real r = Reff - d;
        const Real eps = n * 2 * r * std::sin(kPi / n) / C - 1;
        return std::max(eps, Real(0)) * s.Ks / r - s.k * d;  // decreasing in d
    };
    Real lo = 0, hi = Reff * 0.5;
    for (int i = 0; i < 100; ++i) {
        const Real mid = 0.5 * (lo + hi);
        if (f(mid) > 0) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
}

static std::shared_ptr<Collider> makeCylinderGrid(Real R, Real halfH, Real gridH) {
    const int nu = 96;
    std::vector<Vec3> V;
    Tris T;
    V.push_back({0, halfH, 0});
    V.push_back({0, -halfH, 0});
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < nu; ++i) {
            const Real a = 2 * kPi * i / nu;
            V.push_back({R * std::cos(a), j == 0 ? halfH : -halfH, R * std::sin(a)});
        }
    auto id = [&](int j, int i) { return 2 + j * nu + (i % nu); };
    for (int i = 0; i < nu; ++i) {
        T.push_back({0, id(0, i + 1), id(0, i)});   // top cap
        T.push_back({1, id(1, i), id(1, i + 1)});   // bottom cap
        T.push_back({id(0, i), id(0, i + 1), id(1, i)});
        T.push_back({id(0, i + 1), id(1, i + 1), id(1, i)});
    }
    Real vol = 0;  // make the orientation outward whatever the winding above came out as
    for (const auto& t : T) vol += dot(V[t[0]], cross(V[t[1]], V[t[2]])) / 6;
    if (vol < 0) for (auto& t : T) std::swap(t[1], t[2]);
    auto grid = std::make_shared<SdfGrid>();
    SdfGridParams gp;
    gp.h = gridH;
    gp.band = 2 * gridH;
    CHECK(grid->bake(V, T, gp));
    return std::make_shared<GridCollider>(grid);
}

static RingResult runRing(const RingSetup& s, int frames = 220, std::vector<Real>* triP = nullptr) {
    FabricMaterial mat = ringMaterial(s.Ks);
    std::shared_ptr<Collider> body;
    if (s.useGrid) body = makeCylinderGrid(s.R, 0.25, s.gridH);
    else body = std::make_shared<CylinderCollider>(Vec3(0, 0, 0), s.R, 0.25);

    SolverParams sp;
    sp.substeps = 2;
    sp.gravity = Vec3(0, 0, 0);
    sp.seamStiffness = 2e6;  // a soft seam would add its own stretch to the ring
    sp.tensionStiffness = true;
    sp.selfCollision = false;
    sp.skinStiffness = s.k;
    sp.skinMaxDepth = s.dmax;
    ClothSolver cs(sp);
    cs.setCollider(body);
    PanelMesh mesh = meshRect(s.W, s.H, s.h);
    PanelPlacement a, b;
    a.origin = Vec3(-s.W / 2, -s.H / 2, 0.1); a.ex = Vec3(1, 0, 0);  a.ey = Vec3(0, 1, 0);
    b.origin = Vec3(+s.W / 2, -s.H / 2, -0.1); b.ex = Vec3(-1, 0, 0); b.ey = Vec3(0, 1, 0);
    const int pf = cs.addPanel(mesh, mat, a, kPi / 2, "front");
    const int pb = cs.addPanel(mesh, mat, b, kPi / 2, "back");
    cs.addSeam(pf, 1, pb, 3, true);
    cs.addSeam(pf, 3, pb, 1, true);
    const Real radius = sp.contactOffset + 0.5 * mat.thickness;
    const Real Reff = s.R + radius;
    // pre-wrap on the skin surface (cloth starts at zero skin depth, over-stretched; the solver relaxes it)
    for (int p : {pf, pb})
        for (size_t i = 0; i < mesh.verts.size(); ++i) {
            const Real th = kPi * mesh.verts[i].x / s.W, y = -s.H / 2 + mesh.verts[i].y;
            const Vec3 q = (p == pf) ? Vec3(-Reff * std::cos(th), y, Reff * std::sin(th)) : Vec3(Reff * std::cos(th), y, -Reff * std::sin(th));
            cs.setVertexPosition(cs.firstVertex(p) + static_cast<int>(i), q);
        }
    for (int f = 0; f < frames; ++f) cs.step();

    RingResult r;
    r.Reff = Reff;
    const int nx = std::max(1, static_cast<int>(std::lround(s.W / s.h)));
    r.n = 2 * nx;
    const std::vector<Real> ps = cs.vertexSoftPressure(), pr = cs.vertexRigidPressure(), pt = cs.vertexContactPressure();
    const std::vector<Real> area = cs.vertexAreas();
    int cnt = 0;
    Real asum = 0;
    for (int i = 0; i < cs.vertexCount(); ++i) {
        const Vec3& q = cs.positions()[i];
        if (std::fabs(q.y) > 0.02) continue;  // middle band only: free hems relax the ring
        const Real rad = std::sqrt(q.x * q.x + q.z * q.z);
        r.delta += Reff - rad;
        r.pSoft += ps[i] * area[i]; r.pRigid += pr[i] * area[i]; r.pTotal += pt[i] * area[i];  // area-weighted: see vertexAreas()
        asum += area[i];
        ++cnt;
    }
    r.delta /= cnt; r.pSoft /= asum; r.pRigid /= asum; r.pTotal /= asum;
    r.ke = cs.kineticEnergy();
    r.gap = cs.maxSeamGap();
    if (std::getenv("FL_SKIN_DEBUG"))
        for (int i = 0; i < cs.vertexCount(); ++i) {
            const Vec3& q = cs.positions()[i];
            if (std::fabs(q.y) < 0.004)
                std::printf("    v%3d theta %7.1f r %.5f soft %8.1f rigid %8.1f\n", i, std::atan2(q.z, q.x) * 57.2958, std::sqrt(q.x * q.x + q.z * q.z), ps[i], pr[i]);
        }
    if (triP) *triP = cs.triangleContactPressure();
    return r;
}

static void testRing() {
    std::printf("-- ring on a cylinder (analytic Laplace equilibrium)\n");
    RingSetup s;
    const int n = 2 * static_cast<int>(std::lround(s.W / s.h));
    const Real Reff = s.R + 0.0015 + 0.00025;
    const Real dExp = expectedDelta(s, n, Reff);
    const Real rExp = Reff - dExp;
    const Real epsExp = n * 2 * rExp * std::sin(kPi / n) / (2 * s.W) - 1;
    const Real pExp = epsExp * s.Ks / rExp;
    std::printf("  expected: delta %.4f mm, strain %.3f %%, T %.2f N/m, p = T/r = %.1f Pa (k*delta = %.1f Pa)\n", dExp * 1e3, epsExp * 100,
                epsExp * s.Ks, pExp, s.k * dExp);
    std::vector<Real> triP;
    const RingResult r = runRing(s, 220, &triP);
    std::printf("  analytic collider: delta %.4f mm (err %.2f %%), p_soft %.1f Pa (err %.2f %%), p_rigid %.2f Pa, KE %.1e, seam gap %.3f mm\n",
                r.delta * 1e3, 100 * (r.delta - dExp) / dExp, r.pSoft, 100 * (r.pSoft - pExp) / pExp, r.pRigid, r.ke, r.gap * 1e3);
    CHECK_MSG(std::fabs(r.delta - dExp) < 0.05 * dExp, "delta %.4f mm vs expected %.4f mm", r.delta * 1e3, dExp * 1e3);
    CHECK_MSG(std::fabs(r.pSoft - pExp) < 0.05 * pExp, "pressure %.1f Pa vs expected %.1f Pa", r.pSoft, pExp);
    CHECK_MSG(std::fabs(r.pSoft - s.k * r.delta) < 1e-6 * pExp, "pressure is not k*delta");
    CHECK_MSG(r.pRigid < 0.02 * pExp, "rigid hold carries %.1f Pa inside the soft layer", r.pRigid);
    CHECK(r.delta < s.dmax);
    {
        Real m = 0;
        for (Real v : triP) m += v;
        m /= std::max<size_t>(triP.size(), 1);
        std::printf("  triangle pressure mean over all triangles %.1f Pa (hem rows included)\n", m);
        CHECK(m > 0.5 * pExp && m < 1.1 * pExp);
    }

    // same ring, rigid (k = 0): the rigid pressure estimate from the projection force against Laplace with the
    // rigid strain (delta = 0)
    {
        RingSetup rs = s;
        rs.k = 0; rs.dmax = 0;
        const RingResult rr = runRing(rs, 220);
        const Real eps0 = n * 2 * Reff * std::sin(kPi / n) / (2 * s.W) - 1;
        const Real p0 = eps0 * s.Ks / Reff;
        std::printf("  rigid ring: delta %.4f mm, strain %.3f %%, Laplace p = %.1f Pa, rigid-force estimate %.1f Pa (%.1f %%)\n", rr.delta * 1e3,
                    eps0 * 100, p0, rr.pRigid, 100 * (rr.pRigid - p0) / p0);
        CHECK(std::fabs(rr.delta) < 1e-4);
        CHECK(rr.pSoft == 0);
        CHECK_MSG(std::fabs(rr.pRigid - p0) < 0.10 * p0, "rigid pressure estimate %.1f vs Laplace %.1f", rr.pRigid, p0);
    }
    // a very stiff skin approaches the rigid limit
    {
        RingSetup hs = s;
        hs.k = 1.5e7;
        const Real de = expectedDelta(hs, n, Reff);
        const RingResult rh = runRing(hs, 220);
        std::printf("  stiff skin k=1.5e7: delta %.4f mm (expected %.4f mm)\n", rh.delta * 1e3, de * 1e3);
        // with a checkerboard mesh the vertex depth alternates (F_i / (k A_i)), so the plain mean sits a little above
        // the scalar solution; only require the rigid limit (well below a tenth of a millimetre, close to expected)
        CHECK_MSG(rh.delta > 0 && std::fabs(rh.delta - de) < 0.15 * de, "stiff skin delta %.5f vs %.5f mm", rh.delta * 1e3, de * 1e3);
    }
    // closed-mesh collider: same ring around a baked cylinder mesh
    {
        RingSetup gs = s;
        gs.useGrid = true;
        const RingResult rg = runRing(gs, 220);
        std::printf("  baked-mesh collider (h %.0f mm): delta %.4f mm (err %.2f %%), p_soft %.1f Pa (err %.2f %%)\n", gs.gridH * 1e3, rg.delta * 1e3,
                    100 * (rg.delta - dExp) / dExp, rg.pSoft, 100 * (rg.pSoft - pExp) / pExp);
        CHECK_MSG(std::fabs(rg.delta - dExp) < 0.05 * dExp, "grid delta %.4f mm vs %.4f mm", rg.delta * 1e3, dExp * 1e3);
        CHECK_MSG(std::fabs(rg.pSoft - pExp) < 0.05 * pExp, "grid pressure %.1f vs %.1f Pa", rg.pSoft, pExp);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Part 2: per-collider / per-region stiffness
// ---------------------------------------------------------------------------------------------------------------
static void testScales() {
    std::printf("-- per-region stiffness lookups\n");
    {   // UnionCollider: nearest part's scale
        auto u = std::make_shared<UnionCollider>();
        auto a = std::make_shared<SphereCollider>(Vec3(-1, 0, 0), 0.5);
        auto b = std::make_shared<SphereCollider>(Vec3(1, 0, 0), 0.5);
        a->setSkinScale(2.0);
        b->setSkinScale(0.25);
        u->add(a); u->add(b);
        CHECK(u->skinScale(Vec3(-0.6, 0, 0)) == 2.0);
        CHECK(u->skinScale(Vec3(0.6, 0, 0)) == 0.25);
        CHECK(SphereCollider(Vec3(0, 0, 0), 1).skinScale(Vec3(1, 0, 0)) == 1.0);
    }
    {   // RegionMap on a refined box mesh: upper half label 3, lower half label 5
        std::vector<Vec3> V;
        Tris T;
        const Real hx = 0.1, hy = 0.2, hz = 0.1;
        for (int i = 0; i < 8; ++i) V.push_back({(i & 1) ? hx : -hx, (i & 2) ? hy : -hy, (i & 4) ? hz : -hz});
        const int F[12][3] = {{0, 2, 1}, {1, 2, 3}, {4, 5, 6}, {5, 7, 6}, {0, 1, 4}, {1, 5, 4}, {2, 6, 3}, {3, 6, 7}, {0, 4, 2}, {2, 4, 6}, {1, 3, 5}, {3, 7, 5}};
        for (auto& f : F) T.push_back({f[0], f[1], f[2]});
        Real vol = 0;
        for (const auto& t : T) vol += dot(V[t[0]], cross(V[t[1]], V[t[2]])) / 6;
        if (vol < 0) for (auto& t : T) std::swap(t[1], t[2]);
        Tris T2;
        std::vector<Vec3> V2 = V;
        for (int level = 0; level < 2; ++level) {
            T2.clear();
            for (const auto& t : T) {
                const int a = t[0], b = t[1], c = t[2];
                const int ab = static_cast<int>(V2.size()); V2.push_back((V2[a] + V2[b]) * 0.5);
                const int bc = static_cast<int>(V2.size()); V2.push_back((V2[b] + V2[c]) * 0.5);
                const int ca = static_cast<int>(V2.size()); V2.push_back((V2[c] + V2[a]) * 0.5);
                T2.push_back({a, ab, ca}); T2.push_back({b, bc, ab}); T2.push_back({c, ca, bc}); T2.push_back({ab, bc, ca});
            }
            T = T2;
        }
        std::vector<std::uint8_t> lab;
        for (const auto& t : T2) lab.push_back((V2[t[0]].y + V2[t[1]].y + V2[t[2]].y) / 3 > 0 ? 3 : 5);
        auto map = std::make_shared<RegionMap>();
        CHECK(map->bake(V2, T2, lab, 0.01, 0.05));
        std::printf("  region map: label above %d / below %d (expected 3 / 5), far point -> %d (255 = unknown)\n", map->label(Vec3(hx + 0.01, 0.12, 0)),
                    map->label(Vec3(hx + 0.01, -0.12, 0)), map->label(Vec3(1, 1, 1)));
        CHECK(map->label(Vec3(hx + 0.01, 0.12, 0)) == 3);
        CHECK(map->label(Vec3(hx + 0.01, -0.12, 0)) == 5);
        CHECK(map->label(Vec3(-hx - 0.01, 0.15, 0.03)) == 3);
        CHECK(map->label(Vec3(1, 1, 1)) == RegionMap::kUnknown);
        auto grid = std::make_shared<SdfGrid>();
        CHECK(grid->bake(V2, T2));
        GridCollider gc(grid);
        gc.setRegionStiffness(map, {1, 1, 1, 0.5, 1, 4.0});
        gc.setSkinScale(0.1);
        CHECK(gc.skinScale(Vec3(hx + 0.01, 0.12, 0)) == 0.5);
        CHECK(gc.skinScale(Vec3(hx + 0.01, -0.12, 0)) == 4.0);
        CHECK(gc.skinScale(Vec3(1, 1, 1)) == 0.1);  // unknown label: collider default
    }
    {   // the solver honours the scale: same load on a soft and a 4x stiffer cylinder top
        auto u = std::make_shared<UnionCollider>();
        auto soft = std::make_shared<CylinderCollider>(Vec3(-0.5, 0, 0), 0.2, 0.2);
        auto stiff = std::make_shared<CylinderCollider>(Vec3(0.5, 0, 0), 0.2, 0.2);
        stiff->setSkinScale(4.0);
        u->add(soft); u->add(stiff);
        FabricMaterial mat = ringMaterial(3000.0);
        SolverParams sp;
        sp.skinStiffness = 4e4;
        sp.skinMaxDepth = 0.02;
        sp.selfCollision = false;
        sp.substeps = 2;
        ClothSolver cs(sp);
        cs.setCollider(u);
        PanelMesh m = meshRect(0.2, 0.2, 0.02);
        PanelPlacement pl, pr;
        pl.origin = Vec3(-0.6, 0.2 + 0.002, -0.1); pl.ex = Vec3(1, 0, 0); pl.ey = Vec3(0, 0, 1);
        pr.origin = Vec3(0.4, 0.2 + 0.002, -0.1); pr.ex = Vec3(1, 0, 0); pr.ey = Vec3(0, 0, 1);
        const int a = cs.addPanel(m, mat, pl, 0, "a"), b = cs.addPanel(m, mat, pr, 0, "b");
        for (int f = 0; f < 200; ++f) cs.step();
        auto meanY = [&](int p) {
            Real y = 0; int c = 0;
            for (int i = 0; i < cs.vertexCountOfPanel(p); ++i) { y += cs.positions()[cs.firstVertex(p) + i].y; ++c; }
            return y / c;
        };
        const Real radius = sp.contactOffset + 0.5 * mat.thickness;
        const Real da = 0.2 + radius - meanY(a), db = 0.2 + radius - meanY(b);
        const Real sigma = mat.density * 9.81;  // N/m^2
        std::printf("  sheet on soft/stiff top faces: delta %.4f / %.4f mm, expected sigma/(k s) = %.4f / %.4f mm\n", da * 1e3, db * 1e3,
                    sigma / sp.skinStiffness * 1e3, sigma / (4 * sp.skinStiffness) * 1e3);
        CHECK_MSG(std::fabs(da - sigma / sp.skinStiffness) < 0.06 * sigma / sp.skinStiffness, "soft half delta %.4f mm", da * 1e3);
        CHECK_MSG(std::fabs(db - sigma / (4 * sp.skinStiffness)) < 0.06 * sigma / (4 * sp.skinStiffness), "stiff half delta %.4f mm", db * 1e3);
        const std::vector<Real> ps = cs.vertexSoftPressure();
        Real pa = 0, pb = 0;
        for (int i = 0; i < cs.vertexCountOfPanel(a); ++i) { pa += ps[cs.firstVertex(a) + i]; pb += ps[cs.firstVertex(b) + i]; }
        pa /= cs.vertexCountOfPanel(a); pb /= cs.vertexCountOfPanel(b);
        std::printf("  mean pressure %.3f / %.3f Pa, weight per area %.3f Pa\n", pa, pb, sigma);
        CHECK(std::fabs(pa - sigma) < 0.06 * sigma && std::fabs(pb - sigma) < 0.06 * sigma);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Part 3: tube too small for a convex body with a narrow waist and wide hips, standing on it.
// ---------------------------------------------------------------------------------------------------------------
static Real bodyRadius(Real y) {  // lathe profile: hips at y = 0, waist at y = 0.22, chest at y = 0.44
    const Real e = (y - 0.1) / 0.4;
    return (0.145 + 0.025 * std::cos(2 * kPi * y / 0.44)) * std::sqrt(std::max(0.0, 1 - e * e));
}

static std::shared_ptr<Collider> makeLatheBody() {
    const int nu = 72, nv = 80;
    std::vector<Vec3> V;
    Tris T;
    V.push_back({0, 0.5, 0});
    for (int j = 1; j < nv; ++j) {
        const Real y = 0.5 - 0.8 * j / nv;
        const Real r = bodyRadius(y);
        for (int i = 0; i < nu; ++i) {
            const Real a = 2 * kPi * i / nu;
            V.push_back({r * std::cos(a), y, r * std::sin(a)});
        }
    }
    V.push_back({0, -0.3, 0});
    const int last = static_cast<int>(V.size()) - 1;
    auto id = [&](int j, int i) { return 1 + (j - 1) * nu + (i % nu); };
    for (int i = 0; i < nu; ++i) T.push_back({0, id(1, i + 1), id(1, i)});
    for (int j = 1; j + 1 < nv; ++j)
        for (int i = 0; i < nu; ++i) {
            T.push_back({id(j, i), id(j, i + 1), id(j + 1, i)});
            T.push_back({id(j, i + 1), id(j + 1, i + 1), id(j + 1, i)});
        }
    for (int i = 0; i < nu; ++i) T.push_back({last, id(nv - 1, i), id(nv - 1, i + 1)});
    Real vol = 0;
    for (const auto& t : T) vol += dot(V[t[0]], cross(V[t[1]], V[t[2]])) / 6;
    if (vol < 0) for (auto& t : T) std::swap(t[1], t[2]);
    auto grid = std::make_shared<SdfGrid>();
    CHECK(grid->bake(V, T));
    return std::make_shared<GridCollider>(grid);
}

struct StandResult {
    Real stepLast10 = 0;   // mean vertex step per frame over the last 10 frames (m)
    Real stepMax = 0;      // largest per-frame mean step in that window
    Real ke = 0, minClr = 0, hemY = 0, pMax = 0, pMean = 0, gap = 0;
    int lsFail = 0;
    std::vector<Real> history;  // per-frame mean step, whole run
};

static StandResult runStanding(Real k, Real dmax, Real C, int frames, bool selfCollision = true) {
    auto body = makeLatheBody();
    auto mats = loadMaterialsFromFile(FL_MATERIALS_PATH);
    const FabricMaterial* fab = findMaterial(mats, "cotton_poplin");
    CHECK(fab != nullptr);
    SolverParams sp;
    sp.substeps = 4;
    sp.tensionStiffness = true;
    sp.selfCollision = selfCollision;
    sp.skinStiffness = k;
    sp.skinMaxDepth = dmax;
    ClothSolver cs(sp);
    cs.setCollider(body);
    const Real W = C / 2, H = 0.20, h = 0.02, y0 = 0.10;  // top edge at y0 + H, hem at y0
    PanelMesh mesh = meshRect(W, H, h);
    PanelPlacement a, b;
    a.origin = Vec3(-W / 2, y0, 0.2); a.ex = Vec3(1, 0, 0);  a.ey = Vec3(0, 1, 0);
    b.origin = Vec3(+W / 2, y0, -0.2); b.ex = Vec3(-1, 0, 0); b.ey = Vec3(0, 1, 0);
    const int pf = cs.addPanel(mesh, *fab, a, kPi / 2, "front");
    const int pb = cs.addPanel(mesh, *fab, b, kPi / 2, "back");
    cs.addSeam(pf, 1, pb, 3, true);
    cs.addSeam(pf, 3, pb, 1, true);
    const Real off = sp.contactOffset + 0.5 * fab->thickness;
    // pre-wrap on the body surface at each row's height (stretched where the body is wider than C/2pi)
    for (int p : {pf, pb})
        for (size_t i = 0; i < mesh.verts.size(); ++i) {
            const Real th = kPi * mesh.verts[i].x / W, y = y0 + mesh.verts[i].y;
            const Real R = std::max(bodyRadius(y) + off, C / (2 * kPi));
            const Vec3 q = (p == pf) ? Vec3(-R * std::cos(th), y, R * std::sin(th)) : Vec3(R * std::cos(th), y, -R * std::sin(th));
            cs.setVertexPosition(cs.firstVertex(p) + static_cast<int>(i), q);
        }
    StandResult r;
    std::vector<Vec3> prev = cs.positions();
    for (int f = 0; f < frames; ++f) {
        cs.step();
        Real s = 0;
        for (int i = 0; i < cs.vertexCount(); ++i) s += length(cs.positions()[i] - prev[i]);
        s /= cs.vertexCount();
        r.history.push_back(s);
        prev = cs.positions();
    }
    const int n = static_cast<int>(r.history.size());
    for (int i = n - 10; i < n; ++i) { r.stepLast10 += r.history[i] / 10; r.stepMax = std::max(r.stepMax, r.history[i]); }
    r.ke = cs.kineticEnergy();
    r.minClr = cs.minBodyClearance();
    r.lsFail = cs.lineSearchFailures();
    Real hy = 0; int c = 0;
    for (int p : {pf, pb}) for (int v : cs.edgeChain(p, 0)) { hy += cs.positions()[v].y; ++c; }
    r.hemY = hy / c;
    const std::vector<Real> pt = cs.vertexContactPressure();
    for (Real x : pt) { r.pMax = std::max(r.pMax, x); r.pMean += x / pt.size(); }
    r.gap = cs.maxSeamGap();
    return r;
}

static void testStanding() {
    std::printf("-- tube too small for a narrow-waist / wide-hip body, standing on it\n");
    const Real C = 0.98;
    const int frames = 400;
    const StandResult rigid = runStanding(0, 0, C, frames);
    const StandResult soft = runStanding(2.0e5, 0.008, C, frames);
    std::printf("  rigid: step(last 10) %.4f mm/frame (max %.4f), KE %.1e J, hem y %.1f mm, min clearance %.2f mm, ls-fail %d, p mean %.1f max %.1f Pa\n",
                rigid.stepLast10 * 1e3, rigid.stepMax * 1e3, rigid.ke, rigid.hemY * 1e3, rigid.minClr * 1e3, rigid.lsFail, rigid.pMean, rigid.pMax);
    std::printf("  soft : step(last 10) %.4f mm/frame (max %.4f), KE %.1e J, hem y %.1f mm, min clearance %.2f mm, ls-fail %d, p mean %.1f max %.1f Pa\n",
                soft.stepLast10 * 1e3, soft.stepMax * 1e3, soft.ke, soft.hemY * 1e3, soft.minClr * 1e3, soft.lsFail, soft.pMean, soft.pMax);
    for (int f : {50, 100, 200, 300, 399}) std::printf("    frame %3d: rigid step %.4f mm  soft step %.4f mm\n", f, rigid.history[f] * 1e3, soft.history[f] * 1e3);
    CHECK_MSG(soft.stepLast10 < 1e-6, "soft layer does not settle: %.4f mm/frame", soft.stepLast10 * 1e3);
    CHECK(soft.ke < 1e-5);
    CHECK(soft.lsFail == 0);
}

// ---------------------------------------------------------------------------------------------------------------
// Part 4: disabled layer = old behaviour
// ---------------------------------------------------------------------------------------------------------------
static std::vector<Vec3> runDisabled(Real k, Real dmax) {
    auto mats = loadMaterialsFromFile(FL_MATERIALS_PATH);
    const FabricMaterial* fab = findMaterial(mats, "cotton_poplin");
    auto body = std::make_shared<EllipsoidCollider>(Vec3(0, 0, 0), Vec3(0.17, 0.35, 0.11));
    SolverParams sp;
    sp.substeps = 4;
    sp.skinStiffness = k;
    sp.skinMaxDepth = dmax;
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
    for (int f = 0; f < 120; ++f) cs.step();
    return cs.positions();
}

static void testDisabled() {
    std::printf("-- disabled layer is the old rigid behaviour\n");
    const auto base = runDisabled(0, 0);
    const auto a = runDisabled(1e5, 0);   // stiffness without a depth: off
    const auto b = runDisabled(0, 0.01);  // depth without stiffness: off
    Real d1 = 0, d2 = 0;
    for (size_t i = 0; i < base.size(); ++i) { d1 = std::max(d1, length(base[i] - a[i])); d2 = std::max(d2, length(base[i] - b[i])); }
    std::printf("  max position difference: k>0,dmax=0: %.2e m; k=0,dmax>0: %.2e m\n", d1, d2);
    CHECK(d1 == 0 && d2 == 0);
    const auto c = runDisabled(1e5, 0.01);  // sanity: with the layer on the result does differ
    Real d3 = 0;
    for (size_t i = 0; i < base.size(); ++i) d3 = std::max(d3, length(base[i] - c[i]));
    std::printf("  with the layer on (k=1e5, dmax=10 mm) the drop differs by up to %.2e m\n", d3);
    CHECK(d3 > 1e-5);
}

int main(int argc, char** argv) {
    // optional: name a part to run only that (ring | scales | standing | disabled); no argument runs all
    const std::string only = argc > 1 ? argv[1] : "";
    if (only.empty() || only == "disabled") testDisabled();
    if (only.empty() || only == "scales") testScales();
    if (only.empty() || only == "ring") testRing();
    if (only.empty() || only == "standing") testStanding();
    FINISH();
}
