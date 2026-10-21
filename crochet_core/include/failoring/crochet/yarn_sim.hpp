#pragma once
// Yarn mode: a process simulator (docs/YARN_MODE_PLAN.md section 14). One strand of yarn, from the tail to the ball, simulated as a rod with contact;
// a rigid hook with a real head (a rounded tip, a barb and a throat cut into one side) moved along keyframed poses; the hands as grips that hold
// particles of the yarn. Nothing is placed: the slip knot and every stitch are made by moving the hook, and the geometry is whatever the yarn does.
// The yarn cannot pass through itself (capsule contact at its true radius) or through the hook.
#include <functional>
#include <string>
#include <vector>

#include "failoring/crochet/rod.hpp"
#include "failoring/crochet/yarn_render.hpp"

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
    bool sdf = true;        // the hook as one smooth body (a round head with a barb, a neck, a taper and the shaft) instead of capsules
};
// The hook's signed distance (m) at a point of its own frame (z from the tip toward the handle, the throat opening toward +x), and its gradient.
Real hookSdf(const Vec3& p, const HookShape& s, Vec3* grad);
// The hook as a mesh (for drawing): the head marched from hookSdf at `cell` m, the shaft a tube; in world space.
YarnMesh hookMesh(const HookPose& p, const HookShape& s, Real cell = 0.15e-3);
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
    int substeps = 12, iterations = 4;   // fewer and strands squeezed between the hook and other yarn pass through it
};

class YarnSim {
public:
    YarnSimParams params;
    RodSystem rs;
    HookShape shape;
    bool rig = false;   // the loop was laid by the stitch rig rather than the slip knot; the two need different finish pulls
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
    // The throat's grip: wool squashes round the barb and does not drop out of the throat while the hook draws it, though it slides through
    // freely. While `catchK` > 0 the yarn within `catchReach` of the middle of the throat is pulled toward it, square to its own direction (N/m).
    Real catchK = 0.0, catchReach = 3.5e-3;
    int catchFrom = 0;   // only yarn from this particle on (the yarn just laid over the hook, not the loop it is drawn through)
    // The left index finger: when `fingerOn`, the working yarn runs from the work over a finger (a capsule across the yarn at `hand`, radius
    // `fingerR`) and on to the spool, `spoolOff` from the finger: the yarn near the work is always taut over the finger, slack stays away from the
    // hook. Moving `hand` moves the finger and the spool together.
    bool fingerOn = false;
    Vec3 hand{};
    Vec3 fingerAxis{1, 0, 0};
    Real fingerR = 6e-3, fingerLen = 0.03;
    Vec3 spoolOff{0, -0.02, -0.02};
    Real spoolRadial = 0.02;   // > 0: the spool is this far from the finger straight away from the hook's axis (spoolOff is not used)
    void putFinger(int particle, const Vec3& side);   // a finger under the yarn at `particle`, on its `side` (the yarn then runs over it)
    // The throat holds the yarn in it (a ring at the middle of the throat that the yarn runs through) while `throatRing` is set
    void setThroatRing(bool on, int fromParticle = 0);
    bool throatRingOn() const { return throatRing_ >= 0; }
    bool fingerEyelet = true;
    Real throatX = 0.30;   // where the throat holds a caught yarn, out from the axis toward the throat's mouth (shaft radii)
    Real throatStiffness = 0.5;   // the throat's hold is soft (contact can push back on it), the finger's is firm
    bool homeSet = false;   // where the hook works from, stitch after stitch (set by the first chain)
    int chains = 0;         // completed chains; the left hand goes a little farther below the growing work for the fifth
    Vec3 home{};   // the finger is an eyelet the yarn slides through (true) or a capsule it lies over (false)
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
    std::string stageLog;            // stitch procedures append "stage:loops on the hook" as they go (a diagnostic)

    // measures
    Real maxStrain() const;
    Real minGap() const;            // the smallest distance between yarn axes more than a few segments apart (non-intersecting: >= 2 radius)
    Real hookPenetration() const;   // how far the yarn is inside the hook at most (0 = clear)
    // How many times the yarn goes round the hook's shaft: the signed count of its crossings of the half plane that runs from the hook's axis out
    // along `out` (squared to the axis). 1 (or -1) with a loop on the hook, 0 without. `out` must point where the yarn's ends do not go.
    int loopsOnHook(const Vec3& out) const;
    // The linking number of the yarn with the hook's axis, both closed far away: 1 (or -1) with one loop on the hook.
    Real hookLinking() const;
    // How far the yarn from particle i0 to i1 goes round the hook's axis (turns, signed), counting only the stretches of it within `reach` of the
    // axis and level with the hook; the largest such stretch.
    Real windingNear(int i0, int i1, Real reach = 0.008, int* runLo = nullptr, int* runHi = nullptr) const;
    // The loops round the hook: every stretch of yarn near the hook (within `reach` of its axis, level with it) that winds at least `minTurns`
    // round it, as {first particle, last particle, turns}.
    struct HookRun { int lo, hi; Real turns; };
    std::vector<HookRun> hookRuns(Real reach = 0.009, Real minTurns = 0.55) const;
    std::vector<Vec3> yarn() const { return rs.x; }

private:
    void controlEnd(End& e);
public:
    Vec3 throatPoint() const;   // the middle of the throat, where a caught yarn lies
private:
    void feedHand(End& e);
    int pulled_ = -1;   // the particle the hand pulls on
    std::vector<int> caught_;   // the particles the throat's grip pulls on
    std::vector<int> reeled_;   // the particles the hand draws in
    int throatRing_ = -1, fingerRing_ = -1;   // their indices in rs.rings
public:
    Real reelSlack = 0.6, reelForce = 0.0;   // the hand takes in yarn lying within reelSlack of its length from it, drawing it with reelForce x tension (off)
private:
};

// The slip knot, made the way a crocheter makes it: the yarn laid in a ring with the working yarn behind it, the hook in through the ring, catching
// the working yarn and drawing it through, the ends pulled to close the knot on the hook.
void makeSlipKnot(YarnSim& sim);
// One chain stitch, from a loop on the hook (the hook lying tip to the left): the left hand holds the work just below the hook and carries the
// working yarn over its index finger; the hook picks the yarn up (yarn over), turns its throat down and draws it back through the loop on the hook.
void makeChain(YarnSim& sim);
// The chain in two halves (so the pull-through can be tried from the same start): up to the yarn over, then the pull through and the new loop seated.
struct ChainStart {
    bool ok = false;
    int hold = -1;
    Vec3 tip0{};
    Real along = 0;
    std::vector<int> oldLoop;   // the particles of the loop that was on the hook
    int oldLo = -1, oldHi = -1;
    int loopsAtStart = 0, loopsBefore = 0, loopsAfterYo = 0;   // loopsOnHook(front) at the start and after the yarn over
    Real length = 0;            // the yarn between the tail and the hand (m) when the stitch began
};
ChainStart prepareChain(YarnSim& sim);
void finishChain(YarnSim& sim, const ChainStart& c);
// Was a chain made: a loop is round the hook (in a copy, with the work held and the working yarn pulled firmly, the yarn closes round the shaft),
// the old loop is off it, and more yarn has been drawn from the hand into the work than before the stitch (a stitch that unravelled gives it back).
struct ChainResult {
    Real link = 0;   // degrees of the shaft the yarn closes round when the working yarn is pulled firmly (a loop on the hook: ~360)
    int loops = 0;   // the linking stretch: exactly one loop
    int frontLoops = 0;   // loopsOnHook(front), which also counts the chain's legs beside the hook
    int oldOn = 0;   // particles of the old loop still round the hook
    bool newYarn = false;   // the linking stretch reaches into the yarn drawn after the old loop (not an earlier work loop)
    Real oldWind = 0, newWind = 0;    // how far the old loop's yarn and the yarn after it wind round the hook (turns)
    Real linkMeanR = 0, linkMaxR = 0;  // distance of the final linking stretch from the hook's axis (m)
    Real length = 0;
    bool made = false;
};
ChainResult judgeChain(const YarnSim& sim, const ChainStart& c);
int carriedRound(const YarnSim& sim);   // 360 with a loop on the hook, else 0
// The loop on the hook: the shortest stretch of yarn, from particle `from` to `to` where the yarn touches itself, that the hook's axis passes
// through (linking number 1 with the axis closed far away). False when there is none. Use this for the stitch, not loopsOnHook(front), which
// also counts the chain's legs hanging beside the hook.
bool hookLoop(const YarnSim& sim, int* from, int* to);

// A test rig for stitches: the hook lying tip to the left at `tip`, a loop snug on its shaft, its front leg running down to the work (the tail end,
// held), its back leg up behind to the index finger and over it to the spool. Builds `sim` (finger on, hook posed).
void buildLoopRig(YarnSim& sim, const Vec3& tip);

// The yarn laid out for the slip knot (tail first): exposed so a caller can set the simulation up and drive its own frames.
std::vector<Vec3> slipKnotLayout(Real radius);

}  // namespace fl
