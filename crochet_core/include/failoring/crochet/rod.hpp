#pragma once
// Yarn mode, phase Y1 (docs/YARN_MODE_PLAN.md section 3): a Cosserat rod solver. SI units (m, kg, s).
//
// A yarn is a chain of particles joined by segments; every segment carries an orientation (a unit quaternion whose local z axis is
// the segment's tangent) and the orientations are what carry the twist. Position and orientation based dynamics (Kugelstadt and
// Schomer 2016) on XPBD (Macklin, Mueller and Chentanez 2016): stretch-shear per segment, bend-twist per joint, capsule contact with
// Coulomb friction between segments of any chains. Sub-stepped, Gauss-Seidel, a spatial hash for the contact pairs.
//
// Twist: the rod's rest Darboux vector is the rest bend plus the rest twist; a yarn's own ply twist is NOT a rod twist (the plies'
// helix is drawn on top by the renderer). What the rod carries is the twist added to or removed from the yarn, which is what makes
// stitches lean. Forming a loop twists the yarn; `cumulativeTwist` reads that twist back.
#include <cstdint>
#include <vector>

#include "failoring/math.hpp"

namespace fl {

// A unit quaternion (w, x, y, z); the rotation is applied as q v q*.
struct Quat {
    Real w = 1, x = 0, y = 0, z = 0;
    Quat() = default;
    Quat(Real w_, Real x_, Real y_, Real z_) : w(w_), x(x_), y(y_), z(z_) {}
};
Quat operator*(const Quat& a, const Quat& b);
inline Quat conj(const Quat& q) { return {q.w, -q.x, -q.y, -q.z}; }
Quat normalized(const Quat& q);
Vec3 rotate(const Quat& q, const Vec3& v);
Quat quatFromAxisAngle(const Vec3& axis, Real angle);
Quat quatFromTo(const Vec3& a, const Vec3& b);  // the shortest rotation taking unit a to unit b
Vec3 rotationVector(const Quat& q);             // the log map, |result| <= pi

struct RodParams {
    // material (SI). Defaults: a limp cotton worsted strand; STARTER values, not measured (plan section 10)
    Real radius = 1.25e-3;        // m, the contact and render radius of a strand
    Real linearDensity = 5.0e-4;  // kg/m
    Real EA = 100.0;              // N, stretch stiffness
    Real GAs = 100.0;             // N, shear stiffness
    Real EI = 2.0e-6;             // N m^2, bend stiffness
    Real GJ = 2.0e-6;             // N m^2, twist stiffness
    // contact
    Real friction = 0.4;          // Coulomb mu between strands (static and kinetic alike)
    Real staticFriction = 0.0;    // Coulomb mu between a strand and the statics (a hook, a finger)
    Real contactSkin = 0.5;       // pairs closer than 2 radius + skin * radius enter the contact list
    bool contact = true;          // off: strands pass through each other (a debugging tool and the mutant of the linking test)
    int contactSkipSegments = 2;  // segments of one chain this close (in index) do not touch
    Real radiusScale = 1.0;       // contact radius scale (the plan's "yarn thickness" slider)
    // A flat yarn: in a dense fabric a yarn is squashed, wide in the plane of the fabric and thin across it. With ribbonWidth > 0 the cross-section of every strand is an ellipse with
    // these half widths (at the strand's own radius = params.radius; they scale with it and with radiusScale), its thin axis along the fabric's normal `ribbonNormal` squared to the
    // strand's tangent, and the contact asks for the distance the ellipses need (2 b where strands cross over each other, 2 a side by side). A strand that runs along the
    // normal (a leg through an eye) has the round section of the same area.
    Real ribbonWidth = 0.0, ribbonThickness = 0.0;
    Vec3 ribbonNormal{0, 0, 1};
    Real shapeStiffness = 0.0;    // N/m of the soft spring that pulls a particle back to its anchor (0 = none): the shape memory of a designed piece
    // dynamics
    Vec3 gravity{0, -9.81, 0};
    Real damping = 2.0;           // 1/s, velocity damping
    int substeps = 8;
    int iterations = 2;           // solver iterations per substep (a direct rod solve plus a contact sweep each)
    bool direct = true;           // open chains: solve all their stretch-shear and bend-twist constraints at once (a banded solve)
                                  // instead of sweeping them one by one; sweeps carry stretch and bend along a chain one segment per sweep
    Real maxMoveRadii = 0.4;      // a particle moves at most this many radii in a substep (tunnelling guard)
    bool floorOn = false;
    Real floorY = 0.0;            // a plane with the normal +y, for particles
};

// A fixed capsule (a sphere when a == b) strands cannot enter: the hook, a peg, a table edge. Moved between steps it moves smoothly over the
// step's substeps (and with staticFriction carries the yarn along).
struct StaticCapsule {
    Vec3 a, b;
    Real r = 0;
    Real squash = 1.0;   // the yarn's radius against this capsule, as a fraction of its own (wool squashes in a hook's throat)
};

// A ring the yarn runs through: the yarn passes through `at` (it may slide along itself through it, but not leave it). `seg` is the segment
// last found there (the search looks a few segments either side). Moved between steps, it moves smoothly over the substeps.
struct SlideRing {
    Vec3 at{};
    int seg = 0;
    Vec3 from_{};
    bool started_ = false;
};

struct RodChain {
    int firstParticle = 0, particles = 0, firstSegment = 0, segments = 0, firstJoint = 0, joints = 0;
    bool closed = false;
};

class RodSystem {
public:
    RodParams params;

    // particles
    std::vector<Vec3> x, xPrev, v;
    std::vector<Real> w;               // inverse mass; 0 = pinned
    std::vector<Real> wFree;           // the inverse mass a pinned particle gets back when it is released
    std::vector<Real> wBase;           // the inverse mass of the real yarn (wFree is this over the mass scale)
    std::vector<Vec3> extForce;        // N, applied every step until cleared
    std::vector<Vec3> pinTarget;       // where a pinned particle is heading (it moves linearly over each step)
    std::vector<int> particleChain;
    std::vector<Vec3> anchor;          // where a particle was designed to be (see shapeStiffness)
    // segments (segment s joins particles segA[s] and segB[s])
    std::vector<int> segA, segB, segChain, segIndex;
    std::vector<Quat> q, qPrev;
    std::vector<Vec3> omega;           // world angular velocity
    std::vector<Real> wq;              // inverse rotational inertia; 0 = orientation fixed
    std::vector<Real> restLen;         // m; the stretch constraint's length: scale it for slack
    std::vector<Real> slack;           // per segment: its stretch and shear compliance times this (1 = the yarn's; huge = no constraint, a yarn
                                       // running freely through a hand)
    std::vector<Real> radius;          // m
    std::vector<Real> mass;
    // joints (joint j relates segments jointA[j] and jointB[j] = the next one)
    std::vector<int> jointA, jointB;
    std::vector<Vec3> restDarboux;     // rotation vector of the relative rotation at rest, in the first segment's frame (rad)
    std::vector<RodChain> chains;
    std::vector<StaticCapsule> statics;
    std::vector<SlideRing> rings;

    // Adds a chain through `pts` (>= 2 points; >= 3 when closed). The frames are parallel transported along it. `restFromShape`: the
    // bend of the given shape is the rest bend (a ring that stays a ring); otherwise the rest is straight. Returns the chain index.
    int addChain(const std::vector<Vec3>& pts, bool closed, bool restFromShape = false);
    // Appends points to the END of the LAST chain (open chains only): one particle and one segment per point, frames parallel transported
    // from the chain's last segment. This is how yarn mode grows the yarn as the work is made.
    void extendChain(const std::vector<Vec3>& pts);
    void popEnd();   // removes the last particle (and its segment and joint) of the last chain
    Real restBendMemory = 1.0;   // how much of the bend a yarn is made with extendChain it keeps as its rest bend (0: springs back straight)
    // Resets the rest lengths of a chain to `scale` times the lengths of its present segments.
    void setRestLengthsFromShape(int chain, Real scale = 1.0);
    void scaleRestLengths(Real factor);  // slack: every rest length times factor (1 = as made)
    void setRestTwist(int chain, Real radPerSegment);  // the rest twist of every joint of a chain

    // Only the particles within `radius` of `centre` are simulated; the others stand still (their inverse masses are zeroed and the segments
    // between them are not tested for contact), which keeps a local poke cheap on a big piece. `thaw` puts everything back.
    void freezeOutside(const Vec3& centre, Real radius);
    void thaw();
    bool frozen() const { return !savedW_.empty(); }

    // The segments' rotational inertia times `s`. At the step sizes of an interactive view the bend and twist constraints are hard whatever
    // EI and GJ are, because a segment's own inertia is tiny; a large inertia makes them as soft as the stiffnesses say. The rest shape is not
    // affected (the work is quasi-static). Segments whose orientation is fixed stay fixed.
    void setRotationalInertiaScale(Real s);
    // The particles' masses times `s` (pseudo-mass). A yarn weighs micrograms per node, so at an interactive step size every spring is stiff
    // compared to the step and acts as a hard constraint; with a heavy pseudo-mass the shape-memory and bend springs are as soft as their stiffness
    // says (a response of a few Hz). Only the dynamics change, not the rest shape; only sensible with gravity off.
    void setMassScale(Real s);

    void pin(int particle);
    void unpin(int particle);
    void movePin(int particle, const Vec3& target) { pinTarget[particle] = target; }
    void setAnchorsToShape() { anchor = x; }   // the present shape is the designed one
    void fixOrientation(int segment) { wq[segment] = 0; }

    // How far two strands (segments) are from touching as the yarn is shaped: the distance of their centre lines minus the distance the cross-sections need, negative when they
    // overlap (isotropic yarn: minus the two radii). The segments are not tested for being neighbours.
    Real contactGap(int segA, int segB) const;
    // the half widths of a strand's cross-section (width, thickness) and its thin axis (valid when ribbonWidth > 0)
    void ribbonSection(int seg, Real* a, Real* b, Vec3* thin, Vec3* wide) const;
    void step(Real dt);
    // Runs steps of `dt` until the largest particle speed is below `tol` (m/s) or `maxSteps` have run; returns the steps run.
    int relax(Real dt, Real tol, int maxSteps);

    int particleCount() const { return (int)x.size(); }
    int segmentCount() const { return (int)segA.size(); }
    Real chainLength(int chain) const;      // the chain's present length (sum of segment lengths)
    Real chainRestLength(int chain) const;
    // Per-joint twist of the relative rotation (about the first segment's own axis, rad), summed from the first joint: entry k is the
    // twist accumulated up to and including joint k.
    std::vector<Real> cumulativeTwist(int chain) const;
    Vec3 jointRotation(int joint) const;  // the relative rotation vector of joint j, in the first segment's frame
    Real kineticSpeedMax() const;

    // statistics of the last step()
    int lastContacts = 0;

private:
    struct Pair { int a, b; };
    std::vector<Pair> pairs_;
    Real rotInertiaScale_ = 1.0, massScale_ = 1.0;
    std::vector<Real> savedW_, savedWq_;   // the inverse masses a freeze set aside
    std::vector<char> segActive_;          // per segment: not frozen (a pair of two frozen segments is not tested)
    std::vector<Real> lambdaS_;        // per segment, 3 comps
    std::vector<Real> lambdaB_;        // per joint, 3 comps
    std::vector<Real> lambdaA_;        // per particle, 3 comps (the anchor springs)
    void buildPairs();
    void solveStretchShear(int s, Real h2inv);
    void solveChainDirect(int chain, Real h2inv);
    void solveBendTwist(int j, Real h2inv);
    void solveContacts(Real h);
    void solveFloor(Real h);
    void solveStatics(const std::vector<StaticCapsule>& cur, const std::vector<StaticCapsule>& before);
    void solveRings(Real frac);
    std::vector<StaticCapsule> staticsPrev_;   // where the statics were at the end of the last step
    void solveAnchors(Real h2inv);
    bool skipPair(int a, int b) const;
};

// helpers the tests and the renderer share
struct SegSegResult { Real s, t, dist2; Vec3 pa, pb; };
SegSegResult closestSegmentSegment(const Vec3& p0, const Vec3& p1, const Vec3& q0, const Vec3& q1);
// The Gauss linking number of two closed polygons (an integer for disjoint curves; returned unrounded).
Real linkingNumber(const std::vector<Vec3>& a, const std::vector<Vec3>& b);
// The smallest distance between two segments of the system that are more than `skip` apart in their chain (or in different chains).
Real minStrandGap(const RodSystem& rs, int skip = 2);

}  // namespace fl
