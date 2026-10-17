#pragma once
// Failoring cloth core: XPBD ("small steps") cloth solver with
//  - per-triangle anisotropic warp/weft stretch + shear, driven by the flat 2D pattern
//    (so the grainline, bias cuts and knit/woven differences are physically meaningful)
//  - dihedral bending with warp/weft-dependent rigidity
//  - sewn seams between arbitrary pattern edges (unequal lengths allowed = ease)
//  - SDF body collision with Coulomb friction
//  - vertex-vs-triangle self collision (layers, folds)
//
// Units: metres, kilograms, seconds, newtons.
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "failoring/barrier_contact.hpp"
#include "failoring/collider.hpp"
#include "failoring/intrinsic_mesh.hpp"
#include "failoring/material.hpp"
#include "failoring/math.hpp"
#include "failoring/mesher.hpp"
#include "failoring/sparse_chol.hpp"

namespace fl {

namespace detail {
// Sign convention for the hinge-angle gradient; verified against finite differences in tests.
constexpr Real kHingeGradSign = -1.0;
// Dihedral bending angle (0 = flat) of a hinge: (x1,x2) is the shared edge, x3 and x4 the
// opposite vertices. Writes d(theta)/d(x1..x4) into g[4] when g != nullptr.
// Returns -1 for degenerate triangles.
Real hingeAngle(const Vec3& x1, const Vec3& x2, const Vec3& x3, const Vec3& x4, Vec3* g);
// Sign of the Bridson et al. (2003) gradient term relative to d(theta_s)/dx of hingeAngleSigned below; verified
// with central finite differences of theta_s itself in tests/test_intrinsic.cpp (crochet plan section 2.7 test 3).
constexpr Real kHingeSignedGradSign = 1.0;
// Signed dihedral bending angle of the same hinge, for hinges with a rest angle (crochet plan section 2.2). Same
// arguments as hingeAngle; the triangle normals are consistently oriented (both triangles listed counter-clockwise
// around the same side), so a flat pair gives 0 and a fully folded pair gives +-pi. The sign convention is the one
// documented at BendC::rest: a positive angle means the triangle whose opposite vertex is x4 is folded toward the
// side its own normal points to (equivalently, the side x3's consistently oriented normal points to when flat).
// Writes d(theta_s)/d(x1..x4) into g[4] when g != nullptr (sums to zero). Returns false on degeneracy (zero-length
// edge or zero-area triangle) with thetaOut = 0 and zero gradients; the caller skips the hinge.
bool hingeAngleSigned(const Vec3& x1, const Vec3& x2, const Vec3& x3, const Vec3& x4, Real& thetaOut, Vec3* g);

// J-curve stretch above the lock strain (crochet plan 2.3): energy, dE/dC and the Gauss-Newton
// stiffness factor for C > lock > 0. Below the lock the caller keeps the plain 0.5 k C^2 path.
// Only valid for C > lock > 0 (at C = lock the formula is still C1 with the plain parabola); asserting
// that is the callers' job. k = 1/alpha is the unlocked (linear) stiffness, ratio >= 1 the stiffness
// multiplier above the lock. Shared by assembleEnergy, variationalEnergy and tests, so they cannot drift.
struct JCurveStretch { Real e, dedc, kappa; };
inline JCurveStretch jCurveStretch(Real C, Real k, Real lock, Real ratio) {
    const Real cl = C - lock;
    return {0.5 * k * lock * lock + k * lock * cl + 0.5 * ratio * k * cl * cl,  // e, C1 at C = lock
            k * lock + ratio * k * cl,                                          // dE/dC
            ratio * k};                                                         // kappa (Gauss-Newton)
}
}  // namespace detail

enum class LinearSolver {
    Cholesky,          // sparse direct (nested-dissection ordering); robust, independent of conditioning. Default.
    ConjugateGradient, // matrix-free-ish PCG; kept as fallback and for very large meshes
    CholeskyPCG        // PCG preconditioned by a (possibly stale) sparse Cholesky factor: refactor only every few solves
};

enum class Integrator {
    // Implicit Euler solved with Newton + preconditioned conjugate gradients on the true
    // constraint energies. Reaches the real static equilibrium (accurate drape, sag, fit)
    // independent of iteration counts. Default.
    Implicit,
    // Position-based dynamics (XPBD). Cheap and robust for interactive dragging, but on stiff,
    // finely meshed cloth the static shape is iteration-limited (too soft). Experimental here.
    XPBD,
    // Vertex Block Descent (Chen et al., SIGGRAPH 2024; cloth_vbd.cpp): the same implicit-Euler energy as Implicit
    // (same stretch, shear, bending and seam terms), minimised by Gauss-Seidel over vertices: each vertex solves its own
    // 3x3 Newton step with its neighbours fixed, vertices of one colour (no shared element) in parallel. No global
    // linear system, so it maps to threads and to the GPU. Body contact is a per-vertex quadratic penalty, friction the
    // smoothed Coulomb term with the penalty force as normal force.
    VBD
};

struct SolverParams {
    Integrator integrator = Integrator::Implicit;
    LinearSolver linearSolver = LinearSolver::Cholesky;
    Real dt = 1.0 / 60.0;     // one frame
    int substeps = 2;         // substeps per frame (implicit: 1-4 is typical; XPBD: 10-30)
    int newtonIterations = 12;   // implicit: max Newton steps per substep (stops early once converged)
    bool reuseFactorization = false;  // chord Newton: factor once per step, reuse for later iterations
    Real newtonTolerance = 1e-6;  // implicit: stop when the largest vertex update is below this (m)
    int cgMaxIterations = 2000; // implicit: conjugate-gradient cap per Newton step
    int cholMaxAge = 6;         // CholeskyPCG: refactor after this many solves with the same factor
    Real cgTolerance = 1e-5;    // implicit: relative residual target
    // Strain limiting after each substep's solve: edges longer than (1 + strainLimit) x rest are shortened (0 = off).
    Real strainLimit = 0.0;
    int strainLimitPasses = 4;
    bool cgWarmStart = false;   // implicit CG: start from the previous solve's dx instead of zero
    bool cgBlockJacobi = false; // implicit CG: 3x3 block-Jacobi preconditioner instead of SSOR (parallel; the GPU path)
    // Speed/accuracy trade-off: clamp in-plane stiffness (tension and shear, N/m) to this value in the
    // solver. 0 = off (exact material values). The condition number of the implicit system grows
    // linearly with stiffness, so a cap of a few hundred N/m makes preview drapes several times faster
    // while loads from cloth weight alone still strain the fabric by well under 1%. Do not use it for
    // tight-fit/stretch analysis (knits on a body), where the true stiffness is the point.
    Real stretchStiffnessCap = 0.0;
    int iterations = 1;       // XPBD only: constraint passes per substep
    int vbdIterations = 20;   // VBD: Gauss-Seidel sweeps over all vertices per substep
    Real vbdContactStiffness = 1000.0;  // VBD: body penalty k = this * m / h^2 per vertex (as contactALStiffness)
    int vbdInit = 1;          // VBD initial guess: 0 = inertial prediction y, 1 = adaptive (paper sec. 3.5)
    Real vbdRho = 0.0;        // VBD: Chebyshev acceleration spectral radius estimate (0 = off; the paper uses ~0.9)
    int vbdThreads = 0;       // VBD: worker threads (0 = hardware concurrency, 1 = serial)
    Vec3 gravity{0, -9.81, 0};
    Real seamStiffness = 20000.0;  // N/m of seam length (thread + construction)
    int seamRampFrames = 0;        // >0: seams fade in over this many frames (helps assembling far-apart panels)
    // Multiplied into damp_ in finalize() (CROCHET_PLAN §6.9: the zero-g relaxation of an assembled layout uses
    // x5). 1.0 leaves every existing result bit-identical.
    Real dampingScale = 1.0;
    Real contactOffset = 0.0015;   // m, extra clearance cloth <-> body
    Real contactBand = 1e-4;       // m, implicit: a vertex this close to the body and pushed towards it is held (not released)
    // Implicit: include the (PSD) tension part of the stretch Hessian (off in the core, on in the app). Without it Newton
    // has no stiffness across a taut span and never converges on a ring bridging a gap (the "wedge jitter"); with it,
    // Newton stops converging on a bending-dominated, nearly unstretched strip (test_cantilever fails 5 of 5). Both
    // measured 2026-10-14, docs/handoff/OPUS_SESSION_1.md. Changes convergence, so it changes the results of runs that
    // do not converge.
    bool tensionStiffness = false;
    // Choose the Hessian model per substep (implicit only; overrides tensionStiffness). Default since 2026-10-14.
    // Gauss-Newton is too soft on taut spans (the line search keeps cutting the step: wedge jitter), the tension term
    // is too stiff on bending-dominated, nearly unstretched cloth (full steps that do not shrink: test_cantilever fails).
    // Each substep starts with the model that last converged (Gauss-Newton at first); on three cut steps (Gauss-Newton)
    // or three full steps without halving (tension) it switches once, with a fresh iteration budget, and keeps the new
    // model only if it converged. Measured in docs/handoff/OPUS_SESSION_1.md.
    bool hessianAuto = true;
    bool selfCollision = true;
    Real selfThickness = 0.002;    // m, vertex-triangle contact distance
    // K1 compliant skin (implicit integrator only; see docs/agent_notes/K1_compliant_skin.md). A cloth vertex whose
    // offset-corrected signed distance to the body is phi < 0 sinks delta = -phi into a Winkler layer; energy per
    // unit area 0.5 * k * delta^2, k = skinStiffness * Collider::skinScale(x). Beyond skinMaxDepth the rigid
    // projection takes over. Both must be > 0 to enable the layer; the defaults (0) are the old rigid behaviour.
    // UNVERIFIED placeholder physics: no tissue stiffness value in this code base is measured.
    Real skinStiffness = 0.0;      // Pa/m (= N/m^3)
    Real skinMaxDepth = 0.0;       // m, hard stop of the soft layer
    // K5 (docs/K5_BARRIER_CONTACT.md): IPC-style barrier self contact inside the implicit Newton solve. Point-triangle
    // AND edge-edge pairs get a smooth barrier energy (gradient + PSD-projected Hessian assembled into the Newton
    // system; the Cholesky pattern is rebuilt when new pairs appear), and every line-search trial is capped by
    // conservative continuous collision detection, so no step crosses a cloth surface. Replaces the post-solve
    // vertex-triangle projection (selfCollision is ignored while this is on). Implicit integrator only.
    // Requires an intersection-free start. Off by default: existing behaviour is unchanged.
    bool barrierContact = false;
    contact::BarrierParams barrier;  // d_hat (gap), stiffness, CCD settings; see barrier_contact.hpp

    // K2 (docs/handoff/OPUS_SESSION_1.md): contact diagnostics and experiment switches. Defaults = old behaviour.
    bool contactDiagnostics = false;  // fill ClothSolver::contactDiag() (costs a few extra passes per substep)
    // Body friction: 0 = old (position correction after the solve, budget mu * penetration left after the solve, which
    // the in-Newton projection has already removed: nearly frictionless), 1 = none (diagnostic), 2 = smoothed Coulomb
    // potential inside the Newton solve with lagged normal force (cloth_friction.cpp, implicit integrator). Mode 2 is
    // right on an incline (test_friction) and stills the wedge, but with the rigid hold its lagged normal force is
    // noisy and a wedged tube on a narrow-waist body shows growing stick-slip bursts (~0.1 mm/frame), so it is not the
    // default yet; with the K1 soft skin (smooth normal force) it settles. docs/handoff/OPUS_SESSION_1.md.
    int bodyFrictionMode = 0;
    // m/s, mode 2: slip speed below which a contact counts as sticking (smoothing). A contact held below the friction
    // limit creeps at up to this speed, so it must be small: 1e-3 let a resting soft-skin tube creep 0.015 mm/frame.
    Real frictionSlipVelocity = 1e-5;

    // K2: body contact by persistent augmented-Lagrangian multipliers instead of projection (cloth_contact_al.cpp).
    // Each free vertex keeps a multiplier lambda_i >= 0, its normal force (N), warm-started across substeps; inside a
    // Newton solve the body adds the C1 term -lambda g + rho/2 g^2 (g = distance - contact radius, while
    // lambda - rho g > 0) to the energy, with no projection of iterates or trial points; after the solve
    // lambda <- max(0, lambda - rho g), and the solve repeats while a vertex is deeper than contactALTolerance.
    // rho_i = contactALStiffness * m_i / h^2 (scaled by inertia). Friction mode 2 then takes lambda as its normal force.
    // Requires skin and barrier off (ignored otherwise). Off by default. 1000 measured best on test_wedge (100: the
    // contact oscillates, 8-14x the kinetic energy; 10000: Newton hits its cap in half the substeps).
    bool contactAL = false;
    Real contactALStiffness = 1000.0;
    Real contactALTolerance = 1e-5;  // m
    int contactALOuter = 4;          // max Newton solves per substep
    bool bodyPushAfterSolve = true;   // false (diagnostic): skip the push-out after the solve (in-Newton hold only)
};

// Per-substep contact diagnostics (filled only with SolverParams::contactDiagnostics). Sums over all substeps since
// the start; the `last*` fields describe the most recent substep.
struct ContactDiag {
    long long substeps = 0, newtonIters = 0, iterCapHits = 0;  // cap hit = the substep used every allowed iteration without converging
    double sumDxAtCap = 0;          // sum of the last Newton update size (m) over cap hits
    long long activeSum = 0;        // held vertices in the last Newton iteration, summed over substeps
    long long activeBandOnly = 0;   // ... of which phi > 0 (held inside the band, not touching)
    long long flipsIter = 0;        // held/free changes between consecutive Newton iterations of one substep
    long long flipsSubstep = 0;     // changes of the final held set from one substep to the next
    long long lifts = 0;            // vertices projected out of the body at the start of a Newton iteration
    double liftDist = 0;            // total projection distance (m)
    long long liftsAfterFirstIter = 0;  // lifts in iterations 2.. (the Newton step itself went into the body)
    long long lsFail = 0;
    long long hessianSwitches = 0;  // hessianAuto: substeps that switched the Hessian model
    long long lsRecovered = 0;      // hessianAuto: line searches that failed and were retried with the other model
    long long frictionActive = 0;   // bodyFrictionMode 2: vertex-iterations with a friction term
    long long alOuter = 0;          // contactAL: Newton solves summed over substeps (1 per substep = no re-solve)
    double alMaxPenetration = 0;    // contactAL: deepest vertex after the last solve of a substep (m), max over the run
    double alMaxLambda = 0;         // contactAL: largest multiplier (N)
    long long pushAfter = 0, frictionAfter = 0;  // vertices moved by the post-solve push-out / friction
    double keNewton = 0, keBody = 0, keSelf = 0;  // KE of (x - xPrev)/h after Newton, after solveBody, after self collision (sums)
    double lastKeNewton = 0, lastKeBody = 0, lastKeSelf = 0;
    int lastActive = 0, lastIters = 0;
    double lastMaxDx = 0;
};

// ---- static equilibrium solve (cloth_static.cpp); everything here is opt-in via ClothSolver::solveStatic ----
struct StaticParams {
    // Final gravity load as a multiple of SolverParams::gravity (1 = the real weight).
    Real gravityScale = 1.0;
    // Gravity continuation: the load goes 0 -> gravityScale in about this many increments (adapted: halved on a
    // failed stage, grown on quick ones). Ignored for a warm start.
    int loadSteps = 4;
    // Pseudo-transient proximal term (alpha/2) sum m_i |x - x_anchor|^2, alpha in 1/s^2 (like 1/h^2 of a time step).
    // Cold start: alphaStart, reduced by alphaDecay per successful stage, set to exactly 0 below alphaMin.
    Real alphaStart = 3600.0;
    Real alphaWarm = 5.0;      // starting alpha of a warm start (small: the previous shape is already close)
    Real alphaDecay = 0.2;
    Real alphaMin = 0.05;
    // Stage = one regularised Newton solve (a pseudo time step) with at most this many Newton iterations.
    int stageNewton = 3;
    Real stageTolerance = 1e-4;   // m, a stage ends early once the largest Newton step is below this
    // Final convergence (alpha = 0, full load): scaled force imbalance AND largest vertex step per Newton iteration.
    Real residualTolerance = 1e-3;     // |filtered force| / |weight|, both in the 2-norm over free vertices
    Real displacementTolerance = 1e-5; // m
    int maxIterations = 1000;          // cap on Newton iterations (= numeric factorisations) over the whole solve
    bool warmStart = false;            // start from the current positions at full load with small alpha
    // Vertex-triangle self-collision projection after each static step (SolverParams::selfCollision must be on too).
    // Off by default: the projection is not part of the energy the line search checks, and with static step sizes it
    // scrambles a wedged ring (test_garment's tube: residual 1e4, hem 50 mm too low; converges without it).
    // So the static solve ignores self contact. Measured 2026-10-14, docs/handoff/OPUS_SESSION_1.md.
    bool selfCollision = false;
    bool verbose = false;
};

struct StaticResult {
    bool converged = false;
    // The static solve handles rigid body contact only. With the K1 skin or K5 barrier contact on it does nothing
    // and sets this (it would otherwise solve a different problem than step()).
    bool unsupported = false;
    int iterations = 0;       // Newton iterations (each one numeric Cholesky factorisation), all stages
    int stages = 0;           // pseudo-transient stages / load steps taken (successful ones)
    int failedStages = 0;     // stages rolled back (load increment halved or alpha raised)
    int finalIterations = 0;  // iterations spent in the last phase (alpha = 0, full load)
    Real residual = 0;        // scaled force imbalance at the end (see StaticParams::residualTolerance)
    Real maxStep = 0;         // largest vertex displacement of the last Newton iteration (m)
    Real energy = 0;          // physical potential energy at the end: stretch + shear + bending + seams - gravity work
    Real seconds = 0;         // wall time
    Real cpuSeconds = 0;      // process CPU time (steadier than wall time on a busy machine)
    int lineSearchFailures = 0;
    int symbolicAnalyses = 0; // sparse-Cholesky ordering/analyses done during the call (0 = pattern reused)
};

// Result of ClothSolver::checkStaticStability (path-dependence diagnostic).
struct StaticStability {
    bool converged = false;      // the perturbed re-solve converged
    Real maxShapeDiff = 0;       // m, largest vertex distance between the settled and the re-settled shape
    Real rmsShapeDiff = 0;       // m
    Real energyDiff = 0;         // J, re-settled minus original physical energy (negative: a lower equilibrium exists)
    Real relEnergyDiff = 0;
    int iterations = 0;
};

struct SeamSpec {
    int panelA, edgeA;
    int panelB, edgeB;
    bool reverse;  // false: start of A's chain meets start of B's chain
};

// The filtered implicit system  F (diag(massTerm) + K) F x = F b  (x restricted by the per-vertex filters F: I for a
// free vertex, 0 pinned, I - n n^T resting on the body), as handed to an external linear solver (the GPU conjugate
// gradient of the Godot extension). K is block-CSR with 3x3 blocks (row-major Mat3), columns sorted per row.
struct LinearSystemView {
    int n = 0;                       // vertices
    const int* rowStart = nullptr;   // n + 1
    const int* col = nullptr;        // rowStart[n] block columns
    const Mat3* val = nullptr;       // rowStart[n] blocks
    const int* diag = nullptr;       // index of the (i,i) block per row
    const Real* massTerm = nullptr;  // m_i / h^2 per vertex (added to the diagonal)
    const Mat3* filt = nullptr;      // per-vertex filter F
    const Vec3* rhs = nullptr;       // b
    int maxIterations = 0;
    Real tolerance = 0;              // stop when |r| <= tolerance |F b|
    int patternVersion = 0;          // changes whenever rowStart/col change (re-upload them)
};
// Returns the iterations used, or < 0 when it could not solve (the core then runs its own CG).
using ExternalLinearSolver = std::function<int(const LinearSystemView&, std::vector<Vec3>& x)>;

// The algorithm the GPU kernel runs, on the CPU in single precision with the same operation order: block-Jacobi
// preconditioned CG from x = 0, at most maxIterations, stopping on the relative residual. The reference the GPU
// results are checked against (and a fallback with the GPU's numerics).
int cgBlockJacobiFloat(const LinearSystemView& sys, std::vector<Vec3>& x);
int cgBlockJacobiDouble(const LinearSystemView& sys, std::vector<Vec3>& x);  // the same in double precision

class ClothSolver {
public:
    explicit ClothSolver(SolverParams p = {}) : p_(p) {}

    // Adds a pattern piece. `grain` is the angle (radians, CCW) of the warp yarn
    // direction relative to the panel's +u axis (0 = warp along +u, pi/2 = along +v, pi/4 = bias).
    // Returns the panel index.
    int addPanel(const PanelMesh& mesh, const FabricMaterial& mat, const PanelPlacement& place,
                 Real grain = 0, const std::string& name = {});

    // Adds a crochet-style intrinsic panel (intrinsic_mesh.hpp): every triangle carries its own rest shape
    // (m.triRest) and material (m.triMaterial indexes `mats`), with optional hinge modifiers, chains and lumped
    // extra mass. Vertices start at m.x0, so no placement is applied. `mats` are appended to the solver's material
    // table like addPanel's material. Returns the panel index. Throws std::runtime_error on inconsistent input.
    int addIntrinsicPanel(const IntrinsicMesh& m, const std::vector<FabricMaterial>& mats, const std::string& name = {});

    // Sew outline edge `edgeA` of panel A to `edgeB` of panel B (see PanelMesh::edgeChains).
    void addSeam(int panelA, int edgeA, int panelB, int edgeB, bool reverse = false);

    // Sew vertex `a` directly to vertex `b` (crochet plan 2.4): one seam link with b0 = b1 = b, t = 0 and
    // alpha = 1/(seamStiffness * share), where `share` is the seam length the link stands for (m). Like addSeam,
    // the request is only stored and built in finalize(), so a repeated finalize() rebuilds the same set.
    // Throws std::logic_error after the simulation started, std::invalid_argument for share <= 0 or a == b,
    // std::out_of_range for a vertex outside [0, vertexCount()).
    void addPointSeam(int a, int b, Real share);

    // ---- static equilibrium (opt-in; the dynamic step() is untouched) ---------
    // Finds the equilibrium of the potential energy (stretch, shear, bending, seams, gravity, body contact) from the
    // current positions, without simulating motion. See docs/agent_notes/S1_static_settle.md.
    StaticResult solveStatic(const StaticParams& sp = {});
    // Physical potential energy (constraints - gravity work) of the current positions.
    Real potentialEnergy(Real gravityScale = 1.0) const;
    // Edits that keep the topology (so the symbolic factorisation is reused); follow with solveStatic({warmStart=true}).
    void setPanelMaterial(int panel, const FabricMaterial& mat);
    void setRestUV(int v, const Vec2& restCoords);  // grain-aligned rest coordinates (see restUV())
    // Perturbs the settled state (random displacement of `amplitude` m, load wiggle of +-`gravityWiggle`), re-solves,
    // reports how far the shape and energy moved, and restores the original state. Off unless called.
    StaticStability checkStaticStability(const StaticParams& sp = {}, unsigned seed = 1, Real amplitude = 1e-3, Real gravityWiggle = 0.2);
    int symbolicAnalyses() const { return symbolicAnalyses_; }

    void setCollider(std::shared_ptr<Collider> c) { collider_ = std::move(c); }
    const std::shared_ptr<Collider>& collider() const { return collider_; }
    Real vertexMass(int i) const { return mass_[i]; }
    // Displacement of the collider surface at each cloth vertex over the coming frame (a moving body). Used only
    // by the friction in solveBody (cloth is dragged along by the surface it rests on, not held back by it).
    // Empty vector = a static collider.
    void setColliderMotion(std::vector<Vec3> d) { colliderDisp_ = std::move(d); }

    // Pin a vertex (global index) in place; pinned vertices have infinite mass.
    void pinVertex(int v, bool pinned = true);
    void setVertexPosition(int v, const Vec3& p);  // teleport (also resets its velocity)

    // Advance one frame (dt, split into substeps).
    void step();

    // ---- read access -------------------------------------------------------
    int vertexCount() const { return static_cast<int>(x_.size()); }
    int triangleCount() const { return static_cast<int>(tris_.size()); }
    int panelCount() const { return static_cast<int>(panels_.size()); }
    const std::vector<Vec3>& positions() const { return x_; }
    const std::vector<Vec3>& velocities() const { return v_; }
    // Diagnostics: per-vertex "held on the body" flag of the last Newton iteration (1 = motion filter active).
    const std::vector<char>& heldFlags() const { return contactActive_; }
    const std::vector<std::array<int, 3>>& triangles() const { return tris_; }
    const std::vector<Vec2>& restUV() const { return restUV_; }  // grain-aligned rest coordinates
    int firstVertex(int panel) const { return panels_[panel].firstVertex; }
    int vertexCountOfPanel(int panel) const { return panels_[panel].vertexCount; }
    int firstTriangle(int panel) const { return panels_[panel].firstTri; }
    int triangleCountOfPanel(int panel) const { return panels_[panel].triCount; }
    const std::vector<int>& edgeChain(int panel, int edge) const { return panels_[panel].chains[edge]; }
    const std::string& panelName(int panel) const { return panels_[panel].name; }

    // ---- diagnostics -------------------------------------------------------
    Real kineticEnergy() const;
    Real maxSeamGap() const;          // metres, over all seam constraints
    // Seam pairs for display: vertex a is sewn to the point b0*(1-t) + b1*t of the other side.
    struct SeamLink { int a, b0, b1; Real t; };
    std::vector<SeamLink> seamLinks() const;
    Real minBodyClearance() const;    // metres between cloth vertices and the collider surface (negative = penetration)
    Real totalMass() const;
    int framesSimulated() const { return frame_; }
    const SolverParams& params() const { return p_; }
    SolverParams& params() { return p_; }

private:
    struct Panel {
        std::string name;
        int material = 0;
        int firstVertex = 0, vertexCount = 0, firstTri = 0, triCount = 0;
        std::vector<std::vector<int>> chains;  // global vertex ids per outline edge
        std::vector<Real> extraMass;           // kg per local vertex (intrinsic panels, see IntrinsicMesh::extraMass);
                                               // empty for normal panels
    };
    struct TriC {
        int i0, i1, i2;
        Real a, b, c, d;  // inverse of the rest edge matrix
        Real alphaU, alphaV, alphaS, comp;
        // J-curve lock terms (CROCHET_PLAN §2.3) from the triangle's material: lockU/lockV are the strain at which
        // the loops lock along u (warp)/v (weft), 0 = off; ratio = stiffness above the lock / below.
        Real lockU, lockV, ratio;
        Real lamU, lamV, lamS;
        int slot[9];  // block positions for the implicit solver's Hessian
    };
    struct BendC {
        int i[4];  // shared edge (i0,i1), opposite vertices (i2,i3)
        Real alpha, lam;
        // Signed hinge rest angle (rad): the signed dihedral angle the hinge relaxes to (0 = flat, the default;
        // +-pi = fully folded). Sign convention (crochet plan section 2.2): a positive rest angle folds the second
        // triangle toward the side its own normal points to, i.e. the hinge bends toward the common side of the two
        // consistently oriented triangle normals (both triangles listed counter-clockwise around the same side - the
        // winding addIntrinsicPanel/intrinsic meshes use, so the two normals agree on a flat pair).
        // In our index convention i[2] is the opposite vertex of the first triangle and i[3] of the second (the
        // "second triangle" whose normal the sentence above means), so: with a positive rest angle the i[3] vertex
        // folds to the side the i[2] triangle's oriented normal points to, and a negative rest angle folds it to the
        // other side. Verified in tests/test_intrinsic.cpp (crochet plan section 2.7 test 4): a flat 2-triangle
        // strip pinned on the i[2] triangle with rest = +0.5 rad settles at a measured signed angle of +0.5 rad with
        // the free i[3] vertex on the +normal side of the pinned triangle. detail::hingeAngleSigned computes it.
        // Used by the implicit solver from T1.3 on; XPBD (solveBends) and VBD (vbdLocalEnergy) keep the unsigned
        // positional angle and ignore it - deliberate, not pending.
        Real rest = 0;
        int slot[16];
    };
    struct SeamC {
        int a, b0, b1;
        Real t, alpha, lam;
        int slot[9];
    };
    // addPointSeam request (crochet plan 2.4), kept so finalize() can rebuild seamC_ from it (which makes a
    // repeated finalize() idempotent). a, b are global vertex ids; share is the seam length the link stands for (m).
    struct PointSeamSpec { int a, b; Real share; };

    void finalize();
    // VBD (cloth_vbd.cpp)
    void stepVBD();
    void vbdPrepare();
    void vbdSolveVertex(int i, Real invH2, Real h, Real seamScale);
    Real vbdLocalEnergy(int i, Real invH2, Real seamScale) const;
    struct VbdRef { int type, elem, local; };  // type 0 triangle, 1 hinge, 2 seam
    bool vbdReady_ = false;
    std::vector<int> vbdAdjStart_;
    std::vector<VbdRef> vbdAdj_;
    std::vector<std::vector<int>> vbdColors_;
    std::vector<Real> vbdLambda_;
    std::vector<Vec3> vbdPrev_, vbdAccel_, vbdVPrev_;
    ExternalLinearSolver extSolver_;
    std::vector<Real> extMass_;
    int patternVersion_ = 0;
    struct SlEdge { int a, b; Real l0; };
    std::vector<SlEdge> slEdges_;  // strain limiting: unique mesh edges with rest length
    void limitStrain();  // per vertex, the body penalty force at the start of the substep (friction normal force)
    void solveTriangles(Real invH2);
    void solveBends(Real invH2);
    void solveSeams(Real invH2, Real scale);
    void solveBody();
    void solveSelfCollision();

    SolverParams p_;
    bool finalized_ = false;
    int frame_ = 0;

    std::vector<FabricMaterial> mats_;
    std::vector<Panel> panels_;
    std::vector<SeamSpec> seamSpecs_;
    std::vector<PointSeamSpec> pointSeamSpecs_;  // addPointSeam requests, built into seamC_ by finalize()

    // per vertex
    std::vector<Vec3> x_, xPrev_, v_;
    std::vector<Vec2> restUV_;
    std::vector<Real> mass_, w_, damp_, muS_, muK_, radius_;
    std::vector<int> vertexPanel_;
    std::vector<char> pinned_;
    // topology + constraints
    std::vector<std::array<int, 3>> tris_;
    std::vector<int> triPanel_;
    // Per-triangle rest shape and material (CROCHET_PLAN §2.1 step 1). triRest_[t] holds the triangle's rest corner
    // positions in grain coordinates (u = wale, v = course, metres). finalize() refills it from restUV_ for every
    // non-intrinsic triangle on every call, so setRestUV() + re-finalize keeps it current; triangles of an intrinsic
    // panel (addIntrinsicPanel, T1.2) supply their own rest shapes. triMat_ indexes mats_ (one material per triangle
    // instead of a per-panel lookup); triIntrinsic_ is 1 for triangles of an intrinsic panel, 0 for panel triangles.
    std::vector<std::array<Vec2, 3>> triRest_;
    std::vector<int> triMat_;
    std::vector<char> triIntrinsic_;
    // Hinge modifiers of intrinsic panels (addIntrinsicPanel): key = global shared-edge vertex pair, packed as
    // (min << 32 | max) like noCollide_ below; value = {bendScale, restAngle}. Empty for plain panels.
    std::unordered_map<std::uint64_t, std::pair<Real, Real>> hingeMods_;
    std::vector<TriC> triC_;
    std::vector<BendC> bendC_;
    std::vector<SeamC> seamC_;
    std::unordered_set<std::uint64_t> noCollide_;  // (vertex, triangle) pairs excluded from self collision

    std::shared_ptr<Collider> collider_;
    std::vector<Vec3> colliderDisp_;

    // spatial hash for self collision
    struct Hash {
        Real cell = 0.02, inv = 50;
        size_t table = 0;
        std::vector<int> start, items, stamp;
        int stampId = 0;
    } hash_;
    void buildHash();

    // ---- implicit integrator (cloth_implicit.cpp) --------------------------
    struct BlockMatrix {
        std::vector<int> rowStart, col;  // block-CSR pattern, columns sorted within each row
        std::vector<Mat3> val;
        std::vector<int> diag;           // index of the (i,i) block in each row
        int find(int i, int j) const;
    };
    void buildBlockPattern();
    void bindBlockSlots();     // (re)assign the K_ slot of every constraint for the current pattern
    bool patternValid_ = false;
    bool staticSelfCollision_ = false;  // StaticParams::selfCollision of the running solveStatic
    int symbolicAnalyses_ = 0;
    // static solver internals (cloth_static.cpp)
    Real staticEnergy(Real alpha, Real lam) const;
    struct StaticNewton { int iters = 0; bool ok = true; bool converged = false; Real residual = 0, maxStep = 0; };
    // Regularised Newton solve of one stage; see cloth_static.cpp.
    StaticNewton staticNewton(Real alpha, Real lam, int maxIt, Real stepTol, Real resTol, Real weightNorm, Real maxStepCap);
    void stepImplicit();
    void stepXpbd();
    // Gradient of the constraint energy (into grad_) and Gauss-Newton Hessian (into K_).
    void assembleEnergy(Real seamScale);
    // Total variational energy G(x) = inertia + constraint energy, used by the line search.
    Real variationalEnergy(Real invH2, Real seamScale) const;
    int solveLinear(Real h2inv, const std::vector<Vec3>& rhs, std::vector<Vec3>& dx, bool cholPrec = false, int maxIt = -1);
    bool refactor(Real h2inv);
    int solveCholPcg(Real h2inv, const std::vector<Vec3>& rhs, std::vector<Vec3>& dx);
    // pattern bookkeeping (barrier contact adds vertex couplings to the Hessian pattern)
    void rebuildPattern();
    std::unordered_set<std::uint64_t> extraCouple_;  // (min << 32 | max) vertex pairs coupled by contact pairs
    long long factorizations_ = 0;
    long long patternRebuilds_ = 0;
    // ---- barrier self contact (cloth_barrier.cpp) --------------------------
    void barrierInit(Real h);
    void barrierUpdateSet();                    // candidates at x_, register Hessian couplings (may rebuild pattern)
    void barrierAddToSystem();                  // barrier gradient and PSD Hessian into grad_ / K_
    Real barrierEnergy(const std::vector<Vec3>& x, const std::vector<contact::ContactPair>& c, Real* minDist = nullptr) const;
    // Largest fraction t in [0, 1] of the linear motion x0 -> x1 that no pair crosses (conservative CCD).
    Real barrierMaxStep(const std::vector<Vec3>& x0, const std::vector<Vec3>& x1, std::vector<contact::ContactPair>& scratch);
    void barrierAdapt(Real minDist);
    contact::SelfContact barrier_;
    bool barrierReady_ = false;
    bool patternDirty_ = false;
    Real kappa_ = 0, prevMinDist_ = 1e30;
    std::vector<contact::ContactPair> bCand_, bSwept_;
    std::vector<Vec3> xCcd_, xCcd2_, xLift_;
    contact::BarrierStats bstats_;
    bool cholValid_ = false;
    int cholAge_ = 0;
    // Direct solve; returns -1 if the factorisation failed (caller falls back to CG).
    int solveDirect(Real h2inv, const std::vector<Vec3>& rhs, std::vector<Vec3>& dx, bool refactor = true);
    SparseCholesky chol_;
    std::vector<int> sRowStart_, sCol_;
    std::vector<double> sVal_, sRhs_, sSol_;
    std::vector<char> contactActive_;
    BlockMatrix K_;
    std::vector<Vec3> xBackup_, grad_, rhs_, dx_, xTilde_, cgR_, cgZ_, cgP_, cgAp_;
    std::vector<Mat3> precond_;
    std::vector<Vec3> ssorY_, ssorU_;
    std::vector<Mat3> filt_;      // per-vertex motion filter: I (free), 0 (pinned), I - n n^T (resting on the body)
    std::vector<Vec3> contactN_;  // outward body normal at each vertex (valid when the vertex is near the body)
    std::vector<Real> contactPhi_;
    int lastCgIterations_ = 0;
    int lineSearchFailures_ = 0;
    // K1 compliant skin (cloth_skin.cpp). skinArea_: tributary area (m^2, current configuration, frozen per substep);
    // skinK_: k*area (N/m) per vertex; skinPhi_: offset-corrected signed distance (soft layer surface);
    // rigidForce_: normal force (N) the rigid hold carried in the last Newton iteration.
    std::vector<Real> skinArea_, skinK_, skinPhi_, rigidForce_;
    bool skinOn() const { return p_.skinStiffness > 0 && p_.skinMaxDepth > 0; }
    void updateSkinAreas();
    void assembleSkin();
    // body friction inside Newton (cloth_friction.cpp, bodyFrictionMode 2): mu * lambda and normal lagged per substep
    std::vector<Real> fricLam_;
    std::vector<Vec3> fricN_;
    std::vector<char> fricOn_;  // vertices with a friction term in the current Newton iteration
    void frictionLag(bool skin);
    Vec3 frictionSlip(int i, Real invS) const;
    Real frictionEnergy(Real h) const;
    void assembleFriction(Real h, bool skin);
    // K2 contact multipliers (cloth_contact_al.cpp)
    std::vector<Real> alLambda_, alRho_;
    bool alOn_ = false;           // contactAL active in the current step
    Real alEnergy() const;        // sum of the augmented-Lagrangian terms at x_
    void alAssemble();            // AL gradient and Gauss-Newton Hessian into grad_ / K_; fills contactPhi_/contactN_
public:
    // contactAL: current normal-force multipliers (N) per vertex (empty before the first step with contactAL).
    const std::vector<Real>& contactMultipliers() const { return alLambda_; }
private:
    long long newtonIterations_ = 0;  // Newton iterations used, summed over all substeps
    long long substepsDone_ = 0;
    ContactDiag cdiag_;
    bool autoTension_ = false;  // hessianAuto: current model (true = with the tension term)
    int dxTraceFrame_ = -1;  // diagnostics: print every Newton iteration of this frame to stderr
public:
    void setNewtonTraceFrame(int f) { dxTraceFrame_ = f; }
private:
    std::vector<char> prevActive_, prevSubActive_;
    Real stepKE(Real h) const;  // KE of (x_ - xPrev_)/h

public:
    int lastCgIterations() const { return lastCgIterations_; }
    long long factorNonZeros() const { return chol_.nnzL(); }
    double factorFlops() const { return chol_.flops(); }
    // Hand the CG solves (linearSolver ConjugateGradient) to an external solver, e.g. the GPU; empty = the CPU CG.
    void setExternalLinearSolver(ExternalLinearSolver f) { extSolver_ = std::move(f); }
    bool hasExternalLinearSolver() const { return static_cast<bool>(extSolver_); }
    int factorSupernodes() const { return chol_.supernodeCount(); }
    // Newton steps rejected because no step length reduced the energy (should stay 0).
    int lineSearchFailures() const { return lineSearchFailures_; }
    // Newton iterations used so far, and substeps taken (their ratio is the mean iterations per substep).
    long long newtonIterations() const { return newtonIterations_; }
    long long substepsDone() const { return substepsDone_; }

    // ---- contact pressure (K1) ---------------------------------------------------------------------------------
    // Per-vertex pressure in Pa at the current state. Soft part: k * delta of the compliant skin (0 when the layer
    // is off). Rigid part: normal force the rigid hold carried in the last Newton iteration of the last substep,
    // divided by the vertex's tributary area (an estimate: the force is the net push into the body that the motion
    // filter cancelled, so it includes inertia and any non-converged residual). Total = soft + rigid.
    // Tributary area of each vertex (m^2): a third of the area of each adjacent triangle, current configuration.
    // Vertex pressures are force / area and are noisy on irregular or checkerboard meshes (the cloth force is not
    // shared in proportion to this area); aggregate with sum(p*A)/sum(A) or use triangleContactPressure().
    std::vector<Real> vertexAreas() const;
    std::vector<Real> vertexSoftPressure() const;
    std::vector<Real> vertexRigidPressure() const;
    std::vector<Real> vertexContactPressure() const;
    // Per-triangle mean of the three vertex pressures (total, Pa).
    std::vector<Real> triangleContactPressure() const;

    // Numeric Cholesky factorisations and symbolic re-analyses (pattern rebuilds after start-up) so far.
    long long factorizations() const { return factorizations_; }
    long long patternRebuilds() const { return patternRebuilds_; }
    // Contact diagnostics (SolverParams::contactDiagnostics).
    const ContactDiag& contactDiag() const { return cdiag_; }
    // Barrier contact diagnostics (barrierContact mode).
    const contact::BarrierStats& barrierStats() const { return bstats_; }
    Real barrierKappa() const { return kappa_; }
    // Smallest distance between non-adjacent cloth primitives (point-triangle, edge-edge) within `range` metres
    // (returns `range` if none is closer). Works in either mode (builds the pair structure on demand).
    Real minSelfDistance(Real range = 0.01);
    // Exact count of intersecting non-adjacent triangle pairs (seam-linked vertices count as shared).
    int selfIntersections();
};

}  // namespace fl
