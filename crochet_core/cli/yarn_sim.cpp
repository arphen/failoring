// yarn_sim: the process simulator (docs/YARN_MODE_PLAN.md section 14) run from the command line: the slip knot made by the hook, with a picture
// every few frames and the measures (the smallest gap between strands, the stretch, how far the yarn is inside the hook).
//   yarn_sim [--out prefix] [--every n] [--rad mm]
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "failoring/crochet/yarn_render.hpp"
#include "failoring/crochet/yarn_sim.hpp"

using namespace fl;

namespace {

YarnMesh sceneMesh(const YarnSim& sim) {
    YarnMeshOptions mo;
    mo.colour = Vec3{0.55, 0.62, 0.85};
    YarnMesh m = buildYarnMesh(sim.rs, mo);
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
    int fa = -1, fb = -2, fstep = 1;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string k = argv[i];
        if (k == "--out") out = argv[i + 1];
        else if (k == "--every") every = std::atoi(argv[i + 1]);
        else if (k == "--rad") rad = std::atof(argv[i + 1]);
        else if (k == "--seg") segMm = std::atof(argv[i + 1]);
        else if (k == "--top") topDist = std::atof(argv[i + 1]);
        else if (k == "--close") std::sscanf(argv[i + 1], "%d-%d/%d", &fa, &fb, &fstep);
        else if (k == "--mass") massScale = std::atof(argv[i + 1]);
        else if (k == "--damp") damp = std::atof(argv[i + 1]);
    }
    YarnSim sim;
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
        const Real deep = -sim.params.radius - 3.4 * R + 0.0;
        sim.hook = hookPose(Vec3{6 * m, 0, deep}, Vec3{0, 0, -1}, Vec3{-1, 0, 0});
        sim.moveHook(hookPose(Vec3{0.4 * m, 0, deep}, Vec3{0, 0, -1}, Vec3{-1, 0, 0}), 0.8);
        const int mid = (int)(lo / sim.params.segment);
        auto report = [&](const char* w) {
            const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[mid] - sim.hook.tip) / R;
            int best = -1; Real bd = 1e9;
            for (int i = 0; i < sim.rs.particleCount(); ++i) {
                const Real d = length(sim.rs.x[i] - (sim.hook.tip + sim.hook.axis() * (2.8 * R)));
                if (d < bd) { bd = d; best = i; }
            }
            std::printf("%s: nearest to throat %.2f mm (p%d), p30 local (%.2f %.2f %.2f), strain %.3f\n", w, bd * 1e3, best, l.x, l.y, l.z, sim.maxStrain());
        };
        report("caught");
        sim.onFrame = [&] { if (sim.frame % 15 == 0) report("  pull"); };
        sim.moveHook(hookPose(Vec3{0.4 * m, 0, deep + 0.025}, Vec3{0, 0, -1}, Vec3{-1, 0, 0}), 1.5);
        report("pulled");
        return 0;
    }
    sim.params.radius = rad * 1e-3;
    sim.params.segment = segMm * 1e-3;
    sim.params.massScale = massScale;
    sim.params.damping = damp;
    sim.build(slipKnotLayout(sim.params.radius));
    std::printf("%d particles\n", sim.rs.particleCount());
    int shot = 0;
    Real worstGap = 1e9, worstStrain = 0, worstPen = 0;
    const auto t0 = std::chrono::steady_clock::now();
    auto snap = [&](const char* tag) {
        const YarnMesh m = sceneMesh(sim);
        YarnCamera cam;
        cam.target = Vec3{0, -0.002, 0.004};
        cam.fovDeg = 30;
        cam.distance = 0.12;
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
    sim.onFrame = [&] {
        const Real g = sim.minGap(), e = sim.maxStrain(), p = sim.hookPenetration();
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
            cam.target = sim.hook.tip + sim.hook.axis() * (3.0 * sim.shape.R);
            cam.fovDeg = 30;
            cam.distance = std::getenv("SIM_CDIST") ? std::atof(std::getenv("SIM_CDIST")) : 0.03;
            // views in the hook's frame: from the throat side, from the side, and between
            const Vec3 th = sim.hook.throat(), ax = sim.hook.axis(), sd = cross(ax, th);
            const Vec3 views[3] = {th, sd, normalized(th + sd * 0.8 - ax * 0.5)};
            const char* vn[3] = {"throat", "side", "obl"};
            for (int v = 0; v < 3; ++v) {
                cam.dir = views[v];
                cam.up = v == 1 ? th : sd;
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
    makeSlipKnot(sim);
    snap("end");
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("slip knot: %d frames in %.1f s; worst gap %.2f mm (2r %.2f), worst strain %.3f, worst in hook %.2f mm\n", sim.frame, secs,
                worstGap * 1e3, 2 * rad, worstStrain, worstPen * 1e3);
    return 0;
}
