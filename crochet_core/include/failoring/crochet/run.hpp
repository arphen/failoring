#pragma once
// One call for the whole headless pipeline (docs/CROCHET_PLAN.md section 7.1, ticket T6.4): import the pattern, build the
// model, lay it out, relax it, hang it, optionally put a rigid load in, compute the metrics and write the files. The
// command line tool (crochet_core/cli/crochet_cli.cpp) is a thin wrapper around runCrochet; the GDExtension (T7) uses the
// same pieces one by one.
//
// Decisions recorded for T6.4:
//  - Pattern input is a CrochetPARADE text file (patterns/cp/*.txt). Its exported graph is read from the committed export
//    next to it (<dir>/exported/<stem>.json) or from `exportPath`; when neither exists the exporter is run through node
//    (tools/crochet/cp_export.mjs, CP_DIR pointing to a CrochetPARADE checkout), which is the only place node is needed.
//  - scene "auto" hangs the model by its strap when it has one (hang_pin) and otherwise stops after the relaxation: the
//    circles, the tube and the swatches have no strap to hang from.
//  - Exit codes (plan 7.1): 0 everything settled, 2 something did not settle (relaxation, hang or load release), 1 error.
//  - Files (outPrefix = out/run1): run1.json (metrics and phase numbers), run1.obj (the final state, a `# stitch <id> row
//    <r>` comment before each vertex), run1_rest.obj (the relaxed zero-g shape) and run1.png, run1_rest.png snapshots.
#include <string>

#include "failoring/crochet/metrics.hpp"

namespace fl {

struct RunOptions {
    std::string patternPath;           // CrochetPARADE text
    std::string exportPath;            // exporter JSON; empty = <dir>/exported/<stem>.json, else run node
    std::string exporterPath = "tools/crochet/cp_export.mjs";
    std::string yarnsPath = "materials/yarns.json";
    std::string yarnId = "acrylic_worsted";
    double hookMm = 0;                 // 0 = the yarn's own hook
    double tension = 1.0;
    std::string scene = "auto";        // auto | hang_pin | hang_peg | none
    std::string load = "none";         // none | mba13 | mbp14 | mbp16 | box:LxWxH_mm:kg
    int frames = 1500;                 // frame cap of each scene phase
    std::string outPrefix;             // empty = write nothing
    bool verbose = true;               // progress on stderr
};

struct RunResult {
    int exitCode = 1;                  // 0, 2 or 1
    std::string error;                 // when exitCode == 1
    bool relaxConverged = false, hangSettled = false, loadSettled = false, ranHang = false, ranLoad = false;
    Metrics metrics;
    std::string json;                  // the content of run1.json
    int relaxFrames = 0, hangFrames = 0;
};

RunResult runCrochet(const RunOptions& opt);

}  // namespace fl
