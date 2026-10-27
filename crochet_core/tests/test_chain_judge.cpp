// The chain judge's fixtures (docs/CHAIN_PLAN.md section 4.5.1): analytic yarn, no simulation. The true stitch must pass every column; each
// negative must fail the column it is built to fail. The judge is frozen after Phase 0b: a failure here means the fixture or the judge is wrong,
// and the fixture is changed only to match the geometry the plan describes.
#include <cstdio>
#include <string>

#include "check.hpp"
#include "failoring/crochet/yarn_sim.hpp"

using namespace fl;

namespace {

// Whether the verdict passes the named column.
bool column(const StitchVerdict& v, const std::string& name) {
    if (name == "through") return v.throughValid && v.throughCount >= 1;
    if (name == "oldOff") return std::fabs(v.oldOff) < 0.25;
    if (name == "onShaft") return v.onShaftPass;
    if (name == "contact") return v.contactPass;
    if (name == "fresh") return v.fresh < 0.25;
    return true;
}

// A 22 mm loop round the shaft at height z (mm), both legs running down to the work (y = -10 mm), on the given hook. The loop closes at its
// lowest point: its two legs leave the circle 2r apart. Returns the yarn in the hook's frame.
void buildLoop(YarnSim& sim, Real z, const HookShape& shape) {
    sim.shape = shape;
    sim.hook = hookPose(Vec3{}, Vec3{0, 0, -1}, Vec3{1, 0, 0});
    const Real m = 1e-3, legX = 1.1, rL = 3.9;   // rL: 22.3 mm of loop
    const Real yTop = -std::sqrt(rL * rL - legX * legX);
    std::vector<Vec3> path;
    auto add = [&](Real x, Real y, Real zz) { path.push_back(Vec3{x, y, zz} * m); };
    add(-legX, -10.0, z);
    add(-legX, yTop, z);
    const Real phi0 = std::atan2(yTop, -legX), phiE = std::atan2(yTop, legX);
    const Real arc = phi0 - phiE + 2 * kPi;
    const int n = 40;
    for (int k = 1; k <= n; ++k) add(rL * std::cos(phi0 - arc * k / n), rL * std::sin(phi0 - arc * k / n), z);
    add(legX, -10.0, z);
    // dense, so the build's Catmull-Rom does not overshoot at the corners
    std::vector<Vec3> dense;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const Real k = std::max(1.0, std::ceil(length(path[i + 1] - path[i]) / (0.25 * m)));
        for (int j = 0; j < (int)k; ++j) dense.push_back(path[i] + (path[i + 1] - path[i]) * (j / k));
    }
    dense.push_back(path.back());
    sim.build(dense);
}

}  // namespace

int main() {
    // The true stitch: every column passes.
    {
        StitchFixture f = buildStitchFixture("true");
        CHECK_MSG(f.chain.ok, "the true fixture is built");
        const StitchVerdict v = judgeStitch(f.sim, f.chain);
        CHECK_MSG(v.onHook, "the true stitch's new loop is found on the hook (lo %d hi %d)", v.linkLo, v.linkHi);
        CHECK_MSG(v.pass, "the true stitch passes (%s)", f.note.c_str());
    }
    // Each negative: the judge fails, and the column it is built to trip is among the failing columns.
    const struct {
        const char* name;
        const char* column;
    } negatives[] = {
        {"beside", "through"},     {"beside-far", "through"}, {"old-on-shaft", "oldOff"}, {"neck", "onShaft"},
        {"loose", "onShaft"},      {"contact", "contact"},    {"fresh", "fresh"},
    };
    for (const auto& n : negatives) {
        StitchFixture f = buildStitchFixture(n.name);
        CHECK_MSG(f.chain.ok, "the %s fixture is built", n.name);
        const StitchVerdict v = judgeStitch(f.sim, f.chain);
        CHECK_MSG(!v.pass, "%s fails (%s)", n.name, f.note.c_str());
        CHECK_MSG(!column(v, n.column), "%s fails on %s", n.name, n.column);
    }
    // The loop finder on each preset: a 22 mm loop round the shaft at z = 12 mm and z = 6 mm, both legs down. hookLoop must find it, and the
    // half-plane count (the work's side, +y, where no leg goes) must read one loop.
    for (const char* preset : {"legacy", "inline"}) {
        for (Real z : {12.0, 6.0}) {
            YarnSim sim;
            buildLoop(sim, z, std::string(preset) == "inline" ? HookShape::inlineHook() : HookShape::legacy());
            int lo = -1, hi = -1;
            const bool found = hookLoop(sim, &lo, &hi);
            const int front = sim.loopsOnHook(Vec3{0, 1, 0});
            CHECK_MSG(found && hi > lo, "%s hook, z %.0f mm: hookLoop finds the loop (yarn %d..%d)", preset, z, lo, hi);
            CHECK_MSG(front == 1, "%s hook, z %.0f mm: loopsOnHook(+y) reads one loop (%d)", preset, z, front);
            std::printf("loop finder: %s hook, z %.0f mm: hookLoop %d..%d, loopsOnHook(+y) %d\n", preset, z, lo, hi, front);
        }
    }
    FINISH();
}
