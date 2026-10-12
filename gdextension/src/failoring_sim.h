// Godot-facing wrapper around the C++ cloth core (cloth_core).
// Exposed to GDScript as the class `FailoringSim` (RefCounted). The method set mirrors wasm/wasm_api.cpp (the browser
// build of the same core): panels and seams, a mesh body with a moving surface, display mesh, solver parameters.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "failoring/cloth.hpp"
#include "failoring/collider.hpp"
#include "failoring/material.hpp"
#include "failoring/sdf_grid.hpp"

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

    // --- simulation lifecycle --------------------------------------------------
    // Starts a new simulation: clears panels, seams and the display mesh. Keeps parameters, materials, colliders and
    // the body (wasm: fl_begin). Call before building a garment again.
    void begin();

    // --- pattern pieces --------------------------------------------------------
    // edge_length <= 0 selects the estimator's recommendation (floored at 6 mm).
    // Returns the panel index, or -1 on error. `ex`/`ey` map panel (u,v) into world space.
    // `outline` is a closed polygon in metres (either winding). Edge i runs from outline[i] to outline[i+1].
    int add_rect_panel(real_t width, real_t height, real_t edge_length, const String& material_id, Vector3 origin, Vector3 ex, Vector3 ey,
                       real_t grain_angle, const String& name);
    int add_polygon_panel(const PackedVector2Array& outline, real_t edge_length, const String& material_id, Vector3 origin, Vector3 ex,
                          Vector3 ey, real_t grain_angle, const String& name);
    // Sews outline edge edge_a of panel_a to edge_b of panel_b. reverse = false: the start of A's chain meets the start of
    // B's chain. Returns false (and reports an error) for an invalid panel or edge index.
    bool add_seam(int panel_a, int edge_a, int panel_b, int edge_b, bool reverse);
    void pin_vertex(int vertex, bool pinned);
    void set_vertex_position(int vertex, Vector3 p);                  // teleport one vertex (zeroes its velocity)
    void set_positions(const PackedVector3Array& positions);          // teleport all cloth vertices (wasm fl_set_positions)
    int get_first_vertex(int panel) const;
    int get_panel_vertex_count(int panel) const;
    int get_first_triangle(int panel) const;
    int get_panel_triangle_count(int panel) const;
    int get_panel_count() const;
    PackedInt32Array get_edge_chain(int panel, int edge) const;
    PackedVector2Array get_panel_vertices_2d(int panel) const;  // rest (u,v) coordinates, for the cutting-table view
    int get_warnings() const;  // bit 0: a polygon outline could not be meshed fully (features thinner than edge_length)

    // --- primitive bodies ------------------------------------------------------
    // Analytic colliders; they stay when a mesh body is set or cleared, and clear_colliders() removes them.
    void clear_colliders();
    void add_ellipsoid(Vector3 center, Vector3 radii);
    void add_sphere(Vector3 center, real_t radius);
    void add_capsule(Vector3 a, Vector3 b, real_t radius);
    void add_cylinder(Vector3 center, real_t radius, real_t half_height);  // Y axis
    void add_plane(Vector3 point, Vector3 normal);

    // --- mesh body (closed triangle mesh, counter-clockwise seen from outside) ---
    // Bakes a signed-distance grid of the mesh (about 0.2-0.4 s for a mannequin in the browser build; the native build is
    // faster) and collides the cloth with it. edge_length <= 0 selects 7 mm. Returns the bake time in ms, or -1 on error.
    real_t set_body_mesh(const PackedVector3Array& vertices, const PackedInt32Array& triangles, real_t edge_length);
    // Moves a mesh body set by set_body_mesh: `vertices` are the new positions of the same vertices (same count and order).
    // Re-bakes the grid and sets the surface motion used by friction (wasm fl_set_body_mesh + fl_set_body_motion, as
    // in the walking mannequin). Returns the bake time in ms, or -1 on error. A full bake per call: keep the call rate low.
    real_t set_body_positions(const PackedVector3Array& vertices);
    // Per-cloth-vertex displacement of the body surface over the next step (friction). Empty array = static body.
    void set_body_motion(const PackedVector3Array& displacement);
    void set_body_enabled(bool on);
    void clear_body();
    bool has_body() const;
    // Last bake: {ms, bricks, fine_samples, coarse_samples, bytes, distance_evals, edge_length, triangles}.
    Dictionary get_body_stats() const;
    // Signed distance and outward normal at each point: 4 floats per point (d, nx, ny, nz). Distance 1e30 without a body.
    PackedFloat32Array body_eval(const PackedVector3Array& points) const;

    // --- display mesh ----------------------------------------------------------
    // One midpoint per cloth edge, four triangles per cloth triangle (wasm fl_display_setup). Call after the panels are
    // added and before reading display positions. Display vertices are the cloth vertices followed by the midpoints.
    void setup_display();
    PackedInt32Array get_display_indices() const;  // display triangles, 3 per triangle
    // Display vertex positions: cloth vertices, then edge midpoints pushed out of a body (wasm fl_display_positions).
    // `margin` (m): display triangles are kept at least this far outside the body. Pass 0.0008 for the browser default.
    PackedVector3Array get_display_positions(real_t margin) const;

    // --- simulation ------------------------------------------------------------
    // Parameter names: substeps, newton_iterations, dt, seam_stiffness, seam_ramp_frames, self_collision,
    // contact_offset, contact_band, tension_stiffness (0/1), gravity_y.
    void set_param(const String& name, real_t value);
    void step(int frames);
    void reset_velocities();  // no-op (kept for compatibility); use set_positions to re-place the cloth

    // --- results (read after step) ---------------------------------------------
    PackedVector3Array get_positions() const;
    PackedInt32Array get_indices() const;    // triangle list, 3 per triangle, global vertex indices
    int get_vertex_count() const;
    int get_triangle_count() const;
    int get_frames() const;
    real_t get_kinetic_energy() const;
    real_t get_max_seam_gap() const;
    real_t get_min_body_clearance() const;

protected:
    static void _bind_methods();

private:
    void rebuildColliders();
    void resetSolver();
    bool validPanel(int panel, const char* who) const;
    const fl::FabricMaterial* material(const String& id) const;

    fl::SolverParams params_;
    std::unique_ptr<fl::ClothSolver> solver_;
    std::vector<fl::FabricMaterial> materials_;
    std::vector<std::shared_ptr<fl::Collider>> colliders_;     // analytic primitives
    std::shared_ptr<fl::Collider> active_;                     // what the solver collides with (primitives + body)
    std::vector<int> panelEdges_;                              // outline edge count per panel (for seam checks)
    int warnings_ = 0;

    // mesh body
    struct Body {
        std::shared_ptr<fl::SdfGrid> grid;
        bool enabled = false;
        std::vector<fl::Vec3> verts;                           // last positions (surface motion reference)
        std::vector<std::array<int, 3>> tris;
        fl::Real h = 0.007;
    } body_;

    // display mesh
    std::vector<int> dispEdges_;  // pairs of cloth vertices, one per midpoint
    std::vector<int> dispTris_;   // display triangles over the cloth vertices and midpoints
};

}  // namespace godot
