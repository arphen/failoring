// Body contact: diagnostics for the projection path, and the augmented-Lagrangian (AL) contact path
// (SolverParams::contactAL, idea K2; docs/handoff/K2_PROGRESS.md).
//
// AL contact. For every free vertex i with gap g_i(x) = phi(x_i) - r_i (signed distance minus the contact radius)
// the constraint is g_i >= 0. Each vertex keeps a multiplier lambda_i >= 0 (the normal force, N) that persists across
// Newton iterations and time steps. Inside one Newton solve lambda and rho are fixed and the body adds the
// Powell-Hestenes-Rockafellar term to the variational energy
//
//     Psi_i(g) = -lambda_i g + rho_i/2 g^2      if lambda_i - rho_i g > 0   (active)
//              = -lambda_i^2 / (2 rho_i)         otherwise                   (inactive)
//
// which is C1 in x, so the line search compares exact energies (no projection of trial points). Its gradient is
// -(lambda - rho g) n (the contact force (lambda - rho g) n pushes outward); its Gauss-Newton Hessian is rho n n^T
// (PSD; the curvature term -(lambda - rho g) Hess(phi) is negative semi-definite on a convex body and is dropped).
// After Newton converges the multipliers are updated, lambda <- max(0, lambda - rho g), and Newton is run again
// (at most contactALOuter solves per substep) while some vertex is deeper than contactALTolerance. Because lambda is
// warm-started from the previous substep, a resting garment needs one solve per substep.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "failoring/cloth.hpp"

namespace fl {

// ---------------------------------------------------------------------------------------------------------------
// Diagnostics (both contact modes)

Real ClothSolver::kineticEnergyOf(const std::vector<Vec3>& x, Real h) const {
    Real e = 0;
    for (size_t i = 0; i < x.size(); ++i)
        if (w_[i] != 0) e += 0.5 * mass_[i] * length2((x[i] - xPrev_[i]) / h);
    return e;
}

// contactActive_ state of this iteration vs the previous iteration of the same substep. Bit 0: active, bit 1: within
// contactBand of the body (the filter's distance test passed, so only the sign of the force decided).
void ClothSolver::diagIterationActive(int iteration) {
    const int N = vertexCount();
    if (static_cast<int>(diagPrevActive_.size()) != N) diagPrevActive_.assign(N, 0);
    for (int i = 0; i < N; ++i) {
        const char inBand = (contactPhi_[i] < p_.contactBand) ? 2 : 0;
        const char st = static_cast<char>((contactActive_[i] ? 1 : 0) | inBand);
        if (iteration > 0 && ((st ^ diagPrevActive_[i]) & 1)) {
            ++cstats_.flipsIteration;
            if ((st & 2) && (diagPrevActive_[i] & 2)) ++cstats_.flipsForceSign;
        }
        diagPrevActive_[i] = st;
    }
}

void ClothSolver::diagSubstepEnd() {
    const int N = vertexCount();
    if (static_cast<int>(diagStepActive_.size()) != N) diagStepActive_.assign(N, 0);
    ++cstats_.substeps;
    for (int i = 0; i < N; ++i) {
        const char a = contactActive_[i] ? 1 : 0;
        if (a != diagStepActive_[i]) ++cstats_.flipsStep;
        diagStepActive_[i] = a;
        cstats_.activeEnd += a;
    }
}

// Called with x_ = last rejected trial point, xBackup_ = start of the iteration, dx_ = the Newton direction.
// Classifies the failure: would the unprojected direction have descended (the projection broke it), or is the
// energy change at round-off level for every trial step (converged in practice, tolerance too tight)?
void ClothSolver::diagLineSearchFailure(Real invH2, Real seamScale, Real E0) {
    const int N = vertexCount();
    ++cstats_.lineSearchFailures;
    bool descends = false, flat = true;
    const Real eps = 1e-12 * std::max(std::fabs(E0), Real(1e-30));
    std::vector<Vec3> keep = x_;
    Real step = 1.0;
    for (int ls = 0; ls < 10; ++ls, step *= 0.5) {
        for (int i = 0; i < N; ++i) x_[i] = xBackup_[i] + dx_[i] * step;
        const Real E1 = variationalEnergy(invH2, seamScale);
        if (E1 < E0) descends = true;
        // projected trial energy, as the solver evaluated it
        for (int i = 0; i < N; ++i)
            if (collider_ && w_[i] != 0) {
                Vec3 nrm;
                const Real phi = collider_->eval(x_[i], nrm) - radius_[i];
                if (phi < 0) x_[i] -= nrm * phi;
            }
        const Real E2 = variationalEnergy(invH2, seamScale);
        if (std::fabs(E2 - E0) > eps) flat = false;
    }
    if (descends) ++cstats_.lsFailProjection;
    if (flat) ++cstats_.lsFailFlat;
    if (std::getenv("FL_DEBUG_CONTACT")) {
        Real d2 = 0, rd = 0;
        for (int i = 0; i < N; ++i) { d2 += length2(dx_[i]); rd += dot(rhs_[i], dx_[i]); }
        std::fprintf(stderr, "[ls-fail frame %d] E0=%.9e |dx|=%.2e rhs.dx=%.2e unprojected-descends=%d flat=%d\n", frame_, E0,
                     std::sqrt(d2), rd, descends ? 1 : 0, flat ? 1 : 0);
    }
    x_ = keep;
}

// ---------------------------------------------------------------------------------------------------------------
// Augmented-Lagrangian contact

Real ClothSolver::alContactEnergy() const {
    if (!collider_) return 0;
    const int N = vertexCount();
    Real E = 0;
    for (int i = 0; i < N; ++i) {
        if (w_[i] == 0) continue;
        Vec3 nrm;
        const Real g = collider_->eval(x_[i], nrm) - radius_[i];
        const Real lam = alLambda_[i], rho = alRho_[i];
        if (lam - rho * g > 0) E += -lam * g + 0.5 * rho * g * g;
        else E += -lam * lam / (2 * rho);
    }
    return E;
}

// Adds the AL gradient and Gauss-Newton Hessian at x_ to grad_ / K_ (assembleEnergy must have run). Fills
// contactPhi_ / contactN_ and marks contactActive_ for the diagnostics (vertices with a non-zero contact force).
// The Hessian term rho n n^T is also kept for vertices within contactBand of becoming active (hysteresis in the
// Newton model only; the energy and gradient stay exact), so a vertex grazing the surface does not toggle its
// normal stiffness on and off between iterations.
void ClothSolver::alAssemble() {
    const int N = vertexCount();
    for (int i = 0; i < N; ++i) {
        contactActive_[i] = 0;
        contactPhi_[i] = 1e30;
        if (w_[i] == 0 || !collider_) continue;
        Vec3 nrm;
        const Real g = collider_->eval(x_[i], nrm) - radius_[i];
        contactN_[i] = nrm;
        contactPhi_[i] = g;
        const Real lam = alLambda_[i], rho = alRho_[i];
        const Real f = lam - rho * g;  // normal force of the AL term
        if (f > 0) {
            grad_[i] -= nrm * f;
            contactActive_[i] = 1;
        }
        if (f > -rho * p_.contactBand) K_.val[K_.diag[i]].addOuter(nrm, nrm, rho);
    }
}

void ClothSolver::stepImplicitAL() {
    const int S = std::max(1, p_.substeps);
    const Real h = p_.dt / S;
    const Real invH2 = 1.0 / (h * h);
    const int N = vertexCount();
    const Real seamScale = p_.seamRampFrames > 0 ? std::min(Real(1), std::max(Real(0.02), (frame_ + 1.0) / p_.seamRampFrames)) : Real(1);
    if (static_cast<int>(alLambda_.size()) != N) alLambda_.assign(N, 0);
    if (static_cast<int>(alRho_.size()) != N) alRho_.assign(N, 1.0);
    const bool diag = p_.contactDiagnostics;
    auto totalEnergy = [&]() { return variationalEnergy(invH2, seamScale) + alContactEnergy(); };

    for (int s = 0; s < S; ++s) {
        for (int i = 0; i < N; ++i) {
            xPrev_[i] = x_[i];
            filt_[i] = Mat3();
            if (w_[i] == 0) { v_[i] = Vec3(0, 0, 0); xTilde_[i] = x_[i]; continue; }
            filt_[i].addDiag(1);
            v_[i] *= std::exp(-damp_[i] * h);
            v_[i] += p_.gravity * h;
            xTilde_[i] = x_[i] + v_[i] * h;
            x_[i] = xTilde_[i];
        }
        int cg = 0, nUsed = 0, outer = 0;
        bool rhoSet = false;
        Real maxPen = 0;
        for (; outer < std::max(1, p_.contactALOuter); ++outer) {
            Real prevMaxDx = 1e30;
            bool needFactor = true;
            for (int n = 0; n < std::max(1, p_.newtonIterations); ++n) {
                ++nUsed;
                assembleEnergy(seamScale);
                if (!rhoSet) {  // penalty from the local stiffness, once per substep (lambda does not depend on it)
                    for (int i = 0; i < N; ++i) {
                        const Mat3& D = K_.val[K_.diag[i]];
                        alRho_[i] = p_.contactALStiffness * (mass_[i] * invH2 + (D.m[0] + D.m[4] + D.m[8]) / 3.0);
                    }
                    rhoSet = true;
                }
                alAssemble();
                static bool gcDone = false;
                if (!gcDone && frame_ == 3 && std::getenv("FL_AL_GRADCHECK") && std::any_of(contactActive_.begin(), contactActive_.end(), [](char c) { return c != 0; })) {
                    gcDone = true;
                    // central finite difference of totalEnergy vs grad_ at vertices in contact (diagnostic only)
                    const std::vector<Vec3> g = grad_, keepX = x_;
                    int shown = 0;
                    for (int i = 0; i < N && shown < 6; ++i) {
                        if (w_[i] == 0 || !contactActive_[i]) continue;
                        ++shown;
                        for (int c = 0; c < 3; ++c) {
                            const Real e = 1e-7;
                            Vec3 xp = keepX[i], xm = keepX[i];
                            Vec3 d(c == 0, c == 1, c == 2);
                            xp = xp + d * e; xm = xm - d * e;
                            x_ = keepX; x_[i] = xp; const Real Ep = totalEnergy();
                            x_ = keepX; x_[i] = xm; const Real Em = totalEnergy();
                            x_ = keepX;
                            const Real fd = (Ep - Em) / (2 * e);
                            std::fprintf(stderr, "[gradcheck v%d c%d] grad=%.6e fd=%.6e lam=%.3e phi=%.3e\n", i, c, (c == 0 ? g[i].x : c == 1 ? g[i].y : g[i].z), fd,
                                         alLambda_[i], contactPhi_[i]);
                        }
                    }
                }
                if (diag || n > 0) diagIterationActive(n);
                else diagIterationActive(0);
                for (int i = 0; i < N; ++i)
                    rhs_[i] = w_[i] == 0 ? Vec3(0, 0, 0) : -((x_[i] - xTilde_[i]) * (mass_[i] * invH2) + grad_[i]);
                {
                    int used = -1;
                    if (p_.linearSolver == LinearSolver::CholeskyPCG) used = solveCholPcg(invH2, rhs_, dx_);
                    else if (p_.linearSolver == LinearSolver::Cholesky) { used = solveDirect(invH2, rhs_, dx_, needFactor || !p_.reuseFactorization); needFactor = false; }
                    if (used < 0) used = solveLinear(invH2, rhs_, dx_);
                    cg += used;
                }
                Real maxStep = 0;
                for (int i = 0; i < N; ++i) maxStep = std::max(maxStep, length(dx_[i]));
                if (maxStep < p_.newtonTolerance) break;
                // backtracking line search on the exact (C1) energy: no projection of trial points
                const Real E0 = totalEnergy();
                xBackup_ = x_;
                Real step = 1.0;
                bool accepted = false;
                for (int ls = 0; ls < 10; ++ls) {
                    for (int i = 0; i < N; ++i) x_[i] = xBackup_[i] + dx_[i] * step;
                    const Real E1 = totalEnergy();
                    if (std::isfinite(E1) && E1 <= E0) { accepted = true; break; }
                    step *= 0.5;
                }
                if (!accepted) {
                    x_ = xBackup_;
                    ++lineSearchFailures_;
                    ++cstats_.lineSearchFailures;
                    if (std::getenv("FL_DEBUG_CONTACT")) {
                        Real rd = 0;
                        for (int i = 0; i < N; ++i) rd += dot(rhs_[i], dx_[i]);
                        std::fprintf(stderr, "[al ls-fail frame %d outer %d it %d] E0=%.9e maxdx=%.2e rhs.dx=%.2e\n", frame_, outer, n, E0, maxStep, rd);
                    }
                    break;
                }
                Real maxDx = 0;
                for (int i = 0; i < N; ++i) maxDx = std::max(maxDx, length(x_[i] - xBackup_[i]));
                if (maxDx < p_.newtonTolerance) break;
                if (step < 1.0 || maxDx > 0.5 * prevMaxDx) needFactor = true;
                prevMaxDx = maxDx;
            }
            // multiplier update (first-order PHR): lambda <- max(0, lambda - rho g)
            maxPen = 0;
            for (int i = 0; i < N; ++i) {
                if (w_[i] == 0 || !collider_) continue;
                Vec3 nrm;
                const Real g = collider_->eval(x_[i], nrm) - radius_[i];
                maxPen = std::max(maxPen, -g);
                alLambda_[i] = std::max(Real(0), alLambda_[i] - alRho_[i] * g);
            }
            if (maxPen <= p_.contactALTolerance) { ++outer; break; }
        }
        cstats_.alOuter += outer;
        cstats_.newtonIterations += nUsed;
        cstats_.maxPenetration = std::max(cstats_.maxPenetration, maxPen);
        lastCgIterations_ = cg;
        newtonIterations_ += nUsed;
        ++substepsDone_;
        for (int i = 0; i < N; ++i) cstats_.maxLambda = std::max(cstats_.maxLambda, alLambda_[i]);

        // contact state at the end of the substep: a vertex carries load when its multiplier is positive
        for (int i = 0; i < N; ++i) contactActive_[i] = alLambda_[i] > 0 ? 1 : 0;
        diagSubstepEnd();

        const Real ke0 = diag ? kineticEnergyOf(x_, h) : 0;
        solveBodyAL();
        const Real ke1 = diag ? kineticEnergyOf(x_, h) : 0;
        if (p_.selfCollision) {
            solveSelfCollision();
            // self contact can push a vertex into the body: project it back (no friction, the solve already applied it)
            for (int i = 0; i < N; ++i) {
                if (w_[i] == 0 || !collider_) continue;
                Vec3 nrm;
                const Real g = collider_->eval(x_[i], nrm) - radius_[i];
                if (g < 0) { x_[i] -= nrm * g; ++cstats_.bodyProjections; cstats_.bodyProjectionDistance += -g; }
            }
        }
        if (diag) {
            const Real ke2 = kineticEnergyOf(x_, h);
            cstats_.keAfterNewton += ke0;
            cstats_.keBodyPass += ke1 - ke0;
            cstats_.keSelfPass += ke2 - ke1;
        }
        for (int i = 0; i < N; ++i) {
            if (w_[i] == 0) continue;
            v_[i] = (x_[i] - xPrev_[i]) / h;
            const Real sp = length(v_[i]);
            if (sp > 50.0) v_[i] *= 50.0 / sp;
        }
    }
    ++frame_;
}

// After-the-solve Coulomb friction with the AL multiplier as the normal force. Over a substep of length h the normal
// impulse is lambda h, so the friction can cancel at most mu lambda h^2 / m of tangential displacement (relative to
// the collider's own motion). This is the same position-level rule as solveBody, with the projection depth there
// replaced by lambda h^2 / m (the normal correction the multiplier stands for). No projection: the solve keeps the
// vertex within contactALTolerance of the surface.
void ClothSolver::solveBodyAL() {
    if (!collider_) return;
    const int N = vertexCount();
    const bool movingCollider = static_cast<int>(colliderDisp_.size()) == N;
    const Real invS = Real(1) / std::max(1, p_.substeps);
    const Real h = p_.dt * invS;
    for (int i = 0; i < N; ++i) {
        if (w_[i] == 0 || alLambda_[i] <= 0) continue;
        const Vec3& n = contactN_[i];
        const Real pen = alLambda_[i] * h * h * w_[i];
        Vec3 disp = x_[i] - xPrev_[i];
        if (movingCollider) disp = disp - colliderDisp_[i] * invS;
        const Vec3 tang = disp - n * dot(disp, n);
        const Real dl = length(tang);
        if (dl > 1e-14) {
            if (dl < muS_[i] * pen) { x_[i] -= tang; ++cstats_.frictionStick; cstats_.frictionDistance += dl; }
            else {
                const Real k = std::min(muK_[i] * pen / dl, Real(1));
                x_[i] -= tang * k;
                ++cstats_.frictionSlide;
                cstats_.frictionDistance += dl * k;
            }
        }
    }
}

}  // namespace fl
