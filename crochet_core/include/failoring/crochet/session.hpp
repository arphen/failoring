#pragma once
// A whole crochet run as a stepper (docs/CROCHET_PLAN.md section 7, used by the CLI and by the GDExtension, ticket T7.1): relax,
// hang, and (with a load) insert and release, advancing a few frames at a time so that an editor can show the shape as it
// moves and offer Stop. runCrochet is this run to the end.
//
// Phases: Relax (zero gravity), Hang (hang_pin / hang_peg settle), Insert (the load descends), Release (two-way coupling),
// Done. A pattern with no strap, or scene "none", goes straight from Relax to Done. When the fit check says the load does not
// fit, it is not inserted: the run ends after the hang with fit().fits == false.
#include <memory>
#include <string>
#include <vector>

#include "failoring/crochet/layout.hpp"
#include "failoring/crochet/metrics.hpp"
#include "failoring/crochet/model.hpp"
#include "failoring/crochet/relax.hpp"
#include "failoring/crochet/scene.hpp"

namespace fl {

struct SessionSettings {
    std::string scene = "auto";  // auto (by the strap when there is one) | hang_pin | hang_peg | none
    std::string load = "none";   // none | mba13 | mbp14 | mbp16 | box:LxWxH_mm:kg
    int frameCap = 1500;         // frame cap of each hang / load phase
    Real relaxNoise = 0.3e-3;    // m, start perturbation of the relaxation (see relax.hpp)
};

class CrochetSession {
public:
    enum class Phase { Relax, Hang, Insert, Release, Done };

    // Builds the model and its layout (fast) and prepares the relaxation. Throws std::runtime_error / std::invalid_argument naming
    // the problem: an unknown scene or load, a hang scene or a load on a pattern without a strap.
    CrochetSession(StitchGraph graph, const Yarn& yarn, const ModelOptions& model, const SessionSettings& settings);

    Phase phase() const { return phase_; }
    static const char* phaseName(Phase p);
    const char* phaseName() const { return phaseName(phase_); }
    bool done() const { return phase_ == Phase::Done; }
    // Advance by up to `frames` frames; a phase that finishes hands over to the next one within the call. No-op when done.
    void step(int frames);
    // Frames spent in the current phase.
    int phaseFrames() const;

    const StitchGraph& graph() const { return graph_; }
    const CrochetModel& model() const { return model_; }
    const LayoutReport& layout() const { return layout_; }
    const std::vector<Vec2>& stitchUv() const { return uv_; }  // per stitch, from the layout before the relaxation
    const Yarn& yarn() const { return yarn_; }
    // The positions of the current state: the relaxation's while relaxing, the scene's after.
    const std::vector<Vec3>& positions() const;
    const std::vector<Vec3>& relaxedPositions() const { return relaxed_; }  // empty until the relaxation finished

    bool relaxConverged() const { return relaxConverged_; }
    int relaxFrames() const { return relaxFrames_; }
    bool hangSettled() const { return scene_ != nullptr && scene_->settled(); }
    bool loadSettled() const { return load_ != nullptr && load_->stats().released; }
    bool settled() const;  // everything that ran has settled
    bool hasLoad() const { return load_ != nullptr; }
    const FitResult& fit() const { return fit_; }
    // The rigid load, once it exists (from the fit check on): centre and shape; the peg of hang_peg.
    const LoadSpec& loadSpec() const { return loadSpec_; }
    Vec3 loadCenter() const;
    bool hasPeg() const { return scene_ != nullptr && scene_->peg() != nullptr; }
    CrochetScene* scene() { return scene_.get(); }

    // The numbers of the plan's section 7.5 for the current state.
    Metrics metrics() const;

private:
    void finishRelax();
    void finishHang();
    StitchGraph graph_;
    Yarn yarn_;
    CrochetModel model_;
    LayoutReport layout_;
    std::vector<Vec2> uv_;
    SessionSettings settings_;
    Phase phase_ = Phase::Relax;
    std::unique_ptr<RelaxRun> relax_;
    std::vector<Vec3> relaxed_;
    bool relaxConverged_ = false;
    int relaxFrames_ = 0;
    bool hang_ = false;
    std::unique_ptr<CrochetScene> scene_;
    LoadSpec loadSpec_;
    bool wantLoad_ = false;
    FitResult fit_;
    std::unique_ptr<RigidLoad> load_;
};

}  // namespace fl
