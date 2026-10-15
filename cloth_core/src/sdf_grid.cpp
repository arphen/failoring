#include "failoring/sdf_grid.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>

namespace fl {

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

struct Tri {
    Vec3 a, b, c;
    Vec3 n;  // unit face normal
};

struct Mesh {
    std::vector<Tri> t;
    std::vector<std::array<int, 3>> adj;  // edge-adjacent triangles (-1 on open edges)
    // Squared distance from p to triangle k (Ericson, Real-Time Collision Detection 5.1.5).
    inline double dist2(const Vec3& p, int k) const {
        const Tri& T = t[static_cast<size_t>(k)];
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
                                const double den = 1.0 / (va + vb + vc);
                                q = T.a + ab * (vb * den) + ac * (vc * den);
                            }
                        }
                    }
                }
            }
        }
        const Vec3 d = p - q;
        return dot(d, d);
    }
};

Mesh buildMesh(const std::vector<Vec3>& V, const std::vector<std::array<int, 3>>& T) {
    Mesh m;
    m.t.reserve(T.size());
    std::vector<std::array<int, 3>> kept;
    for (const auto& tr : T) {
        Tri x;
        x.a = V[static_cast<size_t>(tr[0])]; x.b = V[static_cast<size_t>(tr[1])]; x.c = V[static_cast<size_t>(tr[2])];
        const Vec3 cr = cross(x.b - x.a, x.c - x.a);
        const Real l = length(cr);
        if (l < 1e-16) continue;  // degenerate sliver: contributes nothing
        x.n = cr / l;
        m.t.push_back(x);
        kept.push_back(tr);
    }
    m.adj.assign(kept.size(), {-1, -1, -1});
    std::unordered_map<std::uint64_t, std::pair<int, int>> open;
    open.reserve(kept.size() * 2);
    for (int ti = 0; ti < static_cast<int>(kept.size()); ++ti)
        for (int e = 0; e < 3; ++e) {
            const std::uint32_t a = static_cast<std::uint32_t>(kept[static_cast<size_t>(ti)][e]), b = static_cast<std::uint32_t>(kept[static_cast<size_t>(ti)][(e + 1) % 3]);
            const std::uint64_t key = (static_cast<std::uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
            auto it = open.find(key);
            if (it == open.end()) open.emplace(key, std::make_pair(ti, e));
            else {
                m.adj[static_cast<size_t>(ti)][e] = it->second.first;
                m.adj[static_cast<size_t>(it->second.first)][it->second.second] = ti;
                open.erase(it);
            }
        }
    return m;
}

// Winding numbers of the samples of one grid level, from signed ray crossings along +x: every triangle is
// rasterised onto the (y,z) sample rows it covers; a crossing counts +1 where the ray leaves a solid (n.x > 0) and
// -1 where it enters. The sum over crossings beyond a sample is its winding number (1 inside, 0 outside, 2 where
// two parts of the mesh overlap), which stays correct for self-intersecting meshes, unlike a parity count or a
// closest-feature normal. Rows are shifted by a tiny irrational fraction of h so that they never hit edges exactly.
struct Rows {
    int ny = 0, nz = 0;
    std::vector<int> start;              // CSR over rows (j + ny * k)
    std::vector<std::pair<double, int>> cross;  // (x, +-1), sorted by x within a row
    std::vector<int> suffix;             // suffix[r] = sum of signs from position r to the row end
    void build(const Mesh& M, const Vec3& o, Real h, int ny_, int nz_) {
        ny = ny_; nz = nz_;
        const double ey = 0.000137 * h, ez = 0.000263 * h;
        auto rowsOf = [&](const Tri& T, int& j0, int& j1, int& k0, int& k1) {
            const double ylo = std::min({T.a.y, T.b.y, T.c.y}), yhi = std::max({T.a.y, T.b.y, T.c.y});
            const double zlo = std::min({T.a.z, T.b.z, T.c.z}), zhi = std::max({T.a.z, T.b.z, T.c.z});
            j0 = std::max(0, static_cast<int>(std::ceil((ylo - o.y - ey) / h))); j1 = std::min(ny - 1, static_cast<int>(std::floor((yhi - o.y - ey) / h)));
            k0 = std::max(0, static_cast<int>(std::ceil((zlo - o.z - ez) / h))); k1 = std::min(nz - 1, static_cast<int>(std::floor((zhi - o.z - ez) / h)));
        };
        // x of the crossing of row (y,z) with triangle T, if any
        auto hit = [&](const Tri& T, double y, double z, double& x) {
            const double ay = T.a.y - y, az = T.a.z - z, by = T.b.y - y, bz = T.b.z - z, cy = T.c.y - y, cz = T.c.z - z;
            const double wa = by * cz - bz * cy, wb = cy * az - cz * ay, wc = ay * bz - az * by;  // 2D edge functions
            if (!((wa >= 0 && wb >= 0 && wc >= 0) || (wa <= 0 && wb <= 0 && wc <= 0))) return false;
            const double s = wa + wb + wc;
            if (std::fabs(s) < 1e-300) return false;
            x = (wa * T.a.x + wb * T.b.x + wc * T.c.x) / s;
            return true;
        };
        start.assign(static_cast<size_t>(ny) * nz + 1, 0);
        for (const Tri& T : M.t) {
            if (T.n.x == 0) continue;
            int j0, j1, k0, k1;
            rowsOf(T, j0, j1, k0, k1);
            for (int k = k0; k <= k1; ++k)
                for (int j = j0; j <= j1; ++j) {
                    double x;
                    if (hit(T, o.y + j * h + ey, o.z + k * h + ez, x)) ++start[static_cast<size_t>(j + ny * k) + 1];
                }
        }
        for (size_t r = 0; r + 1 < start.size(); ++r) start[r + 1] += start[r];
        cross.assign(static_cast<size_t>(start.back()), {0.0, 0});
        std::vector<int> fill(start.begin(), start.end() - 1);
        for (const Tri& T : M.t) {
            if (T.n.x == 0) continue;
            int j0, j1, k0, k1;
            rowsOf(T, j0, j1, k0, k1);
            for (int k = k0; k <= k1; ++k)
                for (int j = j0; j <= j1; ++j) {
                    double x;
                    if (hit(T, o.y + j * h + ey, o.z + k * h + ez, x)) cross[static_cast<size_t>(fill[static_cast<size_t>(j + ny * k)]++)] = {x, T.n.x > 0 ? 1 : -1};
                }
        }
        suffix.assign(cross.size() + 1, 0);
        for (size_t r = 0; r + 1 < start.size(); ++r) {
            std::sort(cross.begin() + start[r], cross.begin() + start[r + 1]);
            int acc = 0;
            for (int q = start[r + 1] - 1; q >= start[r]; --q) { acc += cross[static_cast<size_t>(q)].second; suffix[static_cast<size_t>(q)] = acc; }
        }
    }
    // winding number of the point (x, row j, row k)
    int winding(double x, int j, int k) const {
        const size_t r = static_cast<size_t>(j + ny * k);
        auto b = cross.begin() + start[r], e = cross.begin() + start[r + 1];
        auto it = std::upper_bound(b, e, std::make_pair(x, 2));
        return it == e ? 0 : suffix[static_cast<size_t>(it - cross.begin())];
    }
};

// Brick layout of the fine level: BS^3 samples per brick (BS = 2), x fastest.
constexpr int BSH = 1, BS = 1 << BSH, BV = BS * BS * BS, BVSH = 3 * BSH;

// One grid level. Exact distances are seeded next to every triangle, then closest triangles are propagated outwards
// in order of increasing distance (Dial's bucket queue, so each sample is finalised once): a finalised sample
// offers its triangle to its unfinished 6-neighbours. Near the surface (d < refineDist) a finalised sample first
// walks to closer edge-adjacent triangles, which makes it exact when the mesh is finer than the grid.
// `Idx` maps (i,j,k) <-> sample number.
template <class Idx>
void bakeLevel(const Mesh& M, const Mesh& Msign, const Idx& G, Real seedR, Real refineDist, int rowsY, int rowsZ, std::vector<float>& out, long long& evals) {
    const int N = G.count();
    std::vector<float> D2(static_cast<size_t>(N), std::numeric_limits<float>::infinity());
    std::vector<int> C(static_cast<size_t>(N), -1);
    std::vector<unsigned char> done(static_cast<size_t>(N), 0);
    const Real bw = 0.5 * G.h;  // bucket width
    std::vector<std::vector<int>> buckets(64);
    auto push = [&](int s, float d2) {
        const size_t b = static_cast<size_t>(std::sqrt(d2) / bw);
        if (b >= buckets.size()) buckets.resize(b + 1);
        buckets[b].push_back(s);
    };
    // seed. Floats are compared with floats: the stored value is a float, and a double compare would let rounding
    // re-trigger updates.
    for (int k = 0; k < static_cast<int>(M.t.size()); ++k) {
        const Tri& T = M.t[static_cast<size_t>(k)];
        const Vec3 lo(std::min({T.a.x, T.b.x, T.c.x}), std::min({T.a.y, T.b.y, T.c.y}), std::min({T.a.z, T.b.z, T.c.z}));
        const Vec3 hi(std::max({T.a.x, T.b.x, T.c.x}), std::max({T.a.y, T.b.y, T.c.y}), std::max({T.a.z, T.b.z, T.c.z}));
        int i0, j0, k0, i1, j1, k1;
        G.range(lo - Vec3(seedR, seedR, seedR), hi + Vec3(seedR, seedR, seedR), i0, j0, k0, i1, j1, k1);
        for (int z = k0; z <= k1; ++z)
            for (int y = j0; y <= j1; ++y)
                for (int x = i0; x <= i1; ++x) {
                    const int s = G.index(x, y, z);
                    if (s < 0) continue;
                    const Vec3 p = G.pos(x, y, z);
                    const double pd = dot(p - T.a, T.n);
                    if (pd * pd >= D2[static_cast<size_t>(s)]) continue;  // plane distance is a lower bound
                    const float d2 = static_cast<float>(M.dist2(p, k));
                    ++evals;
                    if (d2 < D2[static_cast<size_t>(s)]) { D2[static_cast<size_t>(s)] = d2; C[static_cast<size_t>(s)] = k; }
                }
    }
    for (int s = 0; s < N; ++s)
        if (C[static_cast<size_t>(s)] >= 0) push(s, D2[static_cast<size_t>(s)]);
    const float refine2 = static_cast<float>(refineDist * refineDist);
    for (size_t b = 0; b < buckets.size(); ++b) {
        // samples of one distance shell in memory order (reverse, they are popped from the back): far fewer cache
        // misses than processing them in the order they were reached
        std::sort(buckets[b].begin(), buckets[b].end(), std::greater<int>());
        while (!buckets[b].empty()) {
            const int s = buckets[b].back();
            buckets[b].pop_back();
            if (done[static_cast<size_t>(s)]) continue;  // stale entry
            const size_t cb = static_cast<size_t>(std::sqrt(D2[static_cast<size_t>(s)]) / bw);
            if (cb > b) continue;  // stale entry (the sample got a better value and a lower bucket ... handled there)
            done[static_cast<size_t>(s)] = 1;
            int t = C[static_cast<size_t>(s)];
            int x, y, z;
            G.coords(s, x, y, z);
            if (D2[static_cast<size_t>(s)] < refine2) {
                const Vec3 p = G.pos(x, y, z);
                for (int steps = 0; steps < 64; ++steps) {
                    int best = t;
                    float bd = D2[static_cast<size_t>(s)];
                    for (int e = 0; e < 3; ++e) {
                        const int nt = M.adj[static_cast<size_t>(t)][e];
                        if (nt < 0) continue;
                        const float d2 = static_cast<float>(M.dist2(p, nt));
                        ++evals;
                        if (d2 < bd) { bd = d2; best = nt; }
                    }
                    if (best == t) break;
                    t = best;
                    D2[static_cast<size_t>(s)] = bd;
                    C[static_cast<size_t>(s)] = t;
                }
            }
            const int nb[6][3] = {{x - 1, y, z}, {x + 1, y, z}, {x, y - 1, z}, {x, y + 1, z}, {x, y, z - 1}, {x, y, z + 1}};
            for (const auto& q : nb) {
                const int ns = G.index(q[0], q[1], q[2]);
                if (ns < 0 || done[static_cast<size_t>(ns)] || C[static_cast<size_t>(ns)] == t) continue;
                const float d2 = static_cast<float>(M.dist2(G.pos(q[0], q[1], q[2]), t));
                ++evals;
                if (d2 < D2[static_cast<size_t>(ns)]) {
                    D2[static_cast<size_t>(ns)] = d2;
                    C[static_cast<size_t>(ns)] = t;
                    push(ns, d2);
                }
            }
        }
        std::vector<int>().swap(buckets[b]);
    }
    // sign from winding numbers along +x rows
    Rows rows;
    rows.build(Msign, G.o, G.h, rowsY, rowsZ);
    out.resize(static_cast<size_t>(N));
    for (int s = 0; s < N; ++s) {
        int x, y, z;
        G.coords(s, x, y, z);
        const float d = std::sqrt(D2[static_cast<size_t>(s)]);
        out[static_cast<size_t>(s)] = rows.winding(G.o.x + x * G.h, y, z) > 0 ? -d : d;
    }
}

struct DenseIdx {
    Vec3 o;
    Real h, inv;
    int nx, ny, nz;
    int count() const { return nx * ny * nz; }
    int index(int i, int j, int k) const {
        if (i < 0 || j < 0 || k < 0 || i >= nx || j >= ny || k >= nz) return -1;
        return (k * ny + j) * nx + i;
    }
    void coords(int s, int& i, int& j, int& k) const { i = s % nx; j = (s / nx) % ny; k = s / (nx * ny); }
    Vec3 pos(int i, int j, int k) const { return o + Vec3(i * h, j * h, k * h); }
    void range(const Vec3& lo, const Vec3& hi, int& i0, int& j0, int& k0, int& i1, int& j1, int& k1) const {
        i0 = std::max(0, static_cast<int>(std::ceil((lo.x - o.x) * inv))); i1 = std::min(nx - 1, static_cast<int>(std::floor((hi.x - o.x) * inv)));
        j0 = std::max(0, static_cast<int>(std::ceil((lo.y - o.y) * inv))); j1 = std::min(ny - 1, static_cast<int>(std::floor((hi.y - o.y) * inv)));
        k0 = std::max(0, static_cast<int>(std::ceil((lo.z - o.z) * inv))); k1 = std::min(nz - 1, static_cast<int>(std::floor((hi.z - o.z) * inv)));
    }
};

struct BrickIdx {
    Vec3 o;
    Real h, inv;
    int bnx, bny, bnz, nb;
    const std::vector<int>* table;
    std::vector<int> bi, bj, bk;  // brick coordinates per allocated brick
    int count() const { return nb * BV; }
    int index(int i, int j, int k) const {
        if (i < 0 || j < 0 || k < 0 || i >= BS * bnx || j >= BS * bny || k >= BS * bnz) return -1;
        const int b = (*table)[static_cast<size_t>(((k >> BSH) * bny + (j >> BSH)) * bnx + (i >> BSH))];
        return b < 0 ? -1 : (b << BVSH) + ((((k & (BS - 1)) << BSH) + (j & (BS - 1))) << BSH) + (i & (BS - 1));
    }
    void coords(int s, int& i, int& j, int& k) const {
        const int b = s >> BVSH, l = s & (BV - 1);
        i = (bi[static_cast<size_t>(b)] << BSH) + (l & (BS - 1));
        j = (bj[static_cast<size_t>(b)] << BSH) + ((l >> BSH) & (BS - 1));
        k = (bk[static_cast<size_t>(b)] << BSH) + (l >> (2 * BSH));
    }
    Vec3 pos(int i, int j, int k) const { return o + Vec3(i * h, j * h, k * h); }
    void range(const Vec3& lo, const Vec3& hi, int& i0, int& j0, int& k0, int& i1, int& j1, int& k1) const {
        i0 = std::max(0, static_cast<int>(std::ceil((lo.x - o.x) * inv))); i1 = std::min(BS * bnx - 1, static_cast<int>(std::floor((hi.x - o.x) * inv)));
        j0 = std::max(0, static_cast<int>(std::ceil((lo.y - o.y) * inv))); j1 = std::min(BS * bny - 1, static_cast<int>(std::floor((hi.y - o.y) * inv)));
        k0 = std::max(0, static_cast<int>(std::ceil((lo.z - o.z) * inv))); k1 = std::min(BS * bnz - 1, static_cast<int>(std::floor((hi.z - o.z) * inv)));
    }
};

// Uniform quadratic B-spline weights (and derivatives) of the three samples around t in [-0.5, 0.5].
inline void bspline2(Real t, Real* w, Real* d) {
    const Real a = 0.5 - t, b = 0.5 + t;
    w[0] = 0.5 * a * a; w[1] = 0.75 - t * t; w[2] = 0.5 * b * b;
    d[0] = -a; d[1] = -2 * t; d[2] = b;
}

// Winding number at arbitrary points (signed ray crossings along +x), with the triangles bucketed by their (y,z)
// extent. Used once per bake to find buried skin.
struct PointWinding {
    const Mesh& M;
    Real c, inv;
    Vec3 lo;
    int ny = 0, nz = 0;
    std::vector<int> start, items;
    PointWinding(const Mesh& m, const Vec3& lo_, const Vec3& hi, Real cell) : M(m), c(cell), inv(1 / cell), lo(lo_) {
        ny = static_cast<int>((hi.y - lo.y) * inv) + 1;
        nz = static_cast<int>((hi.z - lo.z) * inv) + 1;
        start.assign(static_cast<size_t>(ny) * nz + 1, 0);
        auto cells = [&](const Tri& T, int& j0, int& j1, int& k0, int& k1) {
            j0 = std::max(0, static_cast<int>((std::min({T.a.y, T.b.y, T.c.y}) - lo.y) * inv));
            j1 = std::min(ny - 1, static_cast<int>((std::max({T.a.y, T.b.y, T.c.y}) - lo.y) * inv));
            k0 = std::max(0, static_cast<int>((std::min({T.a.z, T.b.z, T.c.z}) - lo.z) * inv));
            k1 = std::min(nz - 1, static_cast<int>((std::max({T.a.z, T.b.z, T.c.z}) - lo.z) * inv));
        };
        for (const Tri& T : M.t) {
            int j0, j1, k0, k1;
            cells(T, j0, j1, k0, k1);
            for (int k = k0; k <= k1; ++k)
                for (int j = j0; j <= j1; ++j) ++start[static_cast<size_t>(j + ny * k) + 1];
        }
        for (size_t r = 0; r + 1 < start.size(); ++r) start[r + 1] += start[r];
        items.assign(static_cast<size_t>(start.back()), 0);
        std::vector<int> fill(start.begin(), start.end() - 1);
        for (int ti = 0; ti < static_cast<int>(M.t.size()); ++ti) {
            int j0, j1, k0, k1;
            cells(M.t[static_cast<size_t>(ti)], j0, j1, k0, k1);
            for (int k = k0; k <= k1; ++k)
                for (int j = j0; j <= j1; ++j) items[static_cast<size_t>(fill[static_cast<size_t>(j + ny * k)]++)] = ti;
        }
    }
    int winding(const Vec3& p) const {
        const double y = p.y + 0.000137 * c, z = p.z + 0.000263 * c;  // off exact edges
        const int j = static_cast<int>((y - lo.y) * inv), k = static_cast<int>((z - lo.z) * inv);
        if (j < 0 || k < 0 || j >= ny || k >= nz) return 0;
        const size_t r = static_cast<size_t>(j + ny * k);
        int w = 0;
        for (int q = start[r]; q < start[r + 1]; ++q) {
            const Tri& T = M.t[static_cast<size_t>(items[static_cast<size_t>(q)])];
            if (T.n.x == 0) continue;
            const double ay = T.a.y - y, az = T.a.z - z, by = T.b.y - y, bz = T.b.z - z, cy = T.c.y - y, cz = T.c.z - z;
            const double wa = by * cz - bz * cy, wb = cy * az - cz * ay, wc = ay * bz - az * by;
            if (!((wa >= 0 && wb >= 0 && wc >= 0) || (wa <= 0 && wb <= 0 && wc <= 0))) continue;
            const double sum = wa + wb + wc;
            if (std::fabs(sum) < 1e-300) continue;
            if ((wa * T.a.x + wb * T.b.x + wc * T.c.x) / sum > p.x) w += T.n.x > 0 ? 1 : -1;
        }
        return w;
    }
};

// The skin that is buried inside another part of the body (an arm overlapping the torso, a lowered arm folding into
// the shoulder): triangles whose outside is inside the body at two or more of their centre and three inner points. They stay in
// the inside/outside test (winding numbers) but are left out of the distances; otherwise a point inside the arm next
// to the buried torso skin reads as "on the surface" and cloth comes to rest there, inside the arm.
std::vector<char> buriedTriangles(const Mesh& M, const Vec3& lo, const Vec3& hi, Real eps) {
    PointWinding W(M, lo, hi, 0.004);
    std::vector<char> out(M.t.size(), 0);
    for (size_t ti = 0; ti < M.t.size(); ++ti) {
        const Tri& T = M.t[ti];
        const Vec3 ctr = (T.a + T.b + T.c) / 3.0;
        const Vec3 pts[4] = {ctr, (ctr + T.a) * 0.5, (ctr + T.b) * 0.5, (ctr + T.c) * 0.5};
        int in = 0;
        for (const Vec3& q : pts) in += W.winding(q + T.n * eps) > 0;
        out[ti] = in >= 2 ? 1 : 0;   // mostly buried (a triangle crossing into the other part is half in, half out)
    }
    return out;
}

}  // namespace

bool SdfGrid::bake(const std::vector<Vec3>& verts, const std::vector<std::array<int, 3>>& tris, const SdfGridParams& p) {
    const auto t0 = Clock::now();
    p_ = p;
    stats_ = SdfGridStats();
    brickOf_.clear(); fine_.clear(); coarse_.clear(); brickXYZ_.clear();
    if (verts.empty() || tris.empty()) return false;
    const Mesh M = buildMesh(verts, tris);
    if (M.t.empty()) return false;
    stats_.triangles = static_cast<int>(M.t.size());
    Vec3 lo = verts[0], hi = verts[0];
    for (const Vec3& v : verts) {
        lo = Vec3(std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z));
        hi = Vec3(std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z));
    }
    // distances to the visible skin only (buriedTriangles); the sign keeps every triangle
    Mesh Md;
    {
        const std::vector<char> bur = p.dropBuried ? buriedTriangles(M, lo, hi, p.buriedOffset) : std::vector<char>(M.t.size(), 0);
        std::vector<std::array<int, 3>> keep;
        for (size_t i = 0, ti = 0; i < tris.size(); ++i) {
            const auto& tr = tris[i];
            const Vec3 cr = cross(verts[static_cast<size_t>(tr[1])] - verts[static_cast<size_t>(tr[0])], verts[static_cast<size_t>(tr[2])] - verts[static_cast<size_t>(tr[0])]);
            if (length(cr) < 1e-16) continue;  // as buildMesh: degenerate triangles are not in M
            if (!bur[ti++]) keep.push_back(tr);
            else ++stats_.buried;
        }
        Md = keep.size() == M.t.size() ? M : buildMesh(verts, keep);
        if (Md.t.empty()) Md = M;
    }
    stats_.msSetup = msSince(t0);

    // ---- fine level: bricks that hold a sample within band + stencil reach of a triangle (bounding-box test)
    const auto t1 = Clock::now();
    const Real h = p.h;
    const Real reach = p.band + 1.5 * std::sqrt(3.0) * h;
    const Real pad = reach + 2 * h;
    fh_ = h; finv_ = 1 / h;
    fo_ = lo - Vec3(pad, pad, pad);
    auto bricks = [&](Real ext) { return (static_cast<int>(std::ceil(ext / h)) + 1 + BS - 1) / BS; };
    bnx_ = bricks(hi.x - lo.x + 2 * pad); bny_ = bricks(hi.y - lo.y + 2 * pad); bnz_ = bricks(hi.z - lo.z + 2 * pad);
    brickOf_.assign(static_cast<size_t>(bnx_) * bny_ * bnz_, -1);
    BrickIdx F;
    F.o = fo_; F.h = h; F.inv = finv_; F.bnx = bnx_; F.bny = bny_; F.bnz = bnz_; F.table = &brickOf_; F.nb = 0;
    for (const Tri& T : M.t) {
        const Vec3 tl(std::min({T.a.x, T.b.x, T.c.x}) - reach, std::min({T.a.y, T.b.y, T.c.y}) - reach, std::min({T.a.z, T.b.z, T.c.z}) - reach);
        const Vec3 th(std::max({T.a.x, T.b.x, T.c.x}) + reach, std::max({T.a.y, T.b.y, T.c.y}) + reach, std::max({T.a.z, T.b.z, T.c.z}) + reach);
        // sample range strictly inside the expanded box, then the bricks holding those samples
        const int i0 = std::max(0, static_cast<int>(std::ceil((tl.x - fo_.x) * finv_)) >> BSH), i1 = std::min(bnx_ - 1, static_cast<int>(std::floor((th.x - fo_.x) * finv_)) >> BSH);
        const int j0 = std::max(0, static_cast<int>(std::ceil((tl.y - fo_.y) * finv_)) >> BSH), j1 = std::min(bny_ - 1, static_cast<int>(std::floor((th.y - fo_.y) * finv_)) >> BSH);
        const int k0 = std::max(0, static_cast<int>(std::ceil((tl.z - fo_.z) * finv_)) >> BSH), k1 = std::min(bnz_ - 1, static_cast<int>(std::floor((th.z - fo_.z) * finv_)) >> BSH);
        for (int k = k0; k <= k1; ++k)
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i) {
                    int& b = brickOf_[static_cast<size_t>((k * bny_ + j) * bnx_ + i)];
                    if (b < 0) {
                        b = F.nb++;
                        F.bi.push_back(i); F.bj.push_back(j); F.bk.push_back(k);
                    }
                }
    }
    {   // number the bricks in table order, so that spatial neighbours are close in memory
        int id = 0;
        for (size_t q = 0; q < brickOf_.size(); ++q)
            if (brickOf_[q] >= 0) {
                brickOf_[q] = id++;
                const int qi = static_cast<int>(q);
                F.bi[static_cast<size_t>(brickOf_[q])] = qi % bnx_;
                F.bj[static_cast<size_t>(brickOf_[q])] = (qi / bnx_) % bny_;
                F.bk[static_cast<size_t>(brickOf_[q])] = qi / (bnx_ * bny_);
            }
    }
    bakeLevel(Md, M, F, p.seed * h, p.refine ? p.band + 1.5 * h : 0.0, BS * bny_, BS * bnz_, fine_, stats_.distanceEvals);
    brickXYZ_.resize(static_cast<size_t>(3 * F.nb));
    for (int b = 0; b < F.nb; ++b) { brickXYZ_[3 * b] = F.bi[b]; brickXYZ_[3 * b + 1] = F.bj[b]; brickXYZ_[3 * b + 2] = F.bk[b]; }
    stats_.bricks = F.nb;
    stats_.fineSamples = F.nb * BV;
    stats_.msFine = msSince(t1);

    // ---- coarse level: dense
    const auto t2 = Clock::now();
    DenseIdx Cg;
    ch_ = h * p.coarseRatio; cinv_ = 1 / ch_;
    co_ = lo - Vec3(p.margin, p.margin, p.margin);
    cnx_ = static_cast<int>(std::ceil((hi.x - lo.x + 2 * p.margin) * cinv_)) + 1;
    cny_ = static_cast<int>(std::ceil((hi.y - lo.y + 2 * p.margin) * cinv_)) + 1;
    cnz_ = static_cast<int>(std::ceil((hi.z - lo.z + 2 * p.margin) * cinv_)) + 1;
    Cg.o = co_; Cg.h = ch_; Cg.inv = cinv_; Cg.nx = cnx_; Cg.ny = cny_; Cg.nz = cnz_;
    bakeLevel(Md, M, Cg, p.seed * ch_, 0.0, cny_, cnz_, coarse_, stats_.distanceEvals);
    stats_.coarseSamples = Cg.count();
    stats_.msCoarse = msSince(t2);
    stats_.bytes = 4.0 * (fine_.size() + coarse_.size() + brickOf_.size() + brickXYZ_.size());
    stats_.msTotal = msSince(t0);
    return true;
}

bool SdfGrid::evalFine(const Vec3& p, Real& v, Vec3* grad) const {
    const Real gx = (p.x - fo_.x) * finv_, gy = (p.y - fo_.y) * finv_, gz = (p.z - fo_.z) * finv_;
    const int ix = static_cast<int>(std::floor(gx + 0.5)), iy = static_cast<int>(std::floor(gy + 0.5)), iz = static_cast<int>(std::floor(gz + 0.5));
    if (ix < 1 || iy < 1 || iz < 1 || ix + 1 >= BS * bnx_ || iy + 1 >= BS * bny_ || iz + 1 >= BS * bnz_) return false;
    Real wx[3], wy[3], wz[3], dx[3], dy[3], dz[3];
    bspline2(gx - ix, wx, dx); bspline2(gy - iy, wy, dy); bspline2(gz - iz, wz, dz);
    int bx[3], lx[3], by[3], ly[3], bz[3], lz[3];
    for (int t = 0; t < 3; ++t) {
        bx[t] = (ix - 1 + t) >> BSH; lx[t] = (ix - 1 + t) & (BS - 1);
        by[t] = (iy - 1 + t) >> BSH; ly[t] = (iy - 1 + t) & (BS - 1);
        bz[t] = (iz - 1 + t) >> BSH; lz[t] = (iz - 1 + t) & (BS - 1);
    }
    Real val = 0, g0 = 0, g1 = 0, g2 = 0;
    for (int c = 0; c < 3; ++c)
        for (int b = 0; b < 3; ++b) {
            const int row = (bz[c] * bny_ + by[b]) * bnx_, lrow = ((lz[c] << BSH) + ly[b]) << BSH;
            const Real wyz = wy[b] * wz[c], dyz = dy[b] * wz[c], wdz = wy[b] * dz[c];
            for (int a = 0; a < 3; ++a) {
                const int br = brickOf_[static_cast<size_t>(row + bx[a])];
                if (br < 0) return false;
                const Real s = fine_[static_cast<size_t>((br << BVSH) + lrow + lx[a])];
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

Real SdfGrid::evalCoarse(const Vec3& p, Vec3* grad) const {
    Real gx = (p.x - co_.x) * cinv_, gy = (p.y - co_.y) * cinv_, gz = (p.z - co_.z) * cinv_;
    // outside the stencil-valid box: evaluate at the nearest valid point and add the remaining distance
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
            const float* row = &coarse_[static_cast<size_t>(((iz - 1 + c) * cny_ + (iy - 1 + b)) * cnx_ + (ix - 1))];
            const Real wyz = wy[b] * wz[c], dyz = dy[b] * wz[c], wdz = wy[b] * dz[c];
            for (int a = 0; a < 3; ++a) {
                const Real s = row[a];
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

void SdfGrid::fineSamples(std::vector<Vec3>& pos, std::vector<float>& val) const {
    pos.clear();
    val = fine_;
    for (size_t s = 0; s < fine_.size(); ++s) {
        const size_t b = s >> BVSH, l = s & (BV - 1);
        pos.push_back(fo_ + Vec3(((brickXYZ_[3 * b] << BSH) + (l & (BS - 1))) * fh_, ((brickXYZ_[3 * b + 1] << BSH) + ((l >> BSH) & (BS - 1))) * fh_,
                                 ((brickXYZ_[3 * b + 2] << BSH) + (l >> (2 * BSH))) * fh_));
    }
}

Real SdfGrid::eval(const Vec3& p, Vec3* grad) const {
    if (coarse_.empty()) {
        if (grad) *grad = Vec3(0, 1, 0);
        return 1e30;
    }
    Real v;
    if (evalFine(p, v, grad)) {
        ++fineQueries;
        return v;
    }
    ++coarseQueries;
    return evalCoarse(p, grad);
}

}  // namespace fl
