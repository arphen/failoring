#pragma once
// Wedge scenarios shared by test_wedge (and handy for diagnostics): a sewn tube lodged on convex bodies.
//   Scene 0 "tube-ellipsoid": exactly test_garment's setup (two cotton poplin panels pre-wrapped into a tube of
//            circumference 0.80 m, started on the upper dome of a 0.17 x 0.35 x 0.11 m ellipsoid).
//   Scene 1 "tube-thighs":    the same tube on a union of two overlapping vertical ellipsoids ("thighs"), so the
//            ring is wedged on two convex bodies at once and bridges the crease between them.
#include <cmath>
#include <memory>

#include "failoring/cloth.hpp"
#include "failoring/collider.hpp"
#include "failoring/material.hpp"
#include "failoring/mesher.hpp"

namespace wedge {
using namespace fl;

struct Scene {
    std::unique_ptr<ClothSolver> cs;
    int pf = 0, pb = 0;
};

inline Scene make(int which, const FabricMaterial& fab, const SolverParams& sp) {
    Scene s;
    s.cs.reset(new ClothSolver(sp));
    ClothSolver& cs = *s.cs;
    const Real W = 0.40, H = 0.30, h = 0.02;
    Real y0 = 0.25;
    if (which == 0) {
        cs.setCollider(std::make_shared<EllipsoidCollider>(Vec3(0, 0, 0), Vec3(0.17, 0.35, 0.11)));
    } else {
        auto u = std::make_shared<UnionCollider>();
        u->add(std::make_shared<EllipsoidCollider>(Vec3(-0.075, 0, 0), Vec3(0.10, 0.40, 0.10)));
        u->add(std::make_shared<EllipsoidCollider>(Vec3(+0.075, 0, 0), Vec3(0.10, 0.40, 0.10)));
        cs.setCollider(u);
        y0 = 0.30;
    }
    PanelMesh mesh = meshRect(W, H, h);
    PanelPlacement front, back;
    front.origin = Vec3(-W / 2, y0, 0.145); front.ex = Vec3(1, 0, 0);  front.ey = Vec3(0, 1, 0);
    back.origin  = Vec3(+W / 2, y0, -0.145); back.ex  = Vec3(-1, 0, 0); back.ey  = Vec3(0, 1, 0);
    s.pf = cs.addPanel(mesh, fab, front, kPi / 2, "front");
    s.pb = cs.addPanel(mesh, fab, back, kPi / 2, "back");
    cs.addSeam(s.pf, 1, s.pb, 3, true);
    cs.addSeam(s.pf, 3, s.pb, 1, true);
    const Real Rc = W / kPi;
    for (int p : {s.pf, s.pb})
        for (size_t i = 0; i < mesh.verts.size(); ++i) {
            const Real th = kPi * mesh.verts[i].x / W, y = y0 + mesh.verts[i].y;
            const Vec3 q = (p == s.pf) ? Vec3(-Rc * std::cos(th), y, Rc * std::sin(th)) : Vec3(Rc * std::cos(th), y, -Rc * std::sin(th));
            cs.setVertexPosition(cs.firstVertex(p) + static_cast<int>(i), q);
        }
    return s;
}

// Largest edge length / rest length over all triangle edges (1.0 = unstretched).
inline Real maxEdgeStretch(const ClothSolver& cs) {
    Real m = 0;
    const auto& X = cs.positions();
    const auto& U = cs.restUV();
    for (const auto& t : cs.triangles())
        for (int k = 0; k < 3; ++k) {
            const int a = t[k], b = t[(k + 1) % 3];
            const Real r = length(U[a] - U[b]);
            if (r > 1e-9) m = std::max(m, length(X[a] - X[b]) / r);
        }
    return m;
}

inline Real meanHemY(const ClothSolver& cs, const Scene& s) {
    Real y = 0;
    int n = 0;
    for (int p : {s.pf, s.pb})
        for (int v : cs.edgeChain(p, 0)) { y += cs.positions()[v].y; ++n; }
    return y / n;
}
}  // namespace wedge
