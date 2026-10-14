// Body friction inside the implicit Newton solve (SolverParams::bodyFrictionMode = 2; opus session 1).
//
// The old path corrects positions after the solve with a budget mu * (penetration left after the solve). Newton has
// already projected the vertices onto the body by then, so that budget is ~0 and the cloth slides as if frictionless
// (test_friction: a patch on a 10 degree slope slides 19 cm in 0.5 s instead of sticking).
//
// Here friction is a potential in the variational energy, with the normal force and normal lagged from the previous
// substep (the "lagged" friction of IPC, Li et al. 2020; the smoothing f0 below is theirs):
//     D(x) = sum_i mu_i * lambda_i * f0(|u_i|),   u_i = T_i ((x_i - xPrev_i) - collider motion over the substep)
// lambda_i = normal force on vertex i at the end of the previous substep (rigid hold, plus k*delta in the soft skin),
// T_i = I - n_i n_i^T with the lagged normal. f0 is C1: y^2/eps - y^3/(3 eps^2) + eps/3 below eps, y above;
// eps = frictionSlipVelocity * h (the slip per substep below which the contact counts as sticking). Its gradient
// enters the Newton right-hand side, a positive semi-definite approximation of its Hessian (mu lambda f1(y)/y T) the
// matrix, and D itself the line-search energy, so friction cannot inject energy.
#include <cmath>

#include "failoring/cloth.hpp"

namespace fl {

namespace {
inline Real f0(Real y, Real eps) { return y < eps ? y * y / eps - y * y * y / (3 * eps * eps) + eps / 3 : y; }
inline Real f1(Real y, Real eps) { return y < eps ? 2 * y / eps - y * y / (eps * eps) : Real(1); }
}  // namespace

void ClothSolver::frictionLag(bool skin) {
    const int N = vertexCount();
    if (fricLam_.size() != static_cast<size_t>(N)) { fricLam_.assign(N, 0.0); fricN_.assign(N, Vec3(0, 1, 0)); }
    for (int i = 0; i < N; ++i) {
        Real lam = 0;
        if (alOn_ && alLambda_.size() == static_cast<size_t>(N)) lam += alLambda_[i];  // K2: the smooth multiplier
        else if (rigidForce_.size() == static_cast<size_t>(N) && rigidForce_[i] > 0) lam += rigidForce_[i];
        if (skin && skinK_[i] > 0 && skinPhi_[i] < 0) lam += skinK_[i] * (-skinPhi_[i]);
        fricLam_[i] = (w_[i] != 0 && contactPhi_[i] < 1e29) ? lam * muK_[i] : 0.0;  // mu * lambda (N)
        fricN_[i] = contactN_[i];
    }
}

Vec3 ClothSolver::frictionSlip(int i, Real invS) const {
    Vec3 u = x_[i] - xPrev_[i];
    if (colliderDisp_.size() == x_.size()) u = u - colliderDisp_[i] * invS;  // motion relative to the surface
    const Vec3& n = fricN_[i];
    return u - n * dot(u, n);
}

Real ClothSolver::frictionEnergy(Real h) const {
    if (fricLam_.size() != x_.size() || fricOn_.size() != x_.size()) return 0;
    const Real eps = p_.frictionSlipVelocity * h, invS = Real(1) / std::max(1, p_.substeps);
    Real E = 0;
    for (size_t i = 0; i < x_.size(); ++i)
        if (fricOn_[i]) E += fricLam_[i] * f0(length(frictionSlip(static_cast<int>(i), invS)), eps);
    return E;
}

void ClothSolver::assembleFriction(Real h, bool skin) {
    if (fricLam_.size() != x_.size()) return;
    const Real eps = p_.frictionSlipVelocity * h, invS = Real(1) / std::max(1, p_.substeps);
    fricOn_.assign(x_.size(), 0);
    for (size_t k = 0; k < x_.size(); ++k) {
        if (fricLam_[k] <= 0) continue;
        const int i = static_cast<int>(k);
        // only vertices in contact NOW (held within the band, or inside the soft layer); the same set is used for the
        // line-search energy of this iteration
        const bool touching = alOn_ ? (alLambda_[i] - alRho_[i] * contactPhi_[i] > 0)
                                    : (contactPhi_[i] < p_.contactBand || (skin && skinK_[i] > 0 && skinPhi_[i] < 0));
        if (!touching) continue;
        fricOn_[i] = 1;
        const Vec3 u = frictionSlip(i, invS);
        const Real y = length(u);
        const Real s = y > 1e-14 ? f1(y, eps) / y : 2 / eps;  // f1(y)/y, limit 2/eps at y = 0
        grad_[i] += u * (fricLam_[i] * s);                     // d/dx of mu lambda f0(|u|) = mu lambda f1(y) u / y
        const Vec3& n = fricN_[i];
        Mat3& B = K_.val[K_.diag[i]];
        B.addDiag(fricLam_[i] * s);
        B.addOuter(n, n, -fricLam_[i] * s);
        ++cdiag_.frictionActive;
    }
}

}  // namespace fl
