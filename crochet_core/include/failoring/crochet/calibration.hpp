#pragma once
// From four kitchen-table measurements to a stitch calibration record (docs/CROCHET_PLAN.md section 10, ticket T9.1). The record is the
// `Calibration` of stitch_model.hpp; the stitch model uses it instead of the starter values for the parts that were measured.
//
//  1. Gauge:      stitches and rows in 10 cm            -> w = 0.1 / stitches, h = 0.1 / rows (metres)
//  2. Weight:     the swatch's mass and its size        -> density = mass / area (kg/m^2), the size given in cm, or as stitch and row
//                 counts (then the gauge from step 1 gives the area)
//  3. Cantilever: the overhang L at the 41.5 degree line -> bending length c = L / 2, B = density * g * c^3 (N*m); the strip worked
//                 with its long axis along the wale gives bendWale, along the course bendCourse (needs the density of step 2)
//  4. Stretch:    a strip hung from a clip bar across its full width with two weights W1 < W2 and the lengths L1, L2 (L0 unloaded):
//                 line tension T = W g / width (N/m), strain e = (L - L0) / L0, k0 = T1 / e1, the second slope (T2 - T1) / (e2 - e1);
//                 the J-curve locks at e1 when the second slope is at least kLockSteeper times k0 ("clearly steeper"), with
//                 lockRatio = second slope / k0. Otherwise no lock was reached: the lock strain is left at 1 (never reached) and the
//                 notes say to use a heavier W2. The two directions share one lockRatio (the mean of those that locked).
//
// Each step is independent: a step without its numbers is simply not in the record (the starter model stays for that part), a step
// with impossible numbers (zero, negative, W2 <= W1, ...) is an error that names it. The hinge entries come from the hinge fit
// (T9.2), not from here.
#include <string>
#include <vector>

#include "failoring/crochet/stitch_model.hpp"
#include "failoring/crochet/yarn.hpp"

namespace fl {

struct StretchMeasurement {
    Real widthCm = 10;   // the strip's width, the load acts across all of it
    Real l0Mm = 100;     // the marked length, unloaded
    Real w1G = 0, w2G = 0;    // the two weights (g)
    Real l1Mm = 0, l2Mm = 0;  // the marked length under each
    bool given() const { return w1G > 0 || l1Mm > 0; }
};

struct SwatchMeasurements {
    // 1 gauge, 10 cm
    Real stitchesPer10cm = 0, rowsPer10cm = 0;
    // 2 weight: grams, and the swatch's size in cm or its stitch and row counts
    Real massG = 0;
    Real widthCm = 0, heightCm = 0;
    int stitches = 0, rows = 0;
    // 3 cantilever overhang (mm), the strip's long axis along the wale / the course
    Real overhangWaleMm = 0, overhangCourseMm = 0;
    // 4 stretch
    StretchMeasurement wale, course;
};

struct CalibrationFit {
    Calibration cal;
    std::vector<std::string> notes;   // what the fit did: the numbers it got, steps that were left out, "no lock reached ..."
    std::vector<std::string> errors;  // steps whose numbers cannot be right; those steps are not in `cal`
    bool lockReachedWale = false, lockReachedCourse = false;
    bool any() const { return cal.gauge || cal.mass || cal.cantilever || cal.stretch; }
};

constexpr Real kLockSteeper = 1.5;  // the second slope must be this many times the first to count as a lock

CalibrationFit fitCalibration(const SwatchMeasurements& m);

}  // namespace fl
