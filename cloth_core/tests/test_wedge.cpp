// Wedge test (K2, opus session 1): a sewn tube lodged on convex bodies (wedge_scenes.hpp, taken from
// agent/k2-contact-multipliers). Scene 0: tube on the test_garment ellipsoid; scene 1: tube on two overlapping
// ellipsoids ("thighs"), where the ring jitters.
//
// Per frame: kinetic energy, line-search failures, Newton iterations and cap hits, held (active) vertices and how
// often the held set changes, in-Newton projections, KE after Newton / after the body pass / after self collision,
// min body clearance, max edge stretch, hem height. Per-frame rows go to FL_OUT_DIR/wedge_<scene>_<tag>.csv.
//
// Usage: test_wedge [scene] [overrides] [frames]
//   overrides: comma list, e.g. "band=0,fric=1,self=0,push=0,iters=30,sub=4,tension=1"
//     band=<m>   contactBand          fric=<0|1>  bodyFrictionMode     push=<0|1>  bodyPushAfterSolve
//     self=<0|1> selfCollision        iters=<n>   newtonIterations     sub=<n>     substeps
//     tension=<0|1> tensionStiffness  tag=<name>  CSV/print label
// Without arguments: scene 0 and 1 with defaults, 240 frames, checks below.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "failoring/cloth.hpp"
#include "failoring/material.hpp"
#include "wedge_scenes.hpp"

using namespace fl;

namespace {

struct Summary {
    std::string tag;
    int scene = 0, frames = 0, verts = 0;
    double secs = 0;
    Real keFinal = 0, keMeanTail = 0, keMaxTail = 0;
    long long lsTail = 0, lsTotal = 0, capTail = 0, itersTail = 0, subTail = 0;
    double activeTail = 0, flipsIterTail = 0, flipsSubTail = 0, liftsTail = 0, liftsLateTail = 0, liftDistTail = 0;
    double keNewtonTail = 0, keBodyTail = 0, keSelfTail = 0;  // mean per substep, last 60 frames
    Real minClr = 1e30, maxStretch = 0, finalStretch = 0, hem = 0, gap = 0, alMaxPen = 0;
    bool finite = true;
};

std::string tagFrom(const std::string& ov) {
    std::stringstream ss(ov);
    std::string kv;
    while (std::getline(ss, kv, ','))
        if (kv.rfind("tag=", 0) == 0) return kv.substr(4);
    return ov.empty() ? "default" : ov;
}

void applyOverrides(SolverParams& sp, const std::string& ov) {
    std::stringstream ss(ov);
    std::string kv;
    while (std::getline(ss, kv, ',')) {
        const auto eq = kv.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = kv.substr(0, eq);
        const double v = std::atof(kv.c_str() + eq + 1);
        if (k == "band") sp.contactBand = v;
        else if (k == "fric") sp.bodyFrictionMode = static_cast<int>(v);
        else if (k == "push") sp.bodyPushAfterSolve = v != 0;
        else if (k == "self") sp.selfCollision = v != 0;
        else if (k == "iters") sp.newtonIterations = static_cast<int>(v);
        else if (k == "sub") sp.substeps = static_cast<int>(v);
        else if (k == "tension") { sp.tensionStiffness = v != 0; sp.hessianAuto = false; }
        else if (k == "auto") sp.hessianAuto = v != 0;
        else if (k == "al") sp.contactAL = v != 0;
        else if (k == "alk") sp.contactALStiffness = v;
        else if (k == "alouter") sp.contactALOuter = static_cast<int>(v);
        else if (k != "tag") std::printf("  (unknown override %s ignored)\n", k.c_str());
    }
}

Summary run(int scene, const std::string& ov, int frames) {
    auto mats = loadMaterialsFromFile(FL_MATERIALS_PATH);
    const FabricMaterial* fab = findMaterial(mats, "cotton_poplin");
    CHECK(fab != nullptr);
    SolverParams sp;
    sp.substeps = 4;  // as test_garment
    sp.seamRampFrames = 0;
    sp.contactDiagnostics = true;
    applyOverrides(sp, ov);
    wedge::Scene s = wedge::make(scene, *fab, sp);
    ClothSolver& cs = *s.cs;
    if (const char* e = std::getenv("FL_TRACE_FRAME")) cs.setNewtonTraceFrame(std::atoi(e));  // per-iteration trace to stderr

    Summary r;
    r.tag = tagFrom(ov);
    r.scene = scene;
    r.frames = frames;
    r.verts = cs.vertexCount();
    const std::string csvPath = std::string(FL_OUT_DIR) + "/wedge_" + std::to_string(scene) + "_" + r.tag + ".csv";
    std::FILE* csv = std::fopen(csvPath.c_str(), "w");
    if (csv)
        std::fprintf(csv, "frame,ke,ls_fail,newton_iters,cap_hits,active,flips_iter,flips_substep,lifts,lifts_late,"
                          "ke_newton,ke_body,ke_self,min_clearance_mm,max_stretch,hem_mm\n");
    const int tail = std::min(60, frames);
    ContactDiag prev = cs.contactDiag();
    std::vector<Real> ke;
    const auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < frames; ++f) {
        if (f == frames - tail) {  // snapshot at the start of the tail window (before its first frame)
            const ContactDiag& d0 = cs.contactDiag();
            r.lsTail = -d0.lsFail; r.capTail = -d0.iterCapHits; r.itersTail = -d0.newtonIters; r.subTail = -d0.substeps;
            r.activeTail = -double(d0.activeSum); r.flipsIterTail = -double(d0.flipsIter); r.flipsSubTail = -double(d0.flipsSubstep);
            r.liftsTail = -double(d0.lifts); r.liftsLateTail = -double(d0.liftsAfterFirstIter); r.liftDistTail = -d0.liftDist;
            r.keNewtonTail = -d0.keNewton; r.keBodyTail = -d0.keBody; r.keSelfTail = -d0.keSelf;
        }
        cs.step();
        const ContactDiag& d = cs.contactDiag();
        const long long sub = d.substeps - prev.substeps;
        const Real k = cs.kineticEnergy();
        const Real clr = cs.minBodyClearance();
        const Real st = wedge::maxEdgeStretch(cs);
        ke.push_back(k);
        r.minClr = std::min(r.minClr, clr);
        r.maxStretch = std::max(r.maxStretch, st);
        for (const auto& p : cs.positions()) r.finite = r.finite && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        if (csv)
            std::fprintf(csv, "%d,%.6e,%lld,%lld,%lld,%.1f,%lld,%lld,%lld,%lld,%.6e,%.6e,%.6e,%.4f,%.5f,%.3f\n", f, k,
                         d.lsFail - prev.lsFail, d.newtonIters - prev.newtonIters, d.iterCapHits - prev.iterCapHits,
                         sub ? double(d.activeSum - prev.activeSum) / sub : 0.0, d.flipsIter - prev.flipsIter,
                         d.flipsSubstep - prev.flipsSubstep, d.lifts - prev.lifts, d.liftsAfterFirstIter - prev.liftsAfterFirstIter,
                         d.lastKeNewton, d.lastKeBody, d.lastKeSelf, clr * 1e3, st, wedge::meanHemY(cs, s) * 1e3);
        prev = d;
        if (!r.finite) { r.frames = f + 1; break; }
    }
    if (csv) std::fclose(csv);
    r.secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const ContactDiag& d = cs.contactDiag();
    // the tail window ends at the last frame: add the end values
    r.lsTail += d.lsFail; r.capTail += d.iterCapHits; r.itersTail += d.newtonIters; r.subTail += d.substeps;
    r.activeTail += d.activeSum; r.flipsIterTail += d.flipsIter; r.flipsSubTail += d.flipsSubstep;
    r.liftsTail += d.lifts; r.liftsLateTail += d.liftsAfterFirstIter; r.liftDistTail += d.liftDist;
    r.keNewtonTail += d.keNewton; r.keBodyTail += d.keBody; r.keSelfTail += d.keSelf;
    const double ns = std::max<long long>(1, r.subTail);
    r.lsTotal = d.lsFail;
    for (int i = static_cast<int>(ke.size()) - tail; i < static_cast<int>(ke.size()); ++i) {
        r.keMeanTail += ke[i] / tail;
        r.keMaxTail = std::max(r.keMaxTail, ke[i]);
    }
    r.keFinal = ke.back();
    r.finalStretch = wedge::maxEdgeStretch(cs);
    r.hem = wedge::meanHemY(cs, s);
    r.gap = cs.maxSeamGap();
    r.alMaxPen = d.alMaxPenetration;

    std::printf("[scene %d | %s] %d verts, %d frames, %.1f s\n", scene, r.tag.c_str(), r.verts, r.frames, r.secs);
    std::printf("  KE final %.2e J; last %d frames: mean %.2e, max %.2e J\n", r.keFinal, tail, r.keMeanTail, r.keMaxTail);
    std::printf("  last %d frames, per substep: Newton iters %.2f, cap hits %lld of %lld substeps, ls-fail %lld (total %lld)\n",
                tail, r.itersTail / ns, r.capTail, r.subTail, r.lsTail, r.lsTotal);
    std::printf("  held vertices %.1f, held-set flips: between iterations %.2f, between substeps %.2f\n", r.activeTail / ns,
                r.flipsIterTail / ns, r.flipsSubTail / ns);
    std::printf("  in-Newton projections %.2f (after the first iteration %.2f), mean depth %.2e m\n", r.liftsTail / ns,
                r.liftsLateTail / ns, r.liftsTail > 0 ? r.liftDistTail / r.liftsTail : 0.0);
    std::printf("  step KE after Newton %.2e, after body pass %.2e, after self collision %.2e J (mean per substep)\n",
                r.keNewtonTail / ns, r.keBodyTail / ns, r.keSelfTail / ns);
    std::printf("  post-solve totals: pushed %lld, friction applied %lld vertex-substeps; Hessian switches %lld\n", d.pushAfter, d.frictionAfter, d.hessianSwitches);
    if (sp.contactAL)
        std::printf("  contactAL: Newton solves per substep %.2f, deepest after a solve %.3f mm, largest multiplier %.3e N\n",
                    double(d.alOuter) / std::max<long long>(1, d.substeps), d.alMaxPenetration * 1e3, d.alMaxLambda);
    std::printf("  min clearance %.3f mm, max stretch %.4f (final %.4f), hem %.1f mm, seam gap %.2f mm, finite %s\n",
                r.minClr * 1e3, r.maxStretch, r.finalStretch, r.hem * 1e3, r.gap * 1e3, r.finite ? "yes" : "NO");
    if (std::getenv("FL_KE_TOP")) {  // where is the kinetic energy? (held vs free, distance to the body)
        const auto& V = cs.velocities();
        const auto& held = cs.heldFlags();
        const auto& X = cs.positions();
        std::vector<int> id(V.size());
        for (size_t i = 0; i < id.size(); ++i) id[i] = static_cast<int>(i);
        std::sort(id.begin(), id.end(), [&](int a, int b) { return length2(V[a]) > length2(V[b]); });
        Real total = 0, heldKE = 0;
        for (size_t i = 0; i < V.size(); ++i) {
            const Real e = 0.5 * cs.vertexMass(static_cast<int>(i)) * length2(V[i]);
            total += e;
            if (held[i]) heldKE += e;
        }
        std::printf("  KE held vertices %.2e of %.2e J\n", heldKE, total);
        for (int k = 0; k < 10; ++k) {
            const int i = id[k];
            Vec3 n;
            const Real phi = cs.collider()->eval(X[i], n);
            const Vec3 v = V[i];
            std::printf("    v%-4d |v| %.2e m/s  normal part %+.2e  held %d  phi %.2e m  y %.1f mm\n", i, length(v), dot(v, n), held[i],
                        phi, X[i].y * 1e3);
        }
    }
    std::fflush(stdout);
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) {  // experiment mode: one run, no checks beyond finiteness
        const int scene = std::atoi(argv[1]);
        const std::string ov = argc > 2 ? argv[2] : "";
        const int frames = argc > 3 ? std::atoi(argv[3]) : 240;
        const Summary r = run(scene, ov, frames);
        CHECK(r.finite);
        FINISH();
    }
    // Default run. Diagnosis (docs/handoff/OPUS_SESSION_1.md): the jitter of the wedged tube is the free span bridging
    // the crease between the two bodies fluttering, because Newton never converges there with the Gauss-Newton Hessian,
    // which lacks the tension (geometric) stiffness of the taut span. The default solver (hessianAuto) switches to the
    // tension term where Gauss-Newton stalls.
    auto safety = [](const Summary& r) {
        CHECK_MSG(r.finite, "scene %d %s: non-finite", r.scene, r.tag.c_str());
        CHECK_MSG(r.minClr > -0.001, "scene %d %s: penetrates by %.2f mm", r.scene, r.tag.c_str(), -r.minClr * 1e3);  // as test_garment
        CHECK_MSG(r.gap < 0.004, "scene %d %s: seam gap %.2f mm", r.scene, r.tag.c_str(), r.gap * 1e3);              // as test_garment
    };
    // scene 1 with the old solver (Gauss-Newton, friction after the solve): the known jitter (reported, not asserted;
    // measured 3.1e-5 J)
    const Summary off = run(1, "tension=0,fric=0,tag=old_solver", 240);
    safety(off);
    // scene 1 with the default solver (automatic Hessian model): Newton converges and the ring settles (measured
    // 1.9e-7 J; with bodyFrictionMode 2 as well, 2.1e-10 J)
    const Summary on = run(1, "tag=default", 240);
    safety(on);
    CHECK_MSG(on.keMeanTail < 1e-6, "scene 1 default: mean KE over the last 60 frames %.2e J", on.keMeanTail);
    CHECK_MSG(on.keMeanTail * 100 < off.keMeanTail, "scene 1: default solver reduces the tail KE only %.0fx (%.2e -> %.2e J)",
              off.keMeanTail / on.keMeanTail, off.keMeanTail, on.keMeanTail);
    CHECK_MSG(on.capTail == 0, "scene 1 default: %lld substeps hit the Newton iteration cap in the last 60 frames", on.capTail);
    CHECK_MSG(on.lsTail == 0, "scene 1 default: %lld line-search failures in the last 60 frames", on.lsTail);
    // scene 0 (the test_garment geometry) with the default solver: settles as well (measured 8.7e-7 J)
    const Summary on0 = run(0, "tag=default", 240);
    safety(on0);
    CHECK_MSG(on0.keMeanTail < 5e-6, "scene 0 default: mean KE over the last 60 frames %.2e J", on0.keMeanTail);
    // scene 0's free, collapsed top converges linearly (~10-11 iterations of 12): a few cap hits are not jitter
    CHECK_MSG(on0.capTail <= on0.subTail / 20, "scene 0 default: %lld of %lld substeps hit the cap in the last 60 frames", on0.capTail, on0.subTail);
    // K2 contact multipliers (contactAL) on scene 1: settles like the default, Newton converges, the contact holds within
    // its tolerance (measured: 1.13e-7 J vs 1.28e-7 J default, 0 cap hits, 1.02 solves per substep, deepest 0.009 mm)
    const Summary al = run(1, "al=1,tag=contact_al", 240);
    safety(al);
    CHECK_MSG(al.keMeanTail < 1e-6, "scene 1 contactAL: mean KE over the last 60 frames %.2e J", al.keMeanTail);
    CHECK_MSG(al.capTail == 0, "scene 1 contactAL: %lld cap hits in the last 60 frames", al.capTail);
    CHECK_MSG(al.alMaxPen < 2e-5, "scene 1 contactAL: %.3f mm deep after a solve", al.alMaxPen * 1e3);
    std::printf("[summary] scene 1 mean KE last 60 frames: old solver %.2e J, default %.2e J (%.0fx), contactAL %.2e J; scene 0 default %.2e J\n",
                off.keMeanTail, on.keMeanTail, off.keMeanTail / on.keMeanTail, al.keMeanTail, on0.keMeanTail);
    FINISH();
}
