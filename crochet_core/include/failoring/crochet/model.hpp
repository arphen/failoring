#pragma once
// From the mesh topology to something the solver can take (docs/CROCHET_PLAN.md
// sections 6.5-6.7, ticket T5.2): per-triangle materials, hinge modifiers and
// point seams, packed into a fl::IntrinsicMesh. Vertex positions (x0/layoutUV)
// come from the layout stage (T5.3) and are left empty here.
//
// Decisions recorded for T5.2:
//
//  - Materials. A triangle's material is toFabric(stitchMech(type, loopRs)) of
//    its source stitch with the J-curve lock terms mapped in (lockStrainWarp =
//    lockWale, lockStrainWeft = lockCourse, lockStiffnessRatio = lockRatio; the
//    TriC u axis is warp = wale, plan 2.3). Materials are deduplicated by
//    (type, insertion, density, surface-stiffening).
//  - Mass (plan 6.10 test 6). The plan's nominal density yarnLen*lambda/(w*h)
//    would lose mass wherever a stitch's triangles cover less than w*h (an
//    increase triangle is half a stitch), so each stitch's triangles get the
//    density that makes them hold exactly the stitch's yarn: M_s / sum(area of
//    the triangles it owns), M_s = yarnLen_s * lambda. A stitch that owns no
//    triangle (the first stitch of a row, dropped nodes) puts M_s into
//    IntrinsicMesh::extraMass: on its own vertex if it is a node, else on its
//    dropTo vertex (plan 6.1). The sum over the solver's vertex masses is then
//    the yarn model's total (every stitch's yarnLen) up to rounding.
//  - Hinge modifiers (plan 4.4 table, 6.6), on the course edges of the row that
//    was worked into (= the base edges of the polygons of the worked stitches):
//      blo   bendScale 0.30, rest +25 deg toward the WS
//      flo   bendScale 0.35, rest -25 deg (toward the RS)
//      slst  bendScale 1.50 on the base edges and the top edges of the sl st
//            polygons (multiplies with blo/flo when the sl sts are worked blo)
//      surface sl st: bendScale 0.70 on the worked-into edge, and the two faces
//            next to it get kCourse x3 and lockCourse x0.5.
//    The table's angle signs and sizes are UNVERIFIED guesses (plan 4.4); a
//    Calibration::hinge entry named "blo", "flo", "slst" or "surface" replaces
//    them (restAngleDeg is RS-relative: positive = toward the WS).
//    Sign into the solver: the mesh's normals point to the RS and a positive
//    solver rest angle bends toward the common normal side (BendC::rest), so a
//    WS fold is a NEGATIVE solver angle: kWsFoldSign = -1. The simulation test
//    of plan 6.10 test 5 (T5.4) measures it.
//  - Point seams: one per PointSeam (plan 6.7), share = the seam stitch's width
//    w_s (m), on the mesh vertices of the two stitches (a dropped endpoint uses
//    its dropTo vertex).
#include <string>
#include <vector>

#include "failoring/crochet/mesh.hpp"
#include "failoring/crochet/stitch_model.hpp"
#include "failoring/crochet/yarn.hpp"
#include "failoring/intrinsic_mesh.hpp"
#include "failoring/material.hpp"
#include "failoring/math.hpp"

namespace fl {

struct ModelOptions {
    Real hook_m = 5.0e-3;                    // hook diameter, metres
    Real tension = 1.0;                      // 0.9 tight .. 1.1 loose
    const Calibration* calibration = nullptr;
};

// One point seam, on mesh vertex ids (addPointSeam(a, b, share) on the solver,
// once the panel's first vertex is added to a and b).
struct SeamRequest {
    int a = -1, b = -1;
    Real share = 0;  // metres
};

struct CrochetModel {
    CrochetMesh topo;                   // the T5.1 output
    IntrinsicMesh mesh;                 // tris, triRest, triMaterial, hingeMods, extraMass; x0/layoutUV empty (T5.3)
    std::vector<FabricMaterial> materials;
    std::vector<std::string> materialNames;  // parallel to materials, e.g. "sc/both"
    std::vector<SeamRequest> pointSeams;
    Real yarnMass = 0;                  // kg, sum of every stitch's yarnLen * lambda
    std::vector<std::string> warnings;  // topo.warnings plus the model's own
};

// The sign that turns a plan (RS-relative, positive = toward the WS) rest angle
// into ClothSolver's BendC::rest for a mesh whose normals point to the RS.
constexpr Real kWsFoldSign = -1;

CrochetModel buildCrochetModel(const StitchGraph& g, const Yarn& yarn, const ModelOptions& opt);

}  // namespace fl
