// Godot-facing wrapper for yarn mode (docs/YARN_MODE_PLAN.md): YarnSession, the work made one hook action at a time, the yarn as a mesh,
// the loops as openings, the pattern so far as text. All the logic is crochet_core's YarnWork.
#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "failoring/crochet/yarn_work.hpp"

namespace godot {

class YarnSession : public RefCounted {
    GDCLASS(YarnSession, RefCounted)

public:
    YarnSession();
    // A new piece: the slip knot at the origin, one loop on the hook.
    void reset();
    // "chain", "sc" (under both loops), "sc_front" / "sc_back" (one loop only), "skip" (the next stitch), "turn": one hook action. `opening` is the loop to work into (-1: the next one in order). Returns
    // {ok, loop, msg, particles (the yarn now has), row, cursor}.
    Dictionary action(const String& kind, int opening);
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
    // How the piece is looked at: `expand` scales the piece about its middle (the loops open up, the yarn keeps its thickness, so you see how they
    // hook into each other); `thickness` scales the yarn's drawn radius (a thin-yarn view). Neither changes the work.
    void set_view(double expand, double thickness);
    // Pulling on the yarn, in the coordinates the piece is shown in: grab the strand nearest to a ray ({ok, point, particle}), drag it to a point, let go.
    // Only the yarn near the grip is simulated while it is held; letting go, the shape memory brings the work back.
    Dictionary grab(const Vector3& origin, const Vector3& dir);
    void drag_to(const Vector3& point);
    void release();
    bool is_grabbed() const { return grip_ >= 0; }

protected:
    static void _bind_methods();

private:
    fl::YarnWork work_;
    bool started_ = false;
    double expand_ = 1.0, thickness_ = 1.0;
    int grip_ = -1;
    fl::Vec3 unview(const fl::Vec3& p) const;
    fl::Vec3 pivot() const;
    fl::Vec3 view(const fl::Vec3& p) const;   // a point of the work as it is shown
};

}  // namespace godot
