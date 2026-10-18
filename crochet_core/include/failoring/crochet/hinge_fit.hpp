#pragma once
// Fitting a hinge's bendScale from a droop measurement (docs/CROCHET_PLAN.md section 10.5, ticket T9.2).
//
// The swatch is `rowsBelow` rows of single crochet, the hinge row (worked BLO, FLO or as a row of sl sts) and `rowsAbove` more rows, all
// `cols` stitches wide. The user lays it free on a table and measures the angle the upper part settles at (the rest angle, toward the
// WS), then clamps the lower half flat with the hinge at the table edge and measures how far the upper half droops. The simulator runs
// the same setup: the sheet lies flat with the right side up and the rows along +x, the lower half (the foundation chain and the first
// `rowsBelow` rows) is pinned, gravity acts, the model relaxes to equilibrium, and the droop is read off.
//
// Droop = the angle below the horizontal (degrees) of the line from the centre of the last clamped row to the centre of the last row
// (the chord of the upper half). A hinge that folds toward the WS (a positive rest angle) folds down here, so the droop is at least
// the fold; the softer the hinge, the larger the droop. The fit is therefore a 1D bisection on log(bendScale): the droop falls as
// bendScale grows. It reuses the solver, so the result is consistent with the simulation.
#include <string>
#include <vector>

#include "failoring/crochet/calibration.hpp"
#include "failoring/crochet/graph.hpp"
#include "failoring/crochet/yarn.hpp"

namespace fl {

// The swatch as a stitch graph (a foundation chain of cols + 1, then the rows, turned, each stitch worked into the one below it).
// construction: "blo" or "flo" (the hinge row's loop) or "slst" (the hinge row is a row of sl sts worked BLO).
StitchGraph makeHingeSwatch(int cols, int rowsBelow, int rowsAbove, const std::string& construction);

struct HingeSetup {
    std::string construction = "blo";    // also the name of the Calibration::hinge entry that is fitted
    int cols = 10, rowsBelow = 10, rowsAbove = 10;
    Real hookMm = 5.0;
    Real tension = 1.0;
    const Calibration* fabric = nullptr; // the measured fabric (gauge, weight, cantilever, stretch) of the same yarn and hook; null = starter
    int maxFrames = 6000;                // per run (it stops as soon as the droop has stopped changing)
};

// One run: the droop (degrees) at equilibrium for a hinge of this bendScale and rest angle (degrees, positive toward the WS).
// `converged` (optional) tells whether the run settled before maxFrames.
Real simulateDroop(const HingeSetup& setup, const Yarn& yarn, Real bendScale, Real restAngleDeg, bool* converged = nullptr);

struct HingeFit {
    Real bendScale = 1;
    Real droopDeg = 0;      // what the simulation gives for that bendScale
    int runs = 0;           // simulator runs used
    bool bracketed = false; // the measured droop lies between what the softest and the stiffest hinge give
    std::vector<std::string> notes;
};

// Find the bendScale whose simulated droop matches the measured one (within tolDeg, or after maxRuns bisection runs). When the measurement
// is outside what any bendScale in [kMinBendScale, kMaxBendScale] gives, the nearest end is returned with bracketed = false and a note.
constexpr Real kMinBendScale = 0.01, kMaxBendScale = 3.0;
// The droop is much less sensitive to bendScale for a stiff hinge (about 6 deg between bendScale 0.3 and 1 on the starter fabric, 25 deg
// between 0.1 and 0.3): a measured droop that close to the stiff end pins bendScale less well, and the notes say so.
HingeFit fitHingeBendScale(const HingeSetup& setup, const Yarn& yarn, Real restAngleDeg, Real measuredDroopDeg, Real tolDeg = 0.02, int maxRuns = 14);

}  // namespace fl
