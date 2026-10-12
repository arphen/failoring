// Signed-distance grid (mesh collider): accuracy against analytic shapes, continuity, sign where meshes overlap,
// a cloth drop that must match the analytic collider, and lookup cost against a primitive union.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>

#include "check.hpp"
#include "failoring/cloth.hpp"
#include "failoring/collider.hpp"
#include "failoring/material.hpp"
#include "failoring/mesher.hpp"
#include "failoring/sdf_grid.hpp"

using namespace fl;
using Tris = std::vector<std::array<int, 3>>;

static Real signedVolume(const std::vector<Vec3>& V, const Tris& T) {
    Real v = 0;
    for (const auto& t : T) v += dot(V[t[0]], cross(V[t[1]], V[t[2]])) / 6;
    return v;
}
static void orientOutward(const std::vector<Vec3>& V, Tris& T) {
    if (signedVolume(V, T) < 0)
        for (auto& t : T) std::swap(t[1], t[2]);
}

static void icosphere(int sub, Real R, Vec3 c, std::vector<Vec3>& V, Tris& T) {
    const Real t = (1 + std::sqrt(5.0)) / 2;
    std::vector<Vec3> P = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                           {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    Tris F = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
              {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    for (auto& p : P) p = normalized(p);
    for (int s = 0; s < sub; ++s) {
        std::map<std::pair<int, int>, int> mid;
        auto m = [&](int a, int b) {
            const auto key = std::make_pair(std::min(a, b), std::max(a, b));
            auto it = mid.find(key);
            if (it != mid.end()) return it->second;
            P.push_back(normalized(P[a] + P[b]));
            return mid[key] = static_cast<int>(P.size()) - 1;
        };
        Tris G;
        for (const auto& f : F) {
            const int a = m(f[0], f[1]), b = m(f[1], f[2]), cc = m(f[2], f[0]);
            G.push_back({f[0], a, cc}); G.push_back({f[1], b, a}); G.push_back({f[2], cc, b}); G.push_back({a, b, cc});
        }
        F.swap(G);
    }
    const int base = static_cast<int>(V.size());
    for (const auto& p : P) V.push_back(c + p * R);
    for (const auto& f : F) T.push_back({base + f[0], base + f[1], base + f[2]});
}

static void ellipsoidMesh(Vec3 r, int nu, int nv, std::vector<Vec3>& V, Tris& T) {
    V.push_back({0, r.y, 0});
    for (int j = 1; j < nv; ++j)
        for (int i = 0; i < nu; ++i) {
            const Real th = kPi * j / nv, ph = 2 * kPi * i / nu;
            V.push_back({r.x * std::sin(th) * std::cos(ph), r.y * std::cos(th), r.z * std::sin(th) * std::sin(ph)});
        }
    V.push_back({0, -r.y, 0});
    const int last = static_cast<int>(V.size()) - 1;
    auto id = [&](int j, int i) { return 1 + (j - 1) * nu + (i % nu); };
    for (int i = 0; i < nu; ++i) T.push_back({0, id(1, i + 1), id(1, i)});
    for (int j = 1; j + 1 < nv; ++j)
        for (int i = 0; i < nu; ++i) {
            T.push_back({id(j, i), id(j, i + 1), id(j + 1, i)});
            T.push_back({id(j, i + 1), id(j + 1, i + 1), id(j + 1, i)});
        }
    for (int i = 0; i < nu; ++i) T.push_back({last, id(nv - 1, i), id(nv - 1, i + 1)});
    orientOutward(V, T);
}

static Vec3 randomDir(std::mt19937& rng) {
    std::normal_distribution<Real> n(0, 1);
    return normalized(Vec3(n(rng), n(rng), n(rng)));
}

int main() {
    std::mt19937 rng(7);
    std::uniform_real_distribution<Real> U(0, 1);

    // ---- 1. sphere: accuracy near the surface, sign and magnitude far away
    const Real R = 0.12;  // sphere radius (m)
    std::vector<Vec3> V;
    Tris T;
    icosphere(5, R, Vec3(0, 0, 0), V, T);
    orientOutward(V, T);
    auto grid = std::make_shared<SdfGrid>();
    SdfGridParams gp;
    CHECK(grid->bake(V, T, gp));
    const auto& st = grid->stats();
    std::printf("sphere: %d tris, %d bricks (%d fine samples), %d coarse samples, %.1f MB, bake %.1f ms (fine %.1f, coarse %.1f), %lld distance evals\n",
                st.triangles, st.bricks, st.fineSamples, st.coarseSamples, st.bytes / 1e6, st.msTotal, st.msFine, st.msCoarse, st.distanceEvals);
    Real maxErr = 0, maxAng = 0;
    for (int k = 0; k < 20000; ++k) {
        const Vec3 d = randomDir(rng);
        const Real off = (U(rng) * 2 - 1) * gp.band;
        Vec3 g;
        const Real phi = grid->eval(d * (R + off), &g);
        maxErr = std::max(maxErr, std::fabs(phi - off));
        maxAng = std::max(maxAng, std::acos(std::min(Real(1), dot(normalized(g), d))) * 180 / kPi);
    }
    std::printf("  within %.0f mm of the surface: max |error| %.3f mm, max normal error %.2f deg\n", gp.band * 1000, maxErr * 1000, maxAng);
    CHECK_MSG(maxErr < 0.3e-3, "distance error %.3f mm", maxErr * 1000);
    CHECK_MSG(maxAng < 2.0, "normal error %.2f deg", maxAng);
    Real deepErr = 0;
    bool signs = true;
    for (int k = 0; k < 2000; ++k) {
        const Vec3 d = randomDir(rng);
        const Real rr = U(rng) * (R - 0.02);  // deep inside
        const Real phi = grid->eval(d * rr, nullptr);
        signs = signs && phi < 0;
        deepErr = std::max(deepErr, std::fabs(phi - (rr - R)));
        const Real ro = R + 0.02 + U(rng) * 0.5;  // outside, also beyond the grid
        signs = signs && grid->eval(d * ro, nullptr) > 0;
    }
    std::printf("  deep inside (coarse level, smoothed at the centre): max |error| %.1f mm; signs %s\n", deepErr * 1000, signs ? "all correct" : "WRONG");
    CHECK(signs);
    // the coarse level smooths the kink of the distance field at the centre over about one coarse cell
    CHECK_MSG(deepErr < 0.75 * gp.h * gp.coarseRatio, "deep error %.1f mm", deepErr * 1000);

    // ---- 2. continuity along lines through the band (no jumps; value is ~1-Lipschitz)
    Real worstJump = 0;
    for (int k = 0; k < 200; ++k) {
        const Vec3 d = randomDir(rng), t = normalized(cross(d, randomDir(rng)));
        const Vec3 a = d * (R - 0.009) - t * 0.02, b = d * (R + 0.009) + t * 0.02;
        Real prev = grid->eval(a, nullptr);
        const int n = 2000;
        for (int s = 1; s <= n; ++s) {
            const Vec3 p = a + (b - a) * (Real(s) / n);
            const Real v = grid->eval(p, nullptr);
            worstJump = std::max(worstJump, std::fabs(v - prev) / length((b - a) / n));
            prev = v;
        }
    }
    std::printf("  continuity: largest |d phi / ds| along 200 lines = %.3f\n", worstJump);
    CHECK_MSG(worstJump < 1.2, "slope %.3f (discontinuity?)", worstJump);

    // ---- 3. ellipsoid mesh against the analytic ellipsoid collider
    {
        std::vector<Vec3> EV;
        Tris ET;
        const Vec3 r(0.17, 0.35, 0.11);
        ellipsoidMesh(r, 240, 120, EV, ET);
        SdfGrid eg;
        CHECK(eg.bake(EV, ET));
        EllipsoidCollider ec(Vec3(0, 0, 0), r);
        Real e = 0;
        for (int k = 0; k < 20000; ++k) {
            const Vec3 d = randomDir(rng);
            Vec3 q(d.x * r.x, d.y * r.y, d.z * r.z), n;
            ec.eval(q, n);
            const Real off = (U(rng) * 2 - 1) * 0.005;
            const Vec3 p = q + n * off;
            Vec3 n2;
            e = std::max(e, std::fabs(eg.eval(p, &n2) - ec.eval(p, n)));
        }
        std::printf("ellipsoid 0.17 x 0.35 x 0.11 m: %d tris, bake %.1f ms; within 5 mm of the surface max |grid - analytic| %.3f mm\n",
                    eg.stats().triangles, eg.stats().msTotal, e * 1000);
        CHECK_MSG(e < 0.4e-3, "ellipsoid deviation %.3f mm", e * 1000);
    }

    // ---- 4. overlapping closed meshes (like full thighs touching): points in the overlap are inside
    {
        std::vector<Vec3> OV;
        Tris OT;
        icosphere(4, 0.08, Vec3(-0.05, 0, 0), OV, OT);
        icosphere(4, 0.08, Vec3(0.05, 0, 0), OV, OT);
        SdfGrid og;
        CHECK(og.bake(OV, OT));
        int wrong = 0;
        for (int k = 0; k < 4000; ++k) {
            const Vec3 p(U(rng) * 0.3 - 0.15, U(rng) * 0.2 - 0.1, U(rng) * 0.2 - 0.1);
            const Real exact = std::min(length(p - Vec3(-0.05, 0, 0)), length(p - Vec3(0.05, 0, 0))) - 0.08;
            if (std::fabs(exact) < 0.004) continue;  // too close to call within the grid's smoothing
            if ((og.eval(p, nullptr) < 0) != (exact < 0)) ++wrong;
        }
        std::printf("overlapping spheres: %d wrong signs out of 4000 samples\n", wrong);
        CHECK(wrong == 0);
    }

    // ---- 5. cloth draped over the grid sphere behaves like the analytic sphere (centre pinned on the pole, so the
    //         configuration is stable; folds of the hanging part are compared through robust summaries)
    {
        FabricMaterial mat;  // defaults: a light woven
        struct Run { std::vector<Vec3> x; double ms; int ls; Real ke; };
        auto run = [&](std::shared_ptr<Collider> col) {
            SolverParams sp;
            sp.substeps = 2;
            sp.selfCollision = false;
            ClothSolver cs(sp);
            PanelMesh m = meshRect(0.3, 0.3, 0.02);
            PanelPlacement pl;
            pl.origin = Vec3(-0.15, R + 0.0025, -0.15);
            pl.ex = Vec3(1, 0, 0);
            pl.ey = Vec3(0, 0, 1);
            cs.addPanel(m, mat, pl, 0);
            cs.setCollider(col);
            int centre = 0;
            for (int i = 1; i < cs.vertexCount(); ++i)
                if (std::hypot(cs.positions()[i].x, cs.positions()[i].z) < std::hypot(cs.positions()[centre].x, cs.positions()[centre].z)) centre = i;
            cs.pinVertex(centre);
            const auto t0 = std::chrono::steady_clock::now();
            for (int f = 0; f < 150; ++f) cs.step();
            Run r;
            r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 150;
            r.ls = cs.lineSearchFailures();
            r.ke = cs.kineticEnergy();
            r.x = cs.positions();
            return r;
        };
        const Run A = run(std::make_shared<SphereCollider>(Vec3(0, 0, 0), R));
        const Run G = run(std::make_shared<GridCollider>(grid));
        SphereCollider exact(Vec3(0, 0, 0), R);
        auto summary = [&](const Run& r, Real& meanY, Real& clr, int& contact) {
            meanY = 0; clr = 1e9; contact = 0;
            for (const Vec3& p : r.x) { Vec3 n; const Real d = exact.eval(p, n); clr = std::min(clr, d); contact += d < 0.004; meanY += p.y / r.x.size(); }
        };
        Real yA, cA, yG, cG;
        int nA, nG;
        summary(A, yA, cA, nA);
        summary(G, yG, cG, nG);
        std::printf("cloth over a sphere (%zu verts, 150 frames): analytic %.1f ms/frame, %d line-search failures, KE %.1e J; "
                    "grid %.1f ms/frame, %d, KE %.1e J\n", A.x.size(), A.ms, A.ls, A.ke, G.ms, G.ls, G.ke);
        std::printf("  vertices within 4 mm of the sphere: analytic %d, grid %d; mean height %.1f vs %.1f mm; clearance to the true sphere %.2f vs %.2f mm\n",
                    nA, nG, yA * 1000, yG * 1000, cA * 1000, cG * 1000);
        CHECK_MSG(std::abs(nA - nG) <= std::max(3, nA / 10), "contact sets differ: %d vs %d", nA, nG);
        CHECK_MSG(std::fabs(yA - yG) < 0.004, "mean height differs by %.1f mm", std::fabs(yA - yG) * 1000);
        CHECK_MSG(cG > -0.0005, "grid run penetrates the sphere by %.2f mm", -cG * 1000);
    }

    // ---- 6. lookup cost: grid vs a union of 45 analytic primitives (the old mannequin)
    {
        auto uni = std::make_shared<UnionCollider>();
        for (int k = 0; k < 30; ++k) uni->add(std::make_shared<EllipsoidCollider>(Vec3(0, -0.1 + 0.007 * k, 0), Vec3(0.12, 0.05, 0.09)));
        for (int k = 0; k < 15; ++k) uni->add(std::make_shared<CapsuleCollider>(Vec3(0.1 + 0.01 * k, 0, 0), Vec3(0.1 + 0.01 * k, -0.2, 0.02), 0.04));
        GridCollider gc(grid);
        std::vector<Vec3> pts;
        for (int k = 0; k < 200000; ++k) pts.push_back(randomDir(rng) * (R + (U(rng) * 2 - 1) * 0.01));
        Real sink = 0;
        auto time = [&](const Collider& c) {
            const auto t0 = std::chrono::steady_clock::now();
            for (const Vec3& p : pts) { Vec3 n; sink += c.eval(p, n); }
            return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / pts.size();
        };
        const double tg = time(gc), tu = time(*uni);
        std::printf("lookup cost: grid %.0f ns, union of 45 primitives %.0f ns per query (checksum %.3f)\n", tg, tu, sink);
    }
    FINISH();
}
