// Barrier contact, part 2: continuous collision detection (conservative advancement) and an exact
// triangle-triangle intersection test used by the validation tests.
//
// CCD choice: conservative advancement on the exact primitive distance (the idea of "additive CCD", Li, Kaufman
// and Jiang, Codimensional IPC 2021, re-derived here; the paper's own algorithm details are UNVERIFIED).
// The pair moves linearly, x(t) = x0 + t p. Subtracting the mean displacement (distance is translation invariant)
// leaves relative displacements p_i; for PT the distance can shrink at most at rate
//     L = max|p_point| + max_i |p_tri,i|        (EE: max over edge A + max over edge B)
// because every point of the triangle moves with a convex combination of its vertex displacements. Advancing by
// dt = (1 - s) d(t) / L therefore keeps d >= s d(t) > 0 over the step, and we stop (returning the current t) once
// d has fallen below `separation * d(0)`. The returned time is conservative (no contact in [0, t]); the pair is
// at d >= separation-ish fraction of its start distance there. Failure modes: (1) a pair that starts at d = 0
// returns t = 0 (no progress possible), (2) when the motion is huge compared with the gap and the pair grazes
// past without touching, the number of steps grows like L / (s d0); after `maxIterations` we return the current
// (still safe) t, which only shortens the Newton step. There is no floating-point root finding, so no false
// negatives from round-off in a cubic solve; the only floating-point dependence is the distance evaluation itself.
#include "failoring/barrier_contact.hpp"

#include <cmath>
#include <cstdint>

namespace fl {
namespace contact {

namespace {

template <class DistFn>
Real conservativeAdvance(const Vec3 a0[4], const Vec3 a1[4], const CcdParams& prm, int split, DistFn dist2, int* iters) {
    Vec3 p[4];
    Vec3 mean(0, 0, 0);
    for (int i = 0; i < 4; ++i) { p[i] = a1[i] - a0[i]; mean += p[i]; }
    mean = mean * 0.25;
    for (int i = 0; i < 4; ++i) p[i] -= mean;
    // split = number of points in the first primitive (PT: 1, EE: 2)
    Real m1 = 0, m2 = 0;
    for (int i = 0; i < split; ++i) m1 = std::max(m1, length(p[i]));
    for (int i = split; i < 4; ++i) m2 = std::max(m2, length(p[i]));
    const Real L = m1 + m2;
    if (iters) *iters = 0;
    Vec3 x[4] = {a0[0], a0[1], a0[2], a0[3]};
    const Real d0 = std::sqrt(dist2(x));
    if (L <= 0) return 1;
    if (d0 <= 0) return 0;
    const Real target = prm.separation * d0;
    const Real s = prm.separation;
    Real t = 0, d = d0;
    for (int it = 0; it < prm.maxIterations; ++it) {
        if (iters) *iters = it + 1;
        const Real dt = (1 - s) * d / L;
        if (t + dt >= 1) return 1;
        t += dt;
        for (int i = 0; i < 4; ++i) x[i] = a0[i] + (a1[i] - a0[i]) * t;
        d = std::sqrt(dist2(x));
        if (d < target) return t;
    }
    if (iters) *iters = -1;  // iteration cap: t is still safe, just short
    return t;
}

}  // namespace

Real ccdPointTriangle(const Vec3 a0[4], const Vec3 a1[4], const CcdParams& p, int* iters) {
    return conservativeAdvance(a0, a1, p, 1, [](const Vec3* x) { return pointTriangleDistance2(x[0], x[1], x[2], x[3]); }, iters);
}

Real ccdEdgeEdge(const Vec3 a0[4], const Vec3 a1[4], const CcdParams& p, int* iters) {
    return conservativeAdvance(a0, a1, p, 2, [](const Vec3* x) { return edgeEdgeDistance2(x[0], x[1], x[2], x[3]); }, iters);
}

// ---------------------------------------------------------------------------------------------------------
// Exact triangle-triangle intersection on a fixed-point grid
// ---------------------------------------------------------------------------------------------------------
namespace {

using i64 = std::int64_t;
using i128 = __int128;

struct IP { i64 x, y, z; };

IP snap(const Vec3& v) {
    const Real s = 1073741824.0;  // 2^30 per metre
    return {static_cast<i64>(std::llround(v.x * s)), static_cast<i64>(std::llround(v.y * s)), static_cast<i64>(std::llround(v.z * s))};
}

// sign of det[b - a, c - a, d - a]; coordinates below 2^40 keep every product below 2^127
int orient3d(const IP& a, const IP& b, const IP& c, const IP& d) {
    const i128 bx = b.x - a.x, by = b.y - a.y, bz = b.z - a.z;
    const i128 cx = c.x - a.x, cy = c.y - a.y, cz = c.z - a.z;
    const i128 dx = d.x - a.x, dy = d.y - a.y, dz = d.z - a.z;
    const i128 det = bx * (cy * dz - cz * dy) - by * (cx * dz - cz * dx) + bz * (cx * dy - cy * dx);
    return det > 0 ? 1 : (det < 0 ? -1 : 0);
}

struct P2 { i64 u, v; };
int orient2d(const P2& a, const P2& b, const P2& c) {
    const i128 d = static_cast<i128>(b.u - a.u) * (c.v - a.v) - static_cast<i128>(b.v - a.v) * (c.u - a.u);
    return d > 0 ? 1 : (d < 0 ? -1 : 0);
}
bool onSeg2(const P2& a, const P2& b, const P2& p) {  // p collinear with ab: inside the closed segment?
    return std::min(a.u, b.u) <= p.u && p.u <= std::max(a.u, b.u) && std::min(a.v, b.v) <= p.v && p.v <= std::max(a.v, b.v);
}
bool segSeg2(const P2& a, const P2& b, const P2& c, const P2& d) {
    const int o1 = orient2d(a, b, c), o2 = orient2d(a, b, d), o3 = orient2d(c, d, a), o4 = orient2d(c, d, b);
    if (o1 * o2 < 0 && o3 * o4 < 0) return true;
    if (o1 == 0 && onSeg2(a, b, c)) return true;
    if (o2 == 0 && onSeg2(a, b, d)) return true;
    if (o3 == 0 && onSeg2(c, d, a)) return true;
    if (o4 == 0 && onSeg2(c, d, b)) return true;
    return false;
}
bool pointInTri2(const P2& p, const P2& a, const P2& b, const P2& c) {
    const int o1 = orient2d(a, b, p), o2 = orient2d(b, c, p), o3 = orient2d(c, a, p);
    return (o1 >= 0 && o2 >= 0 && o3 >= 0) || (o1 <= 0 && o2 <= 0 && o3 <= 0);
}

// projection that drops the dominant axis of the plane normal (exact: the normal is computed in integers)
int dropAxis(const IP& a, const IP& b, const IP& c) {
    const i128 ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    i128 nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    if (nx < 0) nx = -nx;
    if (ny < 0) ny = -ny;
    if (nz < 0) nz = -nz;
    if (nx >= ny && nx >= nz) return 0;
    if (ny >= nz) return 1;
    return 2;
}
P2 proj(const IP& p, int axis) {
    if (axis == 0) return {p.y, p.z};
    if (axis == 1) return {p.z, p.x};
    return {p.x, p.y};
}

// closed segment pq vs closed triangle abc (non-degenerate)
bool segTri(const IP& p, const IP& q, const IP& a, const IP& b, const IP& c, int axis) {
    const int o1 = orient3d(a, b, c, p), o2 = orient3d(a, b, c, q);
    if (o1 * o2 > 0) return false;
    if (o1 == 0 && o2 == 0) {  // coplanar: 2D tests
        const P2 P = proj(p, axis), Q = proj(q, axis), A = proj(a, axis), B = proj(b, axis), C = proj(c, axis);
        if (pointInTri2(P, A, B, C) || pointInTri2(Q, A, B, C)) return true;
        return segSeg2(P, Q, A, B) || segSeg2(P, Q, B, C) || segSeg2(P, Q, C, A);
    }
    const int s1 = orient3d(p, q, a, b), s2 = orient3d(p, q, b, c), s3 = orient3d(p, q, c, a);
    return (s1 >= 0 && s2 >= 0 && s3 >= 0) || (s1 <= 0 && s2 <= 0 && s3 <= 0);
}

bool degenerate(const IP& a, const IP& b, const IP& c) {
    const i128 ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    return uy * vz - uz * vy == 0 && uz * vx - ux * vz == 0 && ux * vy - uy * vx == 0;
}

}  // namespace

bool trianglesIntersectExact(const Vec3& a0, const Vec3& a1, const Vec3& a2, const Vec3& b0, const Vec3& b1, const Vec3& b2) {
    const IP A[3] = {snap(a0), snap(a1), snap(a2)}, B[3] = {snap(b0), snap(b1), snap(b2)};
    if (degenerate(A[0], A[1], A[2]) || degenerate(B[0], B[1], B[2])) return false;
    // quick reject: all of B strictly on one side of A's plane (or vice versa)
    const int sb0 = orient3d(A[0], A[1], A[2], B[0]), sb1 = orient3d(A[0], A[1], A[2], B[1]), sb2 = orient3d(A[0], A[1], A[2], B[2]);
    if ((sb0 > 0 && sb1 > 0 && sb2 > 0) || (sb0 < 0 && sb1 < 0 && sb2 < 0)) return false;
    const int sa0 = orient3d(B[0], B[1], B[2], A[0]), sa1 = orient3d(B[0], B[1], B[2], A[1]), sa2 = orient3d(B[0], B[1], B[2], A[2]);
    if ((sa0 > 0 && sa1 > 0 && sa2 > 0) || (sa0 < 0 && sa1 < 0 && sa2 < 0)) return false;
    // The intersection of two triangles is empty or has a point on the boundary of one of them, so testing the
    // six edges against the other triangle is complete (the coplanar case includes containment via endpoints).
    const int axA = dropAxis(A[0], A[1], A[2]), axB = dropAxis(B[0], B[1], B[2]);
    for (int e = 0; e < 3; ++e) {
        if (segTri(A[e], A[(e + 1) % 3], B[0], B[1], B[2], axB)) return true;
        if (segTri(B[e], B[(e + 1) % 3], A[0], A[1], A[2], axA)) return true;
    }
    return false;
}

}  // namespace contact
}  // namespace fl
