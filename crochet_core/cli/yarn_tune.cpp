// yarn_tune: searches the design parameters of the yarn lattice (WorkParams) for a chain and an sc row in which every pass through an eye is
// a clean threading (the legs pierce its ring once each way) and no two strands overlap, then prints the best set.
//   yarn_tune [iterations] [seed]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "failoring/crochet/yarn_work.hpp"

using namespace fl;

namespace {

struct Param { const char* name; Real* (*get)(WorkParams&); Real lo, hi; };

#define P(field, lo, hi) {#field, [](WorkParams& w) -> Real* { return &w.field; }, lo, hi}
const Param kParams[] = {
    P(stitchWidth, 0.0055, 0.0120), P(stitchHeight, 0.0055, 0.0120), P(layerGap, 0.0010, 0.0060), P(ringRadius, 0.0024, 0.0060),
    P(legGap, 0.0006, 0.0030), P(approach, 0.0010, 0.0040), P(fingerLength, 0.0005, 0.0030), P(barA, 0.0030, 0.0080), P(barB, 0.0012, 0.0040),
    P(barTilt, 0.15, 0.9), P(postRise, 0.5, 1.1), P(workingRun, 0.0010, 0.0040), P(hdcScale, 1.45, 1.6), P(dcScale, 1.9, 2.1), P(slstScale, 0.3, 0.6),
    P(yoHdc, 0.3, 0.8), P(yoDc, 0.25, 0.6), P(riseDc, 0.55, 0.9), P(midDc, 0.7, 1.0), P(yoLayer, 0.4, 1.6)};
const int kN = (int)(sizeof(kParams) / sizeof(kParams[0]));
const int kFirstNew = 12;   // the parameters from here on belong to hdc, dc and sl st only (the first twelve are the sc and chain design, found earlier)

struct Score { int unclean = 0, passes = 0, extra = 0; Real pen = 0, total = 0; };

Real gRadius = 1.25e-3, gWant = 0.5;   // the yarn's radius, and how much of its width two strands must keep between their centre lines (0.5: worsted may be squeezed)

Score evaluate(const WorkParams& base) {
    YarnWork w;
    w.rodParams.radius = gRadius;
    w.wp = base;
    w.slipKnot(Vec3{0, 0, 0});
    for (int i = 0; i < 6; ++i) w.stitchChain();
    w.stitchTurn(0);
    for (int i = 0; i < 4; ++i) w.stitchSc();
    w.stitchTurn(1);
    for (int i = 0; i < 4; ++i) w.stitch("hdc");
    w.stitchTurn(2);
    for (int i = 0; i < 4; ++i) w.stitch("dc");
    w.stitchTurn(3);
    for (int i = 0; i < 4; ++i) w.stitch("slst");
    Score s;
    for (const auto& t : threadingReport(w)) {
        ++s.passes;
        if (t[0] <= 1) continue;   // the slip knot's own first loop is not part of the design
        if (t[2] < 1 || t[3] < 1) ++s.unclean;                    // a leg that misses the eye is not a stitch
        else s.extra += (t[2] - 1) + (t[3] - 1);                   // legs that wiggle through it more than once are untidy
    }
    // overlap: segment pairs more than 6 apart whose centre lines come closer than 0.8 of two radii
    const RodSystem& rs = w.rod;
    const Real want = gWant * 2.0 * rs.params.radius;
    const int S = rs.segmentCount();
    for (int a = 0; a < S; ++a)
        for (int b = a + 7; b < S; ++b) {
            const SegSegResult r = closestSegmentSegment(rs.x[rs.segA[a]], rs.x[rs.segB[a]], rs.x[rs.segA[b]], rs.x[rs.segB[b]]);
            if (r.dist2 < want * want) {
                const Real d = (want - std::sqrt(r.dist2)) * 1e3;
                s.pen += d * d;
            }
        }
    s.total = 1000.0 * s.unclean + 12.0 * s.extra + s.pen;
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    const int iters = argc > 1 ? std::atoi(argv[1]) : 300;
    if (const char* e = std::getenv("YARN_TUNE_RAD")) gRadius = std::atof(e) * 1e-3;     // mm
    if (const char* e = std::getenv("YARN_TUNE_WANT")) gWant = std::atof(e);
    const bool onlyNew = argc > 3 && std::string(argv[3]) == "new";
    std::mt19937 rng(argc > 2 ? std::atoi(argv[2]) : 1);
    std::uniform_real_distribution<Real> u01(0, 1);
    WorkParams best;
    Score bs = evaluate(best);
    std::printf("start: %d of %d passes miss an eye, %d extra piercings, overlap %.1f, total %.1f\n", bs.unclean, bs.passes, bs.extra, bs.pen, bs.total);
    Real temp = 30.0;
    WorkParams cur = best;
    Score cs = bs;
    for (int it = 0; it < iters; ++it) {
        WorkParams cand = cur;
        const int nChange = 1 + (int)(u01(rng) * 3);
        for (int k = 0; k < nChange; ++k) {
            const int lo = onlyNew ? kFirstNew : 0;
            const Param& p = kParams[lo + (int)(u01(rng) * (kN - lo)) % (kN - lo)];
            Real* f = p.get(cand);
            const Real range = p.hi - p.lo;
            *f = std::clamp(*f + (u01(rng) - 0.5) * 0.35 * range, p.lo, p.hi);
        }
        const Score sc = evaluate(cand);
        if (sc.total < cs.total || u01(rng) < std::exp((cs.total - sc.total) / temp)) {
            cur = cand;
            cs = sc;
            if (sc.total < bs.total) {
                bs = sc;
                best = cand;
                std::printf("it %4d: %d of %d passes miss an eye, %d extra piercings, overlap %.1f, total %.1f\n", it, bs.unclean, bs.passes, bs.extra, bs.pen, bs.total);
            }
        }
        temp = std::max(0.5, temp * 0.993);
    }
    std::printf("best:\n");
    for (int i = 0; i < kN; ++i) std::printf("  %s = %.5f\n", kParams[i].name, *kParams[i].get(best));
    return 0;
}
