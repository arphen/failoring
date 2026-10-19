// Godot-facing wrapper for the yarn process simulator (crochet_core YarnSim, docs/YARN_MODE_PLAN.md section 14): a procedure (the slip knot) is
// simulated once and recorded frame by frame; the view plays the frames back. Nothing in the frames is placed: it is what the simulated yarn did.
#pragma once

#include <string>
#include <vector>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "failoring/crochet/yarn_render.hpp"
#include "failoring/crochet/yarn_sim.hpp"

namespace godot {

class YarnSimSession : public RefCounted {
    GDCLASS(YarnSimSession, RefCounted)

public:
    // Simulates `procedure` ("slip_knot") and records it; returns the number of frames (60 a second).
    int record(const String& procedure);
    int get_frame_count() const { return (int)frames_.size(); }
    // The yarn in frame `i` as a mesh {positions, normals, colors, indices} (metres; the slip knot is made in the x-y plane, z toward the crocheter).
    Dictionary get_yarn_mesh(int i, int sides, int subdiv) const;
    // The hook in frame `i`, drawn from the very capsules the yarn collides with: the same dictionary.
    Dictionary get_hook_mesh(int i) const;
    // {phase (words), tip, axis (tip -> handle), throat, loops_on_hook, min_gap (m), radius (m)}
    Dictionary get_frame_info(int i) const;

protected:
    static void _bind_methods();

private:
    struct Frame {
        std::vector<fl::Vec3> yarn;
        std::vector<fl::StaticCapsule> hook;
        fl::HookPose pose;
        std::string phase;
        int loops = 0;
        double gap = 0;
    };
    std::vector<Frame> frames_;
    double radius_ = 0.0011;
};

}  // namespace godot
