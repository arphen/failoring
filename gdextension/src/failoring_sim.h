// Godot-facing wrapper around the C++ cloth core (cloth_core).
// Exposed to GDScript as the class `FailoringSim` (RefCounted). The method set mirrors wasm/wasm_api.cpp (the browser
// build of the same core): panels and seams, a mesh body with a moving surface, display mesh, solver parameters.
#pragma once

#include <array>
#include <cstdint>
#include <future>
#include <memory>
#include <vector>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
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

// The frame of a box prop: its centre and the world directions of its axes.
struct BoxFrame {
    fl::Vec3 origin{0, 0, 0};
    fl::Vec3 axis[3] = {fl::Vec3(1, 0, 0), fl::Vec3(0, 1, 0), fl::Vec3(0, 0, 1)};
};

// An oriented box (a prop the cloth collides with). Exact signed distance outside, and the distance to the nearest face
// inside. The transform is replaced by FailoringSim::set_box_prop; the solver reads it through eval.
class OrientedBoxCollider : public fl::Collider {
public:
    BoxFrame frame;
    fl::Vec3 half{0.5, 0.5, 0.5};  // half extents along the frame's axes
    fl::Real eval(const fl::Vec3& p, fl::Vec3& n) const override;
};

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
    // The same with holes: `holes` is an Array of PackedVector2Array, each a hole inside the outline (a neck hole, a vent).
    int add_polygon_panel_with_holes(const PackedVector2Array& outline, const Array& holes, real_t edge_length, const String& material_id,
                                     Vector3 origin, Vector3 ex, Vector3 ey, real_t grain_angle, const String& name);
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

    // --- box props (oriented boxes; moving ones are the showcase's boxes and chairs) ---------------------------------
    // transform: the box's local frame to the world (basis = unit axes, origin = centre); half_extents: along those axes.
    // Props stay when colliders or the mesh body change, and clear_box_props() removes them (begin() keeps them).
    // Returns the prop index.
    int add_box_prop(Transform3D transform, Vector3 half_extents);
    // Moves a prop. The rigid motion from its previous transform (the state of the last step) is given to the cloth on
    // the next step(1) as surface motion for the cloth vertices within 2 cm of the prop, so friction sees it. Props that
    // are not moved do not change the body's motion.
    void set_box_prop(int index, Transform3D transform);
    void clear_box_props();
    int get_box_prop_count() const;

    // --- skinning (linear blend skinning of a mannequin, for the showcase's poses) ---------------------------------
    // bind: rest positions of the vertices; bone_index / weight: 4 influences per vertex (the skeleton's skin arrays).
    // Returns false (and reports) on a size mismatch.
    bool set_skin(const PackedVector3Array& bind, const PackedInt32Array& bone_index, const PackedFloat32Array& weight);
    // bone_mats: 12 floats per bone, row-major 3x3 A then translation t, world = A * rest + t. Returns the skinned
    // positions (one per bind vertex), or an empty array on error.
    PackedVector3Array skin(const PackedFloat32Array& bone_mats) const;

    // --- float16 packing (the showcase cache stores positions as float16, little endian) ---------------------------
    PackedByteArray encode_half(const PackedFloat32Array& values) const;     // round to nearest, 2 bytes per value
    PackedFloat32Array decode_half(const PackedByteArray& bytes, int64_t offset, int64_t count) const;

    // --- mesh body (closed triangle mesh, counter-clockwise seen from outside) ---
    // Bakes a signed-distance grid of the mesh (about 0.2-0.4 s for a mannequin in the browser build; the native build is
    // faster) and collides the cloth with it. edge_length <= 0 selects 7 mm. Returns the bake time in ms, or -1 on error.
    real_t set_body_mesh(const PackedVector3Array& vertices, const PackedInt32Array& triangles, real_t edge_length);
    // Moves a mesh body set by set_body_mesh: `vertices` are the new positions of the same vertices (same count and order).
    // Re-bakes the grid and sets the surface motion used by friction (wasm fl_set_body_mesh + fl_set_body_motion, as
    // in the walking mannequin). Returns the bake time in ms, or -1 on error. A full bake per call, unless
    // prefetch_body_positions already baked these vertices in the background. The motion is for one
    // frame, so call this before every step(1) of a moving body; moving it less often makes the body jump and the cloth
    // lag, and the friction sees no surface motion on the frames in between.
    real_t set_body_positions(const PackedVector3Array& vertices);
    // Starts the SDF bake of the body at `vertices` on a background thread, so that it runs while the solver steps the
    // frame before (the editor prefetches the next frame's body during each step). The next set_body_positions with
    // exactly these vertices takes the baked grid; any other vertices are baked there as usual. Ignored without a body.
    void prefetch_body_positions(const PackedVector3Array& vertices);
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
    PackedInt32Array get_display_edges() const;    // the midpoint pairs: display vertex n + e is the middle of pair e
    // Display vertex positions: cloth vertices, then edge midpoints pushed out of a body (wasm fl_display_positions).
    // `margin` (m): display triangles are kept at least this far outside the body. Pass 0.0008 for the browser default.
    PackedVector3Array get_display_positions(real_t margin) const;
    // Unit vertex normals of the display mesh for the given display positions (as from get_display_positions): the sum of
    // the (unnormalised) face normals of the display triangles at each vertex, normalised. The same rule as
    // godot/look.gd cloth_normals, in C++ so that the editor does not loop over every vertex in GDScript each frame.
    // Returns an empty array when the display topology is not set up or the count does not match (the caller then uses
    // look.gd).
    PackedVector3Array get_display_normals(const PackedVector3Array& positions) const;

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
                        Vector3 ey, real_t grain_angle, const String& name,
                        const std::vector<std::vector<fl::Vec2>>& holes = {});
    bool displayCurrent(const char* who) const;
    const fl::FabricMaterial* material(const String& id) const;

    fl::SolverParams params_;
    std::unique_ptr<fl::ClothSolver> solver_;
    std::vector<fl::FabricMaterial> materials_;
    std::vector<std::shared_ptr<fl::Collider>> colliders_;     // analytic primitives
    std::shared_ptr<fl::Collider> active_;                     // what the solver collides with (primitives + body)

    // box props: the collider, and the transform at the last step (the motion of the next step is from it)
    struct BoxProp {
        std::shared_ptr<OrientedBoxCollider> box;
        BoxFrame prev;
        bool moved = false;
        fl::Vec3 displacement(const fl::Vec3& x) const;  // rigid motion prev -> current, applied to x
    };
    std::vector<BoxProp> props_;
    bool propsMoved_ = false;
    std::vector<fl::Vec3> bodyMotion_;                         // the body's surface motion for the next step (when props exist)
    std::vector<fl::Vec3> propMotion() const;                  // cloth-vertex motion of the moved props (+ the body's elsewhere)
    void endPropFrame();

    // skinning
    std::vector<Vector3> skinBind_;
    std::vector<int> skinBone_;
    std::vector<float> skinWeight_;
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
    // A body bake started by prefetch_body_positions: the vertices it was started with (to match the next call) and
    // the grid, which is produced on another thread.
    struct PendingBake {
        std::vector<fl::Vec3> verts;
        std::future<std::shared_ptr<fl::SdfGrid>> grid;
    };
    std::unique_ptr<PendingBake> pending_;

    // display mesh
    std::vector<int> dispEdges_;  // pairs of display vertices, one per midpoint (vertex n + e), each pair earlier than it
    int dispLevels_ = 2;          // midpoint subdivisions of the display mesh (set_param "display_levels", 1 or 2)
    std::vector<int> dispTris_;   // display triangles over the cloth vertices and midpoints
    int dispVertexCount_ = -1;    // cloth vertex count when setup_display() ran (-1 = not set up)
};

}  // namespace godot
