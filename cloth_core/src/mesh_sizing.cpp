#include "failoring/mesh_sizing.hpp"

#include <algorithm>
#include <cmath>

namespace fl {

Real optimalEdgeLength(Real bending, Real youngsModulus) {
    if (bending <= 0 || youngsModulus <= 0) return kMinCellSize;
    const Real y = bending / youngsModulus;
    // g(r) = c1 r^6 + c2 r^4 is negative for small r (c2 < 0), has its minimum at
    // r* = sqrt(-2 c2 / (3 c1)) and increases beyond it; the physical root lies on that branch.
    const Real rStar = std::sqrt(-2.0 * kSizingC2 / (3.0 * kSizingC1));
    auto g = [&](Real r) { return kSizingC1 * std::pow(r, 6) + kSizingC2 * std::pow(r, 4) - y; };
    Real lo = rStar, hi = 1.0;
    if (g(lo) > 0) return kMinCellSize;  // stiffness ratio below the fitted range: finest allowed
    for (int i = 0; i < 200; ++i) {
        const Real mid = 0.5 * (lo + hi);
        if (g(mid) < 0) lo = mid; else hi = mid;
    }
    return std::min(std::max(0.5 * (lo + hi), kMinCellSize), kMaxCellSize);
}

DirectionalResolution optimalResolution(const FabricMaterial& m) {
    const Real t = std::max(m.thickness, Real(1e-6));
    return {optimalEdgeLength(m.bendWarp, m.tensileWarp / t), optimalEdgeLength(m.bendWeft, m.tensileWeft / t)};
}

}  // namespace fl
