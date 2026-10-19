// Yarn mode Y1: the Cosserat rod solver (docs/YARN_MODE_PLAN.md sections 3 and 7).
// Exit criteria: a chain stays inside its length; two linked rings stay linked when pulled; a twisted rod untwists to its rest twist;
// no strand pair closer than 2r - eps after relaxation. Plus the analytic checks that say the stiffnesses mean what they say
// (the catenary, the cantilever) and the timing of a swatch-sized system.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "check.hpp"
#include "failoring/crochet/rod.hpp"

using namespace fl;

#define CHECKP(cond, ...)                                 \
    do {                                                  \
        CHECK(cond);                                      \
        if (cond) {                                       \
            std::printf("PASS ");                         \
            std::printf(__VA_ARGS__);                     \
            std::printf("\n");                            \
        }                                                 \
    } while (0)

namespace {

std::vector<Vec3> ring(const Vec3& centre, const Vec3& ex, const Vec3& ey, Real R, int n) {
    std::vector<Vec3> p(n);
    for (int i = 0; i < n; ++i) {
        const Real a = 2.0 * kPi * i / n;
        p[i] = centre + ex * (R * std::cos(a)) + ey * (R * std::sin(a));
    }
    return p;
}

std::vector<Vec3> particles(const RodSystem& rs, int chain) {
    const RodChain& ch = rs.chains[chain];
    return std::vector<Vec3>(rs.x.begin() + ch.firstParticle, rs.x.begin() + ch.firstParticle + ch.particles);
}

std::vector<Vec3> line(const Vec3& a, const Vec3& b, int segments) {
    std::vector<Vec3> p(segments + 1);
    for (int i = 0; i <= segments; ++i) p[i] = a + (b - a) * ((Real)i / segments);
    return p;
}

// L = 2 a sinh(S / 2a): the catenary parameter `a` of a chain of length L hung over a span S, and its sag
Real catenarySag(Real L, Real S) {
    Real lo = 1e-4, hi = 1e3;  // L(a) = 2 a sinh(S/2a) falls with a
    for (int i = 0; i < 200; ++i) {
        const Real a = 0.5 * (lo + hi);
        if (2.0 * a * std::sinh(S / (2.0 * a)) > L) lo = a; else hi = a;
    }
    const Real a = 0.5 * (lo + hi);
    return a * (std::cosh(S / (2.0 * a)) - 1.0);
}

void testHelpers() {
    std::printf("[helpers]\n");
    const Quat qa = quatFromAxisAngle(normalized(Vec3{1, 2, 3}), 0.7);
    const Vec3 rv = rotationVector(qa);
    CHECKP(std::fabs(length(rv) - 0.7) < 1e-12, "rotationVector gives the angle (%.12f)", length(rv));
    const Vec3 t = rotate(quatFromTo(Vec3{0, 0, 1}, normalized(Vec3{1, 1, 0})), Vec3{0, 0, 1});
    CHECKP(length(t - normalized(Vec3{1, 1, 0})) < 1e-12, "quatFromTo turns a onto b");
    const Vec3 t2 = rotate(quatFromTo(Vec3{0, 0, 1}, Vec3{0, 0, -1}), Vec3{0, 0, 1});
    CHECKP(length(t2 - Vec3{0, 0, -1}) < 1e-12, "quatFromTo handles the half turn");
    const SegSegResult r = closestSegmentSegment({0, 0, 0}, {1, 0, 0}, {0.5, 1, -1}, {0.5, 1, 1});
    CHECKP(std::fabs(std::sqrt(r.dist2) - 1.0) < 1e-12 && std::fabs(r.s - 0.5) < 1e-12 && std::fabs(r.t - 0.5) < 1e-12,
           "closest points of two crossing segments");

    const auto a = ring({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, 1.0, 64);
    const auto b = ring({1, 0, 0}, {1, 0, 0}, {0, 0, 1}, 1.0, 64);
    const auto c = ring({5, 0, 0}, {1, 0, 0}, {0, 0, 1}, 1.0, 64);
    const Real lkAB = linkingNumber(a, b), lkAC = linkingNumber(a, c);
    CHECKP(std::fabs(std::fabs(lkAB) - 1.0) < 1e-6, "linking number of a Hopf link is +-1 (%.9f)", lkAB);
    CHECKP(std::fabs(lkAC) < 1e-6, "linking number of two separate rings is 0 (%.9f)", lkAC);
    std::vector<Vec3> br(b.rbegin(), b.rend());
    CHECKP(std::fabs(linkingNumber(a, br) + lkAB) < 1e-6, "reversing a ring flips the sign");
}

// A chain hung between two pins sags like a catenary and does not stretch.
void testCatenary() {
    std::printf("[catenary]\n");
    RodSystem rs;
    rs.params.EI = 1e-8;  // as limp as a string
    rs.params.contact = false;
    rs.params.damping = 4.0;
    const Real L = 0.30, S = 0.24;
    const int N = 60;
    // start from a half-sine arc of length L (found by bisection on its depth), a little off the catenary
    auto arc = [&](Real depth) {
        std::vector<Vec3> p(N + 1);
        for (int i = 0; i <= N; ++i) {
            const Real u = (Real)i / N;
            p[i] = Vec3{-0.5 * S + S * u, -depth * std::sin(kPi * u), 0.0};
        }
        return p;
    };
    auto polyLen = [](const std::vector<Vec3>& p) {
        Real t = 0;
        for (size_t i = 1; i < p.size(); ++i) t += length(p[i] - p[i - 1]);
        return t;
    };
    Real lo = 0, hi = 0.3;
    for (int i = 0; i < 60; ++i) {
        const Real mid = 0.5 * (lo + hi);
        if (polyLen(arc(mid)) < L) lo = mid; else hi = mid;
    }
    const int c = rs.addChain(arc(0.5 * (lo + hi)), false);
    rs.pin(0);
    rs.pin(N);
    const int steps = rs.relax(1.0 / 120.0, 1e-5, 6000);
    const Real len = rs.chainLength(c), rest = rs.chainRestLength(c);
    Real ymin = 0;
    for (const Vec3& p : particles(rs, c)) ymin = std::min(ymin, p.y);
    const Real sag = -ymin, want = catenarySag(rest, S);
    std::printf("  steps %d, chain length %.5f of %.5f, sag %.5f, catenary %.5f\n", steps, len, rest, sag, want);
    CHECKP(len < rest * 1.005 && len > rest * 0.995, "the chain stays within 0.5%% of its length (%.3f%%)", 100.0 * (len / rest - 1.0));
    CHECKP(std::fabs(sag - want) < 0.03 * want, "the sag is the catenary's within 3%% (%.2f%%)", 100.0 * (sag / want - 1.0));
}

// A clamped rod bends under its own weight like a beam: tip deflection q L^4 / 8 EI.
void testCantilever() {
    std::printf("[cantilever]\n");
    RodSystem rs;
    rs.params.EI = 1.2e-5;
    rs.params.GJ = 1.2e-5;
    rs.params.contact = false;
    rs.params.damping = 25.0;
    const int N = 50;
    const Real l = 0.002;
    const int c = rs.addChain(line({0, 0, 0}, {N * l, 0, 0}, N), false);
    rs.pin(0);
    rs.pin(1);
    rs.fixOrientation(0);
    const int steps = rs.relax(1.0 / 120.0, 1e-6, 20000);
    const Real L = (N - 1) * l, qw = rs.params.linearDensity * 9.81;
    const Real want = qw * L * L * L * L / (8.0 * rs.params.EI);
    const Real tip = rs.x[N].y;
    std::printf("  steps %d, tip %.6f, beam theory %.6f\n", steps, -tip, want);
    CHECKP(std::fabs(-tip - want) < 0.05 * want, "the tip deflection is the beam's within 5%% (%.2f%%)", 100.0 * (-tip / want - 1.0));
    (void)c;
}

// A twisted rod untwists to its rest twist; a rod twisted at its end carries the twist along evenly.
void testTwist() {
    std::printf("[twist]\n");
    {
        RodSystem rs;
        rs.params.gravity = Vec3{};
        rs.params.contact = false;
        rs.params.damping = 30.0;
        const int N = 30;
        const int c = rs.addChain(line({0, 0, 0}, {0, 0, N * 0.002}, N), false);
        const Real rest = 0.05;
        rs.setRestTwist(c, rest);
        const RodChain ch = rs.chains[c];
        for (int k = 0; k < ch.segments; ++k) rs.q[ch.firstSegment + k] = rs.q[ch.firstSegment + k] * quatFromAxisAngle(Vec3{0, 0, 1}, 0.2 * k);
        const auto before = rs.cumulativeTwist(c);
        const int steps = 600;
        for (int k = 0; k < steps; ++k) rs.step(1.0 / 120.0);
        Real worst = 0;
        for (int j = 0; j < ch.joints; ++j) worst = std::max(worst, std::fabs(rs.jointRotation(ch.firstJoint + j).z - rest));
        std::printf("  twist before %.3f rad, %d steps\n", before.back(), steps);
        CHECKP(worst < 2e-3, "a free rod relaxes to its rest twist: worst joint %.5f rad off %.2f", worst, rest);
    }
    {
        RodSystem rs;
        rs.params.gravity = Vec3{};
        rs.params.contact = false;
        rs.params.damping = 30.0;
        const int N = 30;
        const int c = rs.addChain(line({0, 0, 0}, {0, 0, N * 0.002}, N), false);
        const RodChain ch = rs.chains[c];
        const Real T = 2.8;  // less than a half turn: a quaternion cannot hold more in one step
        const int last = ch.firstSegment + ch.segments - 1;
        rs.q[last] = rs.q[last] * quatFromAxisAngle(Vec3{0, 0, 1}, T);
        rs.fixOrientation(ch.firstSegment);
        rs.fixOrientation(last);
        rs.pin(0);
        rs.pin(1);
        rs.pin(N - 1);
        rs.pin(N);
        const int steps = 1500;
        for (int k = 0; k < steps; ++k) rs.step(1.0 / 120.0);
        Real mean = 0, worst = 0, bend = 0;
        for (int j = 0; j < ch.joints; ++j) mean += rs.jointRotation(ch.firstJoint + j).z;
        mean /= ch.joints;
        for (int j = 0; j < ch.joints; ++j) {
            const Vec3 r = rs.jointRotation(ch.firstJoint + j);
            worst = std::max(worst, std::fabs(r.z - mean));
            bend = std::max(bend, std::hypot(r.x, r.y));
        }
        std::printf("  %d steps, twist per joint %.4f rad (a whole twist is %.4f), spread %.5f, bend %.5f\n", steps, mean, T / ch.joints, worst, bend);
        CHECKP(std::fabs(mean * ch.joints - T) < 0.02 * T, "the end twist is carried through the rod (%.3f of %.3f rad)", mean * ch.joints, T);
        CHECKP(worst < 0.03 * mean, "the twist is even along the rod (spread %.4f rad of %.4f)", worst, mean);
    }
}

// Two linked rings stay linked when one is pulled against the other; without contact they pass through each other.
Real linkedPull(bool contact, Real* gap, int* contacts) {
    RodSystem rs;
    rs.params.gravity = Vec3{};
    rs.params.contact = contact;
    rs.params.damping = 6.0;
    const Real R = 0.01;
    const int n = 40;
    const int ca = rs.addChain(ring({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, R, n), true, true);
    const int cb = rs.addChain(ring({R, 0, 0}, {1, 0, 0}, {0, 0, 1}, R, n), true, true);
    const RodChain A = rs.chains[ca];
    for (int i = 0; i < A.particles; ++i) rs.pin(A.firstParticle + i);
    const int grip = rs.chains[cb].firstParticle;  // the point of ring B at (2R, 0, 0)
    rs.pin(grip);
    const Real reach = 0.0383;  // just short of taut: the loop of B round the wire of A is pi R = 31 mm each way
    const int steps = 150;
    int touched = 0;
    for (int k = 1; k <= steps; ++k) {
        rs.movePin(grip, Vec3{R + R + (reach - 2.0 * R) * k / steps, 0, 0});
        rs.step(1.0 / 120.0);
        touched = std::max(touched, rs.lastContacts);
    }
    for (int k = 0; k < 120; ++k) rs.step(1.0 / 120.0);
    *gap = minStrandGap(rs, 2);
    *contacts = touched;
    return linkingNumber(particles(rs, ca), particles(rs, cb));
}

void testLinkedRings() {
    std::printf("[linked rings]\n");
    Real gap = 0;
    int touched = 0;
    const Real lk = linkedPull(true, &gap, &touched);
    std::printf("  with contact: linking number %.4f, smallest centre-line gap %.3f mm (2r = 2.5 mm), %d contacts at most\n", lk, gap * 1e3, touched);
    CHECKP(std::fabs(std::fabs(lk) - 1.0) < 0.01, "the rings are still linked after the pull (%.4f)", lk);
    CHECKP(touched > 0, "the rings touched while pulled (%d contacts)", touched);
    CHECKP(gap > 2.0 * 1.25e-3 * 0.9, "no strand pair closer than 0.9 x 2r (%.3f mm)", gap * 1e3);
    Real gap2 = 0;
    int touched2 = 0;
    const Real lk2 = linkedPull(false, &gap2, &touched2);
    std::printf("  without contact: linking number %.4f\n", lk2);
    CHECKP(std::fabs(lk2) < 0.01, "without contact the rings pass through each other (%.4f)", lk2);
}

// No strand pair closer than 2r - eps after relaxation: a rod pushed together on itself, and a rod pushed through another.
void testNonIntersection() {
    std::printf("[no interpenetration]\n");
    {
        RodSystem rs;
        rs.params.gravity = Vec3{};
        rs.params.damping = 6.0;
        const int N = 250;
        std::mt19937 rng(7);
        std::uniform_real_distribution<Real> u(-1.0, 1.0);
        std::vector<Vec3> pts = line({0, 0, 0}, {N * 0.002, 0, 0}, N);
        for (Vec3& p : pts) {
            p.y += 4e-4 * u(rng);
            p.z += 4e-4 * u(rng);
        }
        const int c = rs.addChain(pts, false);
        rs.pin(0);
        rs.pin(1);
        rs.pin(N);
        rs.pin(N - 1);
        const int steps = 240;
        int touched = 0;
        for (int k = 1; k <= steps; ++k) {
            const Real s = (Real)k / steps;
            rs.movePin(N, Vec3{N * 0.002 - 0.45 * s, 0.0, 0.0});
            rs.movePin(N - 1, Vec3{(N - 1) * 0.002 - 0.45 * s, 0.0, 0.0});
            rs.step(1.0 / 120.0);
            touched = std::max(touched, rs.lastContacts);
        }
        for (int k = 0; k < 360; ++k) {
            rs.step(1.0 / 120.0);
            touched = std::max(touched, rs.lastContacts);
        }
        const Real gap = minStrandGap(rs, 2);
        std::printf("  a rod 50 cm long pushed together to 5 cm: smallest gap %.3f mm (2r = 2.5 mm), %d contacts at most\n", gap * 1e3, touched);
        CHECKP(gap > 2.0 * rs.params.radius * 0.95, "no strand pair closer than 0.95 x 2r after relaxation (%.3f mm)", gap * 1e3);
        CHECKP(touched > 0, "the pushed rod really did touch itself (%d contacts)", touched);
        (void)c;
    }
    {
        // a strand dropped across another that lies on the floor: it ends up resting on it, not through it
        RodSystem rs;
        rs.params.floorOn = true;
        rs.params.damping = 6.0;
        const Real r = rs.params.radius;
        const int lying = rs.addChain(line({-0.05, r, 0}, {0.05, r, 0}, 50), false);
        const RodChain F = rs.chains[lying];
        for (int i = 0; i < F.particles; ++i) rs.pin(F.firstParticle + i);
        const int dropped = rs.addChain(line({0, 0.02, -0.05}, {0, 0.02, 0.05}, 50), false);
        const RodChain D = rs.chains[dropped];
        int touched = 0;
        for (int k = 0; k < 600; ++k) {
            rs.step(1.0 / 120.0);
            touched = std::max(touched, rs.lastContacts);
        }
        const Real gap = minStrandGap(rs, 2);
        const Real midY = rs.x[D.firstParticle + D.particles / 2].y, endY = rs.x[D.firstParticle].y;
        std::printf("  the dropped strand: middle at y = %.2f mm (3r = %.2f), ends at %.2f mm, smallest gap %.3f mm, %d contacts at most\n", midY * 1e3,
                    3.0 * r * 1e3, endY * 1e3, gap * 1e3, touched);
        CHECKP(midY > 2.9 * r, "the middle of the dropped strand rests on the one beneath, not through it (%.2f mm)", midY * 1e3);
        CHECKP(gap > 2.0 * r * 0.95, "no strand pair closer than 0.95 x 2r (%.3f mm)", gap * 1e3);
        CHECKP(touched > 0, "the strands touched (%d contacts)", touched);
    }
}

// Friction: a strand lying across another does not slide off when the plane tilts.
void testFriction() {
    std::printf("[friction]\n");
    auto run = [](Real mu) {
        RodSystem rs;
        rs.params.friction = mu;
        rs.params.gravity = Vec3{9.81 * 0.3, -9.81, 0};  // a tilt of about 17 degrees along x: mu 0.4 holds, mu 0 slides
        rs.params.damping = 3.0;
        const int base = rs.addChain(line({-0.05, 0, -0.03}, {-0.05, 0, 0.03}, 30), false);
        const RodChain B = rs.chains[base];
        for (int i = 0; i < B.particles; ++i) rs.pin(B.firstParticle + i);
        const int top = rs.addChain(line({-0.05 + 0.0, 0.0025, -0.03}, {-0.05 + 0.0, 0.0025, 0.03}, 30), false);
        const RodChain T = rs.chains[top];
        // the top strand is held by its ends so it cannot fall off the sides, but may slide along x
        const Real x0 = rs.x[T.firstParticle + 15].x;
        for (int k = 0; k < 300; ++k) rs.step(1.0 / 120.0);
        return rs.x[T.firstParticle + 15].x - x0;
    };
    // (the strand is free; the base is pinned and only a line: sliding is x displacement of the middle)
    const Real slid = run(0.0), held = run(0.4);
    std::printf("  slid %.2f mm without friction, %.2f mm with mu = 0.4\n", slid * 1e3, held * 1e3);
    CHECKP(std::fabs(held) < 0.25 * std::fabs(slid) || std::fabs(held) < 1e-3, "friction holds the strand (%.2f mm against %.2f mm)", held * 1e3, slid * 1e3);
}

// A rod made with sharp corners (a yarn loop turns 0.5 rad or more per 2 mm node) keeps its shape and does not stretch: the bend-twist
// Jacobian must be the exact one. With the small-angle one, strain went to 50 % in 20 steps.
void testSharpCorners() {
    std::printf("[sharp corners]\n");
    RodSystem rs;
    rs.params.gravity = Vec3{};
    rs.params.contact = false;
    rs.params.damping = 25.0;
    rs.params.EA = 30.0;
    // a square wave of 2 mm nodes with 90 degree corners and a ring of radius 5.5 mm (0.36 rad per node)
    std::vector<Vec3> pts;
    for (int k = 0; k < 6; ++k) {   // a meander: up 4 mm, across 4 mm, down 4 mm, across 4 mm, ...
        const Real x = 0.008 * k;
        pts.push_back({x, 0.0, 0.0});
        pts.push_back({x, 0.002, 0.0});
        pts.push_back({x, 0.004, 0.0});
        pts.push_back({x + 0.002, 0.004, 0.0});
        pts.push_back({x + 0.004, 0.004, 0.0});
        pts.push_back({x + 0.004, 0.002, 0.0});
        pts.push_back({x + 0.004, 0.0, 0.0});
        pts.push_back({x + 0.006, 0.0, 0.0});
    }
    for (int k = 0; k <= 16; ++k) {
        const Real a = 2.0 * kPi * k / 16;
        pts.push_back({0.05 + 0.0055 * std::sin(a), 0.0055 * (1 - std::cos(a)), 0.0});
    }
    const int c = rs.addChain(pts, false, true);
    rs.pin(0);
    Real maxAngle = 0;
    for (int j = 0; j < (int)rs.jointA.size(); ++j) maxAngle = std::max(maxAngle, length(rs.jointRotation(j)));
    for (int k = 0; k < 200; ++k) rs.step(1.0 / 120.0);
    Real strain = 0, drift = 0;
    for (int s = 0; s < rs.segmentCount(); ++s)
        strain = std::max(strain, std::fabs(length(rs.x[rs.segB[s]] - rs.x[rs.segA[s]]) / rs.restLen[s] - 1.0));
    for (int i = 0; i < rs.particleCount(); ++i) drift = std::max(drift, length(rs.x[i] - pts[i]));
    std::printf("  largest joint angle %.2f rad, after 200 steps: strain %.4f, drift from the made shape %.3f mm\n", maxAngle, strain, drift * 1e3);
    CHECKP(maxAngle > 1.4 && maxAngle < 2.0, "the test shape has sharp corners (%.2f rad)", maxAngle);
    CHECKP(strain < 0.02, "the stretch stays under 2%% (%.3f%%)", 100.0 * strain);
    CHECKP(drift < 0.5e-3, "a rod made in a shape stays in it (%.3f mm)", drift * 1e3);
    (void)c;
}

void testTiming() {
    std::printf("[timing]\n");
    RodSystem rs;
    rs.params.floorOn = true;
    const int cols = 100, rows = 60;
    std::vector<Vec3> pts;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const int cc = (r & 1) ? cols - 1 - c : c;
            pts.push_back(Vec3{cc * 0.002, 0.003 + r * 0.0027, 0.0});
        }
    rs.addChain(pts, false);
    const auto t0 = std::chrono::steady_clock::now();
    const int steps = 10;
    for (int k = 0; k < steps; ++k) rs.step(1.0 / 60.0);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / steps;
    std::printf("  %d particles, %d contacts: %.1f ms per 1/60 s step (%d substeps x %d sweeps)\n", rs.particleCount(), rs.lastContacts, ms,
                rs.params.substeps, rs.params.iterations);
    CHECKP(ms < 250.0, "a swatch-sized system steps in under 250 ms on one thread (%.1f ms)", ms);
}

}  // namespace

int main() {
    testHelpers();
    testCatenary();
    testCantilever();
    testTwist();
    testLinkedRings();
    testNonIntersection();
    testFriction();
    testSharpCorners();
    testTiming();
    FINISH();
}
