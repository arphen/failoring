#pragma once
// Yarn mode (docs/YARN_MODE_PLAN.md): a piece of crochet made one hook action at a time, from one or more yarns. The hook is not simulated; its
// motion is cosmetic and the topology is constructive: an action lays yarn along a path through the chosen openings, on a lattice, tight.
//
// The DESIGN is the truth: every yarn is a STRAND, a polyline of nodes (`pts`) with a rest length per segment and a colour. The rod system
// (`rod`) is derived from the strands after every action (it is rebuilt from them) and is only there for softness: a shape memory spring pulls
// each node back to its designed place while strands push each other apart and can be pulled on. Eye frames, threading checks and the editor's
// cursor all read the design, so they do not depend on the soft state.
//
// Vocabulary. A LOOP is a stretch of a strand that is a finger (a hairpin): its two legs pass through the eye of the loop(s) it was pulled
// through, and its own eye is the ring it makes. Every loop is an OPENING: a later finger can be threaded through its eye. The LIVE loops are the
// ones on the hook; a loop leaves the hook when a finger is pulled through it. A yarn over is a ring with no legs (it wraps the hook).
#include <array>
#include <string>
#include <vector>

#include "failoring/crochet/rod.hpp"
#include "failoring/math.hpp"

namespace fl {

struct Strand {
    std::vector<Vec3> pts;    // the design centre line, one point per rod node
    std::vector<Real> rest;   // rest length of segment i (pts[i] -> pts[i+1])
    Vec3 colour{0.80, 0.30, 0.22};
    bool cut = false;         // fastened off: the yarn ends in a tail, it no longer runs to its ball
    int first = 0;            // the global index of pts[0] in the rod system (set when it is built)
};

struct YarnLoop {
    int strand = 0;       // the strand it is made of
    int p0 = 0, p1 = 0;   // its nodes (inclusive, indices into the strand): the two legs and the ring round the hook
    int t0 = -1;          // the strand's last node before this loop's yarn began: the loop's working yarn to its first eye is t0 + 1 ... p0 - 1 (-1: unknown)
    int r0 = 0, r1 = 0;   // the ring part (inclusive); its eye is the area it encloses, closed by the chord r1 -> r0
    bool live = false;    // on the hook
    int row = 0;          // the row it was made in
    Real ringScale = 1.0; // how far the ring's yarn has been pulled in (1 = as made)
    std::vector<int> through;  // the loops whose eyes this loop's legs were pulled through
    Vec3 eyeShift{};      // where the loop is threaded, relative to the middle of its ring (a chain loop is threaded near its tip)
    int top = -1;         // the stitch this loop belongs to, by its top loop (a post, a yarn over or a middle loop point at the stitch's top; a top points at itself)
};

// Which loops of a stitch's top a new stitch goes under: both (through the eye, the usual), or only the front or only the back one (its legs
// straddle that strand alone and the other loop is left free: a ridge).
enum class Loops { Both, Front, Back };

// When a colour change takes effect in a stitch: at its last pull-through (the usual way: the stitch is finished in the new colour, so its top is
// the new colour's first loop) or from the start of the stitch (the whole stitch, post and top, is the new colour).
enum class ColourAt { PullThrough, Stitch };

// One entry of the pattern so far: a run of the same stitch in the same yarn.
struct LogEntry {
    std::string kind;   // "ch", "sc", "hdc", "dc", "sl st", "sc flo", ..., "sk", or "turn" (which starts a new row)
    int count = 1;
    int strand = -1;    // the yarn the stitches were made with (-1: none)
};

struct EyeFrame {
    Vec3 centre, normal;  // the normal follows the yarn's direction by the right hand rule
    Real radius = 0;      // of the circle of the ring's area
};

struct WorkParams {
    // The shape of a finger as it is made (m): two legs side by side through the eyes it is pulled through, then a ring (a bar, for the stitches).
    // The design is made tight, on a lattice; these were found by `yarn_tune` for clean threading with no overlap beyond what worsted squeezes.
    Real hookRadius = 0.0;
    Real legGap = 0.00134;         // half the distance between the two legs
    Real approach = 0.00162;       // the straight run of the legs either side of an eye
    Real fingerLength = 0.00103;   // legs beyond the last eye before they flare out to the ring
    Real flare = 0.0030;           // length over which the legs spread out to the ring
    Real ringRadius = 0.0040;      // the ring of the slip knot's first loop
    Real chainPitch = 0.00846;     // between the rings of a foundation chain
    Real chainA = 0.0060;          // a chain's ring: semi-axis along the chain
    Real chainB = 0.0034;          // ... and across it
    Real chainLayer = 0.0015;      // the two layers the rings of a chain alternate between
    Real chainTaper = 0.5;         // a chain's ring narrows toward its tip (0: an ellipse, 0.5: a teardrop: each loop closes to a V round the next)
    Real chainEye = 0.55;           // the next loop is threaded through this fraction of chainA toward the tip
    Real workingRun = 0.0010;      // the yarn laid out after a finger toward the ball
    Real travelSlack = 3.0;        // the working yarn to the next eye is pulled taut: its rest length is this times the straight distance
    Real segmentLength = 0.0020;   // the rod's node spacing
    Real tighten = 1.0;            // the rest length of every segment is its made length times this
    Real closeFactor = 1.0;        // a loop that a finger is pulled through is pulled in: its ring's rest length times this (once per pass)
    Real closeFloor = 1.0;         // ... but no further than this fraction of the ring as made
    int settleSteps = 0;           // rod steps after each action
    // the lattice the rings are placed on
    Real stitchWidth = 0.00846;    // between neighbouring rings in a row
    Real stitchHeight = 0.00568;   // one row of single crochet
    Real layerGap = 0.00216;       // between the front and the back layer of rings (about a strand)
    Real barA = 0.00304;           // a stitch's top loop is a bar: semi-axis along the row
    Real barB = 0.0022;            // ... and across it
    Real barTilt = 0.75157;        // rad: how far the bar leans back from the horizontal toward the worker
    Real postRise = 0.99442;       // the loop pulled up through the stitch below stands this fraction of its stitch up
    Real hdcScale = 1.58008;           // the heights of the other stitches, in rows of single crochet
    Real dcScale = 2.1;
    Real slstScale = 0.6;
    Real yoHdc = 0.51777;             // where the yarn over of an hdc / dc lies, as a fraction of the stitch's height
    Real yoDc = 0.58307;
    Real riseDc = 0.55;            // the loop pulled up through the stitch (dc), and the middle loop after the first pull-through
    Real midDc = 1.0;
    Real yoLayer = 1.54755;            // how far behind the post the yarn overs lie, in layer gaps
    Real workTension = 0.0;        // N pulling the working end along its last run
    Real tailLength = 0.030;       // the tail of a yarn that is started
};

class YarnWork {
public:
    RodSystem rod;                 // derived from the strands; rebuilt after every action
    RodParams rodParams;           // the soft physics' settings (rod.params is set from this whenever the rod is rebuilt)
    WorkParams wp;
    std::vector<Strand> strands;
    std::vector<YarnLoop> loops;
    std::vector<int> hook;         // the live loops, the last one nearest the hook's tip
    Vec3 progress{1, 0, 0};        // the way the work advances along a row (the hook's axis)
    Vec3 upDir{0, 1, 0};           // the way rows stack
    int row = 0;
    int active = 0;                // the strand being worked with

    YarnWork();

    // Starts the first yarn: a tail, the slip knot's ring and one loop on the hook, at `at`. Returns the new live loop.
    int slipKnot(const Vec3& at, const Vec3& colour = Vec3{0.80, 0.30, 0.22});
    // yarn over, pull through the loop on the hook: a chain stitch. Returns the new live loop.
    int chain();
    // single crochet / half double / double into `opening`. Returns the final live loop.
    int sc(int opening, Loops which = Loops::Both);
    int hdc(int opening, Loops which = Loops::Both);
    int dc(int opening, Loops which = Loops::Both);
    // slip stitch into `opening`: insert, yarn over, pull through the opening and the loop on the hook in one go (a flat loop)
    int slst(int opening, Loops which = Loops::Both);
    // turns the work over: the next row goes back the other way
    void turn();

    // --- colours: the next stitch changes to this colour. If a yarn of that colour was used before it is picked up where it was dropped,
    // otherwise a new yarn is started (a tail near the hook). ---
    void setNextColour(const Vec3& colour, ColourAt when = ColourAt::PullThrough);
    // Fastens off: the yarn is cut, its end is drawn through the loop on the hook (a tail hangs from it) and the loop is pulled tight. The work is finished
    // (no more stitches). Returns the tail as a loop (its yarn is what the hook draws through), or -1 when there is nothing to fasten off.
    int finishOff();
    bool finished() const { return finished_; }
    bool colourPending() const { return pending_; }
    const Vec3& activeColour() const { return strands[active].colour; }

    // --- the editor's view of the work (docs/YARN_MODE_PLAN.md section 11): one click per stitch, with the usual choices made ---
    // The opening the next stitch goes into when the user does not pick one: the next stitch of the row below, in the order they are worked.
    int nextOpening() const;
    // `kind` is "sc", "hdc", "dc" or "slst"; `opening` -1 means the next opening; the cursor moves on. Returns the new live loop, or -1 when there is
    // nothing to work into.
    int stitch(const std::string& kind, int opening = -1, Loops which = Loops::Both);
    int stitchSc(int opening = -1, Loops which = Loops::Both) { return stitch("sc", opening, which); }
    // skips the next stitch of the row below (no yarn: the hook just moves on)
    bool stitchSkip();
    // a chain stitch (it is not worked into anything, so the cursor stays)
    int stitchChain();
    // turns the work over: `turningChain` chain stitches (1 for sc, 2 for hdc, 3 for dc), then the next row goes back along the row just made. After the
    // foundation chain the loop on the hook is the first chain from the hook and is skipped.
    void stitchTurn(int turningChain);
    const std::vector<int>& targetRow() const { return target_; }
    int cursor() const { return cursor_; }
    int rowStitches() const { return (int)made_.size(); }
    // The pattern so far as CrochetPARADE text, one line per row ("ch 8", "sc 7").
    std::string patternText() const;
    // ... and as rows of runs (for drawing it): each row is a list of entries
    std::vector<std::vector<LogEntry>> patternRows() const;
    // Loops the last pull-through of the next stitch picks up as well as its own: the way a seam is joined. They are cleared once a stitch is made.
    void toggleExtraLoop(int loop);
    const std::vector<int>& extraLoops() const { return extra_; }

    // Rebuilds the rod system from the strands (every action does it; call it after changing `rodParams`).
    void materialize();
    // runs `steps` rod steps (settleSteps when 0)
    void settle(int steps = 0);

    EyeFrame eye(int loop) const;
    Vec3 point(int strand, int i) const { return strands[strand].pts[i]; }
    // where a (cosmetic) hook would be: at the ring of the live loop, along its axis; invalid before the slip knot
    struct HookPose { Vec3 tip, axis; bool valid = false; };
    HookPose hookPose() const;
    Vec3 endPosition() const { return strands[active].pts.back(); }
    int particleCount() const { return rod.particleCount(); }
    // The yarn of `loop` while the hook draws it through the eye of the loop it goes through (`s` 0..1): at 0 a hairpin lies in the throat of the hook
    // behind the eye, the legs run through the eye to the yarn already worked; the hook draws it to the worker's side and the loop opens out as it comes,
    // and at 1 it is the loop as it is. `pts` are the rod particles `first` ... `first + pts.size() - 1` as they are at `s`; `tip` is where the hook's
    // throat is and `axis` the way from there toward the handle. A yarn over (no eye) just grows into its ring round the hook.
    // `wrap` (0..1) lays the working yarn over the shaft from behind, over the top, to the front (the yarn over); the yarn to the loop's first eye is part of `pts` for that.
    struct PullFrame { std::vector<Vec3> pts; int first = 0; Vec3 tip, axis, eye; bool through = false; };
    PullFrame pullFrame(int loop, Real s, Real wrap = 0.0) const;
    // the colour of each rod segment (for drawing)
    std::vector<Vec3> segmentColours() const;

private:
    std::vector<int> target_;    // the openings of the row being worked into, in the order they will be worked
    std::vector<int> made_;      // the loops this row has made, in order (the stitches' top loops)
    int cursor_ = 0;
    std::vector<LogEntry> log_;   // the actions, a turn starts a new row
    std::vector<int> extra_;      // loops the next stitch's last pull-through picks up as well (the seam variations)
    Vec3 origin_{0, 0, 0};
    int chainCount_ = 0;
    int turnChains_ = 0;         // turning chains made at the start of this row
    bool pending_ = false;
    bool finished_ = false;
    Vec3 pendingColour_{};
    ColourAt pendingAt_ = ColourAt::PullThrough;
    Real taper_ = 0.0;           // loopAt: the ring narrows toward +U by this much (a teardrop)
    Vec3 zAxis() const { return cross(Vec3{1, 0, 0}, upDir); }   // the front of the work (it does not turn with the row direction)
    // A loop whose ring is put at `ringC`, its plane square to `ringN`; its legs go from the ring through the eyes of `eyesLoops` (in order) and
    // back (none: a yarn over, a ring and nothing else). The ring is a circle of ringRadius or an ellipse (a top loop seen edge on is a bar). `modes`
    // (parallel to `eyesLoops`, optional): 0 through the eye, +1 / -1 round the strand of the ring nearest the worker (`front`) / farthest from it.
    // The live loops among the eyes leave the hook; the new loop is live (a yarn over too).
    int loopAt(const Vec3& ringC, const Vec3& ringN, const std::vector<int>& eyesLoops, const Vec3* major = nullptr, Real semiA = -1, Real semiB = -1,
               const std::vector<int>* modes = nullptr, const Vec3* front = nullptr);
    int addLoop(int strand, int p0, int p1, bool live);
    void pushLog(const std::string& kind, int strand);
    void appendPath(const std::vector<Vec3>& way);   // to the active strand
    int startYarn(const Vec3& colour);                // a new strand: a tail by the hook; returns its index
    void applyPendingColour(ColourAt now);
    int tallStitch(const std::string& kind, int opening, Loops which);
    int tallStitchBody(const std::string& kind, int opening, Loops which);
    Real heightOf(const std::string& kind) const;
};

// The signed piercings of each loop's ring by the yarn's segments: for every loop, the segments (other than the ring's own) that cross the disc of
// the ring (a fan of triangles from its centroid), as {loop, strand of the segment's owner loop or -1, +1 or -1} summed, sorted. For a given set of
// loops this list is unchanged by any motion of the yarn that does not pass a strand through another one.
std::vector<std::array<int, 3>> loopPiercings(const YarnWork& w);

// For every loop that was pulled through other loops: the signed piercings of each of those loops' ring discs by this loop's own legs, as
// {loop, eye, +count, -count}. A finger that really threads an eye has exactly one piercing each way ({.., 1, 1}).
std::vector<std::array<int, 4>> threadingReport(const YarnWork& w);

}  // namespace fl
