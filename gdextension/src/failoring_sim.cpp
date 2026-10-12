#include "failoring_sim.h"

#include <algorithm>

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "failoring/collider.hpp"
#include "failoring/mesh_sizing.hpp"
#include "failoring/mesher.hpp"

using namespace godot;

namespace {
inline fl::Vec3 toFl(const Vector3& v) { return fl::Vec3(v.x, v.y, v.z); }
inline Vector3 toGd(const fl::Vec3& v) { return Vector3(static_cast<real_t>(v.x), static_cast<real_t>(v.y), static_cast<real_t>(v.z)); }
}  // namespace

FailoringSim::FailoringSim() { solver_ = std::make_unique<fl::ClothSolver>(params_); }

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
    return solver_->addPanel(fl::meshRect(width, height, h), *m, pl, grain_angle, name.utf8().get_data());
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
    if (!mesh.boundaryComplete) UtilityFunctions::push_warning("FailoringSim: panel '", name, "' has features thinner than the edge length; use a smaller edge_length");
    fl::PanelPlacement pl;
    pl.origin = toFl(origin); pl.ex = toFl(ex); pl.ey = toFl(ey);
    return solver_->addPanel(mesh, *m, pl, grain_angle, name.utf8().get_data());
}

void FailoringSim::add_seam(int a, int ea, int b, int eb, bool reverse) { solver_->addSeam(a, ea, b, eb, reverse); }
void FailoringSim::pin_vertex(int v, bool pinned) { solver_->pinVertex(v, pinned); }
void FailoringSim::set_vertex_position(int v, Vector3 p) { solver_->setVertexPosition(v, toFl(p)); }
int FailoringSim::get_first_vertex(int panel) const { return solver_->firstVertex(panel); }
int FailoringSim::get_panel_vertex_count(int panel) const { return solver_->vertexCountOfPanel(panel); }

PackedInt32Array FailoringSim::get_edge_chain(int panel, int edge) const {
    PackedInt32Array out;
    for (int v : solver_->edgeChain(panel, edge)) out.push_back(v);
    return out;
}

PackedVector2Array FailoringSim::get_panel_vertices_2d(int panel) const {
    PackedVector2Array out;
    const int first = solver_->firstVertex(panel), n = solver_->vertexCountOfPanel(panel);
    for (int i = 0; i < n; ++i) {
        const fl::Vec2& u = solver_->restUV()[first + i];
        out.push_back(Vector2(u.x, u.y));
    }
    return out;
}

void FailoringSim::rebuildColliders() {
    if (colliders_.empty()) solver_->setCollider(std::shared_ptr<fl::Collider>());
    else if (colliders_.size() == 1) solver_->setCollider(colliders_[0]);
    else {
        auto u = std::make_shared<fl::UnionCollider>();
        for (auto& c : colliders_) u->add(c);
        solver_->setCollider(u);
    }
}
void FailoringSim::clear_colliders() { colliders_.clear(); rebuildColliders(); }
void FailoringSim::add_ellipsoid(Vector3 c, Vector3 r) { colliders_.push_back(std::make_shared<fl::EllipsoidCollider>(toFl(c), toFl(r))); rebuildColliders(); }
void FailoringSim::add_sphere(Vector3 c, real_t r) { colliders_.push_back(std::make_shared<fl::SphereCollider>(toFl(c), r)); rebuildColliders(); }
void FailoringSim::add_capsule(Vector3 a, Vector3 b, real_t r) { colliders_.push_back(std::make_shared<fl::CapsuleCollider>(toFl(a), toFl(b), r)); rebuildColliders(); }
void FailoringSim::add_cylinder(Vector3 c, real_t r, real_t hh) { colliders_.push_back(std::make_shared<fl::CylinderCollider>(toFl(c), r, hh)); rebuildColliders(); }
void FailoringSim::add_plane(Vector3 p, Vector3 n) { colliders_.push_back(std::make_shared<fl::PlaneCollider>(toFl(p), toFl(n))); rebuildColliders(); }

void FailoringSim::set_param(const String& name, real_t v) {
    fl::SolverParams& p = solver_->params();
    if (name == "substeps") p.substeps = static_cast<int>(v);
    else if (name == "newton_iterations") p.newtonIterations = static_cast<int>(v);
    else if (name == "dt") p.dt = v;
    else if (name == "seam_stiffness") p.seamStiffness = v;
    else if (name == "seam_ramp_frames") p.seamRampFrames = static_cast<int>(v);
    else if (name == "self_collision") p.selfCollision = v != 0;
    else if (name == "contact_offset") p.contactOffset = v;
    else if (name == "gravity_y") p.gravity.y = v;
    else UtilityFunctions::push_warning("FailoringSim: unknown parameter ", name);
}

void FailoringSim::step(int frames) { for (int i = 0; i < frames; ++i) solver_->step(); }
void FailoringSim::reset_velocities() { /* velocities are zeroed by set_vertex_position; whole-cloth reset: re-place the panels */ }

PackedVector3Array FailoringSim::get_positions() const {
    PackedVector3Array out;
    const auto& x = solver_->positions();
    out.resize(static_cast<int64_t>(x.size()));
    for (size_t i = 0; i < x.size(); ++i) out.set(static_cast<int64_t>(i), toGd(x[i]));
    return out;
}

PackedInt32Array FailoringSim::get_indices() const {
    PackedInt32Array out;
    const auto& t = solver_->triangles();
    out.resize(static_cast<int64_t>(t.size()) * 3);
    for (size_t i = 0; i < t.size(); ++i)
        for (int k = 0; k < 3; ++k) out.set(static_cast<int64_t>(i) * 3 + k, t[i][k]);
    return out;
}

int FailoringSim::get_vertex_count() const { return solver_->vertexCount(); }
int FailoringSim::get_triangle_count() const { return solver_->triangleCount(); }
real_t FailoringSim::get_kinetic_energy() const { return solver_->kineticEnergy(); }
real_t FailoringSim::get_max_seam_gap() const { return solver_->maxSeamGap(); }
real_t FailoringSim::get_min_body_clearance() const { return solver_->minBodyClearance(); }

void FailoringSim::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_materials", "path"), &FailoringSim::load_materials);
    ClassDB::bind_method(D_METHOD("get_material_ids"), &FailoringSim::get_material_ids);
    ClassDB::bind_method(D_METHOD("get_material", "id"), &FailoringSim::get_material);
    ClassDB::bind_method(D_METHOD("get_recommended_edge_length", "id"), &FailoringSim::get_recommended_edge_length);
    ClassDB::bind_method(D_METHOD("add_rect_panel", "width", "height", "edge_length", "material_id", "origin", "ex", "ey", "grain_angle", "name"), &FailoringSim::add_rect_panel);
    ClassDB::bind_method(D_METHOD("add_polygon_panel", "outline", "edge_length", "material_id", "origin", "ex", "ey", "grain_angle", "name"), &FailoringSim::add_polygon_panel);
    ClassDB::bind_method(D_METHOD("add_seam", "panel_a", "edge_a", "panel_b", "edge_b", "reverse"), &FailoringSim::add_seam);
    ClassDB::bind_method(D_METHOD("pin_vertex", "vertex", "pinned"), &FailoringSim::pin_vertex);
    ClassDB::bind_method(D_METHOD("set_vertex_position", "vertex", "position"), &FailoringSim::set_vertex_position);
    ClassDB::bind_method(D_METHOD("get_first_vertex", "panel"), &FailoringSim::get_first_vertex);
    ClassDB::bind_method(D_METHOD("get_panel_vertex_count", "panel"), &FailoringSim::get_panel_vertex_count);
    ClassDB::bind_method(D_METHOD("get_edge_chain", "panel", "edge"), &FailoringSim::get_edge_chain);
    ClassDB::bind_method(D_METHOD("get_panel_vertices_2d", "panel"), &FailoringSim::get_panel_vertices_2d);
    ClassDB::bind_method(D_METHOD("clear_colliders"), &FailoringSim::clear_colliders);
    ClassDB::bind_method(D_METHOD("add_ellipsoid", "center", "radii"), &FailoringSim::add_ellipsoid);
    ClassDB::bind_method(D_METHOD("add_sphere", "center", "radius"), &FailoringSim::add_sphere);
    ClassDB::bind_method(D_METHOD("add_capsule", "a", "b", "radius"), &FailoringSim::add_capsule);
    ClassDB::bind_method(D_METHOD("add_cylinder", "center", "radius", "half_height"), &FailoringSim::add_cylinder);
    ClassDB::bind_method(D_METHOD("add_plane", "point", "normal"), &FailoringSim::add_plane);
    ClassDB::bind_method(D_METHOD("set_param", "name", "value"), &FailoringSim::set_param);
    ClassDB::bind_method(D_METHOD("step", "frames"), &FailoringSim::step, DEFVAL(1));
    ClassDB::bind_method(D_METHOD("get_positions"), &FailoringSim::get_positions);
    ClassDB::bind_method(D_METHOD("get_indices"), &FailoringSim::get_indices);
    ClassDB::bind_method(D_METHOD("get_vertex_count"), &FailoringSim::get_vertex_count);
    ClassDB::bind_method(D_METHOD("get_triangle_count"), &FailoringSim::get_triangle_count);
    ClassDB::bind_method(D_METHOD("get_kinetic_energy"), &FailoringSim::get_kinetic_energy);
    ClassDB::bind_method(D_METHOD("get_max_seam_gap"), &FailoringSim::get_max_seam_gap);
    ClassDB::bind_method(D_METHOD("get_min_body_clearance"), &FailoringSim::get_min_body_clearance);
}
