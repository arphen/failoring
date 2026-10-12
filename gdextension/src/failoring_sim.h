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
    Dictionary get_material(const String& id) const;      // properties for UI display (keys as add_material takes them)
    // Adds a fabric (or replaces the one with the same id), the counterpart of the browser's fl_add_material. Keys of
    // `properties` are those get_material returns: name, category, density_g_m2, thickness_mm, tensile_warp_N_m,
    // tensile_weft_N_m, shear_N_m_rad, bending_warp_Nm, bending_weft_Nm, compression_ratio, friction_static,
    // friction_kinetic, damping. A missing key keeps the default (or the replaced fabric's value). Returns false and
    // reports an error for an empty id or a non-numeric or non-positive value.
    bool add_material(const String& id, const Dictionary& properties);
    real_t get_recommended_edge_length(const String& id) const;  // Zhang et al. 2025 estimator, metres

    // --- simulation lifecycle --------------------------------------------------
    // Starts a new simulation: clears panels, seams and the display mesh. Keeps parameters, materials, colliders and
    // the body (wasm: fl_begin). Call before building a garment again.
    void begin();

    // --- pattern pieces --------------------------------------------------------
    // edge_length <= 0 selects the estimator's recommendation (floored at 6 mm); a positive value is used as given.
    // Returns the panel index, or -1 on error (unknown material, bad size, or the simulation already started: call
    // begin() first). `ex`/`ey` map panel (u,v) into world space. Panels are meshed with meshPolygon, as in the browser.
    // add_rect_panel: outline edges are 0 bottom, 1 right, 2 top, 3 left.
    // `outline` is a closed polygon in metres. A clockwise outline is reversed to counter-clockwise first (the browser's
    // FL.normalizePiece does the same), so edge i runs from outline[i] to outline[i+1] of the COUNTER-CLOCKWISE outline;
    // for a clockwise input that is the reversed list.
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
    // Pattern (u,v) coordinates of the panel's vertices as meshed, before the grain rotation (the browser's fl_rest_uv);
    // the cutting-table view and any pre-wrapping of the pieces use these.
    PackedVector2Array get_panel_vertices_2d(int panel) const;
    int get_warnings() const;  // bit 0: a polygon outline could not be meshed fully (features thinner than edge_length)

    // --- primitive bodies ------------------------------------------------------
    // Analytic colliders; they stay when a mesh body is set or cleared, and clear_colliders() removes them. The add_*
    // calls append (the browser's fl_set_colliders replaces all): call clear_colliders() first to replace.
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
    // in the walking mannequin). Returns the bake time in ms, or -1 on error. A full bake per call. The motion is for one
    // frame, so call this before every step(1) of a moving body; moving it less often makes the body jump and the cloth
    // lag, and the friction sees no surface motion on the frames in between.
    real_t set_body_positions(const PackedVector3Array& vertices);
    // Per-cloth-vertex displacement of the body surface over the NEXT solver frame (friction). Empty array = static body.
    // The size must be the cloth vertex count (anything else is reported and ignored). step() clears the motion after
    // each frame it runs, so it applies to one frame only, as in the browser (which sets it before every fl_step(1)).
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
    // The display topology depends on the cloth vertex count at this call: adding a panel afterwards makes
    // get_display_indices / get_display_positions report an error (and return empty arrays) until it is called again.
    void setup_display();
    PackedInt32Array get_display_indices() const;  // display triangles, 3 per triangle
    // Display vertex positions: cloth vertices, then edge midpoints pushed out of a body (wasm fl_display_positions).
    // `margin` (m): display triangles are kept at least this far outside the body. Pass 0.0008 for the browser default.
    PackedVector3Array get_display_positions(real_t margin) const;

    // --- simulation ------------------------------------------------------------
    // Parameter names: substeps, newton_iterations, dt, seam_stiffness, seam_ramp_frames, self_collision,
    // contact_offset, contact_band, tension_stiffness (0/1), gravity_y.
    void set_param(const String& name, real_t value);
    // Runs `frames` solver frames. Errors from the solver are reported with push_error and stop the call.
    void step(int frames);

    // --- results (read after step) ---------------------------------------------
    PackedVector3Array get_positions() const;
    PackedInt32Array get_indices() const;    // triangle list, 3 per triangle, global vertex indices
    int get_vertex_count() const;
    int get_triangle_count() const;
    int get_frames() const;
    real_t get_kinetic_energy() const;
    real_t get_max_seam_gap() const;
    real_t get_min_body_clearance() const;
    // {line_search_failures, newton_iterations, substeps, cg_iterations}: the browser's fl_solver_stats. Newton
    // iterations and substeps are totals; cg_iterations is for the last substep.
    Dictionary get_solver_stats() const;

protected:
    static void _bind_methods();

private:
    void rebuildColliders();
    void resetSolver();
    bool validPanel(int panel, const char* who) const;
    int addOutlinePanel(std::vector<fl::Vec2> outline, real_t edge_length, const String& material_id, Vector3 origin, Vector3 ex,
                        Vector3 ey, real_t grain_angle, const String& name);
    bool displayCurrent(const char* who) const;
    const fl::FabricMaterial* material(const String& id) const;

    fl::SolverParams params_;
    std::unique_ptr<fl::ClothSolver> solver_;
    std::vector<fl::FabricMaterial> materials_;
    std::vector<std::shared_ptr<fl::Collider>> colliders_;     // analytic primitives
    std::shared_ptr<fl::Collider> active_;                     // what the solver collides with (primitives + body)
    std::vector<int> panelEdges_;                              // outline edge count per panel (for seam checks)
    int warnings_ = 0;
    std::vector<std::vector<fl::Vec2>> panelUV_;              // pattern (u,v) per panel vertex before grain rotation (wasm restUV)

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
    int dispVertexCount_ = -1;    // cloth vertex count when setup_display() ran (-1 = not set up)
};

}  // namespace godot
