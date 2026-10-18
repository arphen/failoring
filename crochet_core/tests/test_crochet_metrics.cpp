// The result numbers of section 7.5 (docs/CROCHET_PLAN.md, ticket T6.3): every key of the JSON, the corner angle of a hand-built
// 90 degree fold, the strain of a stretched strap against the J-curve, and the metrics of a hung pouch.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "check.hpp"
#include "failoring/crochet/metrics.hpp"
#include "failoring/crochet/pattern.hpp"
#include "failoring/crochet/relax.hpp"
#include "failoring/crochet/scene.hpp"

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

static StitchGraph loadFixture(const std::string& name) {
    return importPatternFile(std::string(FL_PATTERNS_DIR) + "/cp/exported/" + name + ".json", std::string(FL_PATTERNS_DIR) + "/cp/" + name + ".txt");
}

static int addRow(StitchGraph& g, const char* kind, bool turned) {
    Row r;
    r.id = static_cast<int>(g.rows.size());
    r.piece = "main";
    r.kind = kind;
    r.turned = turned;
    g.rows.push_back(r);
    return r.id;
}
static int addSt(StitchGraph& g, const char* type, int row, std::vector<int> targets, const char* loop = nullptr) {
    Stitch s;
    s.id = static_cast<int>(g.st.size());
    s.type = type;
    s.piece = "main";
    s.row = row;
    for (const Stitch& q : g.st) s.index += q.row == row;
    s.index += 1;
    s.loop = loop != nullptr ? loop : (std::string(type) == "ch" ? "space" : "both");
    s.loopRs = s.loop;
    s.targets = std::move(targets);
    s.prev = g.st.empty() ? -1 : static_cast<int>(g.st.size()) - 1;
    g.st.push_back(s);
    return s.id;
}
static StitchGraph makeSwatch(int cols, int rows, int loopRow = 0, const char* loop = "both") {
    StitchGraph g;
    const int r0 = addRow(g, "row", true);
    for (int i = 0; i <= cols; ++i) addSt(g, "ch", r0, {});
    std::vector<int> prevRow;
    for (int c = cols - 1; c >= 0; --c) prevRow.push_back(c);
    for (int r = 1; r <= rows; ++r) {
        const int row = addRow(g, "row", true);
        std::vector<int> cur;
        if (r > 1) addSt(g, "ch", row, {});
        for (int c = 0; c < cols; ++c) cur.push_back(addSt(g, "sc", row, {r == 1 ? prevRow[c] : prevRow[cols - 1 - c]}, r == loopRow ? loop : "both"));
        prevRow = cur;
    }
    resolveFacing(g);
    return g;
}

static bool hasKey(const std::string& json, const char* key) { return json.find(std::string("\"") + key + "\":") != std::string::npos; }

int main() {
    std::printf("[crochet metrics] %s\n", FL_PATTERNS_DIR);
    const YarnBook book = loadYarnsFromFile(FL_YARNS_PATH);
    const Yarn* acrylic = book.find("acrylic_worsted");
    CHECK(acrylic != nullptr);
    if (acrylic == nullptr) FINISH();
    const Real hook = 5e-3;

    // --- a hand-built 90 degree fold reads 90 +- 1 (section 7.5: corner_angle_deg)
    {
        StitchGraph g = makeSwatch(10, 20, 11, "blo");
        Calibration cal;
        cal.hinge["blo"] = {0.3, 90.0};  // the BLO row folds 90 degrees toward the WS
        ModelOptions mo;
        mo.hook_m = hook;
        mo.calibration = &cal;
        CrochetModel cm = buildCrochetModel(g, *acrylic, mo);
        const LayoutReport layout = layoutModel(cm, g);
        RelaxOptions ro;
        for (const Stitch& s : g.st)
            if (s.row <= 10 && cm.topo.stitchVertex[s.id] >= 0) ro.pin.push_back(cm.topo.stitchVertex[s.id]);
        const RelaxResult rr = relaxModel(cm, ro);
        MetricsInput in;
        in.model = &cm;
        in.graph = &g;
        in.layout = &layout;
        in.yarn = acrylic;
        in.relaxed = cm.mesh.x0;
        in.baseEndRow = 10;
        const Metrics m = computeMetrics(in);
        CHECKP(rr.converged && m.hasBase && std::fabs(m.corner_angle_deg - 90.0) < 1.0, "hand-built 90 deg fold (BLO hinge, rest 90 deg): corner angle %.2f deg (want 90 +- 1), relaxed in %d frames", m.corner_angle_deg,
               rr.frames);
        // and a flat swatch reads 180
        StitchGraph flatG = makeSwatch(10, 20);
        CrochetModel flat = buildCrochetModel(flatG, *acrylic, mo);
        const LayoutReport flatLayout = layoutModel(flat, flatG);
        MetricsInput fin;
        fin.model = &flat;
        fin.graph = &flatG;
        fin.layout = &flatLayout;
        fin.yarn = acrylic;
        fin.relaxed = flat.mesh.x0;
        fin.baseEndRow = 10;
        const Metrics fm = computeMetrics(fin);
        CHECKP(std::fabs(fm.corner_angle_deg - 180.0) < 1e-6 && fm.base_sag_mm < 1e-9, "flat swatch: corner angle %.4f deg, base sag %.1e mm", fm.corner_angle_deg, fm.base_sag_mm);
    }

    // --- the strain of a stretched strap: the stiff branch at 1 kg on 5 x 60 sc (as in test_crochet_scene)
    {
        StitchGraph g = makeSwatch(5, 60);
        ModelOptions mo;
        mo.hook_m = hook;
        CrochetModel cm = buildCrochetModel(g, *acrylic, mo);
        const LayoutReport layout = layoutModel(cm, g);
        std::vector<int> top, bottom;
        for (const Stitch& s : g.st) {
            const int v = cm.topo.stitchVertex[s.id];
            if (v < 0) continue;
            if (s.row == 0) top.push_back(v);
            if (s.row == 60) bottom.push_back(v);
        }
        for (const int v : bottom) cm.mesh.extraMass[v] += 1.0 / bottom.size();
        SolverParams sp;
        sp.newtonIterations = 30;
        std::unique_ptr<ClothSolver> solver = makeSolver(cm, sp);
        for (const int v : top) solver->pinVertex(v);
        int calm = 0;
        for (int f = 0; f < 2000 && calm < 30; ++f) {
            solver->step();
            calm = solver->kineticEnergy() < 1e-10 ? calm + 1 : 0;
        }
        MetricsInput in;
        in.model = &cm;
        in.graph = &g;
        in.layout = &layout;
        in.yarn = acrylic;
        in.relaxed = solver->positions();
        const Metrics m = computeMetrics(in);
        const StitchMech mech = stitchMech(*acrylic, hook, 1.0, StitchType::Sc, Insertion::Both, nullptr);
        const Real T = 9.81 / (4 * cm.dims[6].w);  // 1 kg over the 4 cells of mesh width
        const Real eps = mech.lockWale + (T - mech.kWale * mech.lockWale) / (mech.lockRatio * mech.kWale);
        std::printf("   strap strain: wale max %.4f (row %d), p95 %.4f; J-curve %.4f\n", m.strain_wale_max, m.worst_wale_row, m.strain_wale_p95, eps);
        // the 95th percentile is the bulk of the strap; the maximum sits in the bottom row, where the load enters at 5 points
        CHECKP(std::fabs(m.strain_wale_p95 / eps - 1) < 0.05 && m.strain_wale_max >= m.strain_wale_p95 && m.worst_wale_row >= 55,
               "stretched strap: wale strain p95 %.4f vs the J-curve %.4f (%.1f %%), max %.4f in row %d (the bottom), course strain max %.4f", m.strain_wale_p95, eps,
               100 * (m.strain_wale_p95 / eps - 1), m.strain_wale_max, m.worst_wale_row, m.strain_course_max);
    }

    // --- the pouch: relaxed, hung by the pin, every key present
    {
        StitchGraph g = loadFixture("pouch");
        ModelOptions mo;
        mo.hook_m = hook;
        CrochetModel cm = buildCrochetModel(g, *acrylic, mo);
        const LayoutReport layout = layoutModel(cm, g);
        relaxModel(cm, RelaxOptions());
        const std::vector<Vec3> relaxed = cm.mesh.x0;
        CrochetScene scene(cm, g, layout, SceneOptions());
        const SceneStats st = scene.settle();
        MetricsInput in;
        in.model = &cm;
        in.graph = &g;
        in.layout = &layout;
        in.yarn = acrylic;
        in.relaxed = relaxed;
        in.hanging = scene.solver().positions();
        in.frames = st.frames;
        in.newtonIterations = st.newtonIterations;
        in.iterCapHits = st.iterCapHits;
        in.substeps = st.substeps;
        in.lineSearchFailures = scene.solver().contactDiag().lsFail;
        in.finalKineticEnergy = st.kineticEnergy;
        const Metrics m = computeMetrics(in);
        const std::string json = m.toJson();
        const char* keys[] = {"yarn_m", "mass_g", "skeins", "flat_width_cm", "flat_depth_cm", "flat_height_cm", "drop_cm", "bag_height_cm", "opening_gape_cm", "strap_length_rest_cm",
                              "strap_length_loaded_cm", "strap_elongation_pct", "base_sag_mm", "corner_angle_deg", "strain_wale_max", "strain_wale_p95", "strain_course_max",
                              "strain_course_p95", "worst_wale_strain_at", "worst_course_strain_at", "load_tilt_deg", "load_top_above_rim_cm", "max_contact_pressure_kPa", "fits", "tight_fit",
                              "fit_message", "frames", "newton_iterations", "cap_hits", "line_search_failures", "final_kinetic_energy"};
        int missing = 0;
        for (const char* k : keys) missing += !hasKey(json, k);
        std::printf("%s", json.c_str());
        CHECKP(missing == 0, "the JSON has all %zu keys of section 7.5 (%d missing)", sizeof keys / sizeof keys[0], missing);
        // numbers that do not apply are null, not 0: the pouch has a strap but no round base and no load
        CHECKP(json.find("\"corner_angle_deg\": null") != std::string::npos && json.find("\"fits\": null") != std::string::npos && json.find("\"drop_cm\": null") == std::string::npos,
               "not applicable -> null (no base, no load), applicable -> a number");
        Real yarn = 0;
        for (const Stitch& s : g.st) yarn += cm.dims[s.id].yarnLen;
        CHECKP(std::fabs(m.yarn_m / yarn - 1) < 1e-9 && std::fabs(m.mass_g - cm.yarnMass * 1e3) < 1e-9 && std::fabs(m.skeins * acrylic->metres / m.yarn_m - 1) < 1e-9,
               "pouch: %.1f m of yarn, %.2f g, %.3f skeins (%.0f m each)", m.yarn_m, m.mass_g, m.skeins, acrylic->metres);
        CHECKP(m.drop_cm > 33 && m.drop_cm < 45 && m.bag_height_cm > 12 && m.bag_height_cm < 17, "pouch hanging: drop %.1f cm, bag height %.1f cm", m.drop_cm, m.bag_height_cm);
        // 79 row steps x 6.00 mm
        CHECKP(std::fabs(m.strap_length_rest_cm - 47.4) < 0.05 && m.strap_elongation_pct > 0 && m.strap_elongation_pct < 10,
               "pouch strap: rest %.1f cm, loaded %.1f cm, elongation %.2f %%", m.strap_length_rest_cm, m.strap_length_loaded_cm, m.strap_elongation_pct);
        CHECKP(m.opening_gape_cm >= 0 && m.opening_gape_cm < 5 && m.flat_width_cm > 8 && m.flat_width_cm < 12,
               "pouch: opening gape %.2f cm, finished flat size %.1f x %.1f x %.1f cm (w x d x h)", m.opening_gape_cm, m.flat_width_cm, m.flat_depth_cm, m.flat_height_cm);
        CHECKP(m.strain_wale_max > 0 && m.strain_wale_max < 0.3 && m.strain_course_max < 0.3 && m.strain_wale_p95 <= m.strain_wale_max,
               "pouch strain: wale max %.3f (p95 %.3f) at row %d, course max %.3f (p95 %.3f) at row %d", m.strain_wale_max, m.strain_wale_p95, m.worst_wale_row, m.strain_course_max,
               m.strain_course_p95, m.worst_course_row);
    }
    FINISH();
}
