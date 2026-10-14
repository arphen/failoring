// Body contact by persistent augmented-Lagrangian multipliers (SolverParams::contactAL, idea K2; opus session 1).
//
// For every free vertex i with gap g_i(x) = phi(x_i) - r_i (signed distance minus the contact radius) the constraint is
// g_i >= 0. Each vertex keeps a multiplier lambda_i >= 0, its normal force in N, warm-started across Newton solves and
// substeps. Inside one Newton solve lambda and rho are fixed and the body adds the Powell-Hestenes-Rockafellar term
//
//     Psi_i(g) = -lambda_i g + rho_i/2 g^2      if lambda_i - rho_i g > 0   (in contact)
//              = -lambda_i^2 / (2 rho_i)         otherwise
//
// which is C1, so the line search compares exact energies (no projection of iterates or trial points). Its gradient
// is -(lambda - rho g) n; its Gauss-Newton Hessian rho n n^T (the curvature term -(lambda - rho g) Hess(phi) is
// negative semi-definite on a convex body and is dropped). After a solve: lambda <- max(0, lambda - rho g).
//
// What went wrong in the first version (agent/k2-contact-multipliers: lambda 2e8 N, 54 mm penetration at the first
// contact): rho was 1000 x the vertex's elastic stiffness (1e6-1e7 N/m), so a micrometre of residual penetration after
// an unconverged solve became a 10 N multiplier (the vertex weighs ~1e-4 N), and a large lambda keeps pushing until
// g = lambda/rho, i.e. it reaches out and shoves the cloth. Here rho is scaled by inertia (contactALStiffness * m/h^2,
// ~100 N/m for a cloth vertex at 240 Hz), so the multiplier, not the penalty, carries the load; and lambda is updated
// only after a Newton solve.
#include <algorithm>
#include <cmath>

#include "failoring/cloth.hpp"

namespace fl {

Real ClothSolver::alEnergy() const {
    if (!collider_) return 0;
    const int N = vertexCount();
    Real E = 0;
    for (int i = 0; i < N; ++i) {
        if (w_[i] == 0) continue;
        Vec3 nrm;
        const Real g = collider_->eval(x_[i], nrm) - radius_[i];
        const Real lam = alLambda_[i], rho = alRho_[i];
        E += (lam - rho * g > 0) ? -lam * g + 0.5 * rho * g * g : -lam * lam / (2 * rho);
    }
    return E;
}

void ClothSolver::alAssemble() {
    const int N = vertexCount();
    for (int i = 0; i < N; ++i) {
        if (w_[i] == 0 || !collider_) continue;
        const Real g = contactPhi_[i];
        const Vec3& nrm = contactN_[i];
        const Real lam = alLambda_[i], rho = alRho_[i];
        const Real f = lam - rho * g;  // normal force of the AL term (outward)
        // exact Gauss-Newton model of the term: gradient and normal stiffness only where it is in contact. (A band that kept
        // the stiffness a little beyond the switch made a vertex leaving the body crawl: the model said "stiff spring"
        // where the energy is flat, so each Newton step was ~1/(1+contactALStiffness) of the needed one.)
        if (f > 0) {
            grad_[i] -= nrm * f;
            K_.val[K_.diag[i]].addOuter(nrm, nrm, rho);
        }
    }
}

}  // namespace fl
