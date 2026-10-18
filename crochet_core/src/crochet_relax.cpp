// Relaxation of a crochet layout (docs/CROCHET_PLAN.md section 6.9). See crochet/relax.hpp.
#include "failoring/crochet/relax.hpp"

#include <random>
#include <stdexcept>

#include "failoring/error.hpp"

namespace fl {

std::unique_ptr<ClothSolver> makeSolver(const CrochetModel& model, const SolverParams& params, const std::vector<Vec3>* positions) {
    if (model.mesh.x0.empty() && positions == nullptr) FL_THROW(std::runtime_error, "makeSolver: the model has no vertex positions (run layoutModel first)");
    IntrinsicMesh im = model.mesh;
    if (positions != nullptr) im.x0 = *positions;
    auto solver = std::make_unique<ClothSolver>(params);
    solver->addIntrinsicPanel(im, model.materials, "crochet");
    for (const SeamRequest& s : model.pointSeams) solver->addPointSeam(s.a, s.b, s.share);
    return solver;
}

RelaxRun::RelaxRun(const CrochetModel& model, const RelaxOptions& opt) : opt_(opt) {
    SolverParams sp;
    sp.gravity = Vec3(0, 0, 0);
    sp.dampingScale = opt.dampingScale;
    sp.seamRampFrames = opt.seamRampFrames;
    sp.selfCollision = opt.selfCollision;
    sp.newtonIterations = opt.newtonIterations;
    sp.contactDiagnostics = true;
    std::vector<Vec3> start = model.mesh.x0;
    if (opt.noise > 0) {
        std::mt19937 rng(opt.seed);
        std::uniform_real_distribution<Real> u(-1, 1);
        for (Vec3& p : start) p += Vec3(u(rng), u(rng), u(rng)) * opt.noise;
    }
    solver_ = makeSolver(model, sp, &start);
    for (const int v : opt.pin) solver_->pinVertex(v);
}

bool RelaxRun::step(int frames) {
    for (int f = 0; f < frames && !finished_; ++f) {
        solver_->step();
        ++result_.frames;
        result_.kineticEnergy = solver_->kineticEnergy();
        if (result_.frames >= opt_.seamRampFrames && result_.kineticEnergy < opt_.keTarget) {
            result_.converged = true;
            finished_ = true;
        } else if (result_.frames >= opt_.maxFrames) {
            finished_ = true;
        }
    }
    result_.maxSeamGap = solver_->maxSeamGap();
    result_.substeps = solver_->contactDiag().substeps;
    result_.iterCapHits = solver_->contactDiag().iterCapHits;
    return finished_;
}

void RelaxRun::finish(CrochetModel& model) { model.mesh.x0 = solver_->positions(); }

RelaxResult relaxModel(CrochetModel& model, const RelaxOptions& opt) {
    RelaxRun run(model, opt);
    run.step(opt.maxFrames);
    run.finish(model);
    return run.result();
}

}  // namespace fl
