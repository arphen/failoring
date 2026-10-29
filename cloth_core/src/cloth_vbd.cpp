// Vertex Block Descent (Chen, Huang, Ye, Macklin, Liu et al., "Vertex Block Descent", ACM TOG 43(4), SIGGRAPH 2024).
//
// The step minimises the same implicit-Euler energy as the Newton solver (cloth_implicit.cpp):
//     sum_i m_i / (2 h^2) |x_i - y_i|^2  +  stretch + shear + bending + seams  +  body contact  +  friction
// with y the inertial prediction. Instead of one Newton step on all vertices at once (a sparse system, a Cholesky
// factorisation: sequential work), it does Gauss-Seidel over vertices: vertex i takes the Newton step of the energy with
// every other vertex fixed, a 3x3 solve
//     H_i dx = f_i,   f_i = -m_i/h^2 (x_i - y_i) - sum_e dE_e/dx_i,   H_i = m_i/h^2 I + sum_e d2E_e/dx_i2
// with the per-element blocks of the Gauss-Newton model (the same terms assembleEnergy puts on the diagonal). Vertices
// that share no element (a graph colouring of the vertex-element adjacency) are independent and solved in parallel; the
// colours run one after the other. This is the shape a GPU kernel needs (one thread per vertex per colour).
//
// Body contact is a quadratic penalty on the signed distance g = phi(x) - r below 0, stiffness k = vbdContactStiffness
// * m / h^2 (the contactAL scaling: penetration under the cloth's own weight is a micrometre). Friction is the smoothed
// Coulomb term of cloth_friction.cpp (IPC f1 with slip speed frictionSlipVelocity) with the penalty force at the start of
// the substep as the normal force, on the motion relative to the body surface (colliderDisp_).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "failoring/cloth.hpp"

namespace fl {

namespace {

// 3x3 symmetric positive definite solve; false when singular or not finite
bool solve3(const Mat3& H, const Vec3& f, Vec3& out) {
    out = H.inverse() * f;
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

// runs fn(i) for i in [0, n) on `threads` threads (in place when 1 or n small)
template <class Fn>
void parallelFor(int n, int threads, Fn&& fn) {
    if (threads <= 1 || n < 64) {
        for (int i = 0; i < n; ++i) fn(i);
        return;
    }
    std::atomic<int> next{0};
    const int chunk = std::max(16, n / (threads * 4));
    auto work = [&]() {
        for (;;) {
            const int s = next.fetch_add(chunk);
            if (s >= n) return;
            const int e = std::min(n, s + chunk);
            for (int i = s; i < e; ++i) fn(i);
        }
    };
    std::vector<std::thread> pool;
    pool.reserve(threads - 1);
    for (int t = 1; t < threads; ++t) pool.emplace_back(work);
    work();
    for (auto& t : pool) t.join();
}

}  // namespace

void ClothSolver::vbdPrepare() {
    const int N = vertexCount();
    std::vector<std::vector<VbdRef>> adj(N);
    for (int e = 0; e < static_cast<int>(triC_.size()); ++e) {
        const TriC& c = triC_[e];
        adj[c.i0].push_back({0, e, 0});
        adj[c.i1].push_back({0, e, 1});
        adj[c.i2].push_back({0, e, 2});
    }
    for (int e = 0; e < static_cast<int>(bendC_.size()); ++e)
        for (int k = 0; k < 4; ++k) adj[bendC_[e].i[k]].push_back({1, e, k});
    for (int e = 0; e < static_cast<int>(seamC_.size()); ++e) {
        adj[seamC_[e].a].push_back({2, e, 0});
        adj[seamC_[e].b0].push_back({2, e, 1});
        adj[seamC_[e].b1].push_back({2, e, 2});
    }
    vbdAdjStart_.assign(N + 1, 0);
    vbdAdj_.clear();
    for (int i = 0; i < N; ++i) {
        vbdAdjStart_[i] = static_cast<int>(vbdAdj_.size());
        vbdAdj_.insert(vbdAdj_.end(), adj[i].begin(), adj[i].end());
    }
    vbdAdjStart_[N] = static_cast<int>(vbdAdj_.size());
    // greedy colouring: two vertices that share an element get different colours
    auto members = [&](const VbdRef& r, int* out) -> int {
        if (r.type == 0) { const TriC& c = triC_[r.elem]; out[0] = c.i0; out[1] = c.i1; out[2] = c.i2; return 3; }
        if (r.type == 1) { for (int k = 0; k < 4; ++k) out[k] = bendC_[r.elem].i[k]; return 4; }
        const SeamC& s = seamC_[r.elem]; out[0] = s.a; out[1] = s.b0; out[2] = s.b1; return 3;
    };
    std::vector<int> color(N, -1);
    std::vector<int> used;
    int ncol = 0;
    for (int i = 0; i < N; ++i) {
        used.assign(ncol + 1, 0);
        int nb[4];
        for (int k = vbdAdjStart_[i]; k < vbdAdjStart_[i + 1]; ++k) {
            const int m = members(vbdAdj_[k], nb);
            for (int q = 0; q < m; ++q)
                if (nb[q] != i && color[nb[q]] >= 0) used[color[nb[q]]] = 1;
        }
        int c = 0;
        while (c < ncol && used[c]) ++c;
        color[i] = c;
        if (c == ncol) ++ncol;
    }
    vbdColors_.assign(ncol, {});
    for (int i = 0; i < N; ++i)
        if (w_[i] != 0) vbdColors_[color[i]].push_back(i);
    vbdLambda_.assign(N, 0.0);
    vbdReady_ = true;
}

void ClothSolver::vbdSolveVertex(int i, Real invH2, Real h, Real seamScale) {
    const Real m = mass_[i];
    Vec3 f = (xTilde_[i] - x_[i]) * (m * invH2);
    Mat3 H;
    H.setZero();
    H.addDiag(m * invH2);
    auto add = [&](const Vec3& g, Real C, Real kappa) {   // energy 1/2 kappa C^2, dC/dx_i = g
        f -= g * (kappa * C);
        H.addOuter(g, g, kappa);
    };
    for (int k = vbdAdjStart_[i]; k < vbdAdjStart_[i + 1]; ++k) {
        const VbdRef& r = vbdAdj_[k];
        if (r.type == 0) {
            const TriC& c = triC_[r.elem];
            const Vec3 d1 = x_[c.i1] - x_[c.i0], d2 = x_[c.i2] - x_[c.i0];
            const Vec3 wu = d1 * c.a + d2 * c.c;
            const Vec3 wv = d1 * c.b + d2 * c.d;
            const Real bu = r.local == 0 ? -(c.a + c.c) : (r.local == 1 ? c.a : c.c);   // d wu / d x_local
            const Real bv = r.local == 0 ? -(c.b + c.d) : (r.local == 1 ? c.b : c.d);
            const Real lu = length(wu), lv = length(wv);
            if (lu > 1e-12) {
                const Real C = lu - 1;
                add(wu / lu * bu, C, (C < 0 ? c.comp : 1.0) / c.alphaU);
            }
            if (lv > 1e-12) {
                const Real C = lv - 1;
                add(wv / lv * bv, C, (C < 0 ? c.comp : 1.0) / c.alphaV);
            }
            add(wv * bu + wu * bv, dot(wu, wv), 1.0 / c.alphaS);   // shear C = wu . wv
        } else if (r.type == 1) {
            const BendC& b = bendC_[r.elem];
            Vec3 g[4];
            const Real th = detail::hingeAngle(x_[b.i[0]], x_[b.i[1]], x_[b.i[2]], x_[b.i[3]], g);
            if (th >= 0) add(g[r.local], th, 1.0 / b.alpha);
        } else {
            const SeamC& s = seamC_[r.elem];
            const Vec3 d = x_[s.a] - (x_[s.b0] * (1 - s.t) + x_[s.b1] * s.t);
            const Real k = seamScaleOf(s, seamScale) / s.alpha;
            const Real wl = r.local == 0 ? 1.0 : (r.local == 1 ? -(1 - s.t) : -s.t);
            f -= d * (k * wl);
            H.addDiag(k * wl * wl);
        }
    }
    // body: penalty below the contact radius, and friction with the penalty force of the start of the substep
    if (collider_) {
        Vec3 n;
        const Real g = collider_->eval(x_[i], n) - radius_[i];
        const Real kc = p_.vbdContactStiffness * m * invH2;
        if (g < 0) {
            f += n * (-kc * g);
            H.addOuter(n, n, kc);
        }
        const Real lam = vbdLambda_[i];
        const Real mu = muK_[i];
        if (lam > 0 && mu > 0) {
            Vec3 u = x_[i] - xPrev_[i];
            if (colliderDisp_.size() == x_.size()) u = u - colliderDisp_[i] / std::max(1, p_.substeps);
            const Vec3 ut = u - n * dot(u, n);
            const Real ul = length(ut);
            const Real eps = std::max(p_.frictionSlipVelocity * h, Real(1e-9));
            // IPC smoothed friction: force magnitude mu*lam*f1(|u|/eps), f1(y) = 2y - y^2 below 1, 1 above
            const Real y = ul / eps;
            const Real f1_over_u = y < 1 ? (2.0 - y) / eps : 1.0 / std::max(ul, Real(1e-12));
            f -= ut * (mu * lam * f1_over_u);
            // tangent-plane stiffness (PSD): mu lam f1/|u| (I - n n^T)
            const Real kt = mu * lam * f1_over_u;
            H.addDiag(kt);
            H.addOuter(n, n, -kt);
        }
    }
    Vec3 dx;
    if (!solve3(H, f, dx)) return;
    const Real dl = length(dx);
    if (dl > 0.05) dx *= 0.05 / dl;   // a vertex moves at most 5 cm per sweep (a guard against a bad local model)
    // backtracking on the vertex's own energy: the Gauss-Newton model can overshoot a stiff, curved term (a taut yarn
    // turning, a hinge near flat), and an increase would feed energy into the cloth (measured: without it a hanging
    // cotton patch never settles)
    const Vec3 x0 = x_[i];
    const Real e0 = vbdLocalEnergy(i, invH2, seamScale);
    Real step = 1.0;
    for (int ls = 0; ls < 8; ++ls) {
        x_[i] = x0 + dx * step;
        if (vbdLocalEnergy(i, invH2, seamScale) <= e0 + 1e-15) return;
        step *= 0.5;
    }
    x_[i] = x0;
}

// The energy terms that depend on vertex i (inertia, its elements, the body penalty), at the current positions.
Real ClothSolver::vbdLocalEnergy(int i, Real invH2, Real seamScale) const {
    Real E = 0.5 * mass_[i] * invH2 * length2(x_[i] - xTilde_[i]);
    for (int k = vbdAdjStart_[i]; k < vbdAdjStart_[i + 1]; ++k) {
        const VbdRef& r = vbdAdj_[k];
        if (r.type == 0) {
            const TriC& c = triC_[r.elem];
            const Vec3 d1 = x_[c.i1] - x_[c.i0], d2 = x_[c.i2] - x_[c.i0];
            const Vec3 wu = d1 * c.a + d2 * c.c, wv = d1 * c.b + d2 * c.d;
            const Real Cu = length(wu) - 1, Cv = length(wv) - 1, Cs = dot(wu, wv);
            E += 0.5 * (Cu < 0 ? c.comp : 1.0) / c.alphaU * Cu * Cu;
            E += 0.5 * (Cv < 0 ? c.comp : 1.0) / c.alphaV * Cv * Cv;
            E += 0.5 / c.alphaS * Cs * Cs;
        } else if (r.type == 1) {
            const BendC& b = bendC_[r.elem];
            const Real th = detail::hingeAngle(x_[b.i[0]], x_[b.i[1]], x_[b.i[2]], x_[b.i[3]], nullptr);
            if (th >= 0) E += 0.5 / b.alpha * th * th;
        } else {
            const SeamC& s = seamC_[r.elem];
            const Vec3 d = x_[s.a] - (x_[s.b0] * (1 - s.t) + x_[s.b1] * s.t);
            E += 0.5 * seamScaleOf(s, seamScale) / s.alpha * length2(d);
        }
    }
    if (collider_) {
        Vec3 n;
        const Real g = collider_->eval(x_[i], n) - radius_[i];
        if (g < 0) E += 0.5 * p_.vbdContactStiffness * mass_[i] * invH2 * g * g;
    }
    return E;
}

void ClothSolver::stepVBD() {
    if (!vbdReady_) vbdPrepare();
    const int S = std::max(1, p_.substeps);
    const Real h = p_.dt / S;
    const Real invH2 = 1.0 / (h * h);
    const int N = vertexCount();
    const Real seamScale = seamScaleNow();
    const int threads = p_.vbdThreads > 0 ? p_.vbdThreads : std::max(1u, std::thread::hardware_concurrency());
    if (vbdAccel_.size() != static_cast<size_t>(N)) vbdAccel_.assign(N, Vec3(0, 0, 0));
    vbdVPrev_.resize(N);
    for (int s = 0; s < S; ++s) {
        for (int i = 0; i < N; ++i) {
            xPrev_[i] = x_[i];
            if (w_[i] == 0) { v_[i] = Vec3(0, 0, 0); xTilde_[i] = x_[i]; continue; }
            const Vec3 v0 = v_[i] * std::exp(-damp_[i] * h);
            v_[i] = v0 + p_.gravity * h;
            xTilde_[i] = x_[i] + v_[i] * h;
            // initial guess (VBD paper sec. 3.5, "adaptive initialisation"): the inertial prediction y would start
            // every vertex a full free-fall step down, which an unconverged sweep count cannot pull back up (the cloth
            // stretches and gains energy); x + h v0 + h^2 a~ with a~ the part of the previous substep's acceleration
            // along gravity, clamped to [0, |g|], falls only as fast as the vertex actually fell last time
            Vec3 at(0, 0, 0);
            const Real g2 = length2(p_.gravity);
            if (g2 > 0 && vbdAccel_.size() == static_cast<size_t>(N)) {
                const Vec3 gh = p_.gravity / std::sqrt(g2);
                const Real ag = std::min(std::max(dot(vbdAccel_[i], gh), Real(0)), std::sqrt(g2));
                at = gh * ag;
            }
            x_[i] = p_.vbdInit == 1 ? x_[i] + v0 * h + at * (h * h) : xTilde_[i];
            vbdVPrev_[i] = v0;
        }
        // friction normal force: the penalty force at the start of the substep (at the previous positions)
        if (collider_ && p_.bodyFrictionMode == 2)
            for (int i = 0; i < N; ++i) {
                Vec3 n;
                const Real g = w_[i] != 0 ? collider_->eval(xPrev_[i], n) - radius_[i] : 1.0;
                // a vertex resting on the body sits slightly inside the penalty (its weight balanced by it); the force
                // there is kc * depth. Contacts within 1 mm count with the force their weight would need at least.
                vbdLambda_[i] = g < 0 ? p_.vbdContactStiffness * mass_[i] * invH2 * (-g) : 0.0;
            }
        else
            std::fill(vbdLambda_.begin(), vbdLambda_.end(), 0.0);
        static const bool dbg = std::getenv("FL_VBD_DEBUG") != nullptr;
        static const int dbgFrom = dbg ? std::atoi(std::getenv("FL_VBD_DEBUG")) : 0;
        if (dbg && frame_ >= dbgFrom && frame_ < dbgFrom + 2 && s == 0) std::fprintf(stderr, "vbd frame %d start energy %.9e\n", frame_, variationalEnergy(invH2, seamScale));
        // Chebyshev semi-iterative acceleration (VBD paper section 3.6): x_k+1 = w (xbar_k+1 - x_k-1) + x_k-1, with the
        // spectral radius estimate rho (vbdRho; 0 turns it off)
        const Real rho = p_.vbdRho;
        Real omega = 1.0;
        vbdPrev_ = x_;
        std::vector<Vec3> cur;
        for (int it = 0; it < std::max(1, p_.vbdIterations); ++it) {
            if (rho > 0) cur = x_;
            for (const std::vector<int>& col : vbdColors_)
                parallelFor(static_cast<int>(col.size()), threads, [&](int k) { vbdSolveVertex(col[k], invH2, h, seamScale); });
            if (rho > 0) {
                omega = it == 0 ? 1.0 : (it == 1 ? 2.0 / (2.0 - rho * rho) : 4.0 / (4.0 - rho * rho * omega));
                if (it > 0)
                    for (int i = 0; i < N; ++i)
                        if (w_[i] != 0) x_[i] = vbdPrev_[i] + (x_[i] - vbdPrev_[i]) * omega;
                vbdPrev_ = cur;
            }
            if (dbg && frame_ >= dbgFrom && frame_ < dbgFrom + 2 && s == 0) std::fprintf(stderr, "vbd frame %d sweep %d energy %.9e\n", frame_, it, variationalEnergy(invH2, seamScale));
        }
        if (p_.selfCollision) solveSelfCollision();
        for (int i = 0; i < N; ++i) {
            if (w_[i] == 0) continue;
            v_[i] = (x_[i] - xPrev_[i]) / h;
            const Real sp = length(v_[i]);
            if (sp > 50.0) v_[i] *= 50.0 / sp;  // runaway guard
            vbdAccel_[i] = (v_[i] - vbdVPrev_[i]) / h;
        }
    }
    ++frame_;
}

}  // namespace fl
