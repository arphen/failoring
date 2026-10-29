// Foundations: material loading, mesher quality, bending-gradient correctness.
#include <cmath>
#include <cstdio>

#include "check.hpp"
#include "failoring/cloth.hpp"
#include "failoring/material.hpp"
#include "failoring/mesh_sizing.hpp"
#include "failoring/mesher.hpp"
#include "failoring/sparse_chol.hpp"
#include <chrono>
#include <random>

using namespace fl;

static Real meshArea(const PanelMesh& m) {
    Real a = 0;
    for (auto& t : m.tris) a += 0.5 * cross(m.verts[t[1]] - m.verts[t[0]], m.verts[t[2]] - m.verts[t[0]]);
    return a;
}
static Real minAngleDeg(const PanelMesh& m) {
    Real mn = 180;
    for (auto& t : m.tris)
        for (int k = 0; k < 3; ++k) {
            Vec2 a = m.verts[t[k]], b = m.verts[t[(k + 1) % 3]], c = m.verts[t[(k + 2) % 3]];
            Vec2 u = b - a, v = c - a;
            Real ang = std::acos(std::min(Real(1), std::max(Real(-1), dot(u, v) / (length(u) * length(v)))));
            mn = std::min(mn, ang * 180 / kPi);
        }
    return mn;
}

int main() {
    std::printf("[materials]\n");
    auto mats = loadMaterialsFromFile(FL_MATERIALS_PATH);
    CHECK(mats.size() == 10);
    for (auto& m : mats) {
        std::printf("  %-18s %-7s %.0f g/m2  B=%.1e/%.1e Nm  k=%.0f/%.0f N/m  G=%.0f\n", m.id.c_str(), m.category.c_str(), m.density * 1000,
                    m.bendWarp, m.bendWeft, m.tensileWarp, m.tensileWeft, m.shear);
        CHECK(m.density > 0.02 && m.density < 1.5);
        CHECK(m.bendWarp > 1e-7 && m.bendWarp < 1e-2);
    }
    CHECK(findMaterial(mats, "denim_12oz") != nullptr);
    CHECK(findMaterial(mats, "nope") == nullptr);
    bool threw = false;
    try { loadMaterialsFromString("{\"fabrics\":[{\"name\":\"no id\"}]}"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    std::printf("[mesher]\n");
    {
        PanelMesh r = meshRect(0.30, 0.20, 0.02);
        CHECK(r.tris.size() == 300);
        CHECK(std::fabs(meshArea(r) - 0.06) < 1e-12);
        CHECK(r.edgeChains.size() == 4 && r.edgeChains[0].size() == 16 && r.edgeChains[1].size() == 11);
    }
    {
        // a 14 cm square with a 24-point hole of radius 3 cm at its centre: the mesh covers the square minus the hole, every
        // hole segment is a triangle edge, and no triangle centroid lies inside the hole
        std::vector<Vec2> sq = {{0, 0}, {0.14, 0}, {0.14, 0.14}, {0, 0.14}};
        std::vector<Vec2> hole;
        const int nh = 24;
        const Real r = 0.03;
        for (int i = 0; i < nh; ++i) hole.push_back({0.07 + r * std::cos(2 * kPi * i / nh), 0.07 + r * std::sin(2 * kPi * i / nh)});
        PanelMesh hm = meshPolygon(sq, {hole}, 0.01);
        const Real holeArea = 0.5 * nh * r * r * std::sin(2 * kPi / nh);
        const Real want = 0.14 * 0.14 - holeArea;
        std::printf("  square with hole: %zu verts %zu tris, area err %.4f%%, boundary complete=%d\n", hm.verts.size(), hm.tris.size(),
                    100 * (meshArea(hm) - want) / want, (int)hm.boundaryComplete);
        CHECK(hm.boundaryComplete);
        CHECK(std::fabs(meshArea(hm) - want) < 2e-3 * want);
        bool inside = false;
        for (auto& t : hm.tris) {
            Vec2 c = (hm.verts[t[0]] + hm.verts[t[1]] + hm.verts[t[2]]) / 3.0;
            bool in = false;
            for (int i = 0, j = nh - 1; i < nh; j = i++) {
                if ((hole[i].y > c.y) != (hole[j].y > c.y) && c.x < (hole[j].x - hole[i].x) * (c.y - hole[i].y) / (hole[j].y - hole[i].y) + hole[i].x) in = !in;
            }
            inside = inside || in;
        }
        CHECK(!inside);
        // no holes: the overload is the plain mesher
        PanelMesh plain = meshPolygon(sq, {}, 0.01);
        CHECK(plain.tris.size() == meshPolygon(sq, 0.01).tris.size());
    }
    {
        PanelMesh d = meshDisc(0.15, 0.01);
        Real ex = kPi * 0.15 * 0.15;
        std::printf("  disc: %zu verts %zu tris, area err %.3f%%, min angle %.1f deg, boundary complete=%d\n", d.verts.size(), d.tris.size(),
                    100 * (meshArea(d) - ex) / ex, minAngleDeg(d), (int)d.boundaryComplete);
        CHECK(d.boundaryComplete);
        CHECK(std::fabs(meshArea(d) - ex) / ex < 0.01);
        CHECK(minAngleDeg(d) > 10);
    }
    {
        std::vector<Vec2> tee = {{0, 0},      {0.5, 0},     {0.5, 0.45},  {0.72, 0.40}, {0.78, 0.52}, {0.52, 0.66}, {0.36, 0.70},
                                 {0.25, 0.64}, {0.14, 0.70}, {-0.02, 0.66}, {-0.28, 0.52}, {-0.22, 0.40}, {0.0, 0.45}};
        PanelMesh m = meshPolygon(tee, 0.02);
        Real ex = polygonSignedArea(tee);
        std::printf("  tee: %zu verts %zu tris, area err %.3f%%, min angle %.1f deg, boundary complete=%d\n", m.verts.size(), m.tris.size(),
                    100 * (meshArea(m) - ex) / ex, minAngleDeg(m), (int)m.boundaryComplete);
        CHECK(m.boundaryComplete);
        CHECK(std::fabs(meshArea(m) - ex) / ex < 0.01);
        CHECK(minAngleDeg(m) > 8);
        for (auto& ch : m.edgeChains) CHECK(ch.size() >= 2);
        // clockwise input must work too
        std::vector<Vec2> cw(tee.rbegin(), tee.rend());
        PanelMesh m2 = meshPolygon(cw, 0.02);
        CHECK(m2.boundaryComplete);
        CHECK(std::fabs(meshArea(m2) - ex) / ex < 0.01);
    }

    std::printf("[bending gradient vs finite differences]\n");
    {
        const Vec3 base[4] = {{0, 0, 0}, {1, 0.1, 0}, {0.3, 0.8, 0.4}, {0.55, -0.7, 0.3}};
        Vec3 g[4];
        Real th = detail::hingeAngle(base[0], base[1], base[2], base[3], g);
        CHECK(th > 0);
        Real maxErr = 0, maxMag = 0;
        for (int p = 0; p < 4; ++p)
            for (int c = 0; c < 3; ++c) {
                Vec3 a[4] = {base[0], base[1], base[2], base[3]}, b[4] = {base[0], base[1], base[2], base[3]};
                const Real eps = 1e-6;
                Real* pa = (c == 0) ? &a[p].x : (c == 1) ? &a[p].y : &a[p].z;
                Real* pb = (c == 0) ? &b[p].x : (c == 1) ? &b[p].y : &b[p].z;
                *pa += eps; *pb -= eps;
                Real fd = (detail::hingeAngle(a[0], a[1], a[2], a[3], nullptr) - detail::hingeAngle(b[0], b[1], b[2], b[3], nullptr)) / (2 * eps);
                Real an = (c == 0) ? g[p].x : (c == 1) ? g[p].y : g[p].z;
                maxErr = std::max(maxErr, std::fabs(fd - an));
                maxMag = std::max(maxMag, std::fabs(fd));
            }
        std::printf("  theta=%.4f rad, max |analytic-FD| = %.2e (max grad %.2f)\n", th, maxErr, maxMag);
        CHECK_MSG(maxErr < 1e-5, "gradient mismatch %.3e", maxErr);

        // known angles: flat -> 0, right-angle fold -> pi/2
        CHECK(std::fabs(detail::hingeAngle({0, 0, 0}, {1, 0, 0}, {0.3, 1, 0}, {0.5, -1, 0}, nullptr)) < 1e-12);
        CHECK(std::fabs(detail::hingeAngle({0, 0, 0}, {1, 0, 0}, {0.3, 1, 0}, {0.5, 0, 1}, nullptr) - kPi / 2) < 1e-12);
    }
    std::printf("[mesh-resolution estimator vs paper Table 1 (Zhang et al. 2025)]\n");
    {
        struct Row { Real B, E, mm; };
        const Row rows[] = {{2.0e-6, 3.0e5, 7.83}, {2.0e-6, 8.0e5, 6.69}, {7.8e-7, 6.6e4, 8.58}, {2.0e-6, 1.1e5, 9.21},
                            {2.5e-6, 1.1e5, 9.55}, {1.6e-4, 1.3e5, 18.5}, {2.0e-5, 5.0e5, 10.5}, {2.6e-6, 2.0e6, 6.00},
                            {3.2e-7, 8.2e6, 3.61}, {4.0e-8, 1.6e7, 2.70}};
        Real worst = 0;
        for (const Row& r : rows) {
            const Real ours = 1000 * optimalEdgeLength(r.B, r.E);
            worst = std::max(worst, std::fabs(ours - r.mm) / r.mm);
        }
        std::printf("  10 rows, worst relative deviation from the published resolutions: %.2f%%\n", 100 * worst);
        CHECK_MSG(worst < 0.01, "estimator deviates %.2f%% from the paper", 100 * worst);
        CHECK(optimalEdgeLength(1e-12, 1e8) == kMinCellSize);  // clamps at the 2.5 mm floor
        for (auto& m : mats) {
            auto r = optimalResolution(m);
            CHECK(r.warp >= kMinCellSize && r.warp <= kMaxCellSize && r.weft >= kMinCellSize && r.weft <= kMaxCellSize);
        }
    }
    std::printf("[supernodal Cholesky vs reference]\n");
    {
        const int G = 40, N = G * G * 3;  // 3 dof per grid node, 9-point-ish coupling like the cloth system
        std::vector<int> rs(1, 0), col;
        std::vector<double> val;
        std::mt19937 rng(7);
        std::uniform_real_distribution<double> U(-1, 1);
        std::vector<std::vector<std::pair<int, double>>> rows(N);
        auto add = [&](int a, int b, double v) { rows[a].push_back({b, v}); if (a != b) rows[b].push_back({a, v}); };
        for (int y = 0; y < G; ++y)
            for (int x = 0; x < G; ++x)
                for (int dy = 0; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx) {
                        const int x2 = x + dx, y2 = y + dy;
                        if (x2 < 0 || x2 >= G || y2 >= G || (dy == 0 && dx <= 0)) continue;
                        for (int a = 0; a < 3; ++a)
                            for (int b = 0; b < 3; ++b) add(3 * (y * G + x) + a, 3 * (y2 * G + x2) + b, -0.1 * (1 + 0.3 * U(rng)) * ((a == b) ? 1.0 : 0.2));
                    }
        for (int i = 0; i < N; ++i) add(i, i, 12.0 + U(rng));
        std::vector<std::vector<std::pair<int, double>>> merged(N);
        for (int i = 0; i < N; ++i) {
            std::sort(rows[i].begin(), rows[i].end());
            for (auto& e : rows[i]) {
                if (!merged[i].empty() && merged[i].back().first == e.first) merged[i].back().second += e.second;
                else merged[i].push_back(e);
            }
            for (auto& e : merged[i]) { col.push_back(e.first); val.push_back(e.second); }
            rs.push_back(static_cast<int>(col.size()));
        }
        // ordering: nested dissection on the node grid (blocks of 3 dofs)
        std::vector<int> brs(1, 0), bcol;
        std::vector<double> xyz;
        for (int y = 0; y < G; ++y)
            for (int x = 0; x < G; ++x) {
                xyz.insert(xyz.end(), {double(x), double(y), 0.0});
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx) {
                        const int x2 = x + dx, y2 = y + dy;
                        if (x2 >= 0 && x2 < G && y2 >= 0 && y2 < G) bcol.push_back(y2 * G + x2);
                    }
                brs.push_back(static_cast<int>(bcol.size()));
            }
        auto bperm = nestedDissectionOrder(G * G, brs, bcol, xyz);
        std::vector<int> perm;
        for (int b : bperm) for (int a = 0; a < 3; ++a) perm.push_back(3 * b + a);
        SparseCholesky A, B;
        A.analyze(N, rs, col, perm);
        B.analyze(N, rs, col, perm);
        auto t0 = std::chrono::steady_clock::now();
        CHECK(A.factor(val));
        const double tSuper = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        t0 = std::chrono::steady_clock::now();
        CHECK(B.factorReference(val));
        const double tRef = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::vector<double> b(N), x1, x2;
        for (auto& v : b) v = U(rng);
        A.solve(b, x1);
        B.solve(b, x2);
        double dmax = 0, rmax = 0;
        for (int i = 0; i < N; ++i) dmax = std::max(dmax, std::fabs(x1[i] - x2[i]));
        for (int i = 0; i < N; ++i) {
            double r = -b[i];
            for (int e = rs[i]; e < rs[i + 1]; ++e) r += val[e] * x1[col[e]];
            rmax = std::max(rmax, std::fabs(r));
        }
        std::printf("  n=%d nnzL=%lld supernodes=%d  supernodal %.1f ms vs reference %.1f ms (%.1fx); |x1-x2|max=%.1e residual=%.1e\n", N, A.nnzL(),
                    A.supernodeCount(), tSuper * 1e3, tRef * 1e3, tRef / tSuper, dmax, rmax);
        CHECK_MSG(dmax < 1e-10 && rmax < 1e-10, "supernodal solve differs: %.2e / %.2e", dmax, rmax);
    }
    FINISH();
}
