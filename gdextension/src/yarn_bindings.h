// Godot-facing wrapper for yarn mode (docs/YARN_MODE_PLAN.md): YarnSession, the work made one hook action at a time, the yarn as a mesh,
// the loops as openings, the pattern so far as text. All the logic is crochet_core's YarnWork.
#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "failoring/crochet/yarn_render.hpp"
#include "failoring/crochet/yarn_work.hpp"

namespace godot {

class YarnSession : public RefCounted {
    GDCLASS(YarnSession, RefCounted)

public:
    YarnSession();
    // A new piece: the slip knot at the origin, one loop on the hook.
    void reset();
    // "chain", "sc" / "hdc" / "dc" / "slst" (under both loops), the same with "_front" / "_back" (one loop only), "skip" (the next stitch), "finish" (fasten off: cut the yarn and draw the end through the loop on the hook), "turn" (`opening` is
    // then the number of turning chains): one hook action. `opening` is the loop to work into (-1: the next one in order). Returns
    // {ok, loop, loops (the loops it made, in order), msg, particles (the yarn now has), row, cursor}.
    Dictionary action(const String& kind, int opening);
    // The yarn while the hook draws `loop` (one of the loops `action` reports in "loops") through its eye, `s` 0..1: {positions, normals, colors, indices (as get_mesh),
    // tip, axis (from the hook's throat toward the handle, in the piece's coordinates), eye (the middle of the eye it goes through), through}. `wrap` 0..1 lays the working yarn over the shaft (the yarn over). Only the yarn up to that loop is in the mesh.
    Dictionary anim_frame(int loop, double s, double wrap, int sides, int subdiv);
    // `n` soft-physics steps (the yarn settling)
    void tick(int n);
    // The yarn as a mesh: {positions, normals, colors, indices}; `up_to` draws only the first that many particles (-1: all).
    Dictionary get_mesh(int up_to, int sides, int subdiv);
    int get_particle_count() const;
    // [{id, row, live, centre, normal, radius}] for every loop
    TypedArray<Dictionary> get_loops() const;
    // {row, cursor, next (the default opening, -1 none), targets (the row being worked into), live (loops on the hook), hook_tip, hook_axis, text}
    Dictionary get_state() const;
    String get_pattern_text() const;
    void set_param(const String& name, double value);
    // The next stitch changes to this colour: `when` is "pull" (at its last pull-through, the usual way) or "stitch" (from its start). A colour used before
    // is picked up where it was dropped, a new one is started.
    void set_colour(const Color& colour, const String& when);
    TypedArray<Color> get_colours() const;   // the yarns so far
    // Loops the next stitch's last pull-through picks up as well (the way a seam is joined); toggles one on or off. get_state()["extra"] lists them.
    void toggle_extra(int loop);
    // The pattern so far as rows of runs: [[{kind, count, colour}]]
    TypedArray<Array> get_pattern_rows() const;
    // How the piece is looked at: `expand` scales the piece about its middle (the loops open up, the yarn keeps its thickness, so you see how they
    // hook into each other); `thickness` scales the yarn's drawn radius (a thin-yarn view). Neither changes the work.
    void set_view(double expand, double thickness);
    // Pulling on the yarn, in the coordinates the piece is shown in: grab the strand nearest to a ray ({ok, point, particle}), drag it to a point, let go.
    // Only the yarn near the grip is simulated while it is held; letting go, the shape memory brings the work back.
    Dictionary grab(const Vector3& origin, const Vector3& dir);
    void drag_to(const Vector3& point);
    void release();
    bool is_grabbed() const { return grip_ >= 0; }
    // What is under a ray (in the coordinates the piece is shown in): the loop whose yarn is nearest to it, or failing that the loop whose ring the ray goes through
    // ({ok, loop, top (the stitch it belongs to), strand, point}). `top` is what a click means: the stitch to work into.
    Dictionary pick(const Vector3& origin, const Vector3& dir);
    // The yarn of one loop as a mesh, a little thicker than the yarn (for highlighting it); same arrays as get_mesh.
    Dictionary get_loop_mesh(int loop);
    // A yarn along a curve (in the coordinates given), rounded at both ends: the run from the work to its ball.
    Dictionary tube(const PackedVector3Array& points, const Color& colour, double radius, int sides, int subdiv) const;

protected:
    static void _bind_methods();

private:
    fl::YarnWork work_;
    bool started_ = false;
    double expand_ = 1.0, thickness_ = 1.0;
    int grip_ = -1;
    fl::Vec3 unview(const fl::Vec3& p) const;
    static Dictionary mesh_to_dict(const fl::YarnMesh& m);
    fl::Vec3 pivot() const;
    fl::Vec3 view(const fl::Vec3& p) const;   // a point of the work as it is shown
};

}  // namespace godot
