#pragma once
// Mesh-resolution estimation from material properties.
//
// Implements the estimator of Zhang, Wang, Liu, Pei, Xu, Wang,
// "Physics-inspired Estimation of Optimal Cloth Mesh Resolution" (SIGGRAPH 2025):
// wrinkle wavelength scales with (B/K)^(1/4) (Cerda & Mahadevan 2003), which for
// fabrics reduces to
//        c1 * r^6 + c2 * r^4 = B / E,       c1 = 32.08 m^-3, c2 = -1.876e-4 m^-1
// with B the bending rigidity (N*m) and E Young's modulus (N/m^2), and r the
// recommended edge length (m). The constants are the paper's published fit.
#include "failoring/material.hpp"
#include "failoring/math.hpp"

namespace fl {

constexpr Real kSizingC1 = 32.08;
constexpr Real kSizingC2 = -1.876e-4;
constexpr Real kMinCellSize = 0.0025;  // paper's recommended floor for simulation (2.5 mm)
constexpr Real kMaxCellSize = 0.05;    // paper's design-range ceiling (50 mm)

// Optimal edge length (m) for bending rigidity B (N*m) and Young's modulus E (N/m^2),
// clamped to [kMinCellSize, kMaxCellSize].
Real optimalEdgeLength(Real bending, Real youngsModulus);

struct DirectionalResolution {
    Real warp;  // edge length along the warp direction (m)
    Real weft;  // edge length along the weft direction (m)
    Real isotropic() const { return std::min(warp, weft); }
};

// Per-direction recommendation for a fabric (E = tensile stiffness per width / thickness).
DirectionalResolution optimalResolution(const FabricMaterial& m);

}  // namespace fl
