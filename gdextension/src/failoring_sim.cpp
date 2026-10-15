#include "failoring_sim.h"
#include "gpu_cg.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
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

// Edge length of a panel: a positive request is used as given (the browser passes its value directly); otherwise the
// estimator's recommendation, floored at 6 mm.
double panelEdgeLength(const fl::FabricMaterial& m, double requested) {
    return requested > 0 ? requested : std::max(fl::optimalResolution(m).isotropic(), 0.006);
}
}  // namespace

FailoringSim::FailoringSim() { resetSolver(); }

void FailoringSim::resetSolver() {
    // a fresh solver keeps the parameters of the previous one (set_param writes into the solver)
    if (solver_) params_ = solver_->params();
    solver_ = std::make_unique<fl::ClothSolver>(params_);
    solver_->setCollider(active_);
    attachGpu();
    panelEdges_.clear();
    warnings_ = 0;
    panelUV_.clear();
    dispEdges_.clear();
    dispTris_.clear();
    dispVertexCount_ = -1;
}

void FailoringSim::begin() { resetSolver(); }

// The GPU conjugate gradient (gpu_cg.h) as the solver's linear solver (its device is created by set_param("gpu")). When
// it cannot run (no device, shader error) each solve returns -1 and the solver uses its CPU CG.
void FailoringSim::attachGpu() {
    if (!gpu_) { solver_->setExternalLinearSolver(nullptr); return; }
    std::shared_ptr<GpuCg> g = gpu_;
    solver_->setExternalLinearSolver([g](const fl::LinearSystemView& v, std::vector<fl::Vec3>& x) { return g->solve(v, x); });
}

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
    d["compression_ratio"] = m->compressionRatio;
    d["friction_static"] = m->frictionStatic;
    d["friction_kinetic"] = m->frictionKinetic;
    d["damping"] = m->damping;
    return d;
}

real_t FailoringSim::get_recommended_edge_length(const String& id) const {
    const fl::FabricMaterial* m = material(id);
    return m ? static_cast<real_t>(fl::optimalResolution(*m).isotropic()) : real_t(0.01);
}

int FailoringSim::addOutlinePanel(std::vector<fl::Vec2> poly, real_t edge_length, const String& material_id, Vector3 origin, Vector3 ex,
                                  Vector3 ey, real_t grain_angle, const String& name, const std::vector<std::vector<fl::Vec2>>& holes) {
    const fl::FabricMaterial* m = material(material_id);
    if (!m) {
        UtilityFunctions::push_error("FailoringSim: unknown material ", material_id);
        return -1;
    }
    if (poly.size() < 3) {
        UtilityFunctions::push_error("FailoringSim: panel '", name, "' needs an outline of at least 3 points");
        return -1;
    }
    // counter-clockwise outlines, as the browser's FL.normalizePiece: seam edge numbers refer to this order
    if (fl::polygonSignedArea(poly) < 0) std::reverse(poly.begin(), poly.end());
    try {
        fl::PanelMesh mesh = fl::meshPolygon(poly, holes, panelEdgeLength(*m, edge_length));
        if (!mesh.boundaryComplete) {
            warnings_ |= 1;
            UtilityFunctions::push_warning("FailoringSim: panel '", name, "' has features thinner than the edge length; use a smaller edge_length");
        }
        fl::PanelPlacement pl;
        pl.origin = toFl(origin); pl.ex = toFl(ex); pl.ey = toFl(ey);
        const int p = solver_->addPanel(mesh, *m, pl, grain_angle, name.utf8().get_data());
        panelEdges_.push_back(static_cast<int>(poly.size()));
        panelUV_.push_back(mesh.verts);
        return p;
    } catch (const std::exception& e) {
        UtilityFunctions::push_error("FailoringSim: cannot add panel '", name, "': ", String(e.what()), " (begin() starts a new simulation)");
        return -1;
    }
}

int FailoringSim::add_rect_panel(real_t width, real_t height, real_t edge_length, const String& material_id, Vector3 origin, Vector3 ex,
                                 Vector3 ey, real_t grain_angle, const String& name) {
    if (!(width > 0 && height > 0)) {
        UtilityFunctions::push_error("FailoringSim: add_rect_panel: width and height must be positive");
        return -1;
    }
    // the same mesher as every browser panel; outline edges 0 bottom, 1 right, 2 top, 3 left
    std::vector<fl::Vec2> poly = {fl::Vec2(0, 0), fl::Vec2(width, 0), fl::Vec2(width, height), fl::Vec2(0, height)};
    return addOutlinePanel(std::move(poly), edge_length, material_id, origin, ex, ey, grain_angle, name);
}

int FailoringSim::add_polygon_panel(const PackedVector2Array& outline, real_t edge_length, const String& material_id, Vector3 origin,
                                    Vector3 ex, Vector3 ey, real_t grain_angle, const String& name) {
    std::vector<fl::Vec2> poly;
    for (int i = 0; i < outline.size(); ++i) poly.push_back(fl::Vec2(outline[i].x, outline[i].y));
    return addOutlinePanel(std::move(poly), edge_length, material_id, origin, ex, ey, grain_angle, name);
}

int FailoringSim::add_polygon_panel_with_holes(const PackedVector2Array& outline, const Array& holes, real_t edge_length,
                                               const String& material_id, Vector3 origin, Vector3 ex, Vector3 ey, real_t grain_angle,
                                               const String& name) {
    std::vector<fl::Vec2> poly;
    for (int i = 0; i < outline.size(); ++i) poly.push_back(fl::Vec2(outline[i].x, outline[i].y));
    std::vector<std::vector<fl::Vec2>> hs;
    for (int h = 0; h < holes.size(); ++h) {
        PackedVector2Array hp = holes[h];
        std::vector<fl::Vec2> hole;
        for (int i = 0; i < hp.size(); ++i) hole.push_back(fl::Vec2(hp[i].x, hp[i].y));
        hs.push_back(std::move(hole));
    }
    return addOutlinePanel(std::move(poly), edge_length, material_id, origin, ex, ey, grain_angle, name, hs);
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
    try {
        solver_->addSeam(a, ea, b, eb, reverse);
    } catch (const std::exception& e) {
        UtilityFunctions::push_error("FailoringSim: add_seam: ", String(e.what()), " (begin() starts a new simulation)");
        return false;
    }
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
    for (const fl::Vec2& u : panelUV_[static_cast<size_t>(panel)]) out.push_back(Vector2(u.x, u.y));
    return out;
}

void FailoringSim::rebuildColliders() {
    std::vector<std::shared_ptr<fl::Collider>> parts = colliders_;
    if (body_.enabled && body_.grid && !body_.grid->empty()) parts.push_back(std::make_shared<fl::GridCollider>(body_.grid));
    for (const BoxProp& bp : props_) parts.push_back(bp.box);
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

// ---- box props ---------------------------------------------------------------------------------------------------------
namespace {
BoxFrame frameOf(const Transform3D& xf) {
    BoxFrame f;
    f.origin = toFl(xf.origin);
    for (int i = 0; i < 3; ++i) f.axis[i] = toFl(xf.basis.get_column(i));
    return f;
}
BoxFrame frameOfBox(const OrientedBoxCollider& b) { return b.frame; }
}  // namespace

fl::Real OrientedBoxCollider::eval(const fl::Vec3& p, fl::Vec3& n) const {
    const fl::Vec3 d = p - frame.origin;
    const fl::Real l[3] = {dot(d, frame.axis[0]), dot(d, frame.axis[1]), dot(d, frame.axis[2])};
    const fl::Real h[3] = {half.x, half.y, half.z};
    fl::Real q[3], nl[3] = {0, 0, 0};
    bool outside = false;
    for (int i = 0; i < 3; ++i) {
        q[i] = std::fabs(l[i]) - h[i];
        if (q[i] > 0) {
            outside = true;
            nl[i] = l[i] >= 0 ? q[i] : -q[i];
        }
    }
    fl::Real dist;
    if (outside) {
        const fl::Real len = std::sqrt(nl[0] * nl[0] + nl[1] * nl[1] + nl[2] * nl[2]);
        dist = len;
        for (fl::Real& c : nl) c /= len;
    } else {
        int m = 0;
        for (int i = 1; i < 3; ++i)
            if (q[i] > q[m]) m = i;
        dist = q[m];
        nl[m] = l[m] >= 0 ? 1 : -1;
    }
    n = frame.axis[0] * nl[0] + frame.axis[1] * nl[1] + frame.axis[2] * nl[2];
    return dist;
}

// Rigid motion of the prop from its previous frame to the current one, applied to a point x on its surface.
fl::Vec3 FailoringSim::BoxProp::displacement(const fl::Vec3& x) const {
    const fl::Vec3 d = x - prev.origin;
    const fl::Real l0 = dot(d, prev.axis[0]), l1 = dot(d, prev.axis[1]), l2 = dot(d, prev.axis[2]);
    const fl::Vec3 xn = box->frame.origin + box->frame.axis[0] * l0 + box->frame.axis[1] * l1 + box->frame.axis[2] * l2;
    return xn - x;
}

int FailoringSim::add_box_prop(Transform3D transform, Vector3 half_extents) {
    BoxProp bp;
    bp.box = std::make_shared<OrientedBoxCollider>();
    bp.box->frame = frameOf(transform);
    bp.box->half = toFl(half_extents);
    bp.prev = frameOfBox(*bp.box);
    props_.push_back(std::move(bp));
    rebuildColliders();
    return static_cast<int>(props_.size()) - 1;
}

void FailoringSim::set_box_prop(int index, Transform3D transform) {
    if (index < 0 || index >= static_cast<int>(props_.size())) {
        UtilityFunctions::push_error("FailoringSim: set_box_prop: no prop ", index);
        return;
    }
    BoxProp& bp = props_[static_cast<size_t>(index)];
    bp.box->frame = frameOf(transform);  // the union holds the same collider object: no rebuild needed
    bp.moved = true;
    propsMoved_ = true;
}

void FailoringSim::clear_box_props() {
    props_.clear();
    propsMoved_ = false;
    bodyMotion_.clear();
    rebuildColliders();
}

int FailoringSim::get_box_prop_count() const { return static_cast<int>(props_.size()); }

// Surface motion for the next frame when props moved: a cloth vertex within 2 cm of a moved prop takes that prop's rigid
// motion; the others keep the body's motion (set_body_positions / set_body_motion).
std::vector<fl::Vec3> FailoringSim::propMotion() const {
    const std::vector<fl::Vec3> cloth = solver_->positions();
    std::vector<fl::Vec3> out(cloth.size(), fl::Vec3(0, 0, 0));
    const fl::Real band = 0.02;
    for (size_t v = 0; v < cloth.size(); ++v) {
        fl::Real best = band;
        const BoxProp* hit = nullptr;
        fl::Vec3 n;
        for (const BoxProp& bp : props_) {
            if (!bp.moved) continue;
            const fl::Real phi = bp.box->eval(cloth[v], n);
            if (phi < best) {
                best = phi;
                hit = &bp;
            }
        }
        if (hit) out[v] = hit->displacement(cloth[v]);
        else if (v < bodyMotion_.size()) out[v] = bodyMotion_[v];
    }
    return out;
}

void FailoringSim::endPropFrame() {
    for (BoxProp& bp : props_) {
        bp.prev = frameOfBox(*bp.box);
        bp.moved = false;
    }
    propsMoved_ = false;
}

bool FailoringSim::set_skin(const PackedVector3Array& bind, const PackedInt32Array& bone_index, const PackedFloat32Array& weight) {
    const int64_t n = bind.size();
    if (bone_index.size() != 4 * n || weight.size() != 4 * n) {
        UtilityFunctions::push_error("FailoringSim: set_skin: expects 4 influences per bind vertex");
        return false;
    }
    skinBind_.assign(static_cast<size_t>(n), Vector3());
    skinBone_.assign(static_cast<size_t>(4 * n), 0);
    skinWeight_.assign(static_cast<size_t>(4 * n), 0.0f);
    for (int64_t i = 0; i < n; ++i) skinBind_[static_cast<size_t>(i)] = bind[i];
    for (int64_t i = 0; i < 4 * n; ++i) {
        skinBone_[static_cast<size_t>(i)] = bone_index[i];
        skinWeight_[static_cast<size_t>(i)] = weight[i];
    }
    return true;
}

PackedByteArray FailoringSim::encode_half(const PackedFloat32Array& values) const {
    PackedByteArray out;
    out.resize(values.size() * 2);
    uint8_t* o = out.ptrw();
    const float* v = values.ptr();
    for (int64_t i = 0; i < values.size(); ++i) {
        const _Float16 h = static_cast<_Float16>(v[i]);
        uint16_t bits;
        std::memcpy(&bits, &h, sizeof bits);
        o[2 * i] = static_cast<uint8_t>(bits & 0xff);
        o[2 * i + 1] = static_cast<uint8_t>(bits >> 8);
    }
    return out;
}

PackedFloat32Array FailoringSim::decode_half(const PackedByteArray& bytes, int64_t offset, int64_t count) const {
    PackedFloat32Array out;
    if (offset < 0 || count < 0 || offset + 2 * count > bytes.size()) {
        UtilityFunctions::push_error("FailoringSim: decode_half: range outside the bytes");
        return out;
    }
    out.resize(count);
    float* o = out.ptrw();
    const uint8_t* b = bytes.ptr() + offset;
    for (int64_t i = 0; i < count; ++i) {
        const uint16_t bits = static_cast<uint16_t>(b[2 * i] | (b[2 * i + 1] << 8));
        _Float16 h;
        std::memcpy(&h, &bits, sizeof h);
        o[i] = static_cast<float>(h);
    }
    return out;
}

PackedVector3Array FailoringSim::skin(const PackedFloat32Array& bone_mats) const {
    PackedVector3Array out;
    if (bone_mats.size() % 12 != 0) {
        UtilityFunctions::push_error("FailoringSim: skin: bone_mats must hold 12 floats per bone");
        return out;
    }
    const int64_t bones = bone_mats.size() / 12;
    out.resize(static_cast<int64_t>(skinBind_.size()));
    Vector3* o = out.ptrw();
    const float* m = bone_mats.ptr();
    for (size_t i = 0; i < skinBind_.size(); ++i) {
        const Vector3 v = skinBind_[i];
        Vector3 acc(0, 0, 0);
        for (size_t j = 0; j < 4; ++j) {
            const float w = skinWeight_[4 * i + j];
            if (w == 0.0f) continue;
            const int k = skinBone_[4 * i + j];
            if (k < 0 || k >= bones) {
                UtilityFunctions::push_error("FailoringSim: skin: bone index out of range");
                return PackedVector3Array();
            }
            const float* a = m + 12 * k;
            acc += Vector3(a[0] * v.x + a[1] * v.y + a[2] * v.z + a[9], a[3] * v.x + a[4] * v.y + a[5] * v.z + a[10],
                           a[6] * v.x + a[7] * v.y + a[8] * v.z + a[11]) * w;
        }
        o[i] = acc;
    }
    return out;
}

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
    pending_.reset();  // a prefetched bake belongs to the old mesh
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

// Bakes the body grid of the given vertices (the same call on the main thread or on a background thread).
static std::shared_ptr<fl::SdfGrid> bakeBody(const std::vector<fl::Vec3>& verts, const std::vector<std::array<int, 3>>& tris, fl::Real h) {
    fl::SdfGridParams p;
    p.h = h;
    auto grid = std::make_shared<fl::SdfGrid>();
    if (!grid->bake(verts, tris, p)) return nullptr;
    return grid;
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
    std::shared_ptr<fl::SdfGrid> grid;
    // the grid depends only on the body vertices: take the one prefetched for exactly these vertices, else bake now
    if (pending_) {
        std::unique_ptr<PendingBake> pend = std::move(pending_);
        bool same = pend->verts.size() == next.size();
        for (size_t i = 0; same && i < next.size(); ++i)
            same = pend->verts[i].x == next[i].x && pend->verts[i].y == next[i].y && pend->verts[i].z == next[i].z;
        if (same) grid = pend->grid.get();  // waits for the background bake
        else pend->grid.wait();                             // a different pose was prefetched: discard it
    }
    if (!grid) grid = bakeBody(next, body_.tris, body_.h);
    if (!grid) {
        UtilityFunctions::push_error("FailoringSim: set_body_positions: the body could not be baked");
        return -1;
    }
    const std::vector<fl::Vec3> cloth = solver_->positions();
    std::vector<fl::Vec3> motion = surfaceMotion(body_.verts, next, cloth);
    if (!props_.empty()) bodyMotion_ = motion;  // kept for the props' step (propMotion); without props nothing changes
    solver_->setColliderMotion(std::move(motion));
    body_.verts = std::move(next);
    body_.grid = grid;
    rebuildColliders();
    return static_cast<real_t>(grid->stats().msTotal);
}

void FailoringSim::prefetch_body_positions(const PackedVector3Array& vertices) {
    if (!body_.grid || vertices.size() != static_cast<int64_t>(body_.verts.size())) return;
    auto pend = std::make_unique<PendingBake>();
    pend->verts = toFlArray(vertices);
    // the thread owns its copies of the vertices and triangles; the solver is not touched
    pend->grid = std::async(std::launch::async, [v = pend->verts, t = body_.tris, h = body_.h]() { return bakeBody(v, t, h); });
    pending_ = std::move(pend);
}

void FailoringSim::set_body_motion(const PackedVector3Array& displacement) {
    if (displacement.size() != 0 && displacement.size() != solver_->vertexCount()) {
        UtilityFunctions::push_error("FailoringSim: set_body_motion expects one displacement per cloth vertex (", solver_->vertexCount(),
                                     "), got ", displacement.size(), "; ignored");
        return;
    }
    std::vector<fl::Vec3> motion = toFlArray(displacement);
    if (!props_.empty()) bodyMotion_ = motion;
    solver_->setColliderMotion(std::move(motion));
}

void FailoringSim::set_body_enabled(bool on) {
    body_.enabled = on && body_.grid != nullptr;
    rebuildColliders();
}

void FailoringSim::clear_body() {
    pending_.reset();
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
    const bool on = body_.grid != nullptr;  // like the browser's fl_body_eval: evaluates whenever a grid exists, enabled or not
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

// Each level splits every display triangle into four at its edge midpoints. Level 1's midpoints are of cloth vertices,
// level 2's of level-1 display vertices; a midpoint is always numbered after both of its ends, so positions, colours and
// UVs can be filled in order (out[n + e] from out[a] and out[b]). Two levels make a 5 cm cloth triangle 16 display
// triangles of 1.25 cm: the chord of a flat 5 cm triangle across a 3.5 cm shin is ~9 mm deep (skin shows through it),
// of a 1.25 cm one ~0.6 mm, and get_display_positions pushes every display vertex out of the body.
void FailoringSim::setup_display() {
    const auto& tris = solver_->triangles();
    const int64_t n = solver_->vertexCount();
    dispEdges_.clear();
    dispTris_.clear();
    std::unordered_map<int64_t, int> edgeOf;
    auto mid = [&](int a, int b) {  // the midpoint vertex n + e of the edge (a, b)
        const int64_t k = a < b ? (int64_t(a) << 32) | b : (int64_t(b) << 32) | a;
        auto it = edgeOf.find(k);
        if (it != edgeOf.end()) return static_cast<int>(n) + it->second;
        const int e = static_cast<int>(dispEdges_.size() / 2);
        edgeOf.emplace(k, e);
        dispEdges_.push_back(a);
        dispEdges_.push_back(b);
        return static_cast<int>(n) + e;
    };
    std::vector<int> cur;
    cur.reserve(tris.size() * 3);
    for (const auto& t : tris) cur.insert(cur.end(), {t[0], t[1], t[2]});
    const int levels = std::max(1, std::min(dispLevels_, 3));
    for (int lv = 0; lv < levels; ++lv) {
        std::vector<int> next;
        next.reserve(cur.size() * 4);
        for (size_t i = 0; i + 2 < cur.size(); i += 3) {
            const int a = cur[i], b = cur[i + 1], c = cur[i + 2];
            const int ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
            next.insert(next.end(), {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca});
        }
        cur.swap(next);
    }
    dispTris_.swap(cur);
    dispVertexCount_ = static_cast<int>(n);
}

PackedInt32Array FailoringSim::get_display_edges() const {
    PackedInt32Array out;
    if (!displayCurrent("get_display_edges")) return out;
    out.resize(static_cast<int64_t>(dispEdges_.size()));
    int32_t* o = out.ptrw();
    for (size_t i = 0; i < dispEdges_.size(); ++i) o[i] = dispEdges_[i];
    return out;
}

// The display topology is tied to the cloth vertex count at setup_display(): the midpoint of edge e is vertex n + e.
bool FailoringSim::displayCurrent(const char* who) const {
    if (dispVertexCount_ == solver_->vertexCount() && dispVertexCount_ >= 0) return true;
    if (dispVertexCount_ < 0)
        UtilityFunctions::push_error("FailoringSim: ", who, ": call setup_display() after the panels are added");
    else
        UtilityFunctions::push_error("FailoringSim: ", who, ": the cloth has ", solver_->vertexCount(), " vertices but setup_display() saw ",
                                     dispVertexCount_, "; call setup_display() again after adding panels");
    return false;
}

PackedInt32Array FailoringSim::get_display_indices() const {
    PackedInt32Array out;
    if (!displayCurrent("get_display_indices")) return out;
    out.resize(static_cast<int64_t>(dispTris_.size()));
    int32_t* o = out.ptrw();
    for (size_t i = 0; i < dispTris_.size(); ++i) o[i] = dispTris_[i];
    return out;
}

PackedVector3Array FailoringSim::get_display_positions(real_t margin) const {
    if (!displayCurrent("get_display_positions")) return PackedVector3Array();
    const auto& x = solver_->positions();
    const size_t n = x.size(), ne = dispEdges_.size() / 2, nd = n + ne;
    std::vector<fl::Vec3> P(nd), N(nd, fl::Vec3(0, 1, 0));
    std::vector<fl::Real> phi(nd, 1e30), lift(nd, 0);
    for (size_t i = 0; i < n; ++i) P[i] = x[i];
    // seams are soft: the two sides sit a few mm apart while walking, and skin shows through the crack. Shown closed: each
    // sewn vertex is drawn on the point of the other side's edge it is sewn to (a shift of the seam gap, a few mm).
    if (dispLevels_ >= 2)
        for (const auto& s : solver_->seamLinks()) P[s.a] = x[s.b0] * (1 - s.t) + x[s.b1] * s.t;
    if (active_)
        for (size_t i = 0; i < n; ++i) phi[i] = active_->eval(P[i], N[i]);
    if (active_ && dispLevels_ >= 2)
        for (size_t i = 0; i < n; ++i)   // cloth vertices closer than the margin are shown at the margin
            for (int it = 0; it < 4 && phi[i] < fl::Real(margin) - 1e-5; ++it) {
                P[i] += N[i] * (fl::Real(margin) - phi[i]);
                phi[i] = active_->eval(P[i], N[i]);
            }
    for (size_t e = 0; e < ne; ++e) {
        const int i = dispEdges_[2 * e], j = dispEdges_[2 * e + 1];
        fl::Vec3& m = P[n + e];
        m = (P[i] + P[j]) * 0.5;
        if (!active_) continue;
        const bool closeEnds = !(phi[i] > 0.012 && phi[j] > 0.012);
        if (dispLevels_ < 2 && !closeEnds) continue;
        // a midpoint keeps the clearance of its ends (at least the margin): over a curved body the straight chord between
        // two cloth vertices dips into the skin, the cloth would follow the body there. With two levels every midpoint is
        // checked, also between two vertices that are far from the body: a coarse sleeve's hem edge can cut straight
        // through the arm with both ends 2 cm away.
        const fl::Real target = dispLevels_ >= 2 ? (closeEnds ? std::max(std::min(phi[i], phi[j]), fl::Real(margin)) : fl::Real(margin))
                                                  : std::min(phi[i], phi[j]);
        fl::Vec3 nm;
        fl::Real pm = active_->eval(m, nm);
        // the body's distance field is exact only within 6 mm of the skin (SdfGrid band); deeper (a chord 2 cm into a
        // swinging arm) one step lands short, so step again from where it landed
        for (int it = 0; it < 4 && pm < target - 1e-5; ++it) {
            m += nm * (target - pm);
            pm = active_->eval(m, nm);
        }
        phi[n + e] = std::max(pm, target);
        N[n + e] = nm;
    }
    if (active_ && dispLevels_ < 2)   // one level: lift whole triangles (at most 4 mm) where their middles dip in
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

PackedVector3Array FailoringSim::get_display_normals(const PackedVector3Array& positions) const {
    PackedVector3Array out;
    if (!displayCurrent("get_display_normals")) return out;
    const int64_t nd = static_cast<int64_t>(dispVertexCount_) + static_cast<int64_t>(dispEdges_.size() / 2);
    if (positions.size() != nd) return out;  // not the display mesh of this sim: the caller computes them another way
    const Vector3* p = positions.ptr();
    // Smooth normals, each face weighted by the angle it subtends at the vertex, so long thin triangles do not pull the
    // shading (as render/godot-editor's cloth_shade.gd did in GDScript; that weighting was lost when the normals moved
    // here, which weighted by area).
    std::vector<Vector3> n(static_cast<size_t>(nd), Vector3());
    for (size_t t = 0; t + 2 < dispTris_.size(); t += 3) {
        const int a = dispTris_[t], b = dispTris_[t + 1], c = dispTris_[t + 2];
        const Vector3 e1 = p[b] - p[a], e2 = p[c] - p[a];
        const Vector3 fn = e1.cross(e2);
        const real_t len = fn.length();
        if (len <= 1e-12) continue;
        const Vector3 u = fn / len;
        n[static_cast<size_t>(a)] += u * e1.angle_to(e2);
        n[static_cast<size_t>(b)] += u * (p[a] - p[b]).angle_to(p[c] - p[b]);
        n[static_cast<size_t>(c)] += u * (p[a] - p[c]).angle_to(p[b] - p[c]);
    }
    out.resize(nd);
    Vector3* o = out.ptrw();
    for (size_t i = 0; i < n.size(); ++i) o[i] = n[i].normalized();
    return out;
}

void FailoringSim::set_param(const String& name, real_t v) {
    fl::SolverParams& p = solver_->params();
    if (name == "substeps") p.substeps = static_cast<int>(v);
    else if (name == "newton_iterations") p.newtonIterations = static_cast<int>(v);
    else if (name == "dt") p.dt = v;
    else if (name == "display_levels") dispLevels_ = std::max(1, std::min(3, static_cast<int>(v)));  // before setup_display()
    else if (name == "seam_stiffness") p.seamStiffness = v;
    else if (name == "seam_ramp_frames") p.seamRampFrames = static_cast<int>(v);
    else if (name == "self_collision") p.selfCollision = v != 0;
    else if (name == "body_friction_mode") p.bodyFrictionMode = static_cast<int>(v);      // 0 after the solve, 2 inside it
    else if (name == "friction_slip_velocity") p.frictionSlipVelocity = v;              // m/s, mode 2 smoothing
    else if (name == "contact_offset") p.contactOffset = v;
    else if (name == "contact_band") p.contactBand = v;
    else if (name == "tension_stiffness") p.tensionStiffness = v != 0;
    else if (name == "gravity_y") p.gravity.y = v;
    else if (name == "gpu") {  // 1: run the conjugate gradient on the GPU (with linear_solver 1)
        // the device is created here, on the calling (main) thread: created on the editor's worker thread and freed on the
        // main thread with the sim, Godot 4.3 crashed (measured with SwiftShader). The worker then only uses it.
        if (v != 0 && !gpu_) {
            gpu_ = std::make_shared<GpuCg>();
            if (!gpu_->ready()) UtilityFunctions::push_warning("FailoringSim: no GPU solver (", String(gpu_->error().c_str()), "), the CPU solves");
        }
        if (v == 0) gpu_.reset();
        attachGpu();
    }
    else if (name == "gpu_verify") { if (gpu_) gpu_->setVerify(v != 0); }  // after "gpu": check every solve on the CPU
    // fast solve (docs/GPU_SOLVER_NOTES.md): linear_solver 0 = Cholesky, 1 = conjugate gradient
    else if (name == "linear_solver") p.linearSolver = v >= 1 ? fl::LinearSolver::ConjugateGradient : fl::LinearSolver::Cholesky;
    else if (name == "cg_block_jacobi") p.cgBlockJacobi = v != 0;
    else if (name == "cg_max_iterations") p.cgMaxIterations = static_cast<int>(v);
    else if (name == "strain_limit") p.strainLimit = v;
    else if (name == "newton_tolerance") p.newtonTolerance = v;
    else UtilityFunctions::push_warning("FailoringSim: unknown parameter ", name);
}

void FailoringSim::step(int frames) {
    try {
        for (int i = 0; i < frames; ++i) {
            // moved props: their motion (and the body's, elsewhere) for this frame. Without props this branch is not taken.
            if (propsMoved_) solver_->setColliderMotion(propMotion());
            solver_->step();
            // The surface motion described one frame; the solver keeps it until replaced, so clear it (the browser sets
            // it before every fl_step(1)). Otherwise a body moved once is dragged against the cloth on every later frame.
            solver_->setColliderMotion({});
            if (!props_.empty()) {
                bodyMotion_.clear();
                endPropFrame();
            }
        }
    } catch (const std::exception& e) {
        UtilityFunctions::push_error("FailoringSim: step failed: ", String(e.what()));
    }
}

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

Dictionary FailoringSim::get_solver_stats() const {
    Dictionary d;
    d["line_search_failures"] = solver_->lineSearchFailures();
    d["newton_iterations"] = static_cast<int64_t>(solver_->newtonIterations());
    d["substeps"] = static_cast<int64_t>(solver_->substepsDone());
    d["cg_iterations"] = solver_->lastCgIterations();
    if (gpu_) {
        d["gpu_ready"] = gpu_->active();
        d["gpu_device"] = String(gpu_->deviceName().c_str());
        d["gpu_error"] = String(gpu_->error().c_str());
        d["gpu_solves"] = static_cast<int64_t>(gpu_->solves());
        d["gpu_last_ms"] = gpu_->lastMs();
        d["gpu_verify_max_rel"] = gpu_->verifyMaxRel();
        d["gpu_verify_ref_rel"] = gpu_->verifyRefRel();
        d["gpu_verify_ratio"] = gpu_->verifyRatio();
        d["gpu_verify_it_diff"] = gpu_->verifyItDiff();
    }
    return d;
}

bool FailoringSim::add_material(const String& id, const Dictionary& props) {
    if (id.is_empty()) {
        UtilityFunctions::push_error("FailoringSim: add_material needs a non-empty id");
        return false;
    }
    fl::FabricMaterial m;
    const fl::FabricMaterial* old = material(id);
    if (old) m = *old;
    m.id = id.utf8().get_data();
    bool ok = true;
    auto num = [&](const char* key, double& field, double scale, bool positive) {
        const String k(key);
        if (!props.has(k)) return;
        const Variant v = props[k];
        if (v.get_type() != Variant::FLOAT && v.get_type() != Variant::INT) {
            UtilityFunctions::push_error("FailoringSim: add_material: '", k, "' must be a number");
            ok = false;
            return;
        }
        const double x = static_cast<double>(v) * scale;
        if (!std::isfinite(x) || (positive ? x <= 0 : x < 0)) {
            UtilityFunctions::push_error("FailoringSim: add_material: '", k, "' is out of range");
            ok = false;
            return;
        }
        field = x;
    };
    num("density_g_m2", m.density, 0.001, true);
    num("thickness_mm", m.thickness, 0.001, true);
    num("tensile_warp_N_m", m.tensileWarp, 1.0, true);
    num("tensile_weft_N_m", m.tensileWeft, 1.0, true);
    num("shear_N_m_rad", m.shear, 1.0, true);
    num("bending_warp_Nm", m.bendWarp, 1.0, true);
    num("bending_weft_Nm", m.bendWeft, 1.0, true);
    num("compression_ratio", m.compressionRatio, 1.0, false);
    num("friction_static", m.frictionStatic, 1.0, false);
    num("friction_kinetic", m.frictionKinetic, 1.0, false);
    num("damping", m.damping, 1.0, false);
    if (props.has(String("name"))) m.name = String(props[String("name")]).utf8().get_data();
    if (props.has(String("category"))) m.category = String(props[String("category")]).utf8().get_data();
    if (!ok) return false;
    if (old) {
        for (auto& e : materials_)
            if (e.id == m.id) e = m;
    } else {
        materials_.push_back(m);
    }
    return true;
}

void FailoringSim::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_materials", "path"), &FailoringSim::load_materials);
    ClassDB::bind_method(D_METHOD("get_material_ids"), &FailoringSim::get_material_ids);
    ClassDB::bind_method(D_METHOD("get_material", "id"), &FailoringSim::get_material);
    ClassDB::bind_method(D_METHOD("get_recommended_edge_length", "id"), &FailoringSim::get_recommended_edge_length);
    ClassDB::bind_method(D_METHOD("add_material", "id", "properties"), &FailoringSim::add_material);

    ClassDB::bind_method(D_METHOD("begin"), &FailoringSim::begin);
    ClassDB::bind_method(D_METHOD("add_rect_panel", "width", "height", "edge_length", "material_id", "origin", "ex", "ey", "grain_angle", "name"), &FailoringSim::add_rect_panel);
    ClassDB::bind_method(D_METHOD("add_polygon_panel", "outline", "edge_length", "material_id", "origin", "ex", "ey", "grain_angle", "name"), &FailoringSim::add_polygon_panel);
    ClassDB::bind_method(D_METHOD("add_polygon_panel_with_holes", "outline", "holes", "edge_length", "material_id", "origin", "ex", "ey", "grain_angle", "name"), &FailoringSim::add_polygon_panel_with_holes);
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
    ClassDB::bind_method(D_METHOD("add_box_prop", "transform", "half_extents"), &FailoringSim::add_box_prop);
    ClassDB::bind_method(D_METHOD("set_box_prop", "index", "transform"), &FailoringSim::set_box_prop);
    ClassDB::bind_method(D_METHOD("clear_box_props"), &FailoringSim::clear_box_props);
    ClassDB::bind_method(D_METHOD("get_box_prop_count"), &FailoringSim::get_box_prop_count);
    ClassDB::bind_method(D_METHOD("set_skin", "bind", "bone_index", "weight"), &FailoringSim::set_skin);
    ClassDB::bind_method(D_METHOD("skin", "bone_mats"), &FailoringSim::skin);
    ClassDB::bind_method(D_METHOD("encode_half", "values"), &FailoringSim::encode_half);
    ClassDB::bind_method(D_METHOD("decode_half", "bytes", "offset", "count"), &FailoringSim::decode_half);

    ClassDB::bind_method(D_METHOD("set_body_mesh", "vertices", "triangles", "edge_length"), &FailoringSim::set_body_mesh, DEFVAL(0.0));
    ClassDB::bind_method(D_METHOD("set_body_positions", "vertices"), &FailoringSim::set_body_positions);
    ClassDB::bind_method(D_METHOD("prefetch_body_positions", "vertices"), &FailoringSim::prefetch_body_positions);
    ClassDB::bind_method(D_METHOD("set_body_motion", "displacement"), &FailoringSim::set_body_motion);
    ClassDB::bind_method(D_METHOD("set_body_enabled", "on"), &FailoringSim::set_body_enabled);
    ClassDB::bind_method(D_METHOD("clear_body"), &FailoringSim::clear_body);
    ClassDB::bind_method(D_METHOD("has_body"), &FailoringSim::has_body);
    ClassDB::bind_method(D_METHOD("get_body_stats"), &FailoringSim::get_body_stats);
    ClassDB::bind_method(D_METHOD("body_eval", "points"), &FailoringSim::body_eval);

    ClassDB::bind_method(D_METHOD("setup_display"), &FailoringSim::setup_display);
    ClassDB::bind_method(D_METHOD("get_display_indices"), &FailoringSim::get_display_indices);
    ClassDB::bind_method(D_METHOD("get_display_normals", "positions"), &FailoringSim::get_display_normals);
    ClassDB::bind_method(D_METHOD("get_display_edges"), &FailoringSim::get_display_edges);
    ClassDB::bind_method(D_METHOD("get_display_positions", "margin"), &FailoringSim::get_display_positions, DEFVAL(0.0008));

    ClassDB::bind_method(D_METHOD("set_param", "name", "value"), &FailoringSim::set_param);
    ClassDB::bind_method(D_METHOD("step", "frames"), &FailoringSim::step, DEFVAL(1));

    ClassDB::bind_method(D_METHOD("get_positions"), &FailoringSim::get_positions);
    ClassDB::bind_method(D_METHOD("get_indices"), &FailoringSim::get_indices);
    ClassDB::bind_method(D_METHOD("get_vertex_count"), &FailoringSim::get_vertex_count);
    ClassDB::bind_method(D_METHOD("get_triangle_count"), &FailoringSim::get_triangle_count);
    ClassDB::bind_method(D_METHOD("get_frames"), &FailoringSim::get_frames);
    ClassDB::bind_method(D_METHOD("get_kinetic_energy"), &FailoringSim::get_kinetic_energy);
    ClassDB::bind_method(D_METHOD("get_max_seam_gap"), &FailoringSim::get_max_seam_gap);
    ClassDB::bind_method(D_METHOD("get_min_body_clearance"), &FailoringSim::get_min_body_clearance);
    ClassDB::bind_method(D_METHOD("get_solver_stats"), &FailoringSim::get_solver_stats);
}
