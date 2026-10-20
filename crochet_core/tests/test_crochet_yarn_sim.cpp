// The process simulator (docs/YARN_MODE_PLAN.md section 14): the hook catches a yarn and carries it, and the slip knot made by moving the hook ends
// as a loop round the hook's shaft with the yarn never passing through itself or through the hook.
#include <cmath>
#include <cstdio>
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

}  // namespace

int main() {
    testCarry();
    testSlipKnot();
    FINISH();
}
