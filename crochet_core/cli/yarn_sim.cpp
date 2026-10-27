// yarn_sim: the process simulator (docs/YARN_MODE_PLAN.md section 14) run from the command line: the slip knot made by the hook, with a picture
// every few frames and the measures (the smallest gap between strands, the stretch, how far the yarn is inside the hook).
//   yarn_sim [--out prefix] [--every n] [--rad mm]
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "failoring/crochet/yarn_render.hpp"
#include "failoring/crochet/yarn_sim.hpp"

using namespace fl;

namespace {

YarnMesh sceneMesh(const YarnSim& sim) {
    YarnMeshOptions mo;
    mo.colour = Vec3{0.55, 0.62, 0.85};
    YarnMesh m = buildYarnMesh(sim.rs, mo);
    if (sim.shape.sdf) {
        static HookPose cachedPose;
        static YarnMesh cached;
        static bool have = false;
        if (!have || length(cachedPose.tip - sim.hook.tip) > 1e-9 || std::fabs(cachedPose.q.w - sim.hook.q.w) > 1e-12 ||
            std::fabs(cachedPose.q.x - sim.hook.q.x) > 1e-12 || std::fabs(cachedPose.q.y - sim.hook.q.y) > 1e-12) {
            cached = hookMesh(sim.hook, sim.shape, 0.2e-3);
            cachedPose = sim.hook;
            have = true;
        }
        const int off = (int)m.pos.size();
        m.pos.insert(m.pos.end(), cached.pos.begin(), cached.pos.end());
        m.nrm.insert(m.nrm.end(), cached.nrm.begin(), cached.nrm.end());
        m.col.insert(m.col.end(), cached.col.begin(), cached.col.end());
        for (const auto& t : cached.tri) m.tri.push_back({t[0] + off, t[1] + off, t[2] + off});
    }
    YarnMeshOptions ho;
    ho.plyContrast = 0.0;
    ho.sides = 16;
    for (const StaticCapsule& c : sim.rs.statics) {
        std::vector<Vec3> pts;
        for (int i = 0; i <= 4; ++i) pts.push_back(c.a + (c.b - c.a) * (i / 4.0));
        if (length(c.b - c.a) < 1e-9) pts = {c.a, c.a + Vec3{1e-6, 0, 0}};
        const YarnMesh hm = buildTubeMesh(pts, c.r, Vec3{0.35, 0.72, 0.78}, ho);
        const int off = (int)m.pos.size();
        m.pos.insert(m.pos.end(), hm.pos.begin(), hm.pos.end());
        m.nrm.insert(m.nrm.end(), hm.nrm.begin(), hm.nrm.end());
        m.col.insert(m.col.end(), hm.col.begin(), hm.col.end());
        for (const auto& t : hm.tri) m.tri.push_back({t[0] + off, t[1] + off, t[2] + off});
    }
    return m;
}

}  // namespace

int main(int argc, char** argv) {
    std::string out = "sim";
    int every = 30;
    Real rad = 1.1, segMm = 1.0, topDist = 0.07, massScale = 100, damp = 20;
    int fa = -1, fb = -2, fstep = 1, chains = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string k = argv[i];
        if (k == "--out") out = argv[i + 1];
        else if (k == "--every") every = std::atoi(argv[i + 1]);
        else if (k == "--rad") rad = std::atof(argv[i + 1]);
        else if (k == "--seg") segMm = std::atof(argv[i + 1]);
        else if (k == "--top") topDist = std::atof(argv[i + 1]);
        else if (k == "--close") std::sscanf(argv[i + 1], "%d-%d/%d", &fa, &fb, &fstep);
        else if (k == "--chains") chains = std::atoi(argv[i + 1]);
        else if (k == "--mass") massScale = std::atof(argv[i + 1]);
        else if (k == "--damp") damp = std::atof(argv[i + 1]);
    }
    YarnSim sim;
    // The hook's shape: SIM_SDF=1 is the smooth barbed hook; the capsule hook is kept for comparing with the old tests.
    if (std::getenv("SIM_SDF")) sim.shape.sdf = std::atof(std::getenv("SIM_SDF")) > 0;
    if (std::getenv("SIM_SQUASH")) sim.shape.squash = std::atof(std::getenv("SIM_SQUASH"));
    if (std::getenv("SIM_BARBW")) sim.shape.barbHalfWidth = std::atof(std::getenv("SIM_BARBW"));
    if (std::getenv("SIM_BARBX")) sim.shape.barbX = std::atof(std::getenv("SIM_BARBX"));
    if (std::getenv("SIM_BARBR")) sim.shape.barbR = std::atof(std::getenv("SIM_BARBR"));
    if (std::getenv("SIM_HEADR")) sim.shape.headR = std::atof(std::getenv("SIM_HEADR"));
    if (std::getenv("SIM_NECKX")) sim.shape.neckX = std::atof(std::getenv("SIM_NECKX"));
    if (std::getenv("SIM_NECKR")) sim.shape.neckR = std::atof(std::getenv("SIM_NECKR"));
    if (std::getenv("SIM_FLATBACK")) sim.shape.flatBack = std::atoi(std::getenv("SIM_FLATBACK"));
    if (std::getenv("SIM_HOOKSHOT")) {   // the hook alone, close up: from the throat side, from the side, from above, at a slant
        const HookPose hp = hookPose(Vec3{}, Vec3{-1, 0, 0}, Vec3{0, 1, 0});
        const YarnMesh hm = hookMesh(hp, sim.shape, 0.1e-3);
        YarnCamera cam;
        cam.target = Vec3{0.007, 0, 0};
        cam.fovDeg = 30;
        cam.distance = 0.03;
        const Vec3 dirs[4] = {Vec3{0, 1, 0}, Vec3{0, 0, 1}, normalized(Vec3{-0.4, 0.6, 0.7}), normalized(Vec3{0.5, 0.5, 0.7})};
        for (int v = 0; v < 4; ++v) {
            cam.dir = dirs[v];
            cam.up = v == 0 ? Vec3{0, 0, -1} : Vec3{0, 1, 0};
            char name[256];
            std::snprintf(name, sizeof name, "%s_hook%d.png", out.c_str(), v);
            renderYarnPng(name, hm, cam, 600, 600);
        }
        std::printf("%zu triangles\n", hm.tri.size());
        return 0;
    }
    if (std::getenv("SIM_CARRY")) {
        // the hook catches a straight yarn held at both ends (fed from both, loosely) and pulls a bight 25 mm
        sim.params.radius = rad * 1e-3;
        sim.params.massScale = massScale;
        sim.params.damping = damp;
        std::vector<Vec3> line;
        const Real lo = std::getenv("SIM_LO") ? std::atof(std::getenv("SIM_LO")) * 1e-3 : 0.03;
        for (int i = 0; i <= 10; ++i) line.push_back(Vec3{0, -lo + (lo + 0.03) * i / 10.0, 0});
        sim.build(line);
        const Real R = sim.shape.R, m = 1e-3;
        const Real deep = -(std::getenv("SIM_DEEP") ? std::atof(std::getenv("SIM_DEEP")) : 3.84) * R;
        sim.hook = hookPose(Vec3{6 * m, 0, deep}, Vec3{0, 0, -1}, Vec3{-1, 0, 0});
        const Real endX = (std::getenv("SIM_CARRY_X") ? std::atof(std::getenv("SIM_CARRY_X")) : 0.4) * m;
        sim.moveHook(hookPose(Vec3{endX, 0, deep}, Vec3{0, 0, -1}, Vec3{-1, 0, 0}), 0.8);
        if (std::getenv("SIM_CARRY_SETTLE")) sim.settle(std::atoi(std::getenv("SIM_CARRY_SETTLE")));
        const int mid = (int)(lo / sim.params.segment);
        auto report = [&](const char* w) {
            const Vec3 T = sim.throatPoint();
            const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[mid] - sim.hook.tip) / R;
            int best = -1; Real bd = 1e9;
            for (int i = 0; i < sim.rs.particleCount(); ++i) {
                const Real d = length(sim.rs.x[i] - T);
                if (d < bd) { bd = d; best = i; }
            }
            const Vec3 bl = rotate(conj(sim.hook.q), sim.rs.x[best] - sim.hook.tip) / R;
            std::printf("%s: nearest to throat %.2f mm (p%d) local (%.2f %.2f %.2f) R, p30 local (%.2f %.2f %.2f), strain %.3f, in hook %.2f mm\n", w, bd * 1e3,
                        best, bl.x, bl.y, bl.z, l.x, l.y, l.z, sim.maxStrain(), sim.hookPenetration() * 1e3);
            for (int i = std::max(0, mid - 10); i <= std::min(sim.rs.particleCount() - 1, mid + 10); i += 2) {
                const Vec3 q = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip) / R;
                const Real d = hookSdf(q * R, sim.shape, nullptr) / R;
                std::printf("    p%d world x %.2f mm local (%.2f %.2f) R sdf %.2f R\n", i, sim.rs.x[i].x * 1e3, q.x, q.z, d);
            }
        };
        // pictures of the carry: the hook side-on from the mouth's side (the yarn across it), and from along the yarn (both arms of the bight)
        auto snap = [&](const char* tag) {
            const YarnMesh m = sceneMesh(sim);
            YarnCamera cam;
            cam.target = sim.hook.tip + Vec3{0, 0, 0.007};
            cam.fovDeg = 30;
            cam.distance = 0.06;
            char name[256];
            cam.dir = normalized(Vec3{-0.92, -0.2, 0.35});
            cam.up = Vec3{0, 0, 1};
            std::snprintf(name, sizeof name, "%s_%s_mouth.png", out.c_str(), tag);
            renderYarnPng(name, m, cam, 640, 640);
            cam.dir = normalized(Vec3{0.25, -0.9, 0.35});
            cam.up = Vec3{0, 0, 1};
            std::snprintf(name, sizeof name, "%s_%s_along.png", out.c_str(), tag);
            renderYarnPng(name, m, cam, 640, 640);
        };
        report("caught");
        snap("caught");
        sim.onFrame = [&] { if (sim.frame % 15 == 0) report("  pull"); };
        sim.moveHook(hookPose(Vec3{endX, 0, deep + 0.025}, Vec3{0, 0, -1}, Vec3{-1, 0, 0}), 1.5);
        report("pulled");
        snap("pulled");
        return 0;
    }
    sim.params.radius = rad * 1e-3;
    sim.params.segment = segMm * 1e-3;
    sim.params.massScale = massScale;
    sim.params.damping = damp;
    if (std::getenv("SIM_REEL")) std::sscanf(std::getenv("SIM_REEL"), "%lf,%lf", &sim.reelSlack, &sim.reelForce);
    if (std::getenv("SIM_RIM")) sim.shape.rim = true;
    if (std::getenv("SIM_GROOVE")) {
        sim.shape.groove = true;
        if (std::getenv("SIM_GX")) sim.shape.grooveX = std::atof(std::getenv("SIM_GX"));
        if (std::getenv("SIM_GZ")) sim.shape.grooveZ = std::atof(std::getenv("SIM_GZ"));
        if (std::getenv("SIM_GR")) sim.shape.grooveR = std::atof(std::getenv("SIM_GR"));
        if (!std::getenv("SIM_TX")) sim.throatX = sim.shape.grooveX;
        if (!std::getenv("SIM_TZ")) sim.throatZ = sim.shape.grooveZ;
    }
    if (std::getenv("SIM_TX")) sim.throatX = std::atof(std::getenv("SIM_TX"));
    if (std::getenv("SIM_TZ")) sim.throatZ = std::atof(std::getenv("SIM_TZ"));
    if (std::getenv("SIM_THK")) sim.throatStiffness = std::atof(std::getenv("SIM_THK"));
    if (std::getenv("SIM_SDFDBG")) {
        const Real R = sim.shape.R;
        for (Real x = 0; x <= 1.6; x += 0.1)
            std::printf("sdf x %.2fR at y0 z%.2fR: %.3f R\n", x, sim.shape.grooveZ, hookSdf(Vec3{x * R, 0, sim.shape.grooveZ * R}, sim.shape, nullptr) / R);
        for (Real z = 0.8; z <= 2.4; z += 0.1)
            std::printf("sdf z %.2fR at x%.2fR y0: %.3f R\n", z, sim.shape.grooveX, hookSdf(Vec3{sim.shape.grooveX * R, 0, z * R}, sim.shape, nullptr) / R);
    }
    if (std::getenv("SIM_HMU")) sim.params.hookFriction = std::atof(std::getenv("SIM_HMU"));
    if (std::getenv("SIM_SUB")) sim.params.substeps = std::atoi(std::getenv("SIM_SUB"));
    if (std::getenv("SIM_IT")) sim.params.iterations = std::atoi(std::getenv("SIM_IT"));
    if (std::getenv("SIM_MU")) sim.params.friction = std::atof(std::getenv("SIM_MU"));
    // (radiusScale is applied after build: YarnSim::build resets it)
    const bool rig = std::getenv("SIM_RIG") != nullptr;
    sim.rig = rig;
    if (rig) buildLoopRig(sim, Vec3{-0.02, 0, 0.02});
    else sim.build(slipKnotLayout(sim.params.radius));
    if (std::getenv("SIM_RSCALE")) sim.rs.params.radiusScale = std::atof(std::getenv("SIM_RSCALE"));
    std::printf("%d particles\n", sim.rs.particleCount());
    int shot = 0;
    Real worstGap = 1e9, worstStrain = 0, worstPen = 0;
    const auto t0 = std::chrono::steady_clock::now();
    auto snap = [&](const char* tag) {
        const YarnMesh m = sceneMesh(sim);
        YarnCamera cam;
        cam.target = std::getenv("SIM_FOLLOW") ? sim.hook.tip + sim.hook.axis() * 0.012 - Vec3{0, 0.012, 0} : Vec3{0, -0.002, 0.004};
        cam.fovDeg = 30;
        cam.distance = 0.12;
        if (std::getenv("SIM_SHOTDIST")) cam.distance = std::atof(std::getenv("SIM_SHOTDIST"));
        if (std::getenv("SIM_SHOTTARGET")) {
            double x = 0, y = 0, z = 0;
            if (std::sscanf(std::getenv("SIM_SHOTTARGET"), "%lf,%lf,%lf", &x, &y, &z) == 3) cam.target = Vec3{x, y, z};
        }
        if (std::getenv("SIM_SHOTYARN")) {   // frame the whole work, not the hook at the origin
            Vec3 lo{1e9, 1e9, 1e9}, hi{-1e9, -1e9, -1e9};
            for (const Vec3& p : sim.rs.x) {
                lo = Vec3{std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
                hi = Vec3{std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
            }
            if (sim.rs.particleCount() > 0) {
                cam.target = (lo + hi) * 0.5;
                const Real ext = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
                cam.distance = std::max(cam.distance, ext / (2 * std::tan(cam.fovDeg * kPi / 360)) * 1.15);
            }
        }
        cam.dir = Vec3{0, 0, 1};
        char name[256];
        std::snprintf(name, sizeof name, "%s_%03d_%s_front.png", out.c_str(), shot, tag);
        renderYarnPng(name, m, cam, 640, 640);
        cam.dir = normalized(Vec3{0.6, 0.35, 0.75});
        std::snprintf(name, sizeof name, "%s_%03d_%s_obl.png", out.c_str(), shot, tag);
        renderYarnPng(name, m, cam, 640, 640);
        cam.dir = Vec3{0, 1, 0};
        cam.up = Vec3{0, 0, -1};
        cam.distance = topDist;
        cam.target = Vec3{0.001, 0.001, -0.004};
        std::snprintf(name, sizeof name, "%s_%03d_%s_top.png", out.c_str(), shot, tag);
        renderYarnPng(name, m, cam, 640, 640);
        ++shot;
    };
    std::string lastPhase;
    int windFrom = std::getenv("SIM_WIND") ? std::atoi(std::getenv("SIM_WIND")) : -1;
    sim.onFrame = [&] {
        if (windFrom >= 0 && sim.frame >= windFrom && sim.frame % 6 == 0) {
            const int n = sim.rs.particleCount();
            std::printf("  wind f%d [%s] all %.2f  last60 %.2f  last40 %.2f\n", sim.frame, sim.phase.substr(0, 28).c_str(), sim.windingNear(0, n - 1),
                        sim.windingNear(n - 60, n - 1), sim.windingNear(n - 40, n - 1));
        }
        if (std::getenv("SIM_PHASESHOT") && sim.phase != lastPhase && sim.frame > std::atoi(std::getenv("SIM_PHASESHOT"))) {
            snap("phase");
            std::printf("  phase shot %d at frame %d: %s\n", shot - 1, sim.frame, sim.phase.c_str());
        }
        lastPhase = sim.phase;
        const Real g = sim.minGap(), e = sim.maxStrain(), p = sim.hookPenetration();
        const bool contactDbg = std::getenv("SIM_CONTACT") != nullptr;
        if (contactDbg && g < worstGap) {
            int ba = -1, bb = -1;
            Real bd = 1e300;
            const int S = sim.rs.segmentCount();
            for (int a = 0; a < S; ++a)
                for (int b = a + 1; b < S; ++b) {
                    if (sim.rs.segChain[a] == sim.rs.segChain[b]) {
                        int d = std::abs(sim.rs.segIndex[a] - sim.rs.segIndex[b]);
                        const RodChain& ch = sim.rs.chains[sim.rs.segChain[a]];
                        if (ch.closed) d = std::min(d, ch.segments - d);
                        if (d <= sim.rs.params.contactSkipSegments + 1) continue;
                    }
                    const SegSegResult r = closestSegmentSegment(sim.rs.x[sim.rs.segA[a]], sim.rs.x[sim.rs.segB[a]], sim.rs.x[sim.rs.segA[b]], sim.rs.x[sim.rs.segB[b]]);
                    if (r.dist2 < bd) { bd = r.dist2; ba = a; bb = b; }
                }
            std::printf("  contact gap %.3f mm at frame %d (%s): seg %d p%d..%d and seg %d p%d..%d\n", g * 1e3, sim.frame, sim.phase.c_str(), ba,
                        sim.rs.segA[ba], sim.rs.segB[ba], bb, sim.rs.segA[bb], sim.rs.segB[bb]);
        }
        if (g < worstGap && g < 1.9e-3) std::printf("  closer gap %.2f mm at frame %d (%s)\n", g * 1e3, sim.frame, sim.phase.c_str());
        worstGap = std::min(worstGap, g);
        if (e > worstStrain) {
            worstStrain = e;
            int ws = 0; Real wv = 0;
            for (int s = 0; s < sim.rs.segmentCount(); ++s) {
                const Real v = std::fabs(length(sim.rs.x[sim.rs.segB[s]] - sim.rs.x[sim.rs.segA[s]]) / sim.rs.restLen[s] - 1.0);
                if (v > wv) { wv = v; ws = s; }
            }
            if (e > 0.05) std::printf("  worse strain %.3f at frame %d, segment %d\n", e, sim.frame, ws);
        }
        if (contactDbg && p > worstPen) {
            int ws = -1, wk = -1;
            Real wp = 0;
            for (int s2 = 0; s2 < sim.rs.segmentCount(); ++s2)
                for (int k = 0; k <= 2; ++k) {
                    const Vec3 P = sim.rs.x[sim.rs.segA[s2]] + (sim.rs.x[sim.rs.segB[s2]] - sim.rs.x[sim.rs.segA[s2]]) * (0.5 * k);
                    const Real v = sim.params.radius * sim.shape.squash - hookSdf(rotate(conj(sim.hook.q), P - sim.hook.tip), sim.shape, nullptr);
                    if (v > wp) { wp = v; ws = s2; wk = k; }
                }
            std::printf("  contact pen %.3f mm at frame %d (%s): seg %d p%d..%d k%d\n", wp * 1e3, sim.frame, sim.phase.c_str(), ws, sim.rs.segA[ws],
                        sim.rs.segB[ws], wk);
        }
        worstPen = std::max(worstPen, p);
        if (std::getenv("SIM_DEBUG") && sim.frame % 6 == 0) {
            // the yarn nearest the throat, in the hook's own frame (in shaft radii)
            const Real R = sim.shape.R;
            int best = -1; Real bd = 1e9; Vec3 bl{};
            for (int i = 0; i < sim.rs.particleCount(); ++i) {
                const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip) / R;
                const Real d = length(l - Vec3{0.45, 0, 3.4});
                if (d < bd) { bd = d; best = i; bl = l; }
            }
            std::printf("  f%d tip(%.1f %.1f %.1f) nearest p%d local(%.2f %.2f %.2f) d %.2f\n", sim.frame, sim.hook.tip.x * 1e3, sim.hook.tip.y * 1e3,
                        sim.hook.tip.z * 1e3, best, bl.x, bl.y, bl.z, bd);
        }
        if (std::getenv("SIM_LOCAL") && (sim.frame == 176 || sim.frame == 180 || sim.frame == 184)) {
            std::printf("  f%d local (R):", sim.frame);
            for (int i = 128; i <= 162; i += 2) {
                const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip) / sim.shape.R;
                std::printf(" p%d(%.2f %.2f %.2f)", i, l.x, l.y, l.z);
            }
            std::printf("\n");
            for (int c = 0; c < 4; ++c) {
                const StaticCapsule& cp = sim.rs.statics[c];
                const SegSegResult r = closestSegmentSegment(sim.rs.x[143], sim.rs.x[144], cp.a, cp.b);
                std::printf("    capsule %d: seg143 s %.2f dist-need %.2f mm\n", c, r.s, (std::sqrt(r.dist2) - cp.r - sim.params.radius) * 1e3);
            }
        }
        if (std::getenv("SIM_GRIPS") && sim.frame == 20) {
            for (const auto& g : sim.grips) { std::printf("  grip:"); for (int i : g.particles) std::printf(" %d", i); std::printf("\n"); }
        }
        if (std::getenv("SIM_NEAR") && sim.frame == std::atoi(std::getenv("SIM_NEAR"))) {
            const int P = 144;
            for (int i = P - 3; i <= P + 3; ++i) {
                const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip) / sim.shape.R;
                std::printf("  p%d local (%.2f %.2f %.2f) w %.3g\n", i, l.x, l.y, l.z, sim.rs.w[i]);
            }
            for (int s = 0; s < sim.rs.segmentCount(); ++s) {
                if (std::abs(s - P) <= 3) continue;
                const SegSegResult r = closestSegmentSegment(sim.rs.x[P], sim.rs.x[P + 1], sim.rs.x[sim.rs.segA[s]], sim.rs.x[sim.rs.segB[s]]);
                if (r.dist2 < 9e-6) std::printf("  near segment %d: %.2f mm\n", s, std::sqrt(r.dist2) * 1e3);
            }
            for (size_t c = 0; c < sim.rs.statics.size(); ++c) {
                const StaticCapsule& cp = sim.rs.statics[c];
                const SegSegResult r = closestSegmentSegment(sim.rs.x[P], sim.rs.x[P + 1], cp.a, cp.b);
                std::printf("  capsule %zu: s %.2f clearance %.2f mm\n", c, r.s, (std::sqrt(r.dist2) - cp.r - sim.params.radius * cp.squash) * 1e3);
            }
        }
        if (std::getenv("SIM_STRAIN") && sim.frame % 20 == 0) {
            int sStart = 0, sEnd = 1 << 30, sStep = 10;
            std::sscanf(std::getenv("SIM_STRAIN"), "%d-%d/%d", &sStart, &sEnd, &sStep);
            std::printf("  f%d strain by segment (every 10th):", sim.frame);
            for (int s = sStart; s < sim.rs.segmentCount() && s < sEnd; s += sStep)
                std::printf(" %d:%.4f", s, length(sim.rs.x[sim.rs.segB[s]] - sim.rs.x[sim.rs.segA[s]]) / sim.rs.restLen[s] - 1.0);
            std::printf("\n");
        }
        if (sim.frame >= fa && sim.frame <= fb && (sim.frame - fa) % fstep == 0) {
            // a close-up at the throat
            const YarnMesh m = sceneMesh(sim);
            YarnCamera cam;
            static Vec3 fixedTarget{};
            static int fixedFrom = -1;
            if (fixedFrom < 0) { fixedFrom = sim.frame; fixedTarget = sim.hook.tip + sim.hook.axis() * (6.0 * sim.shape.R); }
            cam.target = std::getenv("SIM_CFIX") ? fixedTarget : sim.hook.tip + sim.hook.axis() * (3.0 * sim.shape.R);
            cam.fovDeg = 30;
            cam.distance = std::getenv("SIM_CDIST") ? std::atof(std::getenv("SIM_CDIST")) : 0.03;
            // views in the hook's frame: from the throat side, from the side, and between
            const Vec3 th = sim.hook.throat(), ax = sim.hook.axis(), sd = cross(ax, th);
            Vec3 views[3] = {th, sd, normalized(th + sd * 0.8 - ax * 0.5)};
            if (std::getenv("SIM_CFIX")) { views[0] = Vec3{0, 0, 1}; views[1] = normalized(Vec3{-1, 0.05, 0.1}); views[2] = normalized(Vec3{0, 1, 0.02}); }
            const char* vn[3] = {"throat", "side", "obl"};
            for (int v = 0; v < 3; ++v) {
                cam.dir = views[v];
                cam.up = std::getenv("SIM_CFIX") ? (v == 2 ? Vec3{0, 0, -1} : Vec3{0, 1, 0}) : (v == 1 ? th : sd);
                char name[256];
                std::snprintf(name, sizeof name, "%s_c%04d_%s.png", out.c_str(), sim.frame, vn[v]);
                renderYarnPng(name, m, cam, 480, 480);
            }
        }
        if (every > 0 && sim.frame % every == 0) {
            std::printf("frame %4d: gap %.2f mm (2r %.2f)  strain %.3f  in hook %.2f mm  particles %d  loops %d\n", sim.frame, g * 1e3, 2 * rad, e,
                        p * 1e3, sim.rs.particleCount(), sim.loopsOnHook(Vec3{0, 0, 1}));
            std::fflush(stdout);
            snap("f");
        }
    };
    snap("start");
    if (std::getenv("SIM_DEBUG")) for (int i = 136; i <= 150; ++i) std::printf("  p%d (%.1f %.1f %.1f) w %.3g\n", i, sim.rs.x[i].x * 1e3, sim.rs.x[i].y * 1e3, sim.rs.x[i].z * 1e3, sim.rs.w[i]);
    if (!rig) makeSlipKnot(sim);
    auto loopSize = [&] {
        const Vec3 ax = sim.hook.axis();
        Real mx = 0;
        for (const Vec3& p : sim.rs.x) {
            const Vec3 d = p - sim.hook.tip;
            const Real a = dot(d, ax);
            if (a < 0 || a > 0.04) continue;
            const Real rr = length(d - ax * a);
            if (rr < 0.008) mx = std::max(mx, rr);
        }
        return mx;
    };
    if (std::getenv("SIM_LOOPDBG")) {
        for (int i = 0; i < sim.rs.particleCount(); ++i) {
            const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip);
            const Real rr = std::hypot(l.x, l.y);
            if (rr < 0.01 && l.z > -0.005 && l.z < 0.06) std::printf("  p%d z %.1f r %.1f ang %.0f\n", i, l.z * 1e3, rr * 1e3, std::atan2(l.y, l.x) * 180 / kPi);
        }
    }
    std::printf("loop reaches %.1f mm from the axis; the yarn closes %d degrees round the shaft when pulled\n", loopSize() * 1e3, carriedRound(sim));
    if (std::getenv("SIM_SNUG")) {
        const bool tail = std::string(std::getenv("SIM_SNUG")) == "tail";
        const Vec3 t0 = sim.tail.target;
        for (int k = 1; k <= 90; ++k) {
            if (tail) sim.tail.target = t0 + Vec3{0, -0.02, 0} * (k / 90.0);
            else sim.ball.tension = 0.6;
            sim.step(sim.hook);
            if (sim.onFrame) sim.onFrame();
        }
        sim.settle(30);
        std::printf("after pulling the %s: loop reaches %.1f mm\n", tail ? "tail" : "working yarn", loopSize() * 1e3);
    }
    if (std::getenv("SIM_PULLTEST")) {
        // the pull-through tried from one start: SIM_PULLTEST="NAME=v1,v2;NAME2=..." sweeps environment knobs of finishChain
        for (int k = 0; k < (std::getenv("SIM_PRECHAINS") ? std::atoi(std::getenv("SIM_PRECHAINS")) : 0); ++k) makeChain(sim);
        sim.onFrame = nullptr;
        std::vector<std::pair<std::string, std::vector<std::string>>> axes;
        std::string spec = std::getenv("SIM_PULLTEST");
        size_t pos = 0;
        while (pos < spec.size()) {
            size_t e = spec.find(';', pos);
            if (e == std::string::npos) e = spec.size();
            const std::string part = spec.substr(pos, e - pos);
            const size_t eq = part.find('=');
            if (eq != std::string::npos) {
                std::vector<std::string> vals;
                size_t q = eq + 1;
                while (q <= part.size()) {
                    size_t c2 = part.find(',', q);
                    if (c2 == std::string::npos) c2 = part.size();
                    vals.push_back(part.substr(q, c2 - q));
                    q = c2 + 1;
                }
                axes.push_back({part.substr(0, eq), vals});
            }
            pos = e + 1;
        }
        std::vector<int> idx(axes.size(), 0);
        std::vector<std::vector<int>> combos;
        while (true) {
            combos.push_back(idx);
            size_t k = 0;
            for (; k < axes.size(); ++k) {
                if (++idx[k] < (int)axes[k].second.size()) break;
                idx[k] = 0;
            }
            if (k == axes.size()) break;
        }
        auto runOne = [&](const std::vector<int>& pick, std::string* out) {
            std::string label;
            for (size_t k = 0; k < axes.size(); ++k) {
                setenv(axes[k].first.c_str(), axes[k].second[pick[k]].c_str(), 1);
                label += axes[k].first + "=" + axes[k].second[pick[k]] + " ";
            }
            YarnSim t = sim;
            t.stageLog.clear();
            // Hook friction is normally read once from the command line; the sweep axis may set it too, so the child has to take it
            // into the copy. The solver copies params.hookFriction onto the hook body at every step.
            if (const char* hmu = std::getenv("SIM_HMU")) t.params.hookFriction = std::atof(hmu);
            const ChainStart c0 = prepareChain(t);
            finishChain(t, c0);
            const ChainResult r = judgeChain(t, c0);
            char line[1024];
            std::snprintf(line, sizeof line, "%s-> %d loops%s   [%s]\n", label.c_str(), r.loops, r.made ? "  MADE" : "", t.stageLog.c_str());
            *out = line;
        };
        // Each combination is independent from the same saved start. Run them as separate processes (fork) so the
        // machine's cores are used without making the solver itself threaded; the child's environment is its own, so
        // setenv is safe. The default is one at a time, as before.
        const int jobs = std::max(1, std::min(16, (int)(std::getenv("SIM_JOBS") ? std::atoi(std::getenv("SIM_JOBS")) : 1)));
#if defined(__unix__) || defined(__APPLE__)
        if (jobs > 1) {
            std::fflush(stdout);   // do not duplicate buffered output in every child
            for (size_t base = 0; base < combos.size(); base += (size_t)jobs) {
                const size_t end = std::min(combos.size(), base + (size_t)jobs);
                std::vector<int> fds;
                std::vector<pid_t> pids;
                for (size_t j = base; j < end; ++j) {
                    int fd[2];
                    if (pipe(fd) != 0) {
                        std::printf("SIM_PULLTEST: pipe failed\n");
                        continue;
                    }
                    const pid_t pid = fork();
                    if (pid == 0) {
                        close(fd[0]);
                        if (!std::getenv("SIM_PULLTEST_VERBOSE")) (void)std::freopen("/dev/null", "w", stdout);
                        std::string line;
                        runOne(combos[j], &line);
                        (void)write(fd[1], line.data(), line.size());
                        close(fd[1]);
                        _exit(0);
                    }
                    if (pid < 0) {
                        close(fd[0]);
                        close(fd[1]);
                        std::printf("SIM_PULLTEST: fork failed\n");
                        continue;
                    }
                    close(fd[1]);
                    fds.push_back(fd[0]);
                    pids.push_back(pid);
                }
                for (size_t k = 0; k < fds.size(); ++k) {
                    std::string out;
                    char buf[1024];
                    ssize_t n = 0;
                    while ((n = read(fds[k], buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
                    close(fds[k]);
                    int status = 0;
                    waitpid(pids[k], &status, 0);
                    if (out.empty()) std::printf("SIM_PULLTEST: a child failed\n");
                    else std::fputs(out.c_str(), stdout);
                }
                std::fflush(stdout);
            }
            return 0;
        }
#else
        (void)jobs;
#endif
        for (const auto& pick : combos) {
            std::string line;
            runOne(pick, &line);
            std::fputs(line.c_str(), stdout);
            std::fflush(stdout);
        }
        return 0;
    }
    std::vector<std::pair<int, int>> links;
    for (int c = 0; c < chains; ++c) {
        const ChainStart cs = prepareChain(sim);
        if (cs.ok) finishChain(sim, cs);
        const ChainResult r = judgeChain(sim, cs);
        if (r.made) {
            sim.lastLinkHi = r.linkHi;
            sim.lastLinkLo = r.linkLo;
            sim.lastLinkChain = sim.chains;
        }
        if (r.made) {
            links.push_back({r.linkLo, r.linkHi});
            if (links.size() >= 2) {
                const auto& a = links[links.size() - 2];
                const auto& b = links[links.size() - 1];
                std::vector<Vec3> A(sim.rs.x.begin() + a.first, sim.rs.x.begin() + a.second + 1);
                std::vector<Vec3> B(sim.rs.x.begin() + b.first, sim.rs.x.begin() + b.second + 1);
                std::printf("  chain link %d-%d: linking %.2f\n", (int)links.size() - 1, (int)links.size(), linkingNumber(A, B));
            }
        }
        std::string frontNote;
        if (r.frontLoops != r.loops) frontNote = " (front count " + std::to_string(r.frontLoops) + ")";
        std::printf("chain %d: %d loops%s on the hook (after the yarn over %d), yarn %.1f mm (was %.1f)%s\n", c + 1, r.loops, frontNote.c_str(),
                    cs.loopsAfterYo, r.length * 1e3, cs.length * 1e3, r.made ? "  MADE" : "");
        if (std::getenv("SIM_PATHDIR")) {
            char name[512];
            std::snprintf(name, sizeof name, "%s_ch%d.csv", std::getenv("SIM_PATHDIR"), c + 1);
            if (FILE* f = std::fopen(name, "w")) {
                const Vec3 ax = sim.hook.axis();
                std::fprintf(f, "# tip %.6f %.6f %.6f ax %.6f %.6f %.6f old %d %d link %d %d\n", sim.hook.tip.x, sim.hook.tip.y, sim.hook.tip.z,
                             ax.x, ax.y, ax.z, cs.oldLo, cs.oldHi, r.linkLo, r.linkHi);
                for (int i = 0; i < sim.rs.particleCount(); ++i)
                    std::fprintf(f, "%d,%.6f,%.6f,%.6f\n", i, sim.rs.x[i].x, sim.rs.x[i].y, sim.rs.x[i].z);
                std::fclose(f);
            }
        }
        if (std::getenv("SIM_CHAINSHOT")) { char tag[16]; std::snprintf(tag, sizeof tag, "ch%d", c + 1); snap(tag); }
        continue;
        std::printf("chain %d: loops on hook %d (up) %d (front)\n", c + 1, sim.loopsOnHook(Vec3{0, 1, 0}), sim.loopsOnHook(Vec3{0, 0, 1}));
    }
    snap("end");
    if (std::getenv("SIM_HOOKOFF")) {
        // Take the hook out of the work and let the yarn settle: a linked chain stays a chain; loops that were only hanging on
        // the hook fall straight and the yarn pulls out to the hand.
        sim.setThroatRing(false);
        sim.hook = hookPose(sim.hook.tip + Vec3{0, 0.25, 0}, -sim.hook.axis(), sim.hook.throat());
        sim.settle(150);
        snap("hookoff");
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("slip knot: %d frames in %.1f s; worst gap %.2f mm (2r %.2f), worst strain %.3f, worst in hook %.2f mm\n", sim.frame, secs,
                worstGap * 1e3, 2 * rad, worstStrain, worstPen * 1e3);
    return 0;
}
