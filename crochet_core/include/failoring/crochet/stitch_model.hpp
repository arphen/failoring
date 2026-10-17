#pragma once
// The stitch mechanics model (docs/CROCHET_PLAN.md section 4): (yarn, hook,
// tension, stitch type, insertion) -> the size and the homogenised stiffness
// of one stitch, and the FabricMaterial the solver needs. All SI here; the
// DSL/UI use mm, grams and cm and convert at one place (the compiler).
//
// Everything in the model is a STARTER model: the constants in
// stitch_model.cpp are order-of-magnitude tunables, NOT measurements, kept in
// one block so that section 10 swatch calibration can replace them.
//
// Decisions recorded for ticket T3.1:
//
//  - StitchMech carries compressionRatio: section 4.4's reference list has it,
//    section 4.1's listing omitted the field. Starter value 0.2 at T3.1, tuned to
//    0.1 in T5.4 (reason in stitch_model.cpp); no scaling.
//  - Fibre multipliers come from Yarn::fibreParams, filled by the yarn loader
//    (yarn.cpp) from the book's "fibres" table; stitchMech only reads
//    yarn.fibreParams and never looks a fibre up itself.
//  - Calibration is the section 10 record; nullptr means "use the starter
//    model". Semantics:
//      gauge      -> the measured sc w/h replace the section 4.2 sc formula,
//                    and the other stitch types scale by the section 4.2 type
//                    ratios. The measured values are used exactly (the
//                    measurement was made at the recorded tension, so no
//                    tension factor is applied);
//      mass       -> density = measured, and yarnLen = density * w * h /
//                    lambda_kg (section 4.3's density = yarnLen * lambda /
//                    (w * h) solved for yarnLen);
//      cantilever -> the measured bendWale/bendCourse replace the scaled ones;
//      stretch    -> the measured kWale/kCourse/lockWale/lockCourse/lockRatio
//                    replace the scaled ones;
//      hinge      -> per-construction bendScale/restAngle; filled by T9.1 and
//                    consumed by the mesh compiler, not here.
//  - MagicRing has no row in the section 4.2 table: it is treated exactly like
//    Ch (w = 0.85 w_sc, h = 0.5 h_sc, yarnLen = 0.45 L_sc), thickness 1.6 d.
//    The compiler treats "mr N st" as one ring node plus the stitches worked
//    into it, which is T4.1's job.
//  - Insertion: BLO/FLO use the base type's dims (section 4.2's last table
//    row), and so do Space and RowEnd. In stitchMech, BLO/FLO on Sc applies
//    the section 4.4 "sc BLO/FLO (the faces of that row)" multiplier row; for
//    other base types BLO/FLO uses the base type's multipliers (starter
//    assumption: the plan gives no data for that case).
//  - friction = yarn.fibreParams.friction (reference acrylic 0.5, the
//    FabricMaterial default). toFabric sets frictionStatic = friction and
//    frictionKinetic = 0.7 * friction (the ratio of the FabricMaterial
//    defaults 0.5 and 0.35). damping = 1.0 (section 4.4 R.damping), not
//    scaled. toFabric sets compressionRatio = the mech's.
//  - toFabric maps warp = wale (the repo's FabricMaterial convention):
//    tensileWarp = kWale, tensileWeft = kCourse, bendWarp = bendWale,
//    bendWeft = bendCourse. It deliberately does NOT set lockU/lockV/ratio:
//    those FabricMaterial fields do not exist yet (T1.4 adds them); the lock
//    terms stay in StitchMech and get mapped in T5.2.
//  - Ch and MagicRing have no stiffness multipliers in section 4.4; the sc row
//    (all 1.0) is the starter assumption. lockCourse also has no multiplier
//    column; it uses the lock W multiplier (starter assumption).
//  - Thickness: 1.9 d for sc/hdc/dc and for tr (a post stitch like dc; the
//    plan does not list tr), 1.6 d for sl st fabric (and Ch/MagicRing, above).

#include <map>
#include <string>

#include "failoring/crochet/yarn.hpp"
#include "failoring/material.hpp"

namespace fl {

// Section 4.1. "SlSt" is a slip stitch worked as a stitch; "MagicRing" is the
// magic ring node itself.
enum class StitchType { Ch, SlSt, Sc, Hdc, Dc, Tr, MagicRing };
// RS-relative after compile (see plan 5.5).
enum class Insertion { Both, BLO, FLO, Space, RowEnd };

// Metres; w along the row (wale direction), h up the column (course direction).
struct StitchDims {
    Real w = 0, h = 0, yarnLen = 0, thickness = 0;
};

// Homogenised, per unit width (SI).
struct StitchMech {
    Real density = 0;                        // kg/m^2
    Real kWale = 0, kCourse = 0, shear = 0;  // N/m, low-strain region
    Real lockWale = 0, lockCourse = 0, lockRatio = 0;
    Real bendWale = 0, bendCourse = 0;       // N*m (bendWale: curvature along the column)
    Real thickness = 0, friction = 0, damping = 0;
    Real compressionRatio = 0;               // 4.4 R list; the 4.1 listing omitted it
};

// The section 10 calibration record, one per (yarn, hook, stitch). The hinge
// entry is per construction ("blo", "flo", "slst", "surface", ...).
struct HingeCalibration {
    Real bendScale = 1;
    Real restAngleDeg = 0;
};
struct Calibration {
    bool gauge = false;
    Real w = 0, h = 0;  // measured sc dims (metres)
    bool mass = false;
    Real density = 0;  // kg/m^2 from the weighed swatch
    bool cantilever = false;
    Real bendWale = 0, bendCourse = 0;
    bool stretch = false;
    Real kWale = 0, kCourse = 0, lockWale = 0, lockCourse = 0, lockRatio = 0;
    std::map<std::string, HingeCalibration> hinge;  // filled by T9.1
};

// Size, mass and stiffness of one stitch. Both fall back to the starter model
// when cal is null; see the header comment for the calibration semantics.
StitchDims stitchDims(const Yarn& yarn, Real hook_m, Real tension, StitchType type, Insertion insertion, const Calibration* cal);
StitchMech stitchMech(const Yarn& yarn, Real hook_m, Real tension, StitchType type, Insertion insertion, const Calibration* cal);

// Convert a stitch's mechanics to the solver's FabricMaterial (warp = wale,
// weft = course). Deliberately does not touch lockU/lockV/ratio: those fields
// do not exist yet (T1.4 adds them); the lock terms are mapped in T5.2.
FabricMaterial toFabric(const StitchMech& mech, const std::string& id);

}  // namespace fl
