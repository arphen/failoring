#include "failoring/cloth.hpp"
#include "failoring/error.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace fl {

namespace detail {

Real hingeAngle(const Vec3& x1, const Vec3& x2, const Vec3& x3, const Vec3& x4, Vec3* g) {
    const Vec3 e = x2 - x1, p3 = x3 - x1, p4 = x4 - x1;
    const Vec3 c3 = cross(e, p3), c4 = cross(e, p4);
    const Real l3 = length(c3), l4 = length(c4);
    if (l3 < 1e-14 || l4 < 1e-14) {
        if (g) g[0] = g[1] = g[2] = g[3] = Vec3(0, 0, 0);
        return -1;
    }
    const Vec3 n1 = c3 / l3, n2 = c4 / l4;
    const Real d = dot(n1, n2);
    const Real s = length(cross(n1, n2));
    const Real theta = std::atan2(s, -d);  // 0 when the two triangles are coplanar and flat
    if (!g) return theta;
    if (s < 1e-9) {
        g[0] = g[1] = g[2] = g[3] = Vec3(0, 0, 0);
        return theta;
    }
    // Gradients after Muller et al. 2007 (PBD), converted to our angle convention.
    const Vec3 q3 = (cross(e, n2) + cross(n1, e) * d) / l3;
    const Vec3 q4 = (cross(e, n1) + cross(n2, e) * d) / l4;
    const Vec3 q2 = -(cross(p3, n2) + cross(n1, p3) * d) / l3 - (cross(p4, n1) + cross(n2, p4) * d) / l4;
    const Vec3 q1 = -q2 - q3 - q4;
    const Real k = kHingeGradSign / s;
    g[0] = q1 * k;
    g[1] = q2 * k;
    g[2] = q3 * k;
    g[3] = q4 * k;
    return theta;
}

}  // namespace detail

namespace {

// Hinge-stiffness calibration. Measured with a cylinder-bending energy check on hex meshes
// (tests/test_basics + scratch checks): the hinge sum matches continuum energy 0.5*B*k^2 for a
// *constrained* cylinder at B_h = 4/3 * B. Textile bending rigidity B (KES, cantilever) is that
// of a strip free to curl sideways, which on the equivalent discrete plate (Poisson ~ 1/3) is
// softer by (1 - nu^2) = 8/9, so the factor becomes 4/3 * 9/8 = 3/2.
constexpr Real kBendMeshFactor = 1.5;

struct Closest {
    Vec3 q;
    Real u, v, w;
};

// Closest point on triangle (Ericson, Real-Time Collision Detection 5.1.5) with barycentrics.
Closest closestOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 ab = b - a, ac = c - a, ap = p - a;
    const Real d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return Closest{a, 1, 0, 0};
    const Vec3 bp = p - b;
    const Real d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return Closest{b, 0, 1, 0};
    const Real vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        const Real v = d1 / (d1 - d3);
        return Closest{a + ab * v, 1 - v, v, 0};
    }
    const Vec3 cp = p - c;
    const Real d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return Closest{c, 0, 0, 1};
    const Real vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        const Real w = d2 / (d2 - d6);
        return Closest{a + ac * w, 1 - w, 0, w};
    }
    const Real va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        const Real w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return Closest{b + (c - b) * w, 0, 1 - w, w};
    }
    const Real denom = 1 / (va + vb + vc);
    const Real v = vb * denom, w = vc * denom;
    return Closest{a + ab * v + ac * w, 1 - v - w, v, w};
}

inline std::uint64_t pairKey(int v, int t) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(v)) << 32) | static_cast<std::uint32_t>(t);
}

inline size_t hashCell(int ix, int iy, int iz) {
    return (static_cast<size_t>(static_cast<std::uint32_t>(ix)) * 73856093u) ^
           (static_cast<size_t>(static_cast<std::uint32_t>(iy)) * 19349663u) ^
           (static_cast<size_t>(static_cast<std::uint32_t>(iz)) * 83492791u);
}

// One scalar XPBD constraint over up to 4 particles: C, gradients g[i], inverse masses w[i].
inline void xpbdSolve(int n, Vec3* const* x, const Real* w, const Vec3* g, Real C, Real alphaTilde, Real& lambda) {
    Real denom = alphaTilde;
    for (int k = 0; k < n; ++k) denom += w[k] * length2(g[k]);
    if (denom < 1e-30) return;
    const Real dl = (-C - alphaTilde * lambda) / denom;
    lambda += dl;
    for (int k = 0; k < n; ++k) *x[k] += g[k] * (w[k] * dl);
}

}  // namespace

// ---------------------------------------------------------------------------
int ClothSolver::addPanel(const PanelMesh& mesh, const FabricMaterial& mat, const PanelPlacement& place, Real grain,
                          const std::string& name) {
    if (finalized_) FL_THROW(std::logic_error, "ClothSolver: cannot add panels after the simulation started");
    Panel pn;
    pn.name = name;
    pn.material = static_cast<int>(mats_.size());
    mats_.push_back(mat);
    pn.firstVertex = static_cast<int>(x_.size());
    pn.vertexCount = static_cast<int>(mesh.verts.size());
    pn.firstTri = static_cast<int>(tris_.size());
    pn.triCount = static_cast<int>(mesh.tris.size());
    const int pid = static_cast<int>(panels_.size());

    for (const Vec2& uv : mesh.verts) {
        x_.push_back(place.apply(uv));
        restUV_.push_back(rotate(uv, -grain));
        vertexPanel_.push_back(pid);
        pinned_.push_back(0);
    }
    for (const auto& t : mesh.tris) {
        tris_.push_back({t[0] + pn.firstVertex, t[1] + pn.firstVertex, t[2] + pn.firstVertex});
        triPanel_.push_back(pid);
    }
    for (const auto& ch : mesh.edgeChains) {
        std::vector<int> g;
        for (int v : ch) g.push_back(v + pn.firstVertex);
        pn.chains.push_back(std::move(g));
    }
    panels_.push_back(std::move(pn));
    return pid;
}

void ClothSolver::addSeam(int pa, int ea, int pb, int eb, bool reverse) {
    if (finalized_) FL_THROW(std::logic_error, "ClothSolver: cannot add seams after the simulation started");
    if (pa < 0 || pb < 0 || pa >= panelCount() || pb >= panelCount()) FL_THROW(std::out_of_range, "addSeam: bad panel");
    if (ea < 0 || eb < 0 || ea >= static_cast<int>(panels_[pa].chains.size()) || eb >= static_cast<int>(panels_[pb].chains.size()))
        FL_THROW(std::out_of_range, "addSeam: bad edge");
    seamSpecs_.push_back({pa, ea, pb, eb, reverse});
}

void ClothSolver::pinVertex(int v, bool pinned) {
    pinned_.at(static_cast<size_t>(v)) = pinned ? 1 : 0;
    if (finalized_) w_[v] = pinned ? 0 : 1 / mass_[v];
}

void ClothSolver::setVertexPosition(int v, const Vec3& p) {
    x_.at(static_cast<size_t>(v)) = p;
    if (finalized_) {
        xPrev_[v] = p;
        v_[v] = Vec3(0, 0, 0);
    }
}

void ClothSolver::finalize() {
    const int N = vertexCount();
    xPrev_ = x_;
    v_.assign(N, Vec3(0, 0, 0));
    mass_.assign(N, 0);
    w_.assign(N, 0);
    damp_.assign(N, 0);
    muS_.assign(N, 0);
    muK_.assign(N, 0);
    radius_.assign(N, 0);

    // --- triangles: stretch/shear constraints and masses
    triC_.clear();
    Real edgeSum = 0;
    int edgeCnt = 0;
    std::vector<Real> triArea(tris_.size(), 0);
    for (size_t t = 0; t < tris_.size(); ++t) {
        const auto& tr = tris_[t];
        const FabricMaterial& m = mats_[panels_[triPanel_[t]].material];
        const Vec2 &u0 = restUV_[tr[0]], &u1 = restUV_[tr[1]], &u2 = restUV_[tr[2]];
        const Real du1 = u1.x - u0.x, dv1 = u1.y - u0.y, du2 = u2.x - u0.x, dv2 = u2.y - u0.y;
        const Real det = du1 * dv2 - du2 * dv1;
        const Real area = 0.5 * std::fabs(det);
        triArea[t] = area;
        if (area < 1e-14) continue;  // degenerate sliver: skip constraints, keep mass
        TriC c;
        c.i0 = tr[0]; c.i1 = tr[1]; c.i2 = tr[2];
        c.a = dv2 / det;   // inv(Dm)(0,0)
        c.b = -du2 / det;  // inv(Dm)(0,1)
        c.c = -dv1 / det;  // inv(Dm)(1,0)
        c.d = du1 / det;   // inv(Dm)(1,1)
        // energy density 0.5*k*C^2 per unit rest area  ->  compliance 1/(k*A)
        auto cap = [&](Real k) { return p_.stretchStiffnessCap > 0 ? std::min(k, p_.stretchStiffnessCap) : k; };
        c.alphaU = 1.0 / (cap(m.tensileWarp) * area);
        c.alphaV = 1.0 / (cap(m.tensileWeft) * area);
        c.alphaS = 1.0 / (cap(m.shear) * area);
        c.comp = std::max(m.compressionRatio, 0.02);
        c.lamU = c.lamV = c.lamS = 0;
        triC_.push_back(c);
        for (int k = 0; k < 3; ++k) {
            mass_[tr[k]] += m.density * area / 3.0;
            edgeSum += length(restUV_[tr[k]] - restUV_[tr[(k + 1) % 3]]);
            ++edgeCnt;
        }
    }
    for (int i = 0; i < N; ++i) {
        const FabricMaterial& m = mats_[panels_[vertexPanel_[i]].material];
        if (mass_[i] <= 0) mass_[i] = 1e-6;  // isolated vertex guard
        w_[i] = pinned_[i] ? 0 : 1.0 / mass_[i];
        damp_[i] = m.damping;
        muS_[i] = m.frictionStatic;
        muK_[i] = m.frictionKinetic;
        radius_[i] = p_.contactOffset + 0.5 * m.thickness;
    }
    const Real meanEdge = edgeCnt ? edgeSum / edgeCnt : 0.02;

    // --- hinges for bending (only within a panel: vertices are not shared across panels)
    struct Side { int tri, opp; };
    std::map<std::pair<int, int>, std::vector<Side>> edgeMap;
    for (size_t t = 0; t < tris_.size(); ++t)
        for (int k = 0; k < 3; ++k) {
            int a = tris_[t][k], b = tris_[t][(k + 1) % 3];
            const int opp = tris_[t][(k + 2) % 3];
            if (a > b) std::swap(a, b);
            edgeMap[{a, b}].push_back({static_cast<int>(t), opp});
        }
    bendC_.clear();
    for (const auto& kv : edgeMap) {
        if (kv.second.size() != 2) continue;
        const int p1 = kv.first.first, p2 = kv.first.second;
        const Side &s1 = kv.second[0], &s2 = kv.second[1];
        const FabricMaterial& m = mats_[panels_[triPanel_[s1.tri]].material];
        const Real A1 = triArea[s1.tri], A2 = triArea[s2.tri];
        const Vec2 e = restUV_[p2] - restUV_[p1];
        const Real l = length(e);
        if (l < 1e-9 || A1 < 1e-14 || A2 < 1e-14) continue;
        const Real phi = std::atan2(e.y, e.x);  // hinge direction in grain coordinates
        // A hinge measures curvature along its in-plane normal, i.e. at angle psi = phi + 90 deg to the
        // warp. The relevant rigidity is the directional flexural rigidity of an orthotropic sheet,
        // obtained by compliance mixing:  1/B(psi) = cos^4/Bu + sin^4/Bv + 2 sin^2 cos^2 / sqrt(Bu Bv).
        // (Hinge along the warp -> curvature across the weft -> Bv, and vice versa.)
        const Real cp2 = std::sin(phi) * std::sin(phi), sp2 = std::cos(phi) * std::cos(phi);  // cos^2(psi), sin^2(psi)
        const Real Beff = 1.0 / (cp2 * cp2 / m.bendWarp + sp2 * sp2 / m.bendWeft + 2 * cp2 * sp2 / std::sqrt(m.bendWarp * m.bendWeft));
        const Real Ae = (A1 + A2) / 3.0;
        BendC b;
        b.i[0] = p1; b.i[1] = p2; b.i[2] = s1.opp; b.i[3] = s2.opp;
        // Hinge energy E = 0.5 * Bh * kappa^2 * Ae with kappa = theta / d, d = 2*Ae/l
        //   ->  hinge stiffness Bh*l^2/(4*Ae)  ->  compliance 4*Ae/(Bh*l^2).
        // The hinge-sum over a triangle mesh only sees a fraction of a cylindrical
        // curvature (3/8 on equilateral meshes: <cos^4> over three edge directions), so
        // Bh = B * 8/3 reproduces the strip-bending rigidity B that textile tests report.
        b.alpha = 4.0 * Ae / (kBendMeshFactor * Beff * l * l);
        b.lam = 0;
        bendC_.push_back(b);
    }

    // --- seams
    std::vector<std::vector<int>> vertTris(N);
    for (size_t t = 0; t < tris_.size(); ++t)
        for (int k = 0; k < 3; ++k) vertTris[tris_[t][k]].push_back(static_cast<int>(t));

    auto arcParams = [&](const std::vector<int>& ch, Real& total) {
        std::vector<Real> s(ch.size(), 0);
        for (size_t k = 1; k < ch.size(); ++k) s[k] = s[k - 1] + length(restUV_[ch[k]] - restUV_[ch[k - 1]]);
        total = s.empty() ? 0 : s.back();
        if (total > 0) for (auto& v : s) v /= total;
        return s;
    };
    seamC_.clear();
    noCollide_.clear();
    auto link = [&](const std::vector<int>& from, const std::vector<int>& to) {
        Real Lf = 0, Lt = 0;
        const std::vector<Real> sf = arcParams(from, Lf), st = arcParams(to, Lt);
        if (from.size() < 2 || to.size() < 2 || Lf <= 0 || Lt <= 0) return;
        for (size_t i = 0; i < from.size(); ++i) {
            const Real s = sf[i];
            size_t j = 0;
            while (j + 2 < to.size() && st[j + 1] < s) ++j;
            const Real span = st[j + 1] - st[j];
            const Real t = span > 1e-12 ? std::min(std::max((s - st[j]) / span, Real(0)), Real(1)) : 0;
            const Real prev = i > 0 ? sf[i - 1] : sf[i];
            const Real next = i + 1 < from.size() ? sf[i + 1] : sf[i];
            const Real share = 0.5 * (next - prev) * Lf;  // seam length this vertex is responsible for
            SeamC c;
            c.a = from[i]; c.b0 = to[j]; c.b1 = to[j + 1]; c.t = t;
            c.alpha = 1.0 / (p_.seamStiffness * std::max(share, 1e-6));
            c.lam = 0;
            seamC_.push_back(c);
            // seamed vertices must not collide with the triangles they are sewn to
            for (int tt : vertTris[c.b0]) noCollide_.insert(pairKey(c.a, tt));
            for (int tt : vertTris[c.b1]) noCollide_.insert(pairKey(c.a, tt));
            for (int tt : vertTris[c.a]) { noCollide_.insert(pairKey(c.b0, tt)); noCollide_.insert(pairKey(c.b1, tt)); }
        }
    };
    for (const SeamSpec& sp : seamSpecs_) {
        std::vector<int> A = panels_[sp.panelA].chains[sp.edgeA];
        std::vector<int> B = panels_[sp.panelB].chains[sp.edgeB];
        if (sp.reverse) std::reverse(B.begin(), B.end());
        link(A, B);
        link(B, A);
    }

    // --- self-collision hash
    hash_.cell = std::max(meanEdge, 2 * p_.selfThickness);
    hash_.inv = 1.0 / hash_.cell;
    size_t table = 1024;
    while (table < 2 * tris_.size()) table <<= 1;
    hash_.table = table;
    hash_.start.assign(table + 1, 0);
    hash_.stamp.assign(tris_.size(), 0);
    hash_.stampId = 0;

    buildBlockPattern();
    finalized_ = true;
}

// ---------------------------------------------------------------------------
void ClothSolver::solveTriangles(Real invH2) {
    for (TriC& c : triC_) {
        Vec3 *x0 = &x_[c.i0], *x1 = &x_[c.i1], *x2 = &x_[c.i2];
        const Real w[3] = {w_[c.i0], w_[c.i1], w_[c.i2]};
        if (w[0] + w[1] + w[2] == 0) continue;
        Vec3* xs[3] = {x0, x1, x2};

        // warp stretch (and compression, with reduced stiffness)
        {
            const Vec3 d1 = *x1 - *x0, d2 = *x2 - *x0;
            const Vec3 wu = d1 * c.a + d2 * c.c;
            const Real lu = length(wu);
            if (lu > 1e-12) {
                const Vec3 nu = wu / lu;
                const Vec3 g[3] = {-(nu * c.a + nu * c.c), nu * c.a, nu * c.c};
                const Real C = lu - 1;
                const Real alpha = (C < 0 ? c.alphaU / c.comp : c.alphaU) * invH2;
                xpbdSolve(3, xs, w, g, C, alpha, c.lamU);
            }
        }
        // weft stretch
        {
            const Vec3 d1 = *x1 - *x0, d2 = *x2 - *x0;
            const Vec3 wv = d1 * c.b + d2 * c.d;
            const Real lv = length(wv);
            if (lv > 1e-12) {
                const Vec3 nv = wv / lv;
                const Vec3 g[3] = {-(nv * c.b + nv * c.d), nv * c.b, nv * c.d};
                const Real C = lv - 1;
                const Real alpha = (C < 0 ? c.alphaV / c.comp : c.alphaV) * invH2;
                xpbdSolve(3, xs, w, g, C, alpha, c.lamV);
            }
        }
        // in-plane shear: C = wu . wv  (~ sin of the shear angle for unit-length yarns)
        {
            const Vec3 d1 = *x1 - *x0, d2 = *x2 - *x0;
            const Vec3 wu = d1 * c.a + d2 * c.c;
            const Vec3 wv = d1 * c.b + d2 * c.d;
            const Vec3 g1 = wv * c.a + wu * c.b;
            const Vec3 g2 = wv * c.c + wu * c.d;
            const Vec3 g[3] = {-(g1 + g2), g1, g2};
            xpbdSolve(3, xs, w, g, dot(wu, wv), c.alphaS * invH2, c.lamS);
        }
    }
}

void ClothSolver::solveBends(Real invH2) {
    for (BendC& b : bendC_) {
        Vec3* xs[4] = {&x_[b.i[0]], &x_[b.i[1]], &x_[b.i[2]], &x_[b.i[3]]};
        const Real w[4] = {w_[b.i[0]], w_[b.i[1]], w_[b.i[2]], w_[b.i[3]]};
        if (w[0] + w[1] + w[2] + w[3] == 0) continue;
        Vec3 g[4];
        const Real theta = detail::hingeAngle(*xs[0], *xs[1], *xs[2], *xs[3], g);
        if (theta < 0) continue;
        xpbdSolve(4, xs, w, g, theta, b.alpha * invH2, b.lam);
    }
}

void ClothSolver::solveSeams(Real invH2, Real scale) {
    for (SeamC& s : seamC_) {
        Vec3* xs[3] = {&x_[s.a], &x_[s.b0], &x_[s.b1]};
        const Real w[3] = {w_[s.a], w_[s.b0], w_[s.b1]};
        if (w[0] + w[1] + w[2] == 0) continue;
        const Vec3 target = *xs[1] * (1 - s.t) + *xs[2] * s.t;
        const Vec3 d = *xs[0] - target;
        const Real l = length(d);
        if (l < 1e-12) continue;
        const Vec3 n = d / l;
        const Vec3 g[3] = {n, -n * (1 - s.t), -n * s.t};
        xpbdSolve(3, xs, w, g, l, s.alpha / scale * invH2, s.lam);
    }
}

void ClothSolver::solveBody() {
    if (!collider_) return;
    const int N = vertexCount();
    for (int i = 0; i < N; ++i) {
        if (w_[i] == 0) continue;
        Vec3 n;
        const Real phi = collider_->eval(x_[i], n) - radius_[i];
        if (phi >= 0) continue;
        x_[i] -= n * phi;  // push out to the surface
        const Real pen = -phi;
        const Vec3 disp = x_[i] - xPrev_[i];
        const Vec3 tang = disp - n * dot(disp, n);
        const Real dl = length(tang);
        if (dl > 1e-14) {
            if (dl < muS_[i] * pen) x_[i] -= tang;                                  // static friction: stick
            else x_[i] -= tang * std::min(muK_[i] * pen / dl, Real(1));              // kinetic friction
        }
    }
}

void ClothSolver::buildHash() {
    const Real pad = p_.selfThickness;
    std::fill(hash_.start.begin(), hash_.start.end(), 0);
    const size_t mask = hash_.table - 1;
    auto forCells = [&](size_t t, auto&& fn) {
        const auto& tr = tris_[t];
        Vec3 lo = x_[tr[0]], hi = lo;
        for (int k = 1; k < 3; ++k) {
            const Vec3& p = x_[tr[k]];
            lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
            hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
        }
        const int x0 = static_cast<int>(std::floor((lo.x - pad) * hash_.inv)), x1 = static_cast<int>(std::floor((hi.x + pad) * hash_.inv));
        const int y0 = static_cast<int>(std::floor((lo.y - pad) * hash_.inv)), y1 = static_cast<int>(std::floor((hi.y + pad) * hash_.inv));
        const int z0 = static_cast<int>(std::floor((lo.z - pad) * hash_.inv)), z1 = static_cast<int>(std::floor((hi.z + pad) * hash_.inv));
        if (static_cast<long long>(x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1) > 4096) return;  // diverged triangle, ignore
        for (int ix = x0; ix <= x1; ++ix)
            for (int iy = y0; iy <= y1; ++iy)
                for (int iz = z0; iz <= z1; ++iz) fn(hashCell(ix, iy, iz) & mask);
    };
    for (size_t t = 0; t < tris_.size(); ++t) forCells(t, [&](size_t k) { hash_.start[k + 1]++; });
    for (size_t k = 0; k < hash_.table; ++k) hash_.start[k + 1] += hash_.start[k];
    hash_.items.assign(hash_.start[hash_.table], 0);
    std::vector<int> fill(hash_.start.begin(), hash_.start.end() - 1);
    for (size_t t = 0; t < tris_.size(); ++t) forCells(t, [&](size_t k) { hash_.items[fill[k]++] = static_cast<int>(t); });
}

void ClothSolver::solveSelfCollision() {
    if (tris_.empty()) return;
    buildHash();
    const Real th = p_.selfThickness;
    const size_t mask = hash_.table - 1;
    const int N = vertexCount();
    for (int i = 0; i < N; ++i) {
        const Vec3 pi = x_[i];
        const size_t key = hashCell(static_cast<int>(std::floor(pi.x * hash_.inv)), static_cast<int>(std::floor(pi.y * hash_.inv)),
                                    static_cast<int>(std::floor(pi.z * hash_.inv))) & mask;
        ++hash_.stampId;
        for (int e = hash_.start[key]; e < hash_.start[key + 1]; ++e) {
            const int t = hash_.items[e];
            if (hash_.stamp[t] == hash_.stampId) continue;
            hash_.stamp[t] = hash_.stampId;
            const auto& tr = tris_[t];
            if (tr[0] == i || tr[1] == i || tr[2] == i) continue;
            const Vec3 &a = x_[tr[0]], &b = x_[tr[1]], &c = x_[tr[2]];
            const Closest cp = closestOnTriangle(pi, a, b, c);
            const Vec3 diff = pi - cp.q;
            const Real dist = length(diff);
            if (dist >= th) continue;
            const Real wp = w_[i], wa = w_[tr[0]], wb = w_[tr[1]], wc = w_[tr[2]];
            const Real denom = wp + cp.u * cp.u * wa + cp.v * cp.v * wb + cp.w * cp.w * wc;
            if (denom <= 0) continue;
            if (noCollide_.count(pairKey(i, t))) continue;

            Vec3 n;
            Real amount;
            const bool interior = cp.u > 1e-6 && cp.v > 1e-6 && cp.w > 1e-6;
            if (interior) {
                n = normalized(cross(b - a, c - a));
                if (length2(n) == 0) continue;
                const Real s = dot(diff, n);
                // Which side was the vertex on before this substep? Keep it there.
                const Vec3 &pa = xPrev_[tr[0]], &pb = xPrev_[tr[1]], &pc = xPrev_[tr[2]];
                const Vec3 np = normalized(cross(pb - pa, pc - pa));
                const Vec3 qp = pa * cp.u + pb * cp.v + pc * cp.w;
                const Real side = dot(xPrev_[i] - qp, np) >= 0 ? 1 : -1;
                if (side * s >= th) continue;
                amount = side * th - s;
            } else {
                if (dist < 1e-12) continue;
                n = diff / dist;
                amount = th - dist;
            }
            const Real lam = amount / denom;
            x_[i] += n * (wp * lam);
            x_[tr[0]] -= n * (cp.u * wa * lam);
            x_[tr[1]] -= n * (cp.v * wb * lam);
            x_[tr[2]] -= n * (cp.w * wc * lam);
        }
    }
}

void ClothSolver::step() {
    if (!finalized_) finalize();
    if (p_.integrator == Integrator::Implicit) stepImplicit(); else stepXpbd();
}

void ClothSolver::stepXpbd() {
    const int S = std::max(1, p_.substeps);
    const Real h = p_.dt / S;
    const Real invH2 = 1.0 / (h * h);
    const int N = vertexCount();
    const Real seamScale = p_.seamRampFrames > 0 ? std::min(Real(1), std::max(Real(0.02), (frame_ + 1.0) / p_.seamRampFrames)) : Real(1);

    for (int s = 0; s < S; ++s) {
        for (int i = 0; i < N; ++i) {
            xPrev_[i] = x_[i];
            if (w_[i] == 0) { v_[i] = Vec3(0, 0, 0); continue; }
            v_[i] += p_.gravity * h;
            x_[i] += v_[i] * h;
        }
        for (auto& c : triC_) c.lamU = c.lamV = c.lamS = 0;
        for (auto& b : bendC_) b.lam = 0;
        for (auto& sm : seamC_) sm.lam = 0;

        for (int it = 0; it < p_.iterations; ++it) {
            solveTriangles(invH2);
            solveBends(invH2);
            solveSeams(invH2, seamScale);
            solveBody();
        }
        if (p_.selfCollision) {
            solveSelfCollision();
            solveBody();
        }
        for (int i = 0; i < N; ++i) {
            if (w_[i] == 0) continue;
            v_[i] = (x_[i] - xPrev_[i]) / h * std::exp(-damp_[i] * h);
            const Real sp = length(v_[i]);
            if (sp > 50.0) v_[i] *= 50.0 / sp;  // runaway guard
        }
    }
    ++frame_;
}

// ---------------------------------------------------------------------------
Real ClothSolver::kineticEnergy() const {
    Real e = 0;
    for (size_t i = 0; i < v_.size(); ++i) e += 0.5 * mass_[i] * length2(v_[i]);
    return e;
}

Real ClothSolver::maxSeamGap() const {
    Real g = 0;
    for (const SeamC& s : seamC_) {
        const Vec3 t = x_[s.b0] * (1 - s.t) + x_[s.b1] * s.t;
        g = std::max(g, length(x_[s.a] - t));
    }
    return g;
}

Real ClothSolver::minBodyClearance() const {
    if (!collider_) return 1e30;
    Real m = 1e30;
    Vec3 n;
    for (const Vec3& p : x_) m = std::min(m, collider_->eval(p, n));
    return m;
}

Real ClothSolver::totalMass() const {
    Real m = 0;
    for (Real v : mass_) m += v;
    return m;
}

}  // namespace fl
