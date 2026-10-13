// RegionMap bake and lookup (K1 compliant skin). See skin.hpp.
#include "failoring/skin.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fl {

namespace {
// Squared distance from p to triangle abc (Ericson 5.1.5, distance only).
Real distSqPointTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 ab = b - a, ac = c - a, ap = p - a;
    const Real d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return length2(ap);
    const Vec3 bp = p - b;
    const Real d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return length2(bp);
    const Real vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return length2(p - (a + ab * (d1 / (d1 - d3))));
    const Vec3 cp = p - c;
    const Real d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return length2(cp);
    const Real vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return length2(p - (a + ac * (d2 / (d2 - d6))));
    const Real va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return length2(p - (b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)))));
    const Real denom = 1 / (va + vb + vc);
    return length2(p - (a + ab * (vb * denom) + ac * (vc * denom)));
}
}  // namespace

bool RegionMap::bake(const std::vector<Vec3>& V, const std::vector<std::array<int, 3>>& T,
                     const std::vector<std::uint8_t>& triLabels, Real cell, Real reach) {
    lab_.clear();
    if (V.empty() || T.empty() || triLabels.size() != T.size() || cell <= 0) return false;
    Vec3 lo = V[0], hi = V[0];
    for (const Vec3& p : V) {
        lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
        hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
    }
    h_ = cell;
    inv_ = 1 / cell;
    o_ = lo - Vec3(reach, reach, reach);
    nx_ = static_cast<int>(std::ceil((hi.x - lo.x + 2 * reach) * inv_)) + 1;
    ny_ = static_cast<int>(std::ceil((hi.y - lo.y + 2 * reach) * inv_)) + 1;
    nz_ = static_cast<int>(std::ceil((hi.z - lo.z + 2 * reach) * inv_)) + 1;
    const size_t n = static_cast<size_t>(nx_) * ny_ * nz_;
    lab_.assign(n, kUnknown);
    std::vector<float> best(n, std::numeric_limits<float>::max());
    const Real r2 = reach * reach;
    for (size_t t = 0; t < T.size(); ++t) {
        const Vec3 &a = V[T[t][0]], &b = V[T[t][1]], &c = V[T[t][2]];
        const Vec3 tlo(std::min({a.x, b.x, c.x}) - reach, std::min({a.y, b.y, c.y}) - reach, std::min({a.z, b.z, c.z}) - reach);
        const Vec3 thi(std::max({a.x, b.x, c.x}) + reach, std::max({a.y, b.y, c.y}) + reach, std::max({a.z, b.z, c.z}) + reach);
        const int x0 = std::max(0, static_cast<int>(std::floor((tlo.x - o_.x) * inv_))), x1 = std::min(nx_ - 1, static_cast<int>(std::floor((thi.x - o_.x) * inv_)));
        const int y0 = std::max(0, static_cast<int>(std::floor((tlo.y - o_.y) * inv_))), y1 = std::min(ny_ - 1, static_cast<int>(std::floor((thi.y - o_.y) * inv_)));
        const int z0 = std::max(0, static_cast<int>(std::floor((tlo.z - o_.z) * inv_))), z1 = std::min(nz_ - 1, static_cast<int>(std::floor((thi.z - o_.z) * inv_)));
        for (int k = z0; k <= z1; ++k)
            for (int j = y0; j <= y1; ++j)
                for (int i = x0; i <= x1; ++i) {
                    const Vec3 ctr(o_.x + (i + 0.5) * h_, o_.y + (j + 0.5) * h_, o_.z + (k + 0.5) * h_);
                    const Real d2 = distSqPointTriangle(ctr, a, b, c);
                    const size_t id = (static_cast<size_t>(k) * ny_ + j) * nx_ + i;
                    if (d2 < r2 && d2 < best[id]) { best[id] = static_cast<float>(d2); lab_[id] = triLabels[t]; }
                }
    }
    return true;
}

std::uint8_t RegionMap::label(const Vec3& p) const {
    if (lab_.empty()) return kUnknown;
    const int i = static_cast<int>(std::floor((p.x - o_.x) * inv_));
    const int j = static_cast<int>(std::floor((p.y - o_.y) * inv_));
    const int k = static_cast<int>(std::floor((p.z - o_.z) * inv_));
    if (i < 0 || j < 0 || k < 0 || i >= nx_ || j >= ny_ || k >= nz_) return kUnknown;
    return lab_[(static_cast<size_t>(k) * ny_ + j) * nx_ + i];
}

}  // namespace fl
