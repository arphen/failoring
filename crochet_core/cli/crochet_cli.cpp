// crochet_cli: run a crochet pattern through the whole headless pipeline (docs/CROCHET_PLAN.md section 7.1).
//   crochet_cli patterns/cp/pouch.txt --scene hang_pin --load none|mba13|mbp14|mbp16|box:LxWxH_mm:kg
//               [--yarn id] [--hook mm] [--tension t] [--frames N] [--out out/run1] [--export file.json] [--yarns file] [--json]
// Exit code 0 = settled, 2 = not settled, 1 = error.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "failoring/crochet/run.hpp"

static void usage() {
    std::fprintf(stderr,
                 "usage: crochet_cli <pattern.txt> [--scene auto|hang_pin|hang_peg|none] [--load none|mba13|mbp14|mbp16|box:LxWxH_mm:kg]\n"
                 "                   [--yarn id] [--hook mm] [--tension t] [--frames N] [--out prefix] [--export file.json] [--yarns file] [--exporter file.mjs] [--json] [--quiet]\n"
                 "writes <prefix>.json, .obj, _rest.obj, .png, _rest.png; exit 0 settled, 2 not settled, 1 error\n");
}

int main(int argc, char** argv) {
    fl::RunOptions o;
    bool printJson = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "crochet_cli: %s needs a value\n", name);
                usage();
                std::exit(1);
            }
            return argv[++i];
        };
        if (a == "--scene") o.scene = next("--scene");
        else if (a == "--load") o.load = next("--load");
        else if (a == "--yarn") o.yarnId = next("--yarn");
        else if (a == "--hook") o.hookMm = std::atof(next("--hook").c_str());
        else if (a == "--tension") o.tension = std::atof(next("--tension").c_str());
        else if (a == "--frames") o.frames = std::atoi(next("--frames").c_str());
        else if (a == "--out") o.outPrefix = next("--out");
        else if (a == "--export") o.exportPath = next("--export");
        else if (a == "--yarns") o.yarnsPath = next("--yarns");
        else if (a == "--exporter") o.exporterPath = next("--exporter");
        else if (a == "--json") printJson = true;
        else if (a == "--quiet") o.verbose = false;
        else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "crochet_cli: unknown option %s\n", a.c_str());
            usage();
            return 1;
        } else if (o.patternPath.empty()) o.patternPath = a;
        else {
            std::fprintf(stderr, "crochet_cli: more than one pattern given (%s)\n", a.c_str());
            return 1;
        }
    }
    if (o.patternPath.empty()) {
        usage();
        return 1;
    }
    const fl::RunResult r = fl::runCrochet(o);
    if (r.exitCode == 1) {
        std::fprintf(stderr, "crochet_cli: error: %s\n", r.error.c_str());
        return 1;
    }
    if (printJson) std::printf("%s", r.json.c_str());
    return r.exitCode;
}
