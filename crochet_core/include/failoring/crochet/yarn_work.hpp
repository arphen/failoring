#pragma once
// Yarn mode, phase Y2 (docs/YARN_MODE_PLAN.md sections 3-4): a piece of crochet made one hook action at a time, as ONE continuous
// yarn. The hook is not simulated; its motion is cosmetic and the topology is constructive: an action appends yarn along a path through
// the chosen openings, and the rod solver (rod.hpp) relaxes it, with strand contact keeping every loop hooked into the ones it passes
// through.
//
// Vocabulary. A LOOP is a stretch of the yarn that is a finger (a hairpin): its two legs pass through the eye of the loop(s) it was
// pulled through, and its own eye is the space between its legs, closed at the bottom by the loop it was pulled through. Every loop is
// an OPENING: a later finger can be threaded through its eye. The LIVE loops are the ones on the hook. `pullUp(opening)` is
// "insert the hook into the opening, yarn over, pull a loop through": one new live loop, the opening is not consumed.
// `pullThrough(k)` is "yarn over, pull through the top k loops on the hook": they are consumed and one new live loop is left.
#include <array>
#include <vector>

#include "failoring/crochet/rod.hpp"
#include "failoring/math.hpp"

namespace fl {

struct YarnLoop {
    int p0 = 0, p1 = 0;   // the loop's particles (inclusive): the two legs and the ring round the hook
    int r0 = 0, r1 = 0;   // the ring part (inclusive); its eye is the circle it encloses, closed by the chord r1 -> r0
    bool live = false;    // on the hook
    int row = 0;          // the row it was made in (for the caller's bookkeeping)
    Real ringScale = 1.0; // how far the ring's yarn has been pulled in (1 = as made)
    std::vector<int> through;  // the loops whose eyes this loop's legs were pulled through
};

struct EyeFrame {
    Vec3 centre, normal;  // the normal follows the yarn's direction by the right hand rule
    Real radius = 0;      // of the circle of the ring's area
};

struct WorkParams {
    // The shape of a finger as it is made (m). A loop on the hook is a ring round the hook's shaft with its two legs running back along
    // the shaft, side by side, through the loops it was pulled through. It is made roomy and then tightened (tension on the working end):
    // the loops it passed through close onto its legs, the live ring stays open round the hook.
    Real hookRadius = 0.0;     // the hook's shaft (a 4 mm hook)
    Real legGap = 0.00134;         // half the distance between the two legs
    Real approach = 0.00162;       // the straight run of the legs either side of an eye
    Real fingerLength = 0.00103;   // legs beyond the last eye before they flare out to the ring
    Real flare = 0.0030;          // length over which the legs spread out to the ring
    Real ringRadius = 0.0040;     // the live ring while it is made (the hook's radius and a strand's, once it is tight)
    Real workingRun = 0.0010;     // the yarn laid out after a finger toward the ball
    Real travelSlack = 3.0;      // the working yarn to the next eye is pulled taut: its rest length is this times the straight distance
    Real travelLift = 0.0085;     // the working yarn goes round the outside of the work to the next eye, this far out (m)
    Real segmentLength = 0.0020;  // the rod's node spacing
    Real tighten = 1.0;           // the rest length of every segment is its made length times this
    Real closeFactor = 1.0;       // a loop that a finger is pulled through is pulled in: its ring's rest length times this (once per pass)
    Real closeFloor = 1.0;       // ... but no further than this fraction of the ring as made
    int settleSteps = 0;         // rod steps after each action
    // the lattice the rings are placed on (flat work: rings lie in the fabric plane, rows alternate which side of it they sit on)
    Real stitchWidth = 0.00846;    // between neighbouring rings in a row
    Real stitchHeight = 0.00568;   // between rows
    Real layerGap = 0.00216;       // between the front and the back layer of rings (about a strand)
    Real barA = 0.00304;           // a stitch's top loop is a bar: semi-axis along the row
    Real barB = 0.0022;           // ... and across it
    Real barTilt = 0.75157;          // rad: how far the bar leans back from the horizontal toward the worker (its normal is up, tipped this far toward the front)
    Real postRise = 0.99442;          // the loop pulled up through the stitch below stands this fraction of a row up
    Real workTension = 0.0;      // N pulling the working end along its last run, as the crocheter's hand does
};

class YarnWork {
public:
    RodSystem rod;
    WorkParams wp;
    std::vector<YarnLoop> loops;
    std::vector<int> hook;        // the live loops, the last one nearest the hook's tip
    Vec3 progress{1, 0, 0};       // the way the work advances along a row (the hook's axis)
    Vec3 upDir{0, 1, 0};          // the way rows stack
    int row = 0;
    int side = 1;                 // which layer (+1 front, -1 back) this row's rings sit in

    YarnWork();

    // Starts the yarn: a tail, the slip knot's ring and one loop on the hook, at `at`. Returns the new live loop.
    int slipKnot(const Vec3& at);
    // yarn over, pull through the loop on the hook: a chain stitch. Returns the new live loop.
    int chain();
    // insert the hook in `opening` (a loop), yarn over, pull a loop up through it. The new loop is live; the opening stays.
    int pullUp(int opening);
    // yarn over, pull through the top `k` live loops (they are consumed). Returns the new live loop.
    int pullThrough(int k);
    // single crochet into `opening`: pullUp, then pullThrough(2). Returns the final live loop.
    int sc(int opening);
    // turns the work over: the next row goes back the other way, on the other side
    void turn();
    // --- the editor's view of the work (docs/YARN_MODE_PLAN.md section 11): one click per stitch, with the usual choices made ---
    // The opening the next stitch goes into when the user does not pick one: the next stitch of the row below, in the order they are worked.
    int nextOpening() const;
    // single crochet into `opening` (-1: the next opening); the cursor moves on. Returns the new live loop, or -1 when there is nothing to work into.
    int stitchSc(int opening = -1);
    // a chain stitch (it is not worked into anything, so the cursor stays)
    int stitchChain();
    // turns the work over: `turningChain` chain stitches (1 for sc), then the next row goes back along the row just made. After the foundation
    // chain the loop on the hook is the first chain from the hook and is skipped (sc in the 2nd chain from the hook).
    void stitchTurn(int turningChain);
    // The openings the work is going into, in order, and how far along (for the editor to highlight).
    const std::vector<int>& targetRow() const { return target_; }
    int cursor() const { return cursor_; }
    int rowStitches() const { return (int)made_.size(); }
    // The pattern so far as CrochetPARADE text, one line per row ("ch 8", "sc 7").
    std::string patternText() const;
    // runs `steps` rod steps (settleSteps when 0)
    void settle(int steps = 0);
    // Pulls the whole yarn up to its rest lengths scaled by `factor` (< 1 tightens, > 1 loosens) and relaxes.
    void setSlack(Real factor, int steps = 200);

    EyeFrame eye(int loop) const;
    // where a (cosmetic) hook would be: at the ring of the live loop, along its axis; invalid before the slip knot
    struct HookPose { Vec3 tip, axis; bool valid = false; };
    HookPose hookPose() const;
    Vec3 endPosition() const;
    int particleCount() const { return rod.particleCount(); }

private:
    std::vector<int> target_;    // the openings of the row being worked into, in the order they will be worked
    std::vector<int> made_;      // the loops this row has made, in order (the stitches' top loops)
    int cursor_ = 0;
    std::vector<std::pair<std::string, int>> log_;   // (kind, count) of the actions, a turn starts a new row
    Real slack_ = 1.0;
    Vec3 origin_{0, 0, 0};
    int chainCount_ = 0;
    Vec3 zAxis() const { return cross(Vec3{1, 0, 0}, upDir); }   // the front of the work (it does not turn with the row direction)
    int finger(const std::vector<int>& eyesLoops, bool consume, const Vec3* axisHint = nullptr);
    // A loop whose ring is put at `ringC`, its plane square to `ringN`; its legs go from the ring through the eyes of `eyesLoops` (in order)
    // and back. The legs pass each eye towards the ring, bending to face it when the line is too slanting.
    // The ring is a circle of ringRadius, or an ellipse with semi-axes semiA along `major` and semiB across it (a top loop seen edge on is a bar).
    int loopAt(const Vec3& ringC, const Vec3& ringN, const std::vector<int>& eyesLoops, bool consume, const Vec3* major = nullptr, Real semiA = -1, Real semiB = -1);
    int addLoop(int p0, int p1, bool live);
    void appendPath(const std::vector<Vec3>& pts);
};

// The signed piercings of each loop's ring by the yarn's segments: for every loop, the segments (other than the ring's own) that cross
// the disc of the ring (a fan of triangles from its centroid), as {loop, segment, +1 or -1}, sorted. For a given set of loops this list
// is unchanged by any motion of the yarn that does not pass a strand through another one: the check that expanding and tightening the
// work keeps every loop hooked into what it was hooked into.
std::vector<std::array<int, 3>> loopPiercings(const YarnWork& w);

// For every loop that was pulled through other loops: the signed piercings of each of those loops' ring discs by this loop's own legs, as
// {loop, eye, +count, -count}. A finger that really threads an eye has exactly one piercing each way ({.., 1, 1}).
std::vector<std::array<int, 4>> threadingReport(const YarnWork& w);

}  // namespace fl
