// yarn_presim: a bounded physical simulation of one crochet chain stitch (docs/YARN_MODE_PLAN.md): a hook (a moving capsule), a slip loop on it, the
// working yarn to the ball under tension; the yarn is laid over the hook (a yarn over, by a guide that pulls a point of the yarn round the shaft), the hook
// is drawn back through the loop carrying the new loop, and the work is drawn on. The shapes it settles into are what the design should look like; the
// frames are what the animation should look like.
//   yarn_presim [--out prefix]
// STATUS: experiment, not used by the editor. The yarn over and the hook drawn back run and look plausible, but once the loop is held (as the work holds it)
// and the bight is dragged through it the solver blows up (strain in the hundreds): physics from topology fails here as it did for the stitches
// themselves. The editor's pull-through is kinematic, driven by the design: YarnWork::pullFrame.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "failoring/crochet/rod.hpp"
#include "failoring/crochet/yarn_render.hpp"

using namespace fl;

namespace {

const Real kMm = 1e-3;

struct Sim {
    RodSystem rs;
    int tail = 0, work = 0;       // the pinned ends
    Real hookX = 0;               // the hook's tip along x
    Real hookR = 2.0 * kMm;
    int step = 0;
    void setHook() {
        rs.statics.clear();
        rs.statics.push_back({Vec3{hookX, 0, 0}, Vec3{hookX - 0.07, 0, 0}, hookR});
    }
    void run(int n) {
        for (int i = 0; i < n; ++i) { setHook(); rs.step(1.0 / 60.0); ++step; }
    }
    int nearest(const Vec3& p) const {
        int best = 0; Real bd = 1e300;
        for (int i = 0; i < rs.particleCount(); ++i) { const Real d = length2(rs.x[i] - p); if (d < bd) { bd = d; best = i; } }
        return best;
    }
};

void frame(const Sim& s, const std::string& prefix, const char* tag) {
    YarnMeshOptions mo;
    YarnMesh m = buildYarnMesh(s.rs, mo);
    // the hook as a grey tube
    std::vector<Vec3> hp;
    for (int i = 0; i <= 12; ++i) hp.push_back(Vec3{s.hookX - 0.07 * i / 12.0, 0, 0});
    YarnMeshOptions ho;
    ho.plyContrast = 0.0;
    const YarnMesh hm = buildTubeMesh(hp, s.hookR, Vec3{0.62, 0.64, 0.68}, ho);
    const int off = (int)m.pos.size();
    m.pos.insert(m.pos.end(), hm.pos.begin(), hm.pos.end());
    m.nrm.insert(m.nrm.end(), hm.nrm.begin(), hm.nrm.end());
    m.col.insert(m.col.end(), hm.col.begin(), hm.col.end());
    for (const auto& t : hm.tri) m.tri.push_back({t[0] + off, t[1] + off, t[2] + off});
    YarnCamera cam;
    cam.target = Vec3{s.hookX - 0.012, 0, 0};
    cam.fovDeg = 28;
    cam.distance = 0.085;
    cam.dir = normalized(Vec3{0.35, 0.55, 0.9});
    renderYarnPng(prefix + "_" + tag + "_obl.png", m, cam, 700, 520);
    cam.dir = Vec3{0, 0, 1};
    renderYarnPng(prefix + "_" + tag + "_front.png", m, cam, 700, 520);
}

Real maxStrain(const RodSystem& rs) {
    Real e = 0;
    for (int s = 0; s < rs.segmentCount(); ++s) e = std::max(e, std::fabs(length(rs.x[rs.segB[s]] - rs.x[rs.segA[s]]) / rs.restLen[s] - 1.0));
    return e;
}

}  // namespace

int main(int argc, char** argv) {
    std::string out = "presim";
    int cycles = 1;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string k = argv[i];
        if (k == "--out") out = argv[i + 1];
        else if (k == "--cycles") cycles = std::atoi(argv[i + 1]);
    }
    Sim sim;
    RodSystem& rs = sim.rs;
    rs.params.gravity = Vec3{};
    rs.params.radius = 1.25 * kMm;
    rs.params.EA = 30.0;
    rs.params.EI = 3.0e-6;
    rs.params.GJ = 3.0e-6;
    rs.params.damping = 40.0;
    rs.params.substeps = 4;
    rs.params.iterations = 2;
    rs.params.radiusScale = 0.7;
    rs.params.friction = 0.4;
    rs.params.shapeStiffness = 0.0;
    rs.setMassScale(1.0e4);
    rs.setRotationalInertiaScale(1.0e4);

    // the yarn: tail (pinned) -> a slip loop wrapped once round the shaft at x = -10 mm -> the working yarn out behind to the ball (pinned far away)
    std::vector<Vec3> pts;
    const int tailN = 12;
    for (int i = 0; i < tailN; ++i) pts.push_back(Vec3{(-0.040 + 0.030 * i / tailN), -0.0045, -0.0035});
    const Real rr = 5.5 * kMm;
    const int ringN = 20;
    for (int i = 0; i <= ringN; ++i) {
        const Real a = -0.5 * kPi + 2.0 * kPi * i / ringN;
        pts.push_back(Vec3{-0.010 + 0.0026 * i / ringN, rr * std::cos(a), rr * std::sin(a)});   // a loop bigger than the hook: loops stretch
    }
    // out behind (z negative) and away to the ball, with a little slack
    const Vec3 last = pts.back();
    for (int i = 1; i <= 40; ++i) {
        const Real t = i / 40.0;
        pts.push_back(last + Vec3{0.016 * t, -0.008 * std::sin(kPi * t) - 0.004 * t, -0.010 * t});
    }
    // resample to ~1.4 mm
    std::vector<Vec3> rp;
    rp.push_back(pts[0]);
    for (size_t i = 1; i < pts.size(); ++i) {
        const Real d = length(pts[i] - rp.back());
        if (d < 1.4 * kMm) continue;
        rp.push_back(pts[i]);
    }
    rs.addChain(rp, false, true);
    sim.tail = 0;
    sim.work = rs.particleCount() - 1;
    rs.pin(sim.tail);
    rs.pin(sim.work);
    // the work holds the loop: the ring's lowest points are held where they are (the loop sits in the previous stitch)
    for (int i = tailN; i <= tailN + 3; ++i) rs.pin(i);
    sim.hookX = 0.002;
    sim.setHook();
    sim.run(30);
    std::printf("settled: strain %.3f, %d particles\n", maxStrain(rs), rs.particleCount());
    frame(sim, out, "0_start");

    // yarn over: a point of the working yarn, 30 mm along from the loop, is laid over the shaft from behind to the front (half a turn, the yarn "over"),
    // then caught in the hook's throat (it rides with the hook), and the hook is drawn back through the loop: the loop slips off the tip, the bight stays
    int bi = sim.work;
    {
        Real acc = 0;
        int i = tailN + ringN;
        for (; i < rs.particleCount() - 1; ++i) { acc += length(rs.x[i + 1] - rs.x[i]); if (acc > 0.009) break; }
        bi = i;
    }
    const Vec3 start = rs.x[bi];
    std::printf("the bight is particle %d at (%.1f %.1f %.1f) mm\n", bi, start.x * 1e3, start.y * 1e3, start.z * 1e3);
    rs.pin(bi);
    const Real bx = sim.hookX - 0.005;            // where on the shaft the yarn is laid
    const int overSteps = 120;
    for (int k = 1; k <= overSteps; ++k) {
        const Real t = (Real)k / overSteps;
        // from behind (z<0), over the top (y>0), to the front (z>0) and a little way down the near side
        const Real a = -0.5 * kPi + kPi * 1.15 * t;   // angle around the shaft, -90 deg = behind
        const Real rad = 3.7 * kMm;
        // polar: angle phi measured from +z toward +y; behind is phi = 180 deg, over the top is 90 deg, the front is 0 deg
        const Real phi = kPi * (1.0 - 1.15 * t);
        const Vec3 onShaft{bx, rad * std::sin(phi), rad * std::cos(phi)};
        const Real lift = std::min(1.0, t * 4.0);
        rs.movePin(bi, start + (onShaft - start) * lift);
        sim.run(1);
    }
    sim.run(20);
    std::printf("laid over: strain %.3f\n", maxStrain(rs));
    frame(sim, out, "1_over");

    // the catch: the bight now rides with the hook; draw the hook back 22 mm
    const Vec3 rel = rs.x[bi] - Vec3{sim.hookX, 0, 0};
    const Vec3 work0 = rs.x[sim.work];
    const int pullSteps = 160;
    const Real x0 = sim.hookX;
    int fr = 0;
    for (int k = 1; k <= pullSteps; ++k) {
        const Real t = (Real)k / pullSteps;
        const Real e = t * t * (3 - 2 * t);
        sim.hookX = x0 - 0.024 * e;
        // the bight sits just behind the tip, in the throat: pinned to the hook
        rs.movePin(bi, Vec3{sim.hookX, 0, 0} + Vec3{-0.003, rel.y * 0.2, rel.z * 0.2});
        rs.movePin(sim.work, work0 + Vec3{-0.024 * e, 0, 0});   // the ball end follows: yarn feeds from the ball as the loop is drawn
        sim.run(1);
        if (k % 40 == 0) { char tag[32]; std::snprintf(tag, sizeof tag, "2_pull%d", ++fr); frame(sim, out, tag); std::printf("  pull %d: strain %.3f contacts %d\n", k, maxStrain(rs), rs.lastContacts); }
    }
    sim.run(30);
    frame(sim, out, "3_done");
    std::printf("done: strain %.3f\n", maxStrain(rs));
    return 0;
}
