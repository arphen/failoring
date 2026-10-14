// K5 done-criterion (d): layered sheet-on-sheet. A pinned flat plate (y = 0) and a second flat sheet 1 cm above it, falling
// under gravity onto it. Reports the settled minimal gap between the two layers, the exact intersection count, and the
// number of frames the top layer was in contact range, for the vertex-triangle path (old) and the barrier path (new).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "check.hpp"
#include "failoring/cloth.hpp"
#include "failoring/material.hpp"

using namespace fl;

namespace {

struct Run {
    int frames = 0;
    int framesIntersecting = 0;
    int maxIntersections = 0;
    Real minGap = 0;      // smallest distance between the two layers' primitives after the run (m)
    Real gapAt50 = 0;     // same, after 50 frames (while the top layer is still settling)
    Real topMeanY = 0;    // mean height of the top layer at the end (m)
    double seconds = 0;
    long long newton = 0, factor = 0, rebuilds = 0;
};

// Gap between the top layer and the plate (flat, at y = 0, pinned): the lowest top vertex over the plate's footprint.
// (Not ClothSolver::minSelfDistance: that also counts pairs inside the top sheet.)
Real layerGap(const ClothSolver& cs, int t0, int t1) {
    Real g = 1e9;
    for (int i = t0; i < t1; ++i) {
        const Vec3& q = cs.positions()[i];
        if (std::abs(q.x) <= 0.05 && std::abs(q.z) <= 0.05) g = std::min(g, q.y);
    }
    return g;
}

Run runLayers(bool barrier, int frames, const FabricMaterial& mat) {
    SolverParams sp;
    sp.substeps = 2;
    sp.barrierContact = barrier;
    sp.selfCollision = !barrier;
    ClothSolver cs(sp);
    PanelMesh plateMesh = meshRect(0.10, 0.10, 0.02);
    PanelPlacement pp;
    pp.origin = Vec3(-0.05, 0, -0.05);
    pp.ex = Vec3(1, 0, 0);
    pp.ey = Vec3(0, 0, 1);
    int plate = cs.addPanel(plateMesh, mat, pp, 0.0, "plate");
    for (int i = 0; i < cs.vertexCountOfPanel(plate); ++i) cs.pinVertex(cs.firstVertex(plate) + i);

    // top sheet 6 cm square, 1 cm above the plate, centred on it (its edges must not sit over the plate's edges: the
    // edge-edge barrier there pushes the top layer sideways, which was the first run's drift)
    PanelPlacement pt = pp;
    pt.origin = Vec3(-0.03, 0.01, -0.03);
    int top = cs.addPanel(meshRect(0.06, 0.06, 0.02), mat, pt, 0.0, "top");
    const int t0 = cs.firstVertex(top), t1 = t0 + cs.vertexCountOfPanel(top);

    Run r;
    const auto tStart = std::chrono::steady_clock::now();
    for (int f = 0; f < frames; ++f) {
        cs.step();
        ++r.frames;
        const int n = cs.selfIntersections();
        if (n > 0) ++r.framesIntersecting;
        if (n > r.maxIntersections) r.maxIntersections = n;
        if (f == 49) r.gapAt50 = layerGap(cs, t0, t1);
    }
    r.minGap = layerGap(cs, t0, t1);
    Real y = 0;
    for (int i = t0; i < t1; ++i) y += cs.positions()[i].y;
    r.topMeanY = y / (t1 - t0);
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count();
    r.newton = cs.newtonIterations();
    r.factor = cs.factorizations();
    r.rebuilds = cs.patternRebuilds();
    std::printf("    cost: %.1f ms/frame, Newton iterations %lld (%.2f per substep), factorisations %lld, pattern rebuilds %lld\n",
                1e3 * r.seconds / frames, r.newton, double(r.newton) / cs.substepsDone(), r.factor, r.rebuilds);
    std::printf("  %-14s: min layer gap %.3f mm (frame 50: %.3f mm), top mean height %.3f mm, "
                "intersecting frames %d of %d (max pairs %d)\n",
                barrier ? "barrier (EE)" : "vertex-tri", r.minGap * 1e3, r.gapAt50 * 1e3, r.topMeanY * 1e3,
                r.framesIntersecting, r.frames, r.maxIntersections);
    return r;
}

}  // namespace

int main() {
    std::printf("[layered sheet-on-sheet: 10 cm cotton sheet dropped 1 cm onto a pinned plate]\n");
    auto mats = loadMaterialsFromFile(FL_MATERIALS_PATH);
    const FabricMaterial* mat = findMaterial(mats, "cotton_poplin");
    CHECK_MSG(mat != nullptr, "material cotton_poplin not found");
    if (!mat) FINISH();
    const Run old = runLayers(false, 120, *mat);
    const Run neu = runLayers(true, 120, *mat);
    CHECK_MSG(neu.framesIntersecting == 0, "barrier mode: %d intersecting frames", neu.framesIntersecting);
    CHECK_MSG(neu.topMeanY > 0.0, "barrier mode: top layer ended below the plate plane");
    CHECK_MSG(neu.minGap > 0.0, "barrier mode: layers touch (gap %.6f m)", neu.minGap);
    FINISH();
}
