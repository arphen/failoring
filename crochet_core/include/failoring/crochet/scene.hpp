#pragma once
// Scenes for a relaxed crochet model (docs/CROCHET_PLAN.md section 7.2, ticket T6.1): the model hung by its
// strap, from a pin ("hang_pin": the middle rows of the strap are held, like a hand) or from a peg ("hang_peg":
// a horizontal capsule the strap drops onto), then settled under gravity.
//
// Decisions recorded for T6.1:
//  - Orientation. "Up" is the direction from the centroid of everything that is not strap to the middle of the
//    strap (the point the object is hung from); the assembly is rotated so that up = +y and translated so that the
//    middle of the strap is over the origin in x and z. (The layouts already come out y-up; this makes the scene
//    independent of how the relaxation left the object.)
//  - hang_pin pins every vertex of the two middle rows of the strap at its position.
//  - hang_peg puts the capsule INSIDE the strap loop, pegGap (2 cm) below the inner apex of the loop, axis along x:
//    the plan says "2 cm above the strap's top", which for a loop hung from a peg can only mean that the strap's top
//    has to rest on the peg after a 2 cm drop. bodyFrictionMode = 2 (smoothed Coulomb, plan 7.2).
//  - Settling: gravity on, selfThickness = min(0.8 t, 0.4 min(w, h)), step until the kinetic energy is below keTarget
//    for settleFrames frames in a row or maxFrames.
#include <memory>
#include <vector>

#include "failoring/cloth.hpp"
#include "failoring/crochet/layout.hpp"
#include "failoring/crochet/model.hpp"
#include "failoring/crochet/relax.hpp"

namespace fl {

enum class SceneKind { HangPin, HangPeg };

struct SceneOptions {
    SceneKind kind = SceneKind::HangPin;
    int maxFrames = 1500;
    Real keTarget = 1e-8;  // J
    int settleFrames = 30;
    Real pegRadius = 0.012, pegLength = 0.10, pegGap = 0.02;  // m
    int newtonIterations = 30;
};

struct SceneStats {
    int frames = 0;
    bool settled = false;
    Real kineticEnergy = 0;       // J, at the end
    long long substeps = 0, newtonIterations = 0, iterCapHits = 0;
    int selfIntersections = -1;   // exact count at the end
    Real minClearance = 0;        // m, cloth to the peg (large when there is none)
};

class CrochetScene {
public:
    // model must carry positions (layoutModel, usually relaxModel). Throws std::runtime_error when the layout has
    // no strap (no Strip region), naming what is missing.
    CrochetScene(const CrochetModel& model, const StitchGraph& g, const LayoutReport& layout, const SceneOptions& opt = {});

    SceneStats settle();  // step until settled or maxFrames; adds to the frame count of earlier calls
    ClothSolver& solver() { return *solver_; }
    const ClothSolver& solver() const { return *solver_; }
    const std::vector<int>& pinned() const { return pinned_; }
    const SceneStats& stats() const { return stats_; }

private:
    SceneOptions opt_;
    std::unique_ptr<ClothSolver> solver_;
    std::vector<int> pinned_;
    SceneStats stats_;
    int calmFrames_ = 0;
};

}  // namespace fl
