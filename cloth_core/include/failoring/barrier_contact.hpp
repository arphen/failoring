#pragma once
// Barrier (IPC-style) cloth-vs-cloth self contact: point-triangle and edge-edge primitive distances with
// derivatives, a C2 clamped log barrier (and an optional cubic barrier), the edge-edge mollifier, a BVH broad
// phase, conservative-advancement continuous collision detection and an exact (integer) triangle-triangle
// intersection test for validation. Dependency-free C++17; written for Failoring from the papers (see
// docs/agent_notes/K5_barrier_contact.md for the maths, references and what is verified).
//
// Conventions
//  * All distances are SQUARED distances s = d^2 (as in IPC); d_hat is given in metres, s_hat = d_hat^2.
//  * Point-triangle (PT) pairs use the 4 points (p, t0, t1, t2); edge-edge (EE) pairs (ea0, ea1, eb0, eb1).
//    Derivatives are with respect to the 12 coordinates of these 4 points, in that order.
#include <array>
#include <cstdint>
#include <functional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "failoring/math.hpp"

namespace fl {
namespace contact {

// ---------------------------------------------------------------------------------------------------------
// Primitive distances
// ---------------------------------------------------------------------------------------------------------
// PT feature cases: closest feature of the triangle is a vertex, an edge (E0 = t0t1, E1 = t1t2, E2 = t2t0)
// or the interior (P_T, point-plane).
enum class PTType : std::uint8_t { P_T0, P_T1, P_T2, P_E0, P_E1, P_E2, P_T };
// EE feature cases: four vertex-vertex, four vertex-edge, one line-line (interior-interior).
enum class EEType : std::uint8_t { EA0_EB0, EA0_EB1, EA1_EB0, EA1_EB1, EA_EB0, EA_EB1, EA0_EB, EA1_EB, EA_EB };

PTType pointTriangleType(const Vec3& p, const Vec3& t0, const Vec3& t1, const Vec3& t2);
EEType edgeEdgeType(const Vec3& ea0, const Vec3& ea1, const Vec3& eb0, const Vec3& eb1);
// Squared distance of a given feature case.
Real pointTriangleDistance2(const Vec3& p, const Vec3& t0, const Vec3& t1, const Vec3& t2, PTType type);
Real edgeEdgeDistance2(const Vec3& ea0, const Vec3& ea1, const Vec3& eb0, const Vec3& eb1, EEType type);
// Exact squared distance between the closed primitives (type chosen automatically).
inline Real pointTriangleDistance2(const Vec3& p, const Vec3& t0, const Vec3& t1, const Vec3& t2) {
    return pointTriangleDistance2(p, t0, t1, t2, pointTriangleType(p, t0, t1, t2));
}
inline Real edgeEdgeDistance2(const Vec3& ea0, const Vec3& ea1, const Vec3& eb0, const Vec3& eb1) {
    return edgeEdgeDistance2(ea0, ea1, eb0, eb1, edgeEdgeType(ea0, ea1, eb0, eb1));
}

// Value, gradient (12) and Hessian (12x12, row-major) of a scalar function of 4 points.
struct Deriv12 {
    Real v = 0;
    Real g[12];
    Real H[144];
    void clear();
};
void pointTriangleDistance2Deriv(const Vec3 x[4], PTType type, Deriv12& out);
void edgeEdgeDistance2Deriv(const Vec3 x[4], EEType type, Deriv12& out);
// c = |(ea1 - ea0) x (eb1 - eb0)|^2, used by the edge-edge mollifier.
void edgeEdgeCross2Deriv(const Vec3 x[4], Deriv12& out);

// IPC edge-edge mollifier m(c) = (2 - c/eps) c/eps for c < eps, else 1; eps = 1e-3 |ea_rest|^2 |eb_rest|^2.
// Returns m, m', m''.
void mollifier(Real c, Real eps, Real& m, Real& dm, Real& d2m);

// Clamps the eigenvalues of a symmetric n x n matrix (row-major) at zero (Jacobi eigen-decomposition).
// Returns true if the matrix was changed.
bool projectPSD(Real* H, int n);

// ---------------------------------------------------------------------------------------------------------
// Barrier
// ---------------------------------------------------------------------------------------------------------
enum class BarrierKind {
    Log,    // IPC: b(s) = -(s - s_hat)^2 ln(s / s_hat) for s < s_hat, 0 beyond (C2 at s_hat, infinite at 0)
    Cubic   // cubic (finite at 0, relies on CCD): psi(d) = -(2/3)/d_hat (d - d_hat)^3, d = sqrt(s). Form UNVERIFIED
};
struct Barrier {
    BarrierKind kind = BarrierKind::Log;
    Real dhat = 1e-3;
    // Below sMinFrac^2 * s_hat the barrier continues as its 2nd-order Taylor polynomial at that point, so
    // a pair that starts (or is pushed by the body projection) to near-zero distance has a finite energy.
    Real sMinFrac = 1e-3;
    // b, b' and b'' with respect to s (squared distance).
    void eval(Real s, Real& b, Real& db, Real& d2b) const;
    Real value(Real s) const { Real b, d1, d2; eval(s, b, d1, d2); return b; }
    // Normal stiffness d^2 b(d^2)/dd^2 = 4 s b'' + 2 b' at distance d.
    Real normalStiffness(Real d) const;
};

// ---------------------------------------------------------------------------------------------------------
// Broad phase
// ---------------------------------------------------------------------------------------------------------
struct AABB {
    Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    void add(const Vec3& p) {
        lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
        hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
    }
    void add(const AABB& b) { add(b.lo); add(b.hi); }
    void inflate(Real r) { lo -= Vec3(r, r, r); hi += Vec3(r, r, r); }
    bool overlaps(const AABB& b) const {
        return lo.x <= b.hi.x && b.lo.x <= hi.x && lo.y <= b.hi.y && b.lo.y <= hi.y && lo.z <= b.hi.z && b.lo.z <= hi.z;
    }
};

// Static AABB tree (median split on the longest axis, leaves of up to 4 boxes). Rebuilt per query batch.
class Bvh {
public:
    void build(const std::vector<AABB>& boxes);
    void query(const AABB& q, const std::function<void(int)>& f) const;
    template <class F>
    void queryT(const AABB& q, F&& f) const {
        if (nodes_.empty()) return;
        int stack[128];
        int sp = 0;
        stack[sp++] = 0;
        while (sp) {
            const Node& n = nodes_[stack[--sp]];
            if (!n.box.overlaps(q)) continue;
            if (n.count > 0) {
                for (int k = 0; k < n.count; ++k) {
                    const int id = idx_[n.first + k];
                    if ((*boxes_)[id].overlaps(q)) f(id);
                }
            } else {
                stack[sp++] = n.first;
                stack[sp++] = n.first + 1;
            }
        }
    }

private:
    struct Node { AABB box; int first = 0, count = 0; };  // count > 0: leaf over idx_[first, first+count); else children first, first+1
    std::vector<Node> nodes_;
    std::vector<int> idx_;
    const std::vector<AABB>* boxes_ = nullptr;
    void buildRec(int node, int begin, int end, const std::vector<Vec3>& centres);
};

// ---------------------------------------------------------------------------------------------------------
// Continuous collision detection (conservative advancement, see ccd.cpp)
// ---------------------------------------------------------------------------------------------------------
struct CcdParams {
    Real separation = 0.1;  // stop when the distance has fallen to this fraction of the pair's start distance
    int maxIterations = 1000;
};
// Earliest time t in [0, 1] (conservative: never later than the true one) at which the pair moving linearly from
// a0[] to a1[] reaches `separation * d(0)`. Returns 1 if that does not happen. `iters` (optional) counts steps.
Real ccdPointTriangle(const Vec3 a0[4], const Vec3 a1[4], const CcdParams& p, int* iters = nullptr);
Real ccdEdgeEdge(const Vec3 a0[4], const Vec3 a1[4], const CcdParams& p, int* iters = nullptr);

// Exact intersection test of two closed triangles on coordinates snapped to a 2^-30 m (~0.93 nm) integer grid,
// with 128-bit orientation predicates (exact for |coordinates| < 2^30 m... in practice < 1e3 m). Touching counts.
bool trianglesIntersectExact(const Vec3& a0, const Vec3& a1, const Vec3& a2, const Vec3& b0, const Vec3& b1, const Vec3& b2);

// ---------------------------------------------------------------------------------------------------------
// Self-contact set for one cloth mesh
// ---------------------------------------------------------------------------------------------------------
struct BarrierParams {
    BarrierKind kind = BarrierKind::Log;
    Real dhat = 1e-3;             // m, activation distance (gap thickness) of the barrier
    Real stiffness = 0;           // fixed kappa; 0 = automatic (normal stiffness at d_hat/2 = stiffnessScale * m/h^2)
    Real stiffnessScale = 1.0;    // automatic stiffness, relative to the mean vertex mass / h^2
    bool adaptiveStiffness = true;  // double kappa while the closest pair is below adaptThreshold*d_hat and closing
    Real adaptThreshold = 0.4;
    Real kappaMaxFactor = 1e4;    // cap of the adaptive kappa relative to the automatic start value
    bool mollify = true;          // IPC edge-edge mollifier for (near-)parallel edges
    Real ccdSeparation = 0.1;     // see CcdParams::separation
    int ccdMaxIterations = 1000;
    Real friction = 0;            // lagged smoothed Coulomb friction coefficient on barrier pairs (0 = off)
    Real frictionEps = 1e-4;      // m, tangential slip (per substep) below which friction is smoothed (static)
    bool layerThickness = false;  // use per-panel d_hat (panelDhat), pair d_hat = mean of the two panels
    std::vector<Real> panelDhat;  // per panel, metres (only with layerThickness)
};

struct ContactPair {
    std::uint8_t kind;  // 0 = PT (v = p, t0, t1, t2), 1 = EE (v = ea0, ea1, eb0, eb1)
    int v[4];
    int a, b;           // PT: vertex, triangle; EE: edge, edge
};

// Diagnostics of the barrier contact, accumulated by the solver.
struct BarrierStats {
    long long broadPhaseCalls = 0, ccdCalls = 0, ccdPairs = 0, ccdIterationCapHits = 0;
    double broadPhaseSeconds = 0, ccdSeconds = 0, assembleSeconds = 0, patternSeconds = 0;
    long long patternRebuilds = 0;
    long long activePairsSum = 0, activePairsSamples = 0;
    int activePairsMax = 0;
    long long assembleCalls = 0;
    long long ccdLimitedSteps = 0;     // Newton steps whose length was capped by CCD
    long long trialRejects = 0;        // line-search trials rejected by the CCD check
    long long initialGuessLimited = 0; // substeps whose predictor was cut back by CCD
    double kappa = 0, kappaStart = 0;
    double minDistance = 1e30;         // smallest pair distance seen at an accepted Newton iterate (m)
    long long excludedRestPairs = 0;   // pairs closer than d_hat in the rest (flat) state, ignored
    long long patternCouplings = 0;    // extra vertex couplings in the Hessian pattern
};

class SelfContact {
public:
    // tris: the cloth triangles; restPos: rest (flat pattern) coordinates per vertex embedded in 3D with z = 0;
    // vertexPanel: panel id per vertex (rest-proximity exclusion only within a panel); links: seam-linked vertex
    // pairs (treated like shared vertices: such pairs never collide).
    // mass: vertex masses (per-pair stiffness is scaled by the pair's mean vertex mass relative to the mesh mean).
    void init(int nVerts, const std::vector<std::array<int, 3>>& tris, const std::vector<Vec3>& restPos,
              const std::vector<int>& vertexPanel, const std::vector<std::pair<int, int>>& links, const std::vector<Real>& mass,
              const BarrierParams& p);
    // Per-pair stiffness multiplier (mean vertex mass of the pair / mean vertex mass).
    Real pairWeight(const ContactPair& c) const;
    Real meanMass() const { return meanMass_; }

    const BarrierParams& params() const { return p_; }
    BarrierParams& params() { return p_; }
    int edgeCount() const { return static_cast<int>(edges_.size()); }
    const std::vector<std::array<int, 2>>& edges() const { return edges_; }
    long long excludedRestPairs() const { return excludedRest_; }

    // Candidate pairs whose (swept, if x1 != nullptr) bounding boxes come within `inflate` of each other.
    void candidates(const std::vector<Vec3>& x0, const std::vector<Vec3>* x1, Real inflate, std::vector<ContactPair>& out) const;
    // Pair d_hat (constant, or per-panel with layerThickness).
    Real pairDhat(const ContactPair& c) const;
    // Squared distance of a pair at positions x.
    Real distance2(const ContactPair& c, const std::vector<Vec3>& x) const;
    // Barrier potential sum_k kappa * m_k * b_k(s_k) over the pairs within their d_hat (m_k = edge-edge mollifier).
    // `minDist` (optional) receives the smallest pair distance among the candidates (m).
    Real potential(const std::vector<Vec3>& x, const std::vector<ContactPair>& c, Real kappa, int* active = nullptr,
                   Real* minDist = nullptr) const;
    // Value, gradient and PSD-projected Hessian of one pair's potential; false if the pair is inactive (s >= s_hat).
    bool pairDerivatives(const std::vector<Vec3>& x, const ContactPair& c, Real kappa, Deriv12& out, bool project = true) const;
    // Smallest CCD time of impact over the pairs (1 = none).
    Real maxStep(const std::vector<Vec3>& x0, const std::vector<Vec3>& x1, const std::vector<ContactPair>& c, int* capHits = nullptr) const;
    // Edge-edge mollifier threshold of a pair.
    Real eeEps(int ea, int eb) const { return 1e-3 * restLen2_[ea] * restLen2_[eb]; }
    // Number of intersecting (exact test) non-adjacent, non-seam-linked triangle pairs in state x.
    int countIntersections(const std::vector<Vec3>& x) const;
    bool linked(int a, int b) const;

private:
    BarrierParams p_;
    int n_ = 0;
    std::vector<std::array<int, 3>> tris_;
    std::vector<std::array<int, 2>> edges_;
    std::vector<Real> restLen2_;
    std::vector<int> vertexPanel_;
    std::vector<Real> massRel_;
    Real meanMass_ = 0;
    std::vector<std::vector<int>> links_;  // sorted
    std::unordered_set<std::uint64_t> restExcluded_;
    long long excludedRest_ = 0;
    bool excludedPT(int v, int t) const;
    bool excludedEE(int ea, int eb) const;
    static std::uint64_t key(int kind, int a, int b) {
        return (static_cast<std::uint64_t>(kind) << 62) | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) << 31) |
               static_cast<std::uint32_t>(b);
    }
};

}  // namespace contact
}  // namespace fl
