// Yarn mode: the work made one hook action at a time (docs/YARN_MODE_PLAN.md): the cursor and the pattern text, that every finger really
// threads the loops it is meant to (the legs pierce the eye's disc once each way), working into one loop only, skipping, and determinism.
#include <cmath>
#include <cstdio>
#include <string>

#include "check.hpp"
#include "failoring/crochet/yarn_work.hpp"

using namespace fl;

#define CHECKP(cond, ...)                                 \
    do {                                                  \
        CHECK(cond);                                      \
        if (cond) {                                       \
            std::printf("PASS ");                         \
            std::printf(__VA_ARGS__);                     \
            std::printf("\n");                            \
        }                                                 \
    } while (0)

namespace {

// how many of the passes of the loop `id` through the eye of `eye` pierce its disc, up and down
std::array<int, 2> piercings(const YarnWork& w, int id, int eye) {
    for (const auto& t : threadingReport(w))
        if (t[0] == id && t[1] == eye) return {t[2], t[3]};
    return {-1, -1};
}

void testCursor() {
    std::printf("[cursor and text]\n");
    YarnWork w;
    w.slipKnot(Vec3{0, 0, 0});
    CHECKP(w.nextOpening() < 0 && w.hook.size() == 1, "a slip knot: one loop on the hook, nothing to work into");
    for (int i = 0; i < 5; ++i) w.stitchChain();
    CHECKP(w.rowStitches() == 5, "five chains make five stitches in the row");
    w.stitchTurn(0);
    CHECKP(w.row == 1 && w.targetRow().size() == 4 && w.cursor() == 0, "turning after the foundation chain: four chains to work into (the one on the hook is skipped)");
    CHECKP(w.nextOpening() == w.targetRow()[0], "the next opening is the first of them");
    const int made = w.stitchSc();
    CHECKP(made >= 0 && w.cursor() == 1 && w.nextOpening() == w.targetRow()[1], "an sc works into it and the cursor moves on");
    const int before = w.particleCount();
    const bool skipped = w.stitchSkip();
    CHECKP(skipped && w.cursor() == 2 && w.particleCount() == before, "skipping moves the cursor on and adds no yarn");
    w.stitchSkip();
    w.stitchSc();
    const bool endOfRow = w.nextOpening() < 0;
    const int extra = w.stitchSc();
    const bool extraSkip = w.stitchSkip();
    CHECKP(endOfRow && extra < 0 && !extraSkip, "at the end of the row there is nothing to work into");
    CHECKP(w.patternText() == "ch 5\nsc 1, sk 2, sc 1\n", "the pattern text so far: '%s'", w.patternText().c_str());
    w.stitchTurn(1);
    w.stitchSc();
    CHECKP(w.patternText() == "ch 5\nsc 1, sk 2, sc 1\nch 1, sc 1\n", "the next row starts with its turning chain: '%s'", w.patternText().c_str());
}

void testThreading() {
    std::printf("[threading]\n");
    YarnWork w;
    w.slipKnot(Vec3{0, 0, 0});
    for (int i = 0; i < 6; ++i) w.stitchChain();
    w.stitchTurn(0);
    for (int i = 0; i < 5; ++i) w.stitchSc();
    w.stitchTurn(1);
    for (int i = 0; i < 5; ++i) w.stitchSc();
    int total = 0, clean = 0, threaded = 0;
    for (const auto& t : threadingReport(w)) {
        if (t[0] <= 1) continue;   // the slip knot's own first loop is not part of the design
        ++total;
        if (t[2] >= 1 && t[3] >= 1) ++threaded;
        if (t[2] == 1 && t[3] == 1) ++clean;
    }
    CHECKP(total >= 20 && threaded == total, "every one of the %d passes through an eye goes through it (a piercing each way at least): %d", total, threaded);
    CHECKP(clean * 100 >= total * 70, "and most are clean (exactly one piercing each way; the rest wiggle through the eye more than once): %d of %d", clean, total);
    // every sc goes into its opening: the legs of the loop pulled up pierce the opening's disc
    bool allSc = true;
    for (size_t i = 0; i < w.loops.size(); ++i)
        for (int eye : w.loops[i].through)
            if (w.loops[i].row >= 1 && w.loops[eye].row == w.loops[i].row - 1) {
                const auto p = piercings(w, (int)i, eye);
                if (p[0] + p[1] < 2) allSc = false;
            }
    CHECKP(allSc, "every stitch's loop pulled up through the row below goes through that stitch's eye (both legs)");
}

void testStitchKinds() {
    std::printf("[stitch kinds]\n");
    YarnWork w;
    w.slipKnot(Vec3{0, 0, 0});
    for (int i = 0; i < 6; ++i) w.stitchChain();
    w.stitchTurn(0);
    std::array<Real, 4> topY{};
    const char* kinds[4] = {"sc", "hdc", "dc", "slst"};
    const int turns[4] = {1, 2, 3, 1};
    for (int k = 0; k < 4; ++k) {
        for (int i = 0; i < 3; ++i) w.stitch(kinds[k]);
        // the height of the row's last loop above the foundation (the lattice: sc 1 row, hdc 1.5, dc 2, sl st less than 1)
        topY[k] = w.eye(w.hook.back()).centre.y;
        if (k < 3) w.stitchTurn(turns[k]);
    }
    const Real h = w.wp.stitchHeight;
    const Real incSc = topY[0], incHdc = topY[1] - topY[0], incDc = topY[2] - topY[1];
    std::printf("  the rows rise %.1f (sc), %.1f (hdc), %.1f (dc) mm above the one before\n", incSc * 1e3, incHdc * 1e3, incDc * 1e3);
    CHECKP(incHdc > 0.9 * incSc && incDc > incHdc, "taller stitches stand taller (hdc row %.1f mm, dc row %.1f mm on rows of sc %.1f mm)", incHdc * 1e3, incDc * 1e3, h * 1e3);
    int total = 0, threaded = 0;
    for (const auto& t : threadingReport(w)) {
        if (t[0] <= 1) continue;
        ++total;
        if (t[2] >= 1 && t[3] >= 1) ++threaded;
    }
    CHECKP(threaded == total && total > 50, "every pass of every stitch kind goes through its eye (%d of %d)", threaded, total);
    CHECKP(w.patternText().find("hdc 3") != std::string::npos && w.patternText().find("dc 3") != std::string::npos && w.patternText().find("sl st 3") != std::string::npos,
           "the pattern text names them: '%s'", w.patternText().c_str());
}

// Colours: a colour change starts (or picks up) a yarn at the stitch's last pull-through; extra loops can be picked up in a pull-through (a seam).
void testColoursAndSeams() {
    std::printf("[colours and seams]\n");
    YarnWork w;
    w.slipKnot(Vec3{0, 0, 0});
    for (int i = 0; i < 7; ++i) w.stitchChain();
    w.stitchTurn(0);
    const int redTop = w.stitchSc();   // the top loop of the first stitch, in the first colour
    for (int i = 0; i < 2; ++i) w.stitchSc();
    CHECKP(w.strands.size() == 1 && w.active == 0, "one yarn so far");
    const Vec3 orange{0.95, 0.58, 0.18};
    w.setNextColour(orange);
    CHECKP(w.colourPending() && w.strands.size() == 1, "choosing a colour changes nothing until the next stitch");
    const int seam = w.stitchSc();
    CHECKP(w.strands.size() == 2 && w.active == 1 && !w.colourPending(), "the next stitch starts the new yarn");
    CHECKP(w.loops[seam].strand == 1, "its top loop is the new colour (the usual way: the last pull-through changes the colour)");
    const int post = w.loops[seam].through[0];
    CHECKP(w.loops[post].strand == 0, "while its post, pulled up through the stitch below, is still the old colour");
    CHECKP(w.strands[1].pts.size() > 10 && length(w.strands[1].pts.front() - w.strands[0].pts.back()) < 0.12, "the new yarn's tail is by the work, the dropped one's end stays at the seam");
    w.stitchSc();
    w.stitchTurn(1);
    w.stitchSc();
    // back to the first colour: it is picked up where it was dropped (no third yarn)
    w.setNextColour(Vec3{0.80, 0.30, 0.22});
    const int dropEnd = (int)w.strands[0].pts.size();
    w.stitchSc();
    CHECKP(w.strands.size() == 2 && w.active == 0 && (int)w.strands[0].pts.size() > dropEnd, "going back to the first colour picks that yarn up again, where it was dropped (%zu yarns)", w.strands.size());
    // the whole stitch in the new colour
    YarnWork v;
    v.slipKnot(Vec3{0, 0, 0});
    for (int i = 0; i < 4; ++i) v.stitchChain();
    v.stitchTurn(0);
    v.stitchSc();
    v.setNextColour(orange, ColourAt::Stitch);
    const int whole = v.stitchSc();
    CHECKP(v.loops[whole].strand == 1 && v.loops[v.loops[whole].through[0]].strand == 1, "from the start of the stitch: its post is the new colour too");
    // a seam variation: the last pull-through also picks up a loop of the other colour
    w.toggleExtraLoop(redTop);
    CHECKP(w.extraLoops().size() == 1 && w.extraLoops()[0] == redTop, "an extra loop is marked for the next stitch's last pull-through");
    const int joined = w.stitchSc();
    bool has = false;
    for (int e : w.loops[joined].through) has = has || e == redTop;
    int plus = 0, minus = 0;
    for (const auto& t : threadingReport(w))
        if (t[0] == joined && t[1] == redTop) { plus = t[2]; minus = t[3]; }
    CHECKP(has && plus >= 1 && minus >= 1 && w.extraLoops().empty(), "the pull-through goes through it too (%d up, %d down) and the mark is cleared", plus, minus);
    const auto rows = w.patternRows();
    bool twoYarns = false;
    for (const auto& r : rows) {
        bool y0 = false, y1 = false;
        for (const LogEntry& e : r) { y0 = y0 || e.strand == 0; y1 = y1 || e.strand == 1; }
        twoYarns = twoYarns || (y0 && y1);
    }
    CHECKP(rows.size() >= 3 && twoYarns, "the pattern rows remember which yarn each stitch was made in (%zu rows)", rows.size());
}

void testOneLoop() {
    std::printf("[one loop]\n");
    auto make = [](Loops which, int& post, int& opening) {
        YarnWork w;
        w.slipKnot(Vec3{0, 0, 0});
        for (int i = 0; i < 5; ++i) w.stitchChain();
        w.stitchTurn(0);
        opening = w.nextOpening();
        // the first sc of the row builds the loop pulled up (post) and the top; the post is the loop made before the top
        const int top = w.stitchSc(-1, which);
        post = w.loops[top].through[0];
        return std::array<int, 2>{piercings(w, post, opening)[0], piercings(w, post, opening)[1]};
    };
    int post = 0, opening = 0;
    const auto both = make(Loops::Both, post, opening);
    const auto front = make(Loops::Front, post, opening);
    const auto back = make(Loops::Back, post, opening);
    std::printf("  both loops: legs pierce the eye %d up %d down; front only: %d and %d; back only: %d and %d\n", both[0], both[1], front[0], front[1], back[0], back[1]);
    CHECKP(both[0] == 1 && both[1] == 1, "under both loops the two legs go through the eye, one each way");
    CHECKP(front[0] + front[1] == 1, "under the front loop only, one leg is inside the stitch's loop and the other outside it");
    CHECKP(back[0] + back[1] == 1, "under the back loop only, likewise");
}

void testDeterminism() {
    std::printf("[determinism]\n");
    auto run = []() {
        YarnWork w;
        w.slipKnot(Vec3{0, 0, 0});
        for (int i = 0; i < 4; ++i) w.stitchChain();
        w.stitchTurn(0);
        for (int i = 0; i < 3; ++i) w.stitchSc();
        return w;
    };
    const YarnWork a = run(), b = run();
    bool same = a.rod.x.size() == b.rod.x.size();
    for (size_t i = 0; same && i < a.rod.x.size(); ++i) same = length(a.rod.x[i] - b.rod.x[i]) == 0.0;
    CHECKP(same, "the same actions make the same yarn, bit for bit (%zu particles)", a.rod.x.size());
}

// Pulling on the yarn: a strand follows the pull softly (its neighbours with it, less and less), and when it is let go the work springs back.
// A yarn weighs micrograms per node, so without the pseudo-mass (RodSystem::setMassScale) every spring is a hard constraint at this step size and a
// pulled strand drags nothing along (mutant: scale 1 -> the neighbours stay put).
void pullTest(bool soft) {
    YarnWork w;
    w.slipKnot(Vec3{0, 0, 0});
    for (int i = 0; i < 6; ++i) w.stitchChain();
    RodSystem& rs = w.rod;
    rs.params.radiusScale = 0.55;
    rs.params.shapeStiffness = 40.0;
    rs.params.substeps = 2;
    rs.params.iterations = 1;
    if (!soft) {
        rs.setMassScale(1.0);
        rs.setRotationalInertiaScale(1.0);
    }
    for (int k = 0; k < 100; ++k) rs.step(1.0 / 60.0);   // the work as it settles under contact: this is what "back" means
    const std::vector<Vec3> rest = rs.x;
    const int g = 8;   // in the tail: a free strand
    rs.freezeOutside(rs.x[g], 0.02);
    rs.pin(g);
    const Vec3 start = rs.x[g];
    for (int k = 1; k <= 30; ++k) {
        rs.movePin(g, start + Vec3{0, 0, 0.004} * (k / 30.0));
        rs.step(1.0 / 60.0);
    }
    const Real pulled = length(rs.x[g] - start);
    const Real n1 = length(rs.x[g + 1] - rest[g + 1]), n3 = length(rs.x[g + 3] - rest[g + 3]), n6 = length(rs.x[g + 6] - rest[g + 6]);
    const Real far = length(rs.x[0] - rest[0]);
    std::printf("  %s: the gripped particle moved %.2f mm; its neighbours 1, 3, 6 along %.2f %.2f %.2f mm; the first of the yarn %.4f mm\n", soft ? "soft" : "hard", pulled * 1e3,
                n1 * 1e3, n3 * 1e3, n6 * 1e3, far * 1e3);
    if (soft) {
        CHECKP(pulled > 3.9e-3, "the gripped particle goes where it is pulled (%.2f of 4 mm)", pulled * 1e3);
        CHECKP(n1 > 2.5e-3 && n3 > 1.5e-3 && n6 > 0.3e-3 && n1 > n3 && n3 > n6, "its neighbours follow it, less and less (%.2f, %.2f, %.2f mm)", n1 * 1e3, n3 * 1e3, n6 * 1e3);
        CHECKP(far < 1e-9, "particles outside the region do not move at all (%.2g mm)", far * 1e3);
        rs.thaw();
        rs.unpin(g);
        for (int k = 0; k < 200; ++k) rs.step(1.0 / 60.0);
        Real worst = 0;
        for (int i = 0; i < rs.particleCount(); ++i) worst = std::max(worst, length(rs.x[i] - rest[i]));
        CHECKP(worst < 0.3e-3, "let go, the work springs back to within 0.3 mm of where it was (%.3f mm)", worst * 1e3);
    } else {
        CHECKP(n3 < 0.5e-3, "without the pseudo-mass the neighbours stay put (%.2f mm): the springs are hard constraints at this step", n3 * 1e3);
    }
}

void testSoftPull() {
    std::printf("[soft pull]\n");
    pullTest(true);
    pullTest(false);
}

}  // namespace

int main() {
    testCursor();
    testThreading();
    testStitchKinds();
    testColoursAndSeams();
    testOneLoop();
    testDeterminism();
    testSoftPull();
    FINISH();
}
