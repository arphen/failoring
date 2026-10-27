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
    // Which hook is drawn. Legacy is the hook committed at f19157a (a neck and a taper, the barb at 0.85R); inline is the groove hook (a shaft
    // with a throat cut into it, the head's back wall at 2.05R). The default is legacy; the CLI picks with `--hook legacy|inline`.
    enum class Form { Legacy, Inline };
    Form form = Form::Legacy;
    static HookShape legacy() { return HookShape{}; }
    static HookShape inlineHook();
    Real R = 2.5e-3;        // shaft radius (a 5 mm hook)
    Real length = 0.075;    // tip to the end of what is simulated of the shaft
    Real squash = 0.6;      // the yarn's radius against the hook, as a fraction of its own: wool squashes into the throat
    bool sdf = true;        // the hook as one smooth body (a round head with a barb, a neck, a taper and the shaft) instead of capsules
    // Where the barb sits: x out from the axis and half its length across the throat, in shaft radii. A barb outside the head's 1R
    // envelope blocks old loops; one set inside the mouth still blocks yarn in the throat (x ~0.3-0.6R) without standing in their way.
    Real barbX = 0.85;
    Real barbHalfWidth = 1.0;
    Real barbR = 0.20;
    // The head's dome, the back wall of the throat, and the spine behind the mouth, in shaft radii. A real hook's head is barely wider than
    // its shaft, so a loop opened on the shaft rides over it; the throat is a groove in the shaft with a thick spine behind it, not a thin neck.
    Real headR = 1.0;
    Real headBack = 2.05;
    Real shaftZ = 3.5;
    Real neckX = -0.50, neckR = 0.50;
    // The flat wall across the back of the head stops a yarn drawn toward the tip (the first yarn over leans on it), and with a head as
    // wide as the shaft it also stops an old loop from riding off the tip. The groove and the grip can hold the new yarn without it.
    int flatBack = 1;   // 0: round back, 1: full wall, 2: wall only on the mouth side (the barb's back face)
    // The open throat groove cut across the mouth side of the shaft, one diameter long and just behind the head's back wall. Where its
    // centre sits and its radius, in shaft radii; it runs across the hook in y.
    bool groove = false;
    Real grooveX = 0.70, grooveZ = 1.55, grooveR = 0.55;
    // A low rim round the back and sides of the head's rear edge, open at the mouth: the old loop cannot slip off sideways, so it has
    // to ride over the tip (where the new yarn, caught in the throat, goes through it).
    bool rim = false;
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
    // SIM_ASSIST scales every guide (section 4.6 of the plan): the throat's spring grip, the throat ring, catchK, the loop-holding springs
    // and reelForce. At 0 no guide is created and the hook's body and the hand are the only things acting on the yarn.
    Real assist = 1.0;
    // One scale per guide (section 4.6): each defaults to SIM_ASSIST, and SIM_ASSIST_<NAME> overrides it. At 0 the guide is not created.
    Real assistGrip = 1.0, assistRing = 1.0, assistCatch = 1.0, assistPins = 1.0, assistReel = 1.0;
    void readAssist();   // SIM_ASSIST and SIM_ASSIST_<NAME>, called at the start of each stitch
    // The stitch's running contact: reset by prepareChain, updated every second frame in step(). A stitch that passes through itself or the
    // hook mid-pull and recovers still fails. (Every second frame keeps the O(S^2) gap check off half the frames; a crossing lasts longer than
    // one frame at the stitch's speeds.)
    Real worstGap = 1e9, worstPen = 0;
    // The impulse each guide delivered during the stitch (N s), for the verdict line; the judge ignores them. `impulseHook` is the hook's own
    // contact impulse, on the same line so the two can be compared.
    Real impulseGrip = 0, impulseRing = 0, impulseCatch = 0, impulsePins = 0, impulseReel = 0, impulseHook = 0;
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
    // A hard grip on one particle, held at the middle of the throat and carried with the hook: the barb catching the yarn, not a ring it
    // slides through. Used through the pull so the caught bight really goes through the old loop.
    void gripThroat(int particle);
    void releaseThroatGrip();
    int throatGrip() const { return catchPin_; }
    // A guide spring on one particle: the plan's replacement for a hard pin. It is solved inside the rod's XPBD loop with stiffness k, so it
    // can be scaled by SIM_ASSIST; at a large k it acts as a pin. The caller updates `target` to move it.
    int addGuideSpring(int particle, const Vec3& target, Real k, int kind = 0);
    void moveGuideSpring(int s, const Vec3& target) { if (s >= 0 && s < (int)rs.guideSprings.size()) rs.guideSprings[s].target = target; }
    void releaseGuideSpring(int s) { if (s >= 0 && s < (int)rs.guideSprings.size()) rs.guideSprings[s] = fl::GuideSpring{}; }
    void releaseGuideSprings();
    bool fingerEyelet = true;
    Real throatX = 0.30;   // where the throat holds a caught yarn, out from the axis toward the throat's mouth (shaft radii)
    Real throatZ = 2.50;   // and how far from the tip, along the axis (shaft radii)
    Real throatStiffness = 0.5;   // the throat's hold is soft (contact can push back on it), the finger's is firm
    bool homeSet = false;   // where the hook works from, stitch after stitch (set by the first chain)
    int chains = 0;         // completed chains; the left hand goes a little farther below the growing work for the fifth
    int lastLinkHi = -1;    // final particle of the last valid chain's linking stretch (the row's end; set by the caller after a made chain)
    int lastLinkLo = -1;    // first particle of that stretch: the stitch loop the next chain is worked into
    int lastLinkChain = -1; // the chain count when it was made, so a stitch after a failed chain does not reuse a stale loop
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
    // Checkpoints: the whole simulator state, for exploring from "the slip knot is made" or "stitch k passed" without repeating it. The
    // header carries the git hash, every HookShape and YarnSimParams field, the rod parameters, SIM_ASSIST and the environment knobs that were
    // set. loadState refuses a file whose physics settings differ from this build's.
    bool saveState(const std::string& path) const;
    bool loadState(const std::string& path, bool checkHeader = true);
    // The first state difference between two loaded checkpoints, for the bit-exact check; "identical" when every saved array matches.
    std::string stateDiff(const YarnSim& other) const;

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
    int catchPin_ = -1;                        // the particle the barb holds at the throat, -1 when none
    int catchSpring_ = -1;                     // its guide spring in rs.guideSprings (the barb's hold is a stiff spring, not a pin)
    Vec3 catchOffset_{};                       // where that particle sits in the hook's frame (it is carried rigidly, not pulled into the throat)
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
    int prevHi = -1;            // the last valid chain's final linking stretch ended here (particle index); -1 for the first chain
    int loopsAtStart = 0, loopsBefore = 0, loopsAfterYo = 0;   // loopsOnHook(front) at the start and after the yarn over
    std::vector<int> prepHeld;  // the old loop's front, held through the yarn over (PREP_HOLD=2)
    std::vector<int> prepHeldSprings;   // their guide springs
    int catchParticle = -1;     // the working-yarn particle the throat caught
    bool catchGripped = false;  // the barb holds it (a hard grip) through the pull
    Real length = 0;            // the yarn between the tail and the hand (m) when the stitch began
};
ChainStart prepareChain(YarnSim& sim);
void finishChain(YarnSim& sim, const ChainStart& c);
// Was a chain made: a loop is round the hook (in a copy, with the work held and the working yarn pulled firmly, the yarn closes round the shaft),
// the old loop is off it, and more yarn has been drawn from the hand into the work than before the stitch (a stitch that unravelled gives it back).
struct ChainResult {
    Real link = 0;   // degrees of the shaft the yarn closes round when the working yarn is pulled firmly (a loop on the hook: ~360)
    int loops = 0;   // the linking stretch: exactly one loop, whichever way it winds (the sign is in frontLoops)
    int frontLoops = 0;   // loopsOnHook(front), which also counts the chain's legs beside the hook; its sign is diagnostic
    int oldOn = 0;   // particles shared by the final linking stretch and the old loop
    bool newYarn = false;   // the linking stretch reaches into the yarn drawn after the old loop (not an earlier work loop)
    bool sequential = false;  // the linking stretch ends at or after the last valid chain's end, so the stitches form one row
    int linkLo = -1, linkHi = -1;   // the final linking stretch's particles
    Real oldWind = 0;   // how far the old loop's own yarn outside the final stretch still winds round the hook (turns)
    Real linkMeanR = 0, linkMaxR = 0;  // distance of the final linking stretch from the hook's axis (m)
    Real length = 0;
    bool made = false;
};
// A linked stretch is one loop whichever way it winds; the half-plane count is only a fallback when no stretch closes.
inline int linkedLoopCount(bool linked, int frontLoops) { return linked ? 1 : frontLoops; }
ChainResult judgeChain(const YarnSim& sim, const ChainStart& c);
// A chain stitch, judged without closing any stretch with a chord: the final loop is the shortest stretch of yarn that the hook's axis
// passes through, the old loop's own yarn is off the hook, the yarn drawn after the old loop crosses the disc the old loop spans, and the
// final loop is long enough and far enough from the axis to sit on the shaft. Contact is the state when the stitch has just been made.
struct StitchVerdict {
    bool onHook = false;      // `hookLoop` found the final loop
    Real fresh = 1;           // the final loop's share of particles that belong to the old loop
    bool newYarn = false;     // the final loop reaches the yarn drawn after the old loop
    bool sequential = false;  // the final loop ends at or after the last valid stitch's end
    Real oldOff = 0;          // the old loop's own winding round the hook outside the final loop (turns)
    int through = 0;          // signed crossings of the new loop's outer leg through the old loop's disc (the sum; printed with the sign)
    int throughIn = 0;        // the inner leg's crossings (diagnostic: it starts on the old loop's edge)
    int throughCount = 0;     // the outer leg's crossings whichever way they go; the pass test reads this (a leg may cross either way)
    bool throughValid = true; // false when the old loop's ends are more than three yarn widths apart (its fan is not a disc)
    Real meanR = 0;           // the final loop's mean distance from the hook's axis (m)
    Real length = 0;          // the final loop's yarn length (m)
    Real gap = 0;             // the closest non-neighbour self-gap over the whole stitch (m)
    Real depth = 0;           // the deepest point of the yarn inside the hook (m)
    int linkLo = -1, linkHi = -1;   // the final loop
    bool onShaftPass = false; // the final loop sits on the shaft: radius, length and the length cap
    bool contactPass = false; // the running contact stayed clear: self-gap and hook depth
    bool pass = false;
};
StitchVerdict judgeStitch(const YarnSim& sim, const ChainStart& c);
int carriedRound(const YarnSim& sim);   // 360 with a loop on the hook, else 0
// The loop on the hook: the shortest stretch of yarn, from particle `from` to `to` where the yarn touches itself, that the hook's axis passes
// through (linking number 1 with the axis closed far away). False when there is none. Use this for the stitch, not loopsOnHook(front), which
// also counts the chain's legs hanging beside the hook.
// `touchRadii` is how close two strands of the loop must lie to count as touching, in yarn radii (the simulator uses 5, the judge 2.5).
bool hookLoop(const YarnSim& sim, int* from, int* to, int searchFrom = -1, Real touchRadii = 5.0);

// The judge fixtures (plan section 4.5.1): analytic yarn polylines in the hook's frame, no simulation. `name` is "true" or one of the seven
// negatives ("beside", "beside-far", "old-on-shaft", "neck", "loose", "contact", "fresh"). The hook lies at the origin with its axis along +z
// and its mouth toward +x. `note` says what the geometry is for the log.
struct StitchFixture {
    YarnSim sim;
    ChainStart chain;
    std::string note;
};
StitchFixture buildStitchFixture(const std::string& name);

// A test rig for stitches: the hook lying tip to the left at `tip`, a loop snug on its shaft, its front leg running down to the work (the tail end,
// held), its back leg up behind to the index finger and over it to the spool. Builds `sim` (finger on, hook posed).
void buildLoopRig(YarnSim& sim, const Vec3& tip);

// The yarn laid out for the slip knot (tail first): exposed so a caller can set the simulation up and drive its own frames.
std::vector<Vec3> slipKnotLayout(Real radius);

}  // namespace fl
