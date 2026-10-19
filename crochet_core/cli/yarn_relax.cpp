// yarn_relax: can the contact solver turn the design into a yarn that does not pass through itself? The design is built, then the yarn is inflated from a thin radius to its
// own while the rod solver keeps the strands apart (the shape memory pulls them back toward the design only gently), and what comes out is measured: segment pairs closer than the
// yarn's width, and whether every finger still goes through its loops once each way (the topology survived).
//   yarn_relax "ch 6; turn 0; sc 5" [--out prefix] [--stages n] [--steps n] [--shape k] [--from f]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "failoring/crochet/yarn_render.hpp"
#include "failoring/crochet/yarn_work.hpp"

using namespace fl;

namespace {

// segment pairs of the rod that overlap (their centre lines are closer than the sections need, `frac` of it), and the smallest distance as a fraction of what they need
int countClose(const RodSystem& rs, Real frac, Real* minRatio) {
    const int S = rs.segmentCount();
    int n = 0;
    Real worst = 1e300;
    for (int a = 0; a < S; ++a)
        for (int b = a + 1; b < S; ++b) {
            if (rs.segChain[a] == rs.segChain[b] && std::abs(rs.segIndex[a] - rs.segIndex[b]) <= 4) continue;
            const Real gap = rs.contactGap(a, b);
            // quick reject: far apart
            if (gap > 0.5 * rs.params.radius) continue;
            const Real need = [&]() {
                // the need is the distance minus the gap
                const SegSegResult r = closestSegmentSegment(rs.x[rs.segA[a]], rs.x[rs.segB[a]], rs.x[rs.segA[b]], rs.x[rs.segB[b]]);
                return std::sqrt(r.dist2) - gap;
            }();
            const SegSegResult r = closestSegmentSegment(rs.x[rs.segA[a]], rs.x[rs.segB[a]], rs.x[rs.segA[b]], rs.x[rs.segB[b]]);
            const Real ratio = std::sqrt(r.dist2) / std::max(need, 1e-12);
            worst = std::min(worst, ratio);
            if (ratio < frac) ++n;
        }
    if (minRatio) *minRatio = worst;
    return n;
}

void build(YarnWork& w, const std::string& script) {
    w.slipKnot(Vec3{0, 0, 0});
    std::stringstream ss(script);
    std::string item;
    while (std::getline(ss, item, ';')) {
        std::stringstream is(item);
        std::string verb;
        is >> verb;
        if (verb.empty()) continue;
        if (verb == "fo") { w.finishOff(); continue; }
        int n = 1;
        is >> n;
        if (verb == "turn") { w.stitchTurn(n); continue; }
        for (int i = 0; i < n; ++i) {
            if (verb == "ch") w.stitchChain();
            else if (verb == "sk") w.stitchSkip();
            else w.stitch(verb, -1);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: yarn_relax \"ch 6; turn 0; sc 5\" [--out prefix] [--stages n] [--steps n] [--shape k] [--from f]\n");
        return 1;
    }
    std::string out = "relax";
    int stages = 12, steps = 60;
    Real slack = 1.0, shape = 5.0, from = 0.15, to = 1.0, ribA = 0.0, ribB = 0.0, damping = -1.0;
    int iters = -1, sub = -1;
    Real ea = -1.0, tf = 0.985;
    int tighten = 0, tsteps = 30;
    bool straight = false;
    int smooth = 0, squeeze = 0;
    Real sf = 0.95, zf = 1.0, sshape2 = 20.0;
    Real smoothShape = 2.0, smoothEI = -1.0;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string k = argv[i];
        const double v = std::atof(argv[i + 1]);
        if (k == "--out") out = argv[i + 1];
        else if (k == "--stages") stages = (int)v;
        else if (k == "--steps") steps = (int)v;
        else if (k == "--shape") shape = v;
        else if (k == "--from") from = v;
        else if (k == "--to") to = v;
        else if (k == "--slack") slack = v;
        else if (k == "--iters") iters = (int)v;
        else if (k == "--sub") sub = (int)v;
        else if (k == "--damp") damping = v;
        else if (k == "--ea") ea = v;
        else if (k == "--tighten") tighten = (int)v;
        else if (k == "--tf") tf = v;
        else if (k == "--tsteps") tsteps = (int)v;
        else if (k == "--straight") straight = v != 0;
        else if (k == "--smooth") smooth = (int)v;
        else if (k == "--squeeze") squeeze = (int)v;
        else if (k == "--sf") sf = v;
        else if (k == "--zf") zf = v;
        else if (k == "--sshape2") sshape2 = v;
        else if (k == "--sshape") smoothShape = v;
        else if (k == "--sei") smoothEI = v;
        else if (k == "--a") ribA = v * 1e-3;
        else if (k == "--b") ribB = v * 1e-3;
    }
    YarnWork w;
    build(w, argv[1]);
    RodSystem rs = w.rod;
    rs.params.gravity = Vec3{};
    rs.params.shapeStiffness = shape;
    if (ea > 0) rs.params.EA = ea;
    if (iters > 0) rs.params.iterations = iters;
    if (sub > 0) rs.params.substeps = sub;
    if (damping > 0) rs.params.damping = damping;
    if (ribA > 0.0) {
        rs.params.ribbonWidth = ribA;
        rs.params.ribbonThickness = ribB;
        rs.params.ribbonNormal = Vec3{0, 0, 1};
    }
    if (slack != 1.0) rs.scaleRestLengths(slack);
    const std::vector<Vec3> design = rs.x;
    Real mr = 0;
    rs.params.radiusScale = to;
    std::printf("design:   close(<90%%) %d pairs, closest %.2f of the yarn's width\n", countClose(rs, 0.9, &mr), mr);
    for (int st = 0; st < stages; ++st) {
        const Real scale = from + (to - from) * (stages > 1 ? (Real)st / (stages - 1) : 1.0);
        rs.params.radiusScale = scale;
        for (int k = 0; k < steps; ++k) rs.step(1.0 / 120.0);
        Real maxd = 0;
        for (size_t i = 0; i < rs.x.size(); ++i) maxd = std::max(maxd, length(rs.x[i] - design[i]));
        Real maxStrain = 0, meanStrain = 0;
        int ns = 0;
        for (int sgi = 0; sgi < rs.segmentCount(); ++sgi) {
            const Real e = std::fabs(length(rs.x[rs.segB[sgi]] - rs.x[rs.segA[sgi]]) / rs.restLen[sgi] - 1.0);
            maxStrain = std::max(maxStrain, e);
            meanStrain += e;
            ++ns;
        }
        std::printf("scale %.2f: close(<90%%) %d, closest %.2f, %d contacts, moved up to %.1f mm, strain max %.2f mean %.3f\n", scale, countClose(rs, 0.9, &mr), mr, rs.lastContacts, maxd * 1e3, maxStrain, meanStrain / std::max(1, ns));
    }
    {   // the worst pairs left, and which loops they belong to
        const int S = rs.segmentCount();
        std::vector<int> ownerOf(rs.particleCount(), -1);
        for (size_t li = 0; li < w.loops.size(); ++li)
            for (int i = w.loops[li].p0; i <= w.loops[li].p1; ++i) ownerOf[w.strands[w.loops[li].strand].first + i] = (int)li;
        struct P { Real ratio; int a, b; };
        std::vector<P> worst;
        for (int a = 0; a < S; ++a)
            for (int b = a + 1; b < S; ++b) {
                if (rs.segChain[a] == rs.segChain[b] && std::abs(rs.segIndex[a] - rs.segIndex[b]) <= 4) continue;
                const Real gap = rs.contactGap(a, b);
                if (gap > 0) continue;
                const SegSegResult r = closestSegmentSegment(rs.x[rs.segA[a]], rs.x[rs.segB[a]], rs.x[rs.segA[b]], rs.x[rs.segB[b]]);
                worst.push_back({std::sqrt(r.dist2) / (std::sqrt(r.dist2) - gap), a, b});
            }
        std::sort(worst.begin(), worst.end(), [](const P& x, const P& y) { return x.ratio < y.ratio; });
        for (size_t k = 0; k < worst.size() && k < 10; ++k) {
            const Vec3 pa = rs.x[rs.segA[worst[k].a]], pb = rs.x[rs.segA[worst[k].b]];
            std::printf("  worst %zu: %.2f of the width at (%.1f %.1f %.1f) mm: loop %d (seg %d) vs loop %d (seg %d)\n", k, worst[k].ratio, pa.x * 1e3, pa.y * 1e3, pa.z * 1e3, ownerOf[rs.segA[worst[k].a]], worst[k].a,
                        ownerOf[rs.segA[worst[k].b]], worst[k].b);
            (void)pb;
        }
    }
    if (smooth > 0) {   // let the yarn's own stiffness round the corners the lattice has, while it is held near where it is
        for (Vec3& d : rs.restDarboux) d = Vec3{};
        rs.params.shapeStiffness = smoothShape;
        if (smoothEI > 0) rs.params.EI = smoothEI;
        rs.setAnchorsToShape();
        for (int k = 0; k < smooth; ++k) rs.step(1.0 / 120.0);
        Real mr2 = 0;
        std::printf("smoothed (%d steps): close(<95%%) %d, closest %.2f\n", smooth, countClose(rs, 0.95, &mr2), mr2);
    }
    if (squeeze > 0) {   // pull the stitches closer under contact with the yarn's length fixed: the shape memory's anchors are scaled about the middle in the fabric plane
        Vec3 c0{};
        for (const Vec3& q : rs.x) c0 += q;
        c0 = c0 / (Real)rs.x.size();
        const std::vector<Vec3> a0 = rs.x;
        rs.params.shapeStiffness = sshape2;
        Real fx = 1.0, fz = 1.0;
        for (int st = 0; st < squeeze; ++st) {
            fx *= sf;
            fz *= zf;
            for (size_t i = 0; i < a0.size(); ++i) {
                const Vec3 d = a0[i] - c0;
                rs.anchor[i] = c0 + Vec3{d.x * fx, d.y * fx, d.z * fz};
            }
            for (int k = 0; k < tsteps; ++k) rs.step(1.0 / 120.0);
            if (st % 4 == 3 || st == squeeze - 1) {
                Vec3 lo(1e300, 1e300, 1e300), hi(-1e300, -1e300, -1e300);
                Real maxStrain = 0;
                for (int sgi = 0; sgi < rs.segmentCount(); ++sgi) maxStrain = std::max(maxStrain, std::fabs(length(rs.x[rs.segB[sgi]] - rs.x[rs.segA[sgi]]) / rs.restLen[sgi] - 1.0));
                for (const Vec3& q : rs.x) {
                    lo = Vec3(std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z));
                    hi = Vec3(std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z));
                }
                std::printf("squeeze %d (xy x%.2f): close(<95%%) %d, closest %.2f, strain max %.2f, extent %.0f x %.0f x %.0f mm\n", st + 1, fx, countClose(rs, 0.95, &mr), mr, maxStrain, (hi.x - lo.x) * 1e3,
                            (hi.y - lo.y) * 1e3, (hi.z - lo.z) * 1e3);
            }
        }
    }
    if (tighten > 0) {   // pull the yarn tight: every strand's rest length a little shorter at a time, under contact, until it jams
        if (straight) for (Vec3& d : rs.restDarboux) d = Vec3{};
        rs.params.shapeStiffness = 0.0;
        Real total = 1.0;
        for (int st = 0; st < tighten; ++st) {
            rs.scaleRestLengths(tf);
            total *= tf;
            for (int k = 0; k < tsteps; ++k) rs.step(1.0 / 120.0);
            if (st % 5 == 4 || st == tighten - 1) {
                Real maxStrain = 0, meanStrain = 0;
                for (int sgi = 0; sgi < rs.segmentCount(); ++sgi) {
                    const Real e = length(rs.x[rs.segB[sgi]] - rs.x[rs.segA[sgi]]) / rs.restLen[sgi] - 1.0;
                    maxStrain = std::max(maxStrain, e);
                    meanStrain += e;
                }
                Vec3 lo(1e300, 1e300, 1e300), hi(-1e300, -1e300, -1e300);
                for (const Vec3& p : rs.x) {
                    lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
                    hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
                }
                std::printf("tighten %d (lengths x%.2f): close(<95%%) %d, closest %.2f, strain max %.2f mean %.3f, extent %.0f x %.0f x %.0f mm\n", st + 1, total, countClose(rs, 0.95, &mr), mr, maxStrain,
                            meanStrain / std::max(1, rs.segmentCount()), (hi.x - lo.x) * 1e3, (hi.y - lo.y) * 1e3, (hi.z - lo.z) * 1e3);
            }
        }
    }
    // the topology: every finger through its loops once each way?
    YarnWork w2 = w;
    for (Strand& s : w2.strands)
        for (size_t i = 0; i < s.pts.size(); ++i) s.pts[i] = rs.x[s.first + i];
    int bad = 0, total = 0, badBefore = 0;
    for (const auto& t : threadingReport(w2)) {
        if (t[0] <= 1) continue;
        ++total;
        if (t[2] != 1 || t[3] != 1) ++bad;
    }
    for (const auto& t : threadingReport(w)) {
        if (t[0] <= 1) continue;
        if (t[2] != 1 || t[3] != 1) ++badBefore;
    }
    {
        const auto A = threadingReport(w), B = threadingReport(w2);
        for (size_t i = 0; i < A.size() && i < B.size(); ++i)
            if (A[i][0] > 1) std::printf("  loop %d through loop %d: before %d up %d down; after %d up %d down\n", A[i][0], A[i][1], A[i][2], A[i][3], B[i][2], B[i][3]);
    }
    std::printf("threading after: %d of %d clean (before: %d of %d)\n", total - bad, total, total - badBefore, total);
    w2.rod = rs;
    YarnMeshOptions mo;
    mo.segmentColour = w2.segmentColours();
    mo.ribbonWidth = ribA;
    mo.ribbonThickness = ribB;
    const YarnMesh mesh = buildYarnMesh(rs, mo);
    Vec3 lo(1e300, 1e300, 1e300), hi(-1e300, -1e300, -1e300);
    for (const Vec3& p : rs.x) {
        lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
        hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
    }
    YarnCamera cam;
    cam.target = (lo + hi) * 0.5;
    cam.fovDeg = 30;
    cam.distance = std::max(0.03, length(hi - lo)) * 1.5;
    cam.dir = Vec3{0, 0, 1};
    renderYarnPng(out + "_front.png", mesh, cam, 900, 700);
    cam.dir = normalized(Vec3{0.6, 0.5, 0.9});
    renderYarnPng(out + "_oblique.png", mesh, cam, 900, 700);
    return 0;
}
