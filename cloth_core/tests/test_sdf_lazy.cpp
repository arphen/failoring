// K4: the lazily evaluated signed-distance collider (SdfGridLazy) against a full re-bake (SdfGrid) of the same posed mesh.
//   1. static sphere: value/gradient against the analytic sphere and against the baked grid; C1 along a line
//   2. a synthetic body deformed non-rigidly over a pose sequence: deviation from the full re-bake and from the exact distance
//   3. the real mannequin over walk frames of godot/assets/average (skipped, with a message, when the assets are missing)
//   4. a garment dropped on a moving, deforming body: simulation with the lazy collider against the full re-bake
// Numbers are printed; the CHECK thresholds are set from measurements with margin (see docs/K4_LAZY_SDF.md).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>

#include "check.hpp"
#include "failoring/cloth.hpp"
#include "failoring/material.hpp"
#include "failoring/mesher.hpp"
#include "failoring/sdf_grid.hpp"
#include "failoring/sdf_lazy.hpp"
#include "k4_assets.hpp"
#include "k4_compare.hpp"

#ifndef FL_ASSET_DIR
#define FL_ASSET_DIR "godot/assets/average"
#endif

using namespace fl;
using Tris = std::vector<std::array<int, 3>>;

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

// Non-rigid pose of the unit-ish body at time s in [0,1]: translation, a bend of the upper half about z, a squash and
// a twist. Smooth in space, so the mesh stays a valid closed surface.
static Vec3 posePoint(const Vec3& p, Real s) {
    const Real a = 0.5 * std::sin(6.2831853 * s);       // bend angle (rad)
    const Real w = 1 / (1 + std::exp(-p.y / 0.02));     // 0 below the waist, 1 above
    const Real c = std::cos(a * w), sn = std::sin(a * w);
    Vec3 q(p.x * c - p.y * sn, p.x * sn + p.y * c, p.z);
    const Real tw = 0.6 * std::sin(6.2831853 * s + 1) * q.y;  // twist about y
    q = Vec3(q.x * std::cos(tw) - q.z * std::sin(tw), q.y, q.x * std::sin(tw) + q.z * std::cos(tw));
    q.y *= 1 - 0.1 * std::sin(6.2831853 * s * 2);
    return q + Vec3(0.12 * std::sin(6.2831853 * s), 0.03 * std::sin(12.566 * s), 0.05 * s);
}

int main() {
    std::mt19937 rng(11);
    std::uniform_real_distribution<Real> U(0, 1);

    // ---- 1. static sphere
    const Real R = 0.12;
    std::vector<Vec3> V;
    Tris T;
    icosphere(5, R, Vec3(0, 0, 0), V, T);
    SdfGrid grid;
    SdfGridLazy lazy;
    CHECK(grid.bake(V, T));
    CHECK(lazy.setBody(V, T));
    {
        Real maxSph = 0, maxGrid = 0, maxAng = 0;
        int flips = 0, n = 0;
        for (int k = 0; k < 20000; ++k) {
            const Vec3 d = normalized(Vec3(U(rng) - .5, U(rng) - .5, U(rng) - .5));
            const Vec3 p = d * (R + (U(rng) - .5) * 0.03);  // within 1.5 cm of the surface (farther out the baked grid answers from its coarse level)
            Vec3 gl, gg;
            const Real vl = lazy.eval(p, &gl), vg = grid.eval(p, &gg);
            maxSph = std::max(maxSph, std::fabs(vl - (length(p) - R)));
            maxGrid = std::max(maxGrid, std::fabs(vl - vg));
            maxAng = std::max(maxAng, std::acos(std::min(Real(1), dot(normalized(gl), d))) * 180 / kPi);
            flips += std::fabs(length(p) - R) > 5e-4 && (vl < 0) != (length(p) < R);  // facet sag of the mesh is 0.17 mm
            ++n;
        }
        std::printf("sphere: lazy vs analytic max %.3f mm (facet sag included), lazy vs grid max %.3f mm, normal error max %.2f deg, sign errors %d/%d, %lld fine bricks\n", maxSph * 1e3, maxGrid * 1e3, maxAng, flips, n,
                    lazy.stats().fineBricks);
        CHECK_MSG(maxGrid < 6e-4, "lazy differs from the grid by %.3f mm", maxGrid * 1e3);
        CHECK_MSG(maxSph < 1e-3, "lazy sphere error %.3f mm", maxSph * 1e3);
        CHECK(flips == 0);
        // C1: no jumps along a line through the near-surface band (between levels the field is allowed to jump, so stay within 3 cm)
        Real jump = 0, gjump = 0;
        Vec3 pg, pp;
        Real pv = 0;
        for (int i = 0; i <= 4000; ++i) {
            const Vec3 p = Vec3(R - 0.015 + 0.03 * i / 4000.0, 0.013, 0.007);
            Vec3 g;
            const Real v = lazy.eval(p, &g);
            if (i) { jump = std::max(jump, std::fabs(v - pv) - 0.03 / 4000 * 1.05); gjump = std::max(gjump, length(g - pg)); }
            pv = v; pg = g;
        }
        std::printf("sphere: line scan, max excess value jump %.2e m, max gradient step %.3f\n", jump, gjump);
        CHECK(jump < 2e-5);
        CHECK(gjump < 0.01);
    }

    // ---- 2. deforming synthetic body, pose sequence
    {
        std::vector<Vec3> V0;
        Tris T0;
        icosphere(5, 0.10, Vec3(0, 0, 0), V0, T0);
        for (auto& v : V0) v = Vec3(v.x * 0.8, v.y * 1.6, v.z * 0.6);  // an egg-ish torso
        k4::Acc cloth, shell;
        double tFull = 0, tLazy = 0;
        int frames = 0;
        SdfGridLazy lz;
        for (int f = 0; f < 8; ++f) {
            std::vector<Vec3> P(V0.size());
            for (size_t i = 0; i < V0.size(); ++i) P[i] = posePoint(V0[i], f / 8.0);
            const auto nrm = k4::vertexNormals(P, T0);
            const auto Qc = k4::clothProxy(P, nrm, 2, 0.0015, 0.015);
            const auto Qs = k4::randomShell(P, P.size(), 600, 0.05, 50u + f);
            SdfGrid ref;
            double c0 = k4::cpuMs();
            ref.bake(P, T0);
            tFull += k4::cpuMs() - c0;
            c0 = k4::cpuMs();
            lz.setBody(P, T0);
            lz.prefetch(Qc.data(), static_cast<int>(Qc.size()));
            tLazy += k4::cpuMs() - c0;
            k4::accumulate(cloth, ref, lz, Qc, 0.02);
            k4::accumulate(shell, ref, lz, Qs, 0.015);
            ++frames;
        }
        std::printf("deforming body (%zu tris, %d poses): CPU ms/frame full re-bake %.1f, lazy setBody+prefetch %.1f\n", T0.size(), frames, tFull / frames, tLazy / frames);
        k4::report("cloth band vs re-bake", cloth);
        k4::report("random pts, |d| <= 15 mm", shell);
        CHECK_MSG(k4::percentile(cloth.dev, 0.95) < 0.1, "p95 %.3f mm", k4::percentile(cloth.dev, 0.95));
        CHECK_MSG(k4::maxOf(cloth.dev) < 1.0, "max %.3f mm", k4::maxOf(cloth.dev));
        CHECK_MSG(cloth.flips == 0 && shell.flips == 0, "sign flips %lld / %lld", cloth.flips, shell.flips);
        CHECK_MSG(k4::percentile(cloth.ang, 0.95) < 1.0, "normal p95 %.2f deg", k4::percentile(cloth.ang, 0.95));
    }

    // ---- 3. real mannequin
    {
        std::vector<Vec3> Vm;
        Tris Tm;
        std::vector<std::vector<Vec3>> F;
        if (!k4::loadObj(std::string(FL_ASSET_DIR) + "/body.obj", Vm, Tm) || !k4::loadWalk(std::string(FL_ASSET_DIR) + "/walk.bin", Vm, F)) {
            std::printf("mannequin: assets not found in %s, skipped\n", FL_ASSET_DIR);
        } else {
            k4::Acc cloth, exactL, exactR;
            SdfGridLazy lz;
            double tFull = 0, tLazy = 0;
            int frames = 0;
            for (size_t f = 0; f < F.size(); f += 6) {
                const auto& P = F[f];
                const auto nrm = k4::vertexNormals(P, Tm);
                const auto Qc = k4::clothProxy(P, nrm, 3, 0.0015, 0.015);
                SdfGrid ref;
                double c0 = k4::cpuMs();
                ref.bake(P, Tm);
                tFull += k4::cpuMs() - c0;
                c0 = k4::cpuMs();
                lz.setBody(P, Tm);
                lz.prefetch(Qc.data(), static_cast<int>(Qc.size()));
                tLazy += k4::cpuMs() - c0;
                k4::accumulate(cloth, ref, lz, Qc, 0.02);
                for (size_t i = 0; i < Qc.size(); i += 40) {
                    exactL.dev.push_back(static_cast<float>(std::fabs(std::fabs(lz.eval(Qc[i], nullptr)) - lz.exactDistance(Qc[i])) * 1e3));
                    exactR.dev.push_back(static_cast<float>(std::fabs(std::fabs(ref.eval(Qc[i], nullptr)) - lz.exactDistance(Qc[i])) * 1e3));
                    ++exactL.n; ++exactR.n;
                }
                ++frames;
            }
            std::printf("mannequin (%zu tris, %d of %zu walk frames): CPU ms/frame full re-bake %.1f, lazy setBody+prefetch %.1f\n", Tm.size(), frames, F.size(), tFull / frames, tLazy / frames);
            k4::report("cloth band vs re-bake", cloth);
            k4::report("lazy vs exact distance", exactL);
            k4::report("full re-bake vs exact distance", exactR);
            CHECK_MSG(k4::percentile(cloth.dev, 0.95) < 0.3, "p95 %.3f mm", k4::percentile(cloth.dev, 0.95));
            CHECK_MSG(k4::maxOf(cloth.dev) < 3.0, "max %.3f mm", k4::maxOf(cloth.dev));
            CHECK_MSG(cloth.flips <= 1 + cloth.n / 5000, "sign flips %lld of %lld", cloth.flips, cloth.n);
            // both fields are B-spline smoothed (h^2/8 times the curvature, more on hands, feet and face): the lazy one must be no worse than the baked one
            CHECK_MSG(k4::percentile(exactL.dev, 0.95) <= k4::percentile(exactR.dev, 0.95) + 0.1, "lazy p95 %.3f vs baked %.3f mm", k4::percentile(exactL.dev, 0.95), k4::percentile(exactR.dev, 0.95));
        }
    }

    // ---- 4. garment on a moving, deforming body: lazy collider against the full re-bake
    {
        std::vector<Vec3> V0;
        Tris T0;
        icosphere(4, 0.08, Vec3(0, 0, 0), V0, T0);
        FabricMaterial mat;
        auto run = [&](bool useLazy, double& msCollider) {
            SolverParams sp;
            sp.substeps = 2;
            sp.selfCollision = false;
            ClothSolver cs(sp);
            PanelMesh m = meshRect(0.3, 0.3, 0.02);
            PanelPlacement pl;
            pl.origin = Vec3(-0.15, 0.08 + 0.003, -0.15);
            pl.ex = Vec3(1, 0, 0);
            pl.ey = Vec3(0, 0, 1);
            cs.addPanel(m, mat, pl, 0);
            auto lz = std::make_shared<SdfGridLazy>();
            std::vector<Vec3> P = V0, Pprev = V0;
            msCollider = 0;
            const int nFrames = 120;
            for (int f = 0; f < nFrames; ++f) {
                const Real s = std::max(0, f - 40) / 80.0;  // 40 frames to settle, then 80 frames of motion (0.12 m sway, bend, squash)
                for (size_t i = 0; i < V0.size(); ++i) { Pprev[i] = P[i]; P[i] = posePoint(V0[i], s * 0.5); }
                const auto t0 = std::chrono::steady_clock::now();
                std::shared_ptr<Collider> col;
                if (useLazy) {
                    lz->setBody(P, T0);
                    col = std::make_shared<LazyGridCollider>(lz);
                    // a garment-sized prefetch: the cloth vertices as they are now
                    lz->prefetch(cs.positions().data(), cs.vertexCount());
                } else {
                    auto g = std::make_shared<SdfGrid>();
                    g->bake(P, T0);
                    col = std::make_shared<GridCollider>(g);
                }
                msCollider += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                cs.setCollider(col);
                std::vector<Vec3> disp(static_cast<size_t>(cs.vertexCount()));
                for (int i = 0; i < cs.vertexCount(); ++i) {  // displacement of the closest body vertex over the frame
                    const Vec3 x = cs.positions()[static_cast<size_t>(i)];
                    size_t b = 0;
                    Real bd = 1e30;
                    for (size_t k = 0; k < P.size(); ++k) { const Real d = dot(P[k] - x, P[k] - x); if (d < bd) { bd = d; b = k; } }
                    disp[static_cast<size_t>(i)] = P[b] - Pprev[b];
                }
                cs.setColliderMotion(disp);
                cs.step();
            }
            msCollider /= nFrames;
            return std::make_pair(cs.positions(), cs.lineSearchFailures());
        };
        double msFull = 0, msLazy = 0;
        const auto A = run(false, msFull);
        const auto B = run(true, msLazy);
        double sum = 0, mx = 0;
        for (size_t i = 0; i < A.first.size(); ++i) { const double d = length(A.first[i] - B.first[i]); sum += d; mx = std::max(mx, d); }
        std::printf("garment on a moving body (%zu verts, 120 frames): final vertex difference lazy vs full re-bake: mean %.3f mm, max %.3f mm; line-search failures %d vs %d; collider time per frame (wall, includes prefetch) full %.1f ms, lazy %.1f ms\n",
                    A.first.size(), sum / A.first.size() * 1e3, mx * 1e3, A.second, B.second, msFull, msLazy);
        CHECK_MSG(sum / A.first.size() < 0.0005, "mean difference %.3f mm", sum / A.first.size() * 1e3);
        CHECK_MSG(mx < 0.0015, "max difference %.3f mm", mx * 1e3);
    }
    FINISH();
}
