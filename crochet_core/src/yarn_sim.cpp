#include "failoring/crochet/yarn_sim.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#ifndef FL_GIT_HASH
#define FL_GIT_HASH "unknown"
#endif
#if defined(__APPLE__) || defined(__linux__)
extern char** environ;
#endif

namespace fl {

namespace {

const Real kMm = 1e-3;

Real envOr(const char* k, Real d) { return std::getenv(k) ? std::atof(std::getenv(k)) : d; }

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

namespace {
Real segDist(const Vec3& p, const Vec3& a, const Vec3& b) {
    const Vec3 ab = b - a;
    const Real t = std::clamp(dot(p - a, ab) / std::max(1e-18, dot(ab, ab)), 0.0, 1.0);
    return length(p - (a + ab * t));
}
Real smin(Real a, Real b, Real k) {
    const Real h = std::clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return b + (a - b) * h - k * h * (1 - h);
}
Real smax(Real a, Real b, Real k) { return -smin(-a, -b, k); }
Real capsuleDist(const Vec3& p, const Vec3& a, const Vec3& b, Real r) { return segDist(p, a, b) - r; }
// The hook, in its own frame (z from the tip toward the handle, the throat opening toward +x): a smooth union (min, rounded at the joins) of
// its real parts rather than a cylinder with a slot cut into it. A slot cut with max() is only sign-correct where it is cut: just outside the
// un-cut cylinder it still returns the cylinder's distance, so a yarn wider than the blend is pushed out of a mouth that is really open. A union
// of primitives is a true distance field and can be used with contact radii.
Real hookSdfInline(const Vec3& p, const HookShape& s) {
    const Real R = s.R;
    auto W = [&](Real x, Real z) { return Vec3{x * R, 0, z * R}; };
    // shaft: a capsule whose full radius is reached at shaftZ; the rounded front of the capsule is the shoulder behind the throat
    Real d = capsuleDist(p, W(0, s.shaftZ), W(0, s.length / R), R);
    // spine: a thick neck behind the throat, its rounded front meeting the head's back wall. The mouth side is cut away by the groove
    // below, leaving a spine at least neckR thick behind the yarn.
    d = smin(d, capsuleDist(p, W(s.neckX, s.headBack + s.neckR), W(s.neckX, s.shaftZ + 0.1), s.neckR * R), 0.12 * R);
    // head: a dome on the axis from the tip to a flat back at the front of the throat (the back edge is the barb: a wall a yarn drawn
    // toward the tip is stopped by, while the mouth beside it stays open so the yarn can be laid in from the throat's side)
    Real head = capsuleDist(p, W(0.0, 0.35), W(0.0, 1.0), 0.62 * R);
    head = smin(head, capsuleDist(p, W(0.0, 0.9), W(0.0, 1.5), 0.92 * s.headR * R), 0.12 * R);
    head = smin(head, capsuleDist(p, W(0.0, 1.4), W(0.0, 1.8), s.headR * R), 0.10 * R);
    if (s.flatBack == 1) {
        head = smax(head, p.z - s.headBack * R, 0.08 * R);
    } else if (s.flatBack == 2) {
        // The wall only where the throat is: the barb's back face stops the yarn over, the smooth far side lets an old loop ride off.
        head = smax(head, std::max(p.z - s.headBack * R, -p.x), 0.08 * R);
    }
    // barb: a raised lip along the back edge inside the head's envelope, on the mouth side. The groove pushes the yarn toward the mouth;
    // without the lip it slips out round the head's edge (the hook passes it by), with it the yarn is held between the groove and the lip.
    head = smin(head, capsuleDist(p, Vec3{s.barbX * R, -s.barbHalfWidth * R, (s.headBack - 0.05) * R},
                               Vec3{s.barbX * R, s.barbHalfWidth * R, (s.headBack - 0.05) * R}, s.barbR * R),
                0.10 * R);
    if (s.rim) {
        const Real rr = 0.22 * R, zr = 2.15 * R;
        head = smin(head, capsuleDist(p, Vec3{-0.95 * R, -0.90 * R, zr}, Vec3{-0.95 * R, 0.90 * R, zr}, rr), 0.06 * R);
        head = smin(head, capsuleDist(p, Vec3{-0.95 * R, -0.90 * R, zr}, Vec3{0.90 * R, -0.90 * R, zr}, rr), 0.06 * R);
        head = smin(head, capsuleDist(p, Vec3{-0.95 * R, 0.90 * R, zr}, Vec3{0.90 * R, 0.90 * R, zr}, rr), 0.06 * R);
    }
    d = smin(d, head, 0.15 * R);
    if (s.groove) {
        // cut the open channel across the mouth side, behind the head's back wall: the yarn lies in the groove with its centre inside the
        // shaft's envelope, so the head and yarn together pass through the old loop instead of jamming.
        const Real cut = capsuleDist(p, Vec3{s.grooveX * R, -1.30 * R, s.grooveZ * R}, Vec3{s.grooveX * R, 1.30 * R, s.grooveZ * R}, s.grooveR * R);
        d = smax(d, -cut, 0.06 * R);
    }
    return d;
}
// The committed hook of f19157a, verbatim (its comment above is the one it had there). The default shape; the check against f19157a's text is in
// test_chain_judge.
Real hookSdfLegacy(const Vec3& p, const HookShape& s) {
    const Real R = s.R;
    auto W = [&](Real x, Real z) { return Vec3{x * R, 0, z * R}; };
    // shaft: a capsule along the axis beyond the taper
    Real d = capsuleDist(p, W(0, 5.2), W(0, s.length / R), R);
    // taper: three capsules of growing radius, so it reads as a cone from the neck up to the shaft
    d = smin(d, capsuleDist(p, W(-0.10, 4.9), W(0, 5.4), 0.85 * R), 0.10 * R);
    d = smin(d, capsuleDist(p, W(-0.22, 4.5), W(-0.12, 5.0), 0.65 * R), 0.10 * R);
    d = smin(d, capsuleDist(p, W(-0.36, 4.2), W(-0.28, 4.6), 0.48 * R), 0.10 * R);
    // neck: the spine of the hook, on the far side of the throat from the mouth; it leaves the groove open toward +x
    d = smin(d, capsuleDist(p, W(s.neckX, 2.05), W(s.neckX, 4.2), s.neckR * R), 0.12 * R);
    // head: a dome on the axis from the tip to a flat back at the front of the groove (the back edge is the barb: a wall a yarn drawn
    // toward the tip is stopped by, while the mouth beside it stays open so the yarn can be laid in from the throat's side)
    Real head = capsuleDist(p, W(0.0, 0.35), W(0.0, 1.0), 0.62 * R);
    head = smin(head, capsuleDist(p, W(0.0, 0.9), W(0.0, 1.5), 0.92 * s.headR * R), 0.12 * R);
    head = smin(head, capsuleDist(p, W(0.0, 1.4), W(0.0, 1.8), s.headR * R), 0.10 * R);
    if (s.flatBack == 1) {
        head = smax(head, p.z - 2.05 * R, 0.08 * R);
    } else if (s.flatBack == 2) {
        // The wall only where the throat is: the barb's back face stops the yarn over, the smooth far side lets an old loop ride off.
        head = smax(head, std::max(p.z - 2.05 * R, -p.x), 0.08 * R);
    }
    // barb: a raised lip along the back edge on the mouth side. The neck pushes the yarn toward the mouth; without the lip it slips out round
    // the head's edge (the hook passes it by), with it the yarn is held between the neck and the lip, in front of the head's back.
    head = smin(head, capsuleDist(p, Vec3{s.barbX * R, -s.barbHalfWidth * R, 2.0 * R}, Vec3{s.barbX * R, s.barbHalfWidth * R, 2.0 * R}, s.barbR * R), 0.10 * R);
    if (s.rim) {
        const Real rr = 0.22 * R, zr = 2.15 * R;
        head = smin(head, capsuleDist(p, Vec3{-0.95 * R, -0.90 * R, zr}, Vec3{-0.95 * R, 0.90 * R, zr}, rr), 0.06 * R);
        head = smin(head, capsuleDist(p, Vec3{-0.95 * R, -0.90 * R, zr}, Vec3{0.90 * R, -0.90 * R, zr}, rr), 0.06 * R);
        head = smin(head, capsuleDist(p, Vec3{-0.95 * R, 0.90 * R, zr}, Vec3{0.90 * R, 0.90 * R, zr}, rr), 0.06 * R);
    }
    d = smin(d, head, 0.15 * R);
    if (s.groove) {
        // cut an open channel across the mouth side, between the tip lobe and the barb: the yarn can then be laid into the throat
        // with its centre inside the head's envelope, so the head and yarn together pass through the old loop instead of jamming.
        const Real cut = capsuleDist(p, Vec3{s.grooveX * R, -1.30 * R, s.grooveZ * R}, Vec3{s.grooveX * R, 1.30 * R, s.grooveZ * R}, s.grooveR * R);
        d = smax(d, -cut, 0.06 * R);
    }
    return d;
}

Real hookSdfRaw(const Vec3& p, const HookShape& s) { return s.form == HookShape::Form::Inline ? hookSdfInline(p, s) : hookSdfLegacy(p, s); }

}  // namespace

HookShape HookShape::inlineHook() {
    HookShape s;
    s.form = Form::Inline;
    s.barbX = 0.55;
    s.barbHalfWidth = 0.75;
    s.barbR = 0.18;
    s.neckX = -0.20;
    s.neckR = 0.80;
    s.groove = true;
    s.grooveX = 0.55;
    s.grooveZ = 2.80;
    s.grooveR = 0.60;
    return s;
}

Real hookSdf(const Vec3& p, const HookShape& s, Vec3* grad) {
    const Real d = hookSdfRaw(p, s);
    if (grad) {
        const Real h = 2e-5;
        Vec3 g{hookSdfRaw(p + Vec3{h, 0, 0}, s) - hookSdfRaw(p - Vec3{h, 0, 0}, s), hookSdfRaw(p + Vec3{0, h, 0}, s) - hookSdfRaw(p - Vec3{0, h, 0}, s),
               hookSdfRaw(p + Vec3{0, 0, h}, s) - hookSdfRaw(p - Vec3{0, 0, h}, s)};
        const Real gl = length(g);
        *grad = gl > 1e-12 ? g / gl : Vec3{1, 0, 0};
    }
    return d;
}

YarnMesh hookMesh(const HookPose& pose, const HookShape& s, Real cell) {
    YarnMesh m;
    const Vec3 col{0.74, 0.76, 0.80};
    const Real R = s.R;
    const Real z1 = 6.4 * R;   // marched up to here, a tube after
    const Real xb = 1.75;      // the box the head and barb are marched in, past the barb's outer face
    const int nx = (int)std::ceil(2.0 * xb * R / cell), nz = (int)std::ceil((z1 + 1.3 * R) / cell);
    const Real x0 = -xb * R, y0 = -xb * R, zs = -1.3 * R;
    auto P = [&](int i, int j, int k) { return Vec3{x0 + i * cell, y0 + j * cell, zs + k * cell}; };
    std::vector<Real> F((size_t)(nx + 1) * (nx + 1) * (nz + 1));
    auto idx = [&](int i, int j, int k) { return ((size_t)k * (nx + 1) + j) * (nx + 1) + i; };
    for (int k = 0; k <= nz; ++k)
        for (int j = 0; j <= nx; ++j)
            for (int i = 0; i <= nx; ++i) {
                Vec3 q = P(i, j, k);
                Real d = hookSdf(q, s, nullptr);
                if (q.z > z1) d = std::max(d, z1 - q.z);   // closed where the tube takes over (hidden inside it)
                F[idx(i, j, k)] = d;
            }
    // marching tetrahedra: each cube into 6 tetrahedra
    static const int tets[6][4] = {{0, 5, 1, 6}, {0, 1, 2, 6}, {0, 2, 3, 6}, {0, 3, 7, 6}, {0, 7, 4, 6}, {0, 4, 5, 6}};
    static const int cornerOff[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
    auto emit = [&](const Vec3& a, const Vec3& b, const Vec3& c) {
        const int base = (int)m.pos.size();
        for (const Vec3& v : {a, b, c}) {
            Vec3 g;
            hookSdf(v, s, &g);
            m.pos.push_back(pose.tip + rotate(pose.q, v));
            m.nrm.push_back(rotate(pose.q, g));
            m.col.push_back(col);
        }
        // wind so the face normal agrees with the gradient (outward)
        const Vec3 fn = cross(b - a, c - a);
        Vec3 g;
        hookSdf((a + b + c) / 3.0, s, &g);
        if (dot(fn, g) >= 0) m.tri.push_back({base, base + 1, base + 2});
        else m.tri.push_back({base, base + 2, base + 1});
    };
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < nx; ++j)
            for (int i = 0; i < nx; ++i) {
                Vec3 cp[8];
                Real cv[8];
                bool any = false, anyIn = false, anyOut = false;
                for (int c = 0; c < 8; ++c) {
                    const int ii = i + cornerOff[c][0], jj = j + cornerOff[c][1], kk = k + cornerOff[c][2];
                    cp[c] = P(ii, jj, kk);
                    cv[c] = F[idx(ii, jj, kk)];
                    if (cv[c] < 0) anyIn = true; else anyOut = true;
                }
                any = anyIn && anyOut;
                if (!any) continue;
                for (const auto& t : tets) {
                    std::vector<Vec3> pts;
                    int in[4], nIn = 0, out[4], nOut = 0;
                    for (int v = 0; v < 4; ++v) (cv[t[v]] < 0 ? in[nIn++] : out[nOut++]) = t[v];
                    if (nIn == 0 || nOut == 0) continue;
                    auto cut = [&](int a, int b) { const Real u = cv[a] / (cv[a] - cv[b]); return cp[a] + (cp[b] - cp[a]) * u; };
                    if (nIn == 1) emit(cut(in[0], out[0]), cut(in[0], out[1]), cut(in[0], out[2]));
                    else if (nIn == 3) emit(cut(out[0], in[0]), cut(out[0], in[1]), cut(out[0], in[2]));
                    else {
                        const Vec3 a = cut(in[0], out[0]), b = cut(in[0], out[1]), c2 = cut(in[1], out[0]), d = cut(in[1], out[1]);
                        emit(a, b, d);
                        emit(a, d, c2);
                    }
                }
            }
    // the shaft beyond: a tube
    YarnMeshOptions o;
    o.plyContrast = 0.0;
    o.sides = 24;
    o.subdiv = 1;
    std::vector<Vec3> sp;
    for (int k = 0; k <= 6; ++k) sp.push_back(pose.tip + rotate(pose.q, Vec3{0, 0, (z1 - 0.5 * cell) + (s.length - z1) * k / 6.0}));
    const YarnMesh tube = buildTubeMesh(sp, R, col, o);
    const int off = (int)m.pos.size();
    m.pos.insert(m.pos.end(), tube.pos.begin(), tube.pos.end());
    m.nrm.insert(m.nrm.end(), tube.nrm.begin(), tube.nrm.end());
    m.col.insert(m.col.end(), tube.col.begin(), tube.col.end());
    for (const auto& t : tube.tri) m.tri.push_back({t[0] + off, t[1] + off, t[2] + off});
    return m;
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
    rs.restBendMemory = 0.0;   // yarn paid out from the hand is straight at rest (it does not keep the bend it had when it was added)
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
        if (assistReel <= 0) continue;
        const Real f = reelForce * e.tension * assistReel;
        rs.extForce[i] = normalized(rs.x[i + 1] - rs.x[i - 1]) * f;
        impulseReel += f * params.dt;
        reeled_.push_back(i);
    }
}

Vec3 YarnSim::throatPoint() const {
    // the middle of the cradle: in the open groove behind the head, beside the neck
    return hook.tip + rotate(hook.q, Vec3{throatX * shape.R, 0, throatZ * shape.R});
}

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
    if (!on || assistRing <= 0) {
        dropRing(rs, throatRing_, fingerRing_);
        return;
    }
    const Vec3 T = throatPoint();
    Real d2 = 0;
    const int best = nearestSegment(rs, T, fromParticle, &d2);
    if (best < 0 || d2 > 25e-6) return;   // nothing within 5 mm of the throat: keep the ring that is already there
    dropRing(rs, throatRing_, fingerRing_);
    SlideRing ring;
    ring.at = T;
    ring.seg = best;
    ring.stiffness = throatStiffness * assistRing;
    rs.rings.push_back(ring);
    throatRing_ = (int)rs.rings.size() - 1;
}

void YarnSim::gripThroat(int particle) {
    releaseThroatGrip();
    if (particle < 0 || particle >= rs.particleCount() || assistGrip <= 0) return;
    catchOffset_ = rotate(conj(hook.q), rs.x[particle] - hook.tip);   // hold it where the barb found it, on the surface
    catchPin_ = particle;
    catchSpring_ = addGuideSpring(particle, hook.tip + rotate(hook.q, catchOffset_), 2000.0 * assistGrip, 1);
    setThroatRing(false);   // the grip replaces the sliding ring
}

void YarnSim::releaseThroatGrip() {
    if (catchSpring_ >= 0) releaseGuideSpring(catchSpring_);
    catchSpring_ = -1;
    catchPin_ = -1;
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
    if (shape.sdf) {
        rs.statics.clear();
        if (rs.bodies.empty()) {
            SdfBody b;
            const HookShape sh = shape;
            b.f = [sh](const Vec3& p, Vec3* g) { return hookSdf(p, sh, g); };
            b.squash = shape.squash;
            b.friction = params.hookFriction;
            b.reach = shape.length + 0.005;
            rs.bodies.push_back(b);
        }
        rs.bodies[0].q = hook.q;
        rs.bodies[0].t = hook.tip;
        rs.bodies[0].friction = params.hookFriction;
    } else {
        rs.statics = hookCapsules(hook, shape);
    }
    rs.statics.insert(rs.statics.end(), fixtures.begin(), fixtures.end());
    if (fingerOn && fingerEyelet) {
        ball.target = hand;   // the yarn is fed over the fingertip: the finger is where it comes from
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
    if (catchSpring_ >= 0) moveGuideSpring(catchSpring_, hook.tip + rotate(hook.q, catchOffset_));
    controlEnd(tail);
    if (driveOn_) applyRing();
    controlEnd(ball);
    for (const Grip& g : grips)
        for (size_t k = 0; k < g.particles.size(); ++k) rs.movePin(g.particles[k], g.at + g.offset[k]);
    for (int i : caught_)
        if (i < rs.particleCount() && std::find(reeled_.begin(), reeled_.end(), i) == reeled_.end()) rs.extForce[i] = Vec3{};
    caught_.clear();
    if (catchK > 0 && assistCatch > 0) {
        // the middle of the throat: just behind the head, in the groove (the yarn's squashed radius out from the neck)
        const Vec3 T = throatPoint();
        const int n = rs.particleCount();
        for (int i = std::max(1, catchFrom); i + 1 < n; ++i) {
            if (i == ball.particle || i == pulled_ || rs.w[i] <= 0) continue;
            const Vec3 d = T - rs.x[i];
            if (length(d) > catchReach) continue;
            const Vec3 t = normalized(rs.x[i + 1] - rs.x[i - 1]);
            const Vec3 F = (d - t * dot(d, t)) * (catchK * assistCatch);
            rs.extForce[i] = F;
            impulseCatch += length(F) * params.dt;
            caught_.push_back(i);
        }
    }
    rs.step(params.dt);
    // the guide report: the ring and the hook's own body accumulated their impulses inside the step; the springs are measured from their
    // final error. The judge ignores all of these.
    if (throatRing_ >= 0 && throatRing_ < (int)rs.rings.size()) impulseRing += rs.rings[throatRing_].impulse;
    for (const fl::GuideSpring& g : rs.guideSprings) {
        if (g.particle < 0 || g.particle >= rs.particleCount() || g.k <= 0) continue;
        const Real f = g.k * length(g.target - rs.x[g.particle]);
        if (g.kind == 1) impulseGrip += f * params.dt;
        else impulsePins += f * params.dt;
    }
    impulseHook += rs.lastStaticsImpulse;
    if (!rs.bodies.empty()) impulseHook += rs.bodies[0].impulse;
    // running contact: every second frame (the plan allows this; a crossing lasts longer than a frame at the stitch's speeds)
    if ((frame & 1) == 0) {
        worstGap = std::min(worstGap, minGap());
        worstPen = std::max(worstPen, hookPenetration());
    }
    ++frame;
}


void YarnSim::readAssist() {
    assist = envOr("SIM_ASSIST", assist);
    assistGrip = envOr("SIM_ASSIST_GRIP", assist);
    assistRing = envOr("SIM_ASSIST_RING", assist);
    assistCatch = envOr("SIM_ASSIST_CATCH", assist);
    assistPins = envOr("SIM_ASSIST_PINS", assist);
    assistReel = envOr("SIM_ASSIST_REEL", assist);
}

int YarnSim::addGuideSpring(int particle, const Vec3& target, Real k, int kind) {
    if (particle < 0 || particle >= rs.particleCount() || k <= 0) return -1;
    fl::GuideSpring g;
    g.particle = particle;
    g.target = target;
    g.k = k;
    g.kind = kind;
    rs.guideSprings.push_back(g);
    return (int)rs.guideSprings.size() - 1;
}

void YarnSim::releaseGuideSprings() { rs.guideSprings.clear(); }

namespace {

const char kStateMagic[8] = {'F', 'L', 'Y', 'A', 'R', 'N', '0', '1'};
constexpr uint32_t kStateVersion = 2;

template <class T>
bool wr(FILE* f, const T& v) { return std::fwrite(&v, sizeof(T), 1, f) == 1; }
template <class T>
bool rd(FILE* f, T& v) { return std::fread(&v, sizeof(T), 1, f) == 1; }
bool wrBytes(FILE* f, const void* p, size_t n) { return n == 0 || std::fwrite(p, 1, n, f) == n; }
bool rdBytes(FILE* f, void* p, size_t n) { return n == 0 || std::fread(p, 1, n, f) == n; }
bool wrStr(FILE* f, const std::string& s) {
    const uint64_t n = s.size();
    return wr(f, n) && wrBytes(f, s.data(), (size_t)n);
}
bool rdStr(FILE* f, std::string& s) {
    uint64_t n = 0;
    if (!rd(f, n) || n > 10000000) return false;
    s.resize((size_t)n);
    return rdBytes(f, s.data(), (size_t)n);
}
template <class T>
bool wrVec(FILE* f, const std::vector<T>& v) {
    const uint64_t n = v.size();
    return wr(f, n) && wrBytes(f, v.data(), (size_t)n * sizeof(T));
}
template <class T>
bool rdVec(FILE* f, std::vector<T>& v) {
    uint64_t n = 0;
    if (!rd(f, n) || n > 10000000) return false;
    v.resize((size_t)n);
    return rdBytes(f, v.data(), (size_t)n * sizeof(T));
}

bool rdForm(FILE* f, HookShape::Form& form) {
    int v = 0;
    if (!rd(f, v)) return false;
    form = v == 1 ? HookShape::Form::Inline : HookShape::Form::Legacy;
    return true;
}
bool wrHook(FILE* f, const HookShape& s) {
    return wr(f, s.R) && wr(f, s.length) && wr(f, s.squash) && wr(f, s.sdf) && wr(f, s.barbX) && wr(f, s.barbHalfWidth) &&
           wr(f, s.barbR) && wr(f, s.headR) && wr(f, s.headBack) && wr(f, s.shaftZ) && wr(f, s.neckX) && wr(f, s.neckR) &&
           wr(f, s.flatBack) && wr(f, s.groove) && wr(f, s.grooveX) && wr(f, s.grooveZ) && wr(f, s.grooveR) && wr(f, s.rim) &&
           wr(f, (int)s.form);
}
bool rdHook(FILE* f, HookShape& s) {
    return rd(f, s.R) && rd(f, s.length) && rd(f, s.squash) && rd(f, s.sdf) && rd(f, s.barbX) && rd(f, s.barbHalfWidth) &&
           rd(f, s.barbR) && rd(f, s.headR) && rd(f, s.headBack) && rd(f, s.shaftZ) && rd(f, s.neckX) && rd(f, s.neckR) &&
           rd(f, s.flatBack) && rd(f, s.groove) && rd(f, s.grooveX) && rd(f, s.grooveZ) && rd(f, s.grooveR) && rd(f, s.rim) && rdForm(f, s.form);
}
bool sameHook(const HookShape& a, const HookShape& b) {
    return a.R == b.R && a.length == b.length && a.squash == b.squash && a.sdf == b.sdf && a.barbX == b.barbX &&
           a.barbHalfWidth == b.barbHalfWidth && a.barbR == b.barbR && a.headR == b.headR && a.headBack == b.headBack &&
           a.shaftZ == b.shaftZ && a.neckX == b.neckX && a.neckR == b.neckR && a.flatBack == b.flatBack && a.groove == b.groove &&
           a.grooveX == b.grooveX && a.grooveZ == b.grooveZ && a.grooveR == b.grooveR && a.rim == b.rim && a.form == b.form;
}
bool wrYsp(FILE* f, const YarnSimParams& p) {
    return wr(f, p.radius) && wr(f, p.segment) && wr(f, p.dt) && wr(f, p.tension) && wr(f, p.massScale) && wr(f, p.damping) &&
           wr(f, p.bend) && wr(f, p.friction) && wr(f, p.hookFriction) && wr(f, p.substeps) && wr(f, p.iterations);
}
bool rdYsp(FILE* f, YarnSimParams& p) {
    return rd(f, p.radius) && rd(f, p.segment) && rd(f, p.dt) && rd(f, p.tension) && rd(f, p.massScale) && rd(f, p.damping) &&
           rd(f, p.bend) && rd(f, p.friction) && rd(f, p.hookFriction) && rd(f, p.substeps) && rd(f, p.iterations);
}
bool sameYsp(const YarnSimParams& a, const YarnSimParams& b) {
    return a.radius == b.radius && a.segment == b.segment && a.dt == b.dt && a.tension == b.tension && a.massScale == b.massScale &&
           a.damping == b.damping && a.bend == b.bend && a.friction == b.friction && a.hookFriction == b.hookFriction &&
           a.substeps == b.substeps && a.iterations == b.iterations;
}
bool wrRod(FILE* f, const RodParams& p) {
    return wr(f, p.radius) && wr(f, p.linearDensity) && wr(f, p.EA) && wr(f, p.GAs) && wr(f, p.EI) && wr(f, p.GJ) && wr(f, p.friction) &&
           wr(f, p.staticFriction) && wr(f, p.contactSkin) && wr(f, p.contact) && wr(f, p.contactSkipSegments) && wr(f, p.radiusScale) &&
           wr(f, p.ribbonWidth) && wr(f, p.ribbonThickness) && wr(f, p.ribbonNormal) && wr(f, p.shapeStiffness) && wr(f, p.gravity) &&
           wr(f, p.damping) && wr(f, p.substeps) && wr(f, p.iterations) && wr(f, p.direct) && wr(f, p.maxMoveRadii) && wr(f, p.floorOn) &&
           wr(f, p.floorY);
}
bool rdRod(FILE* f, RodParams& p) {
    return rd(f, p.radius) && rd(f, p.linearDensity) && rd(f, p.EA) && rd(f, p.GAs) && rd(f, p.EI) && rd(f, p.GJ) && rd(f, p.friction) &&
           rd(f, p.staticFriction) && rd(f, p.contactSkin) && rd(f, p.contact) && rd(f, p.contactSkipSegments) && rd(f, p.radiusScale) &&
           rd(f, p.ribbonWidth) && rd(f, p.ribbonThickness) && rd(f, p.ribbonNormal) && rd(f, p.shapeStiffness) && rd(f, p.gravity) &&
           rd(f, p.damping) && rd(f, p.substeps) && rd(f, p.iterations) && rd(f, p.direct) && rd(f, p.maxMoveRadii) && rd(f, p.floorOn) &&
           rd(f, p.floorY);
}
bool sameRod(const RodParams& a, const RodParams& b) {
    return a.radius == b.radius && a.linearDensity == b.linearDensity && a.EA == b.EA && a.GAs == b.GAs && a.EI == b.EI && a.GJ == b.GJ &&
           a.friction == b.friction && a.staticFriction == b.staticFriction && a.contactSkin == b.contactSkin && a.contact == b.contact &&
           a.contactSkipSegments == b.contactSkipSegments && a.radiusScale == b.radiusScale && a.ribbonWidth == b.ribbonWidth &&
           a.ribbonThickness == b.ribbonThickness && a.ribbonNormal.x == b.ribbonNormal.x && a.ribbonNormal.y == b.ribbonNormal.y &&
           a.ribbonNormal.z == b.ribbonNormal.z && a.shapeStiffness == b.shapeStiffness && a.gravity.x == b.gravity.x &&
           a.gravity.y == b.gravity.y && a.gravity.z == b.gravity.z && a.damping == b.damping && a.substeps == b.substeps &&
           a.iterations == b.iterations && a.direct == b.direct && a.maxMoveRadii == b.maxMoveRadii && a.floorOn == b.floorOn &&
           a.floorY == b.floorY;
}
std::vector<std::string> envKnobs() {
    std::vector<std::string> out;
#if defined(__APPLE__) || defined(__linux__)
    for (char** e = environ; e && *e; ++e) {
        const std::string s(*e);
        const size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = s.substr(0, eq);
        static const char* pre[] = {"SIM_", "CH_", "HOLD_", "HOME_", "HOOK_", "PREP_", "PU_", "SEAT_", "SLIP_", "YO_"};
        bool keep = false;
        for (const char* p : pre)
            if (k.rfind(p, 0) == 0) { keep = true; break; }
        if (keep) out.push_back(s);
    }
#endif
    std::sort(out.begin(), out.end());
    return out;
}
bool wrEnd(FILE* f, const YarnSim::End& e) {
    return wr(f, e.particle) && wr(f, e.target) && wr(f, e.hand) && wr(f, e.tension);
}
bool rdEnd(FILE* f, YarnSim::End& e) {
    return rd(f, e.particle) && rd(f, e.target) && rd(f, e.hand) && rd(f, e.tension);
}
bool wrGrip(FILE* f, const YarnSim::Grip& g) { return wr(f, g.at) && wrVec(f, g.particles) && wrVec(f, g.offset); }
bool rdGrip(FILE* f, YarnSim::Grip& g) { return rd(f, g.at) && rdVec(f, g.particles) && rdVec(f, g.offset); }
bool wrBody(FILE* f, const SdfBody& b) {
    return wr(f, b.q) && wr(f, b.qPrev) && wr(f, b.t) && wr(f, b.tPrev) && wr(f, b.squash) && wr(f, b.friction) && wr(f, b.reach) &&
           wr(f, b.started) && wr(f, b.impulse);
}
bool rdBody(FILE* f, SdfBody& b) {
    return rd(f, b.q) && rd(f, b.qPrev) && rd(f, b.t) && rd(f, b.tPrev) && rd(f, b.squash) && rd(f, b.friction) && rd(f, b.reach) &&
           rd(f, b.started) && rd(f, b.impulse);
}

template <class T>
bool sameVec(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}

}  // namespace

bool YarnSim::saveState(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::printf("save: cannot write %s\n", path.c_str());
        return false;
    }
    bool ok = wrBytes(f, kStateMagic, 8) && wr(f, kStateVersion) && wrStr(f, FL_GIT_HASH);
    ok = ok && wrHook(f, shape) && wrYsp(f, params) && wrRod(f, rs.params) && wr(f, assist);
    ok = ok && wr(f, assistGrip) && wr(f, assistRing) && wr(f, assistCatch) && wr(f, assistPins) && wr(f, assistReel);
    const std::vector<std::string> env = envKnobs();
    uint64_t ne = env.size();
    ok = ok && wr(f, ne);
    for (const std::string& e : env) ok = ok && wrStr(f, e);
    // YarnSim
    ok = ok && wr(f, frame) && wr(f, rig) && wr(f, hook) && wr(f, homeSet) && wr(f, chains) && wr(f, lastLinkHi) && wr(f, lastLinkLo) &&
         wr(f, lastLinkChain) && wr(f, home) && wrEnd(f, tail) && wrEnd(f, ball) && wr(f, catchK) && wr(f, catchReach) && wr(f, catchFrom) &&
         wr(f, worstGap) && wr(f, worstPen) && wr(f, impulseGrip) && wr(f, impulseRing) && wr(f, impulseCatch) && wr(f, impulsePins) &&
         wr(f, impulseReel) && wr(f, impulseHook) && wr(f, fingerOn) && wr(f, hand) && wr(f, fingerAxis) && wr(f, fingerR) &&
         wr(f, fingerLen) && wr(f, spoolOff) && wr(f, spoolRadial) && wr(f, fingerEyelet) && wr(f, throatX) && wr(f, throatZ) &&
         wr(f, throatStiffness) && wr(f, reelSlack) && wr(f, reelForce);
    uint64_t ng = grips.size();
    ok = ok && wr(f, ng);
    for (const Grip& g : grips) ok = ok && wrGrip(f, g);
    ok = ok && wrVec(f, fixtures);
    ok = ok && wr(f, pulled_) && wrVec(f, caught_) && wrVec(f, reeled_) && wr(f, throatRing_) && wr(f, fingerRing_) && wr(f, catchPin_) &&
         wr(f, catchSpring_) && wr(f, catchOffset_);
    // RodSystem
    ok = ok && wrVec(f, rs.x) && wrVec(f, rs.xPrev) && wrVec(f, rs.v) && wrVec(f, rs.w) && wrVec(f, rs.wFree) && wrVec(f, rs.wBase) &&
         wrVec(f, rs.extForce) && wrVec(f, rs.pinTarget) && wrVec(f, rs.particleChain) && wrVec(f, rs.anchor) && wrVec(f, rs.segA) &&
         wrVec(f, rs.segB) && wrVec(f, rs.segChain) && wrVec(f, rs.segIndex) && wrVec(f, rs.q) && wrVec(f, rs.qPrev) && wrVec(f, rs.omega) &&
         wrVec(f, rs.wq) && wrVec(f, rs.restLen) && wrVec(f, rs.slack) && wrVec(f, rs.radius) && wrVec(f, rs.mass) && wrVec(f, rs.jointA) &&
         wrVec(f, rs.jointB) && wrVec(f, rs.restDarboux) && wrVec(f, rs.chains) && wrVec(f, rs.statics) && wrVec(f, rs.rings);
    uint64_t nb = rs.bodies.size();
    ok = ok && wr(f, nb);
    for (const SdfBody& b : rs.bodies) ok = ok && wrBody(f, b);
    ok = ok && wrVec(f, rs.guideSprings) && wr(f, rs.lastStaticsImpulse) && wr(f, rs.lastContacts) && wr(f, rs.restBendMemory) &&
         wr(f, rs.rotInertiaScale_) && wr(f, rs.massScale_) && wrVec(f, rs.savedW_) && wrVec(f, rs.savedWq_) && wrVec(f, rs.segActive_) &&
         wrVec(f, rs.staticsPrev_);
    ok = ok && wrBytes(f, kStateMagic, 8);
    if (!ok) std::printf("save: write failed for %s\n", path.c_str());
    std::fclose(f);
    return ok;
}

bool YarnSim::loadState(const std::string& path, bool checkHeader) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (std::getenv("SIM_LOADDBG")) std::fprintf(stderr, "load: open\n");
    if (!f) {
        std::printf("load: cannot read %s\n", path.c_str());
        return false;
    }
    char magic[8]{};
    uint32_t version = 0;
    std::string hash;
    HookShape hs;
    YarnSimParams yp;
    RodParams rp;
    Real as = 1;
    bool ok = rdBytes(f, magic, 8) && std::memcmp(magic, kStateMagic, 8) == 0 && rd(f, version) && version == kStateVersion && rdStr(f, hash);
    if (!ok) {
        std::printf("load: %s is not a checkpoint of this version\n", path.c_str());
        std::fclose(f);
        return false;
    }
    ok = rdHook(f, hs) && rdYsp(f, yp) && rdRod(f, rp) && rd(f, as);
    Real ag = 1, ar = 1, ac = 1, ap = 1, areel = 1;
    ok = ok && rd(f, ag) && rd(f, ar) && rd(f, ac) && rd(f, ap) && rd(f, areel);
    std::vector<std::string> env;
    uint64_t ne = 0;
    ok = ok && rd(f, ne) && ne <= 10000;
    for (uint64_t i = 0; ok && i < ne; ++i) {
        std::string e;
        ok = rdStr(f, e);
        env.push_back(e);
    }
    if (!ok) {
        std::printf("load: %s ends in its header\n", path.c_str());
        std::fclose(f);
        return false;
    }
    if (checkHeader && hash != FL_GIT_HASH) {
        std::printf("load: %s was saved by git %s; this build is %s\n", path.c_str(), hash.c_str(), FL_GIT_HASH);
        std::fclose(f);
        return false;
    }
    if (checkHeader && (!sameHook(hs, shape) || !sameYsp(yp, params) || as != assist)) {
        std::printf("load: %s was saved with different physics (hook %d, params %d, assist %.2f vs %.2f)\n", path.c_str(),
                    sameHook(hs, shape) ? 1 : 0, sameYsp(yp, params) ? 1 : 0, as, assist);
        std::fclose(f);
        return false;
    }
    if (checkHeader && !sameRod(rp, rs.params))
        std::printf("load: rod parameters differ from this run's defaults; the saved solver parameters are used\n");
    const std::vector<std::string> now = envKnobs();
    if (checkHeader && now != env) {
        std::printf("load: environment knobs differ (informational):");
        for (const std::string& s : env)
            if (std::find(now.begin(), now.end(), s) == now.end()) std::printf(" -%s", s.substr(0, s.find('=')).c_str());
        for (const std::string& s : now)
            if (std::find(env.begin(), env.end(), s) == env.end()) std::printf(" +%s", s.substr(0, s.find('=')).c_str());
        std::printf("\n");
    }
    shape = hs;
    params = yp;
    rs.params = rp;
    assist = as;
    assistGrip = ag;
    assistRing = ar;
    assistCatch = ac;
    assistPins = ap;
    assistReel = areel;
    ok = ok && rd(f, frame) && rd(f, rig) && rd(f, hook) && rd(f, homeSet) && rd(f, chains) && rd(f, lastLinkHi) && rd(f, lastLinkLo) &&
         rd(f, lastLinkChain) && rd(f, home) && rdEnd(f, tail) && rdEnd(f, ball) && rd(f, catchK) && rd(f, catchReach) && rd(f, catchFrom) &&
         rd(f, worstGap) && rd(f, worstPen) && rd(f, impulseGrip) && rd(f, impulseRing) && rd(f, impulseCatch) && rd(f, impulsePins) &&
         rd(f, impulseReel) && rd(f, impulseHook) && rd(f, fingerOn) && rd(f, hand) && rd(f, fingerAxis) && rd(f, fingerR) &&
         rd(f, fingerLen) && rd(f, spoolOff) && rd(f, spoolRadial) && rd(f, fingerEyelet) && rd(f, throatX) && rd(f, throatZ) &&
         rd(f, throatStiffness) && rd(f, reelSlack) && rd(f, reelForce);
    uint64_t ng = 0;
    ok = ok && rd(f, ng) && ng <= 10000;
    grips.clear();
    for (uint64_t i = 0; ok && i < ng; ++i) {
        Grip g;
        ok = rdGrip(f, g);
        grips.push_back(g);
    }
    ok = ok && rdVec(f, fixtures);
    ok = ok && rd(f, pulled_) && rdVec(f, caught_) && rdVec(f, reeled_) && rd(f, throatRing_) && rd(f, fingerRing_) && rd(f, catchPin_) &&
         rd(f, catchSpring_) && rd(f, catchOffset_);
    ok = ok && rdVec(f, rs.x) && rdVec(f, rs.xPrev) && rdVec(f, rs.v) && rdVec(f, rs.w) && rdVec(f, rs.wFree) && rdVec(f, rs.wBase) &&
         rdVec(f, rs.extForce) && rdVec(f, rs.pinTarget) && rdVec(f, rs.particleChain) && rdVec(f, rs.anchor) && rdVec(f, rs.segA) &&
         rdVec(f, rs.segB) && rdVec(f, rs.segChain) && rdVec(f, rs.segIndex) && rdVec(f, rs.q) && rdVec(f, rs.qPrev) && rdVec(f, rs.omega) &&
         rdVec(f, rs.wq) && rdVec(f, rs.restLen) && rdVec(f, rs.slack) && rdVec(f, rs.radius) && rdVec(f, rs.mass) && rdVec(f, rs.jointA) &&
         rdVec(f, rs.jointB) && rdVec(f, rs.restDarboux) && rdVec(f, rs.chains) && rdVec(f, rs.statics) && rdVec(f, rs.rings);
    uint64_t nb = 0;
    ok = ok && rd(f, nb) && nb <= 1000;
    rs.bodies.clear();
    for (uint64_t i = 0; ok && i < nb; ++i) {
        SdfBody b;
        ok = rdBody(f, b);
        rs.bodies.push_back(b);
    }
    ok = ok && rdVec(f, rs.guideSprings) && rd(f, rs.lastStaticsImpulse) && rd(f, rs.lastContacts) && rd(f, rs.restBendMemory) &&
         rd(f, rs.rotInertiaScale_) && rd(f, rs.massScale_) && rdVec(f, rs.savedW_) && rdVec(f, rs.savedWq_) && rdVec(f, rs.segActive_) &&
         rdVec(f, rs.staticsPrev_);
    if (std::getenv("SIM_LOADDBG")) std::fprintf(stderr, "load: arrays read ok=%d\n", ok ? 1 : 0);
    char trailer[8]{};
    ok = ok && rdBytes(f, trailer, 8) && std::memcmp(trailer, kStateMagic, 8) == 0;
    std::fclose(f);
    if (!ok) {
        std::printf("load: %s is truncated or corrupt\n", path.c_str());
        return false;
    }
    if (std::getenv("SIM_LOADDBG"))
        std::fprintf(stderr, "load: trailer ok=%d bodies=%zu rings=%zu throat=%d finger=%d catch=%d springs=%zu grips=%zu chains=%zu parts=%d\n",
                     ok ? 1 : 0, rs.bodies.size(), rs.rings.size(), throatRing_, fingerRing_, catchSpring_, rs.guideSprings.size(), grips.size(),
                     rs.chains.size(), rs.particleCount());
    const HookShape sh = shape;
    for (SdfBody& b : rs.bodies) b.f = [sh](const Vec3& p, Vec3* g) { return hookSdf(p, sh, g); };
    rs.pairs_.clear();
    rs.lambdaS_.assign((size_t)rs.segmentCount() * 3, 0.0);
    rs.lambdaB_.assign((size_t)rs.jointA.size() * 3, 0.0);
    rs.lambdaA_.assign((size_t)rs.particleCount() * 3, 0.0);
    return true;
}

std::string YarnSim::stateDiff(const YarnSim& o) const {
    auto where = [&](const char* what, int i) {
        return std::string(what) + " " + std::to_string(i) + " at frame " + std::to_string(frame);
    };
    if (frame != o.frame) return "frame " + std::to_string(frame) + " vs " + std::to_string(o.frame);
    if (rs.particleCount() != o.rs.particleCount()) return "particle count " + std::to_string(rs.particleCount()) + " vs " + std::to_string(o.rs.particleCount());
    for (int i = 0; i < rs.particleCount(); ++i)
        if (std::memcmp(&rs.x[i], &o.rs.x[i], sizeof(Vec3)) != 0) return where("particle", i);
    if (!sameVec(rs.xPrev, o.rs.xPrev)) return "xPrev";
    if (!sameVec(rs.v, o.rs.v)) return "v";
    if (!sameVec(rs.w, o.rs.w)) return "w";
    if (!sameVec(rs.wFree, o.rs.wFree)) return "wFree";
    if (!sameVec(rs.wBase, o.rs.wBase)) return "wBase";
    if (!sameVec(rs.q, o.rs.q)) return "q";
    if (!sameVec(rs.qPrev, o.rs.qPrev)) return "qPrev";
    if (!sameVec(rs.omega, o.rs.omega)) return "omega";
    if (!sameVec(rs.restLen, o.rs.restLen)) return "restLen";
    if (!sameVec(rs.slack, o.rs.slack)) return "slack";
    if (!sameVec(rs.extForce, o.rs.extForce)) return "extForce";
    if (!sameVec(rs.pinTarget, o.rs.pinTarget)) return "pinTarget";
    if (!sameVec(rs.rings, o.rs.rings)) return "rings";
    if (!sameVec(rs.guideSprings, o.rs.guideSprings)) return "guideSprings";
    if (!sameVec(rs.staticsPrev_, o.rs.staticsPrev_)) return "staticsPrev";
    if (!sameVec(rs.savedW_, o.rs.savedW_)) return "savedW";
    if (!sameVec(rs.savedWq_, o.rs.savedWq_)) return "savedWq";
    if (rs.bodies.size() != o.rs.bodies.size()) return "body count";
    for (size_t i = 0; i < rs.bodies.size(); ++i)
        if (std::memcmp(&rs.bodies[i].q, &o.rs.bodies[i].q, sizeof(Quat)) != 0 ||
            std::memcmp(&rs.bodies[i].t, &o.rs.bodies[i].t, sizeof(Vec3)) != 0)
            return "body pose " + std::to_string(i);
    return "identical";
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

// The hook's largest distance from its axis at height z (hook frame, m): the outermost point of its body in any direction. The ring driver
// keeps its yarn this far from the axis plus the yarn's clearance.
static Real hookSectionRadius(const HookShape& s, Real z) {
    Real best = 0;
    for (int k = 0; k < 64; ++k) {
        const Real a = 2 * kPi * k / 64;
        for (Real r = 0; r <= 4 * s.R; r += 0.02 * s.R)
            if (hookSdf(Vec3{r * std::cos(a), r * std::sin(a), z}, s, nullptr) < 0) best = std::max(best, r);
    }
    return best;
}

// The particles beyond each end that the ring driver holds with the stretch (0: the legs are free).
static const int kDriveLegHold = 0;

void YarnSim::driveRing(int lo, int hi, Real zFrom, Real zTo, int frames) {
    releaseRing();   // a new window starts from the pins where they are now
    if (lo < 6 || hi < lo || hi + 6 >= rs.particleCount()) return;
    driveOn_ = true;
    driveRingLo_ = lo;
    driveRingHi_ = hi;
    driveFrame_ = 0;
    driveFrames_ = std::max(1, frames);
    driveZ0_ = zFrom;
    driveZ1_ = zTo;
    // the stretch and three particles of each leg (plan 3.1: the loop involved plus its legs up to 3 mm) are held
    driveLo_ = lo - kDriveLegHold;
    driveHi_ = hi + kDriveLegHold;
    driveStart_.clear();
    driveStartLocal_.clear();
    driveTargets_.assign(driveHi_ - driveLo_ + 1, Vec3{});
    for (int i = driveLo_; i <= driveHi_; ++i) {
        driveStart_.push_back(rs.x[i]);
        driveStartLocal_.push_back(rotate(conj(hook.q), rs.x[i] - hook.tip));
        rs.pin(i);
        rs.movePin(i, rs.x[i]);
    }
    // the legs leave the ring toward the midpoint of the particles just beyond each end; the gap goes on that side
    // (5 particles beyond each end, not 1: the first particle beyond the stretch is at the stretch's own end, which says nothing about where the legs go)
    const Vec3 mid = (rs.x[lo - 5] + rs.x[hi + 5]) * 0.5;
    const Vec3 lm = rotate(conj(hook.q), mid - hook.tip);
    driveGapLeg_ = std::hypot(lm.x, lm.y) > 1e-9 ? std::atan2(lm.y, lm.x) : kPi / 2;
}

void YarnSim::releaseRing() {
    if (!driveOn_) return;
    for (int i = driveLo_; i <= driveHi_; ++i) rs.unpin(i);
    driveOn_ = false;
}

Vec3 YarnSim::driveTarget(int i) const {
    const int k = i - driveLo_;
    return k >= 0 && k < (int)driveTargets_.size() ? driveTargets_[k] : Vec3{};
}

void YarnSim::applyRing() {
    const int n = driveRingHi_ - driveRingLo_ + 1;   // the particles that form the ring
    const int off = driveRingLo_ - driveLo_;         // where the ring's first particle is in the held arrays
    const Real seg = params.segment, r = params.radius;
    const Real t = std::min(1.0, (Real)driveFrame_ / driveFrames_);
    const Real z = driveZ0_ + (driveZ1_ - driveZ0_) * t;   // the ring's plane, in the hook's frame
    // the ring's radius clears the hook here, and its open gap is at least twice the squashed width of the yarn
    const Real arcLen = (n - 1) * seg, gapMin = 2 * (2 * 0.6 * r);
    const Real rho = std::max(hookSectionRadius(shape, z) + 0.6 * r + 0.2 * kMm, (arcLen + gapMin) / (2 * kPi));
    const Real gap = 2 * kPi * rho - arcLen;
    // The yarn comes to the ring in two stages over 8 frames, so no jump and no crossing of the hook. First every particle turns round the
    // axis to its place on the ring, at a radius clear of the ring; then they move in to the ring's radius. The turn is one rotation for the
    // whole stretch: the ring is walked the way that turns the yarn least, and its angles are shifted by whole turns to lie nearest the
    // start, so neighbours turn the same way by nearly the same amount and the chords between them stay outside the hook.
    std::vector<Real> start(n);
    for (int k = 0; k < n; ++k) {
        const Vec3& s0 = driveStartLocal_[off + k];
        const Real a0 = std::atan2(s0.y, s0.x);
        start[k] = k == 0 ? a0 : start[k - 1] + std::remainder(a0 - std::atan2(driveStartLocal_[off + k - 1].y, driveStartLocal_[off + k - 1].x), 2 * kPi);
    }
    std::vector<Real> target(n);
    Real bestCost = 1e18;
    for (const Real dir : {1.0, -1.0}) {
        std::vector<Real> tt(n);
        for (int k = 0; k < n; ++k) tt[k] = driveGapLeg_ + dir * (gap / (2 * rho) + k * seg / rho);
        const Real shift = -2 * kPi * std::round((tt[n / 2] - start[n / 2]) / (2 * kPi));
        Real cost = 0;
        for (int k = 0; k < n; ++k) cost += std::fabs(tt[k] + shift - start[k]);
        if (cost < bestCost) {
            bestCost = cost;
            for (int k = 0; k < n; ++k) target[k] = tt[k] + shift;
        }
    }
    const Real ta = std::min(1.0, (Real)driveFrame_ / 4.0), tb = std::clamp((Real)(driveFrame_ - 4) / 4.0, 0.0, 1.0);
    std::vector<Vec3> ringNow(n);
    for (int k = 0; k < n; ++k) {
        const Vec3& s0 = driveStartLocal_[off + k];
        const Real r0 = std::hypot(s0.x, s0.y);
        const Real rClear = std::max(r0, rho + 2.5 * kMm);
        const Real a = start[k] + (target[k] - start[k]) * ta;
        const Real rad = rClear + (rho - rClear) * tb;
        const Real zk = s0.z + (z - s0.z) * ta;
        ringNow[k] = hook.tip + rotate(hook.q, Vec3{rad * std::cos(a), rad * std::sin(a), zk});
    }
    // every held particle: the ring's particles at their places, and the legs follow the nearest ring end by a share that falls off
    // (0.75, 0.5, 0.25 of its move for the first, second and third particle beyond the end)
    const Vec3 moveLo = ringNow[0] - driveStart_[off], moveHi = ringNow[n - 1] - driveStart_[off + n - 1];
    if (std::getenv("RING_DBG") && driveFrame_ == 2) std::fprintf(stderr, "ring k0 start (%.2f %.2f) start-angle %.1f target %.1f now (%.2f %.2f) gapLeg %.1f rho %.2f dir-ok\n", driveStart_[off].x*1e3, driveStart_[off].y*1e3, start[0]*180/kPi, target[0]*180/kPi, ringNow[0].x*1e3, ringNow[0].y*1e3, driveGapLeg_*180/kPi, rho*1e3);
    for (int p = 0; p <= driveHi_ - driveLo_; ++p) {
        const int idx = driveLo_ + p;
        Vec3 world;
        if (idx >= driveRingLo_ && idx <= driveRingHi_) {
            world = ringNow[idx - driveRingLo_];
        } else if (idx < driveRingLo_) {
            const int j = driveRingLo_ - idx;   // 1..3 beyond the low end
            world = driveStart_[p] + moveLo * (1.0 - j / 4.0);
        } else {
            const int j = idx - driveRingHi_;   // 1..3 beyond the high end
            world = driveStart_[p] + moveHi * (1.0 - j / 4.0);
        }
        driveTargets_[p] = world;
        rs.movePin(idx, world);
    }
    ++driveFrame_;
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

Real YarnSim::windingNear(int i0, int i1, Real reach, int* runLo, int* runHi) const {
    Real best = 0, run = 0, prev = 0;
    int start = -1;
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
            if (std::fabs(run) > std::fabs(best)) {
                best = run;
                if (runLo) *runLo = start;
                if (runHi) *runHi = i;
            }
        } else {
            start = i;
        }
        prev = a;
        have = true;
    }
    return best / (2 * kPi);
}

std::vector<YarnSim::HookRun> YarnSim::hookRuns(Real reach, Real minTurns) const {
    std::vector<HookRun> out;
    const int n = rs.particleCount();
    Real run = 0, prev = 0;
    int start = -1;
    bool have = false;
    auto close = [&](int end) {
        if (have && std::fabs(run) >= minTurns * 2 * kPi) out.push_back({start, end, run / (2 * kPi)});
        have = false;
        run = 0;
    };
    for (int i = 0; i < n; ++i) {
        const Vec3 l = rotate(conj(hook.q), rs.x[i] - hook.tip);
        if (l.z < -2.0 * shape.R || l.z > shape.length || std::hypot(l.x, l.y) > reach) {
            close(i - 1);
            continue;
        }
        const Real a = std::atan2(l.y, l.x);
        if (have) {
            Real d = a - prev;
            while (d > kPi) d -= 2 * kPi;
            while (d < -kPi) d += 2 * kPi;
            run += d;
        } else {
            start = i;
            have = true;
        }
        prev = a;
    }
    close(n - 1);
    return out;
}

Real YarnSim::hookPenetration() const {
    Real worst = 0;
    if (shape.sdf) {
        for (int s2 = 0; s2 < rs.segmentCount(); ++s2)
            for (int k = 0; k <= 2; ++k) {
                const Vec3 P = rs.x[rs.segA[s2]] + (rs.x[rs.segB[s2]] - rs.x[rs.segA[s2]]) * (0.5 * k);
                worst = std::max(worst, params.radius * shape.squash - hookSdf(rotate(conj(hook.q), P - hook.tip), shape, nullptr));
            }
        return worst;
    }
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

static bool originInsideLoop(const YarnSim& sim, int lo, int hi);

void makeSlipKnot(YarnSim& sim) {
    const Real m = kMm, R = sim.shape.R;
    const Real zb = std::max(3.3 * sim.params.radius, 3.6 * m);
    // the hook comes from the front, the tip pointing into the ring, the throat toward the yarn behind the ring
    // the tip this far back puts the yarn behind the ring in the mouth of the throat: 3.4R on the legacy hook, the groove's centre on the inline hook
    const Real deep = -zb - (sim.shape.form == HookShape::Form::Inline ? sim.shape.grooveZ : 3.4) * R;
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
    // A slip knot's noose closes down onto the thin neck until its hole is narrower than the yarn. A crocheter holds the loop at the size it
    // should be while the knot comes snug; a gauge round the shaft does that here, so the starting loop stays a loop the chain can use.
    // The gauge is a guide (plan 3.1): it joins the pins' scale, and at SIM_ASSIST_PINS 0 it is not made.
    const Real gauge = std::getenv("SLIP_GAUGE") ? std::atof(std::getenv("SLIP_GAUGE")) : 0;
    const bool gaugeOn = gauge > 0 && sim.assistPins > 0;
    if (gaugeOn) {
        const Vec3 ax = sim.hook.axis();
        StaticCapsule g;
        g.a = sim.hook.tip + ax * (3.0 * sim.shape.R);
        g.b = sim.hook.tip + ax * (12.0 * sim.shape.R);
        g.r = gauge * m;
        sim.fixtures.push_back(g);
        sim.phase = "The loop held at its size while the knot comes snug";
        sim.settle(30);
    }
    // snugged: the working yarn pulled firmly closes the loop on the shaft
    sim.phase = "The working yarn pulled firmly snugs the loop on the shaft: the slip knot";
    const Real snug = std::getenv("SLIP_SNUG") ? std::atof(std::getenv("SLIP_SNUG")) : 4.0;
    if (snug > 0) {
        sim.ball.tension = snug * sim.params.tension;
        const Real frames = std::getenv("SLIP_SNUGT") ? std::atof(std::getenv("SLIP_SNUGT")) : 150;
        sim.settle((int)frames);
    }
    sim.ball.tension = tension0;
    sim.settle(20);
    if (gaugeOn) {
        sim.fixtures.pop_back();   // the gauge out: the loop stays at the size it was held
        sim.ball.tension = 0.2 * tension0;
        sim.settle(10);
        sim.ball.tension = tension0;
    }
    // The slip knot's loop is a noose: closed on the hook it is short and tight, too short to ride over the head. A crocheter opens it by
    // pulling the loop (the hook) away from the knot while the working yarn feeds; the loop gets longer and looser on the shaft.
    const Real open = std::getenv("SLIP_OPEN") ? std::atof(std::getenv("SLIP_OPEN")) : 0;
    if (open > 0) {
        sim.phase = "The slip knot opened: the hook drawn away from the knot, the loop longer on the shaft";
        sim.ball.tension = 0.3 * tension0;
        sim.moveHook(hookPose(sim.hook.tip + Vec3{0, open * m, 0}, -sim.hook.axis(), sim.hook.throat()), 0.7);
        sim.ball.tension = tension0;
        sim.settle(25);
    }
    if (std::getenv("SIM_CHAINDBG")) {
        int l = -1, h = -1;
        if (hookLoop(sim, &l, &h)) {
            // The same measure as judgeStitch's onShaft: the loop's mean distance from the axis and its centreline length.
            Real sum = 0, len = 0;
            for (int i = l; i <= h; ++i) {
                const Vec3 q = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip);
                sum += std::hypot(q.x, q.y);
                if (i < h) len += sim.rs.restLen[i];
            }
            std::printf("slip knot: the loop on the shaft is yarn %d..%d around the shaft %d, onShaft=%.1f/%.1f\n", l, h,
                        originInsideLoop(sim, l, h) ? 1 : 0, sum / (h - l + 1) * 1e3, len * 1e3);
        } else {
            std::printf("slip knot: no loop on the shaft\n");
        }
    }
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
void seatLoop(YarnSim& sim, Real clear, const Vec3& lift = Vec3{}, int lo = 0, int hi = 1 << 30) {
    const Vec3 ax = sim.hook.axis();
    const Real step = 0.25e-3, most = 0.02;
    const Vec3 dl = lift * (step / most);
    Real moved = 0;
    for (; moved < most; moved += step) {
        bool inThroat = false;
        for (int i : roundHook(sim, -sim.shape.R, clear))
            if (i >= lo && i <= hi) { inThroat = true; break; }   // only the loop's own yarn (the stitch below it hangs near the tip too)
        if (!inThroat) break;
        sim.step(hookPose(sim.hook.tip - ax * step + dl, ax * -1.0, sim.hook.throat()));
        if (sim.onFrame) sim.onFrame();
    }
    if (envOr("SIM_SEATDBG", 0) > 0) {
        Real mn = 1e9;
        for (int i = lo; i <= hi && i < sim.rs.particleCount(); ++i)
            mn = std::min(mn, dot(sim.rs.x[i] - sim.hook.tip, sim.hook.axis()));
        std::printf("  seat: clear %.1f mm, moved %.1f mm, loop front now %.1f mm (%d steps)\n", clear * 1e3, moved * 1e3, mn * 1e3,
                    (int)std::lround(moved / step));
    }
}

// The loop on the hook: the shortest stretch of yarn near the hook that winds at least `turns` round it. False if there is none.
bool loopRange(const YarnSim& sim, Real reach, Real turns, int* lo, int* hi) {
    int a = -1, b = -1;
    sim.windingNear(0, sim.rs.particleCount() - 1, reach, &a, &b);
    if (a < 0) return false;
    std::vector<Real> cum{0};
    Real prev = 0;
    for (int i = a; i <= b; ++i) {
        const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip);
        const Real ang = std::atan2(l.y, l.x);
        if (i > a) {
            Real d = ang - prev;
            while (d > kPi) d -= 2 * kPi;
            while (d < -kPi) d += 2 * kPi;
            cum.push_back(cum.back() + d);
        }
        prev = ang;
    }
    int best = 1 << 30;
    bool found = false;
    for (int i = 0; i < (int)cum.size(); ++i)
        for (int j = i + 1; j < (int)cum.size() && j - i < best; ++j)
            if (std::fabs(cum[j] - cum[i]) >= turns * 2 * kPi) {
                best = j - i;
                *lo = a + i;
                *hi = a + j;
                found = true;
                break;
            }
    if (found) {
        // the loop goes on as long as the yarn stays close round the hook
        const Real near = sim.shape.R + 2.5 * sim.params.radius;
        while (*hi + 1 < sim.rs.particleCount()) {
            const Vec3 d = sim.rs.x[*hi + 1] - sim.hook.tip;
            const Vec3 ax = sim.hook.axis();
            const Real a = dot(d, ax);
            if (a < -sim.shape.R || length(d - ax * a) > near) break;
            ++*hi;
        }
    }
    return found;
}

// Every move of the chain runs until the yarn is in the state the move is for (closed loop), not for a set time: the loop on the hook is found by how
// far the yarn winds round the hook, the yarn over goes on until the working yarn winds a full turn, the pull until the old loop no longer winds.
ChainStart prepareChain(YarnSim& sim) {
    ChainStart c;
    sim.readAssist();
    sim.worstGap = 1e9;
    sim.worstPen = 0;
    sim.impulseGrip = sim.impulseRing = sim.impulseCatch = sim.impulsePins = sim.impulseReel = sim.impulseHook = 0;
    sim.releaseGuideSprings();   // a previous stitch's holds never carry into this one
    c.prevHi = sim.lastLinkHi;
    const int prevLo = sim.lastLinkLo, prevHi = sim.lastLinkHi;
    const bool usePrev = envOr("CH_PREV", 0) > 0 && prevLo >= 0 && prevHi > prevLo && sim.lastLinkChain == sim.chains;
    const Real m = kMm, R = sim.shape.R;
    const Vec3 ax = sim.hook.axis();            // tip -> handle (the hook lies tip to the left)
    const Vec3 up{0, 1, 0}, front{0, 0, 1};
    // A stitch carried from an earlier chain, not the clean loop the rig lays. The slip knot's loop is already a stitch loop, so even
    // its first chain uses the carried placement (the crude first-loop placement makes a 15-20 mm bight).
    const bool carried = !sim.rig || sim.homeSet;
    const bool late = sim.chains >= 4;           // the work has grown: hold it a little farther below the hook for the fifth chain
    const Real holdBelow = envOr("HOLD_BELOW", 10) + (late ? envOr("HOLD_LATE", 2) : 0);
    const Real holdT = envOr("HOLD_T", 0.5) + (late ? envOr("HOLD_T_LATE", 0.05) : 0);
    // The first loop comes from the slip knot and is the whole starting ring (CH_TURNS1/CH_REACH1); after that each loop is a stitch loop,
    // and the usual compact winding test is right.
    const bool firstLoop = sim.chains == 0;
    const Real reach = (firstLoop ? envOr("CH_REACH1", envOr("CH_REACH", 9)) : 9.0) * m;
    const Real turns = firstLoop ? envOr("CH_TURNS1", envOr("CH_TURNS", 0.6)) : 0.6;
    // the loop on the hook
    int lo = -1, hi = -1;
    if (!loopRange(sim, reach, turns, &lo, &hi) &&
        !(sim.loopsOnHook(front) != 0 && loopRange(sim, 1.6 * reach, 0.4, &lo, &hi))) {
        // A loose stitch from the last chain may not close on itself tightly enough for loopRange, but the linking stretch still
        // identifies it; use that rather than treating the hook as empty (the half-plane count has already seen a loop).
        if (!hookLoop(sim, &lo, &hi)) {
            std::printf("chain: no loop on the hook (%d by the half plane)\n", sim.loopsOnHook(front));
            return c;
        }
    }
    // The winding test finds the shortest stretch that winds 0.6 of a turn, which can be one side of a closed loop. If the whole
    // closed loop encloses the shaft, that is the stitch loop the new yarn has to pass through; use it instead.
    int prefLo = -1, prefHi = -1;
    if (envOr("CH_PREFER", 0) > 0 || envOr("CH_FULL", 0) > 0) {
        int hl = -1, hh = -1;
        if (hookLoop(sim, &hl, &hh) && hh > hl && originInsideLoop(sim, hl, hh) && (hh - hl) > (hi - lo)) {
            if (envOr("SIM_CHAINDBG", 0) > 0)
                std::printf("chain: using the closed loop %d..%d that encloses the shaft (not %d..%d)\n", hl, hh, lo, hi);
            lo = hl;
            hi = hh;
            prefLo = hl;
            prefHi = hh;
        }
    }
    c.oldLo = lo;
    c.oldHi = hi;
    for (int i = lo; i <= hi; ++i) c.oldLoop.push_back(i);
    for (int s = 0; s + 1 < sim.rs.segmentCount(); ++s) c.length += sim.rs.restLen[s];
    // the left hand: thumb and middle finger pinch the yarn of the work just before the loop
    sim.phase = "The left hand holds the work below the hook; the yarn runs over its index finger";
    {
        YarnSim::Grip g;
        const int from = std::max(1, lo - (int)envOr("HOLD_N", 22)), to = lo - (int)envOr("HOLD_GAP", 4);
        Vec3 at{};
        int n = 0;
        for (int i = from; i <= to; ++i) { at += sim.rs.x[i]; ++n; }
        g.at = n > 0 ? at / (Real)n : sim.rs.x[std::max(0, lo - 1)];
        for (int i = from; i <= to; ++i) {
            const Vec3 d = sim.rs.x[i] - sim.hook.tip;
            const Real a = dot(d, ax);
            if (a > -2 * R && a < 14 * R && length(d - ax * a) < R + 2.0 * sim.params.radius) continue;
            g.particles.push_back(i);
            g.offset.push_back(sim.rs.x[i] - g.at);
            sim.rs.pin(i);
        }
        sim.grips.push_back(g);
        c.hold = (int)sim.grips.size() - 1;
        // the work brought to hang just below the loop on the hook
        Vec3 lc{};
        for (int i = lo; i <= hi; ++i) lc += sim.rs.x[i];
        lc = lc / (Real)(hi - lo + 1);
        const Vec3 hTo = lc - up * (R + holdBelow * m);
        const Vec3 hFrom = sim.grips[c.hold].at;
        const int hn = (int)(holdT / sim.params.dt);
        for (int k = 1; k <= hn; ++k) {
            const Real t = (Real)k / hn;
            sim.grips[c.hold].at = hFrom + (hTo - hFrom) * (t * t * (3 - 2 * t));
            sim.step(sim.hook);
            if (sim.onFrame) sim.onFrame();
        }
    }
    sim.stageLog += std::string("p") + std::to_string(sim.loopsOnHook(front)) + " ";
    if (envOr("SIM_STAGES", 0) > 0) std::printf("  [%d] prep %s: %d loops (front)\n", sim.frame, "found", sim.loopsOnHook(front));
    if (!sim.fingerOn) {
        sim.putFinger(sim.rs.particleCount() - 6, up * -1.0);
        moveHandTo(sim, sim.hand, 0.1);
    }
    sim.stageLog += std::string("p") + std::to_string(sim.loopsOnHook(front)) + " ";
    if (envOr("SIM_STAGES", 0) > 0) std::printf("  [%d] prep %s: %d loops (front)\n", sim.frame, "held", sim.loopsOnHook(front));
    // the loop slid back onto the shaft, clear of the throat
    if (envOr("SEAT_HAND", 1) > 0) {
        // the left hand slides the work (and the loop with it) back along the shaft until the loop is clear of the throat; the finger goes along
        sim.phase = "The left hand slides the work back along the hook";
        const Real step = 0.25e-3, clear = envOr("SIM_SEAT", 5.0) * R;
        for (Real moved = 0; moved < 0.02; moved += step) {
            bool inThroat = false;
            for (int i : roundHook(sim, -sim.shape.R, clear))
                if (i >= lo && i <= hi) { inThroat = true; break; }
            if (!inThroat) break;
            sim.grips[c.hold].at += ax * step;
            sim.hand += ax * step;
            sim.step(sim.hook);
            if (sim.onFrame) sim.onFrame();
        }
    } else {
        // The seat pushes the hook forward through the loop; the work grip skips yarn near the hook, so without a thumb on it the loop
        // simply travels with the hook and nothing is seated. Hold the compact loop itself while the hook slides through it.
        std::vector<int> seatSprings;
        if (envOr("SEAT_HOLD", 0) > 0 && sim.assistPins > 0) {
            for (int i = lo; i <= hi && i < sim.rs.particleCount(); ++i) {
                const Vec3 d = sim.rs.x[i] - sim.hook.tip;
                const Real a = dot(d, ax);
                if (a > -R && a < 30 * R && length(d - ax * a) < R + 3 * sim.params.radius) {
                    const int sp = sim.addGuideSpring(i, sim.rs.x[i], 2000.0 * sim.assistPins, 0);
                    if (sp >= 0) seatSprings.push_back(sp);
                }
            }
            if (envOr("SIM_CHAINDBG", 0) > 0) std::printf("  seat: holding %d particles of the loop while the hook slides through\n", (int)seatSprings.size());
        }
        seatLoop(sim, envOr("SIM_SEAT", 5.0) * R, up * (envOr("SEAT_UP", 0.0) * m), lo, hi);
        for (int sp : seatSprings) sim.releaseGuideSpring(sp);
    }
    if (!sim.homeSet) { sim.home = sim.hook.tip; sim.homeSet = true; }
    if (envOr("HOME_RETURN", 1) > 0) {
        const Vec3 to = sim.home + ax * dot(sim.hook.tip - sim.home, ax);
        if (length(to - sim.hook.tip) > 0.3 * m) sim.moveHook(hookPose(to, -ax, sim.hook.throat()), 0.4);
    }
    sim.stageLog += std::string("p") + std::to_string(sim.loopsOnHook(front)) + " ";
    if (envOr("SIM_STAGES", 0) > 0) std::printf("  [%d] prep %s: %d loops (front)\n", sim.frame, "seated", sim.loopsOnHook(front));
    // snug on the shaft
    sim.phase = "The yarn drawn snug";
    if (envOr("SIM_CHAINDBG", 0) > 0) {
        Real z0 = 1e9, z1 = -1e9;
        for (int i = lo; i <= hi; ++i) {
            const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip);
            z0 = std::min(z0, l.z);
            z1 = std::max(z1, l.z);
        }
        std::printf("chain [%d]: before the snug the loop is yarn %d..%d at %.1f..%.1f mm behind the tip (throat %.1f)\n", sim.frame, lo, hi, z0 * 1e3, z1 * 1e3,
                    dot(sim.throatPoint() - sim.hook.tip, ax) * 1e3);
    }
    // The tension that draws the working yarn snug also drags a compact loop forward, off the shaft and onto the throat. A crocheter's
    // left hand keeps the work (and with it the old loop) back while the right hand pulls. Pin the old loop's front for the snug so it stays
    // behind the throat where the new yarn can be laid in front of it (experimental: PREP_HOLD=0 keeps the old behaviour).
    std::vector<int> heldLoop, heldSprings;
    if (envOr("PREP_HOLD", 0) > 0 && sim.assistPins > 0) {
        // The loop on the shaft is the compact stretch, not the whole carried loop the prep preferred (the slip knot's ring is much
        // bigger than the stitch loop). Find it again here, ignoring the preferred full stretch, and hold its front while the yarn comes tight.
        int plo = lo, phi = hi;
        loopRange(sim, reach, turns, &plo, &phi);
        std::vector<std::pair<Real, int>> byZ;
        for (int i = plo; i <= phi; ++i) {
            const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip);
            byZ.push_back({l.z, i});
        }
        std::sort(byZ.begin(), byZ.end());
        int n = (int)envOr("PREP_HOLDN", 3);
        if (n <= 0 || n > (int)byZ.size()) n = (int)byZ.size();
        for (int k = 0; k < n; ++k) {
            const int i = byZ[k].second;
            const int sp = sim.addGuideSpring(i, sim.rs.x[i], 2000.0 * sim.assistPins, 0);
            if (sp >= 0) {
                heldLoop.push_back(i);
                heldSprings.push_back(sp);
            }
        }
        if (envOr("SIM_CHAINDBG", 0) > 0)
            std::printf("chain: holding %d of the compact loop %d..%d through the snug (%d..%d)\n", n, plo, phi, byZ.front().second,
                        byZ.back().second);
    }
    sim.ball.tension = envOr("PREP_SNUG", 4.0) * sim.params.tension;
    sim.settle((int)(0.6 / sim.params.dt));
    sim.ball.tension = sim.params.tension;
    if (envOr("PREP_HOLD", 0) >= 2) {
        c.prepHeld = heldLoop;   // hold it through the yarn over; finishChain releases it
        c.prepHeldSprings = heldSprings;
    } else {
        for (int sp : heldSprings) sim.releaseGuideSpring(sp);
    }
    if (!loopRange(sim, reach, turns, &c.oldLo, &c.oldHi)) {
        if (sim.loopsOnHook(front) == 0) {
            std::printf("chain: the loop came off while it was snugged\n");
            for (int sp : c.prepHeldSprings) sim.releaseGuideSpring(sp);
            sim.release(c.hold);
            return c;
        }
        c.oldLo = lo;   // still on the hook (the half plane says so), only looser than the winding test wants: keep what was found
        c.oldHi = hi;
    }
    // A slip knot's loop can close down onto the thin neck, where its hole is narrower than the yarn; the whole starting ring is then the
    // only loop the new yarn can pass through. Re-find it here, after the snug (CH_FULL=1, CH_FULL_CHAINS=1 default).
    if (envOr("CH_FULL", 0) > 0 && sim.chains <= (int)envOr("CH_FULL_CHAINS", 1)) {
        int fl = -1, fh = -1;
        if (hookLoop(sim, &fl, &fh) && fh > fl && originInsideLoop(sim, fl, fh) && (fh - fl) > (c.oldHi - c.oldLo)) {
            if (envOr("SIM_CHAINDBG", 0) > 0)
                std::printf("chain: keeping the whole starting ring %d..%d as the old loop (the compact stretch %d..%d is too small)\n", fl,
                            fh, c.oldLo, c.oldHi);
            c.oldLo = fl;
            c.oldHi = fh;
            c.oldLoop.clear();
            for (int i = fl; i <= fh; ++i) c.oldLoop.push_back(i);
        }
    }
    if (usePrev) {
        const int prevMode = (int)envOr("CH_PREV", 0);
        if (prevMode >= 2 && originInsideLoop(sim, prevLo, prevHi)) {
            // The stitch just made *is* the loop the new yarn has to pass through; the stretch found on the hook before it is earlier
            // work, and unioning the two makes a long loop the new yarn is drawn through by accident (CH_PREV=2).
            if (envOr("SIM_CHAINDBG", 0) > 0)
                std::printf("chain: old loop is the last stitch %d..%d alone (the on-hook stretch %d..%d is earlier work)\n", prevLo,
                            prevHi, c.oldLo, c.oldHi);
            c.oldLo = prevLo;
            c.oldHi = prevHi;
            c.oldLoop.clear();
            for (int i = prevLo; i <= prevHi; ++i) c.oldLoop.push_back(i);
        } else {
            // The stitch just made is a better loop than a fresh sub-stretch of it. Cover both it and the loop found on the hook, so
            // the fan the new yarn is tested against starts before the whole stitch and ends past it.
            const int ulo = std::min(c.oldLo, prevLo), uhi = std::max(c.oldHi, prevHi);
            if (envOr("SIM_CHAINDBG", 0) > 0)
                std::printf("chain: old loop union with the last stitch %d..%d -> %d..%d\n", prevLo, prevHi, ulo, uhi);
            c.oldLo = ulo;
            c.oldHi = uhi;
        }
    }
    if (envOr("HOOK_SEAT_AFTER", 0) > 0) {
        // The snug pulls the loop forward onto the throat. Seat the hook forward again afterwards, with the work still held, so the
        // old loop is left behind the throat and the new yarn has somewhere to be laid in front of it.
        seatLoop(sim, envOr("SIM_SEAT", 5.0) * R, up * (envOr("SEAT_UP", 0.0) * m), c.oldLo, c.oldHi);
    }
    if (envOr("SEAT_AFTER", 0) > 0) {
        // The snug can drag a compact loop forward to the throat. Keep the work back so the loop stays on the shaft behind the
        // throat, leaving room for the new yarn to be laid in front of it.
        const Real step = 0.25e-3;
        for (Real moved = 0; moved < 0.02; moved += step) {
            Real mn = 1e9;
            for (int i = c.oldLo; i <= c.oldHi; ++i) {
                const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip);
                mn = std::min(mn, l.z);
            }
            if (mn > 2.0 * R) break;
            sim.grips[c.hold].at += ax * step;
            sim.hand += ax * step;
            sim.step(sim.hook);
            if (sim.onFrame) sim.onFrame();
        }
    }
    c.loopsAtStart = sim.loopsOnHook(front);
    // where the old loop sits on the shaft: the HOLD_T variants must leave it in the same place for the yarn over to be repeatable
    Real loAx = 1e9, hiAx = -1e9, loRad = 0, radMin = 1e9, radSum = 0;
    for (int i = c.oldLo; i <= c.oldHi; ++i) {
        const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip);
        loAx = std::min(loAx, l.z);
        hiAx = std::max(hiAx, l.z);
        const Real rad = std::hypot(l.x, l.y);
        loRad = std::max(loRad, rad);
        radMin = std::min(radMin, rad);
        radSum += rad;
    }
    std::printf("chain [%d]: the loop on the hook is yarn %d..%d at %.1f..%.1f mm on the shaft (%.1f..%.1f, mean %.1f mm out); %d loops from the front, around %d\n", sim.frame,
                c.oldLo, c.oldHi, loAx * 1e3, hiAx * 1e3, radMin * 1e3, loRad * 1e3, radSum / (Real)(c.oldHi - c.oldLo + 1) * 1e3,
                c.loopsAtStart, originInsideLoop(sim, c.oldLo, c.oldHi) ? 1 : 0);
    sim.stageLog += std::string("p") + std::to_string(sim.loopsOnHook(front)) + " ";
    if (envOr("SIM_STAGES", 0) > 0) std::printf("  prep %s: %d loops (front)\n", "snug", sim.loopsOnHook(front));
    c.tip0 = sim.hook.tip;
    // The roll: the throat holds the yarn at the neck while the hook turns toward the crocheter (over the top) one full turn round the neck
    // (one loop more on the hook), and a little past it so the loop holds; then the hook comes back to where it was.
    int catchParticle = -1;
    auto wind = [&]() -> Real {
        const int holdP = (envOr("YO_HOLD", 0) > 0 && catchParticle >= 0) ? catchParticle : c.oldHi + 2;
        sim.setThroatRing(true, holdP);
        const Real dA = 2 * kPi / (envOr("YO_ROLLT", 1.2) / sim.params.dt);
        Vec3 th = sim.hook.throat();
        const Real rollSense = envOr("YO_ROLL", -1);   // +1: back, up, front, down
        Real rolled = 0;
        while (rolled < envOr("YO_ROLLMAX", 1.2) * 2 * kPi) {
            th = rotate(quatFromAxisAngle(ax, -rollSense * dA), th);   // about the tip->handle axis; seen from the tip this goes back, up, front
            rolled += dA;
            sim.step(hookPose(sim.hook.tip, -ax, th));
            if (sim.onFrame) sim.onFrame();
            const int nowLoops = sim.loopsOnHook(front);
            if (envOr("SIM_ROLLDBG", 0) > 0 && (int)(rolled / dA) % 10 == 0)
                std::printf("    roll %.0f deg: %d loops (front) %d (up) %d (back), throat %s\n", rolled * 180 / kPi, nowLoops, sim.loopsOnHook(up),
                            sim.loopsOnHook(front * -1.0), sim.throatRingOn() ? "holds" : "empty");
            if (nowLoops * (c.loopsAtStart >= 0 ? 1 : -1) == std::abs(c.loopsAtStart) + 1) {   // one loop more, wound the same way
                for (int k = 0; k < (int)(envOr("YO_OVER", 0.1) * 2 * kPi / dA); ++k) {   // and a little further, so it holds
                    th = rotate(quatFromAxisAngle(ax, -rollSense * dA), th);
                    sim.step(hookPose(sim.hook.tip, -ax, th));
                    if (sim.onFrame) sim.onFrame();
                }
                break;
            }
        }
        sim.moveHook(hookPose(c.tip0, -ax, th), envOr("YO_RETURN", 0.6));
        return rolled;
    };
    // yarn over: the finger takes the working yarn round the neck just at the throat, starting behind and above, until the yarn after the loop
    // winds a full turn
    sim.phase = "Yarn over: the finger takes the yarn round the hook, into its throat";
    const Real sense = envOr("YO_SENSE", -1);
    Real turned = 0, wNew = 0;
    const int yoMode = (int)envOr("YO_MODE", 2);
    if (yoMode == 2) {
        // the finger behind the hook, to the left and above: the yarn runs from the work back past the hook to it. The throat turned back toward it,
        // the hook moved back until the yarn lies in the throat; then rolled toward the crocheter over the top: the yarn goes round the neck
        const Vec3 back = front * -1.0;
        const Real yoL = envOr("YO_L", -5);
        Vec3 handAt;
        if (carried) {
            // After a chain the old loop is a stitch near the tip, not the clean loop the rig lays: the yarn must leave its back end and
            // cross the shaft at the throat. Put the finger close in on the far side of the shaft from that end, so the yarn crosses the
            // shaft however the hook has rolled, instead of running past it on the same side.
            const Vec3 hx = rotate(sim.hook.q, Vec3{1, 0, 0}), hy = rotate(sim.hook.q, Vec3{0, 1, 0});
            const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[c.oldHi] - sim.hook.tip);
            const Real lr = std::hypot(l.x, l.y);
            const Real ang = lr > 1e-9 ? std::atan2(-l.y, -l.x) : kPi / 2;
            handAt = c.tip0 - ax * (envOr("YO_L2", -5) * m) + (hx * std::cos(ang) + hy * std::sin(ang)) * (envOr("YO_RADL", 10) * m);
            if (envOr("YO_SOLVE", 1) > 0) {
                // Put the finger on the line from the old loop's back end through the groove centre, so the yarn crosses the
                // throat by construction instead of being pushed aside by the head.
                const Vec3 G{sim.shape.grooveX * R, 0, sim.shape.grooveZ * R};
                const Vec3 dir = normalized(G - l);
                handAt = sim.hook.tip + rotate(sim.hook.q, G + dir * (envOr("YO_SOLVE_D", 20) * m));
            }
            if (envOr("SIM_STAGES", 0) > 0)
                std::printf("  yarn over: the old loop's back end is %.1f mm out at %.0f deg; the finger goes to %.0f deg, %.1f mm out\n",
                            lr * 1e3, std::atan2(l.y, l.x) * 180 / kPi, ang * 180 / kPi, envOr("YO_RADL", 10));
        } else {
            handAt = c.tip0 - ax * (yoL * m) + up * (envOr("YO_U", 10) * m) + back * (envOr("YO_B", 20) * m);
        }
        moveHandTo(sim, handAt, 0.6);
        // the finger moved along the hook until the yarn crosses behind the hook level with the throat
        const Real aT = dot(sim.throatPoint() - sim.hook.tip, ax);
        for (int it = 0; it < (int)envOr("YO_ITERS", 16); ++it) {
            Real best = 1e9, aC = aT, hC = 0;
            for (int i = c.oldHi + 2; i < sim.rs.particleCount(); ++i) {
                const Vec3 d = sim.rs.x[i] - sim.hook.tip;
                const Real a = dot(d, ax), h = dot(d, up), b = dot(d, back);
                if (b < 0 || a < -0.03 || a > 0.05) continue;
                if (std::fabs(h) < best) { best = std::fabs(h); aC = a; hC = b; }
            }
            if (envOr("SIM_STAGES", 0) > 0) std::printf("  yarn over: the yarn crosses %.1f mm behind the hook at %.1f mm (the throat %.1f)\n", hC * 1e3, aC * 1e3, aT * 1e3);
            if (std::fabs(aC - aT) < 1.5 * m) break;
            moveHandTo(sim, sim.hand - ax * ((aC - aT) * envOr("YO_GAIN", 0.7)), 0.35);
        }
        const Vec3 thrBack = normalized(back + up * envOr("YO_TU", 0.3));
        // The carried yarn leaves the old loop's back end and runs to the finger, which sits opposite that end. Turn the throat toward
        // the finger, not to a fixed world direction, so the open mouth faces the yarn instead of the head standing in its way.
        Vec3 aim = thrBack;
        if (envOr("YO_AIMFINGER", 0) > 0) {
            Vec3 toHand = sim.hand - sim.hook.tip;
            toHand = toHand - ax * dot(toHand, ax);
            if (length(toHand) > 1e-6) aim = normalized(toHand + up * envOr("YO_AIMUP", 0.2));
        }
        const HookPose aimPose = hookPose(c.tip0, -ax, aim);
        sim.moveHook(aimPose, 0.4);
        const Real step = envOr("YO_CSTEP", 0.1) * m;
        bool in = false;
        Real closest = 1e9;
        Real moved = 0;
        int bi = -1;
        Vec3 bl{};
        auto probe = [&] {
            closest = 1e9;
            bi = -1;
            for (int i = c.oldHi + 2; i < sim.rs.particleCount(); ++i) {
                // With a groove the throat is a line across the hook, not a point: measure to that line segment in 3D. Ignoring y
                // let a yarn hanging well below the hook count as caught just because it lined up in x-z.
                const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip);
                const Real dy = std::max(Real(0), std::fabs(l.y) - 1.30 * R);
                const Real d = sim.shape.groove ? std::hypot(std::hypot(l.x - sim.shape.grooveX * R, l.z - sim.shape.grooveZ * R), dy)
                                                : length(sim.rs.x[i] - sim.throatPoint());
                if (d < closest) { closest = d; bi = i; bl = l; }
            }
            in = bi >= 0 && closest < envOr("YO_CATCHR", 1.6) * m;
            if (in) catchParticle = bi;
        };
        // The finger lays the working yarn into the groove: move the hand by the yarn's offset from the groove, and repeat. The
        // hook stays put, so the old loop is not pushed off the tip while the yarn is being caught.
        if (envOr("YO_SERVO", 0) > 0) {
            for (int it = 0; it < (int)envOr("YO_SERVO_IT", 24) && !in; ++it) {
                probe();
                if (in || bi < 0) break;
                Vec3 err = rotate(sim.hook.q, Vec3{bl.x - sim.shape.grooveX * R, bl.y, bl.z - sim.shape.grooveZ * R});
                const Real e = length(err);
                if (e < 1e-9) break;
                if (e > 2.5 * m) err = err * (2.5 * m / e);
                if (envOr("YO_HOOKSERVO", 0) > 0) {
                    // The crocheter's right hand takes the hook to the yarn: move the hook so the groove surrounds the yarn. This does
                    // not pull the working yarn through the old loop the way moving the finger does; the hook (and the old loop with it)
                    // comes to where the yarn already lies.
                    sim.hook.tip += err * envOr("YO_SERVO_GAIN", 0.7);
                } else {
                    Vec3& h = sim.fingerOn ? sim.hand : sim.ball.target;
                    h -= err * envOr("YO_SERVO_GAIN", 0.7);
                }
                sim.step(sim.hook);
                if (sim.onFrame) sim.onFrame();
                if (envOr("SIM_SERVODBG", 0) > 0)
                    std::printf("    servo %d: d %.2f mm, err (%.1f %.1f %.1f) mm, finger %d\n", it, closest * 1e3, err.x * 1e3, err.y * 1e3,
                                err.z * 1e3, sim.fingerOn ? 1 : 0);
            }
            if (envOr("SIM_STAGES", 0) > 0)
                std::printf("  yarn over: the finger servo left the yarn %.2f mm from the groove (p%d)\n", closest * 1e3, bi);
        }
        for (; moved < envOr("YO_REACH", 35) * m && !in; moved += step) {
            sim.step(hookPose(sim.hook.tip + thrBack * step, -ax, thrBack));
            if (sim.onFrame) sim.onFrame();
            probe();
        }
        if (envOr("SIM_STAGES", 0) > 0) {
            std::printf("  yarn over: the hook moved %.1f mm back; the yarn came within %.2f mm of the throat", moved * 1e3, closest * 1e3);
            if (bi >= 0) std::printf(" (p%d, local %.1f %.1f %.1f R, %d after the loop)", bi, bl.x / R, bl.y / R, bl.z / R, bi - c.oldHi);
            std::printf("\n");
        }
        c.catchParticle = catchParticle;
        if (envOr("YO_GRIP", 0) > 0 && catchParticle >= 0) {
            sim.gripThroat(catchParticle);
            c.catchGripped = true;
            if (envOr("SIM_STAGES", 0) > 0) std::printf("  yarn over: the barb grips p%d at the throat\n", catchParticle);
        }
        turned = wind();
        if (c.catchGripped) sim.setThroatRing(false);   // the grip carries the yarn; no sliding ring competes with it
    } else if (yoMode == 4) {
        // the finger holds the yarn well away, behind and above the hook, to the left; the yarn runs from the loop to it. The hook scoops round
        // that yarn: its tip circles it (the throat toward the crocheter) until the yarn winds a full turn round the neck
        const Vec3 back = front * -1.0;
        // aimed: on the line from where the yarn leaves the loop through a point just behind and above the throat, some way beyond it
        const Vec3 E = sim.rs.x[std::min(sim.rs.particleCount() - 1, c.oldHi + 3)];
        const Vec3 Tq = sim.throatPoint() + normalized(back * envOr("YO_B", 1.0) + up * envOr("YO_U", 1.0)) * (envOr("YO_D", 6) * m);
        const Vec3 fingerAt = E + normalized(Tq - E) * (envOr("YO_L", 40) * m);
        moveHandTo(sim, fingerAt, 0.6);
        sim.ball.tension = envOr("YO_TEN", 2.0) * sim.params.tension;
        sim.moveHook(hookPose(c.tip0, -ax, front), 0.4);
        sim.settle((int)(envOr("YO_SETTLE", 0.6) / sim.params.dt));
        sim.ball.tension = sim.params.tension;
        // the nearest working yarn to the throat: the neck orbits it, in the plane square to the hook
        const Vec3 T0 = sim.throatPoint();
        Vec3 P = T0;
        Real bd = 1e9;
        for (int i = c.oldHi + 2; i < sim.rs.particleCount(); ++i) {
            const Vec3 d = sim.rs.x[i] - T0;
            const Real dd = length(d - ax * dot(d, ax));   // distance square to the hook
            if (std::fabs(dot(d, ax)) < 8 * m && dd < bd) { bd = dd; P = sim.rs.x[i]; }
        }
        P = P - ax * dot(P - T0, ax);   // level with the throat
        Vec3 r0 = T0 - P;
        const Real rh = envOr("YO_RH", 5) * m;
        const Vec3 e1 = length(r0) > 1e-6 ? normalized(r0) : front, e2 = cross(ax, e1);
        const Vec3 tipOff = sim.hook.tip - T0;
        // the hook goes to the yarn: the throat to beside it
        sim.moveHook(hookPose(P + e1 * rh + tipOff, -ax, front), envOr("YO_APPROACH", 0.5));
        std::printf("yarn over: the yarn is %.1f mm from the throat; the neck circles it at %.1f mm\n", bd * 1e3, rh * 1e3);
        if (envOr("YO_DUMP", 0) > 0) {
            const Vec3 lh = rotate(conj(sim.hook.q), sim.hand - sim.hook.tip);
            const Vec3 le = rotate(conj(sim.hook.q), sim.rs.x[sim.ball.particle] - sim.hook.tip);
            std::printf("    hand axial %.1f radial %.1f angle %.0f; yarn end axial %.1f radial %.1f; finger on %d eyelet %d\n", lh.z * 1e3,
                        std::hypot(lh.x, lh.y) * 1e3, std::atan2(lh.y, lh.x) * 180 / kPi, le.z * 1e3, std::hypot(le.x, le.y) * 1e3, sim.fingerOn,
                        sim.fingerEyelet);
            for (int i = c.oldLo - 4; i < std::min(sim.rs.particleCount(), c.oldHi + 40); i += 2) {
                const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip);
                std::printf("    p%d axial %.1f radial %.1f angle %.0f%s\n", i, l.z * 1e3, std::hypot(l.x, l.y) * 1e3, std::atan2(l.y, l.x) * 180 / kPi,
                            i >= c.oldLo && i <= c.oldHi ? "  (loop)" : "");
            }
        }
        const Real dAng = 2 * kPi / (envOr("YO_T", 1.4) / sim.params.dt);
        Real ang = 0;
        while (turned < 1.6 * 2 * kPi) {
            ang += sense * dAng;
            turned += dAng;
            const Vec3 T = P + (e1 * std::cos(ang) + e2 * std::sin(ang)) * rh;
            sim.step(hookPose(T + tipOff, -ax, front));
            if (sim.onFrame) sim.onFrame();
            wNew = sim.windingNear(c.oldHi + 2, sim.rs.particleCount() - 1, reach);
            if (std::fabs(wNew) >= envOr("YO_WANT", 0.97) && turned > kPi) break;
        }
        sim.setThroatRing(true, c.oldHi + 2);
        // back to where it works, the yarn caught
        sim.moveHook(hookPose(c.tip0, -ax, front), envOr("YO_RETURN", 0.6));
    } else {
        sim.moveHook(hookPose(c.tip0, -ax, front), 0.4);
        const Vec3 cc = c.tip0 + ax * (envOr("YO_AX", 2.5) * R);
        const Real rad = envOr("YO_R", 14) * m;
        Real ang = envOr("YO_A0", 2.6);
        auto at = [&](Real a) { return cc + (front * std::cos(a) + up * std::sin(a)) * rad; };
        moveHandTo(sim, at(ang), 0.6);
        const Real dAng = 2 * kPi / (envOr("YO_T", 1.4) / sim.params.dt);
        while (turned < 1.6 * 2 * kPi) {
            ang -= sense * dAng;
            turned += dAng;
            sim.hand = at(ang);
            sim.step(sim.hook);
            if (sim.onFrame) sim.onFrame();
            wNew = sim.windingNear(c.oldHi + 2, sim.rs.particleCount() - 1, reach);
            if (std::fabs(wNew) >= envOr("YO_WANT", 0.97) && turned > kPi) break;
        }
    }
    sim.setThroatRing(true, (envOr("YO_HOLD", 0) > 0 && catchParticle >= 0) ? catchParticle : c.oldHi + 2);
    c.loopsBefore = c.loopsAtStart;
    c.loopsAfterYo = sim.loopsOnHook(front);
    const bool caught = sim.throatRingOn() || c.catchGripped;
    sim.stageLog += "yo:" + std::to_string(c.loopsAfterYo) + (caught ? "t " : " ");
    if (yoMode == 2)
        std::printf("yarn over: the hook rolled %.0f degrees; %s; %d loops on the hook (were %d)\n", turned * 180 / kPi,
                    caught ? "caught in the throat" : "NOT caught", c.loopsAfterYo, c.loopsAtStart);
    else
        std::printf("yarn over: the working yarn winds %.2f after %.0f degrees; %s; %d loops on the hook (were %d)\n", wNew, turned * 180 / kPi,
                    caught ? "caught in the throat" : "NOT caught", c.loopsAfterYo, c.loopsAtStart);
    c.ok = true;
    return c;
}

void finishChain(YarnSim& sim, const ChainStart& c) {
    // PREP_HOLD=2 lets the old loop slide from the pull on; PREP_HOLD=3 is the left hand holding it while the hook is drawn out: the loop
    // stays where it is and the hook slides through it, instead of the two travelling together and no link forming.
    if (envOr("PREP_HOLD", 0) == 2) for (int sp : c.prepHeldSprings) sim.releaseGuideSpring(sp);
    const Real m = kMm;
    const Vec3 ax = sim.hook.axis();
    const Vec3 up{0, 1, 0}, front{0, 0, 1};
    auto stage = [&](const char* what) {
        sim.stageLog += std::string(what).substr(0, 4) + ":" + std::to_string(sim.loopsOnHook(front)) + (sim.throatRingOn() ? "t " : " ");
        if (envOr("SIM_STAGES", 0) <= 0) return;
        const int n = sim.rs.particleCount();
        (void)n;
        std::printf("  [%d] %s: %d loops on the hook (front), %d (up), throat %s\n", sim.frame, what, sim.loopsOnHook(front), sim.loopsOnHook(up),
                    sim.throatRingOn() ? "holds" : "empty");
    };
    stage("caught");
    // the hook turned toward the crocheter until its throat faces down, the yarn caught in it
    sim.phase = "The hook turns toward you, its throat down, the yarn caught in it";
    const Vec3 thrDown = normalized(front * envOr("PU_RF", 0.4) + up * envOr("PU_UP", -1.0));
    const Real rt = envOr("PU_RT", 0.6) / 2;
    sim.moveHook(hookPose(sim.hook.tip, -ax, normalized(front - up * 0.3)), rt);
    sim.moveHook(hookPose(sim.hook.tip, -ax, thrDown), rt);
    stage("turned");
    // The old loop must stay where it is while the hook (and the new loop) goes through it; otherwise it is dragged along and the new loop
    // never crosses it. The left hand holds it open at three points round its front, as a crocheter's thumb and finger do, and lets the hook
    // slide through the hole. PU_SPREAD is how far each point is opened out; PU_HOLDMM is how far the hook goes before the hand lets go.
    std::vector<int> spreadSprings;
    std::vector<Vec3> spreadBase, spreadDir;
    {
        const Real spread = envOr("PU_SPREAD", 0);
        if (spread > 0 && sim.assistPins > 0 && c.oldLo >= 0 && c.oldHi > c.oldLo) {
            const Vec3 ax0 = sim.hook.axis();
            Real zmin = 1e9, zmax = -1e9;
            for (int i = c.oldLo; i <= c.oldHi; ++i) {
                const Real z = dot(sim.rs.x[i] - sim.hook.tip, ax0);
                zmin = std::min(zmin, z);
                zmax = std::max(zmax, z);
            }
            std::vector<std::pair<Real, int>> ang;
            for (int i = c.oldLo; i <= c.oldHi; ++i) {
                const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip);
                if (dot(sim.rs.x[i] - sim.hook.tip, ax0) <= zmin + envOr("PU_SPREADBAND", 6.0) * m)
                    ang.push_back({std::atan2(l.y, l.x), i});
            }
            std::sort(ang.begin(), ang.end());
            const int n = std::min((int)envOr("PU_SPREADN", 3), (int)ang.size());
            std::vector<Vec3> base;
            std::vector<Vec3> dir;
            for (int k = 0; k < n; ++k) {
                const int i = ang[(size_t)((Real)k * ang.size() / std::max(1, n))].second;
                const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip);
                Vec3 d = rotate(sim.hook.q, Vec3{l.x, l.y, 0});
                const Real len = std::sqrt(dot(d, d));
                if (len < 1e-9) continue;
                d = d / len;
                const int sp = sim.addGuideSpring(i, sim.rs.x[i], 2000.0 * sim.assistPins, 0);
                if (sp < 0) continue;
                base.push_back(sim.rs.x[i]);
                dir.push_back(d);
                spreadSprings.push_back(sp);
                spreadBase.push_back(sim.rs.x[i]);
                spreadDir.push_back(d);
            }
            const int steps = std::max(1, (int)(envOr("PU_SPREADT", 0.4) / sim.params.dt));
            sim.phase = "The left hand holds the old loop open while the hook pulls through";
            for (int s2 = 1; s2 <= steps; ++s2) {
                for (size_t k = 0; k < spreadSprings.size(); ++k)
                    sim.moveGuideSpring(spreadSprings[k], spreadBase[k] + spreadDir[k] * (spread * m * (Real)s2 / steps));
                sim.step(sim.hook);
                if (sim.onFrame) sim.onFrame();
            }
        }
    }
    // The throat (and the new bight in it) must face the old loop's opening as the hook draws back; with the throat at a fixed world
    // direction it often faces the old loop's closed side, and the bight goes round it instead of through it. PU_ALIGN=1 rolls the hook
    // so the throat faces the middle of the old loop's arc (2: the opposite side); the spread hold opens that arc into a gate.
    Vec3 pullThroat = thrDown;
    if (envOr("PU_ALIGN", 0) > 0 && c.oldLo >= 0 && c.oldHi > c.oldLo) {
        const Vec3 tip0 = sim.hook.tip;
        const Quat q0 = sim.hook.q;
        Real sx = 0, sy = 0;
        for (int i = c.oldLo; i <= c.oldHi; ++i) {
            const Vec3 l = rotate(conj(q0), sim.rs.x[i] - tip0);
            const Real a = std::atan2(l.y, l.x);
            sx += std::cos(a);
            sy += std::sin(a);
        }
        Real mid = std::atan2(sy, sx) + envOr("PU_ALIGN_OFF", 0) * kPi / 180;
        if (envOr("PU_ALIGN", 0) >= 2) mid += kPi;
        const Vec3 aim = rotate(q0, Vec3{std::cos(mid), std::sin(mid), 0});
        pullThroat = normalized(aim + thrDown * envOr("PU_ALIGN_BLEND", 0.0));
        if (envOr("SIM_CHAINDBG", 0) > 0)
            std::printf("  pull: throat turned %.0f deg to face the old loop's arc\n", mid * 180 / kPi);
    }
    if (envOr("PU_ALIGN", 0) > 0) {
        sim.moveHook(hookPose(sim.hook.tip, -ax, pullThroat), envOr("PU_ALIGN_T", 0.3));
        if (sim.onFrame) sim.onFrame();
    }
    // drawn back through the loop: the hook (and the finger with it) to the right a little each frame until the old loop no longer winds round it
    sim.phase = "Pull through: the hook draws the yarn through the loop on the hook";
    const Real pullTension0 = sim.ball.tension;
    sim.ball.tension = envOr("PU_TEN", 1.0) * pullTension0;
    {
        const Real step = envOr("PU_V", 12) * m * sim.params.dt;
        const Real reach = envOr("CH_REACH", 9) * m;
        const int loopsBefore = sim.loopsOnHook(front);
        const Real maxPull = envOr("PU_MAX", 50) * m;
        int clear = 0;
        Real moved = 0;
        bool prepReleased = false;
        const Real spreadHold = envOr("PU_HOLDMM", 12) * m;
        for (; moved < maxPull && clear < (int)envOr("PU_EXTRA", 6); moved += step) {
            if (!spreadSprings.empty() && moved >= spreadHold) {
                for (int k = (int)spreadSprings.size() - 1; k >= 0; --k) sim.releaseGuideSpring(spreadSprings[k]);
                spreadSprings.clear();
                if (envOr("SIM_CHAINDBG", 0) > 0) std::printf("  spread: the hand lets the loop go at %.1f mm\\n", moved / m);
            }
            sim.hand += ax * (step * envOr("PU_FOLLOW", 1.0));
            sim.step(hookPose(sim.hook.tip + ax * step, -ax, pullThroat));
            if (sim.onFrame) sim.onFrame();
            if (!prepReleased && envOr("PREP_HOLD", 0) >= 3 && moved >= envOr("PU_HOLDMM", 12) * m) {
                for (int sp : c.prepHeldSprings) sim.releaseGuideSpring(sp);
                prepReleased = true;
            }
            // The rig's clean first loop comes off when the half-plane count drops. From the slip knot the half-plane count also drops when a
            // chain leg leaves, so there the old loop is off only when its own yarn no longer winds round the shaft.
            const bool halfPlane = (sim.rig && envOr("PU_RIGWIND", 0) <= 0) || sim.chains < (int)envOr("PU_HALF_CHAINS", 0);
            if (envOr("PU_CLEAR", 0) > 0) {
                // Winding round the hook's axis cannot tell a loop round the shaft from one that has slid off the tip: the axis line still
                // passes through its bight. The loop is off the tip when every particle of it lies on the tip side of the tip.
                Real aMax = -1e9, aMin = 1e9;
                for (int i = c.oldLo; i <= c.oldHi; ++i) {
                    const Real a = dot(sim.rs.x[i] - sim.hook.tip, ax);
                    aMax = std::max(aMax, a);
                    aMin = std::min(aMin, a);
                }
                clear = aMax < envOr("PU_CLEARMM", 0.5) * m ? clear + 1 : 0;
                if (envOr("SIM_CHAINDBG", 0) > 0 && ((int)std::lround(moved / step)) % 20 == 0)
                    std::printf("  pull %4.1f mm: the old loop sits %5.1f..%5.1f mm from the tip\n", moved * 1e3, aMin * 1e3, aMax * 1e3);
            } else if (halfPlane) {
                clear = std::abs(sim.loopsOnHook(front)) <= std::abs(loopsBefore) - 1 ? clear + 1 : 0;
            } else {
                clear = std::fabs(sim.windingNear(c.oldLo, c.oldHi, reach)) < envOr("PU_OLDWIND", 0.5) ? clear + 1 : 0;
            }
        }
        if (envOr("SIM_CHAINDBG", 0) > 0)
            std::printf("  pull: the hook moved %.1f mm; the old loop is %s (clear %d)\n", moved * 1e3,
                        clear >= (int)envOr("PU_EXTRA", 6) ? "off the tip" : "still on the hook", clear);
        for (int sp : c.prepHeldSprings) sim.releaseGuideSpring(sp);   // the pull through done: the loop may hang freely again
        // The winding and half-plane signals can both say "off" while the final linked stretch is still the old loop (a leg left, or the
        // loop is open but still links the axis). If so, keep drawing the hook through until the judge's own stretch is no longer the old loop.
        auto oldStill = [&] {
            int l = -1, h = -1;
            if (!hookLoop(sim, &l, &h) || c.oldLo < 0) return false;
            const int a = std::max(l, c.oldLo), b = std::min(h, c.oldHi);
            const int ov = b >= a ? b - a + 1 : 0;
            const int span = h - l + 1;
            return span > 0 && (2 * ov >= span || h <= c.oldHi + 1);
        };
        if (envOr("PU_FIX", 0) > 0)
            for (Real moved = 0; moved < envOr("PU_FIXMAX", 40) * m && oldStill(); moved += step) {
                sim.hand += ax * (step * envOr("PU_FOLLOW", 1.0));
                sim.step(hookPose(sim.hook.tip + ax * step, -ax, pullThroat));
                if (sim.onFrame) sim.onFrame();
            }
    }
    for (int k = (int)spreadSprings.size() - 1; k >= 0; --k) sim.releaseGuideSpring(spreadSprings[k]);
    spreadSprings.clear();
    sim.ball.tension = pullTension0;
    // The hook has drawn the new loop through; the old loop has not always slid off the head. The left hand then slips it forward off the tip
    // (the hook stays still, the new loop stays in the throat), instead of dragging the whole work further.
    const Real slip = envOr("PU_SLIP", 0);
    if (slip > 0 && c.oldLo >= 0 && c.oldHi > c.oldLo) {
        const Vec3 ax2 = sim.hook.axis();
        std::vector<std::pair<Real, int>> ord;
        for (int i = c.oldLo; i <= c.oldHi; ++i) ord.push_back({dot(sim.rs.x[i] - sim.hook.tip, ax2), i});
        std::sort(ord.begin(), ord.end());
        const int n = std::min((int)envOr("PU_SLIPN", 4), (int)ord.size());
        std::vector<int> slipSprings;
        std::vector<Vec3> slipBase;
        if (sim.assistPins > 0) {
            for (int k = 0; k < n; ++k) {
                const int i = ord[k].second;
                const int sp = sim.addGuideSpring(i, sim.rs.x[i], 2000.0 * sim.assistPins, 0);
                if (sp < 0) continue;
                slipSprings.push_back(sp);
                slipBase.push_back(sim.rs.x[i]);
            }
        }
        const int steps = std::max(1, (int)(envOr("PU_SLIPT", 0.3) / sim.params.dt));
        sim.phase = "The left hand slips the old loop off the tip";
        for (int k = 1; k <= steps; ++k) {
            for (size_t j = 0; j < slipSprings.size(); ++j)
                sim.moveGuideSpring(slipSprings[j], slipBase[j] - ax2 * (slip * m * (Real)k / steps));
            sim.step(sim.hook);
            if (sim.onFrame) sim.onFrame();
        }
        for (int sp : slipSprings) sim.releaseGuideSpring(sp);
        sim.settle(10);
        if (envOr("SIM_CHAINDBG", 0) > 0) std::printf("  slip: moved the old loop's front %.1f mm toward the tip\n", slip);
    }
    if (c.catchGripped && envOr("YO_GRIPREL", 0) <= 0) sim.releaseThroatGrip();
    stage("pulled");
    // the throat lets go; the yarn drawn snug at the finger; the work let go
    sim.phase = "The new loop is on the hook";
    {
        const int path = (int)envOr("PU_BACK", 2);
        const Real bt = envOr("PU_BT", 0.6) / 2;
        const Vec3 back = front * -1.0;
        if (path == 1) {   // on round the same way: down, back, up
            sim.moveHook(hookPose(sim.hook.tip, -ax, normalized(back - up * 0.3)), bt);
            sim.moveHook(hookPose(sim.hook.tip, -ax, normalized(up + back * 0.3)), bt);
        } else if (path == 2) {   // back the way it came: down, front, up
            sim.moveHook(hookPose(sim.hook.tip, -ax, normalized(front - up * 0.3)), bt);
            sim.moveHook(hookPose(sim.hook.tip, -ax, normalized(up + front * 0.5)), bt);
        }
        if (envOr("PU_HOLDREL", 1) > 0) sim.settle((int)(0.3 / sim.params.dt));
    }
    stage("turned back");
    if (envOr("PU_RELEASE_LATE", 1) <= 0) sim.setThroatRing(false);
    sim.ball.tension = envOr("PU_SNUG", 2.0) * sim.params.tension;
    sim.settle((int)(envOr("PU_SNUGT", 0.5) / sim.params.dt));
    sim.ball.tension = sim.params.tension;
    stage("snug");
    sim.setThroatRing(false);
    sim.release(c.hold);
    sim.settle(20);
    stage("released");
    if (c.catchGripped && envOr("YO_GRIPREL", 0) > 0) sim.releaseThroatGrip();
    sim.releaseGuideSprings();
    ++sim.chains;
}

// Does the old loop's projection along the hook axis enclose the shaft axis? A loop that merely hangs beside the shaft has the
// origin outside its projected polygon, and then the caught yarn cannot be pulled through its hole however the hook moves.
static bool originInsideLoop(const YarnSim& sim, int lo, int hi) {
    if (lo < 0 || hi <= lo + 2) return false;
    bool inside = false;
    for (int i = lo, j = hi; i <= hi; j = i++) {
        const Vec3 a = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip);
        const Vec3 b = rotate(conj(sim.hook.q), sim.rs.x[j] - sim.hook.tip);
        if ((a.y > 0) != (b.y > 0) && 0 < (b.x - a.x) * (-a.y) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside;
}

// Signed crossings of the open yarn path B through a triangle fan spanning the closed loop A. This is the stitch link without
// closing B with an arbitrary chord: +-1 means the working yarn passes through the old loop once.
static int loopCrossings(const std::vector<Vec3>& A, const std::vector<Vec3>& B, Real r) {
    if (A.size() < 3 || B.size() < 2) return 0;
    Vec3 C{};
    for (const Vec3& p : A) C += p;
    C = C / (Real)A.size();
    int sum = 0;
    for (size_t k = 0; k + 1 < B.size(); ++k) {
        const Vec3 p0 = B[k], p1 = B[k + 1], d = p1 - p0;
        for (size_t i = 0; i < A.size(); ++i) {
            const Vec3& a = A[i];
            const Vec3& b = A[(i + 1) % A.size()];
            const Vec3 e1 = a - C, e2 = b - C;
            const Vec3 n = cross(e1, e2);
            const Real den = dot(d, n);
            if (std::fabs(den) < 1e-12) continue;
            const Real t = dot(C - p0, n) / den;
            if (t <= 0 || t >= 1) continue;
            const Vec3 p = p0 + d * t;
            // barycentric in the triangle (C, a, b)
            const Vec3 v0 = a - C, v1 = b - C, v2 = p - C;
            const Real d00 = dot(v0, v0), d01 = dot(v0, v1), d11 = dot(v1, v1), d20 = dot(v2, v0), d21 = dot(v2, v1);
            const Real den2 = d00 * d11 - d01 * d01;
            if (std::fabs(den2) < 1e-18) continue;
            const Real u = (d11 * d20 - d01 * d21) / den2;
            const Real v = (d00 * d21 - d01 * d20) / den2;
            if (u <= 0 || v <= 0 || u + v >= 1) continue;
            // ignore a crossing right at either end of the working-yarn stretch
            sum += den > 0 ? 1 : -1;
        }
    }
    return sum;
}

bool hookLoop(const YarnSim& sim, int* from, int* to, int searchFrom, Real touchRadii, const std::function<bool(int, int)>& accept) {
    // the hook's axis, closed far away
    const Vec3 ax = sim.hook.axis();
    Vec3 u = Vec3{0, 0, 1} - ax * ax.z;
    if (length(u) < 1e-6) u = Vec3{0, 1, 0} - ax * ax.y;
    u = normalized(u);
    const std::vector<Vec3> H{sim.hook.tip - ax * 0.05, sim.hook.tip + ax * 0.08, sim.hook.tip + ax * 0.08 + u * 1.0, sim.hook.tip - ax * 0.05 + u * 1.0};
    // stretches of yarn closed where the yarn touches itself
    const int n = sim.rs.particleCount();
    // Two strands touch when their centrelines are within `touchRadii` yarn radii (the simulator uses 5; the judge uses 2.5, because a loop 5 mm or
    // more across has points 5r apart on its own far side, which must not count as a crossing).
    const Real touch2 = std::pow(touchRadii * sim.params.radius, 2);
    int bestSpan = 1 << 30;
    bool found = false;
    for (int i = std::max(0, searchFrom); i < n; ++i)
        for (int j = i + 12; j < n; ++j) {
            if (j - i >= bestSpan) break;
            if (length2(sim.rs.x[i] - sim.rs.x[j]) > touch2) continue;
            if (accept && !accept(i, j)) continue;
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

// Is a point inside the opening of a yarn loop, in the loop's own best-fit plane? A stitch needs the new loop inside the old
// loop's opening and some of the old loop's yarn inside the new loop's; this is the direct geometric test, independent of closures.
static bool pointInLoop(const std::vector<Vec3>& loop, const Vec3& p) {
    if (loop.size() < 3) return false;
    Vec3 c{};
    for (const Vec3& q : loop) c += q;
    c = c / (Real)loop.size();
    Vec3 n{};
    for (size_t i = 0; i < loop.size(); ++i) n += cross(loop[i] - c, loop[(i + 1) % loop.size()] - c);
    const Real nl = std::sqrt(dot(n, n));
    if (nl < 1e-12) return false;
    n = n / nl;
    Vec3 u = std::fabs(n.x) < 0.9 ? cross(n, Vec3{1, 0, 0}) : cross(n, Vec3{0, 1, 0});
    const Real ul = std::sqrt(dot(u, u));
    if (ul < 1e-12) return false;
    u = u / ul;
    const Vec3 v = cross(n, u);
    const Real px = dot(p - c, u), py = dot(p - c, v);
    int wn = 0;
    for (size_t i = 0; i < loop.size(); ++i) {
        const Vec3& a = loop[i];
        const Vec3& b = loop[(i + 1) % loop.size()];
        const Real ax = dot(a - c, u), ay = dot(a - c, v);
        const Real bx = dot(b - c, u), by = dot(b - c, v);
        if (ay <= py) {
            if (by > py && (bx - ax) * (py - ay) - (by - ay) * (px - ax) > 0) ++wn;
        } else if (by <= py && (bx - ax) * (py - ay) - (by - ay) * (px - ax) < 0) {
            --wn;
        }
    }
    return wn != 0;
}

ChainResult judgeChain(const YarnSim& sim, const ChainStart& c) {
    ChainResult r;
    const int front = sim.loopsOnHook(Vec3{0, 0, 1});
    int lo = -1, hi = -1;
    // The half-plane count also sees the chain's legs hanging beside the hook; the linking stretch is the stitch itself. Fall back to the
    // half-plane count when no stretch closes on itself (the loop is too loose for the linking test).
    const bool linked = hookLoop(sim, &lo, &hi);
    r.frontLoops = front;
    // The linked stretch is one loop. Its front/back sign is a diagnostic in frontLoops: after the turn-back the fifth loop can wind the
    // other way and still be a loop on the shaft, so the sign must not decide `made`.
    r.loops = linkedLoopCount(linked, front);
    Real len = 0;
    for (int s = 0; s + 1 < sim.rs.segmentCount(); ++s) len += sim.rs.restLen[s];
    r.length = len;
    // The linking stretch decides whether the final loop is the old loop or a new one. The half-plane counts before and after the yarn over
    // are diagnostics only: a chain leg can leave while a new loop is made, or the count can rise while the old loop stays. A stitch is one
    // linking loop whose stretch is mostly new yarn, with more yarn drawn from the hand.
    int overlap = 0;
    if (linked && c.oldLo >= 0 && c.oldHi >= c.oldLo) {
        const int a = std::max(lo, c.oldLo), b = std::min(hi, c.oldHi);
        if (b >= a) overlap = b - a + 1;
    }
    const int span = linked ? hi - lo + 1 : 0;
    const Real oldFraction = span > 0 ? (Real)overlap / span : 0;
    r.oldOn = overlap;
    if (linked) {
        Real sum = 0;
        for (int i = lo; i <= hi; ++i) {
            const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip);
            const Real rad = std::hypot(l.x, l.y);
            sum += rad;
            r.linkMaxR = std::max(r.linkMaxR, rad);
        }
        r.linkMeanR = sum / span;
    }
    const bool newLoop = linked && oldFraction < 0.5;
    // Measure the old loop's own yarn outside the final stretch: if it still winds round the shaft, the old loop is a separate bight that
    // the next chain can pick instead of the working loop. It reads near zero in the rig, so the false fifth's bight is the lower work.
    Real oldWind = 0;
    if (c.oldLo >= 0) {
        if (linked) {
            if (c.oldLo < lo) oldWind += sim.windingNear(c.oldLo, lo - 1, sim.shape.R + 3.0 * sim.params.radius);
            if (c.oldHi > hi) oldWind += sim.windingNear(hi + 1, c.oldHi, sim.shape.R + 3.0 * sim.params.radius);
        } else {
            oldWind = sim.windingNear(c.oldLo, c.oldHi, sim.shape.R + 3.0 * sim.params.radius);
        }
    }
    r.oldWind = oldWind / (2 * kPi);
    // A linking stretch entirely before the old loop's last particle is an earlier part of the work, not the yarn just drawn through.
    const bool newYarn = linked && c.oldHi >= 0 && hi > c.oldHi + 1;
    r.newYarn = newYarn;
    // The final stretch's particles: its start is diagnostic, since a new loop can wrap back through the old loop.
    r.linkLo = lo;
    r.linkHi = hi;
    // The new loop may wrap through the old loop, so its stretch can start before the old work ended; it must, however, reach the yarn at
    // the end of the last valid stitch. A stretch that stops short is an earlier work loop (no new stitch was drawn through the row's end).
    r.sequential = linked && (c.prevHi < 0 || hi >= c.prevHi);
    const int added = c.loopsAfterYo - c.loopsBefore;
    // The old loop has to be off the hook: a final linking stretch mostly inside the old loop's particles is the old loop, not a new stitch,
    // even when the half-plane count rose during the yarn over. The stretch must also reach the yarn drawn after the old loop and follow the
    // previous stitch rather than relink an earlier part of the work.
    r.made = c.ok && r.loops == 1 && newLoop && newYarn && r.sequential && len > c.length + 0.004;
    Real oldFinalLink = 0;
    if (linked && c.oldLo >= 0 && c.oldHi >= c.oldLo) {
        const std::vector<Vec3> A(sim.rs.x.begin() + c.oldLo, sim.rs.x.begin() + c.oldHi + 1);
        const std::vector<Vec3> B(sim.rs.x.begin() + lo, sim.rs.x.begin() + hi + 1);
        oldFinalLink = linkingNumber(A, B);
    }
    // A clean stitch link: a self-closing stretch made only of yarn after the old loop, so the two closed curves do not share the old
    // loop's particles (sharing them forces the link to zero by construction). This is the link the chain actually needs.
    int clo = -1, chi = -1;
    Real cleanLink = 0;
    if (c.oldHi >= 0 && hookLoop(sim, &clo, &chi, c.oldHi + 1)) {
        const std::vector<Vec3> A(sim.rs.x.begin() + c.oldLo, sim.rs.x.begin() + c.oldHi + 1);
        const std::vector<Vec3> B(sim.rs.x.begin() + clo, sim.rs.x.begin() + chi + 1);
        cleanLink = linkingNumber(A, B);
    }
    int cross = 0;
    if (c.oldLo >= 0 && c.oldHi >= c.oldLo) {
        const std::vector<Vec3> A(sim.rs.x.begin() + c.oldLo, sim.rs.x.begin() + c.oldHi + 1);
        const std::vector<Vec3> B(sim.rs.x.begin() + c.oldHi + 1, sim.rs.x.end());
        cross = loopCrossings(A, B, sim.params.radius);
    }
    // The whole working yarn includes both legs of the new bight, whose crossings cancel; the clean stretch is the bight alone.
    int crossClean = 0;
    int inOld = 0, inNew = 0, nOld = 0, nNew = 0;
    if (linked && c.oldLo >= 0 && c.oldHi >= c.oldLo) {
        const std::vector<Vec3> A(sim.rs.x.begin() + c.oldLo, sim.rs.x.begin() + c.oldHi + 1);
        const std::vector<Vec3> B(sim.rs.x.begin() + lo, sim.rs.x.begin() + hi + 1);
        for (const Vec3& q : B) {
            ++nNew;
            if (pointInLoop(A, q)) ++inOld;
        }
        for (const Vec3& q : A) {
            ++nOld;
            if (pointInLoop(B, q)) ++inNew;
        }
        if (clo >= 0 && chi > clo) {
            const std::vector<Vec3> Bc(sim.rs.x.begin() + clo, sim.rs.x.begin() + chi + 1);
            crossClean = loopCrossings(A, Bc, sim.params.radius);
        }
    }
    const bool oldAround = originInsideLoop(sim, c.oldLo, c.oldHi);
    if (envOr("SIM_CHAINDBG", 0) > 0)
        std::printf("  judge: old %d..%d, final %d..%d (span %d, overlap %d, old %.0f%%, r mean/max %.1f/%.1f mm), front %d, after %d, "
                    "added %d, linked %d, new %d, newyarn %d, seq %d (prev %d), oldwind %.2f, old-final link %.2f, clean %d..%d link %.2f, "
                    "cross %d, crossClean %d, inOld %d/%d, inNew %d/%d, around %d\n",
                    c.oldLo, c.oldHi, lo, hi, span, overlap, oldFraction * 100, r.linkMeanR * 1e3, r.linkMaxR * 1e3, r.frontLoops,
                    c.loopsAfterYo, added, linked ? 1 : 0, newLoop ? 1 : 0, newYarn ? 1 : 0, r.sequential ? 1 : 0, c.prevHi, r.oldWind,
                    oldFinalLink, clo, chi, cleanLink, cross, crossClean, inOld, nNew, inNew, nOld, oldAround ? 1 : 0);
    if (envOr("SIM_CHAINDBG", 0) > 0 && !linked && c.oldLo >= 0) {
        int a = -1, b = -1;
        if (loopRange(sim, 9e-3, 0.5, &a, &b)) {
            const std::vector<Vec3> A(sim.rs.x.begin() + c.oldLo, sim.rs.x.begin() + c.oldHi + 1);
            const std::vector<Vec3> B(sim.rs.x.begin() + a, sim.rs.x.begin() + b + 1);
            std::printf("  judge: no closing stretch; winding candidate %d..%d, old-final link %.2f\n", a, b, linkingNumber(A, B));
        }
    }
    return r;
}

// Signed crossings of the open yarn path B through a triangle fan spanning the loop A, with their sign along the disc's normal (the Newell
// normal of A). The stitch's two legs are counted separately so a yarn merely running past the old loop cannot pass for a stitch.
struct LegCrossings {
    int in = 0, out = 0;       // signed
    int inCount = 0, outCount = 0;   // unsigned: every crossing counts, whichever way it goes
    bool valid = true;
};

// The signed crossings of B through the fan (the sum), and the number of crossings whichever way they go (`count`).
static int signedFanCrossings(const std::vector<Vec3>& A, const Vec3& C, const Vec3& N, const std::vector<Vec3>& B, Real r, int* count) {
    if (A.size() < 3 || B.size() < 2) return 0;
    int sum = 0;
    if (count) *count = 0;
    for (size_t k = 0; k + 1 < B.size(); ++k) {
        const Vec3 p0 = B[k], p1 = B[k + 1], d = p1 - p0;
        const int sign = dot(d, N) > 0 ? 1 : -1;
        for (size_t i = 0; i < A.size(); ++i) {
            const Vec3& a = A[i];
            const Vec3& b = A[(i + 1) % A.size()];
            const Vec3 n = cross(a - C, b - C);
            const Real den = dot(d, n);
            if (std::fabs(den) < 1e-12) continue;
            const Real t = dot(C - p0, n) / den;
            if (t <= 0 || t >= 1) continue;
            const Vec3 p = p0 + d * t;
            const Vec3 v0 = a - C, v1 = b - C, v2 = p - C;
            const Real d00 = dot(v0, v0), d01 = dot(v0, v1), d11 = dot(v1, v1), d20 = dot(v2, v0), d21 = dot(v2, v1);
            const Real den2 = d00 * d11 - d01 * d01;
            if (std::fabs(den2) < 1e-18) continue;
            const Real u = (d11 * d20 - d01 * d21) / den2;
            const Real v = (d00 * d21 - d01 * d20) / den2;
            if (u <= 0 || v <= 0 || u + v >= 1) continue;
            sum += sign;
            if (count) ++*count;
        }
    }
    return sum;
}

static LegCrossings legCrossings(const std::vector<Vec3>& A, const std::vector<Vec3>& Bin, const std::vector<Vec3>& Bout, Real r) {
    LegCrossings lc;
    if (A.size() < 3) {
        lc.valid = false;
        return lc;
    }
    Vec3 C{};
    for (const Vec3& p : A) C += p;
    C = C / (Real)A.size();
    Vec3 N{};
    for (size_t i = 0; i < A.size(); ++i) N += cross(A[i] - C, A[(i + 1) % A.size()] - C);
    if (length(N) < 1e-12) {
        lc.valid = false;
        return lc;
    }
    N = normalized(N);
    lc.in = signedFanCrossings(A, C, N, Bin, r, &lc.inCount);
    lc.out = signedFanCrossings(A, C, N, Bout, r, &lc.outCount);
    return lc;
}

// The judge's touch distance for the final loop (see hookLoop): 2.5 yarn radii first, where a true stitch's legs touch (2r apart). A loop whose
// legs are looser has no touch that close, so the judge falls back to the simulator's 5 radii for it.
const Real kJudgeTouchRadii = 2.5;
// The old loop itself is not the final loop: a candidate that covers 80% or more of the old loop's yarn is refused.
bool judgeLoop(const YarnSim& sim, const ChainStart& c, int* from, int* to) {
    auto accept = [&](int i, int j) {
        if (c.oldLo < 0 || c.oldHi < c.oldLo) return true;
        const int a = std::max(i, c.oldLo), b = std::min(j, c.oldHi);
        return (b - a + 1) < 0.8 * (c.oldHi - c.oldLo + 1);
    };
    return hookLoop(sim, from, to, -1, kJudgeTouchRadii, accept) || hookLoop(sim, from, to, -1, 5.0, accept);
}

StitchVerdict judgeStitch(const YarnSim& sim, const ChainStart& c) {
    StitchVerdict v;
    const Real R = sim.shape.R, r = sim.params.radius;
    int lo = -1, hi = -1;
    v.onHook = judgeLoop(sim, c, &lo, &hi) && hi > lo;
    v.linkLo = lo;
    v.linkHi = hi;
    const int span = v.onHook ? hi - lo + 1 : 0;
    // The old loop is off the hook when the final loop is mostly new yarn and the old loop's own yarn no longer winds round the shaft.
    if (v.onHook && c.oldLo >= 0 && c.oldHi >= c.oldLo) {
        const int a = std::max(lo, c.oldLo), b = std::min(hi, c.oldHi);
        v.fresh = b >= a ? (Real)(b - a + 1) / span : 0;
    }
    v.newYarn = v.onHook && c.oldHi >= 0 && hi > c.oldHi + 1;
    v.sequential = v.onHook && (c.prevHi < 0 || hi >= c.prevHi);
    Real oldWind = 0;
    if (c.oldLo >= 0) {
        if (v.onHook) {
            if (c.oldLo < lo) oldWind += sim.windingNear(c.oldLo, lo - 1, R + 3.0 * r);
            if (c.oldHi > hi) oldWind += sim.windingNear(hi + 1, c.oldHi, R + 3.0 * r);
        } else {
            oldWind = sim.windingNear(c.oldLo, c.oldHi, R + 3.0 * r);
        }
    }
    v.oldOff = oldWind;   // windingNear already returns turns
    // The yarn drawn through the old loop is split into the new loop's two legs. The inner leg starts on the old loop's edge, so its crossing
    // can fall inside the two skipped particles; the stitch passes when the outer leg crosses the disc at least once. The yarn beyond hi+6,
    // running to the finger, never counts. A disc whose old-loop ends are more than three yarn widths apart is not a disc: `?` fails.
    v.throughValid = c.oldLo >= 0 && c.oldHi > c.oldLo && v.onHook && hi > c.oldHi;
    if (v.throughValid) {
        const int n = sim.rs.particleCount();
        if (length(sim.rs.x[c.oldHi] - sim.rs.x[c.oldLo]) > 6 * r || c.oldHi + 3 >= n) {
            v.throughValid = false;
        } else {
            const int mid = (lo + hi) / 2;
            const int in0 = std::min(n - 1, c.oldHi + 3), in1 = std::max(in0, std::min(mid, n - 1));
            const int out1 = std::min(n - 1, hi + 6);
            const std::vector<Vec3> A(sim.rs.x.begin() + c.oldLo, sim.rs.x.begin() + c.oldHi + 1);
            const std::vector<Vec3> Bin(sim.rs.x.begin() + in0, sim.rs.x.begin() + in1 + 1);
            const std::vector<Vec3> Bout(sim.rs.x.begin() + mid, sim.rs.x.begin() + out1 + 1);
            const LegCrossings lc = legCrossings(A, Bin, Bout, r);
            v.throughIn = lc.in;
            v.throughValid = lc.valid;
            v.through = lc.out;
            v.throughCount = lc.outCount;
        }
    }
    if (v.onHook) {
        Real sum = 0;
        for (int i = lo; i <= hi; ++i) {
            const Vec3 l = rotate(conj(sim.hook.q), sim.rs.x[i] - sim.hook.tip);
            sum += std::hypot(l.x, l.y);
            if (i < hi) v.length += sim.rs.restLen[i];
        }
        v.meanR = sum / span;
    }
    // Running contact: the worst gap and the deepest penetration over the whole stitch, not just at its end.
    v.gap = sim.worstGap < 1e8 ? sim.worstGap : sim.minGap();
    v.depth = sim.worstPen;
    const Real Lstar = 2 * kPi * (R + 0.6 * r) + 2e-3;
    const bool onShaft = v.meanR >= R + 0.6 * r && v.length >= 19e-3 && v.length <= 1.35 * Lstar;
    const bool contact = v.gap >= 0.95 * 2 * r && v.depth <= 0.4e-3;
    v.onShaftPass = onShaft;
    v.contactPass = contact;
    v.pass = v.onHook && v.fresh < 0.25 && v.newYarn && v.sequential && std::fabs(v.oldOff) < 0.25 && v.throughValid && v.throughCount >= 1 &&
             onShaft && contact;
    // A stitch that could not be started has not advanced the chain count; its line still names the stitch it attempted.
    const int k = sim.chains + (c.ok ? 0 : 1);
    char through[32];
    if (v.throughValid) std::snprintf(through, sizeof through, "%+d/%+d", v.throughIn, v.through);
    else std::snprintf(through, sizeof through, "?");
    std::printf("stitch %d %s final %d..%d old %d..%d onHook=%d fresh=%.2f newYarn=%d sequential=%d oldOff=%.2f through=%s onShaft=%.1f/%.1f contact=%.2f/%.2f "
                "assist grip %.2f:%.2f ring %.2f:%.2f catch %.2f:%.2f pins %.2f:%.2f reel %.2f:%.2f hook %.2f mN.s\n",
                k, v.pass ? "PASS" : "FAIL", v.linkLo, v.linkHi, c.oldLo, c.oldHi, v.onHook ? 1 : 0, v.fresh, v.newYarn ? 1 : 0, v.sequential ? 1 : 0, v.oldOff, through,
                v.meanR * 1e3, v.length * 1e3, v.gap * 1e3, v.depth * 1e3, sim.assistGrip, sim.impulseGrip * 1e3, sim.assistRing,
                sim.impulseRing * 1e3, sim.assistCatch, sim.impulseCatch * 1e3, sim.assistPins, sim.impulsePins * 1e3, sim.assistReel,
                sim.impulseReel * 1e3, sim.impulseHook * 1e3);
    return v;
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// The judge fixtures (plan section 4.5.1): analytic yarn in the hook's frame (mm, then m), with no simulation. The hook lies along +z from the
// origin, its mouth toward +x, the work below it toward -y. The true stitch: the new loop N is a circle round the shaft in the plane z = 12 mm, at
// radius R + 0.6r + 0.2 mm. Its two legs run down from the circle's lowest point, 2r apart in x, through the old loop O (a ring in the plane
// y = -7 mm round both legs). The yarn runs earlier work -> O -> the first leg up through O -> N -> the second leg down through O -> the finger.
// Each negative changes one thing; the note names it.
namespace {

// The particle nearest to `p`, searching forward from `from`: the yarn is built in path order, so each marker lies after the one before it.
int nearestFrom(const YarnSim& sim, const Vec3& p, int from) {
    int best = std::max(0, from);
    Real bd = 1e9;
    for (int i = best; i < sim.rs.particleCount(); ++i) {
        const Real d = length(sim.rs.x[i] - p);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

}  // namespace

StitchFixture buildStitchFixture(const std::string& name) {
    StitchFixture f;
    YarnSim& sim = f.sim;
    static const char* const kNames[] = {"true", "beside", "beside-far", "old-on-shaft", "neck", "loose", "contact", "fresh"};
    bool known = false;
    for (const char* k : kNames) known = known || name == k;
    if (!known) {
        f.note = "unknown fixture " + name;
        return f;
    }
    sim.hook = hookPose(Vec3{}, Vec3{0, 0, -1}, Vec3{1, 0, 0});   // the hook's frame is the world frame
    const Real R = sim.shape.R / kMm, r = sim.params.radius / kMm;   // mm
    const Real zc = 12.0, legX = 1.1;                                  // the legs sit 2r apart, 12 mm along the shaft
    // The new loop's centreline radius round the shaft. The plan's 0.2 mm of slack leaves the loop 18.9 mm long, under the 19 mm floor with 2r
    // between the legs; 0.5 mm of slack gives 19.4 mm.
    Real rN = R + 0.6 * r + 0.5;
    Real yO = -7.0, cxO = 0.0;                                         // the old loop's plane and the x of its centre
    const Real rhoO = 3.3;                                             // the old loop: clear of both legs by 2r (3.3 - 1.1 = 2.2 mm)
    if (name == "beside" || name == "beside-far") cxO = -7.6;          // the old loop moved off to the side of the legs
    if (name == "neck") {                                              // a small loop with its legs through a low old loop
        rN = 1.4;
        yO = -3.5;
    }
    if (name == "loose") rN = 5.6;
    const bool shaftRing = name == "old-on-shaft";                     // the old loop still round the shaft, at z = 8 mm
    const Real yTop = -std::sqrt(rN * rN - legX * legX), yBot = yO - 0.5;

    // Points in mm; add() converts them to m.
    std::vector<Vec3> path;
    auto add = [&](const Vec3& p) { path.push_back(p * kMm); };
    auto ringPt = [&](Real th) -> Vec3 {
        if (shaftRing) return Vec3{rN * std::cos(th), rN * std::sin(th), 8.0};
        return Vec3{cxO + rhoO * std::cos(th), yO, zc + rhoO * std::sin(th)};
    };
    // The old loop: 300 degrees, with its gap at the bottom where the yarn comes in. A 60 degree gap keeps the ends 3.3 mm apart, so the ring is
    // not a closed loop of its own (a 340 degree ring closes on itself and the judge takes it for the final loop).
    const Real thS = -60.0 * kPi / 180.0, span = 300.0 * kPi / 180.0;
    const int nO = 30;
    const Vec3 S = ringPt(thS), E0{4.85, -26, 4.8};   // the earlier work comes up from below, from the same place for every fixture
    add(E0);
    add((E0 + S) * 0.5);   // earlier work, from below
    for (int k = 0; k <= nO; ++k) add(ringPt(thS + span * k / nO));
    const Vec3 Oend = ringPt(thS + span);
    add(Vec3{Oend.x, yO - 3.5, Oend.z});   // the connection runs 3.5 mm below the old loop's plane, clear of its ring
    add(Vec3{-legX, yO - 3.5, zc});
    add(Vec3{-legX, yBot, zc});   // the first leg's foot, below the old loop
    add(Vec3{-legX, yTop, zc});   // up through the old loop, to the circle
    const Real phi0 = std::atan2(yTop, -legX), phiE = std::atan2(yTop, legX);
    const Real arc = phi0 - phiE + 2 * kPi;   // round the shaft, the long way, from the first leg to the second
    const int nN = 32;
    for (int k = 1; k <= nN; ++k) {
        const Real phi = phi0 - arc * k / nN;
        add(Vec3{rN * std::cos(phi), rN * std::sin(phi), zc});
    }
    add(Vec3{legX, yBot, zc});   // down through the old loop again
    add(Vec3{legX, -13.0, zc});  // on down, clear of the old loop, before the yarn turns for the finger
    if (name == "beside-far") {
        // the yarn after the stitch runs back through the old loop's disc, well beyond the window the judge reads (hi+6)
        add(Vec3{cxO, -13.0, zc});
        add(Vec3{cxO, yO, zc});
        add(Vec3{cxO, yO + 4.0, zc});
    }
    add(Vec3{20, -5, 40});   // on to the finger

    // Catmull-Rom through sharp corners overshoots by about 2 mm; a dense polyline keeps the legs straight.
    std::vector<Vec3> dense;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const Real n = std::max(1.0, std::ceil(length(path[i + 1] - path[i]) / (0.25 * kMm)));
        for (int k = 0; k < (int)n; ++k) dense.push_back(path[i] + (path[i + 1] - path[i]) * (k / n));
    }
    dense.push_back(path.back());
    sim.build(dense);
    const Vec3 Smm = S * kMm, Oendm = Oend * kMm;
    const int oldLo = nearestFrom(sim, Smm, 0), oldHi = nearestFrom(sim, Oendm, oldLo);
    f.chain.ok = true;
    f.chain.oldLo = oldLo;
    f.chain.oldHi = oldHi;
    f.chain.prevHi = -1;
    sim.chains = 1;   // the old loop is the first stitch's; the fixture's stitch is the second

    if (name == "contact") {
        // one particle of the first leg moved 1.5 mm into the second leg: the yarn passes within 0.4 r of itself
        const int p = nearestFrom(sim, Vec3{-legX, -6.0, zc} * kMm, 0);
        sim.rs.x[p].x += 1.5 * kMm;
    }
    sim.worstGap = sim.minGap();
    sim.worstPen = sim.hookPenetration();
    if (name == "fresh") {
        // the new loop built from the old loop's own yarn: the judge's old loop covers 60% of the stitch's stretch
        int lo = -1, hi = -1;
        judgeLoop(sim, f.chain, &lo, &hi);
        f.chain.oldLo = lo + 1;
        f.chain.oldHi = lo + (hi - lo) * 6 / 10;
    }
    static const char* const kNotes[] = {
        "the true stitch: O round both legs in y=-7 mm, N on the shaft, both legs through O",
        "O moved 6 mm to the side, so the legs pass beside it",
        "as beside, and the yarn beyond hi+6 runs back through O's disc (the judge reads only to hi+6)",
        "O still round the shaft at z=8 mm, N in front of it",
        "N is a small loop (r 1.4 mm), its legs through a low old loop: the stretch is about 13 mm",
        "N is a 35 mm loose loop (r 5.6 mm)",
        "the true stitch with one leg particle moved 1.5 mm into the other leg",
        "the true stitch with the old loop covering 60% of N's stretch",
    };
    for (size_t i = 0; i < sizeof kNames / sizeof kNames[0]; ++i)
        if (name == kNames[i]) f.note = kNotes[i];
    return f;
}

ChainStart makeChainDriven(YarnSim& sim) {
    // Phase D2 builds the driven sequence here; until then the chain is reported as not made.
    std::printf("driven chain: not built yet\n");
    return ChainStart{};
}

void makeChain(YarnSim& sim) {
    const ChainStart c = prepareChain(sim);
    if (c.ok) finishChain(sim, c);
    const ChainResult r = judgeChain(sim, c);
    judgeStitch(sim, c);
    if (r.made) {
        sim.lastLinkHi = r.linkHi;
        sim.lastLinkLo = r.linkLo;
        sim.lastLinkChain = sim.chains;
    }
}

}  // namespace fl

