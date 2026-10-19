#pragma once
// Yarn mode: a process simulator (docs/YARN_MODE_PLAN.md section 14). One strand of yarn, from the tail to the ball, simulated as a rod with contact;
// a rigid hook with a real head (a rounded tip, a barb and a throat cut into one side) moved along keyframed poses; the hands as grips that hold
// particles of the yarn. Nothing is placed: the slip knot and every stitch are made by moving the hook, and the geometry is whatever the yarn does.
// The yarn cannot pass through itself (capsule contact at its true radius) or through the hook.
#include <functional>
#include <string>
#include <vector>

#include "failoring/crochet/rod.hpp"

namespace fl {

// The hook's pose: where its tip is and how it is turned. In the hook's own frame z runs from the tip toward the handle and x is the side the throat
// opens to.
struct HookPose {
    Vec3 tip{0, 0, 0};
    Quat q;   // local to world
    Vec3 axis() const { return rotate(q, Vec3{0, 0, 1}); }    // tip -> handle
    Vec3 throat() const { return rotate(q, Vec3{1, 0, 0}); }  // the way the throat opens
};
// A pose from its tip, the direction the tip points to (handle -> tip) and the direction the throat opens to (squared to the axis).
HookPose hookPose(const Vec3& tip, const Vec3& pointing, const Vec3& throat);
HookPose lerpPose(const HookPose& a, const HookPose& b, Real t);

struct HookShape {
    Real R = 2.5e-3;        // shaft radius (a 5 mm hook)
    Real length = 0.075;    // tip to the end of what is simulated of the shaft
    Real squash = 0.6;      // the yarn's radius against the hook, as a fraction of its own: wool squashes into the throat
};
// The hook as capsules (head, barb, neck, taper, shaft) in world space.
std::vector<StaticCapsule> hookCapsules(const HookPose& p, const HookShape& s);

struct YarnSimParams {
    Real radius = 1.1e-3;      // yarn radius (worsted for a 5 mm hook)
    Real segment = 1.0e-3;     // rest length of one rod segment
    Real dt = 1.0 / 60.0;      // one frame
    Real tension = 0.15;       // N, the left hand's pull on the working yarn
    Real massScale = 100.0;    // pseudo-mass (quasi-static); higher is steadier but drags: moving yarn costs mass * damping * speed
    Real damping = 20.0;       // 1/s
    Real bend = 1.0e-8;        // N m^2, bending (and twisting) stiffness: wool is limp (measured yarns 1e-9 to 1e-7). A stiffer yarn cannot bend
                               // round the hook fast enough to run through its throat: the hook cuts it instead
    Real friction = 0.35;      // yarn on yarn
    Real hookFriction = 0.25;  // wool on an aluminium hook
};

class YarnSim {
public:
    YarnSimParams params;
    RodSystem rs;
    HookShape shape;
    HookPose hook;
    int frame = 0;

    // An end of the yarn. A pinned end stays where its target is (the tail held, or let go). A hand end is the working yarn running through the left
    // hand at `target`: the yarn beyond it is not simulated, the last segment runs freely through the hand, and the yarn next to it is pulled
    // toward the hand with `tension` N; the hand pays yarn out as the work takes it and draws slack back in (particles are added and removed there).
    struct End {
        int particle = -1;
        Vec3 target{};
        bool hand = false;
        Real tension = 0.15;
    };
    End tail, ball;

    // A hand holding the yarn: the particles within `radius` of `at` are pinned and move with `at`.
    struct Grip {
        std::vector<int> particles;
        std::vector<Vec3> offset;
        Vec3 at{};
    };
    std::vector<Grip> grips;
    // Fingers and other fixed things the yarn runs past (it slides along them): capsules added to the hook's.
    std::vector<StaticCapsule> fixtures;
    int grab(const Vec3& at, Real radius);   // returns the grip's index
    void moveGrip(int g, const Vec3& to) { grips[g].at = to; }
    void release(int g);

    // The yarn along `path` (the tail first; the last point is the left hand, where the working yarn runs to the ball), resampled to the rod's
    // segment length.
    void build(const std::vector<Vec3>& path);

    // One frame: the hook at `pose`, the ends controlled, the rod stepped.
    void step(const HookPose& pose);
    // The hook from where it is to `to` over `seconds` (minimum jerk), calling `onFrame` after every frame.
    void moveHook(const HookPose& to, Real seconds);
    void settle(int frames);
    void relaxStraight(Real f, int fromJoint = 0);   // the curl the yarn was laid with, times f (it lets go of it), from a joint on
    std::function<void()> onFrame;   // called after every frame (a recorder, a renderer)
    std::string phase;               // what the hands are doing now, in words (a script sets it)

    // measures
    Real maxStrain() const;
    Real minGap() const;            // the smallest distance between yarn axes more than a few segments apart (non-intersecting: >= 2 radius)
    Real hookPenetration() const;   // how far the yarn is inside the hook at most (0 = clear)
    // How many times the yarn goes round the hook's shaft: the signed count of its crossings of the half plane that runs from the hook's axis out
    // along `out` (squared to the axis). 1 (or -1) with a loop on the hook, 0 without. `out` must point where the yarn's ends do not go.
    int loopsOnHook(const Vec3& out) const;
    std::vector<Vec3> yarn() const { return rs.x; }

private:
    void controlEnd(End& e);
    void feedHand(End& e);
    int pulled_ = -1;   // the particle the hand pulls on
};

// The slip knot, made the way a crocheter makes it: the yarn laid in a ring with the working yarn behind it, the hook in through the ring, catching
// the working yarn and drawing it through, the ends pulled to close the knot on the hook.
void makeSlipKnot(YarnSim& sim);

// The yarn laid out for the slip knot (tail first): exposed so a caller can set the simulation up and drive its own frames.
std::vector<Vec3> slipKnotLayout(Real radius);

}  // namespace fl
