// A whole crochet run as a stepper. See crochet/session.hpp.
#include "failoring/crochet/session.hpp"

#include <stdexcept>

namespace fl {

const char* CrochetSession::phaseName(Phase p) {
    switch (p) {
        case Phase::Relax: return "relax";
        case Phase::Hang: return "hang";
        case Phase::Insert: return "insert";
        case Phase::Release: return "release";
        default: return "done";
    }
}

CrochetSession::CrochetSession(StitchGraph graph, const Yarn& yarn, const ModelOptions& model, const SessionSettings& settings)
    : graph_(std::move(graph)), yarn_(yarn), settings_(settings) {
    if (settings.scene != "auto" && settings.scene != "hang_pin" && settings.scene != "hang_peg" && settings.scene != "none")
        throw std::runtime_error("unknown scene '" + settings.scene + "' (auto, hang_pin, hang_peg, none)");
    wantLoad_ = settings.load != "none";
    if (wantLoad_) loadSpec_ = loadPreset(settings.load);  // throws std::invalid_argument naming the problem
    model_ = buildCrochetModel(graph_, yarn_, model);
    layout_ = layoutModel(model_, graph_);
    bool hasStrap = false;
    for (const LayoutRegion& r : layout_.regions) hasStrap = hasStrap || r.type == LayoutRegion::Strip;
    hang_ = settings.scene == "hang_pin" || settings.scene == "hang_peg" || (settings.scene == "auto" && hasStrap);
    if ((settings.scene == "hang_pin" || settings.scene == "hang_peg") && !hasStrap)
        throw std::runtime_error("scene " + settings.scene + " needs a strap, and this pattern has none");
    if (wantLoad_ && !hang_) throw std::runtime_error("a load needs a hung model (scene hang_pin or hang_peg, and a pattern with a strap)");
    RelaxOptions ro;
    ro.noise = settings.relaxNoise;
    relax_ = std::make_unique<RelaxRun>(model_, ro);
}

const std::vector<Vec3>& CrochetSession::positions() const {
    if (scene_ != nullptr) return scene_->solver().positions();
    if (relax_ != nullptr && !relaxed_.size()) return relax_->positions();
    return relaxed_;
}

int CrochetSession::phaseFrames() const {
    switch (phase_) {
        case Phase::Relax: return relax_ != nullptr ? relax_->result().frames : relaxFrames_;
        case Phase::Hang: return scene_ != nullptr ? scene_->stats().frames : 0;
        case Phase::Insert: return load_ != nullptr ? load_->stats().insertFrames : 0;
        case Phase::Release: return load_ != nullptr ? load_->stats().releaseFrames : 0;
        default: return 0;
    }
}

Vec3 CrochetSession::loadCenter() const { return load_ != nullptr ? load_->center() : Vec3(0, 0, 0); }

bool CrochetSession::settled() const {
    if (!relaxConverged_ && phase_ != Phase::Relax) return false;
    if (hang_ && phase_ == Phase::Done && !hangSettled()) return false;
    if (load_ != nullptr && phase_ == Phase::Done && !loadSettled()) return false;
    return phase_ == Phase::Done;
}

void CrochetSession::finishRelax() {
    relax_->finish(model_);
    relaxed_ = model_.mesh.x0;
    relaxConverged_ = relax_->result().converged;
    relaxFrames_ = relax_->result().frames;
    if (!hang_) {
        phase_ = Phase::Done;
        return;
    }
    SceneOptions so;
    so.kind = settings_.scene == "hang_peg" ? SceneKind::HangPeg : SceneKind::HangPin;
    so.maxFrames = settings_.frameCap;
    scene_ = std::make_unique<CrochetScene>(model_, graph_, layout_, so);
    phase_ = Phase::Hang;
}

void CrochetSession::finishHang() {
    if (!wantLoad_) {
        phase_ = Phase::Done;
        return;
    }
    fit_ = fitCheck(model_, graph_, layout_, loadSpec_);
    if (!fit_.known || !fit_.fits) {
        phase_ = Phase::Done;  // the load is not inserted; fit() says why
        return;
    }
    load_ = std::make_unique<RigidLoad>(*scene_, loadSpec_);
    phase_ = Phase::Insert;
}

void CrochetSession::step(int frames) {
    while (frames > 0 && phase_ != Phase::Done) {
        const int before = phaseFrames();
        switch (phase_) {
            case Phase::Relax:
                relax_->step(frames);
                if (relax_->finished()) finishRelax();
                break;
            case Phase::Hang:
                scene_->advance(frames);
                if (scene_->settled() || scene_->stats().frames >= settings_.frameCap) {
                    scene_->settle();  // fills the self-intersection count and the clearance
                    finishHang();
                }
                break;
            case Phase::Insert:
                load_->insertSteps(frames);
                if (load_->stats().inserted || load_->stats().insertFrames >= settings_.frameCap) phase_ = Phase::Release;
                break;
            case Phase::Release:
                load_->releaseSteps(frames);
                if (load_->stats().released || load_->stats().releaseFrames >= settings_.frameCap) phase_ = Phase::Done;
                break;
            default: break;
        }
        const int used = std::max(1, phaseFrames() - before);
        frames -= used;
    }
}

Metrics CrochetSession::metrics() const {
    MetricsInput in;
    in.model = &model_;
    in.graph = &graph_;
    in.layout = &layout_;
    in.yarn = &yarn_;
    in.relaxed = relaxed_.empty() ? (relax_ != nullptr ? relax_->positions() : model_.mesh.x0) : relaxed_;
    in.frames = relax_ != nullptr ? relax_->result().frames : relaxFrames_;
    in.iterCapHits = relax_ != nullptr ? relax_->result().iterCapHits : 0;
    in.substeps = relax_ != nullptr ? relax_->result().substeps : 0;
    in.finalKineticEnergy = relax_ != nullptr ? relax_->result().kineticEnergy : 0;
    if (scene_ != nullptr) {
        const SceneStats& st = scene_->stats();
        in.hanging = scene_->solver().positions();
        in.frames += st.frames;
        in.newtonIterations = st.newtonIterations;
        in.iterCapHits += st.iterCapHits;
        in.substeps += st.substeps;
        in.lineSearchFailures = scene_->solver().contactDiag().lsFail;
        in.finalKineticEnergy = st.kineticEnergy;
    }
    if (wantLoad_ && fit_.known) {
        in.hasLoad = true;
        in.load = loadSpec_;
        in.fit = fit_;
        if (load_ != nullptr) {
            in.loaded = scene_->solver().positions();
            in.loadCenter = load_->center();
            in.frames += load_->stats().insertFrames + load_->stats().releaseFrames;
            in.newtonIterations = scene_->solver().contactDiag().newtonIters;
            in.iterCapHits = scene_->solver().contactDiag().iterCapHits;
            in.substeps = scene_->solver().contactDiag().substeps;
            in.lineSearchFailures = scene_->solver().contactDiag().lsFail;
            in.finalKineticEnergy = scene_->solver().kineticEnergy();
            Real pmax = 0;
            for (const Real p : scene_->solver().vertexRigidPressure()) pmax = std::max(pmax, p);
            in.maxContactPressure = pmax;
        }
    }
    return computeMetrics(in);
}

}  // namespace fl
