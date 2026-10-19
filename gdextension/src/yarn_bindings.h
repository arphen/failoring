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
    // "chain", "sc", "turn": one hook action. `opening` is the loop to work into (-1: the next one in order). Returns
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

protected:
    static void _bind_methods();

private:
    fl::YarnWork work_;
    bool started_ = false;
};

}  // namespace godot
