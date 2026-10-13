// Barrier contact, part 1: primitive distances (point-triangle, edge-edge) with exact gradients and Hessians,
// the edge-edge mollifier, the barrier functions and the PSD projection. Written for Failoring from the maths in
// the IPC paper (Li et al. 2020); no code was copied from ipc-toolkit or other projects.
//
// Derivatives of a distance case. Every case is the squared length of a difference vector that is affine in the
// coordinates x and in at most two free parameters beta (barycentric / line parameters):
//     r(x, beta) = sum_i (w0_i + sum_k W_ik beta_k) x_i,     s(x) = min_beta |r|^2.
// With beta* the (unconstrained) minimiser, the envelope theorem gives
//     grad_i s = 2 w_i(beta*) r*,
//     Hess s = phi_xx - phi_xb phi_bb^-1 phi_bx,   phi = |r|^2,
// phi_xx(i,j) = 2 w_i w_j I, phi_bb = 2 G (G_kl = t_k . t_l, t_k = sum_i W_ik x_i), phi_xb(i,k) = 2 (w_i t_k + W_ik r*).
// This is exact for point-point, point-line, point-plane and line-line distances (the 7 + 9 feature cases),
// so all cases share one implementation; the finite-difference tests in tests/test_barrier.cpp check it.
#include "failoring/barrier_contact.hpp"

#include <cmath>
#include <cstring>

namespace fl {
namespace contact {

void Deriv12::clear() {
    v = 0;
    std::memset(g, 0, sizeof(g));
    std::memset(H, 0, sizeof(H));
}

namespace {

// Closest feature of point p on segment (a, b): 0 = a, 1 = b, 2 = interior.
int pointSegmentFeature(const Vec3& p, const Vec3& a, const Vec3& b) {
    const Vec3 e = b - a;
    const Real ee = dot(e, e);
    if (ee <= 0) return 0;
    const Real t = dot(p - a, e);
    if (t <= 0) return 0;
    if (t >= ee) return 1;
    return 2;
}

Real pointSegmentDist2(const Vec3& p, const Vec3& a, const Vec3& b) {
    const Vec3 e = b - a;
    const Real ee = dot(e, e);
    Real t = ee > 0 ? dot(p - a, e) / ee : 0;
    t = std::min(Real(1), std::max(Real(0), t));
    return length2(p - (a + e * t));
}

struct AffineCase {
    int nb = 0;          // number of free parameters (0, 1, 2)
    Real w0[4] = {0, 0, 0, 0};
    Real W[4][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
};

// Value, gradient and Hessian of s = min_beta |r(x, beta)|^2 for the 4 points x[0..3].
void affineDistance(const Vec3 x[4], const AffineCase& c, Deriv12& out) {
    out.clear();
    Vec3 r0(0, 0, 0), t[2];
    for (int i = 0; i < 4; ++i) r0 += x[i] * c.w0[i];
    for (int k = 0; k < c.nb; ++k) {
        t[k] = Vec3(0, 0, 0);
        for (int i = 0; i < 4; ++i) t[k] += x[i] * c.W[i][k];
    }
    Real beta[2] = {0, 0};
    Real Gi[2][2] = {{0, 0}, {0, 0}};  // inverse of G
    if (c.nb == 1) {
        const Real G = dot(t[0], t[0]);
        Gi[0][0] = G > 0 ? 1 / G : 0;
        beta[0] = -dot(t[0], r0) * Gi[0][0];
    } else if (c.nb == 2) {
        const Real g00 = dot(t[0], t[0]), g01 = dot(t[0], t[1]), g11 = dot(t[1], t[1]);
        const Real det = g00 * g11 - g01 * g01;
        if (det > 0) {
            Gi[0][0] = g11 / det; Gi[1][1] = g00 / det; Gi[0][1] = Gi[1][0] = -g01 / det;
        }
        const Real b0 = -dot(t[0], r0), b1 = -dot(t[1], r0);
        beta[0] = Gi[0][0] * b0 + Gi[0][1] * b1;
        beta[1] = Gi[1][0] * b0 + Gi[1][1] * b1;
    }
    Vec3 r = r0;
    for (int k = 0; k < c.nb; ++k) r += t[k] * beta[k];
    Real w[4];
    for (int i = 0; i < 4; ++i) w[i] = c.w0[i] + c.W[i][0] * beta[0] + c.W[i][1] * beta[1];
    out.v = dot(r, r);
    for (int i = 0; i < 4; ++i) {
        out.g[3 * i] = 2 * w[i] * r.x; out.g[3 * i + 1] = 2 * w[i] * r.y; out.g[3 * i + 2] = 2 * w[i] * r.z;
    }
    // phi_xx
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            const Real f = 2 * w[i] * w[j];
            for (int a = 0; a < 3; ++a) out.H[(3 * i + a) * 12 + 3 * j + a] += f;
        }
    if (c.nb == 0) return;
    // - phi_xb (2G)^-1 phi_bx
    Vec3 cx[4][2];
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < c.nb; ++k) cx[i][k] = (t[k] * w[i] + r * c.W[i][k]) * 2;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < c.nb; ++k)
                for (int l = 0; l < c.nb; ++l) {
                    const Real f = 0.5 * Gi[k][l];
                    if (f == 0) continue;
                    const Vec3 &u = cx[i][k], &v = cx[j][l];
                    const Real uu[3] = {u.x, u.y, u.z}, vv[3] = {v.x, v.y, v.z};
                    for (int a = 0; a < 3; ++a)
                        for (int b = 0; b < 3; ++b) out.H[(3 * i + a) * 12 + 3 * j + b] -= f * uu[a] * vv[b];
                }
}

AffineCase pointPoint(int i, int j) {
    AffineCase c;
    c.w0[i] = 1; c.w0[j] = -1;
    return c;
}
// point i, line (a, b): r = x_i - x_a - beta (x_b - x_a)
AffineCase pointLine(int i, int a, int b) {
    AffineCase c;
    c.nb = 1;
    c.w0[i] = 1; c.w0[a] = -1;
    c.W[a][0] = 1; c.W[b][0] = -1;
    return c;
}
// point 0, plane (1, 2, 3)
AffineCase pointPlane() {
    AffineCase c;
    c.nb = 2;
    c.w0[0] = 1; c.w0[1] = -1;
    c.W[1][0] = 1; c.W[1][1] = 1;
    c.W[2][0] = -1;
    c.W[3][1] = -1;
    return c;
}
// line (0, 1), line (2, 3)
AffineCase lineLine() {
    AffineCase c;
    c.nb = 2;
    c.w0[0] = 1; c.w0[2] = -1;
    c.W[0][0] = -1; c.W[1][0] = 1;
    c.W[2][1] = 1; c.W[3][1] = -1;
    return c;
}

AffineCase ptCase(PTType t) {
    switch (t) {
        case PTType::P_T0: return pointPoint(0, 1);
        case PTType::P_T1: return pointPoint(0, 2);
        case PTType::P_T2: return pointPoint(0, 3);
        case PTType::P_E0: return pointLine(0, 1, 2);
        case PTType::P_E1: return pointLine(0, 2, 3);
        case PTType::P_E2: return pointLine(0, 3, 1);
        default: return pointPlane();
    }
}
AffineCase eeCase(EEType t) {
    switch (t) {
        case EEType::EA0_EB0: return pointPoint(0, 2);
        case EEType::EA0_EB1: return pointPoint(0, 3);
        case EEType::EA1_EB0: return pointPoint(1, 2);
        case EEType::EA1_EB1: return pointPoint(1, 3);
        case EEType::EA_EB0: return pointLine(2, 0, 1);
        case EEType::EA_EB1: return pointLine(3, 0, 1);
        case EEType::EA0_EB: return pointLine(0, 2, 3);
        case EEType::EA1_EB: return pointLine(1, 2, 3);
        default: return lineLine();
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------
PTType pointTriangleType(const Vec3& p, const Vec3& t0, const Vec3& t1, const Vec3& t2) {
    const Vec3 ab = t1 - t0, ac = t2 - t0;
    const Vec3 n = cross(ab, ac);
    const Real scale = std::max(length2(ab), length2(ac));
    if (length2(n) <= 1e-24 * scale * scale) {
        // degenerate (needle or point) triangle: closest of the three segments
        const Vec3 tt[3] = {t0, t1, t2};
        Real best = 1e300;
        PTType type = PTType::P_T0;
        for (int e = 0; e < 3; ++e) {
            const Vec3 &a = tt[e], &b = tt[(e + 1) % 3];
            const Real d = pointSegmentDist2(p, a, b);
            if (d < best) {
                best = d;
                const int f = pointSegmentFeature(p, a, b);
                if (f == 2) type = static_cast<PTType>(3 + e);
                else type = static_cast<PTType>((e + f) % 3);
            }
        }
        return type;
    }
    // Voronoi regions (Ericson, Real-Time Collision Detection, 5.1.5), written out as feature types
    const Vec3 ap = p - t0;
    const Real d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return PTType::P_T0;
    const Vec3 bp = p - t1;
    const Real d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return PTType::P_T1;
    const Real vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return PTType::P_E0;
    const Vec3 cp = p - t2;
    const Real d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return PTType::P_T2;
    const Real vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return PTType::P_E2;
    const Real va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return PTType::P_E1;
    return PTType::P_T;
}

EEType edgeEdgeType(const Vec3& ea0, const Vec3& ea1, const Vec3& eb0, const Vec3& eb1) {
    // Closest points of two segments (Sunday / Eberly), the clamping decisions mapped to feature cases.
    const Vec3 u = ea1 - ea0, v = eb1 - eb0, w = ea0 - eb0;
    const Real a = dot(u, u), b = dot(u, v), c = dot(v, v), d = dot(u, w), e = dot(v, w);
    const Real D = a * c - b * b;
    Real sN, sD = D, tN, tD = D;
    // 0 = at start, 1 = at end, 2 = interior
    int sF = 2, tF = 2;
    const bool parallel = D <= 1e-20 * a * c || a <= 0 || c <= 0;
    if (parallel) {
        sN = 0; sD = 1; tN = e; tD = c;
        sF = 0;
        if (c <= 0) { tN = 0; tD = 1; }
    } else {
        sN = b * e - c * d;
        tN = a * e - b * d;
        if (sN <= 0) { sN = 0; tN = e; tD = c; sF = 0; }
        else if (sN >= sD) { sN = sD; tN = e + b; tD = c; sF = 1; }
    }
    if (tN <= 0) {
        tN = 0; tF = 0;
        if (-d <= 0) sF = 0;
        else if (-d >= a) sF = 1;
        else sF = 2;
    } else if (tN >= tD) {
        tN = tD; tF = 1;
        if (-d + b <= 0) sF = 0;
        else if (-d + b >= a) sF = 1;
        else sF = 2;
    }
    if (parallel && tF == 2) {
        // ea0 projects inside eb: point-line case ea0-eb, unless another endpoint configuration is closer
        // (overlapping parallel segments have many closest pairs; any endpoint-on-line case gives the distance).
        return EEType::EA0_EB;
    }
    if (sF == 2 && tF == 2) return EEType::EA_EB;
    if (sF == 2) return tF == 0 ? EEType::EA_EB0 : EEType::EA_EB1;
    if (tF == 2) return sF == 0 ? EEType::EA0_EB : EEType::EA1_EB;
    if (sF == 0) return tF == 0 ? EEType::EA0_EB0 : EEType::EA0_EB1;
    return tF == 0 ? EEType::EA1_EB0 : EEType::EA1_EB1;
}

Real pointTriangleDistance2(const Vec3& p, const Vec3& t0, const Vec3& t1, const Vec3& t2, PTType type) {
    switch (type) {
        case PTType::P_T0: return length2(p - t0);
        case PTType::P_T1: return length2(p - t1);
        case PTType::P_T2: return length2(p - t2);
        case PTType::P_E0: { const Vec3 e = t1 - t0; return length2(cross(t0 - p, t1 - p)) / length2(e); }
        case PTType::P_E1: { const Vec3 e = t2 - t1; return length2(cross(t1 - p, t2 - p)) / length2(e); }
        case PTType::P_E2: { const Vec3 e = t0 - t2; return length2(cross(t2 - p, t0 - p)) / length2(e); }
        default: {
            const Vec3 n = cross(t1 - t0, t2 - t0);
            const Real s = dot(p - t0, n);
            return s * s / length2(n);
        }
    }
}

Real edgeEdgeDistance2(const Vec3& ea0, const Vec3& ea1, const Vec3& eb0, const Vec3& eb1, EEType type) {
    auto pl = [](const Vec3& p, const Vec3& a, const Vec3& b) { return length2(cross(a - p, b - p)) / length2(b - a); };
    switch (type) {
        case EEType::EA0_EB0: return length2(ea0 - eb0);
        case EEType::EA0_EB1: return length2(ea0 - eb1);
        case EEType::EA1_EB0: return length2(ea1 - eb0);
        case EEType::EA1_EB1: return length2(ea1 - eb1);
        case EEType::EA_EB0: return pl(eb0, ea0, ea1);
        case EEType::EA_EB1: return pl(eb1, ea0, ea1);
        case EEType::EA0_EB: return pl(ea0, eb0, eb1);
        case EEType::EA1_EB: return pl(ea1, eb0, eb1);
        default: {
            const Vec3 n = cross(ea1 - ea0, eb1 - eb0);
            const Real s = dot(eb0 - ea0, n);
            return s * s / length2(n);
        }
    }
}

void pointTriangleDistance2Deriv(const Vec3 x[4], PTType type, Deriv12& out) { affineDistance(x, ptCase(type), out); }
void edgeEdgeDistance2Deriv(const Vec3 x[4], EEType type, Deriv12& out) { affineDistance(x, eeCase(type), out); }

void edgeEdgeCross2Deriv(const Vec3 x[4], Deriv12& out) {
    // c = |u|^2 |v|^2 - (u.v)^2, u = x1 - x0, v = x3 - x2
    out.clear();
    const Vec3 u = x[1] - x[0], v = x[3] - x[2];
    const Real uu = dot(u, u), vv = dot(v, v), uv = dot(u, v);
    out.v = uu * vv - uv * uv;
    const Vec3 gu = u * (2 * vv) - v * (2 * uv), gv = v * (2 * uu) - u * (2 * uv);
    const Vec3 gp[4] = {-gu, gu, -gv, gv};
    for (int i = 0; i < 4; ++i) { out.g[3 * i] = gp[i].x; out.g[3 * i + 1] = gp[i].y; out.g[3 * i + 2] = gp[i].z; }
    const Real U[3] = {u.x, u.y, u.z}, V[3] = {v.x, v.y, v.z};
    Real Huu[9], Hvv[9], Huv[9];  // Huv(a,b) = d2c / du_a dv_b
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b) {
            const Real id = a == b ? 1 : 0;
            Huu[3 * a + b] = 2 * vv * id - 2 * V[a] * V[b];
            Hvv[3 * a + b] = 2 * uu * id - 2 * U[a] * U[b];
            Huv[3 * a + b] = 4 * U[a] * V[b] - 2 * V[a] * U[b] - 2 * uv * id;
        }
    const Real su[4] = {-1, 1, 0, 0}, sv[4] = {0, 0, -1, 1};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int a = 0; a < 3; ++a)
                for (int b = 0; b < 3; ++b)
                    out.H[(3 * i + a) * 12 + 3 * j + b] = su[i] * su[j] * Huu[3 * a + b] + sv[i] * sv[j] * Hvv[3 * a + b] +
                                                          su[i] * sv[j] * Huv[3 * a + b] + sv[i] * su[j] * Huv[3 * b + a];
}

void mollifier(Real c, Real eps, Real& m, Real& dm, Real& d2m) {
    if (c >= eps || eps <= 0) { m = 1; dm = 0; d2m = 0; return; }
    const Real q = c / eps;
    m = (2 - q) * q;
    dm = (2 - 2 * q) / eps;
    d2m = -2 / (eps * eps);
}

bool projectPSD(Real* H, int n) {
    // cyclic Jacobi eigen-decomposition A = V diag(l) V^T, then rebuild with max(l, 0)
    Real A[144], V[144];
    std::memcpy(A, H, sizeof(Real) * n * n);
    for (int i = 0; i < n * n; ++i) V[i] = 0;
    for (int i = 0; i < n; ++i) V[i * n + i] = 1;
    Real off0 = 0, diag = 0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) (i == j ? diag : off0) += A[i * n + j] * A[i * n + j];
    if (diag + off0 == 0) return false;
    for (int sweep = 0; sweep < 30; ++sweep) {
        Real off = 0;
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j) off += A[i * n + j] * A[i * n + j];
        if (off <= 1e-30 * (diag + off0)) break;
        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q) {
                const Real apq = A[p * n + q];
                if (std::fabs(apq) < 1e-300) continue;
                const Real theta = (A[q * n + q] - A[p * n + p]) / (2 * apq);
                const Real t = (theta >= 0 ? 1 : -1) / (std::fabs(theta) + std::sqrt(theta * theta + 1));
                const Real c = 1 / std::sqrt(t * t + 1), s = t * c;
                for (int k = 0; k < n; ++k) {  // A = J^T A J
                    const Real akp = A[k * n + p], akq = A[k * n + q];
                    A[k * n + p] = c * akp - s * akq;
                    A[k * n + q] = s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k) {
                    const Real apk = A[p * n + k], aqk = A[q * n + k];
                    A[p * n + k] = c * apk - s * aqk;
                    A[q * n + k] = s * apk + c * aqk;
                }
                for (int k = 0; k < n; ++k) {
                    const Real vkp = V[k * n + p], vkq = V[k * n + q];
                    V[k * n + p] = c * vkp - s * vkq;
                    V[k * n + q] = s * vkp + c * vkq;
                }
            }
    }
    bool neg = false;
    Real lam[12];
    for (int i = 0; i < n; ++i) {
        lam[i] = A[i * n + i];
        if (lam[i] < 0) { neg = true; lam[i] = 0; }
    }
    if (!neg) return false;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            Real s = 0;
            for (int k = 0; k < n; ++k) s += V[i * n + k] * lam[k] * V[j * n + k];
            H[i * n + j] = s;
        }
    return true;
}

// ---------------------------------------------------------------------------------------------------------
void Barrier::eval(Real s, Real& b, Real& db, Real& d2b) const {
    const Real sh = dhat * dhat;
    if (s >= sh) { b = db = d2b = 0; return; }
    const Real smin = sMinFrac * sMinFrac * sh;
    auto raw = [&](Real x, Real& v, Real& d1, Real& d2) {
        if (kind == BarrierKind::Log) {
            const Real dd = x - sh, L = std::log(x / sh);
            v = -dd * dd * L;
            d1 = -2 * dd * L - dd * dd / x;
            d2 = -2 * L - 4 * dd / x + dd * dd / (x * x);
        } else {
            // psi(d) = (2/3) (dhat - d)^3 / dhat, written in s = d^2
            const Real d = std::sqrt(x), g = dhat - d;
            const Real pd = -2 * g * g / dhat, pdd = 4 * g / dhat;  // dpsi/dd, d2psi/dd2
            v = (2.0 / 3.0) * g * g * g / dhat;
            d1 = pd / (2 * d);
            d2 = (pdd - pd / d) / (4 * x);
        }
    };
    if (s >= smin) { raw(s, b, db, d2b); return; }
    // quadratic continuation below smin (finite energy for pairs that start, or are pushed, almost touching)
    Real v0, d10, d20;
    raw(smin, v0, d10, d20);
    const Real ds = s - smin;
    b = v0 + d10 * ds + 0.5 * d20 * ds * ds;
    db = d10 + d20 * ds;
    d2b = d20;
}

Real Barrier::normalStiffness(Real d) const {
    Real b, db, d2b;
    eval(d * d, b, db, d2b);
    return 4 * d * d * d2b + 2 * db;
}

}  // namespace contact
}  // namespace fl
