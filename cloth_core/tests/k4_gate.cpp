// K4 real-mesh gate (not in ctest by default): the incremental collider against the full re-bake on the real
// MakeHuman mannequin over a walk cycle. usage: k4_gate <asset dir> [frames] [frame stride]
//   asset dir: body.obj + walk.bin as written by `node tools/export_godot_assets.js --frames 64 --out DIR average`
//   (the committed godot/assets/average has 24 frames).
// Exit status 0 when the accuracy targets hold, 1 when they do not, 2 when the assets are missing.
#include <algorithm>
#include <cstdio>
#include <string>

#include "k4_assets.hpp"
#include "k4_compare.hpp"

using namespace fl;

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "godot/assets/average";
    std::vector<Vec3> V;
    k4::Tris T;
    std::vector<std::vector<Vec3>> F;
    if (!k4::loadObj(dir + "/body.obj", V, T) || !k4::loadWalk(dir + "/walk.bin", V, F)) { std::printf("assets missing in %s: skipped\n", dir.c_str()); return 2; }
    const int nf = argc > 2 ? std::atoi(argv[2]) : static_cast<int>(F.size());
    const int fstep = argc > 3 ? std::max(1, std::atoi(argv[3])) : 1;
    std::printf("real mannequin: %zu verts, %zu tris; %d of %zu walk frames (every %d)\n", V.size(), T.size(), nf, F.size(), fstep);

    // optional garment-like region by height fraction of the standing body: K4_YMIN / K4_YMAX (default 0..1 = whole body)
    double yFrac0 = 0, yFrac1 = 1;
    if (const char* e = std::getenv("K4_YMIN")) yFrac0 = std::atof(e);
    if (const char* e = std::getenv("K4_YMAX")) yFrac1 = std::atof(e);
    double ylo = 1e30, yhi = -1e30;
    for (const Vec3& v : V) { ylo = std::min(ylo, v.y); yhi = std::max(yhi, v.y); }
    // skinning strain of a vertex: largest relative change of an incident edge length against the standing pose (large at joints / bends)
    auto strainOf = [&](const std::vector<Vec3>& P) {
        std::vector<float> st(V.size(), 0.f);
        for (const auto& t : T)
            for (int e = 0; e < 3; ++e) {
                const int a = t[e], b = t[(e + 1) % 3];
                const double l0 = length(V[static_cast<size_t>(a)] - V[static_cast<size_t>(b)]), l1 = length(P[static_cast<size_t>(a)] - P[static_cast<size_t>(b)]);
                const float s = static_cast<float>(std::fabs(l1 - l0) / std::max(l0, 1e-9));
                st[static_cast<size_t>(a)] = std::max(st[static_cast<size_t>(a)], s);
                st[static_cast<size_t>(b)] = std::max(st[static_cast<size_t>(b)], s);
            }
        return st;
    };
    k4::Acc joint, rigidish;
    long long nJoint = 0, nRig = 0;
    k4::Acc cloth, cloth20, shell, shellExactRef, shellExactLazy;
    double tRef = 0, tPrep = 0, tPre = 0, tEval = 0, tEvalRef = 0;
    long long bricksRef = 0, bricksLazy = 0, samplesLazy = 0, frames = 0, tests = 0, evalsDone = 0;
    SdfGridLazy lazy;
    SdfLazyParams lp;
    if (const char* e = std::getenv("K4_FAN")) lp.vertexFan = std::atoi(e);
    if (const char* e = std::getenv("K4_HASH")) lp.hashCell = std::atof(e);
    if (const char* e = std::getenv("K4_WARM")) lp.warmStart = std::atoi(e);
    size_t nCloth = 1;
    for (int f = 0; f < nf; f += fstep) {
        const auto& P = F[static_cast<size_t>(f) % F.size()];
        const auto nrm = k4::vertexNormals(P, T);
        std::vector<int> ids, idsAll;
        const auto QcAll = k4::clothProxy(P, nrm, 3, 0.0015, 0.015, &idsAll);
        std::vector<Vec3> Qc, Qj, Qr;
        const auto strain = strainOf(P);
        for (size_t q = 0; q < QcAll.size(); ++q) {
            const double fy = (V[static_cast<size_t>(idsAll[q])].y - ylo) / (yhi - ylo);
            if (fy < yFrac0 || fy > yFrac1) continue;
            Qc.push_back(QcAll[q]);
            (strain[static_cast<size_t>(idsAll[q])] > 0.03f ? Qj : Qr).push_back(QcAll[q]);
        }
        nCloth = Qc.size();
        const auto Qs = k4::randomShell(P, P.size(), 1000, 0.05, 1000u + static_cast<unsigned>(f));
        SdfGrid ref;
        double c0 = k4::cpuMs();
        ref.bake(P, T);
        tRef += k4::cpuMs() - c0;
        bricksRef += ref.stats().bricks;
        c0 = k4::cpuMs();
        lazy.setBody(P, T, lp);
        const double c1 = k4::cpuMs();
        lazy.prefetch(Qc.data(), static_cast<int>(Qc.size()));
        const double c2 = k4::cpuMs();
        tPrep += c1 - c0;
        tPre += c2 - c1;
        // steady-state evaluation cost (bricks computed): 5 passes over the cloth points
        for (int rep = 0; rep < 5; ++rep)
            for (const Vec3& q : Qc) { Vec3 g; evalsDone += lazy.eval(q, &g) > 1e30; }
        tEval += k4::cpuMs() - c2;
        c0 = k4::cpuMs();
        for (int rep = 0; rep < 5; ++rep)
            for (const Vec3& q : Qc) { Vec3 g; evalsDone += ref.eval(q, &g) > 1e30; }
        tEvalRef += k4::cpuMs() - c0;
        ++frames;
        bricksLazy += lazy.stats().fineBricks;
        samplesLazy += lazy.stats().coarseSamples;
        tests += lazy.stats().triangleTests;
        if (std::getenv("K4_DEBUG"))
            for (size_t q = 0; q < Qc.size(); ++q) {
                const Real dr = ref.eval(Qc[q], nullptr), dl = lazy.eval(Qc[q], nullptr);
                if (std::fabs(dr) > 1e-4 && (dr < 0) != (dl < 0))
                    std::printf("  flip f=%d yfrac %.3f ref %.2f mm lazy %.2f mm exact |d| %.2f mm\n", f, (Qc[q].y - ylo) / (yhi - ylo), dr * 1e3, dl * 1e3, lazy.exactDistance(Qc[q]) * 1e3);
                else if (std::fabs(dr - dl) > 1.0e-3) std::printf("  big f=%d yfrac %.3f ref %.2f lazy %.2f exact %.2f mm\n", f, (Qc[q].y - ylo) / (yhi - ylo), dr * 1e3, dl * 1e3, lazy.exactDistance(Qc[q]) * 1e3);
            }
        k4::accumulate(cloth, ref, lazy, Qc, 1.0);
        k4::accumulate(cloth20, ref, lazy, Qc, 0.020);
        k4::accumulate(joint, ref, lazy, Qj, 0.020);
        k4::accumulate(rigidish, ref, lazy, Qr, 0.020);
        k4::accumulate(shell, ref, lazy, Qs, 0.050);
        if (f % 8 == 0)  // exact distance by brute force on a subset
            for (size_t i = 0; i < Qs.size(); i += 4) {
                const Real ex = lazy.exactDistance(Qs[i]);
                const Real dr = ref.eval(Qs[i], nullptr), dl = lazy.eval(Qs[i], nullptr);
                shellExactRef.dev.push_back(static_cast<float>(std::fabs(std::fabs(dr) - ex) * 1e3));
                shellExactLazy.dev.push_back(static_cast<float>(std::fabs(std::fabs(dl) - ex) * 1e3));
                shellExactRef.n++; shellExactLazy.n++;
            }
    }
    std::printf("per frame (CPU ms, this machine is shared; ratios matter): full bake %.1f | lazy setBody %.1f + prefetch of %zu cloth points %.1f = %.1f | speedup %.1fx\n", tRef / frames,
                tPrep / frames, nCloth, tPre / frames, (tPrep + tPre) / frames, tRef / (tPrep + tPre));
    std::printf("  fine bricks: full %lld/frame, lazy %lld/frame; coarse samples lazy %lld/frame; triangle tests lazy %lld/frame\n", bricksRef / frames, bricksLazy / frames, samplesLazy / frames, tests / frames);
    std::printf("  setBody parts (last frame, wall ms): pose %.1f, vertex hash %.1f, rows fine %.1f, rows coarse %.1f\n", lazy.stats().msPose, lazy.stats().msHash, lazy.stats().msRowsFine, lazy.stats().msRowsCoarse);
    std::printf("  steady-state query cost (ns per eval with gradient): lazy %.0f, full grid %.0f\n", 1e6 * tEval / (5.0 * frames * static_cast<double>(std::max<size_t>(1, nCloth))), 1e6 * tEvalRef / (5.0 * frames * static_cast<double>(std::max<size_t>(1, nCloth))));
    std::printf("accuracy of the incremental field against the full re-bake of the same pose:\n");
    k4::report("cloth proxy, all", cloth);
    k4::report("cloth proxy, |d| <= 20 mm", cloth20);
    k4::report("random pts within 5 cm", shell);
    std::printf("split by skinning strain of the vertex the proxy point sits on (edge length change vs standing > 3%% = joint / bend):\n");
    k4::report("strain > 3% (joints, bends)", joint);
    k4::report("strain <= 3% (near rigid)", rigidish);
    std::printf("against the exact (brute force) distance, random points, |d|:\n");
    k4::report("full re-bake vs exact", shellExactRef);
    k4::report("incremental vs exact", shellExactLazy);
    const bool ok = k4::percentile(cloth20.dev, 0.95) < 0.3 && k4::percentile(cloth20.dev, 0.999) < 0.5 && k4::maxOf(cloth20.dev) < 2.5 && cloth.flips * 10000 <= cloth.n;
    std::printf("targets (cloth band: p95 < 0.3 mm, p99.9 < 0.5 mm, max < 2.5 mm, sign flips <= 0.01%%): %s\n", ok ? "MET" : "NOT MET");
    return ok ? 0 : 1;
}
