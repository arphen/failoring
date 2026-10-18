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
#include "failoring/collider.hpp"
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
    const std::shared_ptr<Collider>& peg() const { return peg_; }       // the hang_peg capsule, null for hang_pin
    const std::vector<int>& mouth() const { return mouth_; }            // vertices of the opening rim (layout.openingRows)
    const std::vector<int>& body() const { return body_; }              // vertices that are not strap
    // The pin reactions: force each pinned vertex needs to hold the settled state, as the gradient of the potential
    // energy of the constraints (the contact forces are external and not in it). Their y sum is the weight of the
    // cloth plus whatever load rests on it.
    std::vector<Vec3> pinReactions();

private:
    SceneOptions opt_;
    std::unique_ptr<ClothSolver> solver_;
    std::vector<int> pinned_, mouth_, body_;
    std::shared_ptr<Collider> peg_;
    SceneStats stats_;
    int calmFrames_ = 0;
};


// ---- the rigid load (plan section 7.3) --------------------------------------------------------------------------

// A box: width is horizontal along the bag's width (x), height is vertical (y), thickness along the front-back axis (z).
struct LoadSpec {
    std::string name;
    Real width = 0, height = 0, thickness = 0;  // m
    Real mass = 0;                              // kg
    Real cornerRadius = 0;                      // m
};
// "mba13", "mbp14", "mbp16" (corner radius 10 mm where the thickness allows it, else 0.9 half the thickness: a rounded
// box needs radius < half thickness) or "box:LxWxH_mm:kg" (L horizontal, W vertical, H thickness). Throws
// std::invalid_argument on anything else. Check the figures against apple.com before publishing them in a UI.
LoadSpec loadPreset(const std::string& name);

struct FitResult {
    bool known = false;      // false when the model has no mouth to measure
    bool fits = true;        // false: the load is more than 5 % bigger than the bag's inside
    bool tight = false;      // within 5 % either way
    Real innerWidth = 0, innerDepth = 0;  // m: half the opening circumference, and the rest depth
    Real widthRatio = 0, heightRatio = 0; // load / inside
    Real shortfall = 0;      // m, the largest amount by which a load dimension exceeds the inside (0 when it fits)
    std::string message;
};
// Plan 7.3 step 4, before inserting: compare the load's width and height with the bag's inner rest width (half the rest
// circumference of the mouth) and rest depth. Rest values, so the check does not depend on the settle.
FitResult fitCheck(const CrochetModel& model, const StitchGraph& g, const LayoutReport& layout, const LoadSpec& load);

struct LoadStats {
    bool inserted = false;       // the descent reached its stop
    bool released = false;       // the release settled (|F_net| < 0.02 m g and |v| < 1 mm/s for 30 frames)
    int insertFrames = 0, releaseFrames = 0;
    Real netForceFraction = 0;   // |F_net| / (m g) at the end
    Real speed = 0;              // m/s at the end
    Vec3 center;                 // of the box
    std::string stopReason;      // why the descent stopped
};

// Inserts a box into the mouth of a hung (settled) scene and lets it go (plan 7.3). Stage A: translation only.
class RigidLoad {
public:
    // The scene must be settled first. Throws std::runtime_error when the model has no mouth.
    RigidLoad(CrochetScene& scene, const LoadSpec& load);
    // Descend at 5 cm/s from 1 cm above the mouth until the bottom face is 1 cm above the lowest cloth point of the body,
    // or the vertical contact force on the box exceeds its weight, or maxFrames.
    bool insert(int maxFrames = 1500);
    // Two-way coupled release; true when it settled before maxFrames.
    bool release(int maxFrames = 1500);
    const LoadStats& stats() const { return stats_; }
    const LoadSpec& spec() const { return spec_; }
    Vec3 center() const { return c_; }
    // Force of the cloth on the box from the last step (contact forces whose vertices are nearer to the box than to the peg).
    Vec3 contactForceOnBox() const { return contact_; }
    void setDamping(Real perSecond) { damping_ = perSecond; }  // v *= exp(-damping dt) per frame

private:
    void frame(const Vec3& dc);
    Vec3 forceFromCloth();
    CrochetScene& scene_;
    LoadSpec spec_;
    std::shared_ptr<RoundedBoxCollider> box_;
    Vec3 c_, v_{0, 0, 0}, contact_{0, 0, 0};
    int calm_ = 0;  // consecutive calm frames of the release
    Real damping_ = 80;  // the plan says 8: measured on the pouch, 8 and 30 leave the box bobbing +-2 cm, 80 settles in ~330 frames
    LoadStats stats_;
    Real dt_ = 1.0 / 60.0;
};

}  // namespace fl
