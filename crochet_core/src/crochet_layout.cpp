// Initial 3D positions for a CrochetModel (docs/CROCHET_PLAN.md section 6.8).
// The decisions are listed in crochet/layout.hpp.
#include "failoring/crochet/layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <unordered_map>

namespace fl {

namespace {

constexpr Real kLsWeight = 0.1;      // plan 6.8: weight of the "follow the targets" term in the row smoothing
constexpr Real kJumpWidths = 3.0;    // rim: base positions this many stitch widths apart break a run
constexpr Real kNaN = std::numeric_limits<Real>::quiet_NaN();

// --- 1D smoothing ---------------------------------------------------------

// Minimise sum_{i>=1} (x_i - x_{i-1} - dir w_i)^2 + mu sum_i (x_i - t_i)^2 over an open row (tridiagonal solve).
// has[i] == 0 drops the target term of stitch i (a chain without a target).
std::vector<Real> spaceOpen(const std::vector<Real>& t, const std::vector<char>& has, const std::vector<Real>& w, Real dir) {
    const int n = static_cast<int>(t.size());
    std::vector<Real> x(n, 0);
    if (n == 0) return x;
    bool any = false;
    for (int i = 0; i < n; ++i) any = any || has[i];
    if (!any) {
        for (int i = 1; i < n; ++i) x[i] = x[i - 1] + dir * w[i];
        return x;
    }
    if (n == 1) return {t[0]};
    std::vector<Real> a(n), b(n), c(n), rhs(n);
    for (int i = 0; i < n; ++i) {
        const Real mu = has[i] ? kLsWeight : 0;
        b[i] = mu + (i >= 1 ? 1 : 0) + (i < n - 1 ? 1 : 0);
        a[i] = i >= 1 ? -1 : 0;
        c[i] = i < n - 1 ? -1 : 0;
        rhs[i] = (has[i] ? mu * t[i] : 0) + (i >= 1 ? dir * w[i] : 0) - (i < n - 1 ? dir * w[i + 1] : 0);
    }
    for (int i = 1; i < n; ++i) {  // Thomas
        const Real m = a[i] / b[i - 1];
        b[i] -= m * c[i - 1];
        rhs[i] -= m * rhs[i - 1];
    }
    x[n - 1] = rhs[n - 1] / b[n - 1];
    for (int i = n - 2; i >= 0; --i) x[i] = (rhs[i] - c[i] * x[i + 1]) / b[i];
    return x;
}

// The same on a closed ring of the given period (Gauss-Seidel; the rings have < 1000 stitches). t must be unwrapped
// (increasing around the ring).
std::vector<Real> spaceRing(const std::vector<Real>& t, const std::vector<Real>& w, Real period) {
    const int n = static_cast<int>(t.size());
    std::vector<Real> x = t;
    if (n < 2) return x;
    for (int it = 0; it < 600; ++it) {
        for (int i = 0; i < n; ++i) {
            const Real xp = i > 0 ? x[i - 1] : x[n - 1] - period;
            const Real xn = i < n - 1 ? x[i + 1] : x[0] + period;
            const Real wn = i < n - 1 ? w[i + 1] : w[0];
            x[i] = (kLsWeight * t[i] + xp + w[i] + xn - wn) / (kLsWeight + 2);
        }
    }
    return x;
}

// --- the offset curve of the centre path of a revolve piece -----------------

// A stadium (a segment of length L offset by r), clockwise seen from +y, in the (x, z) plane, u = 0 at (L, +r).
struct Stadium {
    Real L = 0, r = 1;
    Real perimeter() const { return 2 * L + 2 * kPi * r; }
    Vec2 point(Real u) const {
        const Real P = perimeter();
        u = std::fmod(u, P);
        if (u < 0) u += P;
        if (u < L) return {L - u, r};
        u -= L;
        if (u < kPi * r) {
            const Real th = u / r;
            return {-r * std::sin(th), r * std::cos(th)};
        }
        u -= kPi * r;
        if (u < L) return {u, -r};
        u -= L;
        const Real th = u / r;
        return {L + r * std::sin(th), -r * std::cos(th)};
    }
    // radius-independent coordinate of u: segment 0..3 and the length (sides) or angle (caps)
    void toQ(Real u, int& seg, Real& val) const {
        const Real P = perimeter();
        u = std::fmod(u, P);
        if (u < 0) u += P;
        if (u < L) { seg = 0; val = u; return; }
        u -= L;
        if (u < kPi * r) { seg = 1; val = u / r; return; }
        u -= kPi * r;
        if (u < L) { seg = 2; val = u; return; }
        u -= L;
        seg = 3;
        val = u / r;
    }
    Real fromQ(int seg, Real val) const {
        switch (seg) {
            case 0: return val;
            case 1: return L + r * val;
            case 2: return L + kPi * r + val;
            default: return 2 * L + kPi * r + r * val;
        }
    }
};

struct Q {
    int seg = 0;
    Real val = 0;
};

// --- the layouter -----------------------------------------------------------

struct Region {
    LayoutRegion::Type type = LayoutRegion::Foundation;
    std::vector<int> rows;  // non-empty rows, in order
};

class Layouter {
public:
    Layouter(CrochetModel& cm, const StitchGraph& g, const LayoutOptions& opt) : cm_(cm), g_(g), opt_(opt), t_(cm.topo), d_(cm.dims) {
        nv_ = static_cast<int>(t_.vertexStitch.size());
        pos_.assign(nv_, Vec3(0, 0, 0));
        placed_.assign(nv_, 0);
        q_.assign(nv_, Q());
        rowKept_.resize(g.rows.size());
        for (const Stitch& s : g.st)
            if (t_.stitchVertex[s.id] >= 0) rowKept_[s.row].push_back(s.id);
        closed_.assign(g.rows.size(), 0);
        for (const MeshPolygon& p : t_.polygons)
            if (p.kind == MeshPolygon::Closing) closed_[g.st[p.source].row] = 1;
        triOf_.resize(nv_);
        for (size_t i = 0; i < t_.tris.size(); ++i)
            for (int k = 0; k < 3; ++k) triOf_[t_.tris[i][k]].push_back(static_cast<int>(i));
    }

    LayoutReport run() {
        buildRegions();
        for (const Region& reg : regions_) {
            LayoutRegion lr;
            lr.type = reg.type;
            lr.firstRow = reg.rows.front();
            lr.lastRow = reg.rows.back();
            switch (reg.type) {
                case LayoutRegion::Foundation: break;  // placed by the region that is worked into it
                case LayoutRegion::Flat: lr.mirrored = layoutFlat(reg); break;
                case LayoutRegion::Revolve: lr.mirrored = layoutRevolve(reg); break;
                case LayoutRegion::Rim:
                    for (const int r : reg.rows) layoutRimRow(r);
                    break;
                case LayoutRegion::Strip: lr.mirrored = layoutStrip(reg); break;
            }
            report_.regions.push_back(lr);
        }
        finishUnplaced();
        cm_.mesh.x0 = pos_;
        cm_.mesh.layoutUV.resize(nv_);
        for (int v = 0; v < nv_; ++v) cm_.mesh.layoutUV[v] = Vec2(pos_[v].x, pos_[v].y);
        return report_;
    }

private:
    CrochetModel& cm_;
    const StitchGraph& g_;
    const LayoutOptions& opt_;
    const CrochetMesh& t_;
    const std::vector<StitchDims>& d_;
    int nv_ = 0;
    std::vector<Vec3> pos_;
    std::vector<char> placed_;
    std::vector<Q> q_;  // ring coordinate of revolve vertices
    std::vector<std::vector<int>> rowKept_;  // per row, the kept stitch ids in made order
    std::vector<char> closed_;
    std::vector<std::vector<int>> triOf_;  // vertex -> triangles
    std::vector<Region> regions_;
    LayoutReport report_;
    std::map<int, int> regionOfRow_;

    int vtx(int stitch) const { return t_.stitchVertex[stitch] >= 0 ? t_.stitchVertex[stitch] : t_.dropTo[stitch]; }
    void place(int v, const Vec3& p) { pos_[v] = p; placed_[v] = 1; }

    // ---- regions ----

    std::set<int> parentRows(int r) const {
        std::set<int> out;
        for (const int s : rowKept_[r])
            for (const int tg : g_.st[s].targets) out.insert(g_.st[tg].row);
        return out;
    }
    bool foundationRow(int r) const {
        if (rowKept_[r].empty()) return false;
        for (const int s : rowKept_[r])
            if (!g_.st[s].targets.empty()) return false;
        return true;
    }

    void buildRegions() {
        const int R = static_cast<int>(g_.rows.size());
        std::vector<char> isFoundation(R, 0);
        for (int r = 0; r < R; ++r) isFoundation[r] = foundationRow(r);
        int lastRow = -1;
        for (int r = 0; r < R; ++r) {
            if (rowKept_[r].empty()) continue;
            if (isFoundation[r]) {
                regions_.push_back({LayoutRegion::Foundation, {r}});
                lastRow = r;
                continue;
            }
            const std::set<int> pr = parentRows(r);
            bool onlyFoundation = true;
            for (const int p : pr) onlyFoundation = onlyFoundation && isFoundation[p];
            const bool onlyLast = pr.size() == 1 && *pr.begin() == lastRow;
            const bool isRound = g_.rows[r].kind == "round";
            Region* cur = regions_.empty() ? nullptr : &regions_.back();
            if (closed_[r]) {
                if (cur != nullptr && cur->type == LayoutRegion::Revolve && onlyLast) cur->rows.push_back(r);
                else if (onlyFoundation) regions_.push_back({LayoutRegion::Revolve, {r}});
                else regions_.push_back({LayoutRegion::Rim, {r}});
            } else if (!isRound) {
                if (cur != nullptr && (cur->type == LayoutRegion::Flat || cur->type == LayoutRegion::Strip) && onlyLast) cur->rows.push_back(r);
                else if (onlyFoundation) regions_.push_back({LayoutRegion::Flat, {r}});
                else regions_.push_back({LayoutRegion::Strip, {r}});
            } else {
                regions_.push_back({LayoutRegion::Rim, {r}});
            }
            lastRow = r;
        }
        for (size_t i = 0; i < regions_.size(); ++i)
            for (const int r : regions_[i].rows) regionOfRow_[r] = static_cast<int>(i);
    }

    // ---- geometry helpers ----

    Vec3 triNormal(int tri) const {
        const auto& v = t_.tris[tri];
        return normalized(cross(pos_[v[1]] - pos_[v[0]], pos_[v[2]] - pos_[v[0]]));
    }
    bool triPlaced(int tri) const {
        const auto& v = t_.tris[tri];
        return placed_[v[0]] && placed_[v[1]] && placed_[v[2]];
    }

    // Winding normal of the first placed triangle with a vertex in `vs` (zero when none).
    Vec3 firstNormal(const std::vector<int>& vs) const {
        int best = -1;
        for (const int v : vs)
            for (const int tri : triOf_[v])
                if (triPlaced(tri) && (best < 0 || tri < best)) best = tri;
        return best < 0 ? Vec3(0, 0, 0) : triNormal(best);
    }

    // Outward in-plane direction at the boundary of the placed faces, per vertex (zero inside).
    std::vector<Vec3> boundaryOutward() const {
        std::vector<Vec3> out(nv_, Vec3(0, 0, 0));
        std::unordered_map<std::uint64_t, int> count;
        auto key = [](int a, int b) { return (static_cast<std::uint64_t>(std::min(a, b)) << 32) | static_cast<std::uint32_t>(std::max(a, b)); };
        for (size_t i = 0; i < t_.tris.size(); ++i) {
            if (!triPlaced(static_cast<int>(i))) continue;
            for (int k = 0; k < 3; ++k) count[key(t_.tris[i][k], t_.tris[i][(k + 1) % 3])]++;
        }
        for (size_t i = 0; i < t_.tris.size(); ++i) {
            if (!triPlaced(static_cast<int>(i))) continue;
            const Vec3 n = triNormal(static_cast<int>(i));
            for (int k = 0; k < 3; ++k) {
                const int a = t_.tris[i][k], b = t_.tris[i][(k + 1) % 3];
                if (count[key(a, b)] != 1) continue;
                const Vec3 o = normalized(cross(pos_[b] - pos_[a], n));  // right of a->b = outward for a counter-clockwise face
                out[a] += o;
                out[b] += o;
            }
        }
        for (Vec3& o : out) o = normalized(o);
        return out;
    }

    Real meanWidth(const std::vector<int>& stitches) const {
        Real s = 0;
        for (const int id : stitches) s += d_[id].w;
        return stitches.empty() ? 0 : s / stitches.size();
    }
    Real meanHeight(const std::vector<int>& stitches) const {
        Real s = 0;
        for (const int id : stitches) s += d_[id].h;
        return stitches.empty() ? 0 : s / stitches.size();
    }

    // ---- Flat ----

    bool layoutFlat(const Region& reg) {
        for (int mirror = 0; mirror < 2; ++mirror) {
            flatPass(reg, mirror != 0);
            std::vector<int> vs;
            for (const int r : reg.rows)
                for (const int s : rowKept_[r]) vs.push_back(t_.stitchVertex[s]);
            const Vec3 n = firstNormal(vs);
            if (n.z > 0 || mirror == 1) {
                if (n.z <= 0) report_.warnings.push_back("flat region at row " + std::to_string(reg.rows.front()) + " has no right-side normal toward +z");
                foldIfPaired(reg);
                return mirror != 0;
            }
        }
        return false;
    }

    void flatPass(const Region& reg, bool mirror) {
        // canonical (x, y): x along the course, y up the wale; world = (sx x, -y, 0): the work hangs from its foundation
        const Real sx = mirror ? 1 : -1;
        const int first = reg.rows.front();
        // the foundation nodes: distinct targets of the first row, ordered by made order
        std::vector<int> nodes;
        {
            std::set<int> seen;
            for (const int s : rowKept_[first])
                for (const int tg : g_.st[s].targets)
                    if (seen.insert(tg).second) nodes.push_back(tg);
            std::sort(nodes.begin(), nodes.end());
        }
        const Real spacing = meanWidth(rowKept_[first]);
        // row 1 must progress along +x: look at the first two different targets
        Real sgn = 1;
        {
            const auto& S = rowKept_[first];
            int a = g_.st[S.front()].targets.front();
            for (const int s : S) {
                const int b = g_.st[s].targets.front();
                if (b != a) {
                    sgn = b > a ? 1 : -1;
                    break;
                }
            }
        }
        std::unordered_map<int, Real> cx, cy;  // by vertex
        for (size_t i = 0; i < nodes.size(); ++i) {
            const int v = vtx(nodes[i]);
            cx[v] = sgn * static_cast<Real>(i) * spacing;
            cy[v] = 0;
            place(v, Vec3(sx * cx[v], 0, 0));
        }
        Real prevDir = 1;
        for (size_t ri = 0; ri < reg.rows.size(); ++ri) {
            const auto& S = rowKept_[reg.rows[ri]];
            const int n = static_cast<int>(S.size());
            std::vector<Real> xs(n, 0), ys(n, 0), w(n, 0);
            std::vector<char> has(n, 0);
            for (int i = 0; i < n; ++i) {
                w[i] = d_[S[i]].w;
                const auto& T = g_.st[S[i]].targets;
                if (T.empty()) continue;
                Real sxm = 0, sym = 0;
                for (const int tg : T) {
                    sxm += cx[vtx(tg)];
                    sym += cy[vtx(tg)];
                }
                xs[i] = sxm / T.size();
                ys[i] = sym / T.size() + d_[S[i]].h;
                has[i] = 1;
            }
            // direction of the work along the row
            Real dir = 0;
            {
                int a = -1, b = -1;
                for (int i = 0; i < n; ++i)
                    if (has[i]) {
                        if (a < 0) a = i;
                        b = i;
                    }
                if (a >= 0 && b > a) dir = xs[b] - xs[a] > 0 ? 1 : (xs[b] - xs[a] < 0 ? -1 : 0);
            }
            if (dir == 0) dir = g_.rows[reg.rows[ri == 0 ? 0 : ri - 1]].turned ? -prevDir : prevDir;
            prevDir = dir;
            const std::vector<Real> x = spaceOpen(xs, has, w, dir);
            for (int i = 0; i < n; ++i) {
                Real y = ys[i];
                if (!has[i]) {  // a chain in the row: between its neighbours
                    Real ya = kNaN, yb = kNaN;
                    for (int j = i - 1; j >= 0 && std::isnan(ya); --j) if (has[j]) ya = ys[j];
                    for (int j = i + 1; j < n && std::isnan(yb); ++j) if (has[j]) yb = ys[j];
                    y = std::isnan(ya) ? yb : (std::isnan(yb) ? ya : 0.5 * (ya + yb));
                    if (std::isnan(y)) y = 0;
                }
                const int v = t_.stitchVertex[S[i]];
                cx[v] = x[i];
                cy[v] = y;
                place(v, Vec3(sx * x[i], -y, 0));
            }
        }
    }

    // Fold a Flat region whose rows are paired by both_layers point seams with a constant row sum.
    void foldIfPaired(const Region& reg) {
        const std::set<int> rows(reg.rows.begin(), reg.rows.end());
        std::set<int> sums;
        for (const PointSeam& ps : pointSeams(g_)) {
            if (ps.kind != "both_layers") continue;
            const int f = g_.st[g_.st[ps.stitch].targets.front()].row, h = g_.st[ps.partner].row;
            if (rows.count(f) && rows.count(h)) sums.insert(f + h);
        }
        if (sums.empty()) return;
        if (sums.size() != 1 || (*sums.begin() % 2) == 0) {
            report_.warnings.push_back("both_layers seams of rows " + std::to_string(reg.rows.front()) + ".." + std::to_string(reg.rows.back()) +
                                       " do not pair the rows with one odd row sum: no fold");
            return;
        }
        const int k = (*sums.begin() - 1) / 2;  // fold between rows k and k + 1
        if (!rows.count(k) || !rows.count(k + 1)) return;
        Real yk = 0, yk1 = 0, thickness = 0;
        for (const int s : rowKept_[k]) yk += pos_[t_.stitchVertex[s]].y;
        for (const int s : rowKept_[k + 1]) yk1 += pos_[t_.stitchVertex[s]].y;
        yk /= rowKept_[k].size();
        yk1 /= rowKept_[k + 1].size();
        for (const int s : rowKept_[k]) thickness = std::max(thickness, d_[s].thickness);
        const Real yFold = 0.5 * (yk + yk1), gap = thickness + opt_.foldGap;
        for (const int r : reg.rows)
            if (r > k)
                for (const int s : rowKept_[r]) {
                    Vec3 p = pos_[t_.stitchVertex[s]];
                    p.y = 2 * yFold - p.y;
                    p.z = -gap;
                    pos_[t_.stitchVertex[s]] = p;
                }
        // chains of the foundation are in front; the layer behind keeps its x, so the mouth (rows 1 and 50) is on top
        report_.foldAfterRow = k;
    }

    // ---- Revolve ----

    bool layoutRevolve(const Region& reg) {
        for (int mirror = 0; mirror < 2; ++mirror) {
            revolvePass(reg, mirror != 0);
            std::vector<int> vs;
            for (const int s : rowKept_[reg.rows.front()]) vs.push_back(t_.stitchVertex[s]);
            const Vec3 n = firstNormal(vs);
            if (n.y > 0 || mirror == 1) {
                if (n.y <= 0) report_.warnings.push_back("revolve region at row " + std::to_string(reg.rows.front()) + " has no right-side normal toward +y");
                return mirror != 0;
            }
        }
        return false;
    }

    void revolvePass(const Region& reg, bool mirror) {
        const Real zs = mirror ? -1 : 1;
        const auto& S1 = rowKept_[reg.rows.front()];
        // centre nodes: the distinct targets of the first ring, in made order
        std::vector<int> nodes;
        {
            std::set<int> seen;
            for (const int s : S1)
                for (const int tg : g_.st[s].targets)
                    if (seen.insert(tg).second) nodes.push_back(tg);
            std::sort(nodes.begin(), nodes.end());
        }
        Real C1 = 0;
        for (const int s : S1) C1 += d_[s].w;
        Stadium st;
        const Real h1 = meanHeight(S1);
        if (nodes.size() == 1) {
            st.L = 0;
            st.r = C1 / (2 * kPi);
        } else {
            st.r = h1;
            st.L = std::max((C1 - 2 * kPi * st.r) / 2, 0.5 * h1);
        }
        // centre path: nodes spread over [0, L] with the first stitch's target at x = L (the work descends x on side +z)
        std::unordered_map<int, Real> nodeX;
        const int nn = static_cast<int>(nodes.size());
        Real descending = 1;
        {
            int a = g_.st[S1.front()].targets.front();
            for (const int s : S1) {
                const int b = g_.st[s].targets.front();
                if (b != a) {
                    descending = b < a ? 1 : -1;
                    break;
                }
            }
        }
        for (int i = 0; i < nn; ++i) {
            const Real frac = nn > 1 ? static_cast<Real>(i) / (nn - 1) : 0;
            nodeX[nodes[i]] = descending > 0 ? frac * st.L : (1 - frac) * st.L;
            place(vtx(nodes[i]), Vec3(nodeX[nodes[i]], 0, 0));
        }
        // ring 1: spread evenly by width, starting at the first stitch's target
        Real prevR = st.r, y = 0;
        {
            const int n = static_cast<int>(S1.size());
            const Real P = st.perimeter();
            const Real u0 = st.L - nodeX[g_.st[S1.front()].targets.front()];
            Real acc = 0;
            y = std::sqrt(std::max(h1 * h1 - st.r * st.r, 0.0));
            for (int i = 0; i < n; ++i) {
                if (i > 0) acc += d_[S1[i]].w * P / C1;
                const Real u = u0 + acc;
                placeRingVertex(S1[i], st, u, y, zs);
            }
        }
        // rings 2..: the plan rule
        for (size_t ri = 1; ri < reg.rows.size(); ++ri) {
            const auto& S = rowKept_[reg.rows[ri]];
            const int n = static_cast<int>(S.size());
            Real C = 0;
            for (const int s : S) C += d_[s].w;
            const Real h = meanHeight(S);
            Stadium nx = st;
            nx.r = std::max((C - 2 * st.L) / (2 * kPi), 0.5 * prevR);
            const Real dr = nx.r - prevR;
            y += std::fabs(dr) <= h ? std::sqrt(h * h - dr * dr) : 0;
            // u* from the targets' coordinates on the previous ring, then the ring smoothing
            const Real P = nx.perimeter();
            std::vector<Real> us(n), w(n);
            for (int i = 0; i < n; ++i) {
                w[i] = d_[S[i]].w * P / C;
                const auto& T = g_.st[S[i]].targets;
                Real ref = i == 0 ? kNaN : us[i - 1];
                Real sum = 0;
                int cnt = 0;
                for (const int tg : T) {
                    const Q& q = q_[vtx(tg)];
                    Real u = nx.fromQ(q.seg, q.val);
                    if (!std::isnan(ref)) u += P * std::round((ref - u) / P);
                    else ref = u;
                    sum += u;
                    ++cnt;
                }
                us[i] = cnt ? sum / cnt : (i ? us[i - 1] + w[i] : 0);
            }
            const std::vector<Real> u = spaceRing(us, w, P);
            for (int i = 0; i < n; ++i) placeRingVertex(S[i], nx, u[i], y, zs);
            st = nx;
            prevR = nx.r;
        }
    }

    void placeRingVertex(int stitch, const Stadium& st, Real u, Real y, Real zs) {
        const int v = t_.stitchVertex[stitch];
        const Vec2 p = st.point(u);
        place(v, Vec3(p.x, y, zs * p.y));
        st.toQ(u, q_[v].seg, q_[v].val);
    }

    // ---- Rim ----

    void layoutRimRow(int r) {
        const auto& S = rowKept_[r];
        const int n = static_cast<int>(S.size());
        if (n == 0) return;
        const std::vector<Vec3> outward = boundaryOutward();
        std::vector<Vec3> base(n), out(n);
        std::vector<char> ok(n, 0);
        for (int i = 0; i < n; ++i) {
            const auto& T = g_.st[S[i]].targets;
            Vec3 b(0, 0, 0), o(0, 0, 0);
            int cnt = 0;
            for (const int tg : T) {
                const int v = vtx(tg);
                if (!placed_[v]) continue;
                b += pos_[v];
                o += outward[v];
                ++cnt;
            }
            if (cnt == 0) continue;
            base[i] = b / cnt;
            out[i] = normalized(o);
            ok[i] = 1;
        }
        // a stitch with no usable outward direction takes its neighbour's
        for (int i = 0; i < n; ++i)
            if (ok[i] && length(out[i]) < 0.5) {
                for (int j = 1; j < n && length(out[i]) < 0.5; ++j) {
                    if (i - j >= 0 && ok[i - j] && length(out[i - j]) > 0.5) out[i] = out[i - j];
                    else if (i + j < n && ok[i + j] && length(out[i + j]) > 0.5) out[i] = out[i + j];
                }
            }
        // runs without jumps
        int i = 0;
        while (i < n) {
            if (!ok[i]) { ++i; continue; }
            int j = i;
            while (j + 1 < n && ok[j + 1] && length(base[j + 1] - base[j]) <= kJumpWidths * d_[S[j + 1]].w) ++j;
            placeRun(S, base, out, i, j);
            i = j + 1;
        }
    }

    void placeRun(const std::vector<int>& S, const std::vector<Vec3>& base, const std::vector<Vec3>& out, int a, int b) {
        const int n = b - a + 1;
        std::vector<Real> sx(n, 0), w(n);
        std::vector<char> has(n, 1);
        for (int k = 0; k < n; ++k) {
            w[k] = d_[S[a + k]].w;
            if (k > 0) sx[k] = sx[k - 1] + length(base[a + k] - base[a + k - 1]);
        }
        const std::vector<Real> s = spaceOpen(sx, has, w, 1);
        Vec3 tangent = normalized(cross(out[a], opt_.up));
        if (length(tangent) < 0.5) tangent = normalized(cross(out[a], Vec3(1, 0, 0)));
        const bool degenerate = sx[n - 1] < 1e-9;
        const Real sMean = std::accumulate(s.begin(), s.end(), 0.0) / n;
        for (int k = 0; k < n; ++k) {
            // the point at arc length s[k] of the base polyline (extended straight at the ends)
            Vec3 p, o;
            if (n == 1) {
                p = base[a];
                o = out[a];
            } else if (degenerate) {  // all stitches share a target: spread them along the rim tangent
                p = base[a] + tangent * (s[k] - sMean);
                o = out[a];
            } else {
                int seg = 0;
                while (seg < n - 2 && s[k] > sx[seg + 1]) ++seg;
                while (seg < n - 2 && sx[seg + 1] - sx[seg] < 1e-12) ++seg;
                const Real len = sx[seg + 1] - sx[seg];
                const Real f = len > 1e-12 ? (s[k] - sx[seg]) / len : 0;
                p = base[a + seg] + (base[a + seg + 1] - base[a + seg]) * f;
                o = normalized(out[a + seg] * (1 - std::min(std::max(f, 0.0), 1.0)) + out[a + seg + 1] * std::min(std::max(f, 0.0), 1.0));
                if (length(o) < 0.5) o = out[a + seg];
            }
            place(t_.stitchVertex[S[a + k]], p + o * d_[S[a + k]].h);
        }
    }

    // ---- Strip ----

    bool layoutStrip(const Region& reg) {
        const int a = reg.rows.front(), last = reg.rows.back();
        const std::vector<Vec3> outward = boundaryOutward();
        const auto& Sa = rowKept_[a];
        // origin, outward, surface normal and lateral direction at the rim the strip is worked off
        Vec3 S0(0, 0, 0), out0(0, 0, 0), nrm(0, 0, 0);
        int cnt = 0;
        std::vector<int> tv;
        for (const int s : Sa)
            for (const int tg : g_.st[s].targets) tv.push_back(vtx(tg));
        for (const int v : tv) {
            S0 += pos_[v];
            out0 += outward[v];
            for (const int tri : triOf_[v])
                if (triPlaced(tri)) nrm += triNormal(tri);
            ++cnt;
        }
        if (cnt == 0) return false;
        S0 = S0 / cnt;
        out0 = normalized(out0);
        nrm = normalized(nrm);
        if (length(out0) < 0.5) out0 = Vec3(0, 1, 0);
        Vec3 elam = normalized(cross(nrm, out0));
        {
            const Vec3 dp = pos_[tv.back()] - pos_[tv.front()];
            if (dot(dp, elam) < 0) elam = -elam;
            if (length(elam) < 0.5) elam = normalized(dp);
        }
        // the strip in (lambda, sigma)
        std::unordered_map<int, Real> lam;
        std::unordered_map<int, Real> sig;
        Real sigma = 0, prevDir = 1;
        for (size_t ri = 0; ri < reg.rows.size(); ++ri) {
            const auto& S = rowKept_[reg.rows[ri]];
            const int n = static_cast<int>(S.size());
            std::vector<Real> xs(n, 0), w(n, 0);
            std::vector<char> has(n, 0);
            for (int i = 0; i < n; ++i) {
                w[i] = d_[S[i]].w;
                const auto& T = g_.st[S[i]].targets;
                if (T.empty()) continue;
                Real sum = 0;
                for (const int tg : T) sum += ri == 0 ? dot(pos_[vtx(tg)] - S0, elam) : lam[vtx(tg)];
                xs[i] = sum / T.size();
                has[i] = 1;
            }
            Real dir = 0;
            {
                int fa = -1, fb = -1;
                for (int i = 0; i < n; ++i)
                    if (has[i]) {
                        if (fa < 0) fa = i;
                        fb = i;
                    }
                if (fa >= 0 && fb > fa) dir = xs[fb] - xs[fa] > 0 ? 1 : (xs[fb] - xs[fa] < 0 ? -1 : 0);
            }
            if (dir == 0) dir = ri == 0 ? 1 : (g_.rows[reg.rows[ri - 1]].turned ? -prevDir : prevDir);
            prevDir = dir;
            const std::vector<Real> x = spaceOpen(xs, has, w, dir);
            sigma += meanHeight(S);
            for (int i = 0; i < n; ++i) {
                lam[t_.stitchVertex[S[i]]] = x[i];
                sig[t_.stitchVertex[S[i]]] = sigma;
            }
        }
        // the end of the strap: the join links (link.a is the closing sl st whose own target is the rim stitch)
        Vec3 E(0, 0, 0);
        int endCount = 0;
        {
            std::set<int> endStitches(rowKept_[last].begin(), rowKept_[last].end());
            for (const PointSeam& ps : pointSeams(g_))
                if (ps.kind == "join" && endStitches.count(ps.partner))
                    for (const int tg : g_.st[ps.stitch].targets)
                        if (placed_[vtx(tg)]) {
                            E += pos_[vtx(tg)];
                            ++endCount;
                        }
        }
        const Real total = sigma;
        Vec3 chat, bhat, lat = elam;
        Real R = 0, alpha = 0;
        bool arc = false;
        if (endCount > 0) {
            E = E / endCount;
            const Vec3 chord = E - S0;
            const Real c = length(chord);
            chat = c > 1e-7 ? chord / c : normalized(cross(elam, opt_.up));
            bhat = normalized(opt_.up - chat * dot(opt_.up, chat));
            if (length(bhat) < 0.5) bhat = out0;
            // sin(x)/x = c/total with x = alpha, alpha in (0, pi): bisection
            const Real target = std::min(c / total, 0.9999);
            Real lo = 1e-6, hi = kPi;
            for (int it = 0; it < 100; ++it) {
                const Real mid = 0.5 * (lo + hi);
                if (std::sin(mid) / mid > target) lo = mid;
                else hi = mid;
            }
            alpha = 0.5 * (lo + hi);
            R = total / (2 * alpha);
            lat = normalized(cross(chat, bhat));
            if (dot(lat, elam) < 0) lat = -lat;
            arc = true;
            report_.stripArcLength = total;
            report_.stripChord = c;
        }
        const Vec3 C0 = arc ? (S0 + chat * (0.5 * length(E - S0))) - bhat * (R * std::cos(alpha)) : Vec3(0, 0, 0);
        std::set<int> own;
        for (const int r : reg.rows)
            for (const int s : rowKept_[r]) own.insert(t_.stitchVertex[s]);
        auto placeAll = [&](Real ls) {
            for (const auto& kv : lam) {
                const int v = kv.first;
                const Real l = ls * kv.second, sg = sig[v];
                Vec3 p;
                if (arc) {
                    const Real th = -alpha + sg / R;
                    p = C0 + (chat * std::sin(th) + bhat * std::cos(th)) * R + lat * l;
                } else {
                    p = S0 + out0 * sg + elam * l;
                }
                place(v, p);
            }
        };
        // Orientation: keep the sign of lambda for which the faces of the strip and the faces between it and the rim
        // continue the surface (neighbouring winding normals agree).
        auto agreement = [&]() {
            Real agree = 0;
            std::set<int> tris;
            for (const int v : own)
                for (const int tri : triOf_[v]) tris.insert(tri);
            for (const int tri : tris) {
                if (!triPlaced(tri)) continue;
                for (int k = 0; k < 3; ++k) {
                    const int e0 = t_.tris[tri][k], e1 = t_.tris[tri][(k + 1) % 3];
                    for (const int other : triOf_[e0]) {
                        if (other == tri || !triPlaced(other)) continue;
                        const auto& ov = t_.tris[other];
                        if (ov[0] == e1 || ov[1] == e1 || ov[2] == e1) agree += dot(triNormal(tri), triNormal(other)) > 0 ? 1 : -1;
                    }
                }
            }
            return agree;
        };
        placeAll(1);
        const Real score0 = agreement();
        placeAll(-1);
        if (agreement() > score0) return true;
        placeAll(1);
        return false;
    }

    void finishUnplaced() {
        // anything not placed (dropped regions, unexpected structure) takes the position of a placed neighbour
        bool changed = true;
        int unplaced = 0;
        for (int v = 0; v < nv_; ++v) unplaced += !placed_[v];
        if (unplaced == 0) return;
        report_.warnings.push_back(std::to_string(unplaced) + " vertices were not placed by any region; they take a neighbour's position");
        while (changed) {
            changed = false;
            for (size_t i = 0; i < t_.tris.size(); ++i)
                for (int k = 0; k < 3; ++k) {
                    const int a = t_.tris[i][k], b = t_.tris[i][(k + 1) % 3];
                    if (!placed_[a] && placed_[b]) {
                        place(a, pos_[b]);
                        changed = true;
                    }
                }
        }
    }
};

}  // namespace

LayoutReport layoutModel(CrochetModel& model, const StitchGraph& g, const LayoutOptions& opt) {
    return Layouter(model, g, opt).run();
}

}  // namespace fl
