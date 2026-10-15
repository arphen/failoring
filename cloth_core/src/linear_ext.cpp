// Single-precision reference of the GPU conjugate gradient (gdextension/src/gpu_cg.glsl): the same steps in the same
// order, so the GPU result can be compared with it to float rounding. See LinearSystemView in cloth.hpp.
#include <cmath>
#include <vector>

#include "failoring/cloth.hpp"

namespace fl {

namespace {
template <class T> struct F3T { T x = 0, y = 0, z = 0; };
template <class T> inline F3T<T> mul(const T* m, const F3T<T>& v) { return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z, m[6] * v.x + m[7] * v.y + m[8] * v.z}; }
template <class T> inline T dot3(const F3T<T>& a, const F3T<T>& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
// inverse of a 3x3 (row-major) by the adjugate, as the shader does
template <class T> void inv3(const T* a, T* o) {
    const T c00 = a[4] * a[8] - a[5] * a[7], c01 = a[5] * a[6] - a[3] * a[8], c02 = a[3] * a[7] - a[4] * a[6];
    const T det = a[0] * c00 + a[1] * c01 + a[2] * c02;
    const T id = det != T(0) ? T(1) / det : T(0);
    o[0] = c00 * id; o[1] = (a[2] * a[7] - a[1] * a[8]) * id; o[2] = (a[1] * a[5] - a[2] * a[4]) * id;
    o[3] = c01 * id; o[4] = (a[0] * a[8] - a[2] * a[6]) * id; o[5] = (a[2] * a[3] - a[0] * a[5]) * id;
    o[6] = c02 * id; o[7] = (a[1] * a[6] - a[0] * a[7]) * id; o[8] = (a[0] * a[4] - a[1] * a[3]) * id;
}
template <class T>
int cgBlockJacobiT(const LinearSystemView& s, std::vector<Vec3>& xout) {
    using F3 = F3T<T>;
    const int n = s.n;
    const int nnz = s.rowStart[n];
    std::vector<T> val(static_cast<size_t>(nnz) * 9), filt(static_cast<size_t>(n) * 9), pinv(static_cast<size_t>(n) * 9), mt(n);
    for (int k = 0; k < nnz; ++k) for (int q = 0; q < 9; ++q) val[9 * k + q] = static_cast<T>(s.val[k].m[q]);
    for (int i = 0; i < n; ++i) {
        mt[i] = static_cast<T>(s.massTerm[i]);
        for (int q = 0; q < 9; ++q) filt[9 * i + q] = static_cast<T>(s.filt[i].m[q]);
        T d[9];
        for (int q = 0; q < 9; ++q) d[q] = val[9 * s.diag[i] + q];
        d[0] += mt[i]; d[4] += mt[i]; d[8] += mt[i];
        inv3(d, &pinv[9 * i]);
    }
    std::vector<F3> x(n), r(n), z(n), p(n), ap(n);
    auto prec = [&](int i) { z[i] = mul(&filt[9 * i], mul(&pinv[9 * i], r[i])); };
    for (int i = 0; i < n; ++i) {
        const F3 b{static_cast<T>(s.rhs[i].x), static_cast<T>(s.rhs[i].y), static_cast<T>(s.rhs[i].z)};
        r[i] = mul(&filt[9 * i], b);
        prec(i);
        p[i] = z[i];
    }
    T rzf = 0, rrf = 0;
    for (int i = 0; i < n; ++i) { rzf += dot3(r[i], z[i]); rrf += dot3(r[i], r[i]); }
    const T bnorm = std::sqrt(rrf);
    int it = 0;
    if (bnorm > T(1e-30)) {
        for (; it < s.maxIterations; ++it) {
            T pap = 0;
            for (int i = 0; i < n; ++i) {
                F3 acc{mt[i] * p[i].x, mt[i] * p[i].y, mt[i] * p[i].z};
                for (int k = s.rowStart[i]; k < s.rowStart[i + 1]; ++k) {
                    const F3 t = mul(&val[9 * k], p[s.col[k]]);
                    acc.x += t.x; acc.y += t.y; acc.z += t.z;
                }
                ap[i] = mul(&filt[9 * i], acc);
                pap += dot3(p[i], ap[i]);
            }
            if (!(pap > 0)) break;
            const T alpha = rzf / pap;
            T rr2 = 0;
            for (int i = 0; i < n; ++i) {
                x[i].x += alpha * p[i].x; x[i].y += alpha * p[i].y; x[i].z += alpha * p[i].z;
                r[i].x -= alpha * ap[i].x; r[i].y -= alpha * ap[i].y; r[i].z -= alpha * ap[i].z;
                rr2 += dot3(r[i], r[i]);
            }
            if (std::sqrt(rr2) <= static_cast<T>(s.tolerance) * bnorm) { ++it; break; }
            T rzn = 0;
            for (int i = 0; i < n; ++i) { prec(i); rzn += dot3(r[i], z[i]); }
            const T beta = rzn / rzf;
            rzf = rzn;
            for (int i = 0; i < n; ++i) { p[i].x = z[i].x + beta * p[i].x; p[i].y = z[i].y + beta * p[i].y; p[i].z = z[i].z + beta * p[i].z; }
        }
    }
    xout.resize(n);
    for (int i = 0; i < n; ++i) xout[i] = Vec3(x[i].x, x[i].y, x[i].z);
    return it;
}

}  // namespace

int cgBlockJacobiFloat(const LinearSystemView& s, std::vector<Vec3>& x) { return cgBlockJacobiT<float>(s, x); }
int cgBlockJacobiDouble(const LinearSystemView& s, std::vector<Vec3>& x) { return cgBlockJacobiT<double>(s, x); }

}  // namespace fl
