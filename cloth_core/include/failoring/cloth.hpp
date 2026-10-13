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
    // ---- body contact as an augmented Lagrangian (idea K2, docs/agent_notes/K2_contact_multipliers.md) ----
    // Off by default (the projection + motion filter path above is used). On: each vertex keeps a normal-force
    // multiplier lambda (N) across Newton iterations and time steps; the body enters the variational energy as
    // the C1 augmented-Lagrangian term of g = phi - radius >= 0, there is no projection inside the solve, and the
    // after-the-solve Coulomb friction uses lambda as the normal force.
    bool contactAL = false;
    Real contactALStiffness = 1000.0;  // rho_i = this * (m_i/h^2 + tr(K_ii)/3) at the start of each substep (N/m)
    Real contactALTolerance = 1e-5;    // m: re-solve (outer loop) while a vertex is deeper than this after Newton
    int contactALOuter = 3;            // max Newton solves per substep (multiplier updates in between)
    bool contactDiagnostics = false;   // extra energy bookkeeping in contactStats() (costs energy evaluations)
};

// Counters for body-contact diagnostics, summed since the last resetContactStats(). The counters are kept in both
// contact modes; the energy fields are only filled with SolverParams::contactDiagnostics.
struct ContactStats {
    long long substeps = 0, newtonIterations = 0, lineSearchFailures = 0;
    long long lsFailProjection = 0;  // failed line search that would have descended without the projection
    long long lsFailFlat = 0;        // failed line search with |dE| at round-off level for every step tried
    long long flipsIteration = 0;    // vertices whose contact state changed between consecutive Newton iterations
    long long flipsStep = 0;         // ... between the end of consecutive substeps
    long long flipsForceSign = 0;    // of flipsIteration: vertex stayed within contactBand, only the sign of its force flipped
    long long activeEnd = 0;         // contact-active vertices at the end of each substep (summed)
    long long projections = 0;       // in-Newton projections (iterations after the first)
    double projectionDistance = 0;   // m, summed
    double projectionEnergy = 0;     // J, change of the variational energy caused by those projections (diagnostics)
    long long bodyProjections = 0;   // after-the-solve projections (solveBody)
    double bodyProjectionDistance = 0;
    long long frictionStick = 0, frictionSlide = 0;
    double frictionDistance = 0;     // m, tangential correction applied by friction
    double keAfterNewton = 0;        // J, kinetic energy implied by the Newton result (diagnostics, summed)
    double keBodyPass = 0, keSelfPass = 0;  // J, change of that kinetic energy by the after-solve passes (diagnostics)
    double maxPenetration = 0;       // m, deepest vertex after Newton (before the after-solve pass)
    long long alOuter = 0;           // contactAL: Newton solves (outer iterations) summed over substeps
    double maxLambda = 0;            // contactAL: largest multiplier (N)
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

    void setCollider(std::shared_ptr<Collider> c) { collider_ = std::move(c); }
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
    Real kineticEnergyOf(const std::vector<Vec3>& x, Real h) const;  // KE of the velocities (x - xPrev)/h
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
    void stepImplicit();
    void stepXpbd();
    // Gradient of the constraint energy (into grad_) and Gauss-Newton Hessian (into K_).
    void assembleEnergy(Real seamScale);
    // Total variational energy G(x) = inertia + constraint energy, used by the line search.
    Real variationalEnergy(Real invH2, Real seamScale) const;
    int solveLinear(Real h2inv, const std::vector<Vec3>& rhs, std::vector<Vec3>& dx, bool cholPrec = false, int maxIt = -1);
    bool refactor(Real h2inv);
    int solveCholPcg(Real h2inv, const std::vector<Vec3>& rhs, std::vector<Vec3>& dx);
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
    long long newtonIterations_ = 0;  // Newton iterations used, summed over all substeps
    long long substepsDone_ = 0;

public:
    int lastCgIterations() const { return lastCgIterations_; }
    long long factorNonZeros() const { return chol_.nnzL(); }
    // Newton steps rejected because no step length reduced the energy (should stay 0).
    int lineSearchFailures() const { return lineSearchFailures_; }
    // Newton iterations used so far, and substeps taken (their ratio is the mean iterations per substep).
    long long newtonIterations() const { return newtonIterations_; }
    long long substepsDone() const { return substepsDone_; }

    // ---- body contact diagnostics and augmented Lagrangian (cloth_contact_al.cpp) ----
    const ContactStats& contactStats() const { return cstats_; }
    void resetContactStats() { cstats_ = ContactStats(); }
    // contactAL: current normal-force multipliers (N), one per vertex (empty before the first step).
    const std::vector<Real>& contactMultipliers() const { return alLambda_; }

private:
    ContactStats cstats_;
    std::vector<char> diagPrevActive_, diagStepActive_;
    std::vector<Real> alLambda_, alRho_;
    void diagIterationActive(int iteration);        // counts flips between Newton iterations
    void diagSubstepEnd();                           // counts flips between substeps, active set, penetration
    void diagLineSearchFailure(Real invH2, Real seamScale, Real E0);
    // contactAL implementation
    void stepImplicitAL();
    Real alContactEnergy() const;                    // sum of the augmented-Lagrangian terms at x_
    void alAssemble();                               // adds AL gradient and Hessian (rho n n^T) to grad_ / K_
    void solveBodyAL();                              // after-the-solve: friction with lambda as normal force
};

}  // namespace fl
