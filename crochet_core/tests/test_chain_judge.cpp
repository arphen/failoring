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
    FINISH();
}
