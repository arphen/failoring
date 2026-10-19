// yarn_cli: builds a small piece of crochet one hook action at a time (yarn mode) and writes PNG views of it.
//   yarn_cli <demo> [--out prefix] [--tilt t] [--tighten f] [--settle n] [--leg mm] [--finger mm]
// demos: chain (ch 8), scrow (ch 8, sc into the chain), scrows (two rows of sc)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "failoring/crochet/yarn_render.hpp"
#include "failoring/crochet/yarn_work.hpp"

using namespace fl;

namespace {

void views(const YarnWork& w, const std::string& prefix, const std::vector<std::string>& which) {
    YarnMeshOptions mo;
    const YarnMesh mesh = buildYarnMesh(w.rod, mo);
    Vec3 lo(1e300, 1e300, 1e300), hi(-1e300, -1e300, -1e300);
    for (const YarnLoop& l : w.loops)   // the loops only: the tail and the working end run off
        for (int i = l.p0; i <= l.p1; ++i) {
            const Vec3& p = w.rod.x[i];
            lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
            hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
        }
    const Vec3 c = (lo + hi) * 0.5;
    const Real rad = std::max(0.012, 0.5 * length(hi - lo));
    for (const std::string& v : which) {
        YarnCamera cam;
        cam.target = c;
        cam.fovDeg = 30;
        cam.distance = rad * 2.6;
        if (v == "front") cam.dir = Vec3{0, 0, 1};
        else if (v == "back") cam.dir = Vec3{0, 0, -1};
        else if (v == "top") { cam.dir = Vec3{0, 1, 0}; cam.up = Vec3{0, 0, -1}; }
        else if (v == "side") cam.dir = Vec3{1, 0, 0};
        else cam.dir = normalized(Vec3{0.6, 0.5, 0.9});
        renderYarnPng(prefix + "_" + v + ".png", mesh, cam, 900, 700);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: yarn_cli <chain|scrow|scrows> [--out prefix] ...\n");
        return 1;
    }
    const std::string demo = argv[1];
    std::string out = "yarn";
    int nChains = 8;
    YarnWork w;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string k = argv[i];
        const double v = std::atof(argv[i + 1]);
        if (k == "--out") out = argv[i + 1];
        else if (k == "--tighten") w.wp.tighten = v;
        else if (k == "--settle") w.wp.settleSteps = (int)v;
        else if (k == "--leg") w.wp.legGap = v * 1e-3;
        else if (k == "--finger") w.wp.fingerLength = v * 1e-3;
        else if (k == "--bulb") w.wp.ringRadius = v * 1e-3;
        else if (k == "--hook") w.wp.hookRadius = v * 1e-3;
        else if (k == "--run") w.wp.workingRun = v * 1e-3;
        else if (k == "--approach") w.wp.approach = v * 1e-3;
        else if (k == "--tension") w.wp.workTension = v;
        else if (k == "--close") w.wp.closeFactor = v;
        else if (k == "--lift") w.wp.travelLift = v * 1e-3;
        else if (k == "--n") nChains = (int)v;
        else if (k == "--slack") w.wp.travelSlack = v;
        else if (k == "--ring") w.wp.ringRadius = v * 1e-3;
        else if (k == "--W") w.wp.stitchWidth = v * 1e-3;
        else if (k == "--H") w.wp.stitchHeight = v * 1e-3;
        else if (k == "--layer") w.wp.layerGap = v * 1e-3;
        else if (k == "--gap") w.wp.legGap = v * 1e-3;
        else if (k == "--work") w.wp.workingRun = v * 1e-3;
        else if (k == "--floor") w.wp.closeFloor = v;
    }
    std::vector<int> chainLoops;
    int live = w.slipKnot(Vec3{0, 0, 0});
    chainLoops.push_back(live);
    int n = nChains;
    for (int i = 0; i < n; ++i) {
        live = w.chain();
        chainLoops.push_back(live);
    }
    if (demo == "scrow" || demo == "scrows") {
        // turn and work back along the chain: into every chain loop but the one on the hook
        w.turn();
        for (int i = (int)chainLoops.size() - 2; i >= 1; --i) w.sc(chainLoops[i]);
    }
    std::printf("min strand gap %.3f mm (2r = %.2f)\n", minStrandGap(w.rod, 5) * 1e3, 2e3 * w.rod.params.radius);
    std::printf("%d particles, %d segments, %zu loops\n", w.particleCount(), w.rod.segmentCount(), w.loops.size());
    views(w, out, {"front", "back", "top", "oblique"});
    return 0;
}
