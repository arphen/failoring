// Yarn mode Y1: the Cosserat rod solver. See include/failoring/crochet/rod.hpp for the model and docs/YARN_MODE_PLAN.md.
//
// Conventions used throughout. A segment s has a unit quaternion q[s]; its local z axis, d3 = q e3 q*, is the tangent. An orientation
// correction is a small WORLD rotation vector dtheta applied on the left: q <- exp(dtheta / 2) q. Under it d3 changes by dtheta x d3.
//
//   stretch-shear  C = (p1 - p0) / l - d3                          (3 comps; strain of the segment and its shear)
//     dC/dp0 = -I/l, dC/dp1 = I/l, dC/dtheta = [d3]x               so  S = (w0 + w1)/l^2 I + wq (I - d3 d3^T) + alpha
//   bend-twist     C = log(q_a* q_b) - phi0                        (3 comps, in a's frame: bend x, y; twist z)
//     for small relative rotations dC = u_b - u_a with u = R_a^T dtheta, so each component has S = wq_a + wq_b + alpha_k
//
// alpha is the XPBD compliance over the substep squared. Energies: stretch 1/2 EA l eps^2, bend 1/2 (EI/l) phi^2, twist 1/2 (GJ/l) phi^2.
#include "failoring/crochet/rod.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fl {

// ---------------------------------------------------------------------------------------------------------------- quaternions

Quat operator*(const Quat& a, const Quat& b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

Quat normalized(const Quat& q) {
    const Real n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (n < 1e-300) return Quat();
    return {q.w / n, q.x / n, q.y / n, q.z / n};
}

Vec3 rotate(const Quat& q, const Vec3& v) {
    const Vec3 qv{q.x, q.y, q.z};
    const Vec3 t = 2.0 * cross(qv, v);
    return v + q.w * t + cross(qv, t);
}

Quat quatFromAxisAngle(const Vec3& axis, Real angle) {
    const Real h = 0.5 * angle, s = std::sin(h);
    return {std::cos(h), axis.x * s, axis.y * s, axis.z * s};
}

Quat quatFromTo(const Vec3& a, const Vec3& b) {
    const Real d = dot(a, b);
    if (d < -1.0 + 1e-12) {  // opposite: a half turn about any axis square to a
        Vec3 ax = cross(a, Vec3{1, 0, 0});
        if (length2(ax) < 1e-6) ax = cross(a, Vec3{0, 1, 0});
        return quatFromAxisAngle(normalized(ax), kPi);
    }
    const Vec3 c = cross(a, b);
    return normalized(Quat{1.0 + d, c.x, c.y, c.z});
}

Vec3 rotationVector(const Quat& q0) {
    Quat q = q0;
    if (q.w < 0) q = {-q.w, -q.x, -q.y, -q.z};
    const Vec3 v{q.x, q.y, q.z};
    const Real s = length(v);
    const Real k = s < 1e-9 ? 2.0 / std::max(q.w, 1e-12) : 2.0 * std::atan2(s, q.w) / s;
    return v * k;
}

namespace {

// q <- exp(dtheta / 2) q, the rotation vector dtheta in the world frame
Quat applyRot(const Quat& q, const Vec3& dtheta) {
    const Real a = length(dtheta);
    if (a < 1e-300) return q;
    return normalized(quatFromAxisAngle(dtheta / a, a) * q);
}

const Vec3 kE3{0, 0, 1};

// 3x3 matrices, row-major, for the direct solve
struct M3 {
    Real m[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
};
M3 ident3() {
    M3 a;
    a.m[0] = a.m[4] = a.m[8] = 1;
    return a;
}
M3 operator*(const M3& a, const M3& b) {
    M3 c;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) c.m[3 * i + j] = a.m[3 * i] * b.m[j] + a.m[3 * i + 1] * b.m[3 + j] + a.m[3 * i + 2] * b.m[6 + j];
    return c;
}
M3 operator+(const M3& a, const M3& b) {
    M3 c;
    for (int i = 0; i < 9; ++i) c.m[i] = a.m[i] + b.m[i];
    return c;
}
M3 operator-(const M3& a, const M3& b) {
    M3 c;
    for (int i = 0; i < 9; ++i) c.m[i] = a.m[i] - b.m[i];
    return c;
}
M3 operator*(const M3& a, Real s) {
    M3 c;
    for (int i = 0; i < 9; ++i) c.m[i] = a.m[i] * s;
    return c;
}
M3 tr3(const M3& a) {
    M3 c;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) c.m[3 * i + j] = a.m[3 * j + i];
    return c;
}
Vec3 mulv(const M3& a, const Vec3& v) {
    return {a.m[0] * v.x + a.m[1] * v.y + a.m[2] * v.z, a.m[3] * v.x + a.m[4] * v.y + a.m[5] * v.z, a.m[6] * v.x + a.m[7] * v.y + a.m[8] * v.z};
}
M3 skew3(const Vec3& v) {
    M3 a;
    a.m[1] = -v.z; a.m[2] = v.y;
    a.m[3] = v.z;  a.m[5] = -v.x;
    a.m[6] = -v.y; a.m[7] = v.x;
    return a;
}
M3 outer3(const Vec3& a, const Vec3& b) {
    M3 c;
    const Real av[3] = {a.x, a.y, a.z}, bv[3] = {b.x, b.y, b.z};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) c.m[3 * i + j] = av[i] * bv[j];
    return c;
}
M3 rotMat(const Quat& q) {  // the matrix of v -> q v q*
    const Real w = q.w, x = q.x, y = q.y, z = q.z;
    M3 a;
    a.m[0] = 1 - 2 * (y * y + z * z); a.m[1] = 2 * (x * y - w * z);     a.m[2] = 2 * (x * z + w * y);
    a.m[3] = 2 * (x * y + w * z);     a.m[4] = 1 - 2 * (x * x + z * z); a.m[5] = 2 * (y * z - w * x);
    a.m[6] = 2 * (x * z - w * y);     a.m[7] = 2 * (y * z + w * x);     a.m[8] = 1 - 2 * (x * x + y * y);
    return a;
}
M3 inv3(const M3& a) {
    const Real* m = a.m;
    M3 c;
    c.m[0] = m[4] * m[8] - m[5] * m[7]; c.m[1] = m[2] * m[7] - m[1] * m[8]; c.m[2] = m[1] * m[5] - m[2] * m[4];
    c.m[3] = m[5] * m[6] - m[3] * m[8]; c.m[4] = m[0] * m[8] - m[2] * m[6]; c.m[5] = m[2] * m[3] - m[0] * m[5];
    c.m[6] = m[3] * m[7] - m[4] * m[6]; c.m[7] = m[1] * m[6] - m[0] * m[7]; c.m[8] = m[0] * m[4] - m[1] * m[3];
    const Real det = m[0] * c.m[0] + m[1] * c.m[3] + m[2] * c.m[6];
    return c * (1.0 / det);
}

// J_l^-1(phi): d(log(exp(delta) exp(phi))) / d(delta). A joint's relative rotation r = q_a* q_b changes by a left perturbation exp(delta)
// when either orientation is perturbed, delta = u_b - u_a in a's frame, so this is the exact Jacobian of the bend-twist constraint
// (the identity only for small angles: a loop of yarn turns 0.5 rad or more per 2 mm node).
M3 jlInv(const Vec3& phi) {
    const Real th = length(phi);
    const M3 K = skew3(phi);
    M3 J = ident3() - K * 0.5;
    if (th > 1e-4 && th < 3.0) {
        const Real k = 1.0 / (th * th) - (1.0 + std::cos(th)) / (2.0 * th * std::sin(th));
        J = J + (K * K) * k;
    } else if (th <= 1e-4) {
        J = J + (K * K) * (1.0 / 12.0);
    }
    return J;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------- building

int RodSystem::addChain(const std::vector<Vec3>& pts, bool closed, bool restFromShape) {
    const int n = (int)pts.size();
    const int nSeg = closed ? n : n - 1;
    if (n < 2 || (closed && n < 3)) return -1;
    RodChain ch;
    ch.firstParticle = (int)x.size();
    ch.particles = n;
    ch.firstSegment = (int)segA.size();
    ch.segments = nSeg;
    ch.firstJoint = (int)jointA.size();
    ch.joints = closed ? nSeg : nSeg - 1;
    ch.closed = closed;
    const int ci = (int)chains.size();

    std::vector<Real> len(nSeg);
    std::vector<Vec3> tan(nSeg);
    for (int k = 0; k < nSeg; ++k) {
        const Vec3 d = pts[(k + 1) % n] - pts[k];
        len[k] = length(d);
        tan[k] = len[k] > 1e-300 ? d / len[k] : kE3;
    }
    for (int i = 0; i < n; ++i) {
        Real m = 0;
        if (closed || i > 0) m += 0.5 * len[(i + nSeg - 1) % nSeg];
        if (closed || i < n - 1) m += 0.5 * len[i % nSeg];
        const Real wb = m > 0 ? 1.0 / (params.linearDensity * m) : 0.0;
        const Real wi = wb / massScale_;
        wBase.push_back(wb);
        x.push_back(pts[i]);
        xPrev.push_back(pts[i]);
        v.push_back(Vec3{});
        w.push_back(wi);
        wFree.push_back(wi);
        extForce.push_back(Vec3{});
        pinTarget.push_back(pts[i]);
        particleChain.push_back(ci);
        anchor.push_back(pts[i]);
    }
    Quat qk = quatFromTo(kE3, tan[0]);
    for (int k = 0; k < nSeg; ++k) {
        if (k > 0) qk = normalized(quatFromTo(tan[k - 1], tan[k]) * qk);
        segA.push_back(ch.firstParticle + k);
        segB.push_back(ch.firstParticle + (k + 1) % n);
        segChain.push_back(ci);
        segIndex.push_back(k);
        q.push_back(qk);
        qPrev.push_back(qk);
        omega.push_back(Vec3{});
        const Real m = params.linearDensity * len[k];
        const Real r = params.radius;
        mass.push_back(m);
        wq.push_back(m > 0 ? 12.0 / (m * (3.0 * r * r + len[k] * len[k])) / rotInertiaScale_ : 0.0);
        restLen.push_back(len[k]);
        slack.push_back(1.0);
        radius.push_back(r);
    }
    for (int j = 0; j < ch.joints; ++j) {
        const int a = ch.firstSegment + j, b = ch.firstSegment + (j + 1) % nSeg;
        jointA.push_back(a);
        jointB.push_back(b);
        restDarboux.push_back(restFromShape ? rotationVector(conj(q[a]) * q[b]) : Vec3{});
    }
    chains.push_back(ch);
    lambdaS_.assign(3 * segA.size(), 0.0);
    lambdaB_.assign(3 * jointA.size(), 0.0);
    lambdaA_.assign(3 * x.size(), 0.0);
    return ci;
}

void RodSystem::extendChain(const std::vector<Vec3>& pts) {
    if (chains.empty() || chains.back().closed) return;
    RodChain& ch = chains.back();
    const int ci = (int)chains.size() - 1;
    for (const Vec3& p : pts) {
        const int last = ch.firstParticle + ch.particles - 1;   // the present end particle
        const int lastSeg = ch.firstSegment + ch.segments - 1;
        const Vec3 d = p - x[last];
        const Real len = fl::length(d);
        if (len < 1e-12) continue;
        const Vec3 t = d / len;
        const Vec3 tPrev = rotate(q[lastSeg], kE3);
        const Quat qn = normalized(quatFromTo(tPrev, t) * q[lastSeg]);
        // the old end particle now has a segment on each side: its mass grows by half the new segment
        const Real mOld = wBase[last] > 0 ? 1.0 / wBase[last] : 0.0;
        const Real mNew = mOld + 0.5 * params.linearDensity * len;
        wBase[last] = 1.0 / mNew;
        wFree[last] = wBase[last] / massScale_;
        if (w[last] > 0) w[last] = wFree[last];
        const Real wb = 1.0 / (0.5 * params.linearDensity * len);
        const Real wi = wb / massScale_;
        x.push_back(p); xPrev.push_back(p); v.push_back(Vec3{});
        w.push_back(wi); wFree.push_back(wi); wBase.push_back(wb);
        extForce.push_back(Vec3{}); pinTarget.push_back(p); particleChain.push_back(ci); anchor.push_back(p);
        const int sIdx = (int)segA.size();
        segA.push_back(last);
        segB.push_back((int)x.size() - 1);
        segChain.push_back(ci);
        segIndex.push_back(ch.segments);
        q.push_back(qn); qPrev.push_back(qn); omega.push_back(Vec3{});
        const Real m = params.linearDensity * len, r = params.radius;
        mass.push_back(m);
        wq.push_back(12.0 / (m * (3.0 * r * r + len * len)) / rotInertiaScale_);
        restLen.push_back(len);
        slack.push_back(1.0);
        radius.push_back(r);
        jointA.push_back(lastSeg);
        jointB.push_back(sIdx);
        {   // the yarn keeps the bend it was made with (a loop stays a loop): bend rest from the shape, no rest twist
            Vec3 rd = rotationVector(conj(q[lastSeg]) * qn);
            rd.z = 0;
            restDarboux.push_back(rd * restBendMemory);
        }
        ++ch.particles;
        ++ch.segments;
        ++ch.joints;
    }
    lambdaS_.assign(3 * segA.size(), 0.0);
    lambdaB_.assign(3 * jointA.size(), 0.0);
    lambdaA_.assign(3 * x.size(), 0.0);
}

void RodSystem::popEnd() {
    if (chains.empty() || chains.back().closed) return;
    RodChain& ch = chains.back();
    if (ch.segments < 2) return;
    const int last = (int)x.size() - 1;
    if (ch.firstParticle + ch.particles - 1 != last) return;
    const Real len = restLen.back();
    x.pop_back(); xPrev.pop_back(); v.pop_back(); w.pop_back(); wFree.pop_back(); wBase.pop_back();
    extForce.pop_back(); pinTarget.pop_back(); particleChain.pop_back(); anchor.pop_back();
    segA.pop_back(); segB.pop_back(); segChain.pop_back(); segIndex.pop_back();
    q.pop_back(); qPrev.pop_back(); omega.pop_back(); mass.pop_back(); wq.pop_back(); restLen.pop_back(); radius.pop_back(); slack.pop_back();
    jointA.pop_back(); jointB.pop_back(); restDarboux.pop_back();
    --ch.particles;
    --ch.segments;
    --ch.joints;
    // the new end particle has lost half a segment of mass
    const int e = last - 1;
    const Real m = (wBase[e] > 0 ? 1.0 / wBase[e] : 0.0) - 0.5 * params.linearDensity * len;
    if (m > 1e-12) {
        wBase[e] = 1.0 / m;
        wFree[e] = wBase[e] / massScale_;
        if (w[e] > 0) w[e] = wFree[e];
    }
    lambdaS_.assign(3 * segA.size(), 0.0);
    lambdaB_.assign(3 * jointA.size(), 0.0);
    lambdaA_.assign(3 * x.size(), 0.0);
}

void RodSystem::setRestLengthsFromShape(int chain, Real scale) {
    const RodChain& ch = chains[chain];
    for (int k = 0; k < ch.segments; ++k) {
        const int s = ch.firstSegment + k;
        restLen[s] = length(x[segB[s]] - x[segA[s]]) * scale;
    }
}

void RodSystem::scaleRestLengths(Real factor) {
    for (Real& l : restLen) l *= factor;
}

void RodSystem::setRestTwist(int chain, Real radPerSegment) {
    const RodChain& ch = chains[chain];
    for (int j = 0; j < ch.joints; ++j) restDarboux[ch.firstJoint + j].z = radPerSegment;
}

void RodSystem::freezeOutside(const Vec3& centre, Real radius) {
    if (frozen()) thaw();
    savedW_ = w;
    savedWq_ = wq;
    segActive_.assign(segA.size(), 0);
    const Real r2 = radius * radius;
    std::vector<char> free(x.size(), 0);
    for (size_t i = 0; i < x.size(); ++i) {
        if (length2(x[i] - centre) <= r2) free[i] = 1;
        else w[i] = 0;
    }
    for (size_t s = 0; s < segA.size(); ++s) {
        if (free[segA[s]] || free[segB[s]]) segActive_[s] = 1;
        else wq[s] = 0;
    }
}

void RodSystem::thaw() {
    if (!frozen()) return;
    // a particle pinned while frozen stays pinned: its saved inverse mass was 0 already
    w = savedW_;
    wq = savedWq_;
    savedW_.clear();
    savedWq_.clear();
    segActive_.clear();
}

void RodSystem::setMassScale(Real sc) {
    massScale_ = sc;
    const bool wasFrozen = frozen();
    for (size_t i = 0; i < x.size(); ++i) {
        wFree[i] = wBase[i] / sc;
        if (wasFrozen) { if (savedW_[i] > 0) savedW_[i] = wFree[i]; }
        else if (w[i] > 0) w[i] = wFree[i];
    }
}

void RodSystem::setRotationalInertiaScale(Real sc) {
    rotInertiaScale_ = sc;
    for (size_t s = 0; s < segA.size(); ++s) {
        if (wq[s] <= 0 && !(frozen() && savedWq_[s] > 0)) continue;
        const Real r = radius[s], l = restLen[s];
        const Real v = mass[s] > 0 ? 12.0 / (mass[s] * (3.0 * r * r + l * l)) / sc : 0.0;
        if (frozen()) savedWq_[s] = v;   // a frozen segment gets it when it thaws
        else wq[s] = v;
    }
}

void RodSystem::pin(int i) {
    w[i] = 0;
    pinTarget[i] = x[i];
}

void RodSystem::unpin(int i) { w[i] = wFree[i]; }

Real RodSystem::chainLength(int chain) const {
    const RodChain& ch = chains[chain];
    Real L = 0;
    for (int k = 0; k < ch.segments; ++k) {
        const int s = ch.firstSegment + k;
        L += length(x[segB[s]] - x[segA[s]]);
    }
    return L;
}

Real RodSystem::chainRestLength(int chain) const {
    const RodChain& ch = chains[chain];
    Real L = 0;
    for (int k = 0; k < ch.segments; ++k) L += restLen[ch.firstSegment + k];
    return L;
}

Vec3 RodSystem::jointRotation(int j) const { return rotationVector(conj(q[jointA[j]]) * q[jointB[j]]); }

std::vector<Real> RodSystem::cumulativeTwist(int chain) const {
    const RodChain& ch = chains[chain];
    std::vector<Real> out(ch.joints);
    Real acc = 0;
    for (int j = 0; j < ch.joints; ++j) {
        acc += jointRotation(ch.firstJoint + j).z;
        out[j] = acc;
    }
    return out;
}

Real RodSystem::kineticSpeedMax() const {
    Real m = 0;
    for (size_t i = 0; i < x.size(); ++i)
        if (w[i] > 0) m = std::max(m, length(v[i]));
    return m;
}

// ---------------------------------------------------------------------------------------------------------------- constraints

void RodSystem::solveStretchShear(int s, Real h2inv) {
    const int a = segA[s], b = segB[s];
    const Real l = restLen[s];
    const Vec3 d3 = rotate(q[s], kE3);
    const Vec3 C = (x[b] - x[a]) / l - d3;
    const Real wa = w[a], wb = w[b], wqs = wq[s];
    const Real aStretch = slack[s] * h2inv / (params.EA * l), aShear = slack[s] * h2inv / (params.GAs * l);
    const Real aa = (wa + wb) / (l * l);
    if (aa + wqs <= 0) return;
    Vec3 lam{lambdaS_[3 * s], lambdaS_[3 * s + 1], lambdaS_[3 * s + 2]};
    const Real ld = dot(d3, lam);
    const Vec3 lamPerp = lam - ld * d3;
    const Vec3 r = -C - (aStretch * ld) * d3 - aShear * lamPerp;
    const Real rd = dot(d3, r);
    const Vec3 rp = r - rd * d3;
    const Vec3 dl = d3 * (rd / (aa + aStretch)) + rp / (aa + wqs + aShear);
    lambdaS_[3 * s] += dl.x;
    lambdaS_[3 * s + 1] += dl.y;
    lambdaS_[3 * s + 2] += dl.z;
    x[a] -= dl * (wa / l);
    x[b] += dl * (wb / l);
    if (wqs > 0) q[s] = applyRot(q[s], wqs * cross(dl, d3));
}

void RodSystem::solveBendTwist(int j, Real h2inv) {
    const int sa = jointA[j], sb = jointB[j];
    const Real wa = wq[sa], wb = wq[sb];
    if (wa + wb <= 0) return;
    const Vec3 phi = rotationVector(conj(q[sa]) * q[sb]);
    const Vec3 C = phi - restDarboux[j];
    const M3 Jp = jlInv(phi);
    const Real lAvg = 0.5 * (restLen[sa] + restLen[sb]);
    const Real ab = h2inv * lAvg / params.EI, at = h2inv * lAvg / params.GJ;
    const Vec3 lam{lambdaB_[3 * j], lambdaB_[3 * j + 1], lambdaB_[3 * j + 2]};
    // S = (wa + wb) Jp Jp^T + alpha, a 3x3 solve
    M3 S = (Jp * tr3(Jp)) * (wa + wb);
    S.m[0] += ab; S.m[4] += ab; S.m[8] += at;
    const Vec3 rhs{-C.x - ab * lam.x, -C.y - ab * lam.y, -C.z - at * lam.z};
    const Vec3 dl = mulv(inv3(S), rhs);
    lambdaB_[3 * j] += dl.x;
    lambdaB_[3 * j + 1] += dl.y;
    lambdaB_[3 * j + 2] += dl.z;
    const Vec3 du = mulv(tr3(Jp), dl);   // the correction in a's frame
    const Vec3 dw = rotate(q[sa], du);   // in the world frame
    if (wb > 0) q[sb] = applyRot(q[sb], dw * wb);
    if (wa > 0) q[sa] = applyRot(q[sa], dw * (-wa));
}

// All stretch-shear and bend-twist constraints of one open chain in one banded solve (Deul et al. 2018, "Direct position-based solver
// for stiff rods"). The constraints are ordered s0, j0, s1, j1, ... (s_k the stretch-shear of segment k, j_k the bend-twist between
// segments k and k+1); the matrix J W J^T + alpha is then block pentadiagonal (3x3 blocks) and a block LDL^T solves it in O(n).
// Written out:
//   s_k  diag   (a + aStretch) d d^T + (a + wq + aShear)(I - d d^T),  a = (w_k + w_k+1)/l^2
//   s_k  - s_k+1   -w_{p k+1} / (l_k l_k+1) I                         (the shared particle)
//   s_k  - j_k     -wq_k [d_k]x R_k                                   (the shared orientation of segment k)
//   s_k+1 - j_k    wq_k+1 [d_k+1]x R_k                                (the shared orientation of segment k+1)
//   j_k  diag   (wq_k + wq_k+1) I + diag(ab, ab, at)
//   j_k  - j_k+1   -wq_k+1 R_k^T R_k+1
void RodSystem::solveChainDirect(int ci, Real h2inv) {
    const RodChain& ch = chains[ci];
    const int S = ch.segments;
    const int M = 2 * S - 1;
    if (M <= 0) return;
    thread_local std::vector<M3> A0, A1, A2, L1, L2, Dinv, Dm, Rm, Kj;
    thread_local std::vector<Vec3> rhs, y, z, sol, d3v;
    A0.assign(M, M3()); A1.assign(M, M3()); A2.assign(M, M3());
    L1.assign(M, M3()); L2.assign(M, M3()); Dinv.assign(M, M3()); Dm.assign(M, M3());
    Rm.assign(S, M3());
    Kj.assign(S, M3());
    rhs.assign(M, Vec3{}); y.assign(M, Vec3{}); z.assign(M, Vec3{}); sol.assign(M, Vec3{}); d3v.assign(S, Vec3{});
    const int s0 = ch.firstSegment;
    for (int k = 0; k < S; ++k) {
        Rm[k] = rotMat(q[s0 + k]);
        d3v[k] = Vec3{Rm[k].m[2], Rm[k].m[5], Rm[k].m[8]};
    }
    for (int k = 0; k < S; ++k) {
        const int s = s0 + k, a = segA[s], b = segB[s];
        const Real l = restLen[s], wa = w[a], wb = w[b], wqs = wq[s];
        const Vec3 d = d3v[k];
        const Real aStretch = slack[s] * h2inv / (params.EA * l), aShear = slack[s] * h2inv / (params.GAs * l);
        const Real aa = (wa + wb) / (l * l);
        const M3 dd = outer3(d, d);
        A0[2 * k] = dd * (aa + aStretch) + (ident3() - dd) * (aa + wqs + aShear);
        const Vec3 C = (x[b] - x[a]) / l - d;
        const Vec3 lam{lambdaS_[3 * s], lambdaS_[3 * s + 1], lambdaS_[3 * s + 2]};
        const Real ld = dot(d, lam);
        rhs[2 * k] = -C - d * (aStretch * ld) - (lam - d * ld) * aShear;
        if (k > 0) {
            // with the previous segment: the shared particle (2 back) and the shared orientation (1 back, with joint k-1)
            const int sp = s - 1;
            A2[2 * k] = ident3() * (-w[a] / (restLen[sp] * l));
        }
        if (k < S - 1) {
            const int j = ch.firstJoint + k;
            const int sb2 = s + 1;
            const Real lAvg = 0.5 * (l + restLen[sb2]);
            const Real ab = h2inv * lAvg / params.EI, at = h2inv * lAvg / params.GJ;
            const Real wsum = wqs + wq[sb2];
            const Vec3 phi = rotationVector(conj(q[s]) * q[sb2]);
            const M3 Jp = jlInv(phi);
            Kj[k] = Jp * tr3(Rm[k]);                 // d C / d theta_b (and minus d C / d theta_a), world rotations
            M3 dj = (Jp * tr3(Jp)) * wsum;
            dj.m[0] += ab; dj.m[4] += ab; dj.m[8] += at;
            A0[2 * k + 1] = dj;
            // (s_k, j_k) = -wq_k [d_k]x K^T, so (j_k, s_k) is its transpose
            A1[2 * k + 1] = tr3(skew3(d) * tr3(Kj[k]) * (-wqs));
            const Vec3 C2 = phi - restDarboux[j];
            const Vec3 lamj{lambdaB_[3 * j], lambdaB_[3 * j + 1], lambdaB_[3 * j + 2]};
            rhs[2 * k + 1] = Vec3{-C2.x - ab * lamj.x, -C2.y - ab * lamj.y, -C2.z - at * lamj.z};
        }
    }
    // (s_k+1, j_k) = wq_k+1 [d_k+1]x K_k^T, written at row s_k+1 as its (n, n-1) block; (j_k+1, j_k) = -wq_k+1 K_k+1 K_k^T
    for (int k = 1; k < S; ++k) A1[2 * k] = skew3(d3v[k]) * tr3(Kj[k - 1]) * wq[s0 + k];
    for (int k = 1; k < S - 1; ++k) A2[2 * k + 1] = Kj[k] * tr3(Kj[k - 1]) * (-wq[s0 + k]);
    // block LDL^T: Lambda1 = L(n, n-1), Lambda2 = L(n, n-2), D(n)
    for (int n = 0; n < M; ++n) {
        M3 T = A1[n];
        if (n >= 2) {
            L2[n] = A2[n] * Dinv[n - 2];
            T = T - A2[n] * tr3(L1[n - 1]);
        }
        M3 Dn = A0[n];
        if (n >= 1) {
            L1[n] = T * Dinv[n - 1];
            Dn = Dn - T * tr3(L1[n]);
        }
        if (n >= 2) Dn = Dn - A2[n] * tr3(L2[n]);
        Dm[n] = Dn;
        Dinv[n] = inv3(Dn);
    }
    for (int n = 0; n < M; ++n) {
        Vec3 t = rhs[n];
        if (n >= 1) t -= mulv(L1[n], y[n - 1]);
        if (n >= 2) t -= mulv(L2[n], y[n - 2]);
        y[n] = t;
        z[n] = mulv(Dinv[n], t);
    }
    for (int n = M - 1; n >= 0; --n) {
        Vec3 t = z[n];
        if (n + 1 < M) t -= mulv(tr3(L1[n + 1]), sol[n + 1]);
        if (n + 2 < M) t -= mulv(tr3(L2[n + 2]), sol[n + 2]);
        sol[n] = t;
    }
    // apply: particles and orientations
    for (int k = 0; k < S; ++k) {
        const int s = s0 + k;
        lambdaS_[3 * s] += sol[2 * k].x; lambdaS_[3 * s + 1] += sol[2 * k].y; lambdaS_[3 * s + 2] += sol[2 * k].z;
        if (k < S - 1) {
            const int j = ch.firstJoint + k;
            lambdaB_[3 * j] += sol[2 * k + 1].x; lambdaB_[3 * j + 1] += sol[2 * k + 1].y; lambdaB_[3 * j + 2] += sol[2 * k + 1].z;
        }
    }
    for (int i = 0; i < ch.particles; ++i) {
        const int pi = ch.firstParticle + i;
        if (w[pi] <= 0) continue;
        Vec3 dx{};
        if (i > 0) dx += sol[2 * (i - 1)] / restLen[s0 + i - 1];
        if (i < S) dx -= sol[2 * i] / restLen[s0 + i];
        x[pi] += dx * w[pi];
    }
    for (int k = 0; k < S; ++k) {
        const int s = s0 + k;
        if (wq[s] <= 0) continue;
        Vec3 dth = cross(sol[2 * k], d3v[k]);                    // [d]x^T dlambda
        if (k < S - 1) dth -= mulv(tr3(Kj[k]), sol[2 * k + 1]);      // joint k: segment k is its first (a)
        if (k > 0) dth += mulv(tr3(Kj[k - 1]), sol[2 * k - 1]);      // joint k-1: segment k is its second (b)
        q[s] = applyRot(q[s], dth * wq[s]);
    }
}

// ---------------------------------------------------------------------------------------------------------------- contact

SegSegResult closestSegmentSegment(const Vec3& p0, const Vec3& p1, const Vec3& q0, const Vec3& q1) {
    const Vec3 d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
    const Real a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r);
    Real s = 0, t = 0;
    const Real eps = 1e-18;
    if (a <= eps && e <= eps) {
    } else if (a <= eps) {
        t = std::clamp(f / e, 0.0, 1.0);
    } else {
        const Real c = dot(d1, r);
        if (e <= eps) {
            s = std::clamp(-c / a, 0.0, 1.0);
        } else {
            const Real b = dot(d1, d2), denom = a * e - b * b;
            s = denom > 1e-12 * a * e ? std::clamp((b * f - c * e) / denom, 0.0, 1.0) : 0.0;
            t = (b * s + f) / e;
            if (t < 0) {
                t = 0;
                s = std::clamp(-c / a, 0.0, 1.0);
            } else if (t > 1) {
                t = 1;
                s = std::clamp((b - c) / a, 0.0, 1.0);
            }
        }
    }
    SegSegResult out;
    out.s = s;
    out.t = t;
    out.pa = p0 + d1 * s;
    out.pb = q0 + d2 * t;
    out.dist2 = length2(out.pa - out.pb);
    return out;
}

bool RodSystem::skipPair(int a, int b) const {
    if (segChain[a] != segChain[b]) return false;
    int d = std::abs(segIndex[a] - segIndex[b]);
    const RodChain& ch = chains[segChain[a]];
    if (ch.closed) d = std::min(d, ch.segments - d);
    return d <= params.contactSkipSegments;
}

void RodSystem::buildPairs() {
    pairs_.clear();
    const int S = segmentCount();
    if (S < 2) return;
    Real rmax = 0, lmax = 0;
    for (int s = 0; s < S; ++s) {
        rmax = std::max(rmax, radius[s]);
        lmax = std::max(lmax, length(x[segB[s]] - x[segA[s]]));
    }
    rmax *= params.radiusScale;
    if (params.ribbonWidth > 0.0) rmax *= params.ribbonWidth / std::max(params.radius, 1e-12);
    const Real reach = 2.0 * rmax + params.contactSkin * rmax;  // two radii and the skin: the largest separation that can matter
    const Real cell = std::max(lmax + reach, 1e-6);
    const Real inv = 1.0 / cell;
    auto key = [](int ix, int iy, int iz) {
        return ((uint64_t)(ix + (1 << 20)) << 42) | ((uint64_t)(iy + (1 << 20)) << 21) | (uint64_t)(iz + (1 << 20));
    };
    struct Entry { uint64_t k; int s; };
    std::vector<Entry> entries;
    entries.reserve((size_t)S * 4);
    for (int s = 0; s < S; ++s) {
        const Vec3 &p = x[segA[s]], &qq = x[segB[s]];
        const Real pad = 0.5 * reach;
        const int x0 = (int)std::floor((std::min(p.x, qq.x) - pad) * inv), x1 = (int)std::floor((std::max(p.x, qq.x) + pad) * inv);
        const int y0 = (int)std::floor((std::min(p.y, qq.y) - pad) * inv), y1 = (int)std::floor((std::max(p.y, qq.y) + pad) * inv);
        const int z0 = (int)std::floor((std::min(p.z, qq.z) - pad) * inv), z1 = (int)std::floor((std::max(p.z, qq.z) + pad) * inv);
        for (int ix = x0; ix <= x1; ++ix)
            for (int iy = y0; iy <= y1; ++iy)
                for (int iz = z0; iz <= z1; ++iz) entries.push_back({key(ix, iy, iz), s});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.k != b.k ? a.k < b.k : a.s < b.s; });
    std::vector<uint64_t> cand;
    for (size_t i = 0; i < entries.size();) {
        size_t j = i;
        while (j < entries.size() && entries[j].k == entries[i].k) ++j;
        for (size_t a = i; a < j; ++a)
            for (size_t b = a + 1; b < j; ++b)
                if (entries[a].s != entries[b].s) cand.push_back((uint64_t)entries[a].s * (uint64_t)S + (uint64_t)entries[b].s);
        i = j;
    }
    std::sort(cand.begin(), cand.end());
    cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
    for (uint64_t c : cand) {
        const int a = (int)(c / (uint64_t)S), b = (int)(c % (uint64_t)S);
        if (skipPair(a, b)) continue;
        if (!segActive_.empty() && !segActive_[a] && !segActive_[b]) continue;   // both frozen: nothing moves
        Real R = (radius[a] + radius[b]) * params.radiusScale + params.contactSkin * rmax;
        if (params.ribbonWidth > 0.0) R = (radius[a] + radius[b]) / std::max(params.radius, 1e-12) * params.ribbonWidth * params.radiusScale + params.contactSkin * rmax;
        const SegSegResult r = closestSegmentSegment(x[segA[a]], x[segB[a]], x[segA[b]], x[segB[b]]);
        if (r.dist2 < R * R) pairs_.push_back({a, b});
    }
}

void RodSystem::ribbonSection(int seg, Real* a, Real* b, Vec3* thin, Vec3* wide) const {
    const Real f = radius[seg] / std::max(params.radius, 1e-12) * params.radiusScale;
    Real aa = params.ribbonWidth * f, bb = params.ribbonThickness * f;
    const Vec3 d = x[segB[seg]] - x[segA[seg]];
    const Real dl = length(d);
    Vec3 t = dl > 1e-12 ? d / dl : Vec3{0, 0, 1};
    const Vec3 N = normalized(params.ribbonNormal);
    const Real along = std::fabs(dot(N, t));
    Vec3 tn = N - t * dot(N, t);
    if (length2(tn) < 1e-12) tn = cross(t, Vec3{1, 0, 0}) + Vec3{0, 1e-9, 0};
    tn = normalized(tn);
    const Real u = std::min(1.0, std::max(0.0, (along - 0.55) / 0.35));
    const Real w = u * u * (3.0 - 2.0 * u);            // a strand along the normal is round
    const Real req = std::sqrt(aa * bb);
    aa += (req - aa) * w;
    bb += (req - bb) * w;
    if (a) *a = aa;
    if (b) *b = bb;
    if (thin) *thin = tn;
    if (wide) *wide = normalized(cross(t, tn));
}

namespace {
// the distance between the centre lines two ellipse sections (aligned with segment A's frame) need along the unit direction n
Real ellipseNeed(Real aA, Real bA, Real aB, Real bB, const Vec3& thinA, const Vec3& wideA, const Vec3& n) {
    const Real Wa = aA + aB, Wb = bA + bB;
    const Real c = dot(n, thinA), w = dot(n, wideA);
    const Real den = std::sqrt(Wb * Wb * w * w + Wa * Wa * c * c);
    return den > 1e-12 ? Wa * Wb / den : std::max(Wa, Wb);
}
}  // namespace

Real RodSystem::contactGap(int sa, int sb) const {
    const SegSegResult r = closestSegmentSegment(x[segA[sa]], x[segB[sa]], x[segA[sb]], x[segB[sb]]);
    const Real dist = std::sqrt(r.dist2);
    if (params.ribbonWidth <= 0.0) return dist - (radius[sa] + radius[sb]) * params.radiusScale;
    Vec3 n = dist > 1e-12 ? (r.pa - r.pb) / dist : normalized(cross(x[segB[sa]] - x[segA[sa]], x[segB[sb]] - x[segA[sb]]) + Vec3{1e-9, 0, 0});
    Real aA, bA, aB, bB;
    Vec3 thinA, wideA;
    ribbonSection(sa, &aA, &bA, &thinA, &wideA);
    ribbonSection(sb, &aB, &bB, nullptr, nullptr);
    return dist - ellipseNeed(aA, bA, aB, bB, thinA, wideA, n);
}

void RodSystem::solveContacts(Real /*h*/) {
    const Real mu = params.friction;
    int touching = 0;
    for (const Pair& pr : pairs_) {
        const int ia = segA[pr.a], ib = segB[pr.a], ja = segA[pr.b], jb = segB[pr.b];
        const SegSegResult r = closestSegmentSegment(x[ia], x[ib], x[ja], x[jb]);
        Real R = (radius[pr.a] + radius[pr.b]) * params.radiusScale;
        Real aA = 0, bA = 0, aB = 0, bB = 0;
        Vec3 thinA, wideA;
        if (params.ribbonWidth > 0.0) {
            ribbonSection(pr.a, &aA, &bA, &thinA, &wideA);
            ribbonSection(pr.b, &aB, &bB, nullptr, nullptr);
            R = aA + aB;   // the widest the pair can need
        }
        if (r.dist2 >= R * R) continue;
        const Real dist = std::sqrt(r.dist2);
        Vec3 n;
        if (dist > 1e-12) {
            n = (r.pa - r.pb) / dist;
        } else {  // the centre lines cross: push along the common normal
            n = cross(x[ib] - x[ia], x[jb] - x[ja]);
            if (length2(n) < 1e-24) n = cross(x[ib] - x[ia], Vec3{0, 1, 0});
            if (length2(n) < 1e-24) n = Vec3{1, 0, 0};
            n = normalized(n);
        }
        if (params.ribbonWidth > 0.0) {
            R = ellipseNeed(aA, bA, aB, bB, thinA, wideA, n);
            if (dist >= R) continue;
        }
        ++touching;
        const Real pen = R - dist;
        const Real s = r.s, t = r.t;
        const Real w1 = (1 - s) * (1 - s) * w[ia] + s * s * w[ib];
        const Real w2 = (1 - t) * (1 - t) * w[ja] + t * t * w[jb];
        if (w1 + w2 <= 0) continue;
        const Real lam = pen / (w1 + w2);
        x[ia] += n * ((1 - s) * w[ia] * lam);
        x[ib] += n * (s * w[ib] * lam);
        x[ja] -= n * ((1 - t) * w[ja] * lam);
        x[jb] -= n * (t * w[jb] * lam);
        if (mu > 0) {  // Coulomb friction on the displacement of the contact points over this substep
            const Vec3 d1 = (x[ia] - xPrev[ia]) * (1 - s) + (x[ib] - xPrev[ib]) * s;
            const Vec3 d2 = (x[ja] - xPrev[ja]) * (1 - t) + (x[jb] - xPrev[jb]) * t;
            const Vec3 rel = d1 - d2;
            const Vec3 tang = rel - n * dot(rel, n);
            const Real tl = length(tang);
            if (tl > 1e-15) {
                const Real cap = mu * pen;
                const Real mag = std::min(tl, cap);
                const Vec3 dir = tang / tl;
                const Real lt = mag / (w1 + w2);
                x[ia] -= dir * ((1 - s) * w[ia] * lt);
                x[ib] -= dir * (s * w[ib] * lt);
                x[ja] += dir * ((1 - t) * w[ja] * lt);
                x[jb] += dir * (t * w[jb] * lt);
            }
        }
    }
    lastContacts = touching;
}

namespace {
StaticCapsule lerpCapsule(const StaticCapsule& a, const StaticCapsule& b, Real t) {
    StaticCapsule c = b;
    c.a = a.a + (b.a - a.a) * t;
    c.b = a.b + (b.b - a.b) * t;
    return c;
}
}  // namespace

// The fixed (or kinematically moved) capsules: the yarn is pushed out of them, and with staticFriction it does not slide along them while it is
// pressed against them by less than mu times the push (Coulomb). `before` is where they were at the start of this substep: what they carry along.
void RodSystem::solveStatics(const std::vector<StaticCapsule>& cur, const std::vector<StaticCapsule>& before) {
    const Real mu = params.staticFriction;
    for (size_t ci = 0; ci < cur.size(); ++ci) {
        const StaticCapsule& cap = cur[ci];
        const StaticCapsule& cap0 = before[ci];
        for (int s = 0; s < segmentCount(); ++s) {
            const int ia = segA[s], ib = segB[s];
            const SegSegResult r = closestSegmentSegment(x[ia], x[ib], cap.a, cap.b);
            const Real R = radius[s] * params.radiusScale * cap.squash + cap.r;
            if (r.dist2 >= R * R) continue;
            const Real dist = std::sqrt(r.dist2);
            Vec3 n = dist > 1e-12 ? (r.pa - r.pb) / dist : normalized(cross(x[ib] - x[ia], Vec3{0, 1, 0}) + Vec3{1e-9, 0, 0});
            const Real pen = R - dist;
            const Real w1 = (1 - r.s) * (1 - r.s) * w[ia] + r.s * r.s * w[ib];
            if (w1 <= 0) continue;
            const Real lam = pen / w1;
            x[ia] += n * ((1 - r.s) * w[ia] * lam);
            x[ib] += n * (r.s * w[ib] * lam);
            if (mu <= 0) continue;
            // friction: the yarn's motion along the surface relative to the capsule's, this substep
            const Vec3 dp = (x[ia] - xPrev[ia]) * (1 - r.s) + (x[ib] - xPrev[ib]) * r.s;
            const Vec3 dc = (cap.a - cap0.a) * (1 - r.t) + (cap.b - cap0.b) * r.t;
            Vec3 rel = dp - dc;
            rel -= n * dot(rel, n);
            const Real tl = length(rel);
            if (tl < 1e-15) continue;
            const Vec3 corr = tl <= mu * pen ? rel * -1.0 : rel * (-mu * pen / tl);
            x[ia] += corr * ((1 - r.s) * w[ia] / w1);
            x[ib] += corr * (r.s * w[ib] / w1);
        }
    }
}

// Every ring: the closest point of the yarn near its segment is moved onto the ring's point (the yarn runs through it).
void RodSystem::solveRings(Real frac) {
    const int S = segmentCount();
    for (SlideRing& r : rings) {
        if (!r.started_) { r.from_ = r.at; r.started_ = true; }
        const Vec3 at = r.from_ + (r.at - r.from_) * frac;
        int best = -1;
        Real bd = 1e300, bs = 0;
        for (int k = -4; k <= 4; ++k) {
            const int sg = r.seg + k;
            if (sg < 0 || sg >= S || segChain[sg] != segChain[std::clamp(r.seg, 0, S - 1)]) continue;
            const Vec3 a = x[segA[sg]], b = x[segB[sg]], ab = b - a;
            const Real t = std::clamp(dot(at - a, ab) / std::max(1e-18, dot(ab, ab)), 0.0, 1.0);
            const Real d = length2(a + ab * t - at);
            if (d < bd) { bd = d; best = sg; bs = t; }
        }
        if (best < 0) continue;
        r.seg = best;
        const int ia = segA[best], ib = segB[best];
        const Vec3 c = x[ia] + (x[ib] - x[ia]) * bs;
        const Vec3 d = at - c;
        const Real wa = (1 - bs) * w[ia], wb = bs * w[ib];
        const Real den = (1 - bs) * wa + bs * wb;
        if (den <= 0) continue;
        x[ia] += d * (wa / den);
        x[ib] += d * (wb / den);
    }
}

// The shape memory: a soft spring of stiffness shapeStiffness pulls every free particle back to its anchor (XPBD, one constraint per particle).
void RodSystem::solveAnchors(Real h2inv) {
    const Real alpha = h2inv / params.shapeStiffness;
    for (size_t i = 0; i < x.size(); ++i) {
        if (w[i] <= 0) continue;
        Vec3 lam{lambdaA_[3 * i], lambdaA_[3 * i + 1], lambdaA_[3 * i + 2]};
        const Vec3 C = x[i] - anchor[i];
        const Vec3 dl = (-C - lam * alpha) / (w[i] + alpha);
        lambdaA_[3 * i] += dl.x;
        lambdaA_[3 * i + 1] += dl.y;
        lambdaA_[3 * i + 2] += dl.z;
        x[i] += dl * w[i];
    }
}

void RodSystem::solveFloor(Real /*h*/) {
    const Real mu = params.friction;
    const Real r = params.radius * params.radiusScale;
    for (size_t i = 0; i < x.size(); ++i) {
        if (w[i] <= 0) continue;
        const Real pen = params.floorY + r - x[i].y;
        if (pen <= 0) continue;
        x[i].y += pen;
        const Vec3 d = x[i] - xPrev[i];
        const Vec3 tang{d.x, 0.0, d.z};
        const Real tl = length(tang);
        if (mu > 0 && tl > 1e-15) {
            const Real mag = std::min(tl, mu * pen);
            x[i] -= tang * (mag / tl);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------- stepping

void RodSystem::step(Real dt) {
    const int n = particleCount(), S = segmentCount();
    if (n == 0) return;
    const int sub = std::max(1, params.substeps);
    const Real h = dt / sub, h2inv = 1.0 / (h * h);
    Real rmax = params.radius;
    for (Real r : radius) rmax = std::max(rmax, r);
    const Real maxMove = params.maxMoveRadii * rmax;
    const Real damp = std::exp(-params.damping * h);
    std::vector<Vec3> pinStart(x);
    lastContacts = 0;

    // the statics move from where they were at the end of the last step to where they are now, a little every substep
    if (staticsPrev_.size() != statics.size()) staticsPrev_ = statics;
    std::vector<StaticCapsule> staticsNow(statics.size()), staticsBefore(statics.size());
    for (int sstep = 0; sstep < sub; ++sstep) {
        // 1. predict
        const Real frac = (Real)(sstep + 1) / sub;
        for (size_t c = 0; c < statics.size(); ++c) {
            staticsNow[c] = lerpCapsule(staticsPrev_[c], statics[c], frac);
            staticsBefore[c] = lerpCapsule(staticsPrev_[c], statics[c], (Real)sstep / sub);
        }
        for (int i = 0; i < n; ++i) {
            xPrev[i] = x[i];
            if (w[i] > 0) {
                v[i] += (params.gravity + extForce[i] * w[i]) * h;
                v[i] *= damp;
                Vec3 d = v[i] * h;
                const Real dl = length(d);
                if (dl > maxMove) d *= maxMove / dl;
                x[i] += d;
            } else {
                x[i] = pinStart[i] + (pinTarget[i] - pinStart[i]) * frac;
            }
        }
        for (int s = 0; s < S; ++s) {
            qPrev[s] = q[s];
            if (wq[s] > 0) {
                omega[s] *= damp;
                q[s] = applyRot(q[s], omega[s] * h);
            }
        }
        // 2. constraints
        if (params.contact) buildPairs();
        std::fill(lambdaS_.begin(), lambdaS_.end(), 0.0);
        std::fill(lambdaB_.begin(), lambdaB_.end(), 0.0);
        std::fill(lambdaA_.begin(), lambdaA_.end(), 0.0);
        for (int it = 0; it < params.iterations; ++it) {
            const bool rev = (it & 1) != 0;  // alternate the sweep direction: information travels both ways along a chain
            for (size_t ci = 0; ci < chains.size(); ++ci) {
                const RodChain& ch = chains[ci];
                if (params.direct && !ch.closed) {
                    solveChainDirect((int)ci, h2inv);
                    continue;
                }
                for (int k = 0; k < ch.segments; ++k) solveStretchShear(ch.firstSegment + (rev ? ch.segments - 1 - k : k), h2inv);
                for (int k = 0; k < ch.joints; ++k) solveBendTwist(ch.firstJoint + (rev ? ch.joints - 1 - k : k), h2inv);
            }
            if (params.shapeStiffness > 0) solveAnchors(h2inv);
            if (params.contact) solveContacts(h);
            if (params.floorOn) solveFloor(h);
            if (!statics.empty()) solveStatics(staticsNow, staticsBefore);
            if (!rings.empty()) solveRings(frac);
        }
        // 3. velocities
        for (int i = 0; i < n; ++i) v[i] = w[i] > 0 ? (x[i] - xPrev[i]) / h : Vec3{};
        for (int s = 0; s < S; ++s) omega[s] = wq[s] > 0 ? rotationVector(q[s] * conj(qPrev[s])) / h : Vec3{};
    }
    staticsPrev_ = statics;
    for (SlideRing& r : rings) r.from_ = r.at;
}

int RodSystem::relax(Real dt, Real tol, int maxSteps) {
    int k = 0;
    for (; k < maxSteps; ++k) {
        step(dt);
        if (kineticSpeedMax() < tol) {
            ++k;
            break;
        }
    }
    return k;
}

// ---------------------------------------------------------------------------------------------------------------- measures

namespace {
// the solid angle of the spherical triangle of three unit vectors (Van Oosterom and Strackee), signed
Real triAngle(const Vec3& a, const Vec3& b, const Vec3& c) {
    return 2.0 * std::atan2(dot(a, cross(b, c)), 1.0 + dot(a, b) + dot(b, c) + dot(c, a));
}
}  // namespace

Real linkingNumber(const std::vector<Vec3>& A, const std::vector<Vec3>& B) {
    const size_t na = A.size(), nb = B.size();
    Real sum = 0;
    for (size_t i = 0; i < na; ++i) {
        const Vec3 &p1 = A[i], &p2 = A[(i + 1) % na];
        for (size_t j = 0; j < nb; ++j) {
            const Vec3 &q1 = B[j], &q2 = B[(j + 1) % nb];
            const Vec3 u11 = normalized(q1 - p1), u12 = normalized(q2 - p1), u21 = normalized(q1 - p2), u22 = normalized(q2 - p2);
            sum += triAngle(u11, u12, u22) + triAngle(u11, u22, u21);
        }
    }
    return sum / (4.0 * kPi);
}

Real minStrandGap(const RodSystem& rs, int skip) {
    const int S = rs.segmentCount();
    Real best = 1e300;
    for (int a = 0; a < S; ++a)
        for (int b = a + 1; b < S; ++b) {
            if (rs.segChain[a] == rs.segChain[b]) {
                int d = std::abs(rs.segIndex[a] - rs.segIndex[b]);
                const RodChain& ch = rs.chains[rs.segChain[a]];
                if (ch.closed) d = std::min(d, ch.segments - d);
                if (d <= skip) continue;
            }
            const SegSegResult r = closestSegmentSegment(rs.x[rs.segA[a]], rs.x[rs.segB[a]], rs.x[rs.segA[b]], rs.x[rs.segB[b]]);
            best = std::min(best, r.dist2);
        }
    return std::sqrt(best);
}

}  // namespace fl
