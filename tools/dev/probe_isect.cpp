// Scratch probe: where are the self-intersecting triangle pairs of the default (vertex-triangle) tube?
// Build: g++ -O2 -std=c++17 -Icloth_core/include -DFL_MATERIALS_PATH='"materials/fabrics.json"' tools/dev/probe_isect.cpp build/libcloth_core.a -o probe_isect
// Run:   ./probe_isect <mesh edge m, 0.03> <frames, 60>   (from the repo root, after building cloth_core)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include "failoring/barrier_contact.hpp"
#include "failoring/cloth.hpp"
#include "failoring/collider.hpp"
#include "failoring/material.hpp"
#include "failoring/mesher.hpp"
using namespace fl;

int main(int argc, char** argv) {
    const Real h = argc > 1 ? std::atof(argv[1]) : 0.03;
    const int frames = argc > 2 ? std::atoi(argv[2]) : 60;
    auto mats = loadMaterialsFromFile(FL_MATERIALS_PATH);
    const FabricMaterial* fab = findMaterial(mats, "cotton_poplin");
    auto body = std::make_shared<EllipsoidCollider>(Vec3(0, 0, 0), Vec3(0.17, 0.35, 0.11));
    SolverParams sp; sp.substeps = 4; sp.seamRampFrames = 0;
    ClothSolver cs(sp); cs.setCollider(body);
    const Real W = 0.40, H = 0.30, y0 = 0.25;
    PanelMesh mesh = meshRect(W, H, h);
    PanelPlacement a, b;
    a.origin = Vec3(-W / 2, y0, 0.145); a.ex = Vec3(1, 0, 0);  a.ey = Vec3(0, 1, 0);
    b.origin = Vec3(+W / 2, y0, -0.145); b.ex = Vec3(-1, 0, 0); b.ey = Vec3(0, 1, 0);
    const int pf = cs.addPanel(mesh, *fab, a, kPi / 2, "front"), pb = cs.addPanel(mesh, *fab, b, kPi / 2, "back");
    cs.addSeam(pf, 1, pb, 3, true); cs.addSeam(pf, 3, pb, 1, true);
    const Real Rc = W / kPi;
    for (int p : {pf, pb})
        for (size_t i = 0; i < mesh.verts.size(); ++i) {
            const Real th = kPi * mesh.verts[i].x / W, y = y0 + mesh.verts[i].y;
            cs.setVertexPosition(cs.firstVertex(p) + (int)i, (p == pf) ? Vec3(-Rc * std::cos(th), y, Rc * std::sin(th)) : Vec3(Rc * std::cos(th), y, -Rc * std::sin(th)));
        }
    // seam vertices (side edges of both panels)
    std::set<int> seamV;
    for (int p : {pf, pb}) for (int e : {1, 3}) for (int v : cs.edgeChain(p, e)) seamV.insert(v);
    const int nf = cs.vertexCountOfPanel(pf);
    auto panelOf = [&](int v) { return v < nf ? 0 : 1; };
    const auto& T = cs.triangles();
    for (int f = 0; f <= frames; ++f) {
        if (f > 0) cs.step();
        if (f % 10 && f != 1 && f != 2) continue;
        const auto& X = cs.positions();
        int n = 0, cross = 0, nearSeam = 0; Real maxD = 0;
        for (size_t t = 0; t < T.size(); ++t)
            for (size_t u = t + 1; u < T.size(); ++u) {
                bool share = false;
                for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) share |= T[t][i] == T[u][j];
                if (share) continue;
                if (!contact::trianglesIntersectExact(X[T[t][0]], X[T[t][1]], X[T[t][2]], X[T[u][0]], X[T[u][1]], X[T[u][2]])) continue;
                ++n;
                if (panelOf(T[t][0]) != panelOf(T[u][0])) ++cross;
                bool ns = false;
                for (int i = 0; i < 3; ++i) ns |= seamV.count(T[t][i]) || seamV.count(T[u][i]);
                if (ns) ++nearSeam;
                // distance of the pair's centroids from the nearest seam vertex
                Vec3 c = (X[T[t][0]] + X[T[t][1]] + X[T[t][2]] + X[T[u][0]] + X[T[u][1]] + X[T[u][2]]) / 6.0;
                Real dmin = 1e9; for (int v : seamV) dmin = std::min(dmin, length(X[v] - c));
                maxD = std::max(maxD, dmin);
            }
        std::printf("frame %3d: pairs %3d (counter %3d), across panels %3d, touching a seam vertex %3d, farthest from seam %.1f mm\n",
                    f, n, cs.selfIntersections(), cross, nearSeam, maxD * 1e3);
    }
}
