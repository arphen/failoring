#include "failoring/sdf_lazy.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace fl {

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

// fine brick layout: BS^3 samples per brick, x fastest (the same as SdfGrid)
constexpr int BSH = 1, BS = 1 << BSH, BV = BS * BS * BS;

// Uniform quadratic B-spline weights (and derivatives) of the three samples around t in [-0.5, 0.5] (as SdfGrid).
inline void bspline2(Real t, Real* w, Real* d) {
    const Real a = 0.5 - t, b = 0.5 + t;
    w[0] = 0.5 * a * a; w[1] = 0.75 - t * t; w[2] = 0.5 * b * b;
    d[0] = -a; d[1] = -2 * t; d[2] = b;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// winding rows: a copy of the construction in sdf_grid.cpp (that one lives in an anonymous namespace); see there for
// the reasoning. Signed ray crossings along +x, rows shifted by a tiny irrational fraction of h.
void SdfGridLazy::Rows::build(const std::vector<Tri>& T, const Vec3& o, Real h, int ny_, int nz_) {
    ny = ny_; nz = nz_;
    const double ey = 0.000137 * h, ez = 0.000263 * h;
    auto nx = [](const Tri& t) { return (t.b.y - t.a.y) * (t.c.z - t.a.z) - (t.b.z - t.a.z) * (t.c.y - t.a.y); };
    auto rowsOf = [&](const Tri& t, int& j0, int& j1, int& k0, int& k1) {
        const double ylo = std::min({t.a.y, t.b.y, t.c.y}), yhi = std::max({t.a.y, t.b.y, t.c.y});
        const double zlo = std::min({t.a.z, t.b.z, t.c.z}), zhi = std::max({t.a.z, t.b.z, t.c.z});
        j0 = std::max(0, static_cast<int>(std::ceil((ylo - o.y - ey) / h))); j1 = std::min(ny - 1, static_cast<int>(std::floor((yhi - o.y - ey) / h)));
        k0 = std::max(0, static_cast<int>(std::ceil((zlo - o.z - ez) / h))); k1 = std::min(nz - 1, static_cast<int>(std::floor((zhi - o.z - ez) / h)));
    };
    auto hit = [&](const Tri& t, double y, double z, double& x) {
        const double ay = t.a.y - y, az = t.a.z - z, by = t.b.y - y, bz = t.b.z - z, cy = t.c.y - y, cz = t.c.z - z;
        const double wa = by * cz - bz * cy, wb = cy * az - cz * ay, wc = ay * bz - az * by;
        if (!((wa >= 0 && wb >= 0 && wc >= 0) || (wa <= 0 && wb <= 0 && wc <= 0))) return false;
        const double s = wa + wb + wc;
        if (std::fabs(s) < 1e-300) return false;
        x = (wa * t.a.x + wb * t.b.x + wc * t.c.x) / s;
        return true;
    };
    start.assign(static_cast<size_t>(ny) * nz + 1, 0);
    for (const Tri& t : T) {
        if (nx(t) == 0) continue;
        int j0, j1, k0, k1;
        rowsOf(t, j0, j1, k0, k1);
        for (int k = k0; k <= k1; ++k)
            for (int j = j0; j <= j1; ++j) {
                double x;
                if (hit(t, o.y + j * h + ey, o.z + k * h + ez, x)) ++start[static_cast<size_t>(j + ny * k) + 1];
            }
    }
    for (size_t r = 0; r + 1 < start.size(); ++r) start[r + 1] += start[r];
    cross.assign(static_cast<size_t>(start.back()), {0.0, 0});
    std::vector<int> fill(start.begin(), start.end() - 1);
    for (const Tri& t : T) {
        const double n = nx(t);
        if (n == 0) continue;
        int j0, j1, k0, k1;
        rowsOf(t, j0, j1, k0, k1);
        for (int k = k0; k <= k1; ++k)
            for (int j = j0; j <= j1; ++j) {
                double x;
                if (hit(t, o.y + j * h + ey, o.z + k * h + ez, x)) cross[static_cast<size_t>(fill[static_cast<size_t>(j + ny * k)]++)] = {x, n > 0 ? 1 : -1};
            }
    }
    suffix.assign(cross.size() + 1, 0);
    for (size_t r = 0; r + 1 < start.size(); ++r) {
        std::sort(cross.begin() + start[r], cross.begin() + start[r + 1]);
        int acc = 0;
        for (int q = start[r + 1] - 1; q >= start[r]; --q) { acc += cross[static_cast<size_t>(q)].second; suffix[static_cast<size_t>(q)] = acc; }
    }
}

int SdfGridLazy::Rows::winding(double x, int j, int k) const {
    const size_t r = static_cast<size_t>(j + ny * k);
    auto b = cross.begin() + start[r], e = cross.begin() + start[r + 1];
    auto it = std::upper_bound(b, e, std::make_pair(x, 2));
    return it == e ? 0 : suffix[static_cast<size_t>(it - cross.begin())];
}

// ---------------------------------------------------------------------------------------------------------------
// Squared distance from p to triangle k (Ericson, Real-Time Collision Detection 5.1.5), as in sdf_grid.cpp.
double SdfGridLazy::dist2(const Vec3& p, int k) const {
    ++stats_.triangleTests;
    const Tri& T = T_[static_cast<size_t>(k)];
    const Vec3 ab = T.b - T.a, ac = T.c - T.a, ap = p - T.a;
    const double d1 = dot(ab, ap), d2 = dot(ac, ap);
    Vec3 q;
    if (d1 <= 0 && d2 <= 0) q = T.a;
    else {
        const Vec3 bp = p - T.b;
        const double d3 = dot(ab, bp), d4 = dot(ac, bp);
        if (d3 >= 0 && d4 <= d3) q = T.b;
        else {
            const double vc = d1 * d4 - d3 * d2;
            if (vc <= 0 && d1 >= 0 && d3 <= 0) q = T.a + ab * (d1 / (d1 - d3));
            else {
                const Vec3 cp = p - T.c;
                const double d5 = dot(ab, cp), d6 = dot(ac, cp);
                if (d6 >= 0 && d5 <= d6) q = T.c;
                else {
                    const double vb = d5 * d2 - d1 * d6;
                    if (vb <= 0 && d2 >= 0 && d6 <= 0) q = T.a + ac * (d2 / (d2 - d6));
                    else {
                        const double va = d3 * d6 - d5 * d4;
                        if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) q = T.b + (T.c - T.b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
                        else {
                            const double s = va + vb + vc;
                            if (std::fabs(s) < 1e-300) q = T.a;  // sliver
                            else {
                                const double den = 1.0 / s;
                                q = T.a + ab * (vb * den) + ac * (vc * den);
                            }
                        }
                    }
                }
            }
        }
    }
    const Vec3 d = p - q;
    return dot(d, d);
}

Real SdfGridLazy::exactDistance(const Vec3& p) const {
    double best = std::numeric_limits<double>::infinity();
    for (int k = 0; k < static_cast<int>(T_.size()); ++k) best = std::min(best, dist2(p, k));
    return std::sqrt(best);
}

// ---------------------------------------------------------------------------------------------------------------
bool SdfGridLazy::setBody(const std::vector<Vec3>& verts, const std::vector<std::array<int, 3>>& tris, const SdfLazyParams& p) {
    const auto t0 = Clock::now();
    ready_ = false;
    if (verts.empty() || tris.empty()) return false;
    p_ = p;
    const int nv = static_cast<int>(verts.size());
    // ---- topology, only when the triangle list changed
    bool same = tris_.size() == tris.size() && used_.size() == verts.size();
    if (same) same = std::equal(tris.begin(), tris.end(), tris_.begin());
    if (!same) {
        tris_ = tris;
        used_.assign(static_cast<size_t>(nv), 0);
        adj_.assign(tris.size(), {-1, -1, -1});
        std::unordered_map<std::uint64_t, std::pair<int, int>> open;
        open.reserve(tris.size() * 2);
        std::vector<int> cnt(static_cast<size_t>(nv) + 1, 0);
        for (int ti = 0; ti < static_cast<int>(tris.size()); ++ti)
            for (int e = 0; e < 3; ++e) {
                const std::uint32_t a = static_cast<std::uint32_t>(tris[static_cast<size_t>(ti)][e]), b = static_cast<std::uint32_t>(tris[static_cast<size_t>(ti)][(e + 1) % 3]);
                used_[a] = 1;
                ++cnt[a + 1];
                const std::uint64_t key = (static_cast<std::uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
                auto it = open.find(key);
                if (it == open.end()) open.emplace(key, std::make_pair(ti, e));
                else {
                    adj_[static_cast<size_t>(ti)][e] = it->second.first;
                    adj_[static_cast<size_t>(it->second.first)][it->second.second] = ti;
                    open.erase(it);
                }
            }
        vtStart_.assign(static_cast<size_t>(nv) + 1, 0);
        for (int v = 0; v < nv; ++v) vtStart_[static_cast<size_t>(v) + 1] = vtStart_[static_cast<size_t>(v)] + cnt[static_cast<size_t>(v) + 1];
        vtList_.assign(static_cast<size_t>(vtStart_.back()), 0);
        std::vector<int> fill(vtStart_.begin(), vtStart_.end() - 1);
        for (int ti = 0; ti < static_cast<int>(tris.size()); ++ti)
            for (int e = 0; e < 3; ++e) vtList_[static_cast<size_t>(fill[static_cast<size_t>(tris[static_cast<size_t>(ti)][e])]++)] = ti;
    }
    // ---- pose
    auto tp = Clock::now();
    V_ = verts;
    T_.resize(tris_.size());
    for (size_t k = 0; k < tris_.size(); ++k) T_[k] = {V_[static_cast<size_t>(tris_[k][0])], V_[static_cast<size_t>(tris_[k][1])], V_[static_cast<size_t>(tris_[k][2])]};
    stats_ = SdfLazyStats();
    stats_.triangles = static_cast<int>(tris_.size());
    Vec3 lo = verts[0], hi = verts[0];
    for (const Vec3& v : verts) {
        lo = Vec3(std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z));
        hi = Vec3(std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z));
    }
    const double msPose = msSince(tp);
    tp = Clock::now();
    // ---- vertex hash (only vertices that belong to a triangle)
    {
        Vec3 ulo(1e30, 1e30, 1e30), uhi(-1e30, -1e30, -1e30);
        for (int v = 0; v < nv; ++v)
            if (used_[static_cast<size_t>(v)]) {
                const Vec3& q = V_[static_cast<size_t>(v)];
                ulo = Vec3(std::min(ulo.x, q.x), std::min(ulo.y, q.y), std::min(ulo.z, q.z));
                uhi = Vec3(std::max(uhi.x, q.x), std::max(uhi.y, q.y), std::max(uhi.z, q.z));
            }
        const Real reach = std::max(p_.fineRadius + 0.05, p_.coarseCap) + 2 * p_.hashCell;
        hashLo_ = ulo - Vec3(reach, reach, reach);
        hashInv_ = 1 / p_.hashCell;
        hx_ = static_cast<int>(std::ceil((uhi.x - ulo.x + 2 * reach) * hashInv_)) + 1;
        hy_ = static_cast<int>(std::ceil((uhi.y - ulo.y + 2 * reach) * hashInv_)) + 1;
        hz_ = static_cast<int>(std::ceil((uhi.z - ulo.z + 2 * reach) * hashInv_)) + 1;
        const size_t nc = static_cast<size_t>(hx_) * hy_ * hz_;
        hStart_.assign(nc + 1, 0);
        std::vector<int> cellOf(static_cast<size_t>(nv), -1);
        for (int v = 0; v < nv; ++v)
            if (used_[static_cast<size_t>(v)]) {
                const Vec3& q = V_[static_cast<size_t>(v)];
                const int cx = static_cast<int>((q.x - hashLo_.x) * hashInv_), cy = static_cast<int>((q.y - hashLo_.y) * hashInv_), cz = static_cast<int>((q.z - hashLo_.z) * hashInv_);
                const int c = (cz * hy_ + cy) * hx_ + cx;
                cellOf[static_cast<size_t>(v)] = c;
                ++hStart_[static_cast<size_t>(c) + 1];
            }
        for (size_t c = 0; c < nc; ++c) hStart_[c + 1] += hStart_[c];
        hList_.assign(static_cast<size_t>(hStart_.back()), 0);
        std::vector<int> fill(hStart_.begin(), hStart_.end() - 1);
        for (int v = 0; v < nv; ++v)
            if (cellOf[static_cast<size_t>(v)] >= 0) hList_[static_cast<size_t>(fill[static_cast<size_t>(cellOf[static_cast<size_t>(v)])]++)] = v;
    }
    const double msHash = msSince(tp);
    // ---- fine lattice, as SdfGrid::bake
    const Real h = p_.grid.h;
    const Real reachF = p_.grid.band + 1.5 * std::sqrt(3.0) * h;
    const Real pad = reachF + 2 * h;
    fh_ = h; finv_ = 1 / h;
    fo_ = lo - Vec3(pad, pad, pad);
    auto bricks = [&](Real ext) { return (static_cast<int>(std::ceil(ext / h)) + 1 + BS - 1) / BS; };
    bnx_ = bricks(hi.x - lo.x + 2 * pad); bny_ = bricks(hi.y - lo.y + 2 * pad); bnz_ = bricks(hi.z - lo.z + 2 * pad);
    const size_t nb = static_cast<size_t>(bnx_) * bny_ * bnz_;
    // ---- coarse lattice, as SdfGrid::bake
    ch_ = h * p_.grid.coarseRatio; cinv_ = 1 / ch_;
    co_ = lo - Vec3(p_.grid.margin, p_.grid.margin, p_.grid.margin);
    cnx_ = static_cast<int>(std::ceil((hi.x - lo.x + 2 * p_.grid.margin) * cinv_)) + 1;
    cny_ = static_cast<int>(std::ceil((hi.y - lo.y + 2 * p_.grid.margin) * cinv_)) + 1;
    cnz_ = static_cast<int>(std::ceil((hi.z - lo.z + 2 * p_.grid.margin) * cinv_)) + 1;
    const size_t nc = static_cast<size_t>(cnx_) * cny_ * cnz_;
    if (brickStamp_.size() != nb || coarse_.size() != nc) {  // sizes changed: restart the epochs
        brickStamp_.assign(nb, 0); nearStamp_.assign(nb, 0); brickSlot_.assign(nb, 0); brickNear_.assign(nb, 0);
        coarse_.assign(nc, 0.f); coarseStamp_.assign(nc, 0);
        epoch_ = 0;
    }
    ++epoch_;
    poolUsed_ = 0;
    tp = Clock::now();
    fineRows_.build(T_, fo_, h, BS * bny_, BS * bnz_);
    stats_.msRowsFine = msSince(tp);
    tp = Clock::now();
    coarseRows_.build(T_, co_, ch_, cny_, cnz_);
    stats_.msRowsCoarse = msSince(tp);
    stats_.msPose = msPose; stats_.msHash = msHash;
    stats_.msPrepare = msSince(t0);
    ready_ = true;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Closest vertices by ring search over the vertex hash.
bool SdfGridLazy::nearestVertex(const Vec3& p, Real maxR, double& d2) const {
    const int cx = static_cast<int>(std::floor((p.x - hashLo_.x) * hashInv_)), cy = static_cast<int>(std::floor((p.y - hashLo_.y) * hashInv_)), cz = static_cast<int>(std::floor((p.z - hashLo_.z) * hashInv_));
    if (cx < 0 || cy < 0 || cz < 0 || cx >= hx_ || cy >= hy_ || cz >= hz_) return false;
    const Real cell = p_.hashCell;
    double best = std::numeric_limits<double>::infinity();
    const int rmax = static_cast<int>(std::ceil(maxR * hashInv_)) + 1;
    for (int r = 0; r <= rmax; ++r) {
        for (int z = std::max(0, cz - r); z <= std::min(hz_ - 1, cz + r); ++z)
            for (int y = std::max(0, cy - r); y <= std::min(hy_ - 1, cy + r); ++y) {
                const bool edgeYZ = (z == cz - r || z == cz + r || y == cy - r || y == cy + r);
                const int step = edgeYZ ? 1 : std::max(1, 2 * r);
                for (int x = cx - r; x <= cx + r; x += step) {
                    if (x < 0 || x >= hx_) continue;
                    const size_t c = (static_cast<size_t>(z) * hy_ + y) * hx_ + x;
                    for (int q = hStart_[c]; q < hStart_[c + 1]; ++q) {
                        const Vec3 d = V_[static_cast<size_t>(hList_[static_cast<size_t>(q)])] - p;
                        best = std::min(best, dot(d, d));
                    }
                }
            }
        // everything outside the block of rings 0..r is at least lb away
        const Real lb = std::min({p.x - (hashLo_.x + (cx - r) * cell), hashLo_.x + (cx + r + 1) * cell - p.x, p.y - (hashLo_.y + (cy - r) * cell),
                                  hashLo_.y + (cy + r + 1) * cell - p.y, p.z - (hashLo_.z + (cz - r) * cell), hashLo_.z + (cz + r + 1) * cell - p.z});
        if (best <= lb * lb) break;
        if (lb > maxR) break;
    }
    d2 = best;
    return best <= maxR * maxR;
}

bool SdfGridLazy::nearest(const Vec3& p, Real maxR, double& d2out, int& triOut, int hint) const {
    ++stats_.nearestQueries;
    if (hint >= 0 && p_.warmStart == 1) {  // warm start: walk from the triangle that was closest to a neighbouring sample
        int bt = hint;
        double best = dist2(p, bt);
        for (int steps = 0; steps < 64; ++steps) {
            int nb = bt;
            double nd = best;
            for (int e = 0; e < 3; ++e) {
                const int t = adj_[static_cast<size_t>(bt)][e];
                if (t < 0) continue;
                const double d = dist2(p, t);
                if (d < nd) { nd = d; nb = t; }
            }
            if (nb == bt) break;
            bt = nb; best = nd;
        }
        d2out = best; triOut = bt;
        return best <= maxR * maxR;
    }
    const int cx = static_cast<int>(std::floor((p.x - hashLo_.x) * hashInv_)), cy = static_cast<int>(std::floor((p.y - hashLo_.y) * hashInv_)), cz = static_cast<int>(std::floor((p.z - hashLo_.z) * hashInv_));
    if (cx < 0 || cy < 0 || cz < 0 || cx >= hx_ || cy >= hy_ || cz >= hz_) return false;
    const Real cell = p_.hashCell;
    const int K = std::max(1, std::min(3, p_.vertexFan));
    double bd[3] = {1e300, 1e300, 1e300};
    int bv[3] = {-1, -1, -1};
    const int rmax = static_cast<int>(std::ceil(maxR * hashInv_)) + 1;
    for (int r = 0; r <= rmax; ++r) {
        for (int z = std::max(0, cz - r); z <= std::min(hz_ - 1, cz + r); ++z)
            for (int y = std::max(0, cy - r); y <= std::min(hy_ - 1, cy + r); ++y) {
                const bool edgeYZ = (z == cz - r || z == cz + r || y == cy - r || y == cy + r);
                const int step = edgeYZ ? 1 : std::max(1, 2 * r);
                for (int x = cx - r; x <= cx + r; x += step) {
                    if (x < 0 || x >= hx_) continue;
                    const size_t c = (static_cast<size_t>(z) * hy_ + y) * hx_ + x;
                    for (int q = hStart_[c]; q < hStart_[c + 1]; ++q) {
                        const int v = hList_[static_cast<size_t>(q)];
                        const Vec3 d = V_[static_cast<size_t>(v)] - p;
                        const double dd = dot(d, d);
                        if (dd < bd[K - 1]) {
                            int s = K - 1;
                            while (s > 0 && bd[s - 1] > dd) { bd[s] = bd[s - 1]; bv[s] = bv[s - 1]; --s; }
                            bd[s] = dd; bv[s] = v;
                        }
                    }
                }
            }
        const Real lb = std::min({p.x - (hashLo_.x + (cx - r) * cell), hashLo_.x + (cx + r + 1) * cell - p.x, p.y - (hashLo_.y + (cy - r) * cell),
                                  hashLo_.y + (cy + r + 1) * cell - p.y, p.z - (hashLo_.z + (cz - r) * cell), hashLo_.z + (cz + r + 1) * cell - p.z});
        if (bd[K - 1] <= lb * lb) break;
        if (lb > maxR) break;
    }
    if (bv[0] < 0 || bd[0] > maxR * maxR) return false;
    double best = std::numeric_limits<double>::infinity();
    int bt = -1;
    if (hint >= 0) { best = dist2(p, hint); bt = hint; }
    for (int s = 0; s < K; ++s) {
        if (bv[s] < 0) break;
        for (int q = vtStart_[static_cast<size_t>(bv[s])]; q < vtStart_[static_cast<size_t>(bv[s]) + 1]; ++q) {
            const int t = vtList_[static_cast<size_t>(q)];
            const double d = dist2(p, t);
            if (d < best) { best = d; bt = t; }
        }
    }
    // walk to closer edge-adjacent triangles
    for (int steps = 0; steps < 64 && bt >= 0; ++steps) {
        int nb = bt;
        double nd = best;
        for (int e = 0; e < 3; ++e) {
            const int t = adj_[static_cast<size_t>(bt)][e];
            if (t < 0) continue;
            const double d = dist2(p, t);
            if (d < nd) { nd = d; nb = t; }
        }
        if (nb == bt) break;
        bt = nb; best = nd;
    }
    if (bt < 0) return false;
    d2out = best;
    triOut = bt;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
float SdfGridLazy::fineSample(int i, int j, int k, int& hint) const {
    const Vec3 q = fo_ + Vec3(i * fh_, j * fh_, k * fh_);
    const Real maxR = p_.fineRadius + 0.05;
    double d2;
    int t;
    double d = maxR;
    if (nearest(q, maxR, d2, t, p_.warmStart ? hint : -1)) { d = std::sqrt(d2); hint = t; }
    else if (hint < 0 || !p_.warmStart) hint = -1;
    return fineRows_.winding(q.x, j, k) > 0 ? static_cast<float>(-d) : static_cast<float>(d);
}

float SdfGridLazy::coarseSample(int i, int j, int k) const {
    const size_t idx = (static_cast<size_t>(k) * cny_ + j) * cnx_ + i;
    if (coarseStamp_[idx] == epoch_) return coarse_[idx];
    ++stats_.coarseSamples;
    const Vec3 q = co_ + Vec3(i * ch_, j * ch_, k * ch_);
    double d2;
    int t;
    double d = p_.coarseCap;
    if (nearest(q, p_.coarseCap, d2, t)) d = std::sqrt(d2);
    const float v = coarseRows_.winding(q.x, j, k) > 0 ? static_cast<float>(-d) : static_cast<float>(d);
    coarse_[idx] = v;
    coarseStamp_[idx] = epoch_;
    return v;
}

const float* SdfGridLazy::ensureBrick(int bi, int bj, int bk) const {
    const size_t idx = (static_cast<size_t>(bk) * bny_ + bj) * bnx_ + bi;
    if (brickStamp_[idx] == epoch_) return nullptr;  // caller reads the slot; nullptr = already there
    ++stats_.fineBricks;
    const int slot = poolUsed_++;
    if (static_cast<size_t>(slot + 1) * BV > pool_.size()) pool_.resize(std::max<size_t>(pool_.size() * 2, static_cast<size_t>(slot + 1024) * BV));
    {   // the 8 samples in a path where consecutive samples are neighbours; each starts its search at the previous answer
        static const int path[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 0, 1}, {0, 0, 1}};
        int hint = -1;
        if (p_.warmStart == 1) {  // full search at the brick centre gives the first hint
            const Vec3 c = fo_ + Vec3(((bi << BSH) + 0.5) * fh_, ((bj << BSH) + 0.5) * fh_, ((bk << BSH) + 0.5) * fh_);
            double d2;
            int t;
            if (nearest(c, p_.fineRadius + 0.065, d2, t, -1)) hint = t;
        }
        for (int q = 0; q < 8; ++q) {
            const int lx = path[q][0], ly = path[q][1], lz = path[q][2];
            pool_[static_cast<size_t>(slot) * BV + (((lz << BSH) + ly) << BSH) + lx] = fineSample((bi << BSH) + lx, (bj << BSH) + ly, (bk << BSH) + lz, hint);
        }
    }
    brickSlot_[idx] = slot;
    brickStamp_[idx] = epoch_;
    return nullptr;
}

bool SdfGridLazy::evalFine(const Vec3& p, Real& v, Vec3* grad) const {
    const Real gx = (p.x - fo_.x) * finv_, gy = (p.y - fo_.y) * finv_, gz = (p.z - fo_.z) * finv_;
    const int ix = static_cast<int>(std::floor(gx + 0.5)), iy = static_cast<int>(std::floor(gy + 0.5)), iz = static_cast<int>(std::floor(gz + 0.5));
    if (ix < 1 || iy < 1 || iz < 1 || ix + 1 >= BS * bnx_ || iy + 1 >= BS * bny_ || iz + 1 >= BS * bnz_) return false;
    // near the surface? decided once per brick (of the nearest sample) from the closest mesh vertex
    {
        const size_t bidx = (static_cast<size_t>(iz >> BSH) * bny_ + (iy >> BSH)) * bnx_ + (ix >> BSH);
        if (nearStamp_[bidx] != epoch_) {
            const Vec3 c = fo_ + Vec3(((ix >> BSH) * BS + 0.5) * fh_, ((iy >> BSH) * BS + 0.5) * fh_, ((iz >> BSH) * BS + 0.5) * fh_);
            double d2;
            brickNear_[bidx] = nearestVertex(c, p_.fineRadius + 1.75 * fh_, d2) ? 1 : 2;
            nearStamp_[bidx] = epoch_;
        }
        if (brickNear_[bidx] != 1) return false;
    }
    Real wx[3], wy[3], wz[3], dx[3], dy[3], dz[3];
    bspline2(gx - ix, wx, dx); bspline2(gy - iy, wy, dy); bspline2(gz - iz, wz, dz);
    int bx[3], lx[3], by[3], ly[3], bz[3], lz[3];
    for (int t = 0; t < 3; ++t) {
        bx[t] = (ix - 1 + t) >> BSH; lx[t] = (ix - 1 + t) & (BS - 1);
        by[t] = (iy - 1 + t) >> BSH; ly[t] = (iy - 1 + t) & (BS - 1);
        bz[t] = (iz - 1 + t) >> BSH; lz[t] = (iz - 1 + t) & (BS - 1);
    }
    for (int c = 0; c < 3; ++c)
        for (int b = 0; b < 3; ++b)
            for (int a = 0; a < 3; ++a) ensureBrick(bx[a], by[b], bz[c]);  // cheap when computed (a stamp compare)
    Real val = 0, g0 = 0, g1 = 0, g2 = 0;
    for (int c = 0; c < 3; ++c)
        for (int b = 0; b < 3; ++b) {
            const int lrow = ((lz[c] << BSH) + ly[b]) << BSH;
            const Real wyz = wy[b] * wz[c], dyz = dy[b] * wz[c], wdz = wy[b] * dz[c];
            for (int a = 0; a < 3; ++a) {
                const size_t bidx = (static_cast<size_t>(bz[c]) * bny_ + by[b]) * bnx_ + bx[a];
                const Real s = pool_[static_cast<size_t>(brickSlot_[bidx]) * BV + lrow + lx[a]];
                val += wx[a] * wyz * s;
                g0 += dx[a] * wyz * s;
                g1 += wx[a] * dyz * s;
                g2 += wx[a] * wdz * s;
            }
        }
    v = val;
    if (grad) *grad = Vec3(g0, g1, g2) * finv_;
    return true;
}

Real SdfGridLazy::evalCoarse(const Vec3& p, Vec3* grad) const {
    Real gx = (p.x - co_.x) * cinv_, gy = (p.y - co_.y) * cinv_, gz = (p.z - co_.z) * cinv_;
    const Real cx = std::min(std::max(gx, Real(1)), Real(cnx_ - 2)), cy = std::min(std::max(gy, Real(1)), Real(cny_ - 2)),
               cz = std::min(std::max(gz, Real(1)), Real(cnz_ - 2));
    const Vec3 off((gx - cx) * ch_, (gy - cy) * ch_, (gz - cz) * ch_);
    gx = cx; gy = cy; gz = cz;
    const int ix = static_cast<int>(std::floor(gx + 0.5)), iy = static_cast<int>(std::floor(gy + 0.5)), iz = static_cast<int>(std::floor(gz + 0.5));
    Real wx[3], wy[3], wz[3], dx[3], dy[3], dz[3];
    bspline2(gx - ix, wx, dx); bspline2(gy - iy, wy, dy); bspline2(gz - iz, wz, dz);
    Real val = 0, g0 = 0, g1 = 0, g2 = 0;
    for (int c = 0; c < 3; ++c)
        for (int b = 0; b < 3; ++b) {
            const Real wyz = wy[b] * wz[c], dyz = dy[b] * wz[c], wdz = wy[b] * dz[c];
            for (int a = 0; a < 3; ++a) {
                const Real s = coarseSample(ix - 1 + a, iy - 1 + b, iz - 1 + c);
                val += wx[a] * wyz * s;
                g0 += dx[a] * wyz * s;
                g1 += wx[a] * dyz * s;
                g2 += wx[a] * wdz * s;
            }
        }
    const Real o = length(off);
    if (o > 0) {
        if (grad) *grad = off / o;
        return val + o;
    }
    if (grad) *grad = Vec3(g0, g1, g2) * cinv_;
    return val;
}

Real SdfGridLazy::eval(const Vec3& p, Vec3* grad) const {
    if (!ready_) {
        if (grad) *grad = Vec3(0, 1, 0);
        return 1e30;
    }
    Real v;
    if (evalFine(p, v, grad)) {
        ++stats_.fineQueries;
        return v;
    }
    ++stats_.coarseQueries;
    return evalCoarse(p, grad);
}

void SdfGridLazy::computedSamples(std::vector<Vec3>& pos, std::vector<float>& val) const {
    pos.clear(); val.clear();
    for (int bk = 0; bk < bnz_; ++bk)
        for (int bj = 0; bj < bny_; ++bj)
            for (int bi = 0; bi < bnx_; ++bi) {
                const size_t idx = (static_cast<size_t>(bk) * bny_ + bj) * bnx_ + bi;
                if (brickStamp_[idx] != epoch_) continue;
                for (int l = 0; l < BV; ++l) {
                    pos.push_back(fo_ + Vec3(((bi << BSH) + (l & (BS - 1))) * fh_, ((bj << BSH) + ((l >> BSH) & (BS - 1))) * fh_, ((bk << BSH) + (l >> (2 * BSH))) * fh_));
                    val.push_back(pool_[static_cast<size_t>(brickSlot_[idx]) * BV + l]);
                }
            }
}

void SdfGridLazy::prefetch(const Vec3* pts, int n) const {
    for (int i = 0; i < n; ++i) eval(pts[i], nullptr);
}

}  // namespace fl
