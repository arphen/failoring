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
    rp.substeps = params.substeps;
    rp.iterations = params.iterations;
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
    } else {
        // merge: the end particle goes, its neighbour is the new end, held in the hand (as often as the yarn comes back that fast)
        while (rs.segmentCount() > 8 && length(rs.x[rs.particleCount() - 2] - e.target) < 0.5 * seg) {
            rs.popEnd();
            e.particle = rs.particleCount() - 1;
            rs.extForce[e.particle] = Vec3{};
            rs.pin(e.particle);
        }
        p = e.particle - 1;
    }
    const int s = lastSeg();
    rs.slack[s] = 1.0e9;
    rs.restLen[s] = std::max(1e-4, length(rs.x[e.particle] - rs.x[p]));
    for (int i : reeled_)
        if (i < rs.particleCount()) rs.extForce[i] = Vec3{};
    reeled_.clear();
    pulled_ = p;
    const Vec3 to = e.target - rs.x[p];
    const Real tl = length(to);
    rs.extForce[p] = tl > 1e-9 ? to * (e.tension / tl) : Vec3{};
    reeled_.push_back(p);
    // slack yarn near the hand (much longer than the way straight to it) is drawn in along itself, as it slides through the fingers; taut yarn is
    // left alone (its tension is the hand's)
    Real arc = length(e.target - rs.x[p]);
    for (int k = 1; k <= 10 && p - k > 0; ++k) {
        const int i = p - k;
        arc += length(rs.x[i + 1] - rs.x[i]);
        if (rs.w[i] <= 0 || length(e.target - rs.x[i]) > reelSlack * arc) continue;
        rs.extForce[i] = normalized(rs.x[i + 1] - rs.x[i - 1]) * (reelForce * e.tension);
        reeled_.push_back(i);
    }
}

Vec3 YarnSim::throatPoint() const { return hook.tip + rotate(hook.q, Vec3{0.05 * shape.R, 0, 2.8 * shape.R}); }

void YarnSim::putFinger(int particle, const Vec3& side) {
    const Vec3 t = normalized(rs.x[std::min(particle + 1, rs.particleCount() - 1)] - rs.x[std::max(particle - 1, 0)]);
    Vec3 n = side - t * dot(side, t);
    n = normalized(n);
    fingerAxis = normalized(cross(t, n));
    hand = fingerEyelet ? rs.x[particle] : rs.x[particle] + n * (fingerR + params.radius + 0.3e-3);
    // the spool goes on round the finger: the yarn leaves over the finger's far side
    spoolOff = n * 0.02 + t * 0.006;
    fingerOn = true;
}

namespace {
int nearestSegment(const RodSystem& rs, const Vec3& P, int from, Real* d2) {
    int best = -1;
    Real bd = 1e300;
    for (int sg = std::max(0, from); sg + 1 < rs.segmentCount(); ++sg) {
        const SegSegResult r = closestSegmentSegment(rs.x[rs.segA[sg]], rs.x[rs.segB[sg]], P, P);
        if (r.dist2 < bd) { bd = r.dist2; best = sg; }
    }
    *d2 = bd;
    return best;
}
void dropRing(RodSystem& rs, int& idx, int& other) {
    if (idx < 0) return;
    rs.rings.erase(rs.rings.begin() + idx);
    if (other > idx) --other;
    idx = -1;
}
}  // namespace

void YarnSim::setThroatRing(bool on, int fromParticle) {
    dropRing(rs, throatRing_, fingerRing_);
    if (!on) return;
    const Vec3 T = throatPoint();
    Real d2 = 0;
    const int best = nearestSegment(rs, T, fromParticle, &d2);
    if (best < 0 || d2 > 25e-6) return;   // nothing within 5 mm of the throat
    SlideRing ring;
    ring.at = T;
    ring.seg = best;
    ring.stiffness = throatStiffness;
    rs.rings.push_back(ring);
    throatRing_ = (int)rs.rings.size() - 1;
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
    if (fingerOn && fingerEyelet) {
        if (fingerRing_ < 0) {
            Real d2 = 0;
            SlideRing ring;
            ring.seg = nearestSegment(rs, hand, 0, &d2);
            ring.at = hand;
            rs.rings.push_back(ring);
            fingerRing_ = (int)rs.rings.size() - 1;
        }
        rs.rings[fingerRing_].at = hand;
        Vec3 out = hand - hook.tip;
        const Vec3 ax = hook.axis();
        out = out - ax * dot(out, ax);
        ball.target = hand + normalized(out) * (spoolRadial > 0 ? spoolRadial : 0.02);
    } else if (fingerOn) {
        StaticCapsule f;
        f.a = hand - fingerAxis * (0.5 * fingerLen);
        f.b = hand + fingerAxis * (0.5 * fingerLen);
        f.r = fingerR;
        rs.statics.push_back(f);
        if (spoolRadial > 0) {
            // the yarn leaves the finger away from the hook (it runs on over the hand to the ball), so the finger can go round the hook
            const Vec3 ax = hook.axis();
            Vec3 out = hand - hook.tip;
            out = out - ax * dot(out, ax);
            ball.target = hand + normalized(out) * spoolRadial;
        } else {
            ball.target = hand + spoolOff;
        }
    }
    if (throatRing_ >= 0) rs.rings[throatRing_].at = throatPoint();
    controlEnd(tail);
    controlEnd(ball);
    for (const Grip& g : grips)
        for (size_t k = 0; k < g.particles.size(); ++k) rs.movePin(g.particles[k], g.at + g.offset[k]);
    for (int i : caught_)
        if (i < rs.particleCount() && std::find(reeled_.begin(), reeled_.end(), i) == reeled_.end()) rs.extForce[i] = Vec3{};
    caught_.clear();
    if (catchK > 0) {
        // the middle of the throat: just behind the head, in the groove (the yarn's squashed radius out from the neck)
        const Vec3 T = hook.tip + rotate(hook.q, Vec3{0.05 * shape.R, 0, 2.8 * shape.R});
        const int n = rs.particleCount();
        for (int i = std::max(1, catchFrom); i + 1 < n; ++i) {
            if (i == ball.particle || i == pulled_ || rs.w[i] <= 0) continue;
            const Vec3 d = T - rs.x[i];
            if (length(d) > catchReach) continue;
            const Vec3 t = normalized(rs.x[i + 1] - rs.x[i - 1]);
            rs.extForce[i] = (d - t * dot(d, t)) * catchK;
            caught_.push_back(i);
        }
    }
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

Real YarnSim::windingNear(int i0, int i1, Real reach) const {
    Real best = 0, run = 0, prev = 0;
    bool have = false;
    i0 = std::max(0, i0);
    i1 = std::min(rs.particleCount() - 1, i1);
    for (int i = i0; i <= i1; ++i) {
        const Vec3 l = rotate(conj(hook.q), rs.x[i] - hook.tip);
        if (l.z < -2.0 * shape.R || l.z > shape.length || std::hypot(l.x, l.y) > reach) {
            have = false;
            run = 0;
            continue;
        }
        const Real a = std::atan2(l.y, l.x);
        if (have) {
            Real d = a - prev;
            while (d > kPi) d -= 2 * kPi;
            while (d < -kPi) d += 2 * kPi;
            run += d;
            if (std::fabs(run) > std::fabs(best)) best = run;
        }
        prev = a;
        have = true;
    }
    return best / (2 * kPi);
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


void buildLoopRig(YarnSim& sim, const Vec3& tip) {
    const Real m = kMm, R = sim.shape.R, r = sim.params.radius;
    const Real rl = R + r + 0.3 * m;    // the loop's radius round the shaft
    const Real xl = 17 * m;             // where it sits behind the tip
    // frame: x along the hook (tip -> handle), y up, z toward the crocheter
    auto P = [&](Real x, Real y, Real z) { return tip + Vec3{x, y, z}; };
    std::vector<Vec3> path;
    path.push_back(P(xl, -30 * m, 3 * m));            // the work, held
    path.push_back(P(xl, -14 * m, 3.5 * m));
    path.push_back(P(xl - 0.6 * m, -6 * m, rl));
    for (int k = 0; k <= 16; ++k) {                   // up the front, over the top, down the back, under, and round to the front again
        const Real phi = -kPi / 3 + (2 * kPi - kPi / 6) * k / 16.0;
        path.push_back(P(xl - 0.6 * m + 2.6 * m * k / 16.0, rl * std::sin(phi), rl * std::cos(phi)));
    }
    // crossed in front of the leg to the work, the yarn goes on down and back under the hook and up behind it to the finger
    const Vec3 finger = P(xl - 6 * m, 22 * m, -14 * m);
    path.push_back(P(xl + 3 * m, -rl - 2.5 * m, 1 * m));
    path.push_back(P(xl + 3 * m, -rl - 3 * m, -5 * m));
    path.push_back(P(xl + 2 * m, 0, -9 * m));
    path.push_back(finger + Vec3{0, -sim.fingerR - r - 4 * m, 2 * m});
    path.push_back(finger + Vec3{0, 0, sim.fingerR + r + 0.4 * m});      // up the finger's front
    path.push_back(finger + Vec3{0, sim.fingerR + r + 0.4 * m, 0});      // over its top
    path.push_back(finger + Vec3{0, 0, -(sim.fingerR + r + 0.4 * m)});   // down its back
    path.push_back(finger + Vec3{0, -12 * m, -14 * m});                  // to the spool
    sim.build(path);
    sim.hand = finger;
    sim.fingerAxis = Vec3{1, 0, 0};
    sim.spoolOff = Vec3{0, -12 * m, -14 * m};
    sim.fingerOn = true;
    sim.relaxStraight(0.0);
    sim.hook = hookPose(tip, Vec3{-1, 0, 0}, Vec3{0, 0.5, 1});
    sim.settle(60);
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

namespace {
void moveHandTo(YarnSim& sim, const Vec3& to, Real seconds) {
    Vec3& h = sim.fingerOn ? sim.hand : sim.ball.target;   // the finger (and the spool with it) or the bare hand point
    const Vec3 from = h;
    const int n = std::max(1, (int)(seconds / sim.params.dt));
    for (int k = 1; k <= n; ++k) {
        const Real t = (Real)k / n;
        h = from + (to - from) * (t * t * (3 - 2 * t));
        sim.step(sim.hook);
        if (sim.onFrame) sim.onFrame();
    }
}
Real envOr(const char* k, Real d) { return std::getenv(k) ? std::atof(std::getenv(k)) : d; }
std::vector<int> roundHook(const YarnSim& sim, Real a0, Real a1) {
    std::vector<int> out;
    const Vec3 ax = sim.hook.axis();
    for (int i = 0; i < sim.rs.particleCount(); ++i) {
        const Vec3 d = sim.rs.x[i] - sim.hook.tip;
        const Real a = dot(d, ax);
        if (a > a0 && a < a1 && length(d - ax * a) < sim.shape.R + 2.0 * sim.params.radius) out.push_back(i);
    }
    return out;
}
}  // namespace

// The hook pushed on, tip first, a little at a time, until no yarn round the hook is nearer the tip than `clear` (the throat is free): the loop on
// the hook rides back onto the shaft. `lift` is spread over the push (so the tip passes over what hangs below it).
void seatLoop(YarnSim& sim, Real clear, const Vec3& lift = Vec3{}) {
    const Vec3 ax = sim.hook.axis();
    const Real step = 0.25e-3, most = 0.02;
    const Vec3 dl = lift * (step / most);
    for (Real moved = 0; moved < most; moved += step) {
        bool inThroat = false;
        for (int i : roundHook(sim, -sim.shape.R, clear)) { (void)i; inThroat = true; break; }
        if (!inThroat) break;
        sim.step(hookPose(sim.hook.tip - ax * step + dl, ax * -1.0, sim.hook.throat()));
        if (sim.onFrame) sim.onFrame();
    }
}

ChainStart prepareChain(YarnSim& sim) {
    ChainStart c;
    const Real m = kMm, R = sim.shape.R;
    Vec3 loop;
    Real along = 0;
    if (!loopOnShaft(sim, &loop, &along)) return c;
    const Vec3 ax = sim.hook.axis();            // tip -> handle (the hook lies tip to the left)
    const Vec3 up{0, 1, 0}, front{0, 0, 1};
    // the left hand: thumb and middle finger pinch the work just below the loop on the hook; the index finger, up behind the loop, carries the yarn
    sim.phase = "The left hand holds the work below the hook; the yarn runs up over its index finger";
    {
        // what the thumb and middle finger pinch: the work just under the loop on the hook, i.e. the yarn that comes before the loop (the stitch
        // below it), not the loop itself
        const std::vector<int> on = roundHook(sim, 0.0, 14.0 * R);
        int loopMin = sim.rs.particleCount();
        for (int i : on) loopMin = std::min(loopMin, i);
        YarnSim::Grip g;
        const int from = std::max(1, loopMin - (int)envOr("HOLD_N", 22)), to = loopMin - (int)envOr("HOLD_GAP", 4);
        Vec3 at{};
        for (int i = from; i <= to; ++i) at += sim.rs.x[i];
        at = at / (Real)std::max(1, to - from + 1);
        g.at = at;
        for (int i = from; i <= to; ++i) {
            const Vec3 d = sim.rs.x[i] - sim.hook.tip;
            const Real a = dot(d, ax);
            if (a > -2 * R && a < 14 * R && length(d - ax * a) < R + 2.0 * sim.params.radius) continue;   // on the hook
            g.particles.push_back(i);
            g.offset.push_back(sim.rs.x[i] - at);
            sim.rs.pin(i);
        }
        sim.grips.push_back(g);
        c.hold = (int)sim.grips.size() - 1;
        std::printf("chain: the hand holds %zu particles of the work\n", g.particles.size());
    }
    if (envOr("FINGER", 1) > 0 && !sim.fingerOn) {
        // the index finger slipped under the working yarn a little way from the loop: from now on the yarn runs over it
        int loopMax = -1;
        for (int i : roundHook(sim, 0.0, 14.0 * R)) loopMax = std::max(loopMax, i);
        (void)loopMax;
        const int at = sim.rs.particleCount() - 6;   // near the hand: the finger is slipped under the yarn there, well away from the hook
        sim.putFinger(at, up * -1.0);
        if (sim.fingerEyelet) {
            moveHandTo(sim, sim.hand, 0.1);
            goto fingerDone;
        }
        {
        const Vec3 spool0 = sim.ball.target;
        const Vec3 spool1 = sim.hand + sim.spoolOff;
        sim.fingerOn = false;   // the spool is brought round the finger first, then the finger takes over
        const int n = (int)(0.6 / sim.params.dt);
        StaticCapsule f;
        for (int k = 1; k <= n; ++k) {
            const Real t = (Real)k / n;
            sim.ball.target = spool0 + (spool1 - spool0) * (t * t * (3 - 2 * t));
            sim.fixtures.assign(1, StaticCapsule{sim.hand - sim.fingerAxis * (0.5 * sim.fingerLen), sim.hand + sim.fingerAxis * (0.5 * sim.fingerLen), sim.fingerR});
            sim.step(sim.hook);
            if (sim.onFrame) sim.onFrame();
        }
        sim.fixtures.clear();
        sim.fingerOn = true;
        }
    fingerDone:;
    }
    // the loop pushed back onto the shaft, clear of the throat: the hook slides on through it, tip first
    seatLoop(sim, envOr("SIM_SEAT", 5.0) * R);
    // the hook back to where it works from (the work is held; the hook's height and depth stay the same stitch after stitch, it moves left and
    // right only as the loop sits on it)
    if (!sim.homeSet) { sim.home = sim.hook.tip; sim.homeSet = true; }
    {
        Vec3 to = sim.home + ax * dot(sim.hook.tip - sim.home, ax);
        if (length(to - sim.hook.tip) > 0.3 * m) sim.moveHook(hookPose(to, -ax, sim.hook.throat()), 0.4);
    }
    loopOnShaft(sim, &loop, &along);
    // the working yarn tightened: the loop closes snug round the shaft
    sim.ball.tension = 4.0 * sim.params.tension;
    sim.settle(60);
    sim.ball.tension = sim.params.tension;
    c.oldLoop = roundHook(sim, 4.0 * R, 14.0 * R);
    for (int s = 0; s + 1 < sim.rs.segmentCount(); ++s) c.length += sim.rs.restLen[s];
    c.tip0 = sim.hook.tip;
    c.along = along;
    moveHandTo(sim, loop + up * (25 * m) - front * (15 * m), 0.7);
    sim.phase = "Yarn over: the yarn runs from the loop to the finger, left of the hook; the hook takes it into its throat";
    int loopTop = 0;
    for (int i : c.oldLoop) loopTop = std::max(loopTop, i);
    if (envOr("YO_MODE", 3) == 3) {
        // the finger takes the working yarn once round the neck, just right of the throat (between the throat and the old loop), starting and ending
        // behind the hook: the yarn from the old loop to the finger goes round the hook a full turn
        sim.moveHook(hookPose(c.tip0, -ax, normalized(front - up * envOr("YO_TD", 0.0))), 0.4);
        const Vec3 cc = c.tip0 + ax * (envOr("YO_AX", 2.5) * R);
        const Real rad = envOr("YO_R", 14) * m;
        const Real sense = envOr("YO_SENSE", -1);
        const Real a0 = envOr("YO_A0", 2.6);   // angle in the (front, up) plane: 0 in front, pi/2 above, pi behind
        auto at = [&](Real ang) { return cc + (front * std::cos(ang) + up * std::sin(ang)) * rad; };
        moveHandTo(sim, at(a0), 0.6);
        const int n = (int)(envOr("YO_T", 1.6) / sim.params.dt);
        for (int k = 1; k <= n; ++k) {
            const Real t = (Real)k / n;
            sim.hand = at(a0 - sense * envOr("YO_TURN", 1.0) * 2 * kPi * t * t * (3 - 2 * t));
            sim.step(sim.hook);
            if (sim.onFrame) sim.onFrame();
        }
    } else if (envOr("YO_MODE", 3) == 2) {
        // the finger behind the hook, to the left and above: the yarn runs from the loop back past the throat to it. The throat turned back and up
        // toward it, the hook moved into it until it lies in the throat
        const Vec3 thrBack = normalized(up * envOr("YO_TU", 0.6) - front);
        sim.moveHook(hookPose(c.tip0, -ax, thrBack), 0.4);
        moveHandTo(sim, c.tip0 - ax * (envOr("YO_L", 12) * m) + up * (envOr("YO_U", 12) * m) - front * (envOr("YO_B", 16) * m), 0.8);
        const Real step = 0.2e-3;
        for (Real moved = 0; moved < envOr("YO_LIFT", 10) * m; moved += step) {
            bool in = false;
            const Vec3 T = sim.throatPoint();
            for (int i = loopTop + 3; i < sim.rs.particleCount(); ++i)
                if (length(sim.rs.x[i] - T) < 1.6 * m) { in = true; break; }
            if (in) break;
            sim.step(hookPose(sim.hook.tip + thrBack * step, -ax, sim.hook.throat()));
            if (sim.onFrame) sim.onFrame();
        }
    } else {
        // the index finger takes the yarn once round the hook just in front of the throat, from behind, over the top, to in front and below
        const Vec3 thr = normalized(front - up * envOr("YO_TD", 0.3));
        sim.moveHook(hookPose(c.tip0, -ax, thr), 0.5);
        const Vec3 cc = c.tip0 + ax * (envOr("YO_AX", 3.0) * R);
        const Real rad = envOr("YO_R", 15) * m;
        const Real a0 = envOr("YO_A0", 2.4), a1 = envOr("YO_A1", -0.6);
        auto at = [&](Real ang) { return cc + (front * std::cos(ang) + up * std::sin(ang)) * rad; };
        moveHandTo(sim, at(a0), 0.6);
        const int n = (int)(envOr("YO_T", 1.2) / sim.params.dt);
        for (int k = 1; k <= n; ++k) {
            const Real t = (Real)k / n;
            (sim.fingerOn ? sim.hand : sim.ball.target) = at(a0 + (a1 - a0) * t * t * (3 - 2 * t));
            sim.step(sim.hook);
            if (sim.onFrame) sim.onFrame();
        }
    }
    // the throat holds what it caught from now until the new loop is on the hook
    sim.setThroatRing(true, loopTop + 3);
    std::printf("yarn over: %s, the working yarn winds %.2f\n", sim.throatRingOn() ? "caught in the throat" : "NOT caught",
                sim.windingNear(loopTop + 3, sim.rs.particleCount() - 1, 0.009));
    if (envOr("SIM_YODBG", 0) > 0) {
        std::printf("after the yarn over, yarn round the hook (index: axial/R, angle deg):");
        for (int i : roundHook(sim, -1.0 * R, 14.0 * R)) {
            const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip);
            std::printf(" %d:%.1f/%.0f", i, l.z / R, std::atan2(l.y, l.x) * 180 / kPi);
        }
        std::printf("\n");
    }
    c.ok = true;
    return c;
}

void finishChain(YarnSim& sim, const ChainStart& c) {
    const Real m = kMm, R = sim.shape.R;
    const Vec3 ax = sim.hook.axis();
    const Vec3 up{0, 1, 0}, front{0, 0, 1};
    int oldLo = 1 << 30, oldHi = -1;
    for (int i : c.oldLoop) { oldLo = std::min(oldLo, i); oldHi = std::max(oldHi, i); }
    const int oldMid = (oldLo + oldHi) / 2;
    auto stage = [&](const char* what) {
        if (envOr("SIM_STAGES", 0) <= 0) return;
        int f = 0, t = 0;
        (void)f; (void)t;
        const int n = sim.rs.particleCount();
        std::printf("  %s: carried %d of 360 degrees, throat %s, new yarn winds %.2f\n", what, carriedRound(sim), sim.throatRingOn() ? "holds" : "empty",
                    sim.windingNear(oldHi + 3, n - 1, 0.009));
    };
    stage("caught");
    // the hook turned toward the crocheter until its throat faces down, the yarn caught in it
    sim.phase = "The hook turns toward you, its throat down, the yarn caught in it";
    const Vec3 thrDown = normalized(front * envOr("PU_RF", 0.4) - up);
    const Real rt = envOr("PU_RT", 0.6) / 3;
    sim.moveHook(hookPose(sim.hook.tip, -ax, normalized(up + front * 0.2)), rt);
    sim.moveHook(hookPose(sim.hook.tip, -ax, normalized(front + up * 0.2)), rt);
    sim.moveHook(hookPose(sim.hook.tip, -ax, thrDown), rt);
    stage("turned");
    // drawn back through the loop, with a slight lift: the work held, the hook moves to the right a little each frame until the old loop has come
    // off over the head
    sim.phase = "Pull through: the hook draws the yarn through the loop on the hook";
    {
        const Real step = envOr("PU_V", 12) * m * sim.params.dt;
        const Real follow = envOr("PU_FOLLOW", 1.0);   // the finger travels with the hook (its yarn keeps leaving the throat square to it)
        int clear = 0;
        for (Real moved = 0; moved < 0.05 && clear < (int)envOr("PU_EXTRA", 6); moved += step) {
            sim.hand += ax * (step * follow);
            sim.step(hookPose(sim.hook.tip + ax * step + up * (step * envOr("PU_LIFT", 0.0)), -ax, thrDown));
            if (sim.onFrame) sim.onFrame();
            int on = 0;
            for (int i : roundHook(sim, -1.0 * R, sim.shape.length))
                if (i >= oldLo && i <= oldMid) ++on;
            clear = on == 0 ? clear + 1 : 0;
        }
    }
    stage("pulled");
    if (envOr("PU_STOP", 0) > 0) { sim.setThroatRing(false); return; }
    // the throat turned back up toward the crocheter: the new loop on the hook; the throat lets go
    sim.phase = "The throat turns back up: the new loop is on the hook";
    {
        const Real bt = envOr("PU_BT", 0.6) / 3;
        const int path = (int)envOr("PU_BACK", 1);
        const Vec3 back = front * -1.0;
        if (path == 1) {   // on round the same way: down, back, up, toward the crocheter
            sim.moveHook(hookPose(sim.hook.tip, -ax, normalized(back - up * 0.3)), bt);
            sim.moveHook(hookPose(sim.hook.tip, -ax, normalized(up + back * 0.3)), bt);
            sim.moveHook(hookPose(sim.hook.tip, -ax, normalized(front * 0.6 + up)), bt);
        } else if (path == 2) {   // back the way it came: down, front, up
            sim.moveHook(hookPose(sim.hook.tip, -ax, normalized(front + up * 0.2)), bt);
            sim.moveHook(hookPose(sim.hook.tip, -ax, normalized(up + front * 0.3)), bt);
        } else if (path == 0) {
            sim.moveHook(hookPose(sim.hook.tip, -ax, normalized(front + up * 0.5)), 3 * bt);
        }   // other: the throat stays as it is
    }
    sim.setThroatRing(false);
    stage("back");
    if (envOr("PU_STOP2", 0) > 0) return;
    // the yarn drawn snug at the finger, the loop seated on the shaft clear of the throat, the work let go
    sim.ball.tension = envOr("PU_SNUG", 3.0) * sim.params.tension;
    sim.settle((int)(0.5 / sim.params.dt));
    sim.ball.tension = sim.params.tension;
    stage("snug");
    if (envOr("PU_SEAT", 0) > 0) seatLoop(sim, envOr("SIM_SEAT", 5.0) * R, up * (envOr("PU_SUP", 0) * m));
    sim.release(c.hold);
    sim.settle(20);
    stage("seated");
}

Real YarnSim::hookLinking() const {
    // the hook's axis, long, closed far in front; the yarn closed far below and behind (its ends: the tail hangs below, the hand is above or behind)
    const Vec3 ax = hook.axis();
    const Real L = 0.6;
    const Vec3 front{0, 0, 1}, up{0, 1, 0};
    std::vector<Vec3> H{hook.tip - ax * L, hook.tip + ax * L, hook.tip + ax * L + front * 2.0, hook.tip - ax * L + front * 2.0};
    std::vector<Vec3> Y = rs.x;
    const Vec3 t = rs.x[tail.particle], h = rs.x[ball.particle];
    Y.push_back(h + up * 1.5);
    Y.push_back(Vec3{h.x, h.y + 1.5, -2.0});
    Y.push_back(Vec3{t.x, t.y - 1.5, -2.0});
    Y.push_back(t - up * 1.5);
    // the yarn runs tail first: close it back to the tail through the far points (reversed so the polygon is in order)
    std::vector<Vec3> P;
    P.insert(P.end(), rs.x.begin(), rs.x.end());
    P.push_back(h + up * 1.5);
    P.push_back(Vec3{h.x, h.y + 1.5, -2.0});
    P.push_back(Vec3{t.x, t.y - 1.5, -2.0});
    P.push_back(t - up * 1.5);
    (void)Y;
    return linkingNumber(P, H);
}

bool hookLoop(const YarnSim& sim, int* from, int* to) {
    // the hook's axis, closed far away
    const Vec3 ax = sim.hook.axis();
    Vec3 u = Vec3{0, 0, 1} - ax * ax.z;
    if (length(u) < 1e-6) u = Vec3{0, 1, 0} - ax * ax.y;
    u = normalized(u);
    const std::vector<Vec3> H{sim.hook.tip - ax * 0.05, sim.hook.tip + ax * 0.08, sim.hook.tip + ax * 0.08 + u * 1.0, sim.hook.tip - ax * 0.05 + u * 1.0};
    // stretches of yarn closed where the yarn touches itself
    const int n = sim.rs.particleCount();
    const Real touch2 = std::pow(5.0 * sim.params.radius, 2);   // (a loose crossing counts)
    int bestSpan = 1 << 30;
    bool found = false;
    for (int i = 0; i < n; ++i)
        for (int j = i + 12; j < n; ++j) {
            if (j - i >= bestSpan) break;
            if (length2(sim.rs.x[i] - sim.rs.x[j]) > touch2) continue;
            const std::vector<Vec3> P(sim.rs.x.begin() + i, sim.rs.x.begin() + j + 1);
            if (std::fabs(std::fabs(linkingNumber(P, H)) - 1.0) < 0.25) {
                bestSpan = j - i;
                if (from) *from = i;
                if (to) *to = j;
                found = true;
            }
        }
    return found;
}

int carriedRound(const YarnSim& sim0) {
    // copies of the work, its ends and the hand held where they are, the throat letting go; in each the hook moves 12 mm square to its axis in one
    // of 8 directions: a loop round the hook goes along every way, yarn only lying over or in it is left behind one way at least
    const Vec3 ax = sim0.hook.axis();
    Vec3 u = Vec3{0, 1, 0} - ax * ax.y;
    u = normalized(u);
    const Vec3 v = cross(ax, u);
    int carried = 0;
    for (int k = 0; k < 8; ++k) {
        const Real a = k * kPi / 4;
        const Vec3 d = u * std::cos(a) + v * std::sin(a);
        YarnSim t = sim0;
        t.onFrame = nullptr;
        t.setThroatRing(false);
        t.catchK = 0;
        if (t.tail.particle >= 0) t.tail.target = t.rs.x[t.tail.particle];
        t.moveHook(hookPose(t.hook.tip + d * 0.012, ax * -1.0, t.hook.throat()), 0.5);
        Real best = 1e9;
        for (const Vec3& p : t.rs.x) {
            const Vec3 q = p - t.hook.tip;
            const Real along = dot(q, ax);
            if (along < 0 || along > 0.05) continue;
            best = std::min(best, length(q - ax * along));
        }
        if (best < t.shape.R + 1.5 * t.params.radius) ++carried;
    }
    return carried * 45;
}

ChainResult judgeChain(const YarnSim& sim, const ChainStart& c) {
    ChainResult r;
    r.link = carriedRound(sim);
    r.loops = sim.loopsOnHook(Vec3{0, 1, 0});
    Real len = 0;
    for (int s = 0; s + 1 < sim.rs.segmentCount(); ++s) len += sim.rs.restLen[s];
    r.length = len;
    if (!c.oldLoop.empty()) {
        // the first half of the old loop's yarn (the part furthest from where yarn slides in from the hand) must be off the hook
        int lo = 1 << 30, hi = -1;
        for (int i : c.oldLoop) { lo = std::min(lo, i); hi = std::max(hi, i); }
        const int mid = (lo + hi) / 2;
        for (int i : roundHook(sim, -sim.shape.R, sim.shape.length))
            if (i >= lo && i <= mid) ++r.oldOn;
    }
    int lf = 0, lt = 0;
    const bool has = hookLoop(sim, &lf, &lt);
    r.loopFrom = has ? lf : -1;
    r.loopTo = has ? lt : -1;
    int oldLo = 1 << 30, oldHi = -1;
    for (int i : c.oldLoop) { oldLo = std::min(oldLo, i); oldHi = std::max(oldHi, i); }
    r.link = carriedRound(sim);
    r.made = r.link >= 360 && r.oldOn == 0 && len > c.length + 0.006;
    return r;
}

void makeChain(YarnSim& sim) {
    const ChainStart c = prepareChain(sim);
    if (c.ok) finishChain(sim, c);
}

}  // namespace fl

