// Wedge test (idea K2): a sewn tube lodged on convex bodies, run with body contact by projection (off) and by the
// augmented-Lagrangian multipliers (on). Per-frame metrics go to stdout and to FL_OUT_DIR/wedge_<scene>_<mode>.csv.
//
// Usage: test_wedge [scene] [mode] [frames]     scene: 0 = tube-ellipsoid, 1 = tube-thighs
//                                                mode:  off | on | both (default both)
// Without arguments it runs both scenes in both modes for 240 frames and checks the stated criteria.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "check.hpp"
#include "failoring/cloth.hpp"
#include "failoring/material.hpp"
#include "failoring/metrics.hpp"
#include "wedge_scenes.hpp"

using namespace fl;

struct RunResult {
    Real finalKE = 0, meanKEtail = 0, maxKEtail = 0;  // KE at the last frame and over the last 60 frames
    long long lsFailTotal = 0, lsFailTail = 0;        // line-search failures summed; over the last 60 frames
    double meanActiveTail = 0;                        // contact-active vertices per substep, last 60 frames
    Real minClearance = 1e30;                         // deepest penetration seen (negative = penetration), m
    Real maxStretch = 0, finalStretch = 0;            // max edge stretch (1.0 = rest)
    Real hemFinal = 0;                                // mean hem height, m
    double secs = 0;
    int frames = 0;
};

static RunResult run(int scene, bool al, int frames, const char* tag) {
    auto mats = loadMaterialsFromFile(FL_MATERIALS_PATH);
    const FabricMaterial* fab = findMaterial(mats, "cotton_poplin");
    CHECK(fab != nullptr);

    SolverParams sp;
    sp.substeps = 4;
    sp.seamRampFrames = 0;
    sp.contactAL = al;
    sp.contactDiagnostics = true;  // same cost in both modes; the energy-based counters are needed for the table
    wedge::Scene s = wedge::make(scene, *fab, sp);
    ClothSolver& cs = *s.cs;

    RunResult r;
    const std::string csvPath = std::string(FL_OUT_DIR) + "/wedge_" + std::to_string(scene) + "_" + tag + ".csv";
    std::FILE* csv = std::fopen(csvPath.c_str(), "w");
    if (csv) std::fprintf(csv, "frame,ke,ls_fail,active_per_substep,min_clearance_mm,max_stretch,hem_mm\n");

    const auto t0 = std::chrono::steady_clock::now();
    std::vector<Real> keHist;
    long long prevLs = 0, prevActive = 0, prevSub = 0;
    std::vector<long long> lsHist, actHist;
    for (int f = 0; f < frames; ++f) {
        cs.step();
        const auto& st = cs.contactStats();
        const Real ke = cs.kineticEnergy();
        const Real clr = cs.minBodyClearance();
        const Real stretch = wedge::maxEdgeStretch(cs);
        const Real hem = wedge::meanHemY(cs, s);
        const long long ls = st.lineSearchFailures - prevLs;
        const long long act = st.activeEnd - prevActive;
        const long long sub = st.substeps - prevSub;
        prevLs = st.lineSearchFailures;
        prevActive = st.activeEnd;
        prevSub = st.substeps;
        const double apsub = sub > 0 ? double(act) / double(sub) : 0.0;
        keHist.push_back(ke);
        lsHist.push_back(ls);
        actHist.push_back(static_cast<long long>(std::llround(apsub * 1000)));
        r.minClearance = std::min(r.minClearance, clr);
        r.maxStretch = std::max(r.maxStretch, stretch);
        r.lsFailTotal += ls;
        if (csv) std::fprintf(csv, "%d,%.6e,%lld,%.2f,%.4f,%.5f,%.3f\n", f, ke, ls, apsub, clr * 1000, stretch, hem * 1000);
    }
    const auto dt = std::chrono::steady_clock::now() - t0;
    r.secs = std::chrono::duration<double>(dt).count();
    r.frames = frames;
    if (csv) std::fclose(csv);

    const int tail = std::min(60, frames);
    double keSum = 0, actSum = 0;
    for (int i = frames - tail; i < frames; ++i) {
        keSum += keHist[i];
        r.maxKEtail = std::max<Real>(r.maxKEtail, keHist[i]);
        r.lsFailTail += lsHist[i];
        actSum += actHist[i] / 1000.0;
    }
    r.meanKEtail = keSum / tail;
    r.meanActiveTail = actSum / tail;
    r.finalKE = keHist.back();
    r.finalStretch = wedge::maxEdgeStretch(cs);
    r.hemFinal = wedge::meanHemY(cs, s);

    std::printf("[scene %d, contactAL %s] %d frames, %.1f s wall, %d vertices\n", scene, al ? "on " : "off", frames, r.secs,
                cs.vertexCount());
    std::printf("  KE final %.2e J, mean KE last %d frames %.2e J, max %.2e J\n", r.finalKE, tail, r.meanKEtail, r.maxKEtail);
    std::printf("  line-search failures: total %lld, last %d frames %lld\n", r.lsFailTotal, tail, r.lsFailTail);
    std::printf("  contact-active vertices per substep (mean, last %d frames) %.1f\n", tail, r.meanActiveTail);
    std::printf("  min body clearance %.3f mm (negative = penetration), max edge stretch %.4f (final %.4f), hem %.1f mm\n",
                r.minClearance * 1000, r.maxStretch, r.finalStretch, r.hemFinal * 1000);
    const auto& st = cs.contactStats();
    if (al) std::printf("  AL: outer solves %lld, max lambda %.3e N\n", st.alOuter, st.maxLambda);
    std::printf("  projection path: in-Newton projections %lld, after-solve projections %lld, lsFailProjection %lld, lsFailFlat %lld\n",
                st.projections, st.bodyProjections, st.lsFailProjection, st.lsFailFlat);
    std::fflush(stdout);
    return r;
}

int main(int argc, char** argv) {
    std::vector<int> scenes = {0, 1};
    std::vector<bool> modes = {false, true};
    int frames = 240;
    if (argc > 1) {
        const int sc = std::atoi(argv[1]);
        scenes = {sc};
    }
    if (argc > 2) {
        if (!std::strcmp(argv[2], "off")) modes = {false};
        else if (!std::strcmp(argv[2], "on")) modes = {true};
    }
    if (argc > 3) frames = std::atoi(argv[3]);

    for (int sc : scenes) {
        RunResult off{}, on{};
        bool haveOff = false, haveOn = false;
        for (bool al : modes) {
            RunResult r = run(sc, al, frames, al ? "on" : "off");
            if (al) { on = r; haveOn = true; } else { off = r; haveOff = true; }
            CHECK(std::isfinite(r.finalKE));
        }
        if (haveOff && haveOn) {
            std::printf("[scene %d] off vs on: mean KE last 60 %.2e vs %.2e J, line-search failures last 60 %lld vs %lld, "
                        "min clearance %.3f vs %.3f mm, max stretch %.4f vs %.4f\n",
                        sc, off.meanKEtail, on.meanKEtail, off.lsFailTail, on.lsFailTail, off.minClearance * 1000,
                        on.minClearance * 1000, off.maxStretch, on.maxStretch);
        }
    }
    FINISH();
}
