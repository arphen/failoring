// Static equilibrium solve of ClothSolver (idea S1). See docs/agent_notes/S1_static_settle.md.
//
// Minimises the potential energy
//     Pi(x) = E_constraints(x) - lam * sum_i m_i g.x_i          (stretch, shear, bending, seams, gravity)
// subject to the body contact (signed-distance projection, as in the implicit integrator) without simulating motion.
// A pseudo-transient proximal term  alpha/2 sum_i m_i |x_i - a_i|^2  (a = positions at the start of the stage) makes
// every Newton solve well posed (buckling, unsupported cloth, wedged contact); alpha is reduced geometrically and
// finally set to exactly zero, so the answer is an equilibrium of Pi itself, not of the regularised energy.
// Gravity is ramped in by continuation (lam: 0 -> gravityScale) with an adaptive increment.
// The energy, gradient, Hessian assembly, contact filters, linear solvers and projected line search are the ones of
// cloth_implicit.cpp; this file only adds the outer loops.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <ctime>

#include "failoring/cloth.hpp"

namespace fl {

Real ClothSolver::staticEnergy(Real alpha, Real lam) const {
    // xTilde_ holds the proximal anchor a_i
    Real E = variationalEnergy(alpha, 1.0);
    const int N = vertexCount();
    for (int i = 0; i < N; ++i)
        if (w_[i] != 0) E -= lam * mass_[i] * dot(p_.gravity, x_[i]);
    return E;
}

Real ClothSolver::potentialEnergy(Real gravityScale) const {
    Real E = variationalEnergy(0.0, 1.0);
    const int N = vertexCount();
    for (int i = 0; i < N; ++i)
        if (w_[i] != 0) E -= gravityScale * mass_[i] * dot(p_.gravity, x_[i]);
    return E;
}

void ClothSolver::setPanelMaterial(int panel, const FabricMaterial& mat) {
    mats_.at(static_cast<size_t>(panels_.at(static_cast<size_t>(panel)).material)) = mat;
    if (finalized_) finalize();  // rebuilds constraint parameters and masses; the sparsity pattern and ordering are kept
}

void ClothSolver::setRestUV(int v, const Vec2& restCoords) {
    restUV_.at(static_cast<size_t>(v)) = restCoords;
    if (finalized_) finalize();
}

// Regularised Newton iterations for the stage energy  Pi_lam(x) + alpha/2 |x - xTilde_|^2_M.
// resTol <= 0: only the step criterion is used (intermediate stages); otherwise converged needs both.
ClothSolver::StaticNewton ClothSolver::staticNewton(Real alpha, Real lam, int maxIt, Real stepTol, Real resTol, Real weightNorm,
                                                    Real maxStepCap) {
    const int N = vertexCount();
    StaticNewton out;
    const Real tiny = 1e-3;  // matrix-only floor on alpha when alpha = 0: keeps the factorisation definite, not in the gradient
    for (int n = 0; n < maxIt; ++n) {
        // contact: lift penetrating vertices onto the surface, remember normals (as in stepImplicit)
        for (int i = 0; i < N; ++i) {
            contactPhi_[i] = 1e30;
            if (!collider_ || w_[i] == 0) continue;
            Vec3 nrm;
            Real phi = collider_->eval(x_[i], nrm) - radius_[i];
            if (phi < 0) { x_[i] -= nrm * phi; phi = 0; }
            contactN_[i] = nrm;
            contactPhi_[i] = phi;
        }
        assembleEnergy(1.0);
        for (int i = 0; i < N; ++i)
            rhs_[i] = w_[i] == 0 ? Vec3(0, 0, 0)
                                 : -(grad_[i] + (x_[i] - xTilde_[i]) * (mass_[i] * alpha) - p_.gravity * (lam * mass_[i]));
        Real res2 = 0;
        for (int i = 0; i < N; ++i) {
            Mat3 P;
            contactActive_[i] = 0;
            if (w_[i] != 0) {
                P.addDiag(1);
                if (contactPhi_[i] < p_.contactBand && dot(rhs_[i], contactN_[i]) < 0) {
                    const Vec3& nn = contactN_[i];
                    P.addOuter(nn, nn, -1);
                    contactActive_[i] = 1;
                }
            }
            filt_[i] = P;
            // residual of the PHYSICAL problem (alpha = 0): the proximal force is not an imbalance
            if (w_[i] != 0) {
                const Vec3 f = filt_[i] * (-(grad_[i]) + p_.gravity * (lam * mass_[i]));
                res2 += length2(f);
            }
        }
        out.residual = std::sqrt(res2) / weightNorm;

        // solve (K + alpha M) dx = rhs; retry with a larger matrix floor if the factorisation fails
        int used = -1;
        Real hm = std::max(alpha, tiny);
        for (int attempt = 0; attempt < 4 && used < 0; ++attempt, hm *= 1e3) used = solveDirect(hm, rhs_, dx_, true);
        if (used < 0) used = solveLinear(std::max(alpha, tiny), rhs_, dx_);
        ++out.iters;

        Real maxStep = 0;
        for (int i = 0; i < N; ++i) maxStep = std::max(maxStep, length(dx_[i]));
        out.maxStep = maxStep;
        if (!std::isfinite(maxStep)) { out.ok = false; return out; }
        const bool resOk = resTol <= 0 || out.residual < resTol;
        if (maxStep < stepTol && resOk) { out.converged = true; return out; }
        if (maxStepCap > 0 && maxStep > maxStepCap) {
            const Real s = maxStepCap / maxStep;
            for (int i = 0; i < N; ++i) dx_[i] *= s;
        }

        // projected backtracking line search on the stage energy
        const Real E0 = staticEnergy(alpha, lam);
        const Real slack = 1e-13 * (std::fabs(E0) + 1e-9);
        xBackup_ = x_;
        Real step = 1.0;
        bool accepted = false;
        for (int ls = 0; ls < 14; ++ls) {
            for (int i = 0; i < N; ++i) {
                x_[i] = xBackup_[i] + dx_[i] * step;
                if (collider_ && w_[i] != 0) {
                    Vec3 nrm;
                    const Real phi = collider_->eval(x_[i], nrm) - radius_[i];
                    if (phi < 0) x_[i] -= nrm * phi;
                }
            }
            const Real E1 = staticEnergy(alpha, lam);
            if (std::isfinite(E1) && E1 <= E0 + slack) { accepted = true; break; }
            step *= 0.5;
        }
        if (!accepted) {
            x_ = xBackup_;
            ++lineSearchFailures_;
            out.ok = false;
            return out;
        }
        if (p_.selfCollision && staticSelfCollision_) {  // projection after the step (not part of the line-search energy)
            xPrev_ = xBackup_;
            solveSelfCollision();
        }
    }
    return out;
}

StaticResult ClothSolver::solveStatic(const StaticParams& sp) {
    if (!finalized_) finalize();
    if (skinOn() || p_.barrierContact) {  // not modelled here (see StaticResult::unsupported)
        StaticResult u;
        u.unsupported = true;
        return u;
    }
    const auto wall0 = std::chrono::steady_clock::now();
    const std::clock_t cpu0 = std::clock();
    const int analyses0 = symbolicAnalyses_;
    const int lsf0 = lineSearchFailures_;
    staticSelfCollision_ = sp.selfCollision;
    const int N = vertexCount();
    StaticResult R;

    const Real lamTarget = sp.gravityScale;
    Real weight2 = 0;
    for (int i = 0; i < N; ++i)
        if (w_[i] != 0) weight2 += length2(p_.gravity * (mass_[i] * std::max(std::fabs(lamTarget), Real(1e-3))));
    const Real weightNorm = weight2 > 0 ? std::sqrt(weight2) : 1.0;
    const Real cap = 0.2;  // trust region on the Newton step (m)

    Real lam, alpha, dlam;
    if (sp.warmStart) {
        lam = lamTarget;
        alpha = std::max(sp.alphaWarm, Real(0));
        dlam = 0;
    } else {
        lam = 0;
        alpha = sp.alphaStart;
        dlam = lamTarget / std::max(1, sp.loadSteps);
    }
    const Real alphaMax = 100 * std::max(sp.alphaStart, Real(1));
    const Real minDlam = std::fabs(lamTarget) / 4096;
    int finalRetries = 0;

    while (R.iterations < sp.maxIterations) {
        const bool atFull = std::fabs(lam - lamTarget) <= 1e-12;
        if (atFull && alpha <= 0) {
            // ---- final phase: true equilibrium of the physical energy
            xTilde_ = x_;
            const int budget = sp.maxIterations - R.iterations;
            StaticNewton r = staticNewton(0.0, lam, budget, sp.displacementTolerance, sp.residualTolerance, weightNorm, cap);
            R.iterations += r.iters;
            R.finalIterations += r.iters;
            R.residual = r.residual;
            R.maxStep = r.maxStep;
            if (r.converged) { R.converged = true; break; }
            if (!r.ok) {  // stalled (line search / non-finite): go back to a regularised stage and come down again
                if (++finalRetries > 3) break;
                alpha = std::max(sp.alphaMin * std::pow(10.0, finalRetries), Real(0.1));
                if (sp.verbose) std::fprintf(stderr, "[static] final phase stalled (res %.2e step %.2e), alpha -> %.3g\n", r.residual, r.maxStep, alpha);
                continue;
            }
            break;  // iteration cap
        }
        // ---- stage: regularised Newton solve at load lamTry
        const Real lamTry = atFull ? lam : (std::fabs(lamTarget - lam) <= std::fabs(dlam) * 1.0001 ? lamTarget : lam + dlam);
        const std::vector<Vec3> xStart = x_;
        xTilde_ = x_;
        StaticNewton r = staticNewton(alpha, lamTry, std::max(1, sp.stageNewton), sp.stageTolerance, 0.0, weightNorm, cap);
        R.iterations += r.iters;
        R.residual = r.residual;
        R.maxStep = r.maxStep;
        if (sp.verbose)
            std::fprintf(stderr, "[static] stage lam %.4f alpha %.3g: %d its, ok %d, step %.2e, res %.2e\n", lamTry, alpha, r.iters, r.ok ? 1 : 0, r.maxStep, r.residual);
        if (!r.ok) {
            x_ = xStart;
            ++R.failedStages;
            if (lamTry != lam && std::fabs(dlam) > minDlam) dlam *= 0.5;
            else alpha = std::min(std::max(alpha * 4, sp.alphaMin * 10), alphaMax);
            if (R.failedStages > 200) break;
            continue;
        }
        ++R.stages;
        const bool quick = r.converged && r.iters <= 2;
        lam = lamTry;
        if (!atFull && quick) dlam = std::min(dlam * 1.5, std::fabs(lamTarget) > 0 ? lamTarget : dlam);
        const bool full = std::fabs(lam - lamTarget) <= 1e-12;
        alpha *= quick ? sp.alphaDecay * sp.alphaDecay : sp.alphaDecay;
        if (alpha < sp.alphaMin) alpha = full ? 0.0 : sp.alphaMin;
    }

    // final feasibility pass (no friction): leave no vertex inside the body
    if (collider_)
        for (int i = 0; i < N; ++i) {
            if (w_[i] == 0) continue;
            Vec3 nrm;
            const Real phi = collider_->eval(x_[i], nrm) - radius_[i];
            if (phi < 0) x_[i] -= nrm * phi;
        }
    for (int i = 0; i < N; ++i) { v_[i] = Vec3(0, 0, 0); xPrev_[i] = x_[i]; }
    R.energy = potentialEnergy(lamTarget);
    R.lineSearchFailures = lineSearchFailures_ - lsf0;
    R.symbolicAnalyses = symbolicAnalyses_ - analyses0;
    R.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
    R.cpuSeconds = static_cast<double>(std::clock() - cpu0) / CLOCKS_PER_SEC;
    return R;
}

StaticStability ClothSolver::checkStaticStability(const StaticParams& sp, unsigned seed, Real amplitude, Real gravityWiggle) {
    if (!finalized_) finalize();
    const int N = vertexCount();
    StaticStability S;
    const std::vector<Vec3> x0 = x_;
    const Real E0 = potentialEnergy(sp.gravityScale);

    // deterministic xorshift noise, uniform in [-1, 1]
    unsigned s = seed * 2654435761u + 12345u;
    auto rnd = [&]() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return 2.0 * (s / 4294967296.0) - 1.0;
    };
    const Real comp = amplitude / std::sqrt(3.0);  // rms norm of the displacement = amplitude
    for (int i = 0; i < N; ++i)
        if (w_[i] != 0) x_[i] += Vec3(rnd(), rnd(), rnd()) * (comp * std::sqrt(3.0));

    StaticParams wp = sp;
    wp.warmStart = true;
    int iters = 0;
    for (Real f : {1 + gravityWiggle, 1 - gravityWiggle, 1.0}) {
        wp.gravityScale = sp.gravityScale * f;
        const StaticResult r = solveStatic(wp);
        iters += r.iterations;
        S.converged = r.converged;
    }
    S.iterations = iters;
    Real m2 = 0;
    for (int i = 0; i < N; ++i) {
        const Real d = length(x_[i] - x0[i]);
        S.maxShapeDiff = std::max(S.maxShapeDiff, d);
        m2 += d * d;
    }
    S.rmsShapeDiff = N ? std::sqrt(m2 / N) : 0;
    S.energyDiff = potentialEnergy(sp.gravityScale) - E0;
    S.relEnergyDiff = S.energyDiff / std::max(std::fabs(E0), Real(1e-12));
    x_ = x0;
    for (int i = 0; i < N; ++i) { v_[i] = Vec3(0, 0, 0); xPrev_[i] = x_[i]; }
    return S;
}

}  // namespace fl
