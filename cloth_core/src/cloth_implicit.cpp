// Implicit-Euler backend of ClothSolver.
//
// Each substep minimises the variational form of backward Euler
//     G(x) = 1/(2h^2) |x - x~|^2_M  +  E(x),      x~ = x_n + h v_n + h^2 g
// with Newton steps  (M/h^2 + K) dx = -( M (x - x~)/h^2 + grad E ),
// where K is the Gauss-Newton Hessian of the constraint energies
//     E = sum_c  1/2 * kappa_c * C_c(x)^2          (kappa = 1/compliance)
// Because the iteration is stationary exactly where grad G = 0, the converged
// state is the true static equilibrium of the model no matter how many CG
// iterations were spent - that is what makes drape/sag/fit predictions
// trustworthy. Contacts are handled by position projection after the solve.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <unordered_map>
#include <vector>

#include "failoring/cloth.hpp"

using fl::contact::ContactPair;

namespace fl {

int ClothSolver::BlockMatrix::find(int i, int j) const {
    auto b = col.begin() + rowStart[i], e = col.begin() + rowStart[i + 1];
    auto it = std::lower_bound(b, e, j);
    return (it != e && *it == j) ? static_cast<int>(it - col.begin()) : -1;
}

void ClothSolver::buildBlockPattern() {
    const int N = vertexCount();
    rebuildPattern();
    grad_.assign(N, Vec3());
    rhs_.assign(N, Vec3());
    dx_.assign(N, Vec3());
    xTilde_.assign(N, Vec3());
    cgR_.assign(N, Vec3());
    cgZ_.assign(N, Vec3());
    cgP_.assign(N, Vec3());
    cgAp_.assign(N, Vec3());
    precond_.assign(N, Mat3());
    filt_.assign(N, Mat3());
    ssorY_.assign(N, Vec3());
    ssorU_.assign(N, Vec3());
    xCcd2_.assign(N, Vec3());
    contactActive_.assign(N, 0);
    contactN_.assign(N, Vec3(0, 1, 0));
    contactPhi_.assign(N, 1e30);
}

// Block pattern of the Hessian (elastic couplings plus the registered barrier-contact couplings), the scalar CSR
// pattern, the fill-reducing ordering and the symbolic Cholesky analysis. Called once at start-up and again
// (barrier mode) whenever an active contact pair couples vertices the pattern does not contain yet.
void ClothSolver::rebuildPattern() {
    const int N = vertexCount();
    ++patternVersion_;
    std::vector<std::set<int>> nb(N);
    for (int i = 0; i < N; ++i) nb[i].insert(i);
    auto couple = [&](const int* idx, int n) {
        for (int a = 0; a < n; ++a)
            for (int b = 0; b < n; ++b) nb[idx[a]].insert(idx[b]);
    };
    for (const TriC& c : triC_) { const int idx[3] = {c.i0, c.i1, c.i2}; couple(idx, 3); }
    for (const BendC& b : bendC_) couple(b.i, 4);
    for (const SeamC& s : seamC_) { const int idx[3] = {s.a, s.b0, s.b1}; couple(idx, 3); }
    for (std::uint64_t k : extraCouple_) {
        const int a = static_cast<int>(k >> 32), b = static_cast<int>(k & 0xffffffffu);
        nb[a].insert(b);
        nb[b].insert(a);
    }

    K_.rowStart.assign(N + 1, 0);
    K_.col.clear();
    K_.diag.assign(N, 0);
    for (int i = 0; i < N; ++i) {
        K_.rowStart[i] = static_cast<int>(K_.col.size());
        for (int j : nb[i]) {
            if (j == i) K_.diag[i] = static_cast<int>(K_.col.size());
            K_.col.push_back(j);
        }
    }
    K_.rowStart[N] = static_cast<int>(K_.col.size());
    K_.val.assign(K_.col.size(), Mat3());
    bindBlockSlots();

    // scalar CSR pattern (3x3 blocks expanded) + fill-reducing ordering for the direct solver
    const int n3 = 3 * N;
    sRowStart_.assign(n3 + 1, 0);
    for (int i = 0; i < N; ++i) {
        const int len = K_.rowStart[i + 1] - K_.rowStart[i];
        for (int a = 0; a < 3; ++a) sRowStart_[3 * i + a + 1] = sRowStart_[3 * i + a] + 3 * len;
    }
    sCol_.assign(sRowStart_[n3], 0);
    for (int i = 0; i < N; ++i)
        for (int a = 0; a < 3; ++a) {
            int pos = sRowStart_[3 * i + a];
            for (int k = K_.rowStart[i]; k < K_.rowStart[i + 1]; ++k)
                for (int b = 0; b < 3; ++b) sCol_[pos++] = 3 * K_.col[k] + b;
        }
    std::vector<double> xyz(n3);
    for (int i = 0; i < N; ++i) { xyz[3 * i] = x_[i].x; xyz[3 * i + 1] = x_[i].y; xyz[3 * i + 2] = x_[i].z; }
    const std::vector<int> vorder = nestedDissectionOrder(N, K_.rowStart, K_.col, xyz);
    std::vector<int> perm;
    perm.reserve(n3);
    for (int v : vorder)
        for (int a = 0; a < 3; ++a) perm.push_back(3 * v + a);
    chol_.analyze(n3, sRowStart_, sCol_, perm);
    ++symbolicAnalyses_;
    patternValid_ = true;
    cholValid_ = false;
    sVal_.assign(sCol_.size(), 0.0);
    sRhs_.assign(n3, 0.0);
    sSol_.assign(n3, 0.0);
}

void ClothSolver::bindBlockSlots() {
    for (TriC& c : triC_) {
        const int idx[3] = {c.i0, c.i1, c.i2};
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b) c.slot[a * 3 + b] = K_.find(idx[a], idx[b]);
    }
    for (BendC& bc : bendC_)
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b) bc.slot[a * 4 + b] = K_.find(bc.i[a], bc.i[b]);
    for (SeamC& s : seamC_) {
        const int idx[3] = {s.a, s.b0, s.b1};
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b) s.slot[a * 3 + b] = K_.find(idx[a], idx[b]);
    }
}

void ClothSolver::assembleEnergy(Real seamScale) {
    std::fill(grad_.begin(), grad_.end(), Vec3(0, 0, 0));
    for (Mat3& m : K_.val) m.setZero();

    auto add = [&](int n, const int* idx, const int* slot, const Vec3* g, Real C, Real kappa) {
        for (int a = 0; a < n; ++a) grad_[idx[a]] += g[a] * (kappa * C);
        for (int a = 0; a < n; ++a)
            for (int b = 0; b < n; ++b) K_.val[slot[a * n + b]].addOuter(g[a], g[b], kappa);
    };

    // Tension stiffness: the exact Hessian of 1/2 kappa C^2 with C = |w| - 1 also has the term
    // kappa C / |w| * beta_a beta_b (I - n n^T), positive semi-definite while the yarn direction is stretched
    // (C > 0) and indefinite-making under compression, where it stays dropped (Gauss-Newton). Without it a taut
    // waistline or seam corner has no stiffness across the yarn in the Newton model: each step pulls such vertices
    // centimetres sideways, the line search cuts the step for everyone, and a wedged ring creeps down the body.
    const bool tension = p_.hessianAuto ? autoTension_ : p_.tensionStiffness;
    auto addTension = [&](const int* slot, const Real* beta, const Vec3& nrm, Real s) {
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b) {
                const Real f = s * beta[a] * beta[b];
                Mat3& B = K_.val[slot[a * 3 + b]];
                B.addDiag(f);
                B.addOuter(nrm, nrm, -f);
            }
    };
    for (const TriC& c : triC_) {
        const int idx[3] = {c.i0, c.i1, c.i2};
        const Vec3 &x0 = x_[c.i0], &x1 = x_[c.i1], &x2 = x_[c.i2];
        const Vec3 d1 = x1 - x0, d2 = x2 - x0;
        const Vec3 wu = d1 * c.a + d2 * c.c;
        const Vec3 wv = d1 * c.b + d2 * c.d;
        const Real lu = length(wu), lv = length(wv);
        if (lu > 1e-12) {
            const Vec3 nu = wu / lu;
            const Vec3 g[3] = {-(nu * c.a + nu * c.c), nu * c.a, nu * c.c};
            const Real C = lu - 1;
            if (c.lockU > 0 && C > c.lockU) {  // J-curve: stiff above the lock strain (crochet plan 2.3)
                // add(..., dedc/kappa, kappa) keeps the locked branch in the same form as the plain path: the
                // gradient is g * (kappa * (dedc/kappa)) = g * dedc exactly (kappa * (dedc/kappa) = dedc), and
                // kappa is the Gauss-Newton Hessian factor r*k.
                const detail::JCurveStretch jc = detail::jCurveStretch(C, 1.0 / c.alphaU, c.lockU, c.ratio);
                add(3, idx, c.slot, g, jc.dedc / jc.kappa, jc.kappa);
                // The tension Hessian term uses dE/dC (the locked slope) in place of the plain C/alphaU.
                if (tension) { const Real beta[3] = {-(c.a + c.c), c.a, c.c}; addTension(c.slot, beta, nu, jc.dedc / lu); }
            } else {
                // unlocked arithmetic, unchanged from before T1.4 (lockU = 0 keeps every old material on this path)
                add(3, idx, c.slot, g, C, (C < 0 ? c.comp : 1.0) / c.alphaU);
                if (tension && C > 0) { const Real beta[3] = {-(c.a + c.c), c.a, c.c}; addTension(c.slot, beta, nu, C / (c.alphaU * lu)); }
            }
        }
        if (lv > 1e-12) {
            const Vec3 nv = wv / lv;
            const Vec3 g[3] = {-(nv * c.b + nv * c.d), nv * c.b, nv * c.d};
            const Real C = lv - 1;
            if (c.lockV > 0 && C > c.lockV) {  // J-curve: stiff above the lock strain (crochet plan 2.3)
                const detail::JCurveStretch jc = detail::jCurveStretch(C, 1.0 / c.alphaV, c.lockV, c.ratio);
                add(3, idx, c.slot, g, jc.dedc / jc.kappa, jc.kappa);  // gradient g * dedc; Hessian kappa*g g^T
                if (tension) { const Real beta[3] = {-(c.b + c.d), c.b, c.d}; addTension(c.slot, beta, nv, jc.dedc / lv); }
            } else {
                // unlocked arithmetic, unchanged from before T1.4 (lockV = 0 keeps every old material on this path)
                add(3, idx, c.slot, g, C, (C < 0 ? c.comp : 1.0) / c.alphaV);
                if (tension && C > 0) { const Real beta[3] = {-(c.b + c.d), c.b, c.d}; addTension(c.slot, beta, nv, C / (c.alphaV * lv)); }
            }
        }
        {
            const Vec3 g1 = wv * c.a + wu * c.b;
            const Vec3 g2 = wv * c.c + wu * c.d;
            const Vec3 g[3] = {-(g1 + g2), g1, g2};
            add(3, idx, c.slot, g, dot(wu, wv), 1.0 / c.alphaS);
        }
    }
    for (const BendC& b : bendC_) {
        Vec3 g[4];
        if (b.rest == 0) {  // no rest angle: the unsigned positional angle, unchanged from before T1.3
            const Real theta = detail::hingeAngle(x_[b.i[0]], x_[b.i[1]], x_[b.i[2]], x_[b.i[3]], g);
            if (theta < 0) continue;
            add(4, b.i, b.slot, g, theta, 1.0 / b.alpha);
        } else {  // signed hinge with a rest angle (crochet plan 2.2): C = theta_s - rest, same Gauss-Newton form
            Real ths = 0;
            if (!detail::hingeAngleSigned(x_[b.i[0]], x_[b.i[1]], x_[b.i[2]], x_[b.i[3]], ths, g)) continue;
            add(4, b.i, b.slot, g, ths - b.rest, 1.0 / b.alpha);
        }
    }
    for (const SeamC& s : seamC_) {
        const Vec3 target = x_[s.b0] * (1 - s.t) + x_[s.b1] * s.t;
        const Vec3 d = x_[s.a] - target;
        const Real k = seamScale / s.alpha;
        // energy 0.5*k*|d|^2 is exactly quadratic: gradient k*d*w_a, Hessian k*w_a*w_b*I (no rank-1 approximation)
        const Real w[3] = {1.0, -(1 - s.t), -s.t};
        const int idx[3] = {s.a, s.b0, s.b1};
        for (int a = 0; a < 3; ++a) {
            grad_[idx[a]] += d * (k * w[a]);
            for (int b = 0; b < 3; ++b) K_.val[s.slot[a * 3 + b]].addDiag(k * w[a] * w[b]);
        }
    }
}

Real ClothSolver::variationalEnergy(Real invH2, Real seamScale) const {
    const int N = vertexCount();
    Real E = 0;
    for (int i = 0; i < N; ++i)
        if (w_[i] != 0) E += 0.5 * mass_[i] * invH2 * length2(x_[i] - xTilde_[i]);
    for (const TriC& c : triC_) {
        const Vec3 d1 = x_[c.i1] - x_[c.i0], d2 = x_[c.i2] - x_[c.i0];
        const Vec3 wu = d1 * c.a + d2 * c.c, wv = d1 * c.b + d2 * c.d;
        const Real Cu = length(wu) - 1, Cv = length(wv) - 1, Cs = dot(wu, wv);
        if (c.lockU > 0 && Cu > c.lockU) E += detail::jCurveStretch(Cu, 1.0 / c.alphaU, c.lockU, c.ratio).e;
        else E += 0.5 * (Cu < 0 ? c.comp : 1.0) / c.alphaU * Cu * Cu;  // unlocked: unchanged from before T1.4
        if (c.lockV > 0 && Cv > c.lockV) E += detail::jCurveStretch(Cv, 1.0 / c.alphaV, c.lockV, c.ratio).e;
        else E += 0.5 * (Cv < 0 ? c.comp : 1.0) / c.alphaV * Cv * Cv;  // unlocked: unchanged from before T1.4
        E += 0.5 / c.alphaS * Cs * Cs;
    }
    for (const BendC& b : bendC_) {
        if (b.rest == 0) {  // no rest angle: the unsigned positional angle, unchanged from before T1.3
            const Real th = detail::hingeAngle(x_[b.i[0]], x_[b.i[1]], x_[b.i[2]], x_[b.i[3]], nullptr);
            if (th >= 0) E += 0.5 / b.alpha * th * th;
        } else {  // signed hinge with a rest angle (crochet plan 2.2)
            Real ths = 0;
            if (detail::hingeAngleSigned(x_[b.i[0]], x_[b.i[1]], x_[b.i[2]], x_[b.i[3]], ths, nullptr))
                E += 0.5 / b.alpha * (ths - b.rest) * (ths - b.rest);
        }
    }
    for (const SeamC& s : seamC_) {
        const Vec3 d = x_[s.a] - (x_[s.b0] * (1 - s.t) + x_[s.b1] * s.t);
        E += 0.5 * seamScale / s.alpha * length2(d);
    }
    return E;
}

// Solves (diag(m/h^2) + K) dx = rhs with block-Jacobi PCG on the filtered subspace: each vertex's motion
// is restricted by filt_[i] (identity = free, zero = pinned, I - n n^T = sliding on the body surface).
int ClothSolver::solveLinear(Real h2inv, const std::vector<Vec3>& b, std::vector<Vec3>& x, bool cholPrec, int maxIt) {
    const int N = vertexCount();
    if (extSolver_ && !cholPrec) {
        extMass_.resize(N);
        for (int i = 0; i < N; ++i) extMass_[i] = mass_[i] * h2inv;
        LinearSystemView v;
        v.n = N; v.rowStart = K_.rowStart.data(); v.col = K_.col.data(); v.val = K_.val.data(); v.diag = K_.diag.data();
        v.massTerm = extMass_.data(); v.filt = filt_.data(); v.rhs = b.data();
        v.maxIterations = maxIt > 0 ? std::min(maxIt, p_.cgMaxIterations) : p_.cgMaxIterations;
        v.tolerance = p_.cgTolerance;
        v.patternVersion = patternVersion_;
        const int it = extSolver_(v, x);
        if (it >= 0 && static_cast<int>(x.size()) == N) return it;
    }
    if (!cholPrec)
        for (int i = 0; i < N; ++i) {
            Mat3 D = K_.val[K_.diag[i]];
            D.addDiag(mass_[i] * h2inv);
            precond_[i] = D.inverse();
        }
    // out = P (A P in)
    auto matvec = [&](const std::vector<Vec3>& in, std::vector<Vec3>& out) {
        for (int i = 0; i < N; ++i) {
            Vec3 s = in[i] * (mass_[i] * h2inv);
            for (int k = K_.rowStart[i]; k < K_.rowStart[i + 1]; ++k) s += K_.val[k] * in[K_.col[k]];
            out[i] = filt_[i] * s;
        }
    };
    // Symmetric block Gauss-Seidel (SSOR, omega = 1):  z = (D+U)^-1 D (D+L)^-1 r, then filtered.
    auto applyPrec = [&](const std::vector<Vec3>& r, std::vector<Vec3>& z) {
        if (cholPrec) {
            for (int i = 0; i < N; ++i) {
                const Vec3 f = filt_[i] * r[i];
                sRhs_[3 * i] = f.x; sRhs_[3 * i + 1] = f.y; sRhs_[3 * i + 2] = f.z;
            }
            chol_.solve(sRhs_, sSol_);
            for (int i = 0; i < N; ++i) z[i] = filt_[i] * Vec3(sSol_[3 * i], sSol_[3 * i + 1], sSol_[3 * i + 2]);
            return;
        }
        if (p_.cgBlockJacobi) {  // z_i = D_i^-1 r_i per vertex: the GPU-parallel preconditioner
            for (int i = 0; i < N; ++i) z[i] = w_[i] == 0 ? Vec3(0, 0, 0) : filt_[i] * (precond_[i] * r[i]);
            return;
        }
        for (int i = 0; i < N; ++i) {
            if (w_[i] == 0) { ssorY_[i] = Vec3(0, 0, 0); continue; }
            Vec3 acc = r[i];
            for (int k = K_.rowStart[i]; k < K_.diag[i]; ++k) acc -= K_.val[k] * ssorY_[K_.col[k]];
            ssorY_[i] = precond_[i] * acc;
        }
        for (int i = 0; i < N; ++i)
            ssorU_[i] = w_[i] == 0 ? Vec3(0, 0, 0) : K_.val[K_.diag[i]] * ssorY_[i] + ssorY_[i] * (mass_[i] * h2inv);
        for (int i = N - 1; i >= 0; --i) {
            if (w_[i] == 0) { z[i] = Vec3(0, 0, 0); continue; }
            Vec3 acc = ssorU_[i];
            for (int k = K_.diag[i] + 1; k < K_.rowStart[i + 1]; ++k) acc -= K_.val[k] * z[K_.col[k]];
            z[i] = precond_[i] * acc;
        }
        for (int i = 0; i < N; ++i) z[i] = filt_[i] * z[i];
    };
    auto dotv = [&](const std::vector<Vec3>& a, const std::vector<Vec3>& c) {
        Real s = 0;
        for (int i = 0; i < N; ++i) s += dot(a[i], c[i]);
        return s;
    };

    // warm start (cgWarmStart): begin from the previous solve's dx (time-coherent motion makes consecutive corrections
    // alike, e.g. the stretch that holds a hanging cloth up against gravity), restricted to this solve's filters
    const bool warm = p_.cgWarmStart && !cholPrec && static_cast<int>(x.size()) == N;
    if (warm) {
        for (int i = 0; i < N; ++i) x[i] = filt_[i] * x[i];
        matvec(x, cgAp_);
        for (int i = 0; i < N; ++i) cgR_[i] = filt_[i] * b[i];
    } else {
        x.assign(N, Vec3(0, 0, 0));
        for (int i = 0; i < N; ++i) cgR_[i] = filt_[i] * b[i];
    }
    const Real bnorm = std::sqrt(dotv(cgR_, cgR_));
    if (bnorm < 1e-30) { if (warm) x.assign(N, Vec3(0, 0, 0)); return 0; }
    if (warm) {
        for (int i = 0; i < N; ++i) cgR_[i] = cgR_[i] - cgAp_[i];
        // keep the guess only if it reduces the residual (a stale guess after a jump would be worse than zero)
        if (std::sqrt(dotv(cgR_, cgR_)) >= bnorm) {
            x.assign(N, Vec3(0, 0, 0));
            for (int i = 0; i < N; ++i) cgR_[i] = filt_[i] * b[i];
        }
    }
    applyPrec(cgR_, cgZ_);
    for (int i = 0; i < N; ++i) cgP_[i] = cgZ_[i];
    Real rz = dotv(cgR_, cgZ_);
    int it = 0;
    const int itLimit = maxIt > 0 ? std::min(maxIt, p_.cgMaxIterations) : p_.cgMaxIterations;
    for (; it < itLimit; ++it) {
        matvec(cgP_, cgAp_);
        const Real pAp = dotv(cgP_, cgAp_);
        if (pAp <= 0) break;
        const Real alpha = rz / pAp;
        for (int i = 0; i < N; ++i) {
            x[i] += cgP_[i] * alpha;
            cgR_[i] -= cgAp_[i] * alpha;
        }
        if (std::sqrt(dotv(cgR_, cgR_)) <= p_.cgTolerance * bnorm) { ++it; break; }
        applyPrec(cgR_, cgZ_);
        const Real rzNew = dotv(cgR_, cgZ_);
        const Real beta = rzNew / rz;
        rz = rzNew;
        for (int i = 0; i < N; ++i) cgP_[i] = cgZ_[i] + cgP_[i] * beta;
    }
    return it;
}

// Assemble the scalar (3N x 3N) matrix of the current Newton iteration and factor it. Stiff penalties stand in
// for the filters (pins and body contacts) so the factor is a valid preconditioner for the filtered system.
bool ClothSolver::refactor(Real h2inv) {
    const int N = vertexCount();
    for (int i = 0; i < N; ++i) {
        for (int k = K_.rowStart[i]; k < K_.rowStart[i + 1]; ++k) {
            const Mat3& B = K_.val[k];
            for (int a = 0; a < 3; ++a) {
                const int base = sRowStart_[3 * i + a] + 3 * (k - K_.rowStart[i]);
                for (int b = 0; b < 3; ++b) sVal_[base + b] = B.m[3 * a + b];
            }
        }
        const int dk = K_.diag[i];
        const Mat3& D = K_.val[dk];
        const Real tr = (D.m[0] + D.m[4] + D.m[8]) / 3.0 + mass_[i] * h2inv;
        const Real kc = 1e5 * tr;
        for (int a = 0; a < 3; ++a) {
            const int base = sRowStart_[3 * i + a] + 3 * (dk - K_.rowStart[i]);
            sVal_[base + a] += mass_[i] * h2inv;
            if (w_[i] == 0) sVal_[base + a] += kc;
        }
        if (w_[i] != 0 && contactActive_[i] && !alOn_) {  // the hold's normal lock (not with contactAL: no hold there)
            const Vec3& n = contactN_[i];
            const Real nn[3] = {n.x, n.y, n.z};
            for (int a = 0; a < 3; ++a) {
                const int base = sRowStart_[3 * i + a] + 3 * (dk - K_.rowStart[i]);
                for (int b = 0; b < 3; ++b) sVal_[base + b] += kc * nn[a] * nn[b];
            }
        }
    }
    ++factorizations_;
    return chol_.factor(sVal_);
}

int ClothSolver::solveDirect(Real h2inv, const std::vector<Vec3>& rhs, std::vector<Vec3>& dx, bool doRefactor) {
    const int N = vertexCount();
    if (doRefactor && !refactor(h2inv)) return -1;
    for (int i = 0; i < N; ++i) {
        const Vec3 r = filt_[i] * rhs[i];
        sRhs_[3 * i] = r.x; sRhs_[3 * i + 1] = r.y; sRhs_[3 * i + 2] = r.z;
    }
    chol_.solve(sRhs_, sSol_);
    for (int i = 0; i < N; ++i) dx[i] = filt_[i] * Vec3(sSol_[3 * i], sSol_[3 * i + 1], sSol_[3 * i + 2]);
    return 0;
}

// PCG on the exact current matrix, preconditioned by a Cholesky factor that may be several solves old.
int ClothSolver::solveCholPcg(Real h2inv, const std::vector<Vec3>& rhs, std::vector<Vec3>& dx) {
    if (!cholValid_ || cholAge_ >= p_.cholMaxAge) {
        if (!refactor(h2inv)) { cholValid_ = false; return -1; }
        cholValid_ = true;
        cholAge_ = 0;
    }
    const bool stale = cholAge_ > 0;
    int it = solveLinear(h2inv, rhs, dx, true, stale ? 16 : -1);
    if (stale && it >= 16) {  // stale factor no longer a good preconditioner: refresh and redo
        if (!refactor(h2inv)) { cholValid_ = false; return -1; }
        cholAge_ = 0;
        it += solveLinear(h2inv, rhs, dx, true);
    }
    ++cholAge_;
    return it;
}

Real ClothSolver::stepKE(Real h) const {
    Real e = 0;
    for (size_t i = 0; i < x_.size(); ++i)
        if (w_[i] != 0) e += 0.5 * mass_[i] * length2(x_[i] - xPrev_[i]) / (h * h);
    return e;
}

void ClothSolver::stepImplicit() {
    const int S = std::max(1, p_.substeps);
    const Real h = p_.dt / S;
    const Real invH2 = 1.0 / (h * h);
    const int N = vertexCount();
    const Real seamScale = p_.seamRampFrames > 0 ? std::min(Real(1), std::max(Real(0.02), (frame_ + 1.0) / p_.seamRampFrames)) : Real(1);

    const bool skin = skinOn() && collider_;
    if (rigidForce_.size() != static_cast<size_t>(N)) { rigidForce_.assign(N, 0.0); skinK_.assign(N, 0.0); skinPhi_.assign(N, 1e30); }
    const bool barrier = p_.barrierContact;
    if (barrier) barrierInit(h);
    // Barrier mode: move x_ towards `target` as far as CCD allows (all of it unless a pair would be crossed).
    std::vector<Vec3>& xFrom = xCcd_;
    auto ccdMove = [&](const std::vector<Vec3>& target) -> Real {
        xFrom = x_;
        const Real t = barrierMaxStep(xFrom, target, bSwept_);
        if (t >= 1) { x_ = target; return 1; }
        for (int i = 0; i < N; ++i) x_[i] = xFrom[i] + (target[i] - xFrom[i]) * t;
        return t;
    };

    const bool diag = p_.contactDiagnostics;
    if (diag && prevSubActive_.size() != static_cast<size_t>(N)) { prevSubActive_.assign(N, 0); prevActive_.assign(N, 0); }
    // K2 contact multipliers (cloth_contact_al.cpp): replace the projection + hold for the body when on
    const bool al = p_.contactAL && collider_ && !skin && !barrier;
    alOn_ = al;
    if (al && alLambda_.size() != static_cast<size_t>(N)) { alLambda_.assign(N, 0.0); alRho_.assign(N, 1.0); }

    for (int s = 0; s < S; ++s) {
        if (skin) updateSkinAreas();
        for (int i = 0; i < N; ++i) {
            xPrev_[i] = x_[i];
            if (w_[i] == 0) { v_[i] = Vec3(0, 0, 0); xTilde_[i] = x_[i]; continue; }
            v_[i] *= std::exp(-damp_[i] * h);
            v_[i] += p_.gravity * h;
            xTilde_[i] = x_[i] + v_[i] * h;
            if (!barrier) x_[i] = xTilde_[i];
        }
        if (al)
            for (int i = 0; i < N; ++i) alRho_[i] = p_.contactALStiffness * std::max(mass_[i], Real(1e-12)) * invH2;
        const bool friction = p_.bodyFrictionMode == 2 && collider_;
        if (friction) frictionLag(skin);  // normal forces and normals of the end of the previous substep
        // barrier mode: the predictor (initial guess) is itself a motion and must not cross a surface
        if (barrier && ccdMove(xTilde_) < 1) ++bstats_.initialGuessLimited;
        int cg = 0;
        bool needFactor = true;
        Real prevMaxDx = 1e30;
        int nUsed = 0;
        Real lastDx = 0;
        bool converged = false, lsFailed = false;
        int nMax = std::max(1, p_.newtonIterations);
        bool switched = false;            // hessianAuto: model already switched in this substep
        bool newStalled = false;          // ... and the switched-to model stalled too
        int cutStreak = 0, fullStreak = 0;  // consecutive iterations whose line search cut the step / took the full step
        Real newtonHist[4] = {1e30, 1e30, 1e30, 1e30};  // Newton update sizes of the last iterations (newest first)
        int nBase = 0;  // contactAL: Newton iterations of the earlier solves in this substep
        for (int outer = 0;; ++outer) {  // contactAL: Newton solve, multiplier update, repeat (one pass otherwise)
        if (outer > 0) {
            needFactor = true; prevMaxDx = 1e30; converged = false; lsFailed = false;
            nMax = std::max(1, p_.newtonIterations); switched = false; newStalled = false; cutStreak = fullStreak = 0;
            for (Real& v : newtonHist) v = 1e30;
        }
        for (int n = 0; n < nMax; ++n) {
            nUsed = nBase + n + 1;
            // Contact inside the solve: lift penetrating vertices onto the body surface first ...
            bool lifted = false;
            if (barrier) xLift_ = x_;
            for (int i = 0; i < N; ++i) {
                contactPhi_[i] = 1e30;
                if (skin) { skinK_[i] = 0; skinPhi_[i] = 1e30; }
                if (!collider_ || w_[i] == 0) continue;
                Vec3 nrm;
                Real phi = collider_->eval(x_[i], nrm) - radius_[i];
                Real lim = 0;  // K1: depth of the soft layer here (0 = rigid); the rigid stop sits `lim` below the surface
                if (skin && phi < 0.05) {
                    const Real sc = collider_->skinScale(x_[i]);
                    if (sc > 0) { lim = p_.skinMaxDepth; skinK_[i] = p_.skinStiffness * sc * skinArea_[i]; }
                }
                Real phiR = phi + lim;
                if (phiR < 0 && !al) {
                    if (diag) { ++cdiag_.lifts; cdiag_.liftDist += -phiR; if (n > 0) ++cdiag_.liftsAfterFirstIter; }
                    if (barrier) { xLift_[i] = x_[i] - nrm * phiR; lifted = true; }  // K5: lift is CCD-capped later
                    else x_[i] -= nrm * phiR;
                    phiR = 0; phi = -lim;
                }
                contactN_[i] = nrm;
                contactPhi_[i] = phiR;
                if (skin) skinPhi_[i] = phi;
            }
            if (barrier) {
                if (lifted) ccdMove(xLift_);  // a lift that would cross a cloth surface is cut short (stays held by the filter)
                barrierUpdateSet();
            }
            assembleEnergy(seamScale);
            if (skin) assembleSkin();
            if (barrier) barrierAddToSystem();
            if (friction) assembleFriction(h, skin);
            if (al) alAssemble();
            for (int i = 0; i < N; ++i)
                rhs_[i] = w_[i] == 0 ? Vec3(0, 0, 0) : -((x_[i] - xTilde_[i]) * (mass_[i] * invH2) + grad_[i]);
            // ... then forbid motion into the body for vertices resting on it (unless the net force pulls them away).
            // "Resting" includes a thin band above the surface (contactBand): a vertex that slid tangentially over a
            // convex part of the body lifts off by a few micrometres, and without the band it would be released,
            // pulled into the body by the full Newton step and projected back, every iteration.
            for (int i = 0; i < N; ++i) {
                Mat3 P;
                contactActive_[i] = 0;
                if (w_[i] != 0 && al) {  // contactAL: no hold; "active" = the multiplier term pushes
                    P.addDiag(1);
                    const Real f = alLambda_[i] - alRho_[i] * contactPhi_[i];
                    contactActive_[i] = f > 0;
                    filt_[i] = P;
                    rigidForce_[i] = std::max(Real(0), f);
                    continue;
                }
                if (w_[i] != 0) {
                    P.addDiag(1);
                    if (contactPhi_[i] < p_.contactBand && dot(rhs_[i], contactN_[i]) < 0) {
                        const Vec3& nn = contactN_[i];
                        P.addOuter(nn, nn, -1);
                        contactActive_[i] = 1;
                    }
                }
                filt_[i] = P;
                rigidForce_[i] = contactActive_[i] ? -dot(rhs_[i], contactN_[i]) : 0.0;
            }
            if (diag) {
                if (n > 0)
                    for (int i = 0; i < N; ++i) cdiag_.flipsIter += contactActive_[i] != prevActive_[i];
                prevActive_.assign(contactActive_.begin(), contactActive_.end());
            }
            {
                int used = -1;
                if (p_.linearSolver == LinearSolver::CholeskyPCG) used = solveCholPcg(invH2, rhs_, dx_);
                else if (p_.linearSolver == LinearSolver::Cholesky) { used = solveDirect(invH2, rhs_, dx_, needFactor || !p_.reuseFactorization); needFactor = false; }
                if (used < 0) used = solveLinear(invH2, rhs_, dx_);
                cg += used;
            }
            {   // already converged: the Newton update is below tolerance, nothing to do
                Real maxStep = 0;
                for (int i = 0; i < N; ++i) maxStep = std::max(maxStep, length(dx_[i]));
                lastDx = maxStep;
                if (maxStep < p_.newtonTolerance) { converged = true; break; }
            }
            // backtracking line search on the variational energy (keeps Newton from overshooting)
            xBackup_ = x_;
            Real step = 1.0;
            if (barrier) {
                // CCD step cap: the largest fraction of the full Newton step that no pair crosses. The swept candidate
                // set (every pair whose boxes come within d_hat along the step) also defines the barrier energy below.
                for (int i = 0; i < N; ++i) xCcd2_[i] = xBackup_[i] + dx_[i];
                const Real tc = barrierMaxStep(xBackup_, xCcd2_, bSwept_);
                if (tc < 1) { step = tc; ++bstats_.ccdLimitedSteps; }
            }
            Real E0 = variationalEnergy(invH2, seamScale) + (barrier ? barrierEnergy(xBackup_, bSwept_) : 0);
            if (skin)
                for (int i = 0; i < N; ++i)
                    if (skinK_[i] > 0 && skinPhi_[i] < 0) E0 += 0.5 * skinK_[i] * skinPhi_[i] * skinPhi_[i];
            if (friction) E0 += frictionEnergy(h);
            if (al) E0 += alEnergy();
            bool accepted = false;
            for (int ls = 0; ls < (barrier ? 30 : 10); ++ls) {
                Real Eskin = 0;
                bool projected = false;
                for (int i = 0; i < N; ++i) {
                    x_[i] = xBackup_[i] + dx_[i] * step;
                    if (collider_ && w_[i] != 0 && !al) {  // projected line search: evaluate the energy on the feasible side of the body
                        Vec3 nrm;
                        Real phi = collider_->eval(x_[i], nrm) - radius_[i];
                        const Real lim = (skin && skinK_[i] > 0) ? p_.skinMaxDepth : Real(0);
                        const Real phiR = phi + lim;
                        if (phiR < 0) { x_[i] -= nrm * phiR; phi = -lim; projected = true; }
                        if (lim > 0 && phi < 0) Eskin += 0.5 * skinK_[i] * phi * phi;
                    }
                }
                if (barrier && projected) {
                    // the projection bends the straight path: check the actual motion xBackup -> trial again
                    std::vector<ContactPair> sw;
                    if (barrierMaxStep(xBackup_, x_, sw) < 1) { ++bstats_.trialRejects; step *= 0.5; continue; }
                }
                const Real E1 = variationalEnergy(invH2, seamScale) + Eskin + (barrier ? barrierEnergy(x_, bSwept_) : 0) +
                                (friction ? frictionEnergy(h) : 0) + (al ? alEnergy() : 0);
                if (std::isfinite(E1) && E1 <= E0) { accepted = true; break; }
                step *= 0.5;
            }
            if (!accepted && p_.hessianAuto && !switched) {
                // no step length lowers the energy with this Hessian model: retry from the same point with the other one
                x_ = xBackup_;
                autoTension_ = !autoTension_;
                switched = true;
                nMax = n + 1 + std::max(1, p_.newtonIterations);
                needFactor = true;
                ++cdiag_.hessianSwitches;
                ++cdiag_.lsRecovered;
                continue;
            }
            if (!accepted) {
                if (std::getenv("FL_DEBUG") && lineSearchFailures_ < 3) {
                    Real g2 = 0, d2 = 0, rd = 0;
                    for (int i = 0; i < N; ++i) { g2 += length2(rhs_[i]); d2 += length2(dx_[i]); rd += dot(rhs_[i], dx_[i]); }
                    std::fprintf(stderr, "[ls-fail frame %d] E0=%.6e |rhs|=%.3e |dx|=%.3e rhs.dx=%.3e cg=%d\n", frame_, E0, std::sqrt(g2), std::sqrt(d2), rd, cg);
                    for (Real st : {1.0, 0.1, 1e-2, 1e-3, 1e-4, 1e-6}) {
                        for (int i = 0; i < N; ++i) x_[i] = xBackup_[i] + dx_[i] * st;
                        std::fprintf(stderr, "    step %.0e: dE = %.3e\n", st, variationalEnergy(invH2, seamScale) - E0);
                    }
                }
                x_ = xBackup_;
                ++lineSearchFailures_;
                lsFailed = true;
                break;
            }
            Real maxDx = 0;
            for (int i = 0; i < N; ++i) maxDx = std::max(maxDx, length(x_[i] - xBackup_[i]));
            if (diag && dxTraceFrame_ == frame_) {
                int act = 0, lifts = 0, flips = 0, argmax = 0;
                Real best = -1;
                for (int i = 0; i < N; ++i) {
                    act += contactActive_[i];
                    lifts += contactPhi_[i] == 0;
                    const Real d = length(x_[i] - xBackup_[i]);
                    if (d > best) { best = d; argmax = i; }
                }
                (void)flips;
                std::fprintf(stderr, "[trace f%d s%d n%2d] newton dx %.3e  step %.4f  moved %.3e  held %d  on-surface %d  argmax v%d held=%d phi=%.2e\n",
                             frame_, s, n, lastDx, step, maxDx, act, lifts, argmax, contactActive_[argmax], contactPhi_[argmax]);
            }
            if (p_.hessianAuto) {
                // Model too soft (Gauss-Newton on a taut span): the line search keeps cutting the step.
                // Model too stiff (tension term on a bending-dominated strip): full steps, but the update does not shrink.
                const Real newtonDx = lastDx;  // size of this iteration's Newton update (before the line search)
                cutStreak = step < 1.0 ? cutStreak + 1 : 0;
                fullStreak = step >= 1.0 ? fullStreak + 1 : 0;
                for (int k = 3; k > 0; --k) newtonHist[k] = newtonHist[k - 1];
                newtonHist[0] = newtonDx;
                // stiff-model stall: three full steps and the update has not halved over those three iterations
                const bool tooSoft = !autoTension_ && cutStreak >= 3;
                const bool tooStiff = autoTension_ && fullStreak >= 3 && newtonDx > 0.5 * newtonHist[3];
                if (switched && (tooSoft || tooStiff)) newStalled = true;  // the switched-to model stalls as well
                if (!switched && (tooSoft || tooStiff) && newtonDx > 30 * p_.newtonTolerance) {
                    autoTension_ = !autoTension_;
                    switched = true;
                    nMax = n + 1 + std::max(1, p_.newtonIterations);
                    needFactor = true;
                    ++cdiag_.hessianSwitches;
                    cutStreak = fullStreak = 0;
                    for (Real& v : newtonHist) v = 1e30;
                }
            }
            lastDx = maxDx;
            if (maxDx < p_.newtonTolerance) { converged = true; break; }
            if (step < 1.0 || maxDx > 0.5 * prevMaxDx) needFactor = true;  // chord stalled: refresh the Hessian
            prevMaxDx = maxDx;
        }
        // hessianAuto: keep the switched-to model for the next substep unless it showed its own stall signature too
        // (requiring full convergence threw it away whenever the iteration budget was small, e.g. the editor's 8)
        if (p_.hessianAuto && switched && !converged && newStalled) autoTension_ = !autoTension_;
        nBase = nUsed;
        if (!al) break;
        // contactAL: first-order multiplier update, lambda <- max(0, lambda - rho g); re-solve while too deep
        Real maxPen = 0;
        for (int i = 0; i < N; ++i) {
            if (w_[i] == 0) continue;
            Vec3 nrm;
            const Real g = collider_->eval(x_[i], nrm) - radius_[i];
            maxPen = std::max(maxPen, -g);
            alLambda_[i] = std::max(Real(0), alLambda_[i] - alRho_[i] * g);
            cdiag_.alMaxLambda = std::max(cdiag_.alMaxLambda, double(alLambda_[i]));
        }
        ++cdiag_.alOuter;
        if (maxPen <= p_.contactALTolerance || outer + 1 >= std::max(1, p_.contactALOuter)) {
            cdiag_.alMaxPenetration = std::max(cdiag_.alMaxPenetration, double(maxPen));
            break;
        }
        }  // outer (contactAL)
        lastCgIterations_ = cg;
        newtonIterations_ += nUsed;
        ++substepsDone_;
        if (std::getenv("FL_NEWTON")) std::fprintf(stderr, "[newton frame %d] iters=%d lastMaxDx=%.2e\n", frame_, nUsed, lastDx);
        if (diag) {
            ++cdiag_.substeps;
            cdiag_.newtonIters += nUsed;
            if (lsFailed) ++cdiag_.lsFail;
            else if (!converged) { ++cdiag_.iterCapHits; cdiag_.sumDxAtCap += lastDx; }
            int act = 0;
            for (int i = 0; i < N; ++i) {
                if (contactActive_[i]) { ++act; if (contactPhi_[i] > 0) ++cdiag_.activeBandOnly; }
                cdiag_.flipsSubstep += contactActive_[i] != prevSubActive_[i];
            }
            prevSubActive_.assign(contactActive_.begin(), contactActive_.end());
            cdiag_.activeSum += act;
            cdiag_.lastActive = act;
            cdiag_.lastIters = nUsed;
            cdiag_.lastMaxDx = lastDx;
            cdiag_.lastKeNewton = stepKE(h);
            cdiag_.keNewton += cdiag_.lastKeNewton;
        }

        if (p_.strainLimit > 0) limitStrain();
        if (barrier) {
            // body friction / push-out after the solve, cut short where it would carry a primitive through another;
            // no vertex-triangle projection (the barrier replaces it)
            xLift_ = x_;
            solveBody();
            std::vector<Vec3> target = x_;
            x_ = xLift_;
            ccdMove(target);
            Real dmin = 1e30;
            barrier_.potential(x_, bCand_, kappa_, nullptr, &dmin);
            bstats_.minDistance = std::min(bstats_.minDistance, dmin);
            barrierAdapt(dmin);
        } else if (al) {
            // contactAL: the solve keeps the cloth within contactALTolerance of the body: no push-out; friction is the
            // in-solve mode 2 (bodyFrictionMode 0 has no friction here). Self collision can push a vertex into the body:
            // project those back.
            if (diag) { cdiag_.lastKeBody = stepKE(h); cdiag_.keBody += cdiag_.lastKeBody; }
            if (p_.selfCollision) {
                solveSelfCollision();
                for (int i = 0; i < N; ++i) {
                    if (w_[i] == 0) continue;
                    Vec3 nrm;
                    const Real g = collider_->eval(x_[i], nrm) - radius_[i];
                    if (g < -p_.contactALTolerance) x_[i] -= nrm * (g + p_.contactALTolerance);
                }
            }
            if (diag) { cdiag_.lastKeSelf = stepKE(h); cdiag_.keSelf += cdiag_.lastKeSelf; }
        } else {
            solveBody();
            if (diag) { cdiag_.lastKeBody = stepKE(h); cdiag_.keBody += cdiag_.lastKeBody; }
            if (p_.selfCollision) {
                solveSelfCollision();
                solveBody();
            }
            if (diag) { cdiag_.lastKeSelf = stepKE(h); cdiag_.keSelf += cdiag_.lastKeSelf; }
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

// Strain limiting (Provot 1995): after the solve, every mesh edge longer than (1 + strainLimit) times its rest length
// is shortened back to that, the correction shared by inverse mass, over strainLimitPasses Gauss-Seidel passes. The
// velocity update that follows turns the correction into velocity, so the limit also removes the stretching motion.
// An inexact solve (capped CG) leaves stretch unresolved where loads concentrate (a pinned corner carries the cloth's
// weight); this pass keeps those spots from running away. Off unless strainLimit > 0.
void ClothSolver::limitStrain() {
    if (slEdges_.empty()) {
        // Rest length per mesh edge from the triangles' own rest shapes (CROCHET_PLAN §2.1 step 6): restUV_ is only
        // a layout parameterisation for an intrinsic panel, so the edge vector must come from triRest_. For panel
        // triangles the map value equals length(restUV_[a] - restUV_[b]) bit for bit (the same two numbers, and a
        // negated difference has an identical length), so panel behaviour is unchanged. restLen keys are packed
        // (min << 32 | max) like cloth.cpp's pairKey; the first triangle visiting an edge wins.
        std::unordered_map<std::uint64_t, Real> restLen;
        std::vector<std::pair<int, int>> e;
        for (size_t t = 0; t < tris_.size(); ++t)
            for (int k = 0; k < 3; ++k) {
                int a = tris_[t][k], b = tris_[t][(k + 1) % 3];
                const Real l = length(triRest_[t][k] - triRest_[t][(k + 1) % 3]);
                if (a > b) std::swap(a, b);
                e.push_back({a, b});
                const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) << 32) |
                                          static_cast<std::uint32_t>(b);
                restLen.emplace(key, l);
            }
        std::sort(e.begin(), e.end());
        e.erase(std::unique(e.begin(), e.end()), e.end());
        for (const auto& pr : e) {
            const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(pr.first)) << 32) |
                                      static_cast<std::uint32_t>(pr.second);
            const auto it = restLen.find(key);
            const Real l0 = it != restLen.end() ? it->second : length(restUV_[pr.first] - restUV_[pr.second]);
            if (l0 > 1e-9) slEdges_.push_back({pr.first, pr.second, l0});
        }
    }
    const Real s = 1 + p_.strainLimit;
    for (int pass = 0; pass < std::max(1, p_.strainLimitPasses); ++pass) {
        bool any = false;
        for (const SlEdge& e : slEdges_) {
            const Real wa = w_[e.a], wb = w_[e.b];
            if (wa + wb == 0) continue;
            const Vec3 d = x_[e.b] - x_[e.a];
            const Real l = length(d);
            const Real lmax = s * e.l0;
            if (l <= lmax) continue;
            const Vec3 c = d * ((l - lmax) / (l * (wa + wb)));
            x_[e.a] += c * wa;
            x_[e.b] -= c * wb;
            any = true;
        }
        if (!any) break;
    }
}

}  // namespace fl
