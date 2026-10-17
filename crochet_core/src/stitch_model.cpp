#include "failoring/crochet/stitch_model.hpp"

#include <cmath>

namespace fl {
namespace {

// ---------------------------------------------------------------------------
// STARTER CONSTANTS (UNVERIFIED). Section 4.2 (geometry), 4.3 (mass) and 4.4
// (stiffness) of docs/CROCHET_PLAN.md. Every number here is an
// order-of-magnitude tunable, not a measurement; a section 10 swatch
// calibration record replaces the ones it measures. The relative effects
// (smaller hook -> denser and stiffer, thicker yarn -> stiffer) are the part
// to trust.
// ---------------------------------------------------------------------------

// Section 4.2, sc (the reference stitch), H = hook diameter, d = yarn
// diameter, T = tension factor, all in metres:
//   w_sc = T (1.00 H + 0.60 d)   h_sc = T (0.90 H + 0.60 d)
//   L_sc = 1.30 pi (H + d)
constexpr Real kScW = 1.00;
constexpr Real kScH = 0.90;
constexpr Real kScDia = 0.60;
constexpr Real kScYarn = 1.30;

// Section 4.2 stitch-type ratios relative to sc (w, h, yarn per stitch).
struct DimRatios {
    Real w, h, yarn;
};

DimRatios dimRatios(StitchType t) {
    switch (t) {
        case StitchType::Sc: return {1.00, 1.00, 1.00};
        case StitchType::Hdc: return {1.00, 1.45, 1.35};
        case StitchType::Dc: return {1.00, 1.90, 1.75};
        case StitchType::Tr: return {1.00, 2.50, 2.20};
        case StitchType::SlSt: return {1.00, 0.35, 0.60};
        case StitchType::Ch: return {0.85, 0.50, 0.45};
        case StitchType::MagicRing: return {0.85, 0.50, 0.45};  // == Ch (no table row; see the header)
    }
    return {1.00, 1.00, 1.00};
}

// Section 4.2 thickness: 1.9 d for sc/hdc/dc (and tr, a post stitch like dc),
// 1.6 d for sl st fabric (and Ch/MagicRing).
Real thicknessMult(StitchType t) {
    switch (t) {
        case StitchType::SlSt:
        case StitchType::Ch:
        case StitchType::MagicRing: return 1.6;
        default: return 1.9;
    }
}

// Section 4.4 reference values, acrylic worsted sc on a 5 mm hook.
constexpr Real kRkW = 400.0;         // N/m
constexpr Real kRkC = 300.0;         // N/m
constexpr Real kRShear = 60.0;       // N/m
constexpr Real kRLockW = 0.15;       // strain
constexpr Real kRLockC = 0.25;       // strain
constexpr Real kRLockRatio = 30.0;   // stiffness above lock / below
constexpr Real kRBendW = 3.0e-4;     // N*m
constexpr Real kRBendC = 2.5e-4;     // N*m
constexpr Real kRDamping = 1.0;      // 1/s, not scaled
// 4.4's R list says 0.2; T5.4 tuned it to 0.1 (4.1's struct listing omitted the field). With 0.2 the plan's
// 6.10 test 2 circles fail: +6 sc per round at the model's w/h = 1.083 has about 3 % more rim than the
// radial stitch heights allow, and with a compression stiffness of 0.2 k the sheet buckles to a ruffle
// (+-10 % of the radius) from any 0.3 mm perturbation. At 0.07..0.15 the +6 circle stays flat (0.0 %) for
// every seed and perturbation size, +5 is a 43 % deep cup and +8 a strong ruffle. UNVERIFIED like all of 4.4:
// the swatch calibration (section 10) replaces it.
constexpr Real kRCompression = 0.1;
constexpr Real kTau0 = 0.5;          // reference tightness d/H
constexpr Real kD0 = 2.5e-3;         // m, reference yarn diameter

// Section 4.4 stitch-type multipliers relative to sc. lock applies to both
// lockWale and lockCourse: the table has a lock column for the wale only, and
// the course reuses it (starter assumption).
struct MultRow {
    Real kW, kC, lock, bendW, bendC, shear;
};

MultRow typeMult(StitchType t, Insertion in) {
    // Section 4.4 "sc BLO/FLO (the faces of that row)". Other base types have
    // no BLO/FLO row in the plan, so they keep their base multipliers
    // (starter assumption); Space and RowEnd do too.
    if (t == StitchType::Sc && (in == Insertion::BLO || in == Insertion::FLO)) return {0.60, 1.00, 1.30, 0.50, 1.00, 0.90};
    switch (t) {
        case StitchType::Sc: return {1.00, 1.00, 1.00, 1.00, 1.00, 1.00};
        case StitchType::Hdc: return {0.70, 0.95, 1.30, 0.70, 0.90, 0.80};
        case StitchType::Dc: return {0.50, 0.90, 1.60, 0.50, 0.85, 0.60};
        case StitchType::Tr: return {0.35, 0.85, 1.90, 0.35, 0.80, 0.45};
        case StitchType::SlSt: return {2.00, 1.60, 0.40, 1.40, 1.30, 1.50};
        case StitchType::Ch:
        case StitchType::MagicRing: return {1.00, 1.00, 1.00, 1.00, 1.00, 1.00};  // no table row; sc row (starter assumption)
    }
    return {1.00, 1.00, 1.00, 1.00, 1.00, 1.00};
}

// Geometry + mass of one stitch. Insertion does not enter: BLO/FLO/Space/
// RowEnd all use the base type's dims (section 4.2's last table row).
StitchDims dimsOf(const Yarn& yarn, Real hook_m, Real tension, StitchType type, const Calibration* cal) {
    const Real d = yarn.diameterMm * 1e-3;  // m
    const Real T = tension;
    Real w0, h0, L0;
    if (cal != nullptr && cal->gauge) {
        // Section 10 gauge: the measured sc dims are used exactly (they were
        // measured at the recorded tension, so T does not scale them again).
        w0 = cal->w;
        h0 = cal->h;
        L0 = kScYarn * kPi * (hook_m + d);
    } else {
        // Section 4.2: T multiplies w and h; yarn per stitch has no T.
        w0 = T * (kScW * hook_m + kScDia * d);
        h0 = T * (kScH * hook_m + kScDia * d);
        L0 = kScYarn * kPi * (hook_m + d);
    }
    const DimRatios r = dimRatios(type);
    StitchDims out;
    out.w = r.w * w0;
    out.h = r.h * h0;
    out.yarnLen = r.yarn * L0;
    out.thickness = thicknessMult(type) * d;
    if (cal != nullptr && cal->mass) {
        // Section 4.3 solved for yarnLen: yarnLen = density * w * h / lambda.
        out.yarnLen = cal->density * out.w * out.h / (yarn.lambda() * 1e-3);
    }
    return out;
}

}  // namespace

StitchDims stitchDims(const Yarn& yarn, Real hook_m, Real tension, StitchType type, Insertion insertion, const Calibration* cal) {
    (void)insertion;  // insertions share the base type's dims (section 4.2)
    return dimsOf(yarn, hook_m, tension, type, cal);
}

StitchMech stitchMech(const Yarn& yarn, Real hook_m, Real tension, StitchType type, Insertion insertion, const Calibration* cal) {
    const StitchDims dims = dimsOf(yarn, hook_m, tension, type, cal);
    const Real d = yarn.diameterMm * 1e-3;  // m
    const Real T = tension;
    const Real tauRatio = (d / hook_m) / kTau0;  // tau / tau0
    const Real invTauRatio = kTau0 / (d / hook_m);
    const Real dRatio = d / kD0;  // d / d0
    const YarnFibre& fb = yarn.fibreParams;

    // Section 4.4 scaling of the reference values.
    const Real bendW = kRBendW * fb.bend * dRatio * dRatio * dRatio * tauRatio * tauRatio / (T * T);
    const Real bendC = kRBendC * fb.bend * dRatio * dRatio * dRatio * tauRatio * tauRatio / (T * T);
    const Real kW = kRkW * fb.tensile * dRatio * tauRatio * tauRatio * tauRatio / (T * T * T);
    const Real kC = kRkC * fb.tensile * dRatio * tauRatio * tauRatio * tauRatio / (T * T * T);
    const Real shear = kRShear * fb.tensile * tauRatio * tauRatio;
    const Real lockW = kRLockW * fb.lock * invTauRatio * T;
    const Real lockC = kRLockC * fb.lock * invTauRatio * T;

    const MultRow mul = typeMult(type, insertion);

    StitchMech m;
    m.density = (cal != nullptr && cal->mass) ? cal->density : dims.yarnLen * (yarn.lambda() * 1e-3) / (dims.w * dims.h);
    m.kWale = mul.kW * kW;
    m.kCourse = mul.kC * kC;
    m.shear = mul.shear * shear;
    m.lockWale = mul.lock * lockW;
    m.lockCourse = mul.lock * lockC;
    m.lockRatio = kRLockRatio;
    m.bendWale = mul.bendW * bendW;
    m.bendCourse = mul.bendC * bendC;
    m.thickness = dims.thickness;
    m.friction = fb.friction;
    m.damping = kRDamping;
    m.compressionRatio = kRCompression;

    if (cal != nullptr && cal->cantilever) {
        m.bendWale = cal->bendWale;
        m.bendCourse = cal->bendCourse;
    }
    if (cal != nullptr && cal->stretch) {
        m.kWale = cal->kWale;
        m.kCourse = cal->kCourse;
        m.lockWale = cal->lockWale;
        m.lockCourse = cal->lockCourse;
        m.lockRatio = cal->lockRatio;
    }
    return m;
}

FabricMaterial toFabric(const StitchMech& mech, const std::string& id) {
    FabricMaterial f;
    f.id = id;
    f.name = id;
    f.category = "knit";  // crochet is a looped fabric, like a knit
    f.notes = "crochet stitch, STARTER model (UNVERIFIED): docs/CROCHET_PLAN.md section 4; calibrate with section 10";

    f.density = mech.density;
    f.thickness = mech.thickness;
    f.tensileWarp = mech.kWale;    // warp = wale (FabricMaterial convention)
    f.tensileWeft = mech.kCourse;  // weft = course
    f.shear = mech.shear;
    f.bendWarp = mech.bendWale;
    f.bendWeft = mech.bendCourse;
    f.compressionRatio = mech.compressionRatio;
    f.frictionStatic = mech.friction;
    f.frictionKinetic = 0.7 * mech.friction;  // ratio of the default 0.5 / 0.35
    f.damping = mech.damping;
    return f;
}

}  // namespace fl
