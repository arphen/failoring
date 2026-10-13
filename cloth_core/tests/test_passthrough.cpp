// K5 done-criterion (b): edge-edge passthrough. A thin pinned strip A lies flat (y = 0, along z, |x| < 1 cm). A coarse
// vertical sheet B (two triangles, 10 cm wide) falls onto it so that its bottom EDGE crosses A's strip while B's bottom
// corners stay 4 cm away from A. No vertex of B is near a triangle of A, so vertex-triangle self contact should miss the
// crossing; edge-edge contact should not. Intersections are counted exactly (triangle-triangle, every frame).
#include <cstdio>
#include <memory>

#include "check.hpp"
#include "failoring/cloth.hpp"
#include "failoring/material.hpp"

using namespace fl;

namespace {

struct Run {
    int framesWithIntersection = 0;
    int maxIntersections = 0;
    Real minBottomY = 1e9;  // lowest point of B (m). Informational only: B has no floor, so a sheet draped over A keeps sinking
    Real minDistAtEnd = 0;
    int frames = 0;
};

Run runScene(bool barrier, int frames, const FabricMaterial& mat) {
    SolverParams sp;
    sp.substeps = 2;
    sp.barrierContact = barrier;
    sp.selfCollision = !barrier;  // the old path: vertex-triangle projection after the solve
    ClothSolver cs(sp);

    // A: strip in the y = 0 plane, along z, |x| <= 0.01, pinned at all vertices (flat, fixed)
    PanelMesh stripMesh = meshRect(0.02, 0.10, 0.02);
    PanelPlacement pa;
    pa.origin = Vec3(-0.01, 0, -0.05);
    pa.ex = Vec3(1, 0, 0);
    pa.ey = Vec3(0, 0, 1);
    int a = cs.addPanel(stripMesh, mat, pa, 0.0, "strip");
    for (int i = 0; i < cs.vertexCountOfPanel(a); ++i) cs.pinVertex(cs.firstVertex(a) + i);

    // B: vertical sheet in the z = 0 plane, 10 cm wide, its bottom edge 3 cm above A
    PanelMesh sheetMesh = meshRect(0.10, 0.10, 0.10);
    PanelPlacement pb;
    pb.origin = Vec3(-0.05, 0.03, 0);
    pb.ex = Vec3(1, 0, 0);
    pb.ey = Vec3(0, 1, 0);
    cs.addPanel(sheetMesh, mat, pb, 0.0, "sheet");

    Run r;
    const int nA0 = cs.firstVertex(a), nA1 = nA0 + cs.vertexCountOfPanel(a);  // A's vertices are [nA0, nA1)
    for (int f = 0; f < frames; ++f) {
        cs.step();
        ++r.frames;
        const int n = cs.selfIntersections();
        if (n > 0) ++r.framesWithIntersection;
        if (n > r.maxIntersections) r.maxIntersections = n;
        // B's vertices are everything outside A's range
        for (int i = 0; i < cs.vertexCount(); ++i)
            if (i < nA0 || i >= nA1) r.minBottomY = std::min(r.minBottomY, cs.positions()[i].y);
    }
    r.minDistAtEnd = cs.minSelfDistance(0.05);
    std::printf("  %-14s frames %d: frames with intersections %d, max intersecting pairs %d, lowest B point %.2f mm, "
                "min self distance at end %.3f mm\n",
                barrier ? "barrier (EE)" : "vertex-tri", r.frames, r.framesWithIntersection, r.maxIntersections,
                r.minBottomY * 1e3, r.minDistAtEnd * 1e3);
    return r;
}

}  // namespace

int main() {
    std::printf("[edge-edge passthrough: thin strip crossed by a falling sheet's bottom edge]\n");
    auto mats = loadMaterialsFromFile(FL_MATERIALS_PATH);
    const FabricMaterial* mat = findMaterial(mats, "cotton_poplin");
    CHECK_MSG(mat != nullptr, "material cotton_poplin not found");
    if (!mat) FINISH();
    const Run old = runScene(false, 120, *mat);
    const Run neu = runScene(true, 120, *mat);
    // The old mode is expected to FAIL (exact triangle-triangle intersections); the new mode must PASS.
    std::printf("  old mode passthrough: %s; new mode passthrough: %s\n",
                old.framesWithIntersection > 0 ? "yes" : "no", neu.framesWithIntersection > 0 ? "yes" : "no");
    CHECK_MSG(old.framesWithIntersection > 0, "scenario does not reproduce the vertex-triangle miss");
    CHECK_MSG(neu.framesWithIntersection == 0, "barrier mode lets %d frames intersect", neu.framesWithIntersection);
    FINISH();
}
