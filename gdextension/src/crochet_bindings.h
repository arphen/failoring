// Godot-facing wrappers around crochet_core (docs/CROCHET_PLAN.md section 8.1, ticket T7.1): CrochetPattern (compile, errors,
// summary, chart, yarn and hook) and CrochetScene (the stepped run: relax, hang, insert, release; positions, metrics).
// All the logic is crochet_core's: the same CrochetSession the command line tool runs.
#pragma once

#include <memory>
#include <string>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "failoring/crochet/calibration.hpp"
#include "failoring/crochet/hinge_fit.hpp"
#include "failoring/crochet/layout.hpp"
#include "failoring/crochet/model.hpp"
#include "failoring/crochet/session.hpp"
#include "failoring/crochet/yarn.hpp"

namespace godot {

class CrochetPattern : public RefCounted {
    GDCLASS(CrochetPattern, RefCounted)

public:
    // Compile the pattern text (CrochetPARADE syntax plus `#!` extension lines). The export runs through node: tools_dir holds
    // tools/crochet/cp_export.mjs and third_party/crochetparade (default: the parent of the Godot project, the repo root).
    bool compile(const String& text, const String& yarns_path);
    TypedArray<Dictionary> get_errors() const;  // [{line, msg}]
    Dictionary get_summary() const;
    Dictionary get_chart() const;
    void set_yarn(const String& id);
    String get_yarn() const { return String(yarn_id_.c_str()); }
    void set_hook(double mm);
    double get_hook() const { return hook_mm_; }
    void set_tension(double t);
    PackedStringArray get_yarn_ids() const;
    Dictionary get_yarn_info(const String& id) const;
    void set_tools_dir(const String& dir) { tools_dir_ = dir.utf8().get_data(); }
    bool is_compiled() const { return compiled_; }
    // The swatch calibration record of docs/CROCHET_PLAN.md section 10 (a Dictionary as CrochetCalibration.fit() returns it under
    // "record"; empty = none): the stitch model uses it for the parts it measured. get_summary() then says "calibrated".
    void set_calibration(const Dictionary& record);
    Dictionary get_calibration() const;

    // for CrochetScene
    const fl::StitchGraph& graph() const { return graph_; }
    const fl::YarnBook& book() const { return book_; }
    std::string yarn_id() const { return yarn_id_; }
    double hook_mm() const { return hook_mm_; }
    double tension() const { return tension_; }
    const fl::Calibration* calibration() const { return cal_.gauge || cal_.mass || cal_.cantilever || cal_.stretch || !cal_.hinge.empty() ? &cal_ : nullptr; }

protected:
    static void _bind_methods();

private:
    void rebuild_model();
    fl::Calibration cal_;
    void set_error(int line, const std::string& msg);
    fl::StitchGraph graph_;
    fl::YarnBook book_;
    std::string yarn_id_ = "acrylic_worsted", tools_dir_;
    double hook_mm_ = 0, tension_ = 1.0;
    bool compiled_ = false;
    fl::CrochetModel model_;
    fl::LayoutReport layout_;
    bool modelReady_ = false;
    std::vector<std::pair<int, std::string>> errors_;
    std::string modelError_;
};

// Four kitchen-table measurements -> a calibration record (gauge, weight, cantilever, stretch; plan section 10), and what that does to
// one stitch compared with the starter model. The measurements and the record are Dictionaries; see crochet_core's calibration.hpp.
class CrochetCalibration : public RefCounted {
    GDCLASS(CrochetCalibration, RefCounted)

public:
    // measurements: {stitches_per_10cm, rows_per_10cm, mass_g, width_cm, height_cm, stitches, rows, overhang_wale_mm,
    // overhang_course_mm, wale: {width_cm, l0_mm, w1_g, w2_g, l1_mm, l2_mm}, course: {...}}; missing keys = not measured.
    // Returns {record: {w, h (m), density (kg/m^2), bendWale, bendCourse (N*m), kWale, kCourse (N/m), lockWale, lockCourse, lockRatio}
    // with only the measured parts, notes: [...], errors: [...], lock_reached_wale, lock_reached_course, empty}.
    Dictionary fit(const Dictionary& measurements) const;
    // The sc stitch of a yarn on a hook (mm, 0 = the yarn's) without and with a record: {starter: {...}, calibrated: {...}}.
    bool load_yarns(const String& path);
    Dictionary compare(const String& yarn_id, double hook_mm, const Dictionary& record) const;
    // The hinge fit of section 10.5: a swatch of 10 + 1 + 10 rows with the hinge row worked as `construction` ("blo", "flo", "slst"),
    // clamped flat with the hinge at the edge, drooping under its weight. hinge_droop() is one simulator run (degrees below the
    // horizontal); fit_hinge() bisects bendScale until the simulated droop matches the measured one: {bend_scale, droop_deg, runs,
    // bracketed, notes}. `record` is the fabric's calibration record ({} = the starter values); hook_mm 0 = the yarn's.
    double hinge_droop(const String& construction, double bend_scale, double rest_deg, const String& yarn_id, double hook_mm, const Dictionary& record) const;
    Dictionary fit_hinge(const String& construction, double rest_deg, double droop_deg, const String& yarn_id, double hook_mm, const Dictionary& record) const;

protected:
    static void _bind_methods();

private:
    fl::YarnBook book_;
    bool have_book_ = false;
};

class CrochetScene : public RefCounted {
    GDCLASS(CrochetScene, RefCounted)

public:
    // scene: "auto" | "hang_pin" | "hang_peg" | "none"; load: "none" | "mba13" | "mbp14" | "mbp16" | "box:LxWxH_mm:kg".
    bool build(const Ref<CrochetPattern>& pattern, const String& scene, const String& load);
    void relax_step(int n);   // up to n frames of the relaxation (a no-op in later phases)
    void step(int n);         // up to n frames of the current phase, handing over to the next
    String phase() const;     // "relax", "hang", "insert", "release", "done"
    bool is_done() const;
    bool is_settled() const;
    int get_phase_frames() const;
    PackedVector3Array get_positions() const;
    PackedInt32Array get_indices() const;
    PackedVector3Array get_normals() const;
    PackedInt32Array get_stitch_of_vertex() const;
    PackedVector2Array get_vertex_uv() const;       // stitch coordinates (u along the row, v up the rows) for the surface shader
    PackedColorArray get_vertex_colors() const;     // r = stitch type, g = insertion, b = 1 on a RS row (all 0..1)
    double get_thickness() const;                   // m, the fabric thickness for the shell
    Dictionary get_metrics() const;
    Transform3D get_load_transform() const;
    Dictionary get_load() const;  // {name, size: Vector3 (width, height, thickness), mass, fits, tight, message}
    Dictionary get_peg() const;   // {} or {center, radius, length}
    String get_error() const { return String(error_.c_str()); }

protected:
    static void _bind_methods();

private:
    std::unique_ptr<fl::CrochetSession> session_;
    std::string error_;
};

}  // namespace godot
