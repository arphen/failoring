// K1 compliant skin layer and contact pressure output (see docs/agent_notes/K1_compliant_skin.md).
//
// Model: Winkler foundation on the collider surface. For a cloth vertex i with offset-corrected signed distance
// phi_i = collider(x_i) - radius_i and sink depth delta_i = max(0, -phi_i):
//     E_skin = sum_i 0.5 * K_i * delta_i^2,    K_i = k * s(x_i) * A_i     (k Pa/m, s region scale, A_i tributary area)
//     grad   = -K_i * delta_i * n_i,           Hessian = K_i * n_i n_i^T  (PSD, active set only)
// The hard stop sits at delta_max: the rigid machinery of the solver (lift, motion filter, solveBody) works on
// phi + delta_max instead of phi for vertices that have a skin stiffness.
#include <algorithm>
#include <cmath>

#include "failoring/cloth.hpp"

namespace fl {

std::vector<Real> ClothSolver::vertexAreas() const {
    std::vector<Real> a(x_.size(), 0.0);
    for (const auto& t : tris_) {
        const Real ar = 0.5 * length(cross(x_[t[1]] - x_[t[0]], x_[t[2]] - x_[t[0]])) / 3.0;
        for (int k = 0; k < 3; ++k) a[t[k]] += ar;
    }
    return a;
}

void ClothSolver::updateSkinAreas() { skinArea_ = vertexAreas(); }

// Adds the skin gradient/Hessian to grad_ / K_ (diagonal blocks). Uses contactN_ and skinPhi_ of this Newton iteration.
void ClothSolver::assembleSkin() {
    const int N = vertexCount();
    for (int i = 0; i < N; ++i) {
        if (skinK_[i] <= 0 || skinPhi_[i] >= 0) continue;
        const Real delta = -skinPhi_[i];
        const Vec3& n = contactN_[i];
        grad_[i] += n * (-skinK_[i] * delta);
        K_.val[K_.diag[i]].addOuter(n, n, skinK_[i]);
    }
}

std::vector<Real> ClothSolver::vertexSoftPressure() const {
    std::vector<Real> p(x_.size(), 0.0);
    if (!collider_ || p_.skinStiffness <= 0) return p;
    for (size_t i = 0; i < x_.size(); ++i) {
        Vec3 n;
        const Real phi = collider_->eval(x_[i], n) - radius_[i];
        if (phi >= 0) continue;
        p[i] = p_.skinStiffness * collider_->skinScale(x_[i]) * std::min(-phi, p_.skinMaxDepth);
    }
    return p;
}

std::vector<Real> ClothSolver::vertexRigidPressure() const {
    std::vector<Real> p(x_.size(), 0.0);
    if (rigidForce_.size() != x_.size()) return p;
    const std::vector<Real> a = vertexAreas();
    for (size_t i = 0; i < p.size(); ++i)
        if (a[i] > 1e-14) p[i] = rigidForce_[i] / a[i];
    return p;
}

std::vector<Real> ClothSolver::vertexContactPressure() const {
    std::vector<Real> p = vertexSoftPressure();
    const std::vector<Real> r = vertexRigidPressure();
    for (size_t i = 0; i < p.size(); ++i) p[i] += r[i];
    return p;
}

std::vector<Real> ClothSolver::triangleContactPressure() const {
    const std::vector<Real> p = vertexContactPressure();
    std::vector<Real> t(tris_.size(), 0.0);
    for (size_t k = 0; k < tris_.size(); ++k) t[k] = (p[tris_[k][0]] + p[tris_[k][1]] + p[tris_[k][2]]) / 3.0;
    return t;
}

}  // namespace fl
