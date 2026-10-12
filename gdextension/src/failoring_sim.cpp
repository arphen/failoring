#include "failoring_sim.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "failoring/mesh_sizing.hpp"
#include "failoring/mesher.hpp"

using namespace godot;

namespace {
inline fl::Vec3 toFl(const Vector3& v) { return fl::Vec3(v.x, v.y, v.z); }
inline Vector3 toGd(const fl::Vec3& v) { return Vector3(static_cast<real_t>(v.x), static_cast<real_t>(v.y), static_cast<real_t>(v.z)); }

std::vector<fl::Vec3> toFlArray(const PackedVector3Array& a) {
    std::vector<fl::Vec3> out(static_cast<size_t>(a.size()));
    const Vector3* p = a.ptr();
    for (size_t i = 0; i < out.size(); ++i) out[i] = toFl(p[i]);
    return out;
}

PackedVector3Array toGdArray(const std::vector<fl::Vec3>& v) {
    PackedVector3Array out;
    out.resize(static_cast<int64_t>(v.size()));
    Vector3* p = out.ptrw();
    for (size_t i = 0; i < v.size(); ++i) p[i] = toGd(v[i]);
    return out;
}

// Cloth-vertex displacement from the body's motion: each cloth vertex takes the displacement of the nearest body vertex
// of the previous pose (within 5 cm; cell hash of 3 cm), as the browser's surfaceMotion (app/src/worker.js).
std::vector<fl::Vec3> surfaceMotion(const std::vector<fl::Vec3>& prev, const std::vector<fl::Vec3>& next, const std::vector<fl::Vec3>& cloth) {
    const double cell = 0.03, maxD2 = 0.05 * 0.05;
    auto cellOf = [&](double v) { return static_cast<int64_t>(std::floor(v / cell)); };
    auto key = [](int64_t x, int64_t y, int64_t z) { return static_cast<int64_t>((x * 73856093LL) ^ (y * 19349663LL) ^ (z * 83492791LL)); };
    std::unordered_map<int64_t, std::vector<int>> grid;
    for (size_t i = 0; i < prev.size(); ++i)
        grid[key(cellOf(prev[i].x), cellOf(prev[i].y), cellOf(prev[i].z))].push_back(static_cast<int>(i));
    std::vector<fl::Vec3> out(cloth.size(), fl::Vec3(0, 0, 0));
    for (size_t v = 0; v < cloth.size(); ++v) {
        const fl::Vec3& x = cloth[v];
        const int64_t cx = cellOf(x.x), cy = cellOf(x.y), cz = cellOf(x.z);
        int best = -1;
        double bd = maxD2;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz) {
                    auto it = grid.find(key(cx + dx, cy + dy, cz + dz));
                    if (it == grid.end()) continue;
                    for (int i : it->second) {
                        const fl::Vec3 d = prev[static_cast<size_t>(i)] - x;
                        const double d2 = fl::dot(d, d);
                        if (d2 < bd) { bd = d2; best = i; }
                    }
                }
        if (best >= 0) out[v] = next[static_cast<size_t>(best)] - prev[static_cast<size_t>(best)];
    }
    return out;
}
}  // namespace

FailoringSim::FailoringSim() { resetSolver(); }

void FailoringSim::resetSolver() {
    // a fresh solver keeps the parameters of the previous one (set_param writes into the solver)
    if (solver_) params_ = solver_->params();
    solver_ = std::make_unique<fl::ClothSolver>(params_);
    solver_->setCollider(active_);
    panelEdges_.clear();
    warnings_ = 0;
    dispEdges_.clear();
    dispTris_.clear();
}

void FailoringSim::begin() { resetSolver(); }

bool FailoringSim::load_materials(const String& path) {
    Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
    if (f.is_null()) {
        UtilityFunctions::push_error("FailoringSim: cannot open ", path);
        return false;
    }
    try {
        materials_ = fl::loadMaterialsFromString(f->get_as_text().utf8().get_data());
    } catch (const std::exception& e) {
        UtilityFunctions::push_error("FailoringSim: bad materials file: ", String(e.what()));
        return false;
    }
    return true;
}

PackedStringArray FailoringSim::get_material_ids() const {
    PackedStringArray out;
    for (const auto& m : materials_) out.push_back(String(m.id.c_str()));
    return out;
}

const fl::FabricMaterial* FailoringSim::material(const String& id) const { return fl::findMaterial(materials_, id.utf8().get_data()); }

Dictionary FailoringSim::get_material(const String& id) const {
    Dictionary d;
    const fl::FabricMaterial* m = material(id);
    if (!m) return d;
    d["id"] = String(m->id.c_str());
    d["name"] = String(m->name.c_str());
    d["category"] = String(m->category.c_str());
    d["density_g_m2"] = m->density * 1000.0;
    d["thickness_mm"] = m->thickness * 1000.0;
    d["tensile_warp_N_m"] = m->tensileWarp;
    d["tensile_weft_N_m"] = m->tensileWeft;
    d["shear_N_m_rad"] = m->shear;
    d["bending_warp_Nm"] = m->bendWarp;
    d["bending_weft_Nm"] = m->bendWeft;
    d["friction_static"] = m->frictionStatic;
    return d;
}

real_t FailoringSim::get_recommended_edge_length(const String& id) const {
    const fl::FabricMaterial* m = material(id);
    return m ? static_cast<real_t>(fl::optimalResolution(*m).isotropic()) : real_t(0.01);
}

int FailoringSim::add_rect_panel(real_t width, real_t height, real_t edge_length, const String& material_id, Vector3 origin, Vector3 ex,
                                 Vector3 ey, real_t grain_angle, const String& name) {
    const fl::FabricMaterial* m = material(material_id);
    if (!m) {
        UtilityFunctions::push_error("FailoringSim: unknown material ", material_id);
        return -1;
    }
    const double h = edge_length > 0 ? edge_length : std::max(fl::optimalResolution(*m).isotropic(), 0.006);
    fl::PanelPlacement pl;
    pl.origin = toFl(origin); pl.ex = toFl(ex); pl.ey = toFl(ey);
    const int p = solver_->addPanel(fl::meshRect(width, height, h), *m, pl, grain_angle, name.utf8().get_data());
    panelEdges_.push_back(4);
    return p;
}

int FailoringSim::add_polygon_panel(const PackedVector2Array& outline, real_t edge_length, const String& material_id, Vector3 origin,
                                    Vector3 ex, Vector3 ey, real_t grain_angle, const String& name) {
    const fl::FabricMaterial* m = material(material_id);
    if (!m || outline.size() < 3) {
        UtilityFunctions::push_error("FailoringSim: bad panel (material or outline)");
        return -1;
    }
    std::vector<fl::Vec2> poly;
    for (int i = 0; i < outline.size(); ++i) poly.push_back(fl::Vec2(outline[i].x, outline[i].y));
    const double h = edge_length > 0 ? edge_length : std::max(fl::optimalResolution(*m).isotropic(), 0.006);
    fl::PanelMesh mesh = fl::meshPolygon(poly, h);
    if (!mesh.boundaryComplete) {
        warnings_ |= 1;
        UtilityFunctions::push_warning("FailoringSim: panel '", name, "' has features thinner than the edge length; use a smaller edge_length");
    }
    fl::PanelPlacement pl;
    pl.origin = toFl(origin); pl.ex = toFl(ex); pl.ey = toFl(ey);
    const int p = solver_->addPanel(mesh, *m, pl, grain_angle, name.utf8().get_data());
    panelEdges_.push_back(static_cast<int>(poly.size()));
    return p;
}

bool FailoringSim::validPanel(int panel, const char* who) const {
    if (panel >= 0 && panel < static_cast<int>(panelEdges_.size())) return true;
    UtilityFunctions::push_error("FailoringSim: ", who, ": no panel ", panel);
    return false;
}

bool FailoringSim::add_seam(int a, int ea, int b, int eb, bool reverse) {
    if (!validPanel(a, "add_seam") || !validPanel(b, "add_seam")) return false;
    if (ea < 0 || ea >= panelEdges_[a] || eb < 0 || eb >= panelEdges_[b]) {
        UtilityFunctions::push_error("FailoringSim: add_seam: edge out of range");
        return false;
    }
    solver_->addSeam(a, ea, b, eb, reverse);
    return true;
}

void FailoringSim::pin_vertex(int v, bool pinned) {
    if (v < 0 || v >= solver_->vertexCount()) return;
    solver_->pinVertex(v, pinned);
}
void FailoringSim::set_vertex_position(int v, Vector3 p) {
    if (v < 0 || v >= solver_->vertexCount()) return;
    solver_->setVertexPosition(v, toFl(p));
}
void FailoringSim::set_positions(const PackedVector3Array& positions) {
    if (positions.size() != solver_->vertexCount()) {
        UtilityFunctions::push_error("FailoringSim: set_positions expects one position per cloth vertex");
        return;
    }
    const Vector3* p = positions.ptr();
    for (int i = 0; i < positions.size(); ++i) solver_->setVertexPosition(i, toFl(p[i]));
}

int FailoringSim::get_first_vertex(int panel) const { return validPanel(panel, "get_first_vertex") ? solver_->firstVertex(panel) : -1; }
int FailoringSim::get_panel_vertex_count(int panel) const { return validPanel(panel, "get_panel_vertex_count") ? solver_->vertexCountOfPanel(panel) : 0; }
int FailoringSim::get_first_triangle(int panel) const { return validPanel(panel, "get_first_triangle") ? solver_->firstTriangle(panel) : -1; }
int FailoringSim::get_panel_triangle_count(int panel) const { return validPanel(panel, "get_panel_triangle_count") ? solver_->triangleCountOfPanel(panel) : 0; }
int FailoringSim::get_panel_count() const { return solver_->panelCount(); }
int FailoringSim::get_warnings() const { return warnings_; }

PackedInt32Array FailoringSim::get_edge_chain(int panel, int edge) const {
    PackedInt32Array out;
    if (!validPanel(panel, "get_edge_chain") || edge < 0 || edge >= panelEdges_[panel]) return out;
    for (int v : solver_->edgeChain(panel, edge)) out.push_back(v);
    return out;
}

PackedVector2Array FailoringSim::get_panel_vertices_2d(int panel) const {
    PackedVector2Array out;
    if (!validPanel(panel, "get_panel_vertices_2d")) return out;
    const int first = solver_->firstVertex(panel), n = solver_->vertexCountOfPanel(panel);
    for (int i = 0; i < n; ++i) {
        const fl::Vec2& u = solver_->restUV()[first + i];
        out.push_back(Vector2(u.x, u.y));
    }
    return out;
}

void FailoringSim::rebuildColliders() {
    std::vector<std::shared_ptr<fl::Collider>> parts = colliders_;
    if (body_.enabled && body_.grid && !body_.grid->empty()) parts.push_back(std::make_shared<fl::GridCollider>(body_.grid));
    if (parts.empty()) active_.reset();
    else if (parts.size() == 1) active_ = parts[0];
    else {
        auto u = std::make_shared<fl::UnionCollider>();
        for (auto& c : parts) u->add(c);
        active_ = u;
    }
    solver_->setCollider(active_);
}

void FailoringSim::clear_colliders() { colliders_.clear(); rebuildColliders(); }
void FailoringSim::add_ellipsoid(Vector3 c, Vector3 r) { colliders_.push_back(std::make_shared<fl::EllipsoidCollider>(toFl(c), toFl(r))); rebuildColliders(); }
void FailoringSim::add_sphere(Vector3 c, real_t r) { colliders_.push_back(std::make_shared<fl::SphereCollider>(toFl(c), r)); rebuildColliders(); }
void FailoringSim::add_capsule(Vector3 a, Vector3 b, real_t r) { colliders_.push_back(std::make_shared<fl::CapsuleCollider>(toFl(a), toFl(b), r)); rebuildColliders(); }
void FailoringSim::add_cylinder(Vector3 c, real_t r, real_t hh) { colliders_.push_back(std::make_shared<fl::CylinderCollider>(toFl(c), r, hh)); rebuildColliders(); }
void FailoringSim::add_plane(Vector3 p, Vector3 n) { colliders_.push_back(std::make_shared<fl::PlaneCollider>(toFl(p), toFl(n))); rebuildColliders(); }

real_t FailoringSim::set_body_mesh(const PackedVector3Array& vertices, const PackedInt32Array& triangles, real_t edge_length) {
    const int64_t nv = vertices.size(), nt = triangles.size() / 3;
    if (nv == 0 || nt == 0 || triangles.size() % 3 != 0) {
        UtilityFunctions::push_error("FailoringSim: set_body_mesh needs vertices and 3 indices per triangle");
        return -1;
    }
    const int32_t* ti = triangles.ptr();
    std::vector<std::array<int, 3>> tris(static_cast<size_t>(nt));
    for (int64_t i = 0; i < nt; ++i)
        for (int k = 0; k < 3; ++k) {
            const int32_t v = ti[3 * i + k];
            if (v < 0 || v >= nv) {
                UtilityFunctions::push_error("FailoringSim: set_body_mesh: triangle index out of range");
                return -1;
            }
            tris[static_cast<size_t>(i)][k] = v;
        }
    fl::SdfGridParams p;
    if (edge_length > 0) p.h = edge_length;
    std::vector<fl::Vec3> verts = toFlArray(vertices);
    auto grid = std::make_shared<fl::SdfGrid>();
    if (!grid->bake(verts, tris, p)) {
        body_.grid.reset();
        body_.enabled = false;
        rebuildColliders();
        UtilityFunctions::push_error("FailoringSim: set_body_mesh: the mesh could not be baked");
        return -1;
    }
    body_.grid = grid;
    body_.enabled = true;
    body_.verts = std::move(verts);
    body_.tris = std::move(tris);
    body_.h = p.h;
    rebuildColliders();
    return static_cast<real_t>(grid->stats().msTotal);
}

real_t FailoringSim::set_body_positions(const PackedVector3Array& vertices) {
    if (!body_.grid) {
        UtilityFunctions::push_error("FailoringSim: set_body_positions before set_body_mesh");
        return -1;
    }
    if (vertices.size() != static_cast<int64_t>(body_.verts.size())) {
        UtilityFunctions::push_error("FailoringSim: set_body_positions: vertex count differs from set_body_mesh");
        return -1;
    }
    std::vector<fl::Vec3> next = toFlArray(vertices);
    fl::SdfGridParams p;
    p.h = body_.h;
    auto grid = std::make_shared<fl::SdfGrid>();
    if (!grid->bake(next, body_.tris, p)) {
        UtilityFunctions::push_error("FailoringSim: set_body_positions: the body could not be baked");
        return -1;
    }
    const std::vector<fl::Vec3> cloth = solver_->positions();
    solver_->setColliderMotion(surfaceMotion(body_.verts, next, cloth));
    body_.verts = std::move(next);
    body_.grid = grid;
    rebuildColliders();
    return static_cast<real_t>(grid->stats().msTotal);
}

void FailoringSim::set_body_motion(const PackedVector3Array& displacement) { solver_->setColliderMotion(toFlArray(displacement)); }

void FailoringSim::set_body_enabled(bool on) {
    body_.enabled = on && body_.grid != nullptr;
    rebuildColliders();
}

void FailoringSim::clear_body() {
    body_ = Body();
    rebuildColliders();
}

bool FailoringSim::has_body() const { return body_.grid != nullptr; }

Dictionary FailoringSim::get_body_stats() const {
    Dictionary d;
    if (!body_.grid) return d;
    const fl::SdfGridStats& st = body_.grid->stats();
    d["ms"] = st.msTotal;
    d["bricks"] = st.bricks;
    d["fine_samples"] = st.fineSamples;
    d["coarse_samples"] = st.coarseSamples;
    d["bytes"] = st.bytes;
    d["distance_evals"] = static_cast<int64_t>(st.distanceEvals);
    d["edge_length"] = body_.h;
    d["triangles"] = st.triangles;
    return d;
}

PackedFloat32Array FailoringSim::body_eval(const PackedVector3Array& points) const {
    PackedFloat32Array out;
    out.resize(points.size() * 4);
    float* o = out.ptrw();
    const Vector3* p = points.ptr();
    const bool on = body_.grid && body_.enabled;
    const fl::GridCollider c(on ? body_.grid : nullptr);
    for (int64_t i = 0; i < points.size(); ++i) {
        fl::Vec3 n(0, 1, 0);
        double d = 1e30;
        if (on) d = c.eval(toFl(p[i]), n);
        o[4 * i] = static_cast<float>(d);
        o[4 * i + 1] = static_cast<float>(n.x);
        o[4 * i + 2] = static_cast<float>(n.y);
        o[4 * i + 3] = static_cast<float>(n.z);
    }
    return out;
}

void FailoringSim::setup_display() {
    const auto& tris = solver_->triangles();
    const int64_t n = solver_->vertexCount();
    dispEdges_.clear();
    dispTris_.clear();
    std::unordered_map<int64_t, int> edgeOf;
    auto mid = [&](int a, int b) {  // the midpoint vertex n + e of the edge (a, b)
        const int64_t k = a < b ? a * n + b : b * n + a;
        auto it = edgeOf.find(k);
        if (it != edgeOf.end()) return static_cast<int>(n) + it->second;
        const int e = static_cast<int>(dispEdges_.size() / 2);
        edgeOf.emplace(k, e);
        dispEdges_.push_back(a);
        dispEdges_.push_back(b);
        return static_cast<int>(n) + e;
    };
    dispTris_.reserve(tris.size() * 12);
    for (const auto& t : tris) {
        const int a = t[0], b = t[1], c = t[2];
        const int ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
        const int quad[12] = {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca};
        dispTris_.insert(dispTris_.end(), quad, quad + 12);
    }
}

PackedInt32Array FailoringSim::get_display_indices() const {
    PackedInt32Array out;
    out.resize(static_cast<int64_t>(dispTris_.size()));
    int32_t* o = out.ptrw();
    for (size_t i = 0; i < dispTris_.size(); ++i) o[i] = dispTris_[i];
    return out;
}

PackedVector3Array FailoringSim::get_display_positions(real_t margin) const {
    const auto& x = solver_->positions();
    const size_t n = x.size(), ne = dispEdges_.size() / 2, nd = n + ne;
    std::vector<fl::Vec3> P(nd), N(nd, fl::Vec3(0, 1, 0));
    std::vector<fl::Real> phi(nd, 1e30), lift(nd, 0);
    for (size_t i = 0; i < n; ++i) P[i] = x[i];
    if (active_)
        for (size_t i = 0; i < n; ++i) phi[i] = active_->eval(x[i], N[i]);
    for (size_t e = 0; e < ne; ++e) {
        const int i = dispEdges_[2 * e], j = dispEdges_[2 * e + 1];
        fl::Vec3& m = P[n + e];
        m = (x[i] + x[j]) * 0.5;
        if (!active_ || (phi[i] > 0.012 && phi[j] > 0.012)) continue;
        const fl::Real target = std::min(phi[i], phi[j]);
        fl::Vec3 nm;
        const fl::Real pm = active_->eval(m, nm);
        if (pm < target) m += nm * (target - pm);
        phi[n + e] = std::max(pm, target);
        N[n + e] = nm;
    }
    if (active_)
        for (size_t t = 0; t + 2 < dispTris_.size(); t += 3) {
            const int a = dispTris_[t], b = dispTris_[t + 1], c = dispTris_[t + 2];
            if (phi[a] > 0.012 && phi[b] > 0.012 && phi[c] > 0.012) continue;
            const fl::Vec3 probe[4] = {(P[a] + P[b] + P[c]) / 3.0, (P[a] + P[b]) * 0.5, (P[b] + P[c]) * 0.5, (P[c] + P[a]) * 0.5};
            fl::Real low = 1e30;
            for (const fl::Vec3& q : probe) {
                fl::Vec3 nq;
                low = std::min(low, active_->eval(q, nq));
            }
            const fl::Real need = std::min(fl::Real(0.004), margin - low);
            for (int v : {a, b, c})
                if (need > lift[v]) lift[v] = need;
        }
    PackedVector3Array out;
    out.resize(static_cast<int64_t>(nd));
    Vector3* o = out.ptrw();
    for (size_t i = 0; i < nd; ++i) o[i] = toGd(P[i] + N[i] * lift[i]);
    return out;
}

void FailoringSim::set_param(const String& name, real_t v) {
    fl::SolverParams& p = solver_->params();
    if (name == "substeps") p.substeps = static_cast<int>(v);
    else if (name == "newton_iterations") p.newtonIterations = static_cast<int>(v);
    else if (name == "dt") p.dt = v;
    else if (name == "seam_stiffness") p.seamStiffness = v;
    else if (name == "seam_ramp_frames") p.seamRampFrames = static_cast<int>(v);
    else if (name == "self_collision") p.selfCollision = v != 0;
    else if (name == "contact_offset") p.contactOffset = v;
    else if (name == "contact_band") p.contactBand = v;
    else if (name == "tension_stiffness") p.tensionStiffness = v != 0;
    else if (name == "gravity_y") p.gravity.y = v;
    else UtilityFunctions::push_warning("FailoringSim: unknown parameter ", name);
}

void FailoringSim::step(int frames) { for (int i = 0; i < frames; ++i) solver_->step(); }
void FailoringSim::reset_velocities() { /* velocities are zeroed by set_vertex_position / set_positions; kept for compatibility */ }

PackedVector3Array FailoringSim::get_positions() const { return toGdArray(solver_->positions()); }

PackedInt32Array FailoringSim::get_indices() const {
    PackedInt32Array out;
    const auto& t = solver_->triangles();
    out.resize(static_cast<int64_t>(t.size()) * 3);
    int32_t* o = out.ptrw();
    for (size_t i = 0; i < t.size(); ++i)
        for (int k = 0; k < 3; ++k) o[3 * i + k] = t[i][k];
    return out;
}

int FailoringSim::get_vertex_count() const { return solver_->vertexCount(); }
int FailoringSim::get_triangle_count() const { return solver_->triangleCount(); }
int FailoringSim::get_frames() const { return solver_->framesSimulated(); }
real_t FailoringSim::get_kinetic_energy() const { return solver_->kineticEnergy(); }
real_t FailoringSim::get_max_seam_gap() const { return solver_->maxSeamGap(); }
real_t FailoringSim::get_min_body_clearance() const { return solver_->minBodyClearance(); }

void FailoringSim::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_materials", "path"), &FailoringSim::load_materials);
    ClassDB::bind_method(D_METHOD("get_material_ids"), &FailoringSim::get_material_ids);
    ClassDB::bind_method(D_METHOD("get_material", "id"), &FailoringSim::get_material);
    ClassDB::bind_method(D_METHOD("get_recommended_edge_length", "id"), &FailoringSim::get_recommended_edge_length);

    ClassDB::bind_method(D_METHOD("begin"), &FailoringSim::begin);
    ClassDB::bind_method(D_METHOD("add_rect_panel", "width", "height", "edge_length", "material_id", "origin", "ex", "ey", "grain_angle", "name"), &FailoringSim::add_rect_panel);
    ClassDB::bind_method(D_METHOD("add_polygon_panel", "outline", "edge_length", "material_id", "origin", "ex", "ey", "grain_angle", "name"), &FailoringSim::add_polygon_panel);
    ClassDB::bind_method(D_METHOD("add_seam", "panel_a", "edge_a", "panel_b", "edge_b", "reverse"), &FailoringSim::add_seam);
    ClassDB::bind_method(D_METHOD("pin_vertex", "vertex", "pinned"), &FailoringSim::pin_vertex);
    ClassDB::bind_method(D_METHOD("set_vertex_position", "vertex", "position"), &FailoringSim::set_vertex_position);
    ClassDB::bind_method(D_METHOD("set_positions", "positions"), &FailoringSim::set_positions);
    ClassDB::bind_method(D_METHOD("get_first_vertex", "panel"), &FailoringSim::get_first_vertex);
    ClassDB::bind_method(D_METHOD("get_panel_vertex_count", "panel"), &FailoringSim::get_panel_vertex_count);
    ClassDB::bind_method(D_METHOD("get_first_triangle", "panel"), &FailoringSim::get_first_triangle);
    ClassDB::bind_method(D_METHOD("get_panel_triangle_count", "panel"), &FailoringSim::get_panel_triangle_count);
    ClassDB::bind_method(D_METHOD("get_panel_count"), &FailoringSim::get_panel_count);
    ClassDB::bind_method(D_METHOD("get_edge_chain", "panel", "edge"), &FailoringSim::get_edge_chain);
    ClassDB::bind_method(D_METHOD("get_panel_vertices_2d", "panel"), &FailoringSim::get_panel_vertices_2d);
    ClassDB::bind_method(D_METHOD("get_warnings"), &FailoringSim::get_warnings);

    ClassDB::bind_method(D_METHOD("clear_colliders"), &FailoringSim::clear_colliders);
    ClassDB::bind_method(D_METHOD("add_ellipsoid", "center", "radii"), &FailoringSim::add_ellipsoid);
    ClassDB::bind_method(D_METHOD("add_sphere", "center", "radius"), &FailoringSim::add_sphere);
    ClassDB::bind_method(D_METHOD("add_capsule", "a", "b", "radius"), &FailoringSim::add_capsule);
    ClassDB::bind_method(D_METHOD("add_cylinder", "center", "radius", "half_height"), &FailoringSim::add_cylinder);
    ClassDB::bind_method(D_METHOD("add_plane", "point", "normal"), &FailoringSim::add_plane);

    ClassDB::bind_method(D_METHOD("set_body_mesh", "vertices", "triangles", "edge_length"), &FailoringSim::set_body_mesh, DEFVAL(0.0));
    ClassDB::bind_method(D_METHOD("set_body_positions", "vertices"), &FailoringSim::set_body_positions);
    ClassDB::bind_method(D_METHOD("set_body_motion", "displacement"), &FailoringSim::set_body_motion);
    ClassDB::bind_method(D_METHOD("set_body_enabled", "on"), &FailoringSim::set_body_enabled);
    ClassDB::bind_method(D_METHOD("clear_body"), &FailoringSim::clear_body);
    ClassDB::bind_method(D_METHOD("has_body"), &FailoringSim::has_body);
    ClassDB::bind_method(D_METHOD("get_body_stats"), &FailoringSim::get_body_stats);
    ClassDB::bind_method(D_METHOD("body_eval", "points"), &FailoringSim::body_eval);

    ClassDB::bind_method(D_METHOD("setup_display"), &FailoringSim::setup_display);
    ClassDB::bind_method(D_METHOD("get_display_indices"), &FailoringSim::get_display_indices);
    ClassDB::bind_method(D_METHOD("get_display_positions", "margin"), &FailoringSim::get_display_positions, DEFVAL(0.0008));

    ClassDB::bind_method(D_METHOD("set_param", "name", "value"), &FailoringSim::set_param);
    ClassDB::bind_method(D_METHOD("step", "frames"), &FailoringSim::step, DEFVAL(1));
    ClassDB::bind_method(D_METHOD("reset_velocities"), &FailoringSim::reset_velocities);

    ClassDB::bind_method(D_METHOD("get_positions"), &FailoringSim::get_positions);
    ClassDB::bind_method(D_METHOD("get_indices"), &FailoringSim::get_indices);
    ClassDB::bind_method(D_METHOD("get_vertex_count"), &FailoringSim::get_vertex_count);
    ClassDB::bind_method(D_METHOD("get_triangle_count"), &FailoringSim::get_triangle_count);
    ClassDB::bind_method(D_METHOD("get_frames"), &FailoringSim::get_frames);
    ClassDB::bind_method(D_METHOD("get_kinetic_energy"), &FailoringSim::get_kinetic_energy);
    ClassDB::bind_method(D_METHOD("get_max_seam_gap"), &FailoringSim::get_max_seam_gap);
    ClassDB::bind_method(D_METHOD("get_min_body_clearance"), &FailoringSim::get_min_body_clearance);
}
