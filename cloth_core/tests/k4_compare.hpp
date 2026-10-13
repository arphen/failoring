#pragma once
// Shared by test_sdf_incremental.cpp and the real-mesh tool k4_gate.cpp: query-point sets and the accuracy metrics of
// an incremental SDF against a full re-bake of the same pose.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <random>
#include <vector>

#include "failoring/math.hpp"
#include "failoring/sdf_grid.hpp"
#include "failoring/sdf_lazy.hpp"

namespace k4 {

using fl::Real;
using fl::Vec3;
using Tris = std::vector<std::array<int, 3>>;

inline double cpuMs() { return 1e3 * double(std::clock()) / CLOCKS_PER_SEC; }

inline std::vector<Vec3> vertexNormals(const std::vector<Vec3>& P, const Tris& T) {
    std::vector<Vec3> n(P.size(), Vec3(0, 0, 0));
    for (const auto& t : T) {
        const Vec3 c = fl::cross(P[static_cast<size_t>(t[1])] - P[static_cast<size_t>(t[0])], P[static_cast<size_t>(t[2])] - P[static_cast<size_t>(t[0])]);
        for (int k = 0; k < 3; ++k) n[static_cast<size_t>(t[k])] += c;
    }
    for (auto& v : n) v = fl::normalized(v);
    return n;
}

// Stand-in for the garment: every `stride`-th vertex pushed out along its normal by a fixed offset in
// [lo, hi] metres (the cloth sits 1.5 mm + half its thickness from the body and hangs off it a little).
inline std::vector<Vec3> clothProxy(const std::vector<Vec3>& P, const std::vector<Vec3>& nrm, int stride, Real lo, Real hi, std::vector<int>* ids = nullptr) {
    std::vector<Vec3> q;
    for (size_t i = 0; i < P.size(); i += static_cast<size_t>(stride)) {
        const Real u = std::fmod(static_cast<Real>(i) * 0.6180339887, 1.0);
        q.push_back(P[i] + nrm[i] * (lo + (hi - lo) * u));
        if (ids) ids->push_back(static_cast<int>(i));
    }
    return q;
}

// Random points within `r` of a vertex (so within r of the surface), inside or outside.
inline std::vector<Vec3> randomShell(const std::vector<Vec3>& P, size_t usedVerts, int n, Real r, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<Real> U(0, 1);
    std::normal_distribution<Real> N(0, 1);
    std::vector<Vec3> q;
    for (int k = 0; k < n; ++k) {
        const Vec3 v = P[static_cast<size_t>(U(rng) * static_cast<Real>(usedVerts - 1))];
        const Vec3 d = fl::normalized(Vec3(N(rng), N(rng), N(rng)));
        q.push_back(v + d * (r * U(rng)));
    }
    return q;
}

// Accumulates deviations of a candidate field from a reference field over many frames.
struct Acc {
    std::vector<float> dev;     // |d_cand - d_ref| in mm
    std::vector<float> ang;     // gradient direction error in degrees
    long long n = 0, flips = 0; // sign flips (|d_ref| > 0.1 mm)
    double signedMax = 0;       // largest signed d_cand - d_ref in mm (kept for the sign of the bias)
    double sumSigned = 0;
};

inline void accumulate(Acc& a, const fl::SdfGrid& ref, const fl::SdfGridLazy& cand, const std::vector<Vec3>& Q, Real bandMax) {
    for (const Vec3& p : Q) {
        Vec3 gr, gc;
        const Real dr = ref.eval(p, &gr), dc = cand.eval(p, &gc);
        if (std::fabs(dr) > bandMax) continue;
        ++a.n;
        if (std::fabs(dr) > 1e-4 && (dr < 0) != (dc < 0)) ++a.flips;
        a.dev.push_back(static_cast<float>(std::fabs(dc - dr) * 1e3));
        a.sumSigned += (dc - dr) * 1e3;
        const Real lr = fl::length(gr), lc = fl::length(gc);
        if (lr > 1e-9 && lc > 1e-9) a.ang.push_back(static_cast<float>(std::acos(std::min(Real(1), std::max(Real(-1), fl::dot(gr, gc) / (lr * lc)))) * 180 / fl::kPi));
    }
}

inline double percentile(std::vector<float> v, double q) {
    if (v.empty()) return 0;
    const size_t k = std::min(v.size() - 1, static_cast<size_t>(q * static_cast<double>(v.size() - 1) + 0.5));
    std::nth_element(v.begin(), v.begin() + static_cast<long>(k), v.end());
    return v[k];
}
inline double maxOf(const std::vector<float>& v) { return v.empty() ? 0 : *std::max_element(v.begin(), v.end()); }

inline void report(const char* name, const Acc& a) {
    std::printf("  %-34s n=%8lld  |dd| mm: median %.4f  p95 %.4f  p99.9 %.4f  max %.4f   grad deg: p95 %.3f max %.2f   sign flips %lld\n", name, a.n, percentile(a.dev, 0.5), percentile(a.dev, 0.95),
                percentile(a.dev, 0.999), maxOf(a.dev), percentile(a.ang, 0.95), maxOf(a.ang), a.flips);
}

}  // namespace k4
