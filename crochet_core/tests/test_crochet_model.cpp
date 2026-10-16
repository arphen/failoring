// The stitch mechanics model (docs/CROCHET_PLAN.md section 4): section 4.2
// gauge sanity for worsted sc, section 4.3 density sanity, monotonicity with
// hook / yarn thickness / fibre, the toFabric warp = wale mapping, every
// stitch type x insertion combination, and the section 10 calibration record.
#include <cmath>
#include <cstdio>

#include "check.hpp"
#include "failoring/crochet/stitch_model.hpp"

using namespace fl;

// CHECK plus a PASS line carrying the measured number, so ctest output is
// readable when the test fails on one of the bounds.
#define CHECKP(cond, ...)                                 \
    do {                                                  \
        CHECK(cond);                                      \
        if (cond) {                                       \
            std::printf("PASS ");                         \
            std::printf(__VA_ARGS__);                     \
            std::printf("\n");                            \
        }                                                 \
    } while (0)

int main() {
    std::printf("[stitch model] %s\n", FL_YARNS_PATH);
    const YarnBook book = loadYarnsFromFile(FL_YARNS_PATH);
    const Yarn* acrylic = book.find("acrylic_worsted");
    CHECK(acrylic != nullptr);
    if (acrylic == nullptr) {
        std::printf("cannot run the model without acrylic_worsted\n");
        FINISH();
    }
    const Real hook = 5.0e-3;   // 5.0 mm
    const Real hook4 = 4.0e-3;  // 4.0 mm

    // (a) Section 4.2 sanity: acrylic worsted on a 5 mm hook gives worsted sc
    // gauge, 14-16 sts and 16-18 rows per 10 cm.
    const StitchDims sc = stitchDims(*acrylic, hook, 1.0, StitchType::Sc, Insertion::Both, nullptr);
    const Real wmm = sc.w * 1e3;
    const Real hmm = sc.h * 1e3;
    const Real sts10 = 100.0 / wmm;   // stitches per 10 cm
    const Real rows10 = 100.0 / hmm;  // rows per 10 cm
    CHECKP(wmm >= 6.0 && wmm <= 7.0, "(a) sc w = %.4f mm in [6.0, 7.0]", wmm);
    CHECKP(hmm >= 5.5 && hmm <= 6.5, "(a) sc h = %.4f mm in [5.5, 6.5]", hmm);
    CHECKP(sts10 >= 14.0 && sts10 <= 16.0, "(a) sc gauge = %.2f sts/10cm in [14, 16]", sts10);
    CHECKP(rows10 >= 16.0 && rows10 <= 18.0, "(a) sc gauge = %.2f rows/10cm in [16, 18]", rows10);
    std::printf("  sc dims: w %.6f m, h %.6f m, yarnLen %.6f m, thickness %.6f m\n", sc.w, sc.h, sc.yarnLen, sc.thickness);

    // (b) Section 4.3 sanity: areal density about 0.42 kg/m^2, so a 10 x 10 cm
    // swatch weighs about 4.2 g.
    const StitchMech scMe = stitchMech(*acrylic, hook, 1.0, StitchType::Sc, Insertion::Both, nullptr);
    CHECKP(std::fabs(scMe.density - 0.42) <= 0.03, "(b) density = %.4f kg/m^2 in 0.42 +/- 0.03", scMe.density);
    const Real swatchG = scMe.density * (0.1 * 0.1) * 1000.0;  // 10 x 10 cm in grams
    CHECKP(swatchG >= 3.9 && swatchG <= 4.5, "(b) 10x10 cm swatch weight = %.3f g (in 4.2 +/- 0.3)", swatchG);

    // Section 10 mass calibration: density = measured, yarnLen = density * w * h / lambda.
    Calibration calMass;
    calMass.mass = true;
    calMass.density = 0.42;
    const StitchDims scMassDim = stitchDims(*acrylic, hook, 1.0, StitchType::Sc, Insertion::Both, &calMass);
    const StitchMech scMassMe = stitchMech(*acrylic, hook, 1.0, StitchType::Sc, Insertion::Both, &calMass);
    const Real wantYarnLen = 0.42 * scMassDim.w * scMassDim.h / (acrylic->lambda() * 1e-3);
    CHECKP(std::fabs(scMassMe.density - 0.42) < 1e-15 && std::fabs(scMassDim.yarnLen - wantYarnLen) < 1e-15,
           "(b2) mass calibration: density = %.4f kg/m^2, yarnLen = %.6f m", scMassMe.density, scMassDim.yarnLen);

    // (c) Smaller hook, same yarn: smaller gauge, denser and stiffer, locks sooner.
    const StitchDims sc4 = stitchDims(*acrylic, hook4, 1.0, StitchType::Sc, Insertion::Both, nullptr);
    const StitchMech sc4Me = stitchMech(*acrylic, hook4, 1.0, StitchType::Sc, Insertion::Both, nullptr);
    CHECKP(sc4.w < sc.w && sc4.h < sc.h, "(c) hook 4.0 vs 5.0 mm: w %.4f < %.4f mm, h %.4f < %.4f mm", sc4.w * 1e3,
           sc.w * 1e3, sc4.h * 1e3, sc.h * 1e3);
    CHECKP(sc4Me.density > scMe.density, "(c) density %.4f > %.4f kg/m^2", sc4Me.density, scMe.density);
    CHECKP(sc4Me.bendWale > scMe.bendWale, "(c) bendWale %.6e > %.6e N*m", sc4Me.bendWale, scMe.bendWale);
    CHECKP(sc4Me.lockWale < scMe.lockWale, "(c) lockWale %.4f < %.4f", sc4Me.lockWale, scMe.lockWale);

    // (d) Thicker yarn on the same hook: stiffer in tension, bending and shear,
    // and locks sooner.
    Yarn thick = *acrylic;
    thick.diameterMm = 3.5;  // vs 2.5
    const StitchMech thickMe = stitchMech(thick, hook, 1.0, StitchType::Sc, Insertion::Both, nullptr);
    CHECKP(thickMe.kWale > scMe.kWale, "(d) d 3.5 vs 2.5 mm: kWale %.2f > %.2f N/m", thickMe.kWale, scMe.kWale);
    CHECKP(thickMe.bendWale > scMe.bendWale, "(d) bendWale %.6e > %.6e N*m", thickMe.bendWale, scMe.bendWale);
    CHECKP(thickMe.shear > scMe.shear, "(d) shear %.2f > %.2f N/m", thickMe.shear, scMe.shear);
    CHECKP(thickMe.lockWale < scMe.lockWale, "(d) lockWale %.4f < %.4f", thickMe.lockWale, scMe.lockWale);

    // (e) Fibre ordering: cotton bends more stiffly than acrylic, acrylic more
    // than wool (the yarns.json "bend" multipliers 1.6 / 1.0 / 0.8).
    const YarnFibre* fbCotton = book.fibre("cotton");
    const YarnFibre* fbAcrylic = book.fibre("acrylic");
    const YarnFibre* fbWool = book.fibre("wool");
    CHECK(fbCotton != nullptr && fbAcrylic != nullptr && fbWool != nullptr);
    if (fbCotton == nullptr || fbAcrylic == nullptr || fbWool == nullptr) {
        std::printf("cannot run the fibre ordering without cotton, acrylic and wool\n");
        FINISH();
    }
    Yarn yCotton = *acrylic;
    yCotton.fibreParams = *fbCotton;
    Yarn yAcrylic = *acrylic;
    yAcrylic.fibreParams = *fbAcrylic;
    Yarn yWool = *acrylic;
    yWool.fibreParams = *fbWool;
    const Real bendCotton = stitchMech(yCotton, hook, 1.0, StitchType::Sc, Insertion::Both, nullptr).bendWale;
    const Real bendAcrylic = stitchMech(yAcrylic, hook, 1.0, StitchType::Sc, Insertion::Both, nullptr).bendWale;
    const Real bendWool = stitchMech(yWool, hook, 1.0, StitchType::Sc, Insertion::Both, nullptr).bendWale;
    CHECKP(bendCotton > bendAcrylic && bendAcrylic > bendWool, "(e) bendWale cotton %.6e > acrylic %.6e > wool %.6e N*m",
           bendCotton, bendAcrylic, bendWool);

    // (f) toFabric maps warp = wale, weft = course, and the scalar fields.
    const FabricMaterial fab = toFabric(scMe, "sc_acrylic_5mm");
    CHECKP(fab.tensileWarp == scMe.kWale && fab.tensileWeft == scMe.kCourse,
           "(f) tensileWarp == kWale = %.2f N/m, tensileWeft == kCourse = %.2f N/m", fab.tensileWarp, fab.tensileWeft);
    CHECKP(fab.bendWarp == scMe.bendWale && fab.bendWeft == scMe.bendCourse,
           "(f) bendWarp == bendWale = %.6e N*m, bendWeft == bendCourse = %.6e N*m", fab.bendWarp, fab.bendWeft);
    CHECKP(fab.shear == scMe.shear && fab.density == scMe.density && fab.thickness == scMe.thickness,
           "(f) shear %.2f N/m, density %.4f kg/m^2, thickness %.6f m", fab.shear, fab.density, fab.thickness);
    CHECKP(fab.compressionRatio == scMe.compressionRatio, "(f) compressionRatio %.2f == mech", fab.compressionRatio);
    CHECKP(fab.frictionStatic == scMe.friction && fab.frictionKinetic == 0.7 * scMe.friction,
           "(f) frictionStatic %.2f == mech.friction, frictionKinetic %.3f == 0.7 * friction", fab.frictionStatic,
           fab.frictionKinetic);
    CHECKP(fab.damping == scMe.damping, "(f) damping %.2f 1/s == mech", fab.damping);

    // (g) All 7 types x 5 insertions: every dim and mech value finite and > 0.
    const StitchType types[] = {StitchType::Ch,  StitchType::SlSt, StitchType::Sc, StitchType::Hdc,
                                StitchType::Dc,  StitchType::Tr,   StitchType::MagicRing};
    const Insertion insertions[] = {Insertion::Both, Insertion::BLO, Insertion::FLO, Insertion::Space, Insertion::RowEnd};
    int combos = 0;
    bool allPositive = true;
    for (const StitchType t : types) {
        for (const Insertion in : insertions) {
            const StitchDims d = stitchDims(*acrylic, hook, 1.0, t, in, nullptr);
            const StitchMech m = stitchMech(*acrylic, hook, 1.0, t, in, nullptr);
            const Real vals[] = {d.w,      d.h,        d.yarnLen,  d.thickness,          m.density,          m.kWale,
                                 m.kCourse, m.shear,    m.lockWale, m.lockCourse,         m.lockRatio,        m.bendWale,
                                 m.bendCourse, m.thickness, m.friction, m.damping,        m.compressionRatio};
            for (const Real v : vals) allPositive = allPositive && std::isfinite(v) && v > 0.0;
            ++combos;
        }
    }
    CHECKP(allPositive && combos == 35, "(g) %d type x insertion combos: all dims and mech values finite and > 0", combos);

    // (h) BLO/FLO dims equal Both dims (section 4.2's last table row); the sc
    // BLO/FLO stiffness row is section 4.4's "faces of that row".
    const StitchDims scBloDim = stitchDims(*acrylic, hook, 1.0, StitchType::Sc, Insertion::BLO, nullptr);
    const StitchDims scFloDim = stitchDims(*acrylic, hook, 1.0, StitchType::Sc, Insertion::FLO, nullptr);
    const bool bloSame = scBloDim.w == sc.w && scBloDim.h == sc.h && scBloDim.yarnLen == sc.yarnLen && scBloDim.thickness == sc.thickness;
    const bool floSame = scFloDim.w == sc.w && scFloDim.h == sc.h && scFloDim.yarnLen == sc.yarnLen && scFloDim.thickness == sc.thickness;
    CHECKP(bloSame && floSame, "(h) sc BLO/FLO dims == Both (w %.4f mm, h %.4f mm, yarnLen %.6f m)", scBloDim.w * 1e3,
           scBloDim.h * 1e3, scBloDim.yarnLen);
    const StitchMech scBloMe = stitchMech(*acrylic, hook, 1.0, StitchType::Sc, Insertion::BLO, nullptr);
    CHECKP(scBloMe.kWale == 0.60 * scMe.kWale && scBloMe.bendWale == 0.50 * scMe.bendWale && scBloMe.shear == 0.90 * scMe.shear,
           "(h2) sc BLO mech: kWale = 0.60x = %.2f N/m, bendWale = 0.50x = %.6e N*m, shear = 0.90x = %.2f N/m", scBloMe.kWale,
           scBloMe.bendWale, scBloMe.shear);

    // (i) Section 10 gauge override: the measured sc dims are used exactly and
    // the other types scale by the section 4.2 ratios.
    Calibration calGauge;
    calGauge.gauge = true;
    calGauge.w = 0.1 / 15.4;  // 100 mm / 15.4 sts
    calGauge.h = 0.1 / 16.7;  // 100 mm / 16.7 rows
    const StitchDims scGauge = stitchDims(*acrylic, hook, 1.0, StitchType::Sc, Insertion::Both, &calGauge);
    const StitchDims dcGauge = stitchDims(*acrylic, hook, 1.0, StitchType::Dc, Insertion::Both, &calGauge);
    CHECKP(std::fabs(scGauge.w - calGauge.w) < 1e-12 && std::fabs(scGauge.h - calGauge.h) < 1e-12,
           "(i) gauge override: sc w = %.9f m (%.4f mm), h = %.9f m (%.4f mm)", scGauge.w, scGauge.w * 1e3, scGauge.h,
           scGauge.h * 1e3);
    CHECKP(std::fabs(dcGauge.w - calGauge.w) < 1e-12 && std::fabs(dcGauge.h - 1.90 * calGauge.h) < 1e-12,
           "(i) gauge override: dc w = sc w, dc h = 1.90 * sc h = %.9f m", dcGauge.h);

    // Section 10 cantilever and stretch overrides replace the scaled values and
    // leave the others alone.
    Calibration calBend;
    calBend.cantilever = true;
    calBend.bendWale = 1.23e-4;
    calBend.bendCourse = 4.56e-5;
    const StitchMech bendMe = stitchMech(*acrylic, hook, 1.0, StitchType::Sc, Insertion::Both, &calBend);
    CHECKP(bendMe.bendWale == 1.23e-4 && bendMe.bendCourse == 4.56e-5 && bendMe.kWale == scMe.kWale,
           "(i2) cantilever calibration: bendWale = %.6e, bendCourse = %.6e N*m, kWale kept %.2f N/m", bendMe.bendWale,
           bendMe.bendCourse, bendMe.kWale);
    Calibration calStretch;
    calStretch.stretch = true;
    calStretch.kWale = 111.0;
    calStretch.kCourse = 222.0;
    calStretch.lockWale = 0.05;
    calStretch.lockCourse = 0.06;
    calStretch.lockRatio = 12.0;
    const StitchMech strMe = stitchMech(*acrylic, hook, 1.0, StitchType::Sc, Insertion::Both, &calStretch);
    CHECKP(strMe.kWale == 111.0 && strMe.kCourse == 222.0 && strMe.lockWale == 0.05 && strMe.lockCourse == 0.06 &&
               strMe.lockRatio == 12.0 && strMe.bendWale == scMe.bendWale,
           "(i3) stretch calibration: kWale = %.0f, kCourse = %.0f N/m, lockWale = %.2f, lockCourse = %.2f, ratio %.0f, bend kept",
           strMe.kWale, strMe.kCourse, strMe.lockWale, strMe.lockCourse, strMe.lockRatio);

    // (j) Tension scales w and h only, never the yarn per stitch.
    const StitchDims tight = stitchDims(*acrylic, hook, 0.9, StitchType::Sc, Insertion::Both, nullptr);
    const StitchDims loose = stitchDims(*acrylic, hook, 1.1, StitchType::Sc, Insertion::Both, nullptr);
    CHECKP(tight.w < loose.w && tight.h < loose.h, "(j) tension 0.9 vs 1.1: w %.4f < %.4f mm, h %.4f < %.4f mm",
           tight.w * 1e3, loose.w * 1e3, tight.h * 1e3, loose.h * 1e3);
    CHECKP(tight.yarnLen == loose.yarnLen, "(j) yarnLen unchanged by tension: %.6f m", tight.yarnLen);

    // (k) The loader fills fibreParams from the book when a yarn is loaded.
    const Yarn* cottonWorsted = book.find("cotton_worsted");
    CHECKP(cottonWorsted != nullptr && std::fabs(cottonWorsted->fibreParams.bend - 1.6) < 1e-12,
           "(k) cotton_worsted fibreParams.bend = %.2f (cotton)", cottonWorsted ? cottonWorsted->fibreParams.bend : -1.0);
    CHECKP(acrylic->fibreParams.tensile == 1.0 && acrylic->fibreParams.friction == 0.5,
           "(k) acrylic_worsted fibreParams: tensile %.2f, friction %.2f", acrylic->fibreParams.tensile,
           acrylic->fibreParams.friction);

    FINISH();
}
