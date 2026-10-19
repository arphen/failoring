// yarn_cli: builds a small piece of crochet one hook action at a time (yarn mode) and writes PNG views of it.
//   yarn_cli "ch 8; turn 0; sc 7; turn 1; hdc 7" [--out prefix] [--colour 0|1] [--last n] [--cscale s]
// actions, separated by ; : `ch N`, `sc N`, `hdc N`, `dc N`, `slst N`, `sk N` (skip), `turn K` (K turning chains), `colour R G B` (the next stitch
// changes to this colour, 0..1 each), `colourat R G B` (from the start of the next stitch). Prints the threading report and the overlap.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "failoring/crochet/yarn_render.hpp"
#include "failoring/crochet/yarn_work.hpp"

using namespace fl;

namespace {

void views(const YarnWork& w, const std::string& prefix, const std::vector<std::string>& which, int lastLoops, bool colourByRow) {
    YarnMeshOptions mo;
    mo.segmentColour = w.segmentColours();
    if (colourByRow) {   // by row; the yarn between the loops (travel, tail) is pale
        static const Vec3 pal[] = {{0.25, 0.45, 0.85}, {0.88, 0.28, 0.22}, {0.25, 0.70, 0.35}, {0.90, 0.70, 0.20}};
        mo.segmentColour.assign(w.rod.segmentCount(), Vec3{0.80, 0.78, 0.74});
        for (const YarnLoop& l : w.loops)
            for (int i = l.p0; i < l.p1; ++i) mo.segmentColour[w.strands[l.strand].first + i] = pal[l.row % 4];
    }
    const YarnMesh mesh = buildYarnMesh(w.rod, mo);
    Vec3 lo(1e300, 1e300, 1e300), hi(-1e300, -1e300, -1e300);
    const int first = lastLoops > 0 ? std::max(0, (int)w.loops.size() - lastLoops) : 0;
    for (size_t li = first; li < w.loops.size(); ++li) {   // the loops only: the tails run off
        const YarnLoop& l = w.loops[li];
        for (int i = l.p0; i <= l.p1; ++i) {
            const Vec3& p = w.point(l.strand, i);
            lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
            hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
        }
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
        std::fprintf(stderr, "usage: yarn_cli \"ch 8; turn 0; sc 7\" [--out prefix] [--colour 0|1] [--last n]\n");
        return 1;
    }
    const std::string script = argv[1];
    std::string out = "yarn";
    int lastLoops = 0;
    bool colour = false;
    YarnWork w;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string k = argv[i];
        const double v = std::atof(argv[i + 1]);
        if (k == "--out") out = argv[i + 1];
        else if (k == "--last") lastLoops = (int)v;
        else if (k == "--colour") colour = v != 0;
        else if (k == "--cscale") w.rodParams.radiusScale = v;
        else if (k == "--shape") w.rodParams.shapeStiffness = v;
    }
    w.slipKnot(Vec3{0, 0, 0});
    std::stringstream ss(script);
    std::string item;
    while (std::getline(ss, item, ';')) {
        std::stringstream is(item);
        std::string verb;
        is >> verb;
        if (verb.empty()) continue;
        if (verb == "turn") {
            int k = 0;
            is >> k;
            w.stitchTurn(k);
        } else if (verb == "colour" || verb == "colourat") {
            double r = 0, g = 0, b = 0;
            is >> r >> g >> b;
            w.setNextColour(Vec3{r, g, b}, verb == "colour" ? ColourAt::PullThrough : ColourAt::Stitch);
        } else {
            int n = 1;
            is >> n;
            for (int i = 0; i < n; ++i) {
                if (verb == "ch") w.stitchChain();
                else if (verb == "sk") w.stitchSkip();
                else if (w.stitch(verb, -1) < 0) std::fprintf(stderr, "could not make %s\n", verb.c_str());
            }
        }
    }
    {
        int bad = 0, total = 0;
        for (const auto& t : threadingReport(w)) {
            if (t[0] <= 1) continue;
            ++total;
            if (t[2] != 1 || t[3] != 1) {
                ++bad;
                std::printf("  loop %d through eye of loop %d: legs pierce its disc %d up, %d down\n", t[0], t[1], t[2], t[3]);
            }
        }
        std::printf("threading: %d of %d passes are a clean 1+1 piercing\n", total - bad, total);
    }
    std::printf("min strand gap %.3f mm (2r = %.2f)\n", minStrandGap(w.rod, 5) * 1e3, 2e3 * w.rodParams.radius);
    std::printf("%d particles, %zu strands, %zu loops\n%s", w.particleCount(), w.strands.size(), w.loops.size(), w.patternText().c_str());
    views(w, out, {"front", "back", "top", "side", "oblique"}, lastLoops, colour);
    return 0;
}
