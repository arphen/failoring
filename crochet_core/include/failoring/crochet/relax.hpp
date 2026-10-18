#pragma once
// Putting a CrochetModel into a ClothSolver, and the zero-gravity relaxation of the
// initial layout (docs/CROCHET_PLAN.md section 6.9, ticket T5.4).
//
// The layout is only a guess, so the triangles' rest shapes disagree with it and the
// joining seams start open. Before any loading the layout is relaxed with gravity 0,
// no colliders, the seams fading in over seamRampFrames, self collision on and the
// damping multiplied by dampingScale, stepping until the kinetic energy is below
// keTarget (and the ramp is over) or maxFrames frames. The result goes back into
// model.mesh.x0, so later scenes start from the relaxed shape.
//
// Decisions recorded for T5.4:
//  - `noise` adds a deterministic random displacement (fixed seed) to the start
//    positions. A flat layout of a mesh with more or less rest area than a flat
//    sheet holds is an unstable equilibrium: the ruffle of an over-increased
//    circle and the choice of side of an under-increased one are decided by a
//    perturbation, as in a real piece of work.
#include <memory>
#include <vector>

#include "failoring/cloth.hpp"
#include "failoring/crochet/model.hpp"

namespace fl {

struct RelaxOptions {
    int maxFrames = 400;
    Real keTarget = 1e-9;       // J
    Real dampingScale = 5;      // plan 6.9
    int seamRampFrames = 60;    // plan 6.9
    bool selfCollision = true;
    int newtonIterations = 30;  // SolverParams::newtonIterations: 12 (the default) hits the cap in 80 % of the pouch substeps
    Real noise = 0;             // m, amplitude of the start perturbation (0 = none)
    unsigned seed = 1;
    std::vector<int> pin;       // vertices held in place (scenes and tests that need a fixed part)
    Real gravity = 0;           // m/s^2 pulling along -y: 0 for the zero-gravity relaxation, 9.81 for an equilibrium under weight (hinge fit)
};

struct RelaxResult {
    int frames = 0;
    bool converged = false;     // kinetic energy below keTarget before maxFrames
    Real kineticEnergy = 0;     // J, at the end
    Real maxSeamGap = 0;        // m, at the end
    long long substeps = 0, iterCapHits = 0;  // Newton diagnostics (plan 1, rule 6)
};

// The relaxation as a stepper (the editor needs progress and a Stop; relaxModel is this run to the end).
class RelaxRun {
public:
    // model must carry positions. The run works on a copy of them: the model is not changed until finish().
    RelaxRun(const CrochetModel& model, const RelaxOptions& opt);
    // Advance up to `frames` frames; stops early when converged or at maxFrames. Returns finished().
    bool step(int frames);
    bool finished() const { return finished_; }
    const RelaxResult& result() const { return result_; }
    const std::vector<Vec3>& positions() const { return solver_->positions(); }
    // Writes the current positions into model.mesh.x0.
    void finish(CrochetModel& model);

private:
    RelaxOptions opt_;
    std::unique_ptr<ClothSolver> solver_;
    RelaxResult result_;
    bool finished_ = false;
};

// A solver holding the model as one intrinsic panel (panel 0, vertex v of the model is solver vertex v) plus its
// point seams. positions overrides model.mesh.x0 when given. The solver is not finalized yet.
std::unique_ptr<ClothSolver> makeSolver(const CrochetModel& model, const SolverParams& params, const std::vector<Vec3>* positions = nullptr);

// Relax model.mesh.x0 in place. Throws std::runtime_error when the model has no positions.
RelaxResult relaxModel(CrochetModel& model, const RelaxOptions& opt = {});

}  // namespace fl
