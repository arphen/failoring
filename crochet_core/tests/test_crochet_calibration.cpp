// The swatch calibration of docs/CROCHET_PLAN.md section 10 (T9.1): four kitchen-table measurements -> a record, and the stitch model uses
// the record instead of the starter values.
#include <cmath>
#include <cstdio>
#include <string>

#include "check.hpp"
#include "failoring/crochet/calibration.hpp"
#include "failoring/crochet/model.hpp"
#include "failoring/crochet/pattern.hpp"

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

static bool near(double a, double b, double rel = 1e-9) { return std::fabs(a - b) <= rel * std::fabs(b) + 1e-15; }
static bool hasNote(const std::vector<std::string>& v, const char* needle) {
    for (const std::string& s : v)
        if (s.find(needle) != std::string::npos) return true;
    return false;
}

int main() {
    std::printf("[crochet calibration]\n");
    const YarnBook book = loadYarnsFromFile(FL_YARNS_PATH);
    const Yarn* yarn = book.find("acrylic_worsted");
    CHECK(yarn != nullptr);
    if (yarn == nullptr) FINISH();

    // the example numbers: 14 stitches and 16 rows in 10 cm; a 12 x 12 cm swatch of 9.0 g; overhangs 52 mm (wale) and 38 mm (course);
    // stretch strips 10 cm wide, L0 100 mm, 250 g and 1 kg: the wale strip stretches 4 mm and 8 mm (locks), the course strip 10 mm and 50 mm
    SwatchMeasurements m;
    m.stitchesPer10cm = 14;
    m.rowsPer10cm = 16;
    m.massG = 9.0;
    m.widthCm = 12;
    m.heightCm = 12;
    m.overhangWaleMm = 52;
    m.overhangCourseMm = 38;
    m.wale = {10, 100, 250, 1000, 104, 108};
    m.course = {10, 100, 250, 1000, 110, 150};
    const CalibrationFit fit = fitCalibration(m);
    const Calibration& c = fit.cal;
    CHECKP(fit.errors.empty() && c.gauge && c.mass && c.cantilever && c.stretch, "all four steps in the record (%zu errors)", fit.errors.size());
    CHECKP(near(c.w, 0.1 / 14) && near(c.h, 0.1 / 16), "gauge: w = %.3f mm, h = %.3f mm", c.w * 1e3, c.h * 1e3);
    CHECKP(near(c.density, 0.009 / 0.0144), "weight: density %.4f kg/m^2 (9 g on 144 cm^2)", c.density);
    const double bw = c.density * 9.81 * std::pow(0.026, 3), bc = c.density * 9.81 * std::pow(0.019, 3);
    CHECKP(near(c.bendWale, bw) && near(c.bendCourse, bc) && c.bendWale > c.bendCourse, "cantilever: bendWale %.3g, bendCourse %.3g N*m (c = L / 2)", c.bendWale, c.bendCourse);
    const double t1 = 0.25 * 9.81 / 0.10, t2 = 1.0 * 9.81 / 0.10;
    CHECKP(near(c.kWale, t1 / 0.04) && near(c.kCourse, t1 / 0.10), "stretch: kWale %.1f, kCourse %.1f N/m (T1 / e1)", c.kWale, c.kCourse);
    const double ratioWale = ((t2 - t1) / 0.04) / (t1 / 0.04);
    CHECKP(fit.lockReachedWale && near(c.lockWale, 0.04) && near(c.lockRatio, ratioWale) && ratioWale > 2.9 && ratioWale < 3.1,
           "stretch: the wale strip locks at strain %.3f, second slope %.2f x k0", c.lockWale, c.lockRatio);
    CHECKP(!fit.lockReachedCourse && near(c.lockCourse, 1.0) && hasNote(fit.notes, "no lock reached, use a heavier W2"),
           "stretch: the course strip reaches no lock (lock strain left at %.1f), and the notes say so", c.lockCourse);

    // the stitch model uses the record, for the parts that were measured
    const double hook = 5.0e-3;
    const StitchDims d0 = stitchDims(*yarn, hook, 1.0, StitchType::Sc, Insertion::Both, nullptr);
    const StitchDims d1 = stitchDims(*yarn, hook, 1.0, StitchType::Sc, Insertion::Both, &c);
    const StitchMech m0 = stitchMech(*yarn, hook, 1.0, StitchType::Sc, Insertion::Both, nullptr);
    const StitchMech m1 = stitchMech(*yarn, hook, 1.0, StitchType::Sc, Insertion::Both, &c);
    CHECKP(near(d1.w, c.w) && near(d1.h, c.h) && !near(d1.w, d0.w, 1e-3), "stitchDims: sc is the measured %.3f x %.3f mm (starter %.3f x %.3f)", d1.w * 1e3, d1.h * 1e3, d0.w * 1e3, d0.h * 1e3);
    CHECKP(near(m1.density, c.density) && !near(m1.density, m0.density, 1e-3), "stitchMech: density %.4f (starter %.4f)", m1.density, m0.density);
    CHECKP(near(m1.bendWale, c.bendWale) && near(m1.bendCourse, c.bendCourse) && near(m1.kWale, c.kWale) && near(m1.kCourse, c.kCourse) && near(m1.lockWale, c.lockWale) &&
               near(m1.lockRatio, c.lockRatio),
           "stitchMech: bending, stiffness and lock terms are the measured ones (kWale %.1f vs starter %.1f)", m1.kWale, m0.kWale);

    // and the whole model: the pouch with the record weighs and stiffens differently
    const StitchGraph g = importPatternFile(std::string(FL_PATTERNS_DIR) + "/cp/exported/pouch.json", std::string(FL_PATTERNS_DIR) + "/cp/pouch.txt");
    ModelOptions plain, cal;
    plain.hook_m = cal.hook_m = hook;
    cal.calibration = &c;
    const CrochetModel a = buildCrochetModel(g, *yarn, plain), b = buildCrochetModel(g, *yarn, cal);
    CHECKP(a.mesh.tris.size() == b.mesh.tris.size() && !near(a.yarnMass, b.yarnMass, 1e-3) && !near(a.materials[0].density, b.materials[0].density, 1e-3),
           "pouch model: yarn %.2f g with the record, %.2f g without; same %zu triangles", b.yarnMass * 1e3, a.yarnMass * 1e3, b.mesh.tris.size());

    // steps are independent; impossible numbers are named
    SwatchMeasurements g1;
    g1.stitchesPer10cm = 14;
    g1.rowsPer10cm = 16;
    const CalibrationFit f1 = fitCalibration(g1);
    CHECKP(f1.errors.empty() && f1.cal.gauge && !f1.cal.mass && !f1.cal.cantilever && !f1.cal.stretch, "gauge alone is a record with only the gauge");
    SwatchMeasurements bad;
    bad.stitchesPer10cm = 14;  // no rows
    bad.massG = 9;             // no size
    bad.overhangWaleMm = 50;   // no density, no course strip
    bad.wale = {10, 100, 250, 1000, 96, 100};  // shorter than L0
    const CalibrationFit f2 = fitCalibration(bad);
    CHECKP(!f2.any() && f2.errors.size() == 4 && hasNote(f2.errors, "gauge: needs both") && hasNote(f2.errors, "weight: needs the swatch's size") &&
               hasNote(f2.errors, "cantilever: B = density") && hasNote(f2.errors, "stretch (wale)"),
           "impossible numbers: four errors that name the step (%zu), nothing in the record", f2.errors.size());
    SwatchMeasurements counts;
    counts.stitchesPer10cm = 14;
    counts.rowsPer10cm = 16;
    counts.massG = 3.0;
    counts.stitches = 28;
    counts.rows = 32;  // 20 cm x 20 cm by the gauge
    const CalibrationFit f3 = fitCalibration(counts);
    CHECKP(f3.cal.mass && near(f3.cal.density, 0.003 / (28 * (0.1 / 14) * 32 * (0.1 / 16))), "the weighed swatch given by its counts: density %.4f kg/m^2 (area from the gauge)", f3.cal.density);
    SwatchMeasurements counts2 = counts;
    counts2.stitchesPer10cm = 0;
    counts2.rowsPer10cm = 0;
    const CalibrationFit f4 = fitCalibration(counts2);
    CHECKP(!f4.cal.mass && hasNote(f4.errors, "needs the gauge of step 1"), "counts without the gauge: an error that says why");
    SwatchMeasurements one;
    one.wale = {10, 100, 250, 0, 104, 0};
    one.course = {10, 100, 250, 0, 110, 0};
    const CalibrationFit f5 = fitCalibration(one);
    CHECKP(f5.cal.stretch && near(f5.cal.kWale, t1 / 0.04) && !f5.lockReachedWale && hasNote(f5.notes, "use a heavier W2"), "one weight only: k0 and a note to use a heavier W2");
    SwatchMeasurements half;
    half.wale = {10, 100, 250, 1000, 104, 108};
    const CalibrationFit f7 = fitCalibration(half);
    CHECKP(!f7.cal.stretch && hasNote(f7.errors, "stretch: needs both directions"), "only the wale strip: an error that asks for the course strip");
    const CalibrationFit f6 = fitCalibration(SwatchMeasurements());
    CHECKP(!f6.any() && f6.errors.empty(), "nothing measured: an empty record without errors");

    FINISH();
}
