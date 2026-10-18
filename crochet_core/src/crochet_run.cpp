// The headless pipeline of section 7.1. See crochet/run.hpp.
#include "failoring/crochet/run.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "failoring/crochet/layout.hpp"
#include "failoring/crochet/model.hpp"
#include "failoring/crochet/pattern.hpp"
#include "failoring/crochet/relax.hpp"
#include "failoring/crochet/scene.hpp"
#include "failoring/crochet/snapshot.hpp"

namespace fl {

namespace {

bool fileExists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

std::string stemOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}
std::string dirOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (const char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if (static_cast<unsigned char>(c) < 0x20) continue;
        o += c;
    }
    return o;
}

// Run the exporter through node into a temporary file and return its path.
std::string exportWithNode(const RunOptions& opt) {
    const std::string out = (opt.outPrefix.empty() ? std::string("/tmp/crochet_export") : opt.outPrefix) + "_export.json";
    const std::string cmd = "node \"" + opt.exporterPath + "\" --out \"" + out + "\" \"" + opt.patternPath + "\" 1>&2";
    if (std::system(cmd.c_str()) != 0 || !fileExists(out))
        throw std::runtime_error("no exported graph for " + opt.patternPath + ": looked for " + dirOf(opt.patternPath) + "/exported/" + stemOf(opt.patternPath) +
                                 ".json and `node " + opt.exporterPath + "` failed (set CP_DIR to a CrochetPARADE checkout, or pass --export)");
    return out;
}

bool writeObjFile(const std::string& path, const CrochetModel& m, const StitchGraph& g, const std::vector<Vec3>& x) {
    std::ofstream f(path);
    if (!f) return false;
    f << "# crochet model: vertex = stitch node; the comment before each vertex gives its stitch id and row\n";
    for (size_t v = 0; v < x.size(); ++v) {
        const Stitch& s = g.st[m.topo.vertexStitch[v]];
        f << "# stitch " << s.id << " row " << s.row << "\nv " << x[v].x << " " << x[v].y << " " << x[v].z << "\n";
    }
    for (const auto& t : m.mesh.tris) f << "f " << t[0] + 1 << " " << t[1] + 1 << " " << t[2] + 1 << "\n";
    return static_cast<bool>(f);
}

std::vector<SnapshotColour> rowColours(const CrochetModel& m, const StitchGraph& g) {
    std::vector<SnapshotColour> c(m.mesh.tris.size());
    for (size_t t = 0; t < c.size(); ++t) {
        const int row = g.st[m.topo.triSource[t]].row;
        c[t] = row % 2 == 1 ? SnapshotColour{205, 125, 60} : SnapshotColour{225, 150, 80};
    }
    return c;
}

}  // namespace

RunResult runCrochet(const RunOptions& opt) {
    RunResult res;
    const auto t0 = std::chrono::steady_clock::now();
    auto say = [&](const std::string& s) {
        if (!opt.verbose) return;
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "[%6.0f s] %s\n", sec, s.c_str());
    };
    try {
        if (opt.patternPath.empty()) throw std::runtime_error("no pattern given");
        if (!fileExists(opt.patternPath)) throw std::runtime_error("cannot read pattern '" + opt.patternPath + "'");
        std::string exportPath = opt.exportPath;
        if (exportPath.empty()) {
            const std::string beside = dirOf(opt.patternPath) + "/exported/" + stemOf(opt.patternPath) + ".json";
            exportPath = fileExists(beside) ? beside : exportWithNode(opt);
        } else if (!fileExists(exportPath)) {
            throw std::runtime_error("cannot read export '" + exportPath + "'");
        }
        const YarnBook book = loadYarnsFromFile(opt.yarnsPath);
        const Yarn* yarn = book.find(opt.yarnId);
        if (yarn == nullptr) throw std::runtime_error("unknown yarn '" + opt.yarnId + "' in " + opt.yarnsPath);
        const double hookMm = opt.hookMm > 0 ? opt.hookMm : yarn->hookMm;
        if (!(hookMm > 0)) throw std::runtime_error("no hook size (give --hook mm)");
        if (opt.scene != "auto" && opt.scene != "hang_pin" && opt.scene != "hang_peg" && opt.scene != "none")
            throw std::runtime_error("unknown scene '" + opt.scene + "' (auto, hang_pin, hang_peg, none)");
        LoadSpec load;
        const bool wantLoad = opt.load != "none";
        if (wantLoad) load = loadPreset(opt.load);  // throws std::invalid_argument naming the problem

        StitchGraph g = importPatternFile(exportPath, opt.patternPath);
        ModelOptions mo;
        mo.hook_m = hookMm * 1e-3;
        mo.tension = opt.tension;
        CrochetModel cm = buildCrochetModel(g, *yarn, mo);
        say("model: " + std::to_string(g.st.size()) + " stitches, " + std::to_string(cm.topo.vertexStitch.size()) + " vertices, " + std::to_string(cm.mesh.tris.size()) + " faces");
        for (const std::string& w : cm.warnings) say("warning: " + w);
        const LayoutReport layout = layoutModel(cm, g);
        for (const std::string& w : layout.warnings) say("layout warning: " + w);

        // relaxation (plan 6.9); a small seeded perturbation lets over- and under-increased pieces choose their shape
        RelaxOptions ro;
        ro.noise = 0.3e-3;
        const RelaxResult rr = relaxModel(cm, ro);
        res.relaxConverged = rr.converged;
        res.relaxFrames = rr.frames;
        say("relaxed: " + std::to_string(rr.frames) + " frames, KE " + std::to_string(rr.kineticEnergy) + " J, seam gap " + std::to_string(rr.maxSeamGap * 1e3) + " mm");
        MetricsInput in;
        in.model = &cm;
        in.graph = &g;
        in.layout = &layout;
        in.yarn = yarn;
        in.relaxed = cm.mesh.x0;
        in.frames = rr.frames;
        in.newtonIterations = 0;
        in.iterCapHits = rr.iterCapHits;
        in.substeps = rr.substeps;
        in.finalKineticEnergy = rr.kineticEnergy;

        bool hasStrap = false;
        for (const LayoutRegion& r : layout.regions) hasStrap = hasStrap || r.type == LayoutRegion::Strip;
        const bool hang = opt.scene == "hang_pin" || opt.scene == "hang_peg" || (opt.scene == "auto" && hasStrap);
        if (opt.scene == "hang_pin" || opt.scene == "hang_peg") {
            if (!hasStrap) throw std::runtime_error("scene " + opt.scene + " needs a strap, and this pattern has none");
        }
        if (wantLoad && !hang) throw std::runtime_error("a load needs a hung model (scene hang_pin or hang_peg, and a pattern with a strap)");

        std::vector<Vec3> finalX = cm.mesh.x0;
        bool settled = rr.converged;
        if (hang) {
            SceneOptions so;
            so.kind = opt.scene == "hang_peg" ? SceneKind::HangPeg : SceneKind::HangPin;
            so.maxFrames = opt.frames;
            CrochetScene scene(cm, g, layout, so);
            say(std::string("hanging (") + (so.kind == SceneKind::HangPeg ? "hang_peg" : "hang_pin") + ")");
            const SceneStats st = scene.settle();
            res.ranHang = true;
            res.hangSettled = st.settled;
            res.hangFrames = st.frames;
            settled = settled && st.settled;
            say("hang: " + std::string(st.settled ? "settled" : "NOT settled") + " in " + std::to_string(st.frames) + " frames, KE " + std::to_string(st.kineticEnergy) + " J, Newton cap " +
                std::to_string(st.iterCapHits) + "/" + std::to_string(st.substeps));
            in.hanging = scene.solver().positions();
            in.frames += st.frames;
            in.newtonIterations += st.newtonIterations;
            in.iterCapHits += st.iterCapHits;
            in.substeps += st.substeps;
            in.lineSearchFailures += scene.solver().contactDiag().lsFail;
            in.finalKineticEnergy = st.kineticEnergy;
            finalX = in.hanging;
            if (wantLoad) {
                const FitResult fit = fitCheck(cm, g, layout, load);
                in.hasLoad = true;
                in.load = load;
                in.fit = fit;
                say("fit: " + fit.message);
                if (fit.known && fit.fits) {
                    RigidLoad rl(scene, load);
                    const bool inserted = rl.insert(opt.frames);
                    say(std::string("load inserted (") + rl.stats().stopReason + ") after " + std::to_string(rl.stats().insertFrames) + " frames");
                    const bool released = inserted && rl.release(opt.frames);
                    res.ranLoad = true;
                    res.loadSettled = released;
                    settled = settled && released;
                    say(std::string("load: ") + (released ? "settled" : "NOT settled") + " after " + std::to_string(rl.stats().releaseFrames) + " frames, |F_net| " +
                        std::to_string(rl.stats().netForceFraction) + " of its weight");
                    in.loaded = scene.solver().positions();
                    in.loadCenter = rl.center();
                    in.frames += rl.stats().insertFrames + rl.stats().releaseFrames;
                    in.newtonIterations += scene.solver().contactDiag().newtonIters - 0;
                    in.iterCapHits = scene.solver().contactDiag().iterCapHits;
                    in.substeps = scene.solver().contactDiag().substeps;
                    in.lineSearchFailures = scene.solver().contactDiag().lsFail;
                    in.finalKineticEnergy = scene.solver().kineticEnergy();
                    Real pmax = 0;
                    for (const Real p : scene.solver().vertexRigidPressure()) pmax = std::max(pmax, p);
                    in.maxContactPressure = pmax;
                    finalX = in.loaded;
                } else {
                    say(fit.known ? "the load is not inserted" : "the pattern has no mouth: the load is not inserted");
                }
            }
        }
        res.metrics = computeMetrics(in);

        std::ostringstream js;
        js << "{\n \"pattern\": \"" << jsonEscape(opt.patternPath) << "\",\n \"settings\": {\"yarn\": \"" << jsonEscape(opt.yarnId) << "\", \"hook_mm\": " << hookMm
           << ", \"tension\": " << opt.tension << ", \"scene\": \"" << (hang ? (opt.scene == "hang_peg" ? "hang_peg" : "hang_pin") : "none") << "\", \"load\": \""
           << jsonEscape(opt.load) << "\", \"frames_cap\": " << opt.frames << "},\n \"phases\": {\"relax\": {\"frames\": " << res.relaxFrames << ", \"converged\": "
           << (res.relaxConverged ? "true" : "false") << "}, \"hang\": " << (res.ranHang ? std::string("{\"frames\": ") + std::to_string(res.hangFrames) + ", \"settled\": " + (res.hangSettled ? "true" : "false") + "}" : "null")
           << ", \"load\": " << (res.ranLoad ? std::string("{\"settled\": ") + (res.loadSettled ? "true" : "false") + "}" : "null") << "},\n \"settled\": " << (settled ? "true" : "false")
           << ",\n \"metrics\": " << res.metrics.toJson() << "}\n";
        res.json = js.str();

        if (!opt.outPrefix.empty()) {
            std::ofstream(opt.outPrefix + ".json") << res.json;
            const bool objOk = writeObjFile(opt.outPrefix + ".obj", cm, g, finalX) && writeObjFile(opt.outPrefix + "_rest.obj", cm, g, in.relaxed);
            const std::vector<SnapshotColour> colours = rowColours(cm, g);
            const bool pngOk = writeSnapshotPng(opt.outPrefix + ".png", finalX, cm.mesh.tris, colours) && writeSnapshotPng(opt.outPrefix + "_rest.png", in.relaxed, cm.mesh.tris, colours);
            if (!objOk || !pngOk) throw std::runtime_error("cannot write the output files under '" + opt.outPrefix + "'");
            say("wrote " + opt.outPrefix + ".json/.obj/_rest.obj/.png/_rest.png");
        }
        res.exitCode = settled ? 0 : 2;
    } catch (const std::exception& e) {
        res.exitCode = 1;
        res.error = e.what();
    }
    return res;
}

}  // namespace fl
