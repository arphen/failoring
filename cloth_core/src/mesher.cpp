#include "failoring/mesher.hpp"
#include "failoring/error.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

namespace fl {

Real polygonSignedArea(const std::vector<Vec2>& p) {
    Real a = 0;
    for (size_t i = 0; i < p.size(); ++i) a += cross(p[i], p[(i + 1) % p.size()]);
    return 0.5 * a;
}

namespace {

bool pointInPolygon(const Vec2& pt, const std::vector<Vec2>& poly) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec2& a = poly[i];
        const Vec2& b = poly[j];
        if (((a.y > pt.y) != (b.y > pt.y)) && (pt.x < (b.x - a.x) * (pt.y - a.y) / (b.y - a.y) + a.x)) in = !in;
    }
    return in;
}

Real distToSegment(const Vec2& p, const Vec2& a, const Vec2& b) {
    Vec2 ab = b - a;
    Real t = dot(p - a, ab) / std::max(dot(ab, ab), Real(1e-30));
    t = std::min(std::max(t, Real(0)), Real(1));
    return length(p - (a + ab * t));
}

struct DTri {
    int a, b, c;
    Real cx, cy, r2;
    bool bad;
};

void circumcircle(const std::vector<Vec2>& P, DTri& t) {
    const Vec2 &A = P[t.a], &B = P[t.b], &C = P[t.c];
    Real d = 2 * (A.x * (B.y - C.y) + B.x * (C.y - A.y) + C.x * (A.y - B.y));
    if (std::fabs(d) < 1e-300) { t.cx = t.cy = 0; t.r2 = 1e300; return; }  // degenerate: contains everything
    Real a2 = dot(A, A), b2 = dot(B, B), c2 = dot(C, C);
    t.cx = (a2 * (B.y - C.y) + b2 * (C.y - A.y) + c2 * (A.y - B.y)) / d;
    t.cy = (a2 * (C.x - B.x) + b2 * (A.x - C.x) + c2 * (B.x - A.x)) / d;
    t.r2 = (A.x - t.cx) * (A.x - t.cx) + (A.y - t.cy) * (A.y - t.cy);
}

// Bowyer-Watson Delaunay triangulation. Returns triangles over the input points.
std::vector<std::array<int, 3>> delaunay(std::vector<Vec2> pts) {
    const int n = static_cast<int>(pts.size());
    Vec2 lo = pts[0], hi = pts[0];
    for (const auto& p : pts) {
        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
    }
    const Real span = std::max(hi.x - lo.x, hi.y - lo.y) + 1e-9;
    const Vec2 mid((lo.x + hi.x) * 0.5, (lo.y + hi.y) * 0.5);
    pts.push_back({mid.x - 40 * span, mid.y - 20 * span});
    pts.push_back({mid.x + 40 * span, mid.y - 20 * span});
    pts.push_back({mid.x, mid.y + 40 * span});

    std::vector<DTri> tris;
    tris.push_back({n, n + 1, n + 2, 0, 0, 0, false});
    circumcircle(pts, tris.back());

    std::vector<std::pair<int, int>> edges;
    for (int i = 0; i < n; ++i) {
        const Vec2& p = pts[i];
        edges.clear();
        for (auto& t : tris) {
            Real dx = p.x - t.cx, dy = p.y - t.cy;
            t.bad = dx * dx + dy * dy < t.r2 * (1 - 1e-12);
        }
        for (const auto& t : tris) {
            if (!t.bad) continue;
            edges.push_back({t.a, t.b});
            edges.push_back({t.b, t.c});
            edges.push_back({t.c, t.a});
        }
        tris.erase(std::remove_if(tris.begin(), tris.end(), [](const DTri& t) { return t.bad; }), tris.end());
        // keep only edges that occur once (the cavity boundary)
        std::vector<bool> keep(edges.size(), true);
        for (size_t e = 0; e < edges.size(); ++e)
            for (size_t f = e + 1; f < edges.size(); ++f)
                if ((edges[e].first == edges[f].second && edges[e].second == edges[f].first) ||
                    (edges[e].first == edges[f].first && edges[e].second == edges[f].second)) {
                    keep[e] = keep[f] = false;
                }
        for (size_t e = 0; e < edges.size(); ++e) {
            if (!keep[e]) continue;
            DTri t{edges[e].first, edges[e].second, i, 0, 0, 0, false};
            circumcircle(pts, t);
            tris.push_back(t);
        }
    }
    std::vector<std::array<int, 3>> out;
    for (const auto& t : tris)
        if (t.a < n && t.b < n && t.c < n) out.push_back({t.a, t.b, t.c});
    return out;
}

}  // namespace

PanelMesh meshPolygon(const std::vector<Vec2>& outline, Real h) {
    return meshPolygon(outline, {}, h);
}

PanelMesh meshPolygon(const std::vector<Vec2>& outline, const std::vector<std::vector<Vec2>>& holes, Real h) {
    if (outline.size() < 3) FL_THROW(std::invalid_argument, "meshPolygon: need at least 3 outline points");
    if (h <= 0) FL_THROW(std::invalid_argument, "meshPolygon: edge length must be positive");
    for (const auto& hole : holes)
        if (hole.size() < 3) FL_THROW(std::invalid_argument, "meshPolygon: a hole needs at least 3 points");

    PanelMesh m;
    std::vector<Vec2>& P = m.verts;
    const size_t no = outline.size();

    // 1. boundary points, exact on the outline segments
    std::vector<std::pair<int, int>> boundarySegs;  // consecutive boundary vertices
    int firstOfEdge0 = 0;
    for (size_t i = 0; i < no; ++i) {
        const Vec2 a = outline[i], b = outline[(i + 1) % no];
        const int nseg = std::max(1, static_cast<int>(std::lround(length(b - a) / h)));
        const int start = static_cast<int>(P.size());
        if (i == 0) firstOfEdge0 = start;
        for (int k = 0; k < nseg; ++k) P.push_back(a + (b - a) * (Real(k) / nseg));
        const int cnt = static_cast<int>(P.size()) - start;
        for (int k = 0; k < cnt; ++k) {
            const int v0 = start + k;
            const int v1 = (k + 1 < cnt) ? start + k + 1 : (i + 1 < no ? static_cast<int>(P.size()) : firstOfEdge0);
            boundarySegs.push_back({v0, v1});
        }
    }
    const int nBoundary = static_cast<int>(P.size());
    // hole loops: the same exact boundary points, closed on themselves
    for (const auto& hole : holes) {
        const size_t nh = hole.size();
        const int holeFirst = static_cast<int>(P.size());
        for (size_t i = 0; i < nh; ++i) {
            const Vec2 a = hole[i], b = hole[(i + 1) % nh];
            const int nseg = std::max(1, static_cast<int>(std::lround(length(b - a) / h)));
            const int start = static_cast<int>(P.size());
            for (int k = 0; k < nseg; ++k) P.push_back(a + (b - a) * (Real(k) / nseg));
            const int cnt = static_cast<int>(P.size()) - start;
            for (int k = 0; k < cnt; ++k) {
                const int v0 = start + k;
                const int v1 = (k + 1 < cnt) ? start + k + 1 : (i + 1 < nh ? static_cast<int>(P.size()) : holeFirst);
                boundarySegs.push_back({v0, v1});
            }
        }
    }

    // 2. interior hex lattice, kept away from the boundary and out of the holes
    Vec2 lo = outline[0], hi = outline[0];
    for (const auto& p : outline) {
        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
    }
    const Real rowH = h * std::sqrt(3.0) / 2;
    int row = 0;
    for (Real y = lo.y + rowH * 0.5; y < hi.y; y += rowH, ++row) {
        const Real off = (row % 2) ? h * 0.5 : 0;
        for (Real x = lo.x + off + h * 0.25; x < hi.x; x += h) {
            Vec2 p(x, y);
            if (!pointInPolygon(p, outline)) continue;
            bool inHole = false;
            for (const auto& hole : holes)
                if (pointInPolygon(p, hole)) { inHole = true; break; }
            if (inHole) continue;
            Real dmin = 1e30;
            for (size_t i = 0; i < no; ++i) dmin = std::min(dmin, distToSegment(p, outline[i], outline[(i + 1) % no]));
            for (const auto& hole : holes)
                for (size_t i = 0; i < hole.size(); ++i)
                    dmin = std::min(dmin, distToSegment(p, hole[i], hole[(i + 1) % hole.size()]));
            if (dmin > 0.6 * h) P.push_back(p);
        }
    }

    // 3. Delaunay, clip to polygon and out of the holes
    auto all = delaunay(P);
    for (auto t : all) {
        Vec2 c = (P[t[0]] + P[t[1]] + P[t[2]]) / 3.0;
        if (!pointInPolygon(c, outline)) continue;
        bool inHole = false;
        for (const auto& hole : holes)
            if (pointInPolygon(c, hole)) { inHole = true; break; }
        if (inHole) continue;
        Real ar = cross(P[t[1]] - P[t[0]], P[t[2]] - P[t[0]]);
        if (std::fabs(ar) < 1e-14) continue;
        if (ar < 0) std::swap(t[1], t[2]);
        m.tris.push_back(t);
    }

    // 4. validation: every boundary segment must be a triangle edge
    std::map<std::pair<int, int>, int> edgeCount;
    for (const auto& t : m.tris)
        for (int k = 0; k < 3; ++k) {
            int a = t[k], b = t[(k + 1) % 3];
            if (a > b) std::swap(a, b);
            edgeCount[{a, b}]++;
        }
    for (auto s : boundarySegs) {
        int a = s.first, b = s.second;
        if (a > b) std::swap(a, b);
        auto it = edgeCount.find({a, b});
        if (it == edgeCount.end() || it->second != 1) m.boundaryComplete = false;
    }

    // 5. chains per outline edge, by geometric membership (orientation independent)
    m.edgeChains.assign(no, {});
    for (size_t i = 0; i < no; ++i) {
        const Vec2 a = outline[i], b = outline[(i + 1) % no];
        const Vec2 ab = b - a;
        std::vector<std::pair<Real, int>> on;
        for (int v = 0; v < nBoundary; ++v) {
            if (distToSegment(P[v], a, b) < 1e-9 * std::max(Real(1), length(ab)))
                on.push_back({dot(P[v] - a, ab) / std::max(dot(ab, ab), Real(1e-30)), v});
        }
        std::sort(on.begin(), on.end());
        for (auto& pr : on) m.edgeChains[i].push_back(pr.second);
        // the end corner belongs to this edge as well
    }
    // Interior points that were appended after the boundary are never on an edge by construction.
    // Make sure each chain includes its end corner vertex (it lies exactly at t=1).
    return m;
}

PanelMesh meshRect(Real w, Real height, Real h) {
    if (w <= 0 || height <= 0 || h <= 0) FL_THROW(std::invalid_argument, "meshRect: sizes must be positive");
    const int nx = std::max(1, static_cast<int>(std::lround(w / h)));
    const int ny = std::max(1, static_cast<int>(std::lround(height / h)));
    PanelMesh m;
    auto id = [&](int i, int j) { return j * (nx + 1) + i; };
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i <= nx; ++i) m.verts.push_back({w * i / nx, height * j / ny});
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            const int v00 = id(i, j), v10 = id(i + 1, j), v01 = id(i, j + 1), v11 = id(i + 1, j + 1);
            if ((i + j) % 2 == 0) {
                m.tris.push_back({v00, v10, v11});
                m.tris.push_back({v00, v11, v01});
            } else {
                m.tris.push_back({v00, v10, v01});
                m.tris.push_back({v10, v11, v01});
            }
        }
    m.edgeChains.assign(4, {});
    for (int i = 0; i <= nx; ++i) m.edgeChains[0].push_back(id(i, 0));
    for (int j = 0; j <= ny; ++j) m.edgeChains[1].push_back(id(nx, j));
    for (int i = nx; i >= 0; --i) m.edgeChains[2].push_back(id(i, ny));
    for (int j = ny; j >= 0; --j) m.edgeChains[3].push_back(id(0, j));
    return m;
}

PanelMesh meshDisc(Real radius, Real h) {
    const int n = std::max(8, static_cast<int>(std::lround(2 * kPi * radius / h)));
    std::vector<Vec2> poly;
    for (int i = 0; i < n; ++i) poly.push_back({radius * std::cos(2 * kPi * i / n), radius * std::sin(2 * kPi * i / n)});
    return meshPolygon(poly, h);
}

}  // namespace fl
