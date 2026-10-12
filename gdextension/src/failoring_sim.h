// Godot-facing wrapper around the C++ cloth core (cloth_core).
// Exposed to GDScript as the class `FailoringSim` (RefCounted).
#pragma once

#include <memory>
#include <vector>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "failoring/cloth.hpp"
#include "failoring/material.hpp"

namespace godot {

class FailoringSim : public RefCounted {
    GDCLASS(FailoringSim, RefCounted)

public:
    FailoringSim();

    // --- materials -------------------------------------------------------------
    bool load_materials(const String& path);              // fabrics.json (res:// or absolute path)
    PackedStringArray get_material_ids() const;
    Dictionary get_material(const String& id) const;      // properties for UI display
    real_t get_recommended_edge_length(const String& id) const;  // Zhang et al. 2025 estimator, metres

    // --- pattern pieces --------------------------------------------------------
    // edge_length <= 0 selects the estimator's recommendation (floored at 6 mm).
    // Returns the panel index, or -1 on error. `ex`/`ey` map panel (u,v) into world space.
    int add_rect_panel(real_t width, real_t height, real_t edge_length, const String& material_id, Vector3 origin, Vector3 ex, Vector3 ey,
                       real_t grain_angle, const String& name);
    int add_polygon_panel(const PackedVector2Array& outline, real_t edge_length, const String& material_id, Vector3 origin, Vector3 ex,
                          Vector3 ey, real_t grain_angle, const String& name);
    void add_seam(int panel_a, int edge_a, int panel_b, int edge_b, bool reverse);
    void pin_vertex(int vertex, bool pinned);
    void set_vertex_position(int vertex, Vector3 p);
    int get_first_vertex(int panel) const;
    int get_panel_vertex_count(int panel) const;
    PackedInt32Array get_edge_chain(int panel, int edge) const;
    PackedVector2Array get_panel_vertices_2d(int panel) const;  // rest (u,v) coordinates, for the cutting-table view

    // --- bodies ----------------------------------------------------------------
    void clear_colliders();
    void add_ellipsoid(Vector3 center, Vector3 radii);
    void add_sphere(Vector3 center, real_t radius);
    void add_capsule(Vector3 a, Vector3 b, real_t radius);
    void add_cylinder(Vector3 center, real_t radius, real_t half_height);  // Y axis
    void add_plane(Vector3 point, Vector3 normal);

    // --- simulation ------------------------------------------------------------
    void set_param(const String& name, real_t value);  // substeps, newton_iterations, seam_stiffness, seam_ramp_frames, self_collision, ...
    void step(int frames);
    void reset_velocities();

    // --- results (read after step) ---------------------------------------------
    PackedVector3Array get_positions() const;
    PackedInt32Array get_indices() const;    // triangle list, 3 per triangle, global vertex indices
    int get_vertex_count() const;
    int get_triangle_count() const;
    real_t get_kinetic_energy() const;
    real_t get_max_seam_gap() const;
    real_t get_min_body_clearance() const;

protected:
    static void _bind_methods();

private:
    void rebuildColliders();
    const fl::FabricMaterial* material(const String& id) const;

    fl::SolverParams params_;
    std::unique_ptr<fl::ClothSolver> solver_;
    std::vector<fl::FabricMaterial> materials_;
    std::vector<std::shared_ptr<fl::Collider>> colliders_;
};

}  // namespace godot
