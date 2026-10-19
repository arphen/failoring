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
    int total = 0, clean = 0;
    for (const auto& t : threadingReport(w)) {
        if (t[0] <= 1) continue;   // the slip knot's own first loop is not part of the design
        ++total;
        if (t[2] == 1 && t[3] == 1) ++clean;
    }
    CHECKP(total >= 20 && clean * 100 >= total * 90, "at least 90%% of the %d passes through an eye are clean threadings (one piercing each way): %d", total, clean);
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

}  // namespace

int main() {
    testCursor();
    testThreading();
    testOneLoop();
    testDeterminism();
    FINISH();
}
