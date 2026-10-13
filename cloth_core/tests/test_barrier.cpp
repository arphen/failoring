// Barrier contact (K5), stages 1 and 2: primitive distances and their derivatives, barrier functions,
// mollifier, PSD projection, CCD and the exact triangle-triangle test. No solver involved.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>

#include "check.hpp"
#include "failoring/barrier_contact.hpp"

using namespace fl;
using namespace fl::contact;

static std::mt19937 rng(12345);
static Real U(Real a, Real b) { return std::uniform_real_distribution<Real>(a, b)(rng); }
static Vec3 rv(Real s = 1) { return Vec3(U(-s, s), U(-s, s), U(-s, s)); }
static Real& comp(Vec3& v, int c) { return c == 0 ? v.x : (c == 1 ? v.y : v.z); }

// relative error of analytic vs central differences of f (value, gradient) at x
struct FdErr { Real g = 0, H = 0; };
template <class F>
static FdErr fdCheck(const Vec3 x[4], F f) {  // f(const Vec3*, Deriv12&) computes value/grad/Hessian
    Deriv12 D;
    f(x, D);
    Real gmax = 1e-300, Hmax = 1e-300, ge = 0, He = 0;
    for (int i = 0; i < 12; ++i) gmax = std::max(gmax, std::fabs(D.g[i]));
    for (int i = 0; i < 144; ++i) Hmax = std::max(Hmax, std::fabs(D.H[i]));
    Real scale = 0;
    for (int i = 0; i < 4; ++i) scale = std::max(scale, length(x[i] - x[0]));
    const Real eps = 1e-6 * std::max(scale, Real(1e-3));
    for (int k = 0; k < 12; ++k) {
        Vec3 a[4] = {x[0], x[1], x[2], x[3]}, b[4] = {x[0], x[1], x[2], x[3]};
        comp(a[k / 3], k % 3) += eps;
        comp(b[k / 3], k % 3) -= eps;
        Deriv12 Da, Db;
        f(a, Da);
        f(b, Db);
        ge = std::max(ge, std::fabs((Da.v - Db.v) / (2 * eps) - D.g[k]));
        for (int j = 0; j < 12; ++j) He = std::max(He, std::fabs((Da.g[j] - Db.g[j]) / (2 * eps) - D.H[j * 12 + k]));
    }
    return {ge / gmax, He / Hmax};
}

// brute-force squared distances by dense sampling plus local refinement (independent of the case analysis)
static Real bruteDistPT(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    const int N = 120;
    Real best = 1e300, bu = 0, bv = 0;
    for (int i = 0; i <= N; ++i)
        for (int j = 0; i + j <= N; ++j) {
            const Real u = Real(i) / N, v = Real(j) / N;
            const Real d = length2(p - (a + (b - a) * u + (c - a) * v));
            if (d < best) { best = d; bu = u; bv = v; }
        }
    Real h = 1.0 / N;  // pattern search refinement inside the triangle
    for (int it = 0; it < 200; ++it) {
        bool improved = false;
        for (int dir = 0; dir < 6; ++dir) {
            const Real du[6] = {1, -1, 0, 0, 1, -1}, dv[6] = {0, 0, 1, -1, -1, 1};
            const Real u = bu + h * du[dir], v = bv + h * dv[dir];
            if (u < 0 || v < 0 || u + v > 1) continue;
            const Real d = length2(p - (a + (b - a) * u + (c - a) * v));
            if (d < best) { best = d; bu = u; bv = v; improved = true; }
        }
        if (!improved) h *= 0.5;
        if (h < 1e-14) break;
    }
    return best;
}
static Real bruteDistEE(const Vec3& a0, const Vec3& a1, const Vec3& b0, const Vec3& b1) {
    const int N = 200;
    Real best = 1e300, bs = 0, bt = 0;
    for (int i = 0; i <= N; ++i)
        for (int j = 0; j <= N; ++j) {
            const Real s = Real(i) / N, t = Real(j) / N;
            const Real d = length2(a0 + (a1 - a0) * s - b0 - (b1 - b0) * t);
            if (d < best) { best = d; bs = s; bt = t; }
        }
    Real h = 1.0 / N;
    for (int it = 0; it < 200; ++it) {
        bool improved = false;
        for (int dir = 0; dir < 8; ++dir) {
            const Real ds[8] = {1, -1, 0, 0, 1, -1, 1, -1}, dt[8] = {0, 0, 1, -1, 1, -1, -1, 1};
            const Real s = bs + h * ds[dir], t = bt + h * dt[dir];
            if (s < 0 || s > 1 || t < 0 || t > 1) continue;
            const Real d = length2(a0 + (a1 - a0) * s - b0 - (b1 - b0) * t);
            if (d < best) { best = d; bs = s; bt = t; improved = true; }
        }
        if (!improved) h *= 0.5;
        if (h < 1e-14) break;
    }
    return best;
}

int main() {
    // ------------------------------------------------------------------------------------------------
    std::printf("[distance derivatives vs finite differences, all 7 PT and 9 EE cases]\n");
    {
        Real worstG = 0, worstH = 0;
        for (int trial = 0; trial < 40; ++trial) {
            Vec3 x[4] = {rv(), rv(), rv(), rv()};
            for (int t = 0; t < 7; ++t) {
                const FdErr e = fdCheck(x, [&](const Vec3* X, Deriv12& D) { pointTriangleDistance2Deriv(X, static_cast<PTType>(t), D); });
                worstG = std::max(worstG, e.g);
                worstH = std::max(worstH, e.H);
            }
            for (int t = 0; t < 9; ++t) {
                const FdErr e = fdCheck(x, [&](const Vec3* X, Deriv12& D) { edgeEdgeDistance2Deriv(X, static_cast<EEType>(t), D); });
                worstG = std::max(worstG, e.g);
                worstH = std::max(worstH, e.H);
            }
            const FdErr e = fdCheck(x, [&](const Vec3* X, Deriv12& D) { edgeEdgeCross2Deriv(X, D); });
            worstG = std::max(worstG, e.g);
            worstH = std::max(worstH, e.H);
        }
        // the value returned by the derivative routine equals the closed-form distance of the case
        Real valErr = 0;
        for (int trial = 0; trial < 200; ++trial) {
            Vec3 x[4] = {rv(), rv(), rv(), rv()};
            for (int t = 0; t < 7; ++t) {
                Deriv12 D;
                pointTriangleDistance2Deriv(x, static_cast<PTType>(t), D);
                valErr = std::max(valErr, std::fabs(D.v - pointTriangleDistance2(x[0], x[1], x[2], x[3], static_cast<PTType>(t))));
            }
            for (int t = 0; t < 9; ++t) {
                Deriv12 D;
                edgeEdgeDistance2Deriv(x, static_cast<EEType>(t), D);
                valErr = std::max(valErr, std::fabs(D.v - edgeEdgeDistance2(x[0], x[1], x[2], x[3], static_cast<EEType>(t))));
            }
        }
        std::printf("  40 random configurations x 17 functions: max relative error gradient %.2e, Hessian %.2e; value mismatch %.1e\n",
                    worstG, worstH, valErr);
        CHECK_MSG(worstG < 1e-5 && worstH < 1e-5, "derivative mismatch g %.2e H %.2e", worstG, worstH);
        CHECK(valErr < 1e-12);
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("[distances vs brute-force sampling, random and degenerate configurations]\n");
    {
        Real worstPT = 0, worstEE = 0, negPT = 0, negEE = 0;
        int nPT = 0, nEE = 0;
        int typeCount[16] = {0};
        auto checkPT = [&](const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
            const Real ex = pointTriangleDistance2(p, a, b, c);
            ++typeCount[static_cast<int>(pointTriangleType(p, a, b, c))];
            const Real br = bruteDistPT(p, a, b, c);
            const Real sc = std::max({length(b - a), length(c - a), length(p - a), Real(1e-9)});
            worstPT = std::max(worstPT, std::fabs(std::sqrt(br) - std::sqrt(ex)) / sc);
            negPT = std::max(negPT, (std::sqrt(ex) - std::sqrt(br)) / sc);  // exact must not exceed a sampled distance
            ++nPT;
        };
        auto checkEE = [&](const Vec3& a0, const Vec3& a1, const Vec3& b0, const Vec3& b1) {
            const Real ex = edgeEdgeDistance2(a0, a1, b0, b1);
            ++typeCount[7 + static_cast<int>(edgeEdgeType(a0, a1, b0, b1))];
            const Real br = bruteDistEE(a0, a1, b0, b1);
            const Real sc = std::max({length(a1 - a0), length(b1 - b0), length(b0 - a0), Real(1e-9)});
            worstEE = std::max(worstEE, std::fabs(std::sqrt(br) - std::sqrt(ex)) / sc);
            negEE = std::max(negEE, (std::sqrt(ex) - std::sqrt(br)) / sc);
            ++nEE;
        };
        for (int trial = 0; trial < 400; ++trial) {
            checkPT(rv(), rv(), rv(), rv());
            checkEE(rv(), rv(), rv(), rv());
        }
        // degenerate / adversarial configurations
        for (int trial = 0; trial < 60; ++trial) {
            const Vec3 a = rv(), b = rv(), c = rv();
            const Real u = U(0, 1), v = U(0, 1 - u);
            const Vec3 n = normalized(cross(b - a, c - a));
            checkPT(a + (b - a) * u + (c - a) * v, a, b, c);                    // point in the triangle (d = 0)
            checkPT(a + (b - a) * u + (c - a) * v + n * 1e-9, a, b, c);         // just above
            checkPT(a + (b - a) * U(0, 1), a, b, c);                            // on an edge
            checkPT(a, a + rv(1e-7), b, c);                                     // at a vertex of a tiny edge
            checkPT(rv(), a, b, a + (b - a) * 0.5 + rv(1e-9));                  // needle triangle
            const Vec3 d = rv();
            const Vec3 off = normalized(cross(d, rv())) * U(0, 0.3);
            const Real s0 = U(-1, 1), s1 = U(-1, 1);
            checkEE(a, a + d, a + off + d * s0, a + off + d * (s0 + s1));     // exactly parallel
            checkEE(a, a + d, a + d * s0, a + d * (s0 + s1));                 // collinear (overlapping or not)
            const Vec3 rot = d + cross(d, rv()) * 1e-8;
            checkEE(a, a + d, a + off + rot * s0, a + off + rot * (s0 + 1));  // nearly parallel
            checkEE(a, b, a + (b - a) * U(0, 1), c);                          // touching at a point of edge A
            checkEE(a, b, a, c);                                              // shared endpoint
        }
        std::printf("  %d PT and %d EE configurations; |exact - sampled| / size: PT %.1e, EE %.1e; exact above sampled by at most %.1e / %.1e\n",
                    nPT, nEE, worstPT, worstEE, negPT, negEE);
        std::printf("  feature cases hit: PT");
        for (int i = 0; i < 7; ++i) std::printf(" %d", typeCount[i]);
        std::printf(" | EE");
        for (int i = 7; i < 16; ++i) std::printf(" %d", typeCount[i]);
        std::printf("\n");
        CHECK_MSG(worstPT < 1e-7 && worstEE < 1e-7, "distance differs from brute force: PT %.2e EE %.2e", worstPT, worstEE);
        CHECK_MSG(negPT < 1e-9 && negEE < 1e-9, "exact distance larger than a sampled one: %.2e %.2e", negPT, negEE);
        for (int i = 0; i < 16; ++i) CHECK_MSG(typeCount[i] > 0, "feature case %d never exercised", i);
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("[barrier functions]\n");
    {
        Real worst = 0;
        for (BarrierKind k : {BarrierKind::Log, BarrierKind::Cubic}) {
            Barrier B;
            B.kind = k;
            B.dhat = 1e-3;
            const Real sh = B.dhat * B.dhat;
            for (Real f : {1e-8, 5e-7, 1e-6, 1e-5, 1e-3, 0.01, 0.1, 0.3, 0.5, 0.9, 0.999}) {
                const Real s = f * sh, h = 1e-6 * s;
                Real b, d1, d2, bp, d1p, d2p, bm, d1m, d2m;
                B.eval(s, b, d1, d2);
                B.eval(s + h, bp, d1p, d2p);
                B.eval(s - h, bm, d1m, d2m);
                const Real e1 = std::fabs((bp - bm) / (2 * h) - d1) / std::max(std::fabs(d1), 1e-300);
                const Real e2 = std::fabs((d1p - d1m) / (2 * h) - d2) / std::max(std::fabs(d2), 1e-300);
                worst = std::max({worst, e1, e2});
                CHECK(b >= 0 && d1 <= 0 && d2 >= 0);  // positive, decreasing, convex in s
            }
            // C2 at s_hat: value, slope and curvature vanish
            Real b, d1, d2;
            B.eval(sh * (1 - 1e-9), b, d1, d2);
            CHECK_MSG(std::fabs(b) < 1e-20 && std::fabs(d1) < 1e-12 && std::fabs(d2) < 1e-5 / sh, "barrier not C2 at d_hat: %g %g %g", b, d1, d2);
            std::printf("  %s: b(0.5 dhat^2)=%.3e, normal stiffness at dhat/2 = %.3e /m^2\n", k == BarrierKind::Log ? "log  " : "cubic",
                        B.value(0.25 * sh), B.normalStiffness(0.5 * B.dhat));
        }
        std::printf("  derivative relative error (FD in s, incl. the quadratic continuation below 1e-3 dhat): %.2e\n", worst);
        CHECK_MSG(worst < 1e-5, "barrier derivative mismatch %.2e", worst);
        Real m, dm, d2m;
        mollifier(0.5, 1.0, m, dm, d2m);
        CHECK(std::fabs(m - 0.75) < 1e-15);
        mollifier(1.0 - 1e-12, 1.0, m, dm, d2m);
        CHECK(std::fabs(m - 1) < 1e-11 && std::fabs(dm) < 1e-11);
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("[pair potential derivatives (barrier o distance, with mollifier) vs finite differences]\n");
    {
        // A tiny mesh: one triangle (0,1,2) and one triangle (3,4,5), all pairs allowed.
        SelfContact sc;
        BarrierParams bp;
        bp.dhat = 0.05;
        std::vector<std::array<int, 3>> tris = {{0, 1, 2}, {3, 4, 5}};
        std::vector<Vec3> rest = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {5, 0, 0}, {6, 0, 0}, {5, 1, 0}};
        std::vector<int> panel = {0, 0, 0, 1, 1, 1};
        std::vector<Real> mass(6, 1.0);
        Real worstG = 0, worstH = 0;
        int tested = 0, mollified = 0;
        for (BarrierKind kind : {BarrierKind::Log, BarrierKind::Cubic}) {
            bp.kind = kind;
            sc.init(6, tris, rest, panel, {}, mass, bp);
            for (int trial = 0; trial < 400 && tested < 120; ++trial) {
                std::vector<Vec3> x(6);
                const bool ee = trial % 2;
                ContactPair c;
                if (!ee) {
                    x[1] = rv(); x[2] = rv(); x[3] = rv();
                    const Vec3 n = normalized(cross(x[2] - x[1], x[3] - x[1]));
                    x[0] = x[1] * 0.3 + x[2] * 0.3 + x[3] * 0.4 + n * U(0.005, 0.045) + rv(0.03);
                    c = {0, {0, 1, 2, 3}, 0, 1};
                } else {
                    x[0] = rv(); x[1] = rv();
                    const bool nearPar = trial % 4 == 1;
                    const Vec3 d = nearPar ? (x[1] - x[0]) + rv(0.02) : rv();
                    const Vec3 o = normalized(cross(x[1] - x[0], d)) * U(0.005, 0.045);
                    x[2] = x[0] * 0.5 + x[1] * 0.5 + o - d * 0.5;
                    x[3] = x[2] + d;
                    c = {1, {0, 1, 2, 3}, 0, 1};
                }
                Deriv12 D;
                if (!sc.pairDerivatives(x, c, 1.0, D, false)) continue;
                // skip configurations sitting on a feature-case boundary (the distance is only C1 there)
                const Vec3 X[4] = {x[c.v[0]], x[c.v[1]], x[c.v[2]], x[c.v[3]]};
                bool stable = true;
                for (int k = 0; k < 12 && stable; ++k) {
                    Vec3 a[4] = {X[0], X[1], X[2], X[3]};
                    comp(a[k / 3], k % 3) += 1e-4;
                    if (c.kind == 0) stable = pointTriangleType(a[0], a[1], a[2], a[3]) == pointTriangleType(X[0], X[1], X[2], X[3]);
                    else stable = edgeEdgeType(a[0], a[1], a[2], a[3]) == edgeEdgeType(X[0], X[1], X[2], X[3]);
                }
                if (!stable) continue;
                if (c.kind == 1 && length2(cross(X[1] - X[0], X[3] - X[2])) < sc.eeEps(0, 1)) ++mollified;
                const FdErr e = fdCheck(X, [&](const Vec3* Y, Deriv12& out) {
                    std::vector<Vec3> y = x;
                    for (int i = 0; i < 4; ++i) y[c.v[i]] = Y[i];
                    if (!sc.pairDerivatives(y, c, 1.0, out, false)) out.clear();
                });
                worstG = std::max(worstG, e.g);
                worstH = std::max(worstH, e.H);
                ++tested;
            }
        }
        std::printf("  %d active pairs (%d with the mollifier active): max relative error gradient %.2e, Hessian %.2e\n", tested, mollified,
                    worstG, worstH);
        CHECK(tested >= 60 && mollified >= 5);
        CHECK_MSG(worstG < 1e-5 && worstH < 1e-5, "pair derivative mismatch g %.2e H %.2e", worstG, worstH);
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("[PSD projection]\n");
    {
        Real worstNeg = 0, worstKeep = 0;
        for (int trial = 0; trial < 50; ++trial) {
            Real A[144], B[144], P[144];
            for (int i = 0; i < 12; ++i)
                for (int j = 0; j <= i; ++j) A[i * 12 + j] = A[j * 12 + i] = U(-1, 1);
            for (int i = 0; i < 144; ++i) P[i] = A[i];
            projectPSD(P, 12);
            for (int t = 0; t < 30; ++t) {  // x^T P x >= 0
                Real v[12], q = 0, n2 = 0;
                for (Real& e : v) { e = U(-1, 1); n2 += e * e; }
                for (int i = 0; i < 12; ++i)
                    for (int j = 0; j < 12; ++j) q += v[i] * P[i * 12 + j] * v[j];
                worstNeg = std::min(worstNeg, q / n2);
            }
            // an SPD matrix is left unchanged
            for (int i = 0; i < 12; ++i)
                for (int j = 0; j < 12; ++j) {
                    Real s = 0;
                    for (int k = 0; k < 12; ++k) s += A[i * 12 + k] * A[j * 12 + k];
                    B[i * 12 + j] = s;
                }
            for (int i = 0; i < 144; ++i) P[i] = B[i];
            projectPSD(P, 12);
            for (int i = 0; i < 144; ++i) worstKeep = std::max(worstKeep, std::fabs(P[i] - B[i]));
        }
        std::printf("  min Rayleigh quotient after projection %.1e, change of an SPD matrix %.1e\n", worstNeg, worstKeep);
        CHECK(worstNeg > -1e-10);
        CHECK(worstKeep < 1e-10);
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("[exact triangle-triangle intersection]\n");
    {
        const Vec3 a0(0, 0, 0), a1(1, 0, 0), a2(0, 1, 0);
        CHECK(trianglesIntersectExact(a0, a1, a2, {0.2, 0.2, -1}, {0.2, 0.2, 1}, {0.3, 0.5, 1}));       // piercing
        CHECK(!trianglesIntersectExact(a0, a1, a2, {0.2, 0.2, 1e-6}, {0.6, 0.2, 1}, {0.3, 0.5, 1}));     // just above
        CHECK(trianglesIntersectExact(a0, a1, a2, {0.2, 0.2, 0}, {0.6, 0.2, 1}, {0.3, 0.5, 1}));         // touching at a vertex
        CHECK(trianglesIntersectExact(a0, a1, a2, {0.5, 0.5, 0}, {2, 2, 0}, {0.5, 2, 0}));               // coplanar, overlapping
        CHECK(trianglesIntersectExact(a0, a1, a2, {0.1, 0.1, 0}, {0.2, 0.1, 0}, {0.1, 0.2, 0}));         // coplanar, contained
        CHECK(!trianglesIntersectExact(a0, a1, a2, {0.6, 0.6, 0}, {2, 2, 0}, {0.6, 2, 0}));              // coplanar, disjoint
        CHECK(trianglesIntersectExact(a0, a1, a2, {0.5, -0.5, -0.5}, {0.5, 0.8, 0.5}, {0.5, -0.5, 0.5}));  // edge crossing
        // random: consistency with a sampled segment-triangle test
        int agree = 0, total = 0;
        for (int t = 0; t < 2000; ++t) {
            const Vec3 b0 = rv(), b1 = rv(), b2 = rv();
            const bool ex = trianglesIntersectExact(a0, a1, a2, b0, b1, b2);
            // floating reference: any of the 6 edges crosses the other triangle (Moller-Trumbore style)
            auto segTri = [](const Vec3& p, const Vec3& q, const Vec3& A, const Vec3& B, const Vec3& C) {
                const Vec3 d = q - p, e1 = B - A, e2 = C - A, h = cross(d, e2);
                const Real det = dot(e1, h);
                if (std::fabs(det) < 1e-14) return false;
                const Real f = 1 / det;
                const Vec3 s = p - A;
                const Real u = f * dot(s, h);
                if (u < 0 || u > 1) return false;
                const Vec3 qq = cross(s, e1);
                const Real v = f * dot(d, qq);
                if (v < 0 || u + v > 1) return false;
                const Real tt = f * dot(e2, qq);
                return tt >= 0 && tt <= 1;
            };
            const Vec3 A[3] = {a0, a1, a2}, B[3] = {b0, b1, b2};
            bool ref = false;
            for (int e = 0; e < 3; ++e)
                ref = ref || segTri(A[e], A[(e + 1) % 3], b0, b1, b2) || segTri(B[e], B[(e + 1) % 3], a0, a1, a2);
            agree += ex == ref;
            ++total;
        }
        std::printf("  fixed cases ok; random: %d / %d agree with a floating-point reference\n", agree, total);
        CHECK(agree == total);
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("[CCD: randomized trajectories vs dense sampling, and adversarial cases]\n");
    {
        CcdParams cp;  // separation 0.1, conservative advancement
        int missed = 0, unsafe = 0, hits = 0, n = 0, capHits = 0;
        long long iterSum = 0;
        Real worstEarly = 0;  // how early (in t) CCD stops relative to the reference contact
        auto runPair = [&](bool pt, const Vec3 a0[4], const Vec3 a1[4]) {
            auto dist = [&](Real t) {
                Vec3 x[4];
                for (int i = 0; i < 4; ++i) x[i] = a0[i] + (a1[i] - a0[i]) * t;
                return std::sqrt(pt ? pointTriangleDistance2(x[0], x[1], x[2], x[3]) : edgeEdgeDistance2(x[0], x[1], x[2], x[3]));
            };
            int it = 0;
            const Real t = pt ? ccdPointTriangle(a0, a1, cp, &it) : ccdEdgeEdge(a0, a1, cp, &it);
            if (it < 0) ++capHits; else iterSum += it;
            // reference: first sample where the distance reaches (numerically) zero or the signed volume flips
            // while the distance is tiny (the primitives pass through each other)
            const int S = 20000;
            Real tc = 2;
            const Real d0 = dist(0);
            Real prevVol = 0;
            for (int k = 0; k <= S; ++k) {
                const Real tk = Real(k) / S;
                Vec3 x[4];
                for (int i = 0; i < 4; ++i) x[i] = a0[i] + (a1[i] - a0[i]) * tk;
                const Real vol = dot(cross(x[1] - x[0], x[2] - x[0]), x[3] - x[0]);
                const Real dk = dist(tk);
                if (dk < 1e-9 * std::max(d0, Real(1e-6)) || (k > 0 && vol * prevVol < 0 && dk < 2e-3)) {
                    // refine: the true contact is at or before tk (distance is Lipschitz in t)
                    tc = tk;
                    break;
                }
                prevVol = vol;
            }
            ++n;
            if (tc <= 1) {
                ++hits;
                if (t > tc + 1e-4) ++missed;
                else worstEarly = std::max(worstEarly, tc - t);
            }
            // safety: the distance never drops to zero on [0, t]
            for (int k = 0; k <= 200; ++k)
                if (dist(t * k / 200.0) <= 0) { ++unsafe; break; }
            if (t > 0 && dist(t) < 0.5 * cp.separation * cp.separation * d0) ++unsafe;
        };
        for (int trial = 0; trial < 1500; ++trial) {
            const bool pt = trial % 2 == 0;
            Vec3 a0[4], a1[4];
            for (int i = 0; i < 4; ++i) a0[i] = rv(0.05);
            if (pt) a0[0] = (a0[1] + a0[2] + a0[3]) / 3.0 + normalized(cross(a0[2] - a0[1], a0[3] - a0[1])) * U(1e-4, 0.01);
            const Vec3 common = rv(0.05);
            for (int i = 0; i < 4; ++i) a1[i] = a0[i] + common + rv(0.01);
            // drive through: move the point (or edge A) across the other primitive in many trials
            if (trial % 3 != 2) {
                const Vec3 target = pt ? (a0[1] + a0[2] + a0[3]) / 3.0 : (a0[2] + a0[3]) * 0.5;
                const Vec3 src = pt ? a0[0] : (a0[0] + a0[1]) * 0.5;
                const Vec3 through = (target - src) * U(1.5, 3.0);
                const int k1 = pt ? 1 : 2;
                for (int i = 0; i < k1; ++i) a1[i] = a0[i] + common + through;
            }
            runPair(pt, a0, a1);
        }
        // adversarial cases
        {
            // 1. edge-edge crossing at high speed: 1 m of motion against a 0.1 mm gap
            Vec3 a0[4] = {{-1, 0, 1e-4}, {1, 0, 1e-4}, {0, -1, 0}, {0, 1, 0}}, a1[4] = {{-1, 0, -1}, {1, 0, -1}, {0, -1, 0}, {0, 1, 0}};
            runPair(false, a0, a1);
            // 2. a vertex hitting a triangle edge exactly
            Vec3 b0[4] = {{0.5, 0, 0.01}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, b1[4] = {{0.5, 0, -0.01}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
            runPair(true, b0, b1);
            // 3. parallel moving edges passing through each other
            Vec3 c0[4] = {{0, 0, 0.002}, {1, 0, 0.002}, {0.3, 0, 0}, {1.3, 0, 0}}, c1[4] = {{0, 0, -0.002}, {1, 0, -0.002}, {0.3, 0, 0}, {1.3, 0, 0}};
            runPair(false, c0, c1);
            // 4. vertex hitting a triangle vertex, and a vertex grazing past an edge very closely without contact
            Vec3 d0[4] = {{0, 0, 0.01}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, d1[4] = {{0, 0, -0.01}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
            runPair(true, d0, d1);
            Vec3 e0[4] = {{0.5, -1e-6, 0.5}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, e1[4] = {{0.5, -1e-6, -0.5}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
            int it = 0;
            const Real tg = ccdPointTriangle(e0, e1, cp, &it);
            std::printf("  grazing pass at 1 um: t = %.3f after %d iterations (conservative: may stop early, never late)\n", tg, it);
            // 5. rotating triangle sweeping through a static point (nonlinear distance in t)
            Vec3 f0[4] = {{0.2, 0.2, 0}, {-1, -1, 0.3}, {2, -1, 0.3}, {-1, 2, 0.3}}, f1[4] = {{0.2, 0.2, 0}, {-1, -1, -0.3}, {2, -1, 0.5}, {-1, 2, -0.1}};
            runPair(true, f0, f1);
        }
        std::printf("  %d trajectories, %d with contact in the reference: missed %d, unsafe %d; latest stop before contact %.3f (in t);"
                    " mean iterations %.1f, iteration caps %d\n",
                    n, hits, missed, unsafe, worstEarly, n ? double(iterSum) / std::max(1, n - capHits) : 0.0, capHits);
        CHECK(hits > 500);
        CHECK_MSG(missed == 0, "CCD missed %d collisions", missed);
        CHECK_MSG(unsafe == 0, "CCD returned %d unsafe times", unsafe);
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("[broad phase: BVH candidates vs all pairs]\n");
    {
        // a crumpled random sheet: every pair within d_hat must appear in the candidate list
        const int G = 14;
        std::vector<Vec3> x, rest;
        std::vector<std::array<int, 3>> tris;
        for (int j = 0; j < G; ++j)
            for (int i = 0; i < G; ++i) {
                rest.push_back(Vec3(i * 0.01, j * 0.01, 0));
                x.push_back(Vec3(0.02 * std::sin(i * 0.9 + j), 0.02 * std::cos(j * 1.3), 0.02 * std::sin(i * 0.4 * j)) + rv(0.004));
            }
        for (int j = 0; j + 1 < G; ++j)
            for (int i = 0; i + 1 < G; ++i) {
                const int a = j * G + i;
                tris.push_back({a, a + 1, a + G + 1});
                tris.push_back({a, a + G + 1, a + G});
            }
        SelfContact sc;
        BarrierParams bp;
        bp.dhat = 0.003;
        sc.init(G * G, tris, rest, std::vector<int>(G * G, 0), {}, std::vector<Real>(G * G, 1.0), bp);
        std::vector<ContactPair> cand;
        auto t0 = std::chrono::steady_clock::now();
        sc.candidates(x, nullptr, 0, cand);
        const double ms = 1e3 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::set<std::pair<int, int>> got[2];
        for (auto& c : cand) got[c.kind].insert({c.a, c.b});
        int missing = 0, within = 0;
        for (int v = 0; v < G * G; ++v)
            for (int t = 0; t < static_cast<int>(tris.size()); ++t) {
                const auto& tr = tris[t];
                if (tr[0] == v || tr[1] == v || tr[2] == v) continue;
                if (pointTriangleDistance2(x[v], x[tr[0]], x[tr[1]], x[tr[2]]) < bp.dhat * bp.dhat) {
                    ++within;
                    if (!got[0].count({v, t})) ++missing;
                }
            }
        const auto& E = sc.edges();
        for (int e = 0; e < static_cast<int>(E.size()); ++e)
            for (int f = e + 1; f < static_cast<int>(E.size()); ++f) {
                if (E[e][0] == E[f][0] || E[e][0] == E[f][1] || E[e][1] == E[f][0] || E[e][1] == E[f][1]) continue;
                if (edgeEdgeDistance2(x[E[e][0]], x[E[e][1]], x[E[f][0]], x[E[f][1]]) < bp.dhat * bp.dhat) {
                    ++within;
                    if (!got[1].count({e, f})) ++missing;
                }
            }
        std::printf("  %zu candidates (%.2f ms), %d pairs within d_hat by brute force, %d missing\n", cand.size(), ms, within, missing);
        CHECK(within > 0);
        CHECK(missing == 0);
    }
    FINISH();
}
