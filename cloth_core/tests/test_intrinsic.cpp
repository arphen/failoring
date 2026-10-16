// Crochet plan §2.7 test 1: an IntrinsicMesh whose triRest corners are copied from the vertex UVs must behave
// exactly like the equivalent normal panel. Both solvers get the same mesh, placement, material and params; the
// acceptance is max |x_A - x_B| < 1e-9 m, plus a non-vacuousness check that the sheet really settled onto the body.
// The numbers go to docs/CROCHET_STATUS.md.
#include <cmath>
#include <cstdio>
#include <memory>
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
    FINISH();
}
