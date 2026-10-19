// yarn_cli: builds a small piece of crochet one hook action at a time (yarn mode) and writes PNG views of it.
//   yarn_cli "ch 8; turn 0; sc 7; turn 1; hdc 7" [--out prefix] [--colour 0|1] [--last n] [--cscale s]
// actions, separated by ; : `ch N`, `sc N`, `hdc N`, `dc N`, `slst N`, `sk N` (skip), `turn K` (K turning chains), `colour R G B` (the next stitch
// changes to this colour, 0..1 each), `colourat R G B` (from the start of the next stitch), `fo` (fasten off). Prints the threading report and the overlap.
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

// the design parameters by name (--wp "stitchWidth=0.0185,legGap=0.0014")
struct NamedParam { const char* name; Real* (*get)(WorkParams&); };
#define NP(field) {#field, [](WorkParams& w) -> Real* { return &w.field; }}
const NamedParam kNamed[] = {NP(stitchWidth), NP(stitchHeight), NP(layerGap), NP(ringRadius), NP(legGap), NP(approach), NP(fingerLength), NP(barA), NP(barB), NP(barTilt), NP(postRise),
                             NP(workingRun), NP(hdcScale), NP(dcScale), NP(slstScale), NP(yoHdc), NP(yoDc), NP(riseDc), NP(midDc), NP(yoLayer), NP(chainPitch), NP(chainA), NP(chainB),
                             NP(chainLayer), NP(chainTaper), NP(chainEye), NP(flare), NP(tailLength)};
void setNamed(WorkParams& wp, const std::string& list) {
    std::stringstream ss(list);
    std::string item;
    while (std::getline(ss, item, ',')) {
        const size_t eq = item.find('=');
        if (eq == std::string::npos) continue;
        const std::string name = item.substr(0, eq);
        for (const NamedParam& n : kNamed)
            if (name == n.name) *n.get(wp) = std::atof(item.c_str() + eq + 1);
    }
}

Real gThick = 1.0;   // the drawn thickness of the yarn

void views(const YarnWork& w, const std::string& prefix, const std::vector<std::string>& which, int lastLoops, bool colourByRow) {
    YarnMeshOptions mo;
    mo.radiusScale = gThick;
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

// The hook drawing `loop` through its eye, at a few moments (docs/YARN_MODE_PLAN.md, the pull-through animation).
void pullViews(const YarnWork& w, int loop, const std::string& prefix) {
    const YarnLoop& L = w.loops[loop];
    const Real sv[] = {0.0, 0.3, 0.6, 1.0};
    int k = 0;
    for (Real s : sv) {
        const YarnWork::PullFrame f = w.pullFrame(loop, s, std::max(0.0, 1.0 - 2.0 * s));
        RodSystem r = w.rod;
        for (size_t i = 0; i < f.pts.size(); ++i) r.x[f.first + i] = f.pts[i];
        YarnMeshOptions mo;
        mo.segmentColour = w.segmentColours();
        mo.maxParticles = std::min(r.particleCount(), w.strands[L.strand].first + L.p1 + 2);
        YarnMesh m = buildYarnMesh(r, mo);
        std::vector<Vec3> hp;
        for (int i = 0; i <= 12; ++i) hp.push_back(f.tip + f.axis * (0.06 * i / 12.0));
        YarnMeshOptions ho;
        ho.plyContrast = 0.0;
        const YarnMesh hm = buildTubeMesh(hp, 0.0019, Vec3{0.62, 0.64, 0.68}, ho);
        const int off = (int)m.pos.size();
        m.pos.insert(m.pos.end(), hm.pos.begin(), hm.pos.end());
        m.nrm.insert(m.nrm.end(), hm.nrm.begin(), hm.nrm.end());
        m.col.insert(m.col.end(), hm.col.begin(), hm.col.end());
        for (const auto& t : hm.tri) m.tri.push_back({t[0] + off, t[1] + off, t[2] + off});
        for (const char* v : {"obl", "side"}) {
            YarnCamera cam;
            cam.target = f.tip;
            cam.fovDeg = 30;
            cam.distance = 0.07;
            Vec3 side = cross(f.axis, Vec3{0, 1, 0});
            if (length2(side) < 1e-8) side = Vec3{0, 0, 1};
            side = normalized(side);
            if (side.z < 0) side = -side;   // from the front of the work
            cam.dir = std::string(v) == "obl" ? normalized(side + Vec3{0, 0.7, 0} + f.axis * 0.4) : side;
            renderYarnPng(prefix + "_pull" + std::to_string(k) + "_" + v + ".png", m, cam, 700, 520);
        }
        if (k == 0) std::printf("pull of loop %d: axis (%.2f %.2f %.2f), through %d\n", loop, f.axis.x, f.axis.y, f.axis.z, (int)f.through);
        ++k;
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
    int pull = -2;
    int showOverlaps = 0;
    bool stand = false;
    YarnWork w;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string k = argv[i];
        const double v = std::atof(argv[i + 1]);
        if (k == "--out") out = argv[i + 1];
        else if (k == "--last") lastLoops = (int)v;
        else if (k == "--colour") colour = v != 0;
        else if (k == "--pull") pull = (int)v;
        else if (k == "--overlaps") showOverlaps = (int)v;
        else if (k == "--stand") stand = v != 0;
        else if (k == "--cscale") w.rodParams.radiusScale = v;
        else if (k == "--pitch") w.wp.chainPitch = v * 1e-3;
        else if (k == "--chA") w.wp.chainA = v * 1e-3;
        else if (k == "--chB") w.wp.chainB = v * 1e-3;
        else if (k == "--thick") gThick = v;
        else if (k == "--wp") setNamed(w.wp, argv[i + 1]);
        else if (k == "--open") { if (v != 0) w.useOpenLattice(); }
        else if (k == "--rad") w.rodParams.radius = v * 1e-3;
        else if (k == "--taper") w.wp.chainTaper = v;
        else if (k == "--chEye") w.wp.chainEye = v;
        else if (k == "--chLayer") w.wp.chainLayer = v * 1e-3;
        else if (k == "--gap") w.wp.legGap = v * 1e-3;
        else if (k == "--flare") w.wp.flare = v * 1e-3;
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
        if (verb == "fo") {
            if (w.finishOff() < 0) std::fprintf(stderr, "could not fasten off\n");
        } else if (verb == "turn") {
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
    {   // how often the design's centre lines come closer than a yarn allows (docs: the yarn must not pass through itself)
        const Real twoR = 2.0 * w.rodParams.radius;
        const std::vector<YarnOverlap> ov = findOverlaps(w, twoR);
        int c25 = 0, c50 = 0, c75 = 0, c0 = 0, c01 = 0;
        for (const YarnOverlap& o : ov) {
            if (o.dist < 0.05e-3) ++c0;
            if (o.dist < 0.30e-3) ++c01;
            if (o.dist < 0.25 * twoR) ++c25;
            if (o.dist < 0.50 * twoR) ++c50;
            if (o.dist < 0.75 * twoR) ++c75;
        }
        std::printf("design overlaps (segment pairs closer than the yarn's width 2r = %.2f mm): %zu; closer than 75%% / 50%% / 25%% of it: %d / %d / %d; closer than 0.05 mm / 0.3 mm: %d / %d\n", twoR * 1e3, ov.size(), c75, c50, c25, c0, c01);
        for (size_t k = 0; k < ov.size() && k < (size_t)showOverlaps; ++k)
            std::printf("  %.2f mm at (%.1f %.1f %.1f) / (%.1f %.1f %.1f): strand %d seg %d (loop %d) vs strand %d seg %d (loop %d)\n", ov[k].dist * 1e3, ov[k].at.x * 1e3, ov[k].at.y * 1e3,
                        ov[k].at.z * 1e3, ov[k].atB.x * 1e3, ov[k].atB.y * 1e3, ov[k].atB.z * 1e3, ov[k].strand, ov[k].i, ov[k].loopA, ov[k].strandB, ov[k].j, ov[k].loopB);
    }
    std::printf("%d particles, %zu strands, %zu loops\n%s", w.particleCount(), w.strands.size(), w.loops.size(), w.patternText().c_str());
    if (stand) {   // the loops on the hook stand round it, the hook through them (as in the photographs)
        const Vec3 tipDir = normalized(Vec3{-0.86, -0.38, -0.30});
        YarnWork ws = w;
        const Vec3 tip = ws.standTip(ws.hook.back(), tipDir);
        for (size_t hi = 0; hi < ws.hook.size(); ++hi) {
            const int x = ws.hook[hi];
            const std::vector<Vec3> ring = w.standRing(x, tipDir, tip - tipDir * (0.003 * (ws.hook.size() - 1 - hi) * -1.0), 1.0);
            for (int i = ws.loops[x].r0; i <= ws.loops[x].r1; ++i) ws.rod.x[ws.strands[ws.loops[x].strand].first + i] = ring[i - ws.loops[x].r0];
        }
        views(ws, out, {"front", "oblique", "side", "top"}, lastLoops, colour);
        std::printf("standing: hook tip at (%.1f %.1f %.1f) mm\n", tip.x * 1e3, tip.y * 1e3, tip.z * 1e3);
        return 0;
    }
    views(w, out, {"front", "back", "top", "side", "oblique"}, lastLoops, colour);
    if (pull >= -1) pullViews(w, pull < 0 ? (int)w.loops.size() - 1 + pull + 1 : pull, out);
    return 0;
}
