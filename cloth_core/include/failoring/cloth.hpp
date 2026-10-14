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
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "failoring/barrier_contact.hpp"
#include "failoring/collider.hpp"
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
    XPBD
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
    // Speed/accuracy trade-off: clamp in-plane stiffness (tension and shear, N/m) to this value in the
    // solver. 0 = off (exact material values). The condition number of the implicit system grows
    // linearly with stiffness, so a cap of a few hundred N/m makes preview drapes several times faster
    // while loads from cloth weight alone still strain the fabric by well under 1%. Do not use it for
    // tight-fit/stretch analysis (knits on a body), where the true stiffness is the point.
    Real stretchStiffnessCap = 0.0;
    int iterations = 1;       // XPBD only: constraint passes per substep
    Vec3 gravity{0, -9.81, 0};
    Real seamStiffness = 20000.0;  // N/m of seam length (thread + construction)
    int seamRampFrames = 0;        // >0: seams fade in over this many frames (helps assembling far-apart panels)
    Real contactOffset = 0.0015;   // m, extra clearance cloth <-> body
    Real contactBand = 1e-4;       // m, implicit: a vertex this close to the body and pushed towards it is held (not released)
    bool tensionStiffness = false;  // garment option (the app turns it on): exact PSD tension term, see ARCHITECTURE.md  // implicit: include the (PSD) tension part of the stretch Hessian; changes convergence, not the physics
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
    // Body friction: 0 = old (position correction after the solve, budget mu * penetration left after the solve),
    // 1 = none (diagnostic).
    int bodyFrictionMode = 0;
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
    int maxIterations = 400;           // cap on Newton iterations (= numeric factorisations) over the whole solve
    bool warmStart = false;            // start from the current positions at full load with small alpha
    bool verbose = false;
};

struct StaticResult {
    bool converged = false;
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

class ClothSolver {
public:
    explicit ClothSolver(SolverParams p = {}) : p_(p) {}

    // Adds a pattern piece. `grain` is the angle (radians, CCW) of the warp yarn
    // direction relative to the panel's +u axis (0 = warp along +u, pi/2 = along +v, pi/4 = bias).
    // Returns the panel index.
    int addPanel(const PanelMesh& mesh, const FabricMaterial& mat, const PanelPlacement& place,
                 Real grain = 0, const std::string& name = {});

    // Sew outline edge `edgeA` of panel A to `edgeB` of panel B (see PanelMesh::edgeChains).
    void addSeam(int panelA, int edgeA, int panelB, int edgeB, bool reverse = false);

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
    };
    struct TriC {
        int i0, i1, i2;
        Real a, b, c, d;  // inverse of the rest edge matrix
        Real alphaU, alphaV, alphaS, comp;
        Real lamU, lamV, lamS;
        int slot[9];  // block positions for the implicit solver's Hessian
    };
    struct BendC {
        int i[4];  // shared edge (i0,i1), opposite vertices (i2,i3)
        Real alpha, lam;
        int slot[16];
    };
    struct SeamC {
        int a, b0, b1;
        Real t, alpha, lam;
        int slot[9];
    };

    void finalize();
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

    // per vertex
    std::vector<Vec3> x_, xPrev_, v_;
    std::vector<Vec2> restUV_;
    std::vector<Real> mass_, w_, damp_, muS_, muK_, radius_;
    std::vector<int> vertexPanel_;
    std::vector<char> pinned_;
    // topology + constraints
    std::vector<std::array<int, 3>> tris_;
    std::vector<int> triPanel_;
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
    long long newtonIterations_ = 0;  // Newton iterations used, summed over all substeps
    long long substepsDone_ = 0;
    ContactDiag cdiag_;
    int dxTraceFrame_ = -1;  // diagnostics: print every Newton iteration of this frame to stderr
public:
    void setNewtonTraceFrame(int f) { dxTraceFrame_ = f; }
private:
    std::vector<char> prevActive_, prevSubActive_;
    Real stepKE(Real h) const;  // KE of (x_ - xPrev_)/h

public:
    int lastCgIterations() const { return lastCgIterations_; }
    long long factorNonZeros() const { return chol_.nnzL(); }
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
