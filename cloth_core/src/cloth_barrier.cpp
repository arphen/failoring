// K5: barrier self contact inside the implicit Newton solve (SolverParams::barrierContact).
//
// The substep minimises  G(x) + B(x)  where B is the sum over point-triangle and edge-edge pairs of
//     kappa * w_k * m_k(x) * b(d_k(x)^2),
// b the clamped log barrier of IPC (Li et al. 2020; zero beyond d_hat, infinite at contact), m_k the edge-edge
// mollifier and w_k a mass weight (barrier_contact.hpp). The barrier gradient and its PSD-projected Hessian go into
// the Newton system; the Hessian pattern grows by the vertex couplings of every pair that has come within 2 d_hat
// (the symbolic Cholesky analysis is redone when it grows). Every position change during the substep (predictor,
// body projection, line-search trial, post-solve body friction) is a linear motion checked by conservative CCD
// (barrier_ccd.cpp), so no motion takes two primitives through each other. Notes and numbers: docs/K5_BARRIER_CONTACT.md.
#include <algorithm>
#include <chrono>
#include <cmath>

#include "failoring/cloth.hpp"

namespace fl {

using contact::ContactPair;

namespace {
inline std::uint64_t coupleKey(int a, int b) {
    if (a > b) std::swap(a, b);
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) << 32) | static_cast<std::uint32_t>(b);
}
double since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
}  // namespace

void ClothSolver::barrierInit(Real h) {
    const int N = vertexCount();
    if (!barrierReady_) {
        std::vector<Vec3> rest(N);
        for (int i = 0; i < N; ++i) rest[i] = Vec3(restUV_[i].x, restUV_[i].y, 0);
        std::vector<std::pair<int, int>> links;
        for (const SeamC& s : seamC_) {
            links.push_back({s.a, s.b0});
            links.push_back({s.a, s.b1});
        }
        barrier_.init(N, tris_, rest, vertexPanel_, links, mass_, p_.barrier);
        bstats_.excludedRestPairs = barrier_.excludedRestPairs();
        barrierReady_ = true;
    }
    // Automatic stiffness: the barrier's normal stiffness at d_hat/2 equals stiffnessScale * (mean vertex mass) / h^2,
    // i.e. it is comparable to the inertia term of one vertex. Adaptive doubling (barrierAdapt) takes it from there.
    if (h > 0 && kappa_ <= 0) {
        contact::Barrier bar;
        bar.kind = p_.barrier.kind;
        bar.dhat = p_.barrier.dhat;
        const Real kn = bar.normalStiffness(0.5 * bar.dhat);
        kappa_ = p_.barrier.stiffness > 0 ? p_.barrier.stiffness : p_.barrier.stiffnessScale * barrier_.meanMass() / (h * h) / kn;
        bstats_.kappaStart = bstats_.kappa = kappa_;
    }
}

Real ClothSolver::barrierEnergy(const std::vector<Vec3>& x, const std::vector<ContactPair>& c, Real* minDist) const {
    return barrier_.potential(x, c, kappa_, nullptr, minDist);
}

void ClothSolver::barrierUpdateSet() {
    auto t0 = std::chrono::steady_clock::now();
    const Real dh = p_.barrier.dhat;
    barrier_.candidates(x_, nullptr, 2 * dh, bCand_);
    ++bstats_.broadPhaseCalls;
    bstats_.broadPhaseSeconds += since(t0);
    // register the Hessian couplings of every pair within 2 d_hat (pattern only grows: no churn when pairs flicker)
    bool grew = false;
    for (const ContactPair& c : bCand_) {
        if (barrier_.distance2(c, x_) >= 4 * dh * dh) continue;
        for (int a = 0; a < 4; ++a)
            for (int b = a + 1; b < 4; ++b)
                if (extraCouple_.insert(coupleKey(c.v[a], c.v[b])).second && K_.find(c.v[a], c.v[b]) < 0) grew = true;
    }
    if (grew) {
        auto t1 = std::chrono::steady_clock::now();
        rebuildPattern();
        ++patternRebuilds_;
        ++bstats_.patternRebuilds;
        bstats_.patternCouplings = static_cast<long long>(extraCouple_.size());
        bstats_.patternSeconds += since(t1);
    }
}

void ClothSolver::barrierAddToSystem() {
    auto t0 = std::chrono::steady_clock::now();
    contact::Deriv12 D;
    int active = 0;
    for (const ContactPair& c : bCand_) {
        if (!barrier_.pairDerivatives(x_, c, kappa_, D, true)) continue;
        ++active;
        for (int a = 0; a < 4; ++a) {
            grad_[c.v[a]] += Vec3(D.g[3 * a], D.g[3 * a + 1], D.g[3 * a + 2]);
            for (int b = 0; b < 4; ++b) {
                const int k = K_.find(c.v[a], c.v[b]);
                if (k < 0) continue;  // cannot happen: barrierUpdateSet registered the coupling
                Mat3& M = K_.val[k];
                for (int r = 0; r < 3; ++r)
                    for (int q = 0; q < 3; ++q) M.m[3 * r + q] += D.H[(3 * a + r) * 12 + 3 * b + q];
            }
        }
    }
    ++bstats_.assembleCalls;
    bstats_.activePairsSum += active;
    ++bstats_.activePairsSamples;
    bstats_.activePairsMax = std::max(bstats_.activePairsMax, active);
    bstats_.assembleSeconds += since(t0);
}

Real ClothSolver::barrierMaxStep(const std::vector<Vec3>& x0, const std::vector<Vec3>& x1, std::vector<ContactPair>& scratch) {
    auto t0 = std::chrono::steady_clock::now();
    barrier_.candidates(x0, &x1, 0, scratch);  // swept boxes, inflated by d_hat
    ++bstats_.broadPhaseCalls;
    auto t1 = std::chrono::steady_clock::now();
    bstats_.broadPhaseSeconds += std::chrono::duration<double>(t1 - t0).count();
    int caps = 0;
    const Real t = barrier_.maxStep(x0, x1, scratch, &caps);
    ++bstats_.ccdCalls;
    bstats_.ccdPairs += static_cast<long long>(scratch.size());
    bstats_.ccdIterationCapHits += caps;
    bstats_.ccdSeconds += since(t1);
    return t;
}

void ClothSolver::barrierAdapt(Real minDist) {
    // IPC-style adaptive stiffness: while the closest pair keeps approaching and is already deep inside the barrier
    // zone, double kappa (capped). The exact IPC update rule was not re-checked against the paper; this is our variant.
    if (!p_.barrier.adaptiveStiffness) { prevMinDist_ = minDist; return; }
    const Real cap = p_.barrier.kappaMaxFactor * bstats_.kappaStart;
    if (minDist < p_.barrier.adaptThreshold * p_.barrier.dhat && minDist < prevMinDist_ && kappa_ < cap) kappa_ = std::min(2 * kappa_, cap);
    prevMinDist_ = minDist;
    bstats_.kappa = kappa_;
}

Real ClothSolver::minSelfDistance(Real range) {
    if (!finalized_) finalize();
    if (!barrierReady_) barrierInit(0);
    std::vector<ContactPair> c;
    barrier_.candidates(x_, nullptr, range, c);
    Real m2 = range * range;
    for (const ContactPair& p : c) m2 = std::min(m2, barrier_.distance2(p, x_));
    return std::sqrt(m2);
}

int ClothSolver::selfIntersections() {
    if (!finalized_) finalize();
    if (!barrierReady_) barrierInit(0);
    return barrier_.countIntersections(x_);
}

}  // namespace fl
