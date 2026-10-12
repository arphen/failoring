// Native timing of the mesh-collider bake and of a garment solve, for comparison with the browser (wasm) numbers.
// Not part of ctest: configure with -DFL_BENCH=ON and run build/bench_native. Prints one line per case.
//   bake:   SDF grid of an ellipsoid mannequin (triangle count in the output), default h = 7 mm, median of 5 bakes.
//   solve:  the test_garment setup (tube dress on an ellipsoid, 100 frames, substeps 4), median of 3 runs.
//           solve_sdf repeats it with the baked SDF grid of the same ellipsoid as the collider.
// A checksum of the baked field and of the final cloth positions is printed so that builds can be checked to agree.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "failoring/cloth.hpp"
#include "failoring/collider.hpp"
#include "failoring/material.hpp"
#include "failoring/mesher.hpp"
#include "failoring/sdf_grid.hpp"

using namespace fl;
using Tris = std::vector<std::array<int, 3>>;

namespace {

double nowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// UV-sphere ellipsoid with nu x nv quads (2 * nu * (nv - 1) triangles), outward winding.
void ellipsoidMesh(Vec3 r, int nu, int nv, std::vector<Vec3>& V, Tris& T) {
    V.clear();
    T.clear();
    V.push_back({0, r.y, 0});
    for (int j = 1; j < nv; ++j)
        for (int i = 0; i < nu; ++i) {
            const Real th = kPi * j / nv, ph = 2 * kPi * i / nu;
            V.push_back({r.x * std::sin(th) * std::cos(ph), r.y * std::cos(th), r.z * std::sin(th) * std::sin(ph)});
        }
    V.push_back({0, -r.y, 0});
    const int south = static_cast<int>(V.size()) - 1;
    auto ring = [&](int j, int i) { return 1 + (j - 1) * nu + (i % nu); };
    for (int i = 0; i < nu; ++i) T.push_back({0, ring(1, i + 1), ring(1, i)});
    for (int j = 1; j < nv - 1; ++j)
        for (int i = 0; i < nu; ++i) {
            const int a = ring(j, i), b = ring(j, i + 1), c = ring(j + 1, i), d = ring(j + 1, i + 1);
            T.push_back({a, b, c});
            T.push_back({b, d, c});
        }
    for (int i = 0; i < nu; ++i) T.push_back({ring(nv - 1, i), ring(nv - 1, i + 1), south});
}

// Deterministic probe points for the checksum of a baked grid.
double gridChecksum(const SdfGrid& g) {
    double s = 0;
    for (int i = 0; i < 2000; ++i) {
        const Real u = std::fmod(i * 0.6180339887, 1.0), v = std::fmod(i * 0.3247179572, 1.0), w = std::fmod(i * 0.7548776662, 1.0);
        s += g.eval(Vec3(-0.25 + 0.5 * u, -0.45 + 0.9 * v, -0.2 + 0.4 * w), nullptr);
    }
    return s;
}

void benchBake(int nu, int nv, int repeats) {
    std::vector<Vec3> V;
    Tris T;
    ellipsoidMesh(Vec3(0.17, 0.35, 0.11), nu, nv, V, T);
    SdfGridParams p;  // defaults: h = 7 mm
    std::vector<double> ms;
    double sum = 0;
    SdfGridStats st;
    for (int r = 0; r < repeats; ++r) {
        SdfGrid g;
        const double t0 = nowMs();
        g.bake(V, T, p);
        ms.push_back(nowMs() - t0);
        st = g.stats();
        if (r == 0) sum = gridChecksum(g);
    }
    std::printf("bake   tris %6zu  verts %6zu  h %.1f mm  median %8.1f ms  (min %.1f)  fine %d bricks  distance evals %lld  checksum %.9e\n",
                T.size(), V.size(), p.h * 1000, median(ms), *std::min_element(ms.begin(), ms.end()), st.bricks, st.distanceEvals, sum);
}

// One garment case: test_garment's setup with the given panel edge length and body collider.
void benchSolve(const char* label, Real edge, bool sdfBody, int frames, int repeats) {
    auto mats = loadMaterialsFromFile(FL_MATERIALS_PATH);
    const FabricMaterial* fab = findMaterial(mats, "cotton_poplin");
    const Vec3 radii(0.17, 0.35, 0.11);
    std::shared_ptr<Collider> body = std::make_shared<EllipsoidCollider>(Vec3(0, 0, 0), radii);
    if (sdfBody) {
        std::vector<Vec3> V;
        Tris T;
        ellipsoidMesh(radii, 128, 64, V, T);
        auto g = std::make_shared<SdfGrid>();
        g->bake(V, T, SdfGridParams());
        body = std::make_shared<GridCollider>(g);
    }
    const Real W = 0.40, H = 0.30, y0 = 0.25;

    std::vector<double> ms;
    double checksum = 0;
    int nVerts = 0, nTris = 0;
    for (int r = 0; r < repeats; ++r) {
        PanelMesh mesh = meshRect(W, H, edge);
        SolverParams sp;
        sp.substeps = 4;
        sp.seamRampFrames = 0;
        ClothSolver cs(sp);
        cs.setCollider(body);
        PanelPlacement front, back;
        front.origin = Vec3(-W / 2, y0, 0.145); front.ex = Vec3(1, 0, 0); front.ey = Vec3(0, 1, 0);
        back.origin = Vec3(+W / 2, y0, -0.145); back.ex = Vec3(-1, 0, 0); back.ey = Vec3(0, 1, 0);
        const int pf = cs.addPanel(mesh, *fab, front, kPi / 2, "front");
        const int pb = cs.addPanel(mesh, *fab, back, kPi / 2, "back");
        cs.addSeam(pf, 1, pb, 3, true);
        cs.addSeam(pf, 3, pb, 1, true);
        const Real Rc = W / kPi;
        for (int p : {pf, pb})
            for (size_t i = 0; i < mesh.verts.size(); ++i) {
                const Real th = kPi * mesh.verts[i].x / W, y = y0 + mesh.verts[i].y;
                const Vec3 q = (p == pf) ? Vec3(-Rc * std::cos(th), y, Rc * std::sin(th)) : Vec3(Rc * std::cos(th), y, -Rc * std::sin(th));
                cs.setVertexPosition(cs.firstVertex(p) + static_cast<int>(i), q);
            }
        const double t0 = nowMs();
        for (int f = 0; f < frames; ++f) cs.step();
        ms.push_back((nowMs() - t0) / frames);
        nVerts = cs.vertexCount();
        nTris = cs.triangleCount();
        if (r == 0)
            for (const Vec3& x : cs.positions()) checksum += x.x + 2 * x.y + 3 * x.z;
    }
    std::printf("solve  %-14s verts %5d tris %5d edge %4.1f mm  %3d frames  median %7.2f ms/step  (min %.2f)  checksum %.9e\n", label, nVerts,
                nTris, edge * 1000, frames, median(ms), *std::min_element(ms.begin(), ms.end()), checksum);
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // progress lines appear as each case finishes
    benchBake(128, 64, 5);
    benchBake(256, 128, 5);
    benchSolve("analytic", 0.02, false, 100, 3);
    benchSolve("sdf", 0.02, true, 100, 3);
    benchSolve("analytic_fine", 0.01, false, 100, 1);
    return 0;
}
