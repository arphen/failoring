#pragma once
// Signed-distance colliders. The solver only needs `eval`: signed distance
// (negative inside) and the outward unit normal at the query point.
// Body meshes will plug in later through a voxelised SDF implementing the same
// interface; these analytic shapes are for tests, mannequins and drape rigs.
#include <memory>
#include <vector>

#include "failoring/error.hpp"
#include "failoring/math.hpp"

namespace fl {

class Collider {
public:
    virtual ~Collider() = default;
    // Returns signed distance; writes the outward unit normal into `n`.
    virtual Real eval(const Vec3& p, Vec3& n) const = 0;

    // ---- compliant skin (K1) -------------------------------------------------------------------------------
    // Relative stiffness of the soft skin layer at a point near the surface; the solver multiplies it with
    // SolverParams::skinStiffness (Pa/m). 1 = the global value, 0 = rigid here. Only used when the skin layer is
    // enabled. Analytic colliders carry one number (setSkinScale); UnionCollider answers with the nearest part;
    // GridCollider can look it up in a per-region label map (see skin.hpp). UNVERIFIED placeholder semantics: no
    // tissue stiffness is measured.
    virtual Real skinScale(const Vec3& /*p*/) const { return skinScale_; }
    void setSkinScale(Real s) { skinScale_ = s; }

protected:
    Real skinScale_ = 1.0;
};

class SphereCollider : public Collider {
public:
    SphereCollider(Vec3 c, Real r) : c_(c), r_(r) {}
    Real eval(const Vec3& p, Vec3& n) const override {
        Vec3 d = p - c_;
        Real l = length(d);
        n = l > 1e-12 ? d / l : Vec3(0, 1, 0);
        return l - r_;
    }
private:
    Vec3 c_;
    Real r_;
};

class CapsuleCollider : public Collider {
public:
    CapsuleCollider(Vec3 a, Vec3 b, Real r) : a_(a), b_(b), r_(r) {}
    Real eval(const Vec3& p, Vec3& n) const override {
        Vec3 ab = b_ - a_;
        Real t = dot(p - a_, ab) / std::max(dot(ab, ab), Real(1e-30));
        t = std::min(std::max(t, Real(0)), Real(1));
        Vec3 d = p - (a_ + ab * t);
        Real l = length(d);
        n = l > 1e-12 ? d / l : Vec3(0, 1, 0);
        return l - r_;
    }
private:
    Vec3 a_, b_;
    Real r_;
};

// Y-axis aligned capped cylinder (the pedestal of a Cusick drape tester, a waist, a limb...).
class CylinderCollider : public Collider {
public:
    CylinderCollider(Vec3 center, Real radius, Real halfHeight) : c_(center), r_(radius), h_(halfHeight) {}
    Real eval(const Vec3& p, Vec3& n) const override {
        Vec3 q = p - c_;
        Real rxz = std::sqrt(q.x * q.x + q.z * q.z);
        Real dx = rxz - r_, dy = std::fabs(q.y) - h_;
        const Real sy = q.y >= 0 ? 1 : -1;
        Vec3 dir = rxz > 1e-12 ? Vec3(q.x / rxz, 0, q.z / rxz) : Vec3(1, 0, 0);
        if (dx > 0 && dy > 0) {
            Vec3 g = dir * dx + Vec3(0, sy * dy, 0);
            Real l = length(g);
            n = g / l;
            return l;
        }
        if (dx > dy) n = dir; else n = Vec3(0, sy, 0);
        return std::max(dx, dy);  // exact outside (single axis), interior distance inside
    }
private:
    Vec3 c_;
    Real r_, h_;
};

class PlaneCollider : public Collider {
public:
    PlaneCollider(Vec3 point, Vec3 normal) : p_(point), n_(normalized(normal)) {}
    Real eval(const Vec3& p, Vec3& n) const override {
        n = n_;
        return dot(p - p_, n_);
    }
private:
    Vec3 p_, n_;
};

// Axis-aligned ellipsoid, first-order distance approximation (accurate near the surface).
class EllipsoidCollider : public Collider {
public:
    EllipsoidCollider(Vec3 c, Vec3 radii) : c_(c), r_(radii) {}
    Real eval(const Vec3& p, Vec3& n) const override {
        Vec3 q = p - c_;
        Vec3 a(q.x / r_.x, q.y / r_.y, q.z / r_.z);
        Vec3 b(a.x / r_.x, a.y / r_.y, a.z / r_.z);
        Real k0 = length(a), k1 = length(b);
        if (k1 < 1e-12) { n = Vec3(0, 1, 0); return -std::min({r_.x, r_.y, r_.z}); }
        n = b / k1;
        return k0 * (k0 - 1) / k1;
    }
private:
    Vec3 c_, r_;
};

// Oriented box with rounded edges and corners (a box prop, a laptop on which the cloth rests). Frame convention:
// `world = center + R * local`, i.e. `local = R^T (p - center)`; `rotation` is the row-major R so its columns are
// the world directions of the local axes (the same frame idea as the GDExtension's OrientedBoxCollider, §2.6).
// The SDF is the box of the core half extents H = halfExtents - cornerRadius, minus cornerRadius:
//   q_i = |l_i| - H_i,  d = length(max(q, 0)) + min(max_i q_i, 0) - r
// exact outside including the rounded slabs, and the (negative) distance to the core surface minus r inside the
// core - e.g. 1 cm inside a flat face with r < 1 cm reads exactly -0.01. Normals are exact away from the core's
// medial axis: outside the core normalize(max(q, 0)), inside the core the sign of the nearest face's coordinate.
// Rounded corners avoid the jumping normals of a sharp box (the reason §2.6 exists). cornerRadius = 0 is allowed
// and degenerates to the sharp box.
class RoundedBoxCollider : public Collider {
public:
    RoundedBoxCollider(Vec3 center, Mat3 rotation, Vec3 halfExtents, Real cornerRadius)
        : center_(center), half_(halfExtents), r_(cornerRadius) {
        checkShape();
        setPose(center, rotation);
    }

    // Moves (and re-orients) the box; the shape stays. `rotation` must be a rotation matrix as above.
    void setPose(Vec3 center, Mat3 rotation) {
        center_ = center;
        axis_[0] = rotation * Vec3(1, 0, 0);
        axis_[1] = rotation * Vec3(0, 1, 0);
        axis_[2] = rotation * Vec3(0, 0, 1);
    }

    Real eval(const Vec3& p, Vec3& n) const override {
        const Vec3 d = p - center_;
        const Real l[3] = {dot(d, axis_[0]), dot(d, axis_[1]), dot(d, axis_[2])};
        const Real h[3] = {half_.x - r_, half_.y - r_, half_.z - r_};  // core half extents H
        Real q[3], nl[3] = {0, 0, 0};
        bool outside = false;
        for (int i = 0; i < 3; ++i) {
            q[i] = std::fabs(l[i]) - h[i];
            if (q[i] > 0) {
                outside = true;
                nl[i] = l[i] >= 0 ? q[i] : -q[i];
            }
        }
        Real dist;
        if (outside) {
            const Real len = std::sqrt(nl[0] * nl[0] + nl[1] * nl[1] + nl[2] * nl[2]);
            dist = len - r_;
            for (Real& c : nl) c /= len;
        } else {
            int m = 0;
            for (int i = 1; i < 3; ++i)
                if (q[i] > q[m]) m = i;
            dist = q[m] - r_;
            nl[m] = l[m] >= 0 ? 1 : -1;
        }
        n = axis_[0] * nl[0] + axis_[1] * nl[1] + axis_[2] * nl[2];
        return dist;
    }

private:
    void checkShape() const {
        if (!(half_.x > 0 && half_.y > 0 && half_.z > 0))
            FL_THROW(std::invalid_argument, "RoundedBoxCollider: halfExtents must all be > 0");
        if (!(r_ >= 0 && r_ < std::min(half_.x, std::min(half_.y, half_.z))))
            FL_THROW(std::invalid_argument, "RoundedBoxCollider: cornerRadius must be >= 0 and < min(halfExtents)");
    }
    Vec3 center_{0, 0, 0};
    Vec3 half_{0.5, 0.5, 0.5};
    Real r_ = 0;
    Vec3 axis_[3] = {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)};  // world directions of the local axes (R's columns)
};

// Union of several colliders (a mannequin built from primitives).
class UnionCollider : public Collider {
public:
    void add(std::shared_ptr<Collider> c) { parts_.push_back(std::move(c)); }
    Real eval(const Vec3& p, Vec3& n) const override {
        Real best = 1e30;
        Vec3 bn(0, 1, 0);
        for (const auto& c : parts_) {
            Vec3 ni;
            Real d = c->eval(p, ni);
            if (d < best) { best = d; bn = ni; }
        }
        n = bn;
        return best;
    }
    // Skin stiffness scale of the part nearest to p (ties: first part).
    Real skinScale(const Vec3& p) const override {
        Real best = 1e30, sc = skinScale_;
        for (const auto& c : parts_) {
            Vec3 ni;
            const Real d = c->eval(p, ni);
            if (d < best) { best = d; sc = c->skinScale(p); }
        }
        return sc;
    }
private:
    std::vector<std::shared_ptr<Collider>> parts_;
};

}  // namespace fl
