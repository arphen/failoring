// Flat C API of the cloth core for the browser (WebAssembly, wasi-sdk). One global simulation.
// All arrays cross the boundary as raw pointers into wasm memory (see fl_alloc / fl_free).
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

#include "failoring/cloth.hpp"
#include "failoring/collider.hpp"
#include "failoring/material.hpp"
#include "failoring/mesh_sizing.hpp"
#include "failoring/mesher.hpp"
#include "failoring/sdf_grid.hpp"

#define EXPORT(name) __attribute__((export_name(#name)))

using namespace fl;

namespace {
struct State {
    SolverParams params;
    std::vector<FabricMaterial> mats;
    std::unique_ptr<ClothSolver> solver;
    std::vector<std::shared_ptr<Collider>> colliders;
    std::vector<float> restUV;   // panel (u,v) per vertex (before grain rotation), metres
    std::vector<float> posF;     // scratch for fl_positions
    std::vector<int> triI;       // scratch for fl_triangles
    int warnings = 0;            // bit0: a panel boundary could not be fully meshed
    std::vector<float> savedPos; // fl_snapshot
    std::shared_ptr<Collider> collider;  // what the solver currently collides with
    std::vector<float> dispF;    // scratch for fl_display_positions
    std::vector<int> dispEdges;  // display subdivision: edge (i,j) -> midpoint vertex n + e
    std::vector<int> dispTris;   // display triangles over the n + ne display vertices
};
State g;

// The body (a mesh baked into a signed-distance grid) lives outside State: fl_init starts a new simulation but keeps
// the baked body until fl_set_body_mesh replaces it, so re-simulating the same body costs no bake.
struct Body {
    std::shared_ptr<SdfGrid> grid;
    bool enabled = false;
    double stats[8] = {0, 0, 0, 0, 0, 0, 0, 0};
} gb;

void rebuildColliders() {
    if (!g.solver) return;
    std::vector<std::shared_ptr<Collider>> parts = g.colliders;
    if (gb.enabled && gb.grid && !gb.grid->empty()) parts.push_back(std::make_shared<GridCollider>(gb.grid));
    if (parts.empty()) g.collider.reset();
    else if (parts.size() == 1) g.collider = parts[0];
    else {
        auto u = std::make_shared<UnionCollider>();
        for (auto& c : parts) u->add(c);
        g.collider = u;
    }
    g.solver->setCollider(g.collider);
}
}  // namespace

extern "C" {

EXPORT(fl_alloc) void* fl_alloc(int bytes) { return std::malloc(static_cast<size_t>(bytes)); }
EXPORT(fl_free) void fl_free(void* p) { std::free(p); }

EXPORT(fl_init) void fl_init() {
    g = State();
    g.solver.reset(new ClothSolver(g.params));
}

EXPORT(fl_add_material) int fl_add_material(double density, double thickness, double tw, double tf, double shear, double bw, double bf,
                                            double comp, double muS, double muK, double damping) {
    FabricMaterial m;
    m.density = density; m.thickness = thickness; m.tensileWarp = tw; m.tensileWeft = tf; m.shear = shear;
    m.bendWarp = bw; m.bendWeft = bf; m.compressionRatio = comp; m.frictionStatic = muS; m.frictionKinetic = muK; m.damping = damping;
    g.mats.push_back(m);
    return static_cast<int>(g.mats.size()) - 1;
}

// Recommended mesh edge length (m) from the Zhang et al. 2025 estimator.
EXPORT(fl_recommended_edge) double fl_recommended_edge(int mat) { return optimalResolution(g.mats[mat]).isotropic(); }

// ids: 0 substeps, 1 newton iterations, 2 seam stiffness, 3 seam ramp frames, 4 self collision, 5 gravity y, 6 dt,
//      7 tension stiffness in the Newton matrix (0/1), 8 contact band (m)
EXPORT(fl_set_param) void fl_set_param(int id, double v) {
    SolverParams& p = g.solver ? g.solver->params() : g.params;
    switch (id) {
        case 0: p.substeps = static_cast<int>(v); break;
        case 1: p.newtonIterations = static_cast<int>(v); break;
        case 2: p.seamStiffness = v; break;
        case 3: p.seamRampFrames = static_cast<int>(v); break;
        case 4: p.selfCollision = v != 0; break;
        case 5: p.gravity.y = v; break;
        case 6: p.dt = v; break;
        case 7: p.tensionStiffness = v != 0; break;
        case 8: p.contactBand = v; break;
        default: break;
    }
    g.params = p;
}

// Start a new simulation with the current parameters, materials and colliders (panels are cleared).
EXPORT(fl_begin) void fl_begin() {
    g.solver.reset(new ClothSolver(g.params));
    g.restUV.clear();
    g.warnings = 0;
    rebuildColliders();
}

// Outline: n points (x,y) in metres, either winding. Returns the panel index.
EXPORT(fl_add_panel) int fl_add_panel(const double* xy, int n, double edge, int mat, double grain) {
    if (n < 3 || mat < 0 || mat >= static_cast<int>(g.mats.size())) return -1;
    std::vector<Vec2> poly;
    for (int i = 0; i < n; ++i) poly.push_back(Vec2(xy[2 * i], xy[2 * i + 1]));
    PanelMesh m = meshPolygon(poly, edge);
    if (!m.boundaryComplete) g.warnings |= 1;
    for (auto& v : m.verts) { g.restUV.push_back(static_cast<float>(v.x)); g.restUV.push_back(static_cast<float>(v.y)); }
    PanelPlacement pl;
    pl.origin = Vec3(0, 0, 0); pl.ex = Vec3(1, 0, 0); pl.ey = Vec3(0, 1, 0);
    return g.solver->addPanel(m, g.mats[mat], pl, grain, "");
}

EXPORT(fl_add_seam) int fl_add_seam(int pa, int ea, int pb, int eb, int reverse) {
    if (pa < 0 || pb < 0 || pa >= g.solver->panelCount() || pb >= g.solver->panelCount()) return -1;
    g.solver->addSeam(pa, ea, pb, eb, reverse != 0);
    return 0;
}

EXPORT(fl_warnings) int fl_warnings() { return g.warnings; }
EXPORT(fl_panel_first_vertex) int fl_panel_first_vertex(int p) { return g.solver->firstVertex(p); }
EXPORT(fl_panel_vertex_count) int fl_panel_vertex_count(int p) { return g.solver->vertexCountOfPanel(p); }
EXPORT(fl_panel_first_triangle) int fl_panel_first_triangle(int p) { return g.solver->firstTriangle(p); }
EXPORT(fl_panel_triangle_count) int fl_panel_triangle_count(int p) { return g.solver->triangleCountOfPanel(p); }

// Writes the global vertex indices of an outline edge chain into out; returns the count.
EXPORT(fl_edge_chain) int fl_edge_chain(int p, int e, int* out, int cap) {
    const auto& ch = g.solver->edgeChain(p, e);
    int k = 0;
    for (int v : ch) if (k < cap) out[k++] = v;
    return k;
}

// Colliders: records of 8 doubles [type, a, b, c, d, e, f, g]
//   type 0 sphere:   center(a,b,c) radius d
//   type 1 capsule:  p0(a,b,c) p1(d,e,f) radius g
//   type 2 ellipsoid center(a,b,c) radii(d,e,f)
EXPORT(fl_set_colliders) void fl_set_colliders(const double* d, int count) {
    g.colliders.clear();
    for (int i = 0; i < count; ++i) {
        const double* r = d + 8 * i;
        if (r[0] == 0) g.colliders.push_back(std::make_shared<SphereCollider>(Vec3(r[1], r[2], r[3]), r[4]));
        else if (r[0] == 1) g.colliders.push_back(std::make_shared<CapsuleCollider>(Vec3(r[1], r[2], r[3]), Vec3(r[4], r[5], r[6]), r[7]));
        else if (r[0] == 2) g.colliders.push_back(std::make_shared<EllipsoidCollider>(Vec3(r[1], r[2], r[3]), Vec3(r[4], r[5], r[6])));
        else if (r[0] == 3) g.colliders.push_back(std::make_shared<PlaneCollider>(Vec3(r[1], r[2], r[3]), Vec3(r[4], r[5], r[6])));   // point, normal
    }
    rebuildColliders();
}

// Body collider from a closed triangle mesh: nv vertices (3 floats each, metres), nt triangles (3 ints each,
// counter-clockwise seen from outside). Bakes a signed-distance grid with fine spacing h (0 = default 7 mm) and
// enables it. Returns the bake time in ms, or -1 on failure. The grid joins the primitives of fl_set_colliders.
EXPORT(fl_set_body_mesh) double fl_set_body_mesh(const float* pos, int nv, const int* tris, int nt, double h) {
    std::vector<Vec3> V(static_cast<size_t>(nv));
    for (int i = 0; i < nv; ++i) V[static_cast<size_t>(i)] = Vec3(pos[3 * i], pos[3 * i + 1], pos[3 * i + 2]);
    std::vector<std::array<int, 3>> T(static_cast<size_t>(nt));
    for (int i = 0; i < nt; ++i) T[static_cast<size_t>(i)] = {tris[3 * i], tris[3 * i + 1], tris[3 * i + 2]};
    SdfGridParams p;
    if (h > 0) p.h = h;
    auto grid = std::make_shared<SdfGrid>();
    if (!grid->bake(V, T, p)) { gb.grid.reset(); gb.enabled = false; rebuildColliders(); return -1; }
    gb.grid = grid;
    gb.enabled = true;
    const SdfGridStats& st = grid->stats();
    const double v[8] = {st.msTotal, double(st.bricks), double(st.fineSamples), double(st.coarseSamples), st.bytes, double(st.distanceEvals), p.h, double(st.triangles)};
    for (int i = 0; i < 8; ++i) gb.stats[i] = v[i];
    rebuildColliders();
    return st.msTotal;
}
// Per-cloth-vertex displacement (3 floats each) of the body surface over the next frame, for friction; n = 0 clears it.
EXPORT(fl_set_body_motion) void fl_set_body_motion(const float* d, int n) {
    if (!g.solver) return;
    std::vector<Vec3> v;
    for (int i = 0; i < n; ++i) v.push_back(Vec3(d[3 * i], d[3 * i + 1], d[3 * i + 2]));
    g.solver->setColliderMotion(std::move(v));
}
EXPORT(fl_body_enable) void fl_body_enable(int on) { gb.enabled = on != 0 && gb.grid != nullptr; rebuildColliders(); }
// Last bake: [ms, bricks, fine samples, coarse samples, bytes, distance evaluations, h, triangles]
EXPORT(fl_body_stats) const double* fl_body_stats() { return gb.stats; }
// Signed distance and outward normal of the body at n points (3 floats each): 4 floats per point into out.
EXPORT(fl_body_eval) void fl_body_eval(const float* pts, int n, float* out) {
    for (int i = 0; i < n; ++i) {
        Vec3 nrm(0, 1, 0);
        double d = 1e30;
        if (gb.grid) { GridCollider c(gb.grid); d = c.eval(Vec3(pts[3 * i], pts[3 * i + 1], pts[3 * i + 2]), nrm); }
        out[4 * i] = static_cast<float>(d); out[4 * i + 1] = static_cast<float>(nrm.x); out[4 * i + 2] = static_cast<float>(nrm.y); out[4 * i + 3] = static_cast<float>(nrm.z);
    }
}
// Display mesh of the cloth: every edge (i,j) gets a midpoint vertex (n + e), and every triangle becomes four
// (dtris). Set once per simulation, after the panels are meshed.
EXPORT(fl_display_setup) void fl_display_setup(const int* edges, int ne, const int* dtris, int ndt) {
    g.dispEdges.assign(edges, edges + 2 * ne);
    g.dispTris.assign(dtris, dtris + 3 * ndt);
}

// Cloth positions for display: 3 floats for each of the n + ne display vertices. Flat cloth triangles resting on a
// curved body cut into it between their vertices (a 4 cm triangle on a 10 cm radius hip sags ~2.7 mm, more than the
// 1.65 mm contact offset). So (1) each edge midpoint near the body is pushed out along the body normal until it is
// as far from the body as the nearer of its two vertices (the cloth follows the body instead of its chord), and
// (2) where the centroid or an edge midpoint of a display triangle is still closer than `margin` metres, that
// triangle's vertices are lifted along the body normal by the shortfall (at most 4 mm).
EXPORT(fl_display_positions) const float* fl_display_positions(double margin) {
    const auto& x = g.solver->positions();
    const size_t n = x.size(), ne = g.dispEdges.size() / 2, nd = n + ne;
    std::vector<Vec3> P(nd), N(nd, Vec3(0, 1, 0));
    std::vector<Real> phi(nd, 1e30), lift(nd, 0);
    for (size_t i = 0; i < n; ++i) P[i] = x[i];
    if (g.collider)
        for (size_t i = 0; i < n; ++i) phi[i] = g.collider->eval(x[i], N[i]);
    for (size_t e = 0; e < ne; ++e) {
        const int i = g.dispEdges[2 * e], j = g.dispEdges[2 * e + 1];
        Vec3& m = P[n + e];
        m = (x[i] + x[j]) * 0.5;
        if (!g.collider || (phi[i] > 0.012 && phi[j] > 0.012)) continue;
        const Real target = std::min(phi[i], phi[j]);
        Vec3 nm;
        const Real pm = g.collider->eval(m, nm);
        if (pm < target) m += nm * (target - pm);
        phi[n + e] = std::max(pm, target);
        N[n + e] = nm;
    }
    if (g.collider)
        for (size_t t = 0; t + 2 < g.dispTris.size(); t += 3) {
            const int a = g.dispTris[t], b = g.dispTris[t + 1], c = g.dispTris[t + 2];
            if (phi[a] > 0.012 && phi[b] > 0.012 && phi[c] > 0.012) continue;
            const Vec3 probe[4] = {(P[a] + P[b] + P[c]) / 3.0, (P[a] + P[b]) * 0.5, (P[b] + P[c]) * 0.5, (P[c] + P[a]) * 0.5};
            Real low = 1e30;
            for (const Vec3& q : probe) { Vec3 nq; low = std::min(low, g.collider->eval(q, nq)); }
            const Real need = std::min(Real(0.004), margin - low);
            for (int v : {a, b, c}) if (need > lift[v]) lift[v] = need;
        }
    g.dispF.resize(nd * 3);
    for (size_t i = 0; i < nd; ++i) {
        const Vec3 p = P[i] + N[i] * lift[i];
        g.dispF[3 * i] = static_cast<float>(p.x); g.dispF[3 * i + 1] = static_cast<float>(p.y); g.dispF[3 * i + 2] = static_cast<float>(p.z);
    }
    return g.dispF.data();
}

EXPORT(fl_vertex_count) int fl_vertex_count() { return g.solver->vertexCount(); }
EXPORT(fl_triangle_count) int fl_triangle_count() { return g.solver->triangleCount(); }
EXPORT(fl_rest_uv) const float* fl_rest_uv() { return g.restUV.data(); }

EXPORT(fl_triangles) const int* fl_triangles() {
    const auto& t = g.solver->triangles();
    g.triI.resize(t.size() * 3);
    for (size_t i = 0; i < t.size(); ++i) for (int k = 0; k < 3; ++k) g.triI[3 * i + k] = t[i][k];
    return g.triI.data();
}

EXPORT(fl_positions) const float* fl_positions() {
    const auto& x = g.solver->positions();
    g.posF.resize(x.size() * 3);
    for (size_t i = 0; i < x.size(); ++i) { g.posF[3 * i] = static_cast<float>(x[i].x); g.posF[3 * i + 1] = static_cast<float>(x[i].y); g.posF[3 * i + 2] = static_cast<float>(x[i].z); }
    return g.posF.data();
}

// Teleport every vertex (also zeroes velocities): 3 floats per vertex.
EXPORT(fl_set_positions) void fl_set_positions(const float* p) {
    const int n = g.solver->vertexCount();
    for (int i = 0; i < n; ++i) g.solver->setVertexPosition(i, Vec3(p[3 * i], p[3 * i + 1], p[3 * i + 2]));
}

EXPORT(fl_pin) void fl_pin(int v, int pinned) { g.solver->pinVertex(v, pinned != 0); }
EXPORT(fl_step) void fl_step(int n) { for (int i = 0; i < n; ++i) g.solver->step(); }
EXPORT(fl_kinetic) double fl_kinetic() { return g.solver->kineticEnergy(); }
EXPORT(fl_seam_gap) double fl_seam_gap() { return g.solver->maxSeamGap(); }
EXPORT(fl_clearance) double fl_clearance() { return g.solver->minBodyClearance(); }
EXPORT(fl_frames) int fl_frames() { return g.solver->framesSimulated(); }
// Solver diagnostics: [line-search failures, Newton iterations (total), substeps (total), CG iterations of the last substep]
EXPORT(fl_solver_stats) const double* fl_solver_stats() {
    static double s[4];
    s[0] = g.solver->lineSearchFailures(); s[1] = static_cast<double>(g.solver->newtonIterations());
    s[2] = static_cast<double>(g.solver->substepsDone()); s[3] = g.solver->lastCgIterations();
    return s;
}

}  // extern "C"
