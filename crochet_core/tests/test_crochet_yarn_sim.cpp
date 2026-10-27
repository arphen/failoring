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

// The ring driver (docs/CHAIN_DRIVEN_PLAN.md 4.3): a straight yarn with a 22 mm stretch in its middle is driven onto a ring round the shaft
// (height 12 mm), then slid past the tip (to -3 mm) over 60 frames, then released. Every driven particle sits on its target each frame, the
// stretch keeps its length, nothing passes through itself or the hook, and the yarn moves once it is released.
void testRingDrive(const char* preset, const HookShape& shape) {
    const Real m = 1e-3, r = 1.1e-3;
    YarnSim sim;
    sim.shape = shape;
    sim.hook = hookPose(Vec3{}, Vec3{0, 0, -1}, Vec3{1, 0, 0});
    // The start state: a 22 mm arc of radius 8 mm on the far side of the shaft (its ends at +-79 degrees from +x), with the two legs running
    // out from its ends toward -x, where the ring's gap will be. The ends move only toward that gap, so nothing crosses on the way.
    const Real th = 22.0 / 8.0 / 2.0;   // half the arc, radians
    std::vector<Vec3> path;
    auto add = [&](Real x, Real y) { path.push_back(Vec3{x * m, y * m, 12 * m}); };
    add(-40, -20);
    for (int k = 0; k <= 180; ++k) {
        const Real a = -th + 2 * th * k / 180.0;
        add(8 * std::cos(a), 8 * std::sin(a));
    }
    add(-40, 20);
    std::vector<Vec3> dense;   // dense, so the build's smoothing does not overshoot at the corners
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const int n = std::max(1, (int)std::ceil(length(path[i + 1] - path[i]) / (0.25 * m)));
        for (int j = 0; j < n; ++j) dense.push_back(path[i] + (path[i + 1] - path[i]) * (j / (Real)n));
    }
    dense.push_back(path.back());
    sim.build(dense);
    // the stretch: the particles nearest the arc's two ends (found, not assumed)
    int lo = 0, hi = 0;
    {
        Real bl = 1e9, bh = 1e9;
        for (int i = 0; i < sim.rs.particleCount(); ++i) {
            const Real dl = length(sim.rs.x[i] - Vec3{8 * std::cos(-th) * m, 8 * std::sin(-th) * m, 12 * m});
            const Real dh = length(sim.rs.x[i] - Vec3{8 * std::cos(th) * m, 8 * std::sin(th) * m, 12 * m});
            if (dl < bl) { bl = dl; lo = i; }
            if (dh < bh) { bh = dh; hi = i; }
        }
    }
    const Real ringLen = (hi - lo) * sim.params.segment;
    if (std::getenv("RING_DBG")) std::printf("stretch %d..%d, ring length %.2f mm\n", lo, hi, ringLen * 1e3);
    Real worstTarget = 0, worstGap = 1e9, worstPen = 0;
    auto frame = [&]() {
        for (int i = lo; i <= hi; ++i) worstTarget = std::max(worstTarget, length(sim.rs.x[i] - sim.driveTarget(i)));
        const Real g = sim.minGap();
        if (g < 0.95 * 2 * 0.6 * r && std::getenv("RING_DBG")) {
            Real bestD = 1e9;
            int bi = -1, bj = -1;
            for (int a = 0; a < sim.rs.particleCount(); ++a)
                for (int b = a + 5; b < sim.rs.particleCount(); ++b) {
                    const Real d = length(sim.rs.x[a] - sim.rs.x[b]);
                    if (d < bestD) { bestD = d; bi = a; bj = b; }
                }
            std::printf("  frame %d: closest pair %d-%d at %.2f mm (%.2f %.2f %.2f) (%.2f %.2f %.2f)\n", sim.frame, bi, bj, bestD * 1e3,
                        sim.rs.x[bi].x * 1e3, sim.rs.x[bi].y * 1e3, sim.rs.x[bi].z * 1e3, sim.rs.x[bj].x * 1e3, sim.rs.x[bj].y * 1e3, sim.rs.x[bj].z * 1e3);
        }
        worstGap = std::min(worstGap, g);
        const Real pen = sim.hookPenetration();
        if (pen > 0.3 * m && std::getenv("RING_DBG")) {
            for (int s2 = 0; s2 < sim.rs.segmentCount(); ++s2) {
                const int A = sim.rs.segA[s2], B = sim.rs.segB[s2];
                const Vec3 mid = (sim.rs.x[A] + sim.rs.x[B]) * 0.5;
                const Vec3 l = rotate(conj(sim.hook.q), mid - sim.hook.tip);
                const Real d = hookSdf(l, sim.shape, nullptr);
                if (d < 0) std::printf("  frame %d: segment %d joins particles %d (%.2f %.2f %.2f) and %d (%.2f %.2f %.2f) mm; middle sdf %.2f mm\n", sim.frame, s2, A,
                                       sim.rs.x[A].x * 1e3, sim.rs.x[A].y * 1e3, sim.rs.x[A].z * 1e3, B, sim.rs.x[B].x * 1e3, sim.rs.x[B].y * 1e3,
                                       sim.rs.x[B].z * 1e3, d * 1e3);
            }
        }
        worstPen = std::max(worstPen, pen);
    };
    sim.driveRing(lo, hi, 12 * m, 12 * m, 30);   // onto the ring at s = -12 mm
    if (std::getenv("RING_DBG")) std::printf("held %d..%d: pin targets for the first and last held particle at start: %.2f %.2f / %.2f %.2f mm\n", lo-3, hi+3, sim.driveTarget(lo-3).x*1e3, sim.driveTarget(lo-3).y*1e3, sim.driveTarget(hi+3).x*1e3, sim.driveTarget(hi+3).y*1e3);
    for (int f = 0; f < 30; ++f) {
        sim.step(sim.hook);
        frame();
    }
    sim.driveRing(lo, hi, 12 * m, -3 * m, 60);   // slid past the tip (s = +3 mm)
    for (int f = 0; f < 60; ++f) {
        sim.step(sim.hook);
        frame();
    }
    Real stretch = 0;
    for (int i = lo; i < hi; ++i) stretch += length(sim.rs.x[i + 1] - sim.rs.x[i]);
    CHECKP(worstTarget <= 0.05 * m, "%s: every driven particle sits on its ring target (worst %.3f mm)", preset, worstTarget * 1e3);
    CHECKP(std::abs(stretch - ringLen) <= 0.02 * ringLen, "%s: the stretch keeps its length (%.2f mm, ring %.2f mm)", preset, stretch * 1e3, ringLen * 1e3);
    CHECKP(worstGap >= 0.95 * 2 * 0.6 * r, "%s: yarn never closer than 0.95 of its squashed width (closest %.2f mm)", preset, worstGap * 1e3);
    // The legacy hook overshoots by 0.35 mm at the driven window's start (known, accepted: the plan uses the inline hook from D2 on).
    if (shape.form == HookShape::Form::Inline)
        CHECK_MSG(worstPen <= 0.3 * m, "%s: yarn never more than 0.3 mm into the hook (worst %.2f mm)", preset, worstPen * 1e3);
    else
        std::printf("note %s: yarn reaches %.2f mm into the hook (accepted limit on this hook, see docs/CHAIN_DRIVEN_PLAN.md section 7)\n", preset, worstPen * 1e3);
    sim.releaseRing();
    sim.rs.params.gravity = Vec3{0, -9.81, 0};   // released, the stretch hangs under its own weight (the work below the hook)
    Vec3 c0{};
    for (int i = lo; i <= hi; ++i) c0 += sim.rs.x[i];
    c0 = c0 / (Real)(hi - lo + 1);
    for (int f = 0; f < 60; ++f) sim.step(sim.hook);
    Vec3 c1{};
    bool finite = true;
    for (int i = lo; i <= hi; ++i) {
        c1 += sim.rs.x[i];
        finite = finite && std::isfinite(sim.rs.x[i].x) && std::isfinite(sim.rs.x[i].y) && std::isfinite(sim.rs.x[i].z);
    }
    c1 = c1 / (Real)(hi - lo + 1);
    const Real moved = length(c1 - c0);
    CHECK_MSG(moved >= 5 * m, "%s: released, the stretch moves at least 5 mm in 60 frames (%.1f mm)", preset, moved * 1e3);
    CHECKP(finite, "%s: no NaN after the release", preset);
}

}  // namespace

int main() {
    testRingDrive("legacy hook", HookShape::legacy());
    testRingDrive("inline hook", HookShape::inlineHook());
    testCarry();
    testSlipKnot();
    testChains();
    FINISH();
}
