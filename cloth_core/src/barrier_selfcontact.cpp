// Barrier contact, part 3: the self-contact set of one cloth mesh (BVH broad phase, topology and seam exclusions,
// pair potential with derivatives, CCD over a candidate set, exact intersection count for the tests).
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "failoring/barrier_contact.hpp"

namespace fl {
namespace contact {

// ---------------------------------------------------------------------------------------------------------
// BVH
// ---------------------------------------------------------------------------------------------------------
void Bvh::build(const std::vector<AABB>& boxes) {
    boxes_ = &boxes;
    nodes_.clear();
    idx_.resize(boxes.size());
    if (boxes.empty()) return;
    std::vector<Vec3> centres(boxes.size());
    for (size_t i = 0; i < boxes.size(); ++i) {
        idx_[i] = static_cast<int>(i);
        centres[i] = (boxes[i].lo + boxes[i].hi) * 0.5;
    }
    nodes_.reserve(boxes.size());
    nodes_.emplace_back();
    buildRec(0, 0, static_cast<int>(boxes.size()), centres);
}

void Bvh::buildRec(int node, int begin, int end, const std::vector<Vec3>& centres) {
    AABB box, cbox;
    for (int k = begin; k < end; ++k) { box.add((*boxes_)[idx_[k]]); cbox.add(centres[idx_[k]]); }
    nodes_[node].box = box;
    if (end - begin <= 4) {
        nodes_[node].first = begin;
        nodes_[node].count = end - begin;
        return;
    }
    const Vec3 ext = cbox.hi - cbox.lo;
    const int axis = (ext.x >= ext.y && ext.x >= ext.z) ? 0 : (ext.y >= ext.z ? 1 : 2);
    const int mid = (begin + end) / 2;
    auto coord = [&](int id) { const Vec3& c = centres[id]; return axis == 0 ? c.x : (axis == 1 ? c.y : c.z); };
    std::nth_element(idx_.begin() + begin, idx_.begin() + mid, idx_.begin() + end, [&](int a, int b) { return coord(a) < coord(b); });
    const int child = static_cast<int>(nodes_.size());
    nodes_.emplace_back();
    nodes_.emplace_back();
    nodes_[node].first = child;
    nodes_[node].count = 0;
    buildRec(child, begin, mid, centres);
    buildRec(child + 1, mid, end, centres);
}

void Bvh::query(const AABB& q, const std::function<void(int)>& f) const { queryT(q, f); }

// ---------------------------------------------------------------------------------------------------------
// SelfContact
// ---------------------------------------------------------------------------------------------------------
void SelfContact::init(int nVerts, const std::vector<std::array<int, 3>>& tris, const std::vector<Vec3>& restPos,
                       const std::vector<int>& vertexPanel, const std::vector<std::pair<int, int>>& links, const std::vector<Real>& mass,
                       const BarrierParams& p) {
    p_ = p;
    n_ = nVerts;
    tris_ = tris;
    vertexPanel_ = vertexPanel;
    std::set<std::pair<int, int>> es;
    for (const auto& t : tris)
        for (int k = 0; k < 3; ++k) {
            int a = t[k], b = t[(k + 1) % 3];
            if (a > b) std::swap(a, b);
            es.insert({a, b});
        }
    edges_.clear();
    restLen2_.clear();
    for (const auto& e : es) {
        edges_.push_back({e.first, e.second});
        restLen2_.push_back(length2(restPos[e.first] - restPos[e.second]));
    }
    links_.assign(nVerts, {});
    for (const auto& l : links) {
        if (l.first == l.second) continue;
        links_[l.first].push_back(l.second);
        links_[l.second].push_back(l.first);
    }
    for (auto& v : links_) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    }
    meanMass_ = 0;
    for (Real m : mass) meanMass_ += m;
    meanMass_ = mass.empty() ? 1 : meanMass_ / mass.size();
    massRel_.assign(nVerts, 1.0);
    for (int i = 0; i < nVerts && i < static_cast<int>(mass.size()); ++i) massRel_[i] = mass[i] / meanMass_;

    // Pairs of one panel that are already within d_hat in the flat rest state are not contacts (they would push the
    // flat cloth apart); they are excluded for good. On regular meshes with edges longer than d_hat there are none.
    restExcluded_.clear();
    excludedRest_ = 0;
    std::vector<ContactPair> cand;
    restExcluded_.reserve(64);
    candidates(restPos, nullptr, 0, cand);  // uses d_hat inflation internally
    for (const ContactPair& c : cand) {
        if (vertexPanel_[c.v[0]] != vertexPanel_[c.v[3]]) continue;
        const Real dh = pairDhat(c);
        if (distance2(c, restPos) < dh * dh) {
            restExcluded_.insert(key(c.kind, c.a, c.b));
            ++excludedRest_;
        }
    }
}

bool SelfContact::linked(int a, int b) const {
    const auto& l = links_[a];
    return std::binary_search(l.begin(), l.end(), b);
}

bool SelfContact::excludedPT(int v, int t) const {
    const auto& tr = tris_[t];
    for (int k = 0; k < 3; ++k)
        if (tr[k] == v || linked(v, tr[k])) return true;
    return !restExcluded_.empty() && restExcluded_.count(key(0, v, t)) > 0;
}

bool SelfContact::excludedEE(int ea, int eb) const {
    const auto &A = edges_[ea], &B = edges_[eb];
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            if (A[i] == B[j] || linked(A[i], B[j])) return true;
    return !restExcluded_.empty() && restExcluded_.count(key(1, ea, eb)) > 0;
}

Real SelfContact::pairDhat(const ContactPair& c) const {
    if (!p_.layerThickness || p_.panelDhat.empty()) return p_.dhat;
    auto pd = [&](int v) {
        const int pn = vertexPanel_[v];
        return pn >= 0 && pn < static_cast<int>(p_.panelDhat.size()) ? p_.panelDhat[pn] : p_.dhat;
    };
    return 0.5 * (pd(c.v[0]) + pd(c.v[c.kind == 0 ? 1 : 2]));
}

Real SelfContact::pairWeight(const ContactPair& c) const {
    return 0.25 * (massRel_[c.v[0]] + massRel_[c.v[1]] + massRel_[c.v[2]] + massRel_[c.v[3]]);
}

void SelfContact::candidates(const std::vector<Vec3>& x0, const std::vector<Vec3>* x1, Real inflate, std::vector<ContactPair>& out) const {
    out.clear();
    Real r = inflate;
    if (p_.layerThickness)
        for (Real d : p_.panelDhat) r = std::max(r, d);
    r = std::max(r, p_.dhat);
    auto vbox = [&](int v) {
        AABB b;
        b.add(x0[v]);
        if (x1) b.add((*x1)[v]);
        return b;
    };
    std::vector<AABB> vb(n_);
    for (int v = 0; v < n_; ++v) vb[v] = vbox(v);
    // point-triangle
    {
        std::vector<AABB> tb(tris_.size());
        for (size_t t = 0; t < tris_.size(); ++t) {
            AABB b;
            for (int k = 0; k < 3; ++k) b.add(vb[tris_[t][k]]);
            b.inflate(r);
            tb[t] = b;
        }
        Bvh bvh;
        bvh.build(tb);
        for (int v = 0; v < n_; ++v)
            bvh.queryT(vb[v], [&](int t) {
                if (excludedPT(v, t)) return;
                const auto& tr = tris_[t];
                out.push_back({0, {v, tr[0], tr[1], tr[2]}, v, t});
            });
    }
    // edge-edge
    {
        std::vector<AABB> eb(edges_.size());
        for (size_t e = 0; e < edges_.size(); ++e) {
            AABB b = vb[edges_[e][0]];
            b.add(vb[edges_[e][1]]);
            eb[e] = b;
        }
        std::vector<AABB> ebInfl = eb;
        for (auto& b : ebInfl) b.inflate(r);
        Bvh bvh;
        bvh.build(ebInfl);
        for (int e = 0; e < static_cast<int>(edges_.size()); ++e)
            bvh.queryT(eb[e], [&](int f) {
                if (f <= e || excludedEE(e, f)) return;
                out.push_back({1, {edges_[e][0], edges_[e][1], edges_[f][0], edges_[f][1]}, e, f});
            });
    }
}

Real SelfContact::distance2(const ContactPair& c, const std::vector<Vec3>& x) const {
    if (c.kind == 0) return pointTriangleDistance2(x[c.v[0]], x[c.v[1]], x[c.v[2]], x[c.v[3]]);
    return edgeEdgeDistance2(x[c.v[0]], x[c.v[1]], x[c.v[2]], x[c.v[3]]);
}

Real SelfContact::potential(const std::vector<Vec3>& x, const std::vector<ContactPair>& cs, Real kappa, int* active, Real* minDist) const {
    Real E = 0;
    int na = 0;
    Real dmin2 = 1e300;
    Barrier bar;
    bar.kind = p_.kind;
    for (const ContactPair& c : cs) {
        const Real s = distance2(c, x);
        dmin2 = std::min(dmin2, s);
        bar.dhat = pairDhat(c);
        if (s >= bar.dhat * bar.dhat) continue;
        Real m = 1;
        if (c.kind == 1 && p_.mollify) {
            const Vec3 u = x[c.v[1]] - x[c.v[0]], v = x[c.v[3]] - x[c.v[2]];
            const Real cc = length2(cross(u, v));
            Real dm, d2m;
            mollifier(cc, eeEps(c.a, c.b), m, dm, d2m);
        }
        E += kappa * pairWeight(c) * m * bar.value(s);
        ++na;
    }
    if (active) *active = na;
    if (minDist) *minDist = dmin2 < 1e300 ? std::sqrt(dmin2) : 1e30;
    return E;
}

bool SelfContact::pairDerivatives(const std::vector<Vec3>& x, const ContactPair& c, Real kappa, Deriv12& out, bool project) const {
    const Vec3 X[4] = {x[c.v[0]], x[c.v[1]], x[c.v[2]], x[c.v[3]]};
    Barrier bar;
    bar.kind = p_.kind;
    bar.dhat = pairDhat(c);
    Deriv12 D;
    if (c.kind == 0) pointTriangleDistance2Deriv(X, pointTriangleType(X[0], X[1], X[2], X[3]), D);
    else edgeEdgeDistance2Deriv(X, edgeEdgeType(X[0], X[1], X[2], X[3]), D);
    if (D.v >= bar.dhat * bar.dhat) return false;
    Real b, db, d2b;
    bar.eval(D.v, b, db, d2b);
    const Real k = kappa * pairWeight(c);
    Real m = 1, dm = 0, d2m = 0;
    Deriv12 C;
    bool moll = false;
    if (c.kind == 1 && p_.mollify) {
        edgeEdgeCross2Deriv(X, C);
        mollifier(C.v, eeEps(c.a, c.b), m, dm, d2m);
        moll = m < 1;
    }
    out.v = k * m * b;
    for (int i = 0; i < 12; ++i) out.g[i] = k * (m * db * D.g[i] + (moll ? dm * b * C.g[i] : 0));
    for (int i = 0; i < 12; ++i)
        for (int j = 0; j < 12; ++j) {
            Real h = m * (d2b * D.g[i] * D.g[j] + db * D.H[i * 12 + j]);
            if (moll)
                h += d2m * b * C.g[i] * C.g[j] + dm * b * C.H[i * 12 + j] + dm * db * (C.g[i] * D.g[j] + D.g[i] * C.g[j]);
            out.H[i * 12 + j] = k * h;
        }
    if (project) projectPSD(out.H, 12);
    return true;
}

Real SelfContact::maxStep(const std::vector<Vec3>& x0, const std::vector<Vec3>& x1, const std::vector<ContactPair>& cs, int* capHits) const {
    CcdParams cp;
    cp.separation = p_.ccdSeparation;
    cp.maxIterations = p_.ccdMaxIterations;
    Real tmin = 1;
    int caps = 0;
    for (const ContactPair& c : cs) {
        const Vec3 a0[4] = {x0[c.v[0]], x0[c.v[1]], x0[c.v[2]], x0[c.v[3]]};
        const Vec3 a1[4] = {x1[c.v[0]], x1[c.v[1]], x1[c.v[2]], x1[c.v[3]]};
        // cheap reject: the distance cannot shrink below separation*d0 if the relative motion is short
        int it = 0;
        const Real t = c.kind == 0 ? ccdPointTriangle(a0, a1, cp, &it) : ccdEdgeEdge(a0, a1, cp, &it);
        if (it < 0) ++caps;
        tmin = std::min(tmin, t);
    }
    if (capHits) *capHits = caps;
    return tmin;
}

int SelfContact::countIntersections(const std::vector<Vec3>& x) const {
    std::vector<AABB> tb(tris_.size());
    for (size_t t = 0; t < tris_.size(); ++t) {
        AABB b;
        for (int k = 0; k < 3; ++k) b.add(x[tris_[t][k]]);
        tb[t] = b;
    }
    Bvh bvh;
    bvh.build(tb);
    int count = 0;
    for (int t = 0; t < static_cast<int>(tris_.size()); ++t)
        bvh.queryT(tb[t], [&](int u) {
            if (u <= t) return;
            const auto &A = tris_[t], &B = tris_[u];
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    if (A[i] == B[j] || linked(A[i], B[j])) return;
            if (trianglesIntersectExact(x[A[0]], x[A[1]], x[A[2]], x[B[0]], x[B[1]], x[B[2]])) ++count;
        });
    return count;
}

}  // namespace contact
}  // namespace fl
