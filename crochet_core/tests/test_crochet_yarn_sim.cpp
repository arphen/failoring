// The process simulator (docs/YARN_MODE_PLAN.md section 14): the hook catches a yarn and carries it, and the slip knot made by moving the hook ends
// as a loop round the hook's shaft with the yarn never passing through itself or through the hook.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "check.hpp"
#include "failoring/crochet/yarn_sim.hpp"

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

void testCarry() {
    YarnSim sim;
    std::vector<Vec3> line;
    for (int i = 0; i <= 10; ++i) line.push_back(Vec3{0, -0.03 + 0.06 * i / 10.0, 0});
    sim.build(line);
    const Real R = sim.shape.R, m = 1e-3;
    const Real deep = -sim.params.radius - 3.4 * R;
    sim.hook = hookPose(Vec3{6 * m, 0, deep}, Vec3{0, 0, -1}, Vec3{-1, 0, 0});
    sim.moveHook(hookPose(Vec3{0.4 * m, 0, deep}, Vec3{0, 0, -1}, Vec3{-1, 0, 0}), 0.8);
    Real worst = 0;
    sim.onFrame = [&] { worst = std::max(worst, sim.maxStrain()); };
    sim.moveHook(hookPose(Vec3{0.4 * m, 0, deep + 0.025}, Vec3{0, 0, -1}, Vec3{-1, 0, 0}), 1.5);
    Real near = 1e9;
    const Vec3 throat = sim.throatPoint();
    for (const Vec3& p : sim.rs.x) near = std::min(near, length(p - throat));
    CHECKP(near < 1.5 * m, "the hook drawn back 25 mm carries the yarn in its throat (%.2f mm from it)", near * 1e3);
    CHECKP(worst < 0.15, "the yarn runs through the throat as it is carried, stretching at most %.0f%%", worst * 100);
    CHECKP(sim.hookPenetration() < 0.3 * m, "and stays out of the hook (%.2f mm in)", sim.hookPenetration() * 1e3);
}

void testSlipKnot() {
    YarnSim sim;
    sim.build(slipKnotLayout(sim.params.radius));
    Real worstGap = 1e9, worstPen = 0;
    sim.onFrame = [&] {
        worstGap = std::min(worstGap, sim.minGap());
        worstPen = std::max(worstPen, sim.hookPenetration());
    };
    makeSlipKnot(sim);
    const Real r = sim.params.radius;
    CHECKP(worstGap > 0.95 * 2 * r, "the yarn never passes through itself while the knot is made (closest %.2f mm, yarn %.2f mm)", worstGap * 1e3,
           2 * r * 1e3);
    CHECKP(worstPen < 0.4e-3, "nor through the hook (at most %.2f mm in)", worstPen * 1e3);
    const int loops = sim.loopsOnHook(Vec3{0, 0, 1});
    CHECK_MSG(std::abs(loops) == 1, "%d loops", loops);
    CHECKP(std::abs(loops) == 1, "the slip knot leaves one loop round the hook's shaft");
    CHECKP(sim.maxStrain() < 0.15, "at rest the yarn is not stretched (%.0f%%)", sim.maxStrain() * 100);
}

void testChains() {
    // A linked stretch is one loop whichever way it winds (the rig's fifth attempt at HOLD_BELOW=10 reads front -1); only the fallback uses
    // the half-plane count.
    CHECK(linkedLoopCount(true, -1) == 1);
    CHECK(linkedLoopCount(true, 2) == 1);
    CHECK(linkedLoopCount(false, -1) == -1);
    // The five-chain slip-knot run starts with these settings; three chains is the part kept in the test for runtime.
    setenv("HOLD_BELOW", "12", 1);
    setenv("YO_L", "-15", 1);
    setenv("YO_MODE", "2", 1);
    setenv("YO_ROLLMAX", "1.2", 1);
    // Let the throat ring go before the snug. With the late release the second chain's final linked stretch stays on an earlier work loop
    // (old 0% only because it is not the immediate old loop); the early release leaves the new yarn as the linked stretch.
    setenv("PU_RELEASE_LATE", "0", 1);
    YarnSim sim;
    sim.params.damping = 15;
    sim.build(slipKnotLayout(sim.params.radius));
    makeSlipKnot(sim);
    Real worstGap = 1e9, worstPen = 0;
    sim.onFrame = [&] {
        worstGap = std::min(worstGap, sim.minGap());
        worstPen = std::max(worstPen, sim.hookPenetration());
    };
    for (int k = 0; k < 3; ++k) {
        const ChainStart c = prepareChain(sim);
        CHECK_MSG(c.ok, "chain %d prepared", k + 1);
        if (!c.ok) return;
        finishChain(sim, c);
        const ChainResult r = judgeChain(sim, c);
        CHECK_MSG(r.sequential, "chain %d follows the previous stitch (link %d..%d, previous end %d)", k + 1, r.linkLo, r.linkHi, c.prevHi);
        CHECK_MSG(r.made, "chain %d made (linked %d, front %d, yarn %.0f -> %.0f mm)", k + 1, r.loops, r.frontLoops, c.length * 1e3,
                  r.length * 1e3);
        if (r.made) {
            sim.lastLinkHi = r.linkHi;
            sim.lastLinkLo = r.linkLo;
            sim.lastLinkChain = sim.chains;
        }
        CHECK_MSG(r.loops == 1, "chain %d ends with one linked loop (%d)", k + 1, r.loops);
        CHECK_MSG(r.newYarn, "chain %d's linked stretch includes the yarn drawn after the old loop", k + 1);
        CHECKP(r.linkMeanR > 0, "chain %d linked stretch radius mean/max %.1f/%.1f mm", k + 1, r.linkMeanR * 1e3, r.linkMaxR * 1e3);
        CHECK_MSG(r.length > c.length + 0.004, "chain %d draws yarn from the hand", k + 1);
    }
    const Real r = sim.params.radius;
    std::printf("chain contact: closest %.2f mm (yarn %.2f mm), hook %.2f mm in\n", worstGap * 1e3, 2 * r * 1e3, worstPen * 1e3);
    CHECKP(worstGap > 0.95 * 2 * r, "the yarn never passes through itself during the chains (closest %.2f mm, yarn %.2f mm)", worstGap * 1e3,
            2 * r * 1e3);
    CHECKP(worstPen < 0.4e-3, "nor through the hook during the chains (at most %.2f mm in)", worstPen * 1e3);
}

}  // namespace

int main() {
    testCarry();
    testSlipKnot();
    testChains();
    FINISH();
}
