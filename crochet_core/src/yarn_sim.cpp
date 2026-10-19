#include "failoring/crochet/yarn_sim.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace fl {

namespace {

const Real kMm = 1e-3;

Vec3 catmull(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, Real t) {
    const Real t2 = t * t, t3 = t2 * t;
    return (p1 * 2.0 + (p2 - p0) * t + (p0 * 2.0 - p1 * 5.0 + p2 * 4.0 - p3) * t2 + (p1 * 3.0 - p0 - p2 * 3.0 + p3) * t3) * 0.5;
}

// A smooth curve through `pts`, sampled every `step` m of arc.
std::vector<Vec3> smoothResample(const std::vector<Vec3>& pts, Real step) {
    std::vector<Vec3> dense;
    const int n = (int)pts.size();
    for (int i = 0; i + 1 < n; ++i) {
        const Vec3& p0 = pts[std::max(0, i - 1)];
        const Vec3& p3 = pts[std::min(n - 1, i + 2)];
        for (int k = 0; k < 24; ++k) dense.push_back(catmull(p0, pts[i], pts[i + 1], p3, k / 24.0));
    }
    dense.push_back(pts.back());
    std::vector<Vec3> out{dense[0]};
    Real carry = 0;
    for (size_t i = 1; i < dense.size(); ++i) {
        Vec3 a = dense[i - 1];
        const Vec3 b = dense[i];
        Real seg = length(b - a);
        while (carry + seg >= step) {
            const Real f = (step - carry) / seg;
            a = a + (b - a) * f;
            out.push_back(a);
            seg = length(b - a);
            carry = 0;
        }
        carry += seg;
    }
    return out;
}

Quat poseQuat(const Vec3& pointing, const Vec3& throat) {
    const Vec3 z = normalized(pointing * -1.0);
    Vec3 x = throat - z * dot(throat, z);
    x = normalized(x);
    const Vec3 y = cross(z, x);
    // rotation matrix columns x, y, z -> quaternion
    const Real m00 = x.x, m01 = y.x, m02 = z.x, m10 = x.y, m11 = y.y, m12 = z.y, m20 = x.z, m21 = y.z, m22 = z.z;
    const Real tr = m00 + m11 + m22;
    Quat q;
    if (tr > 0) {
        const Real s = std::sqrt(tr + 1.0) * 2;
        q = {0.25 * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s};
    } else if (m00 > m11 && m00 > m22) {
        const Real s = std::sqrt(1.0 + m00 - m11 - m22) * 2;
        q = {(m21 - m12) / s, 0.25 * s, (m01 + m10) / s, (m02 + m20) / s};
    } else if (m11 > m22) {
        const Real s = std::sqrt(1.0 + m11 - m00 - m22) * 2;
        q = {(m02 - m20) / s, (m01 + m10) / s, 0.25 * s, (m12 + m21) / s};
    } else {
        const Real s = std::sqrt(1.0 + m22 - m00 - m11) * 2;
        q = {(m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25 * s};
    }
    return normalized(q);
}

}  // namespace

HookPose hookPose(const Vec3& tip, const Vec3& pointing, const Vec3& throat) {
    HookPose p;
    p.tip = tip;
    p.q = poseQuat(pointing, throat);
    return p;
}

HookPose lerpPose(const HookPose& a, const HookPose& b, Real t) {
    HookPose p;
    p.tip = a.tip + (b.tip - a.tip) * t;
    Quat d = b.q * conj(a.q);
    if (d.w < 0) d = {-d.w, -d.x, -d.y, -d.z};
    const Vec3 rv = rotationVector(d);
    const Real ang = length(rv);
    p.q = ang > 1e-12 ? normalized(quatFromAxisAngle(rv / ang, ang * t) * a.q) : a.q;
    return p;
}

// The head of a hook, in its own frame (z from the tip toward the handle, the throat opening toward +x): a rounded head that leans out over the
// throat side (the barb: its back faces in toward the throat, so a yarn pulled against it is pushed deeper, not off), a neck cut away on the throat
// side (the throat), a taper back up to the full shaft. Smooth, no creases or slots: a rigid yarn wedged in a slot stretches instead of squashing.
std::vector<StaticCapsule> hookCapsules(const HookPose& p, const HookShape& s) {
    const Real R = s.R;
    auto W = [&](Real x, Real z) { return p.tip + rotate(p.q, Vec3{x * R, 0, z * R}); };
    std::vector<StaticCapsule> c;
    c.push_back({W(0.05, 0.95), W(0.35, 1.50), 0.85 * R});    // head, leaning out over the throat: its back faces in, so it holds the yarn (the barb)
    c.push_back({W(-0.60, 1.40), W(-0.60, 4.60), 0.35 * R});   // neck (the throat is the room beside it on +x)
    c.push_back({W(-0.25, 4.60), W(-0.05, 5.60), 0.78 * R});   // taper
    c.push_back({W(0.0, 5.60), W(0.0, s.length / R), R});      // shaft
    for (StaticCapsule& k : c) k.squash = s.squash;
    return c;
}

void YarnSim::build(const std::vector<Vec3>& path) {
    RodParams& rp = rs.params;
    rp.gravity = Vec3{};
    rp.radius = params.radius;
    rp.EA = 30.0;
    rp.GAs = 30.0;
    rp.EI = params.bend;
    rp.GJ = rp.EI;
    rp.damping = params.damping;
    rp.substeps = 6;
    rp.iterations = 2;
    rp.radiusScale = 1.0;
    rp.friction = params.friction;
    rp.staticFriction = params.hookFriction;
    rp.contactSkipSegments = std::max(2, (int)std::ceil(2.0 * params.radius / params.segment));
    rs.addChain(smoothResample(path, params.segment), false, true);   // the yarn keeps the curl it was laid with (relaxStraight lets it go)
    rs.setMassScale(params.massScale);
    rs.setRotationalInertiaScale(1.0e4);
    tail.particle = 0;
    tail.target = rs.x[0];
    rs.pin(tail.particle);
    ball.particle = rs.particleCount() - 1;
    ball.target = rs.x[ball.particle];
    ball.hand = true;
    ball.tension = params.tension;
    rs.pin(ball.particle);
}

void YarnSim::controlEnd(End& e) {
    if (e.particle < 0) return;
    if (e.hand) feedHand(e);
    rs.movePin(e.particle, e.target);
}

// The left hand: the last segment runs freely through it (no stretch constraint), the yarn next to it is pulled toward it with the hand's tension;
// a segment grown long is split (yarn paid out), one grown short is merged away (yarn drawn back in).
void YarnSim::feedHand(End& e) {
    const Real seg = params.segment;
    auto lastSeg = [&] { return rs.segmentCount() - 1; };
    if (rs.particleCount() < 3) return;
    int p = rs.particleCount() - 2;
    const Real d = length(rs.x[p] - e.target);
    if (d > 1.8 * seg) {
        // split: the end particle moves out of the hand to one segment from its neighbour, a new end takes its place in the hand
        const int old = e.particle;
        const Vec3 dir = (e.target - rs.x[p]) / d;
        const Vec3 qpt = rs.x[p] + dir * seg;
        rs.unpin(old);
        rs.x[old] = rs.xPrev[old] = rs.pinTarget[old] = qpt;
        rs.v[old] = Vec3{};
        rs.restLen[lastSeg()] = seg;
        rs.slack[lastSeg()] = 1.0;
        rs.extForce[p] = Vec3{};
        rs.extendChain({e.target});
        e.particle = rs.particleCount() - 1;
        rs.pin(e.particle);
        p = e.particle - 1;
    } else if (d < 0.5 * seg && rs.segmentCount() > 8) {
        // merge: the end particle goes, its neighbour is the new end, held in the hand
        rs.popEnd();
        e.particle = rs.particleCount() - 1;
        rs.extForce[e.particle] = Vec3{};
        rs.pin(e.particle);
        p = e.particle - 1;
    }
    const int s = lastSeg();
    rs.slack[s] = 1.0e9;
    rs.restLen[s] = std::max(1e-4, length(rs.x[e.particle] - rs.x[p]));
    if (pulled_ >= 0 && pulled_ < rs.particleCount() && pulled_ != p) rs.extForce[pulled_] = Vec3{};
    pulled_ = p;
    const Vec3 to = e.target - rs.x[p];
    const Real tl = length(to);
    rs.extForce[p] = tl > 1e-9 ? to * (e.tension / tl) : Vec3{};
}

int YarnSim::grab(const Vec3& at, Real radius) {
    Grip g;
    g.at = at;
    for (int i = 0; i < rs.particleCount(); ++i) {
        if (i == tail.particle || i == ball.particle || length(rs.x[i] - at) > radius) continue;
        g.particles.push_back(i);
        g.offset.push_back(rs.x[i] - at);
        rs.pin(i);
    }
    grips.push_back(g);
    return (int)grips.size() - 1;
}

void YarnSim::release(int g) {
    for (int i : grips[g].particles) rs.unpin(i);
    grips[g].particles.clear();
    grips[g].offset.clear();
}

void YarnSim::step(const HookPose& pose) {
    hook = pose;
    rs.statics = hookCapsules(hook, shape);
    rs.statics.insert(rs.statics.end(), fixtures.begin(), fixtures.end());
    controlEnd(tail);
    controlEnd(ball);
    for (const Grip& g : grips)
        for (size_t k = 0; k < g.particles.size(); ++k) rs.movePin(g.particles[k], g.at + g.offset[k]);
    rs.step(params.dt);
    ++frame;
}

void YarnSim::moveHook(const HookPose& to, Real seconds) {
    const HookPose from = hook;
    const int n = std::max(1, (int)(seconds / params.dt + 0.5));
    for (int k = 1; k <= n; ++k) {
        const Real t = (Real)k / n;
        const Real e = t * t * t * (10 - 15 * t + 6 * t * t);
        step(lerpPose(from, to, e));
        if (onFrame) onFrame();
    }
}

void YarnSim::settle(int frames) {
    for (int k = 0; k < frames; ++k) {
        step(hook);
        if (onFrame) onFrame();
    }
}

void YarnSim::relaxStraight(Real f, int fromJoint) {
    for (int j = std::max(0, fromJoint); j < (int)rs.restDarboux.size(); ++j) rs.restDarboux[j] = rs.restDarboux[j] * f;
}

Real YarnSim::maxStrain() const {
    Real e = 0;
    for (int s = 0; s < rs.segmentCount(); ++s)
        if (rs.slack[s] <= 1.0) e = std::max(e, std::fabs(length(rs.x[rs.segB[s]] - rs.x[rs.segA[s]]) / rs.restLen[s] - 1.0));
    return e;
}

Real YarnSim::minGap() const { return minStrandGap(rs, rs.params.contactSkipSegments + 1); }

int YarnSim::loopsOnHook(const Vec3& outDir) const {
    const Vec3 ax = hook.axis();
    Vec3 u = outDir - ax * dot(outDir, ax);
    u = normalized(u);
    const Vec3 nrm = cross(ax, u);
    int count = 0;
    for (int s = 0; s < rs.segmentCount(); ++s) {
        const Vec3 &a = rs.x[rs.segA[s]], &b = rs.x[rs.segB[s]];
        const Real da = dot(a - hook.tip, nrm), db = dot(b - hook.tip, nrm);
        if ((da > 0) == (db > 0)) continue;
        const Vec3 p = a + (b - a) * (da / (da - db));
        const Real along = dot(p - hook.tip, ax), out = dot(p - hook.tip, u);
        if (out <= 0 || along < -0.01 || along > shape.length) continue;
        count += da > 0 ? 1 : -1;
    }
    return count;
}

Real YarnSim::hookPenetration() const {
    Real worst = 0;
    for (const StaticCapsule& c : rs.statics)
        for (int s = 0; s < rs.segmentCount(); ++s) {
            const SegSegResult r = closestSegmentSegment(rs.x[rs.segA[s]], rs.x[rs.segB[s]], c.a, c.b);
            worst = std::max(worst, c.r + params.radius * c.squash - std::sqrt(r.dist2));
        }
    return worst;
}

// The slip knot's start, as a crocheter lays it: the tail hanging down, a ring above it, the yarn crossing over the tail where the ring closes and then
// running up behind the middle of the ring toward the ball.
std::vector<Vec3> slipKnotLayout(Real r) {
    const Real m = kMm;
    const Real zo = std::max(3.4 * r, 3.8 * m), zb = std::max(3.3 * r, 3.6 * m);
    std::vector<Vec3> p;
    p.push_back(Vec3{-6, -55, 0} * m);
    p.push_back(Vec3{-3, -35, 0} * m);
    p.push_back(Vec3{0.5, -20, 0} * m);
    const Real ringR = 11 * m;
    for (int a = -80; a <= 260; a += 20) {
        const Real t = a * kPi / 180.0;
        p.push_back(Vec3{ringR * std::cos(t), ringR * std::sin(t), 0});
    }
    p.push_back(Vec3{2.8 * m, -14.5 * m, zo});   // over the tail
    p.push_back(Vec3{5.5 * m, -16 * m, 0.3 * zo});
    p.push_back(Vec3{5.0 * m, -15 * m, -zb});
    p.push_back(Vec3{1.5 * m, -12.5 * m, -zb});   // round behind, held there by the pinch
    p.push_back(Vec3{1.0 * m, 0.0, -zb});         // straight up behind the middle of the ring
    p.push_back(Vec3{1.0 * m, 22 * m, -zb});
    p.push_back(Vec3{1.0 * m, 45 * m, -zb});      // the left hand, where the working yarn runs to the ball
    return p;
}


void makeSlipKnot(YarnSim& sim) {
    const Real m = kMm, R = sim.shape.R;
    const Real zb = std::max(3.3 * sim.params.radius, 3.6 * m);
    // the hook comes from the front, the tip pointing into the ring, the throat toward the yarn behind the ring
    const Real deep = -zb - 3.4 * R;                   // the tip this far back puts the yarn behind the ring at the mouth of the throat
    // the left hand pinches the ring where it closes (over the tail) and holds the working yarn loosely: the ring keeps its shape
    sim.phase = "The yarn laid in a ring, the working yarn behind it; the left hand pinches where the ring crosses the tail";
    const int pinch = sim.grab(Vec3{2.8 * m, -14.5 * m, -0.3 * m}, 5.2 * m);
    const Real tension0 = sim.ball.tension;
    sim.ball.tension = 0.3 * tension0;   // held loosely while the hook works
    // the hook comes in right of the yarn with the throat toward it (both ends of the caught yarn must leave through the throat: a yarn that also runs
    // round the back of the hook is a hitch on it and will not slide)
    const Vec3 th{-1, 0, 0};
    sim.hook = hookPose(Vec3{6.5 * m, 1 * m, 30 * m}, Vec3{0, 0, -1}, th);
    sim.settle(10);
    sim.phase = "The hook goes in through the ring";
    sim.moveHook(hookPose(Vec3{6.5 * m, 1 * m, deep}, Vec3{0, 0, -1}, th), 1.4);   // in through the ring, past the yarn behind it
    sim.phase = "It catches the working yarn behind the ring in its throat";
    sim.moveHook(hookPose(Vec3{1.3 * m, 1 * m, deep}, Vec3{0, 0, -1}, th), 1.0);   // across, behind the yarn: it is deep in the throat
    sim.phase = "It draws a bight of it back out through the ring";
    sim.moveHook(hookPose(Vec3{1.3 * m, 1 * m, 22 * m}, Vec3{0, 0, -1}, th), 1.8);   // back out through the ring: the yarn comes through in a bight
    // The bight hangs in the throat, its legs running back through the ring: it lies beside the hook, not round it. Swinging the handle round to
    // the right (the tip now points left, the throat toward the crocheter) puts the hook through it: the loop on the hook.
    const Real thr = 2.6 * R;
    sim.phase = "The handle swings round to the right: the hook is now through the bight, a loop on the hook";
    sim.moveHook(hookPose(Vec3{1.3 * m - thr, 1 * m, 22 * m + thr}, Vec3{-1, 0, 0}, Vec3{0, 0, 1}), 1.4);
    // the left hand still holds the knot: the hook pushed on, tip first, slides through the loop, which rides back off the neck onto the shaft
    sim.phase = "The hook slides on through the loop, which rides onto the shaft";
    sim.moveHook(hookPose(Vec3{1.3 * m - thr - 14 * m, 1 * m, 22 * m + thr}, Vec3{-1, 0, 0}, Vec3{0, 0.5, 1}), 1.2);
    // the knot closed: the pinch let go, the tail drawn down, the working yarn held tighter
    sim.phase = "The left hand lets go; the tail is pulled down and the working yarn up: the knot closes";
    sim.release(pinch);
    const Vec3 t0 = sim.tail.target;
    const int n = (int)(1.6 / sim.params.dt);
    for (int k = 1; k <= n; ++k) {
        const Real t = (Real)k / n;
        sim.tail.target = t0 + Vec3{0, -18 * m, 12 * m} * (t * t * (3 - 2 * t));
        sim.ball.tension = tension0 * (0.3 + 3.7 * t);
        sim.relaxStraight(0.97);
        sim.step(sim.hook);
        if (sim.onFrame) sim.onFrame();
    }
    // snugged: the working yarn pulled firmly closes the loop on the shaft
    sim.phase = "The working yarn pulled firmly snugs the loop on the shaft: the slip knot";
    sim.ball.tension = 4.0 * sim.params.tension;
    sim.settle(150);
    sim.ball.tension = tension0;
    sim.settle(20);
}

namespace {
// Where the loop on the hook sits: the mean of the yarn close round the shaft (within `reach` of the axis), and how far behind the tip.
bool loopOnShaft(const YarnSim& sim, Vec3* centre, Real* along) {
    const Vec3 ax = sim.hook.axis();
    Vec3 c{};
    int n = 0;
    for (const Vec3& p : sim.rs.x) {
        const Vec3 d = p - sim.hook.tip;
        const Real a = dot(d, ax);
        if (a < 0 || a > 0.04) continue;
        if (length(d - ax * a) > sim.shape.R + 3.0 * sim.params.radius) continue;
        c += p;
        ++n;
    }
    if (n == 0) return false;
    *centre = c / (Real)n;
    *along = dot(*centre - sim.hook.tip, ax);
    return true;
}
}  // namespace

void makeChain(YarnSim& sim) {
    const Real m = kMm, R = sim.shape.R;
    Vec3 loop;
    Real along = 0;
    if (!loopOnShaft(sim, &loop, &along)) return;
    const Vec3 ax = sim.hook.axis();            // tip -> handle (the hook lies tip to the left)
    const Vec3 up{0, 1, 0}, front{0, 0, 1};
    auto moveHand = [&](const Vec3& to, Real seconds) {
        const Vec3 from = sim.ball.target;
        const int n = std::max(1, (int)(seconds / sim.params.dt));
        for (int k = 1; k <= n; ++k) {
            const Real t = (Real)k / n;
            sim.ball.target = from + (to - from) * (t * t * (3 - 2 * t));
            sim.step(sim.hook);
            if (sim.onFrame) sim.onFrame();
        }
    };
    // the left hand: thumb and middle finger pinch the work just below the loop on the hook; the index finger, up behind the loop, carries the yarn
    sim.phase = "The left hand holds the work below the hook; the yarn runs up over its index finger";
    const int hold = sim.grab(loop - up * (R + 4 * m), 3.5 * m);
    std::printf("chain: loop %.1f mm behind the tip, loops on hook %d\n", along * 1e3, sim.loopsOnHook(up));
    // the loop pushed back onto the shaft, clear of the throat: the hook slides on through it, tip first
    const Real seat = (std::getenv("SIM_SEAT") ? std::atof(std::getenv("SIM_SEAT")) : 8.0) * R;
    if (along < seat) {
        sim.moveHook(hookPose(sim.hook.tip - ax * (seat - along), -ax, sim.hook.throat()), 0.6);
        loopOnShaft(sim, &loop, &along);
        std::printf("chain: loop now %.1f mm behind the tip\n", along * 1e3);
    }
    // the working yarn tightened: the loop closes snug round the shaft
    sim.ball.tension = 4.0 * sim.params.tension;
    sim.settle(60);
    sim.ball.tension = sim.params.tension;
    const Vec3 tip0 = sim.hook.tip;
    const Vec3 throat = tip0 + ax * (3.0 * R);  // the middle of the throat
    moveHand(loop + up * (25 * m) - front * (15 * m), 0.7);
    // yarn over: the index finger brings the yarn down behind the hook, level with it and to the left, so that it runs from the loop past the
    // throat; the throat turned back toward it, the hook moves back into it: the yarn lands in the throat (the catch that made the slip knot)
    sim.phase = "Yarn over: the hook takes the yarn running to the finger into its throat";
    const Vec3 back = front * -1.0;
    auto env = [](const char* k, Real d) { return std::getenv(k) ? std::atof(std::getenv(k)) : d; };
    const Vec3 thr = normalized(front * env("YO_RF", 0.5) - up);   // the throat, down and a little toward the crocheter
    if (env("YO_MODE", 3) == 3) {
        // the yarn over: the index finger takes the yarn once round the hook just in front of the throat, from behind, over the top, down in
        // front, under and up behind again; the throat faces down into the wrap
        sim.moveHook(hookPose(tip0, -ax, thr), 0.5);
        const Vec3 c = tip0 + ax * (env("YO_AX", 3.0) * R);
        const Real rad = env("YO_R", 15) * m;
        const Real a0 = env("YO_A0", 2.2);   // the angle in the (back, up) plane where the finger starts: behind and above
        auto at = [&](Real ang) { return c + (front * -std::cos(ang) + up * std::sin(ang)) * rad; };
        moveHand(at(a0), 0.6);
        const int n = (int)(env("YO_T", 1.6) / sim.params.dt);
        for (int k = 1; k <= n; ++k) {
            const Real t = (Real)k / n;
            const Real e = t * t * (3 - 2 * t);
            sim.ball.target = at(a0 - 2 * kPi * e);   // decreasing angle: from behind-up over the top toward the front, down, under, back
            sim.step(sim.hook);
            if (sim.onFrame) sim.onFrame();
        }
    } else if (env("YO_MODE", 3) == 2) {
        moveHand(loop - ax * (env("YO_L", 40) * m) + up * (env("YO_U", 4) * m) + back * (env("YO_B", 2) * m), 0.8);
        sim.moveHook(hookPose(tip0, -ax, normalized(up + back * env("YO_TB", 0.2))), 0.5);
        sim.moveHook(hookPose(tip0 + up * (env("YO_D", 2.5) * m), -ax, normalized(up + back * env("YO_TB", 0.2))), 0.5);
    } else {
        moveHand(loop - ax * (env("YO_L", 14) * m) + back * (env("YO_B", 16) * m) + up * (env("YO_U", 1) * m), 0.7);
        sim.moveHook(hookPose(tip0, -ax, back), 0.5);
        sim.moveHook(hookPose(tip0 + back * (env("YO_D", 7) * m), -ax, back), 0.8);
    }
    {
        int n = 0, lo = 1 << 30, hi = -1;
        for (int i = 0; i < sim.rs.particleCount(); ++i) {
            const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip) / R;
            if (l.x > -0.3 && l.x < 1.0 && std::fabs(l.y) < 1.2 && l.z > 1.4 && l.z < 4.6) { ++n; lo = std::min(lo, i); hi = std::max(hi, i); }
        }
        std::printf("chain: after the catch %d particles in the throat (%d..%d), %d particles;", n, lo, hi, sim.rs.particleCount());
        for (int i = 0; i < sim.rs.particleCount(); ++i) {
            const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip) / R;
            if (l.x > -0.3 && l.x < 1.0 && std::fabs(l.y) < 1.2 && l.z > 1.4 && l.z < 4.6) std::printf(" %d", i);
        }
        int lo2 = 1 << 30, hi2 = -1;
        for (int i = 0; i < sim.rs.particleCount(); ++i) {
            const Vec3 d = sim.rs.x[i] - sim.hook.tip;
            const Real a = dot(d, ax);
            if (a > 5 * R && a < 12 * R && length(d - ax * a) < R + 2.5 * sim.params.radius) { lo2 = std::min(lo2, i); hi2 = std::max(hi2, i); }
        }
        std::printf("; round the shaft %d..%d\n", lo2, hi2);
    }
    // the throat rolled down and forward: the yarn is over the hook, caught
    sim.phase = "The hook turns its throat down, the yarn caught in it";
    if (env("YO_MODE", 3) != 3) {
        const Vec3 lift = sim.hook.tip - tip0;
        sim.moveHook(hookPose(tip0 + lift, -ax, front), 0.4);
        sim.moveHook(hookPose(tip0, -ax, thr), 0.4);
        moveHand(tip0 + ax * (3 * R) + front * (env("YO_HF", 14) * m) - up * (env("YO_HD", 10) * m), 0.8);
    }
    {
        int best = -1; Real bd = 1e9; Vec3 bl{};
        for (int i = 0; i < sim.rs.particleCount(); ++i) {
            const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip) / R;
            const Real d = length(l - Vec3{0.1, 0, 2.9});
            if (d < bd) { bd = d; best = i; bl = l; }
        }
        std::printf("chain: before the pull the yarn nearest the throat is p%d at (%.2f %.2f %.2f) R, %.2f R away; loops on hook %d\n", best, bl.x, bl.y,
                    bl.z, bd, sim.loopsOnHook(up));
    }
    // drawn back through the loop: the hand holds the work, the hook moves to the right; the old loop slides over the head and off, the yarn in the
    // throat comes through it as the new loop
    sim.phase = "Pull through: the hook draws the yarn through the loop on the hook";
    sim.moveHook(hookPose(tip0 + ax * (along + env("YO_PULL", 8) * R), -ax, thr), 1.6);
    {
        int lo2 = 1 << 30, hi2 = -1;
        for (int i = 0; i < sim.rs.particleCount(); ++i) {
            const Vec3 d = sim.rs.x[i] - sim.hook.tip;
            const Real a = dot(d, ax);
            if (a > 0 && a < 12 * R && length(d - ax * a) < R + 2.5 * sim.params.radius) { lo2 = std::min(lo2, i); hi2 = std::max(hi2, i); }
        }
        std::printf("chain: after the pull, round the hook:");
        for (int i = 0; i < sim.rs.particleCount(); ++i) {
            const Vec3 d = sim.rs.x[i] - sim.hook.tip;
            const Real a = dot(d, ax);
            if (a > 0 && a < 12 * R && length(d - ax * a) < R + 2.5 * sim.params.radius) std::printf(" %d", i);
        }
        std::printf("\n");
    }
    // the throat back toward the crocheter, and the hook pushed on through the new loop (the hand still holding the work) so that it rides out
    // of the throat onto the shaft; the yarn back over the finger
    sim.phase = "The new loop on the hook: it rides onto the shaft";
    sim.moveHook(hookPose(sim.hook.tip, -ax, normalized(front + up * 0.6)), 0.6);
    if (loopOnShaft(sim, &loop, &along) && along < seat)
        sim.moveHook(hookPose(sim.hook.tip - ax * (seat - along), -ax, sim.hook.throat()), 0.8);
    moveHand(sim.hook.tip + ax * seat + up * (25 * m) - front * (15 * m), 0.6);
    sim.release(hold);
    sim.settle(20);
}

}  // namespace fl

