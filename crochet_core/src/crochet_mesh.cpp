// Stitch graph -> mesh topology (docs/CROCHET_PLAN.md sections 6.1-6.4). The
// decisions the plan leaves open are listed in crochet/mesh.hpp.
#include "failoring/crochet/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "failoring/error.hpp"

namespace fl {

namespace {

constexpr int kMaxBasePath = 6;      // plan 6.2: BFS limit, in edges
constexpr Real kMinHeightFrac = 0.3; // plan 6.3: Hf >= 0.3 h_s

StitchType stitchTypeOf(const std::string& t, int id) {
    if (t == "ch") return StitchType::Ch;
    if (t == "slst") return StitchType::SlSt;
    if (t == "sc") return StitchType::Sc;
    if (t == "hdc") return StitchType::Hdc;
    if (t == "dc") return StitchType::Dc;
    if (t == "tr") return StitchType::Tr;
    FL_THROW(std::runtime_error, "stitch " + std::to_string(id) + " has unknown type '" + t + "'");
}

Insertion insertionOf(const std::string& loop) {
    if (loop == "blo") return Insertion::BLO;
    if (loop == "flo") return Insertion::FLO;
    if (loop == "space") return Insertion::Space;
    return Insertion::Both;  // both, fpost, bpost
}

std::uint64_t edgeKey(int a, int b) {
    if (a > b) std::swap(a, b);
    return (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint32_t>(b);
}

struct EdgeInfo {
    int faces = 0;
    Real len = 0;  // rest length (m), set when the edge is first created
};

struct Tri {
    std::array<int, 3> v;          // stitch ids
    std::array<Vec2, 3> rest;      // (u = wale, v = course)
    int src = -1;
};

class Builder {
public:
    Builder(const StitchGraph& g, const std::vector<StitchDims>& d) : g_(g), d_(d) {
        const size_t n = g.st.size();
        adj_.resize(n);
        join_.assign(n, 0);
        linked_.assign(n, 0);
        explained_.assign(n, 0);
        stamp_.assign(n, 0);
        parent_.assign(n, -1);
        rowStitches_.resize(g.rows.size());
        for (const Stitch& s : g.st) rowStitches_[s.row].push_back(s.id);
        for (const Link& l : g.links) linked_[l.a] = linked_[l.b] = 1;
        findJoins();
        addChainEdges();
    }

    CrochetMesh run() {
        for (size_t r = 0; r < g_.rows.size(); ++r) processRow(static_cast<int>(r));
        return finish();
    }

private:
    const StitchGraph& g_;
    const std::vector<StitchDims>& d_;
    std::unordered_map<std::uint64_t, EdgeInfo> edges_;
    std::vector<std::vector<int>> adj_;  // stitch id -> neighbours over existing edges
    std::vector<char> join_, linked_, explained_;
    std::vector<std::vector<int>> rowStitches_;
    std::vector<Tri> tris_;
    std::vector<std::string> warnings_;
    std::vector<MeshJump> jumps_;
    std::vector<int> stamp_, parent_;
    int gen_ = 0;

    // A closing sl st: the last stitch of a round, with one target, in its own round.
    void findJoins() {
        for (size_t r = 0; r < g_.rows.size(); ++r) {
            if (g_.rows[r].kind != "round" || rowStitches_[r].empty()) continue;
            const Stitch& s = g_.st[rowStitches_[r].back()];
            if (s.type != "slst" || s.targets.size() != 1) continue;
            if (g_.st[s.targets[0]].row == s.row) join_[s.id] = 1;
        }
    }

    EdgeInfo& ensureEdge(int a, int b, Real len) {
        const std::uint64_t k = edgeKey(a, b);
        auto it = edges_.find(k);
        if (it != edges_.end()) return it->second;
        adj_[a].push_back(b);
        adj_[b].push_back(a);
        EdgeInfo& e = edges_[k];
        e.len = len;
        return e;
    }

    // Foundation chains: the prev link between two target-less chains is a chain edge.
    void addChainEdges() {
        for (const Stitch& s : g_.st) {
            if (s.type != "ch" || !s.targets.empty() || s.prev < 0) continue;
            const Stitch& p = g_.st[s.prev];
            if (p.type == "ch" && p.targets.empty()) ensureEdge(p.id, s.id, d_[s.id].w);
        }
    }

    // Shortest path u -> v of at most kMaxBasePath edges over edges in fewer than
    // two faces (or over any edge when `any`). Empty when there is none.
    std::vector<int> path(int u, int v, bool any) {
        if (u == v) return {u};
        ++gen_;
        stamp_[u] = gen_;
        std::vector<int> frontier{u}, next;
        for (int level = 0; level < kMaxBasePath && !frontier.empty(); ++level) {
            next.clear();
            for (const int x : frontier) {
                for (const int y : adj_[x]) {
                    if (stamp_[y] == gen_) continue;
                    if (!any && edges_[edgeKey(x, y)].faces >= 2) continue;
                    stamp_[y] = gen_;
                    parent_[y] = x;
                    if (y == v) {
                        std::vector<int> p{v};
                        for (int c = v; c != u;) p.push_back(c = parent_[c]);
                        std::reverse(p.begin(), p.end());
                        return p;
                    }
                    next.push_back(y);
                }
            }
            frontier.swap(next);
        }
        return {};
    }

    Real dist(const Vec2& a, const Vec2& b) const { return length(a - b); }

    // Zipper between the top polyline A (current row, left to right in working
    // order) and the base polyline B (plan 6.2 step 3, 6.3). `s` is the upper
    // stitch whose height and material rule the polygon.
    bool addStrip(const std::vector<int>& A, const std::vector<int>& B, int s) {
        const size_t m = A.size(), n = B.size();
        for (const int a : A)
            for (const int b : B)
                if (a == b) {
                    warnings_.push_back("stitch " + std::to_string(a) + " is both on top and on the base of one face (stitch " +
                                        std::to_string(s) + ")");
                    return false;
                }
        // rest positions in the polygon frame (x along the work, y up the wale)
        std::vector<Real> xa(m, 0), xb(n, 0);
        Real wt = 0, wb = 0;
        for (size_t i = 1; i < m; ++i) xa[i] = (wt += d_[A[i]].w);  // course width at the right end
        for (size_t j = 1; j < n; ++j) xb[j] = (wb += edges_[edgeKey(B[j - 1], B[j])].len);
        for (Real& x : xa) x -= wt / 2;
        for (Real& x : xb) x -= wb / 2;
        const Real hs = d_[s].h;
        const Real dx = (wb - wt) / 2;
        const Real hf = std::sqrt(std::max(hs * hs - dx * dx, (kMinHeightFrac * hs) * (kMinHeightFrac * hs)));
        auto posA = [&](size_t i) { return Vec2(xa[i], hf); };
        auto posB = [&](size_t j) { return Vec2(xb[j], 0); };

        // edges: the top polyline (course), the two legs, then the zipper
        for (size_t i = 1; i < m; ++i) ensureEdge(A[i - 1], A[i], d_[A[i]].w);
        ensureEdge(A[0], B[0], d_[A[0]].h);
        ensureEdge(A[m - 1], B[n - 1], d_[A[m - 1]].h);

        size_t i = 0, j = 0;
        while (i + 1 < m || j + 1 < n) {
            bool advanceA;
            if (i + 1 >= m) advanceA = false;
            else if (j + 1 >= n) advanceA = true;
            else advanceA = dist(posA(i + 1), posB(j)) <= dist(posA(i), posB(j + 1));
            Tri t;
            t.src = s;
            std::array<int, 3> id;
            std::array<Vec2, 3> p;
            if (advanceA) {  // (B_j, A_{i+1}, A_i): counter-clockwise in (x, y)
                id = {B[j], A[i + 1], A[i]};
                p = {posB(j), posA(i + 1), posA(i)};
                ++i;
            } else {  // (B_j, B_{j+1}, A_i)
                id = {B[j], B[j + 1], A[i]};
                p = {posB(j), posB(j + 1), posA(i)};
                ++j;
            }
            t.v = id;
            for (int k = 0; k < 3; ++k) t.rest[k] = Vec2(p[k].y, p[k].x);  // (u, v) = (y, x)
            for (int k = 0; k < 3; ++k) {
                const int a = id[k], b = id[(k + 1) % 3];
                ensureEdge(a, b, dist(p[k], p[(k + 1) % 3])).faces++;
            }
            tris_.push_back(t);
        }
        return true;
    }

    // A pair (p, s) whose base nodes no boundary path connects.
    void unresolved(int p, int s, int u, int v) {
        if (g_.st[p].type == "slst" && g_.st[s].type == "slst" && !path(u, v, true).empty()) {
            explained_[p] = explained_[s] = 1;  // surface sl st: the base edges are full, the row lies on the fabric
            return;
        }
        if (linked_[p] || linked_[s]) {
            jumps_.push_back({p, s});  // a point seam supplies the connection
            explained_[p] = explained_[s] = 1;
            return;
        }
        warnings_.push_back("hole between stitch " + std::to_string(p) + " and " + std::to_string(s) + " (targets " +
                            std::to_string(u) + " and " + std::to_string(v) + " are not connected within " +
                            std::to_string(kMaxBasePath) + " edges)");
    }

    void processRow(int r) {
        int prevAnchor = -1, firstAnchor = -1;
        std::vector<int> mids;  // target-less chains since the previous anchor
        for (const int id : rowStitches_[r]) {
            const Stitch& s = g_.st[id];
            if (join_[id]) continue;
            if (s.targets.empty()) {
                if (prevAnchor >= 0) mids.push_back(id);
                continue;
            }
            if (prevAnchor >= 0) {
                const int u = g_.st[prevAnchor].targets.back(), v = s.targets.front();
                const std::vector<int> base = path(u, v, false);
                if (base.empty()) {
                    unresolved(prevAnchor, id, u, v);
                } else {
                    std::vector<int> top{prevAnchor};
                    top.insert(top.end(), mids.begin(), mids.end());
                    top.push_back(id);
                    addStrip(top, base, id);
                }
            } else {
                firstAnchor = id;
            }
            // a decrease: fill the polygon between consecutive targets and the stitch (plan 6.2 step 2)
            for (size_t k = 0; k + 1 < s.targets.size(); ++k) {
                const std::vector<int> base = path(s.targets[k], s.targets[k + 1], false);
                if (base.empty()) unresolved(id, id, s.targets[k], s.targets[k + 1]);
                else addStrip({id}, base, id);
            }
            prevAnchor = id;
            mids.clear();
        }
        // a round closes onto its first anchor when the base ends are the same or neighbouring nodes
        if (g_.rows[r].kind == "round" && firstAnchor >= 0 && prevAnchor != firstAnchor) {
            const int u = g_.st[prevAnchor].targets.back(), v = g_.st[firstAnchor].targets.front();
            const std::vector<int> base = path(u, v, false);
            if (!base.empty() && base.size() <= 2) addStrip({prevAnchor, firstAnchor}, base, firstAnchor);
        }
    }

    // --- finishing: manifold check, orientation, vertices, dropped stitches ---

    static bool hasDirected(const Tri& t, int a, int b) {
        for (int k = 0; k < 3; ++k)
            if (t.v[k] == a && t.v[(k + 1) % 3] == b) return true;
        return false;
    }

    static void flip(Tri& t) {
        std::swap(t.v[1], t.v[2]);
        std::swap(t.rest[1], t.rest[2]);
        for (Vec2& p : t.rest) p.y = -p.y;  // mirror the course coordinate
    }

    CrochetMesh finish() {
        const int T = static_cast<int>(tris_.size());
        std::unordered_map<std::uint64_t, std::vector<int>> edgeTris;
        for (int t = 0; t < T; ++t)
            for (int k = 0; k < 3; ++k) edgeTris[edgeKey(tris_[t].v[k], tris_[t].v[(k + 1) % 3])].push_back(t);
        for (const auto& kv : edgeTris)
            if (kv.second.size() > 2)
                warnings_.push_back("edge between stitch " + std::to_string(static_cast<int>(kv.first >> 32)) + " and " +
                                    std::to_string(static_cast<int>(kv.first & 0xffffffffu)) + " is in " +
                                    std::to_string(kv.second.size()) + " faces");

        // orientation: BFS over the dual graph across manifold edges
        std::vector<int> comp(T, -1);
        int components = 0;
        std::vector<int> queue;
        for (int start = 0; start < T; ++start) {
            if (comp[start] >= 0) continue;
            comp[start] = components;
            queue.assign(1, start);
            for (size_t qi = 0; qi < queue.size(); ++qi) {
                const int t = queue[qi];
                for (int k = 0; k < 3; ++k) {
                    const int a = tris_[t].v[k], b = tris_[t].v[(k + 1) % 3];
                    const std::vector<int>& sharing = edgeTris[edgeKey(a, b)];
                    if (sharing.size() != 2) continue;
                    const int o = sharing[0] == t ? sharing[1] : sharing[0];
                    if (comp[o] < 0) {
                        if (hasDirected(tris_[o], a, b)) flip(tris_[o]);
                        comp[o] = components;
                        queue.push_back(o);
                    } else if (hasDirected(tris_[o], a, b)) {
                        FL_THROW(std::runtime_error, "cannot orient the surface: the faces of stitch " +
                                                         std::to_string(tris_[t].src) + " and " + std::to_string(tris_[o].src) +
                                                         " traverse the edge between stitch " + std::to_string(a) + " and " +
                                                         std::to_string(b) + " in the same direction");
                    }
                }
            }
            ++components;
        }

        CrochetMesh m;
        m.stitchVertex.assign(g_.st.size(), -1);
        std::vector<char> used(g_.st.size(), 0);
        for (const Tri& t : tris_)
            for (const int v : t.v) used[v] = 1;
        for (size_t i = 0; i < g_.st.size(); ++i)
            if (used[i]) {
                m.stitchVertex[i] = static_cast<int>(m.vertexStitch.size());
                m.vertexStitch.push_back(static_cast<int>(i));
            }
        m.tris.reserve(T);
        for (int t = 0; t < T; ++t) {
            m.tris.push_back({m.stitchVertex[tris_[t].v[0]], m.stitchVertex[tris_[t].v[1]], m.stitchVertex[tris_[t].v[2]]});
            m.triRest.push_back(tris_[t].rest);
            m.triSource.push_back(tris_[t].src);
        }
        m.triComponent = comp;
        m.components = components;

        // dropped stitches: nearest kept stitch over the graph's prev/target links
        m.dropTo.assign(g_.st.size(), -1);
        std::vector<std::vector<int>> link(g_.st.size());
        for (const Stitch& s : g_.st) {
            if (s.prev >= 0) {
                link[s.id].push_back(s.prev);
                link[s.prev].push_back(s.id);
            }
            for (const int t : s.targets) {
                link[s.id].push_back(t);
                link[t].push_back(s.id);
            }
        }
        std::vector<int> frontier;
        for (size_t i = 0; i < g_.st.size(); ++i)
            if (used[i]) {
                m.dropTo[i] = m.stitchVertex[i];
                frontier.push_back(static_cast<int>(i));
            }
        for (size_t qi = 0; qi < frontier.size(); ++qi)
            for (const int y : link[frontier[qi]])
                if (m.dropTo[y] < 0) {
                    m.dropTo[y] = m.dropTo[frontier[qi]];
                    frontier.push_back(y);
                }
        for (const Stitch& s : g_.st)
            if (!used[s.id] && s.type != "ch" && !join_[s.id] && !explained_[s.id])
                warnings_.push_back("stitch " + std::to_string(s.id) + " (" + s.type + ", row " + std::to_string(s.row) +
                                    ") is in no face");

        m.warnings = warnings_;
        m.jumps = jumps_;
        return m;
    }
};

}  // namespace

std::vector<StitchDims> graphDims(const StitchGraph& g, const Yarn& yarn, Real hook_m, Real tension,
                                  const Calibration* calibration) {
    std::map<std::pair<int, int>, StitchDims> cache;
    std::vector<StitchDims> out;
    out.reserve(g.st.size());
    for (const Stitch& s : g.st) {
        const StitchType type = stitchTypeOf(s.type, s.id);
        const Insertion ins = insertionOf(s.loopRs);
        const auto key = std::make_pair(static_cast<int>(type), static_cast<int>(ins));
        auto it = cache.find(key);
        if (it == cache.end()) it = cache.emplace(key, stitchDims(yarn, hook_m, tension, type, ins, calibration)).first;
        out.push_back(it->second);
    }
    return out;
}

CrochetMesh buildMeshTopology(const StitchGraph& g, const std::vector<StitchDims>& dims) {
    if (dims.size() != g.st.size())
        FL_THROW(std::runtime_error, "buildMeshTopology: " + std::to_string(dims.size()) + " stitch sizes for " +
                                         std::to_string(g.st.size()) + " stitches");
    for (const Stitch& s : g.st)
        if (!(dims[s.id].w > 0) || !(dims[s.id].h > 0))
            FL_THROW(std::runtime_error, "buildMeshTopology: stitch " + std::to_string(s.id) + " has a non-positive size");
    return Builder(g, dims).run();
}

}  // namespace fl
