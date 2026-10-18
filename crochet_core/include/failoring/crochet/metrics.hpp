#pragma once
// The result numbers of a crochet run (docs/CROCHET_PLAN.md section 7.5, ticket T6.3), all in one struct with a JSON
// writer. Every key of section 7.5 is always written; a number that does not apply to the run (no load, no strap) is
// written as null, never as a silent 0.
//
// Decisions recorded for T6.3:
//  - Sizes come from the zero-gravity relaxed shape (flat size) and the settled hanging shape (drop, height, gape).
//  - drop_cm = highest point (the strap apex held by the pin or resting on the peg) to the lowest point.
//  - opening_gape_cm = extent of the mouth rim vertices along the horizontal axis of least extent (front rim centre to
//    back rim centre for a flat pouch, the short axis of an oval mouth).
//  - strap_length_*: the centre line through the row centroids of the strap, rest = sum of the strap rows' heights.
//  - base_sag_mm: the largest distance of the base round vertices (the rounds up to and including the last increase
//    round) from their best-fit plane. corner_angle_deg: 180 deg minus the mean turning angle between the winding
//    normals of the faces below and above the base-to-wall row (the edges between the last increase round and the next):
//    flat 180, a crisp box corner about 90, a rounded slump about 150. Null for models with no base.
//  - strain: |F e_u| - 1 (wale) and |F e_v| - 1 (course) per triangle from the deformation gradient against its rest shape;
//    max and 95th percentile of the positive part over all triangles, with the row and stitch of the worst triangle.
//  - load_tilt_deg is 0 in stage A (translation only, plan 7.3); load_top_above_rim_cm is negative when the load's top is
//    inside the mouth; max_contact_pressure_kPa is the largest vertex rigid-contact pressure.
#include <string>
#include <vector>

#include "failoring/crochet/layout.hpp"
#include "failoring/crochet/model.hpp"
#include "failoring/crochet/scene.hpp"
#include "failoring/crochet/yarn.hpp"
#include "failoring/math.hpp"

namespace fl {

struct MetricsInput {
    const CrochetModel* model = nullptr;
    const StitchGraph* graph = nullptr;
    const LayoutReport* layout = nullptr;
    const Yarn* yarn = nullptr;
    std::vector<Vec3> relaxed;   // zero-g relaxed positions
    std::vector<Vec3> hanging;   // settled hanging, no load (may be empty)
    std::vector<Vec3> loaded;    // settled with the load (empty = no load)
    int baseEndRow = -1;         // last row of the base; -1 = the last increase round of the revolve region
    // solver health, summed over the runs
    long long frames = 0, newtonIterations = 0, iterCapHits = 0, lineSearchFailures = 0, substeps = 0;
    Real finalKineticEnergy = 0;
    // load
    bool hasLoad = false;
    LoadSpec load;
    FitResult fit;
    Vec3 loadCenter;
    Real maxContactPressure = 0;  // Pa
};

struct Metrics {
    // yarn
    double yarn_m = 0, mass_g = 0, skeins = 0;
    // finished flat size (relaxed)
    double flat_width_cm = 0, flat_depth_cm = 0, flat_height_cm = 0;
    // hanging (null when there is no hanging state)
    bool hasHang = false;
    double drop_cm = 0, bag_height_cm = 0, opening_gape_cm = 0;
    bool hasStrap = false;
    double strap_length_rest_cm = 0, strap_length_loaded_cm = 0, strap_elongation_pct = 0;
    // base
    bool hasBase = false;
    double base_sag_mm = 0, corner_angle_deg = 0;
    // strain of the last state
    double strain_wale_max = 0, strain_wale_p95 = 0, strain_course_max = 0, strain_course_p95 = 0;
    int worst_wale_row = -1, worst_wale_stitch = -1, worst_course_row = -1, worst_course_stitch = -1;
    // load
    bool hasLoad = false;
    double load_tilt_deg = 0, load_top_above_rim_cm = 0, max_contact_pressure_kPa = 0;
    bool fits = true, tight = false;
    std::string fit_message;
    // solver health
    long long frames = 0, newton_iterations = 0, cap_hits = 0, line_search_failures = 0;
    double final_kinetic_energy = 0;

    std::string toJson() const;
};

Metrics computeMetrics(const MetricsInput& in);

}  // namespace fl
