#pragma once
// Minimal vector math for the cloth core. Double precision on purpose: the
// solver uses very stiff constraints and we want verification runs that are
// not polluted by float round-off. Switch `Real` to float later if profiling
// says so (all code is written against `Real`).
#include <algorithm>
#include <cmath>

namespace fl {

using Real = double;
constexpr Real kPi = 3.14159265358979323846;

struct Vec2 {
    Real x = 0, y = 0;
    Vec2() = default;
    Vec2(Real x_, Real y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(Real s) const { return {x * s, y * s}; }
    Vec2 operator/(Real s) const { return {x / s, y / s}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
};
inline Real dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline Real cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
inline Real length(const Vec2& a) { return std::sqrt(dot(a, a)); }
inline Vec2 rotate(const Vec2& a, Real ang) {
    const Real c = std::cos(ang), s = std::sin(ang);
    return {c * a.x - s * a.y, s * a.x + c * a.y};
}

struct Vec3 {
    Real x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(Real x_, Real y_, Real z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(Real s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(Real s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(Real s) { x *= s; y *= s; z *= s; return *this; }
};
inline Vec3 operator*(Real s, const Vec3& v) { return v * s; }
inline Real dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Real length2(const Vec3& a) { return dot(a, a); }
inline Real length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalized(const Vec3& a, Real eps = 1e-30) {
    const Real l = length(a);
    return l > eps ? a / l : Vec3(0, 0, 0);
}

// 3x3 matrix, row-major. Used for the per-vertex blocks of the implicit solver.
struct Mat3 {
    Real m[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    Vec3 operator*(const Vec3& v) const {
        return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z, m[6] * v.x + m[7] * v.y + m[8] * v.z};
    }
    void addOuter(const Vec3& a, const Vec3& b, Real s) {
        m[0] += s * a.x * b.x; m[1] += s * a.x * b.y; m[2] += s * a.x * b.z;
        m[3] += s * a.y * b.x; m[4] += s * a.y * b.y; m[5] += s * a.y * b.z;
        m[6] += s * a.z * b.x; m[7] += s * a.z * b.y; m[8] += s * a.z * b.z;
    }
    void addDiag(Real d) { m[0] += d; m[4] += d; m[8] += d; }
    void setZero() { for (Real& v : m) v = 0; }
    // Inverse of a (symmetric positive definite) block; returns identity-scaled fallback if singular.
    Mat3 inverse() const {
        Mat3 r;
        const Real c00 = m[4] * m[8] - m[5] * m[7], c01 = m[5] * m[6] - m[3] * m[8], c02 = m[3] * m[7] - m[4] * m[6];
        const Real det = m[0] * c00 + m[1] * c01 + m[2] * c02;
        if (std::fabs(det) < 1e-300) { r.addDiag(1.0 / std::max(std::fabs(m[0]) + std::fabs(m[4]) + std::fabs(m[8]), Real(1e-30)) * 3); return r; }
        const Real id = 1.0 / det;
        r.m[0] = c00 * id;
        r.m[1] = (m[2] * m[7] - m[1] * m[8]) * id;
        r.m[2] = (m[1] * m[5] - m[2] * m[4]) * id;
        r.m[3] = c01 * id;
        r.m[4] = (m[0] * m[8] - m[2] * m[6]) * id;
        r.m[5] = (m[2] * m[3] - m[0] * m[5]) * id;
        r.m[6] = c02 * id;
        r.m[7] = (m[1] * m[6] - m[0] * m[7]) * id;
        r.m[8] = (m[0] * m[4] - m[1] * m[3]) * id;
        return r;
    }
};

// Rigid transform used to place a flat 2D panel into 3D: p3 = origin + ex*u + ey*v.
struct PanelPlacement {
    Vec3 origin{0, 0, 0};
    Vec3 ex{1, 0, 0};  // image of the panel's +u axis
    Vec3 ey{0, 1, 0};  // image of the panel's +v axis
    Vec3 apply(const Vec2& uv) const { return origin + ex * uv.x + ey * uv.y; }
};

}  // namespace fl
