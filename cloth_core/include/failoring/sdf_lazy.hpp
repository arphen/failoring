#pragma once
// K4: a signed-distance collider for a body mesh that is posed again every frame (a walking mannequin), without
// re-baking the whole grid.
//
// SdfGrid::bake re-computes every sample of the fine level (about 51,000 bricks on the mannequin) and of the coarse
// level each time the mesh moves. The cloth only ever looks at the samples around its own vertices. SdfGridLazy keeps
// the SAME sample lattice, the same quadratic B-spline and the same winding-number sign as SdfGrid, but a brick of 2x2x2
// fine samples (and a coarse sample) is computed on first use in a frame, from an exact closest-triangle search, and
// is thrown away when the next pose is set. The cost of a frame is therefore proportional to the volume the cloth
// actually samples, not to the surface of the body, and no margin or "how far can the cloth move" guess is needed:
// a brick the cloth reaches later in the frame is simply computed then. `prefetch` computes the bricks around a set of
// points ahead of time (the garment's vertices), which moves the cost out of the solver's inner loop.
//
// Differences from SdfGrid (documented, tested):
//  * the fine level answers wherever the closest mesh vertex is within `fineRadius` (default 4 cm) instead of "all 27
//    stencil bricks exist" (which is about 4 to 6 cm); beyond that the coarse level answers, as before;
//  * coarse samples are exact up to `coarseCap` (12 cm) from a mesh vertex and saturate at +-coarseCap farther away
//    (the baked grid holds the exact distance there). The solver only looks at the sign that far from the body;
//  * the closest triangle is found with a vertex hash + the triangles around the closest vertices + a walk over
//    edge-adjacent triangles, not by propagation. It is exact for meshes whose triangles are small against the
//    distance to the next sheet (the mannequin); `exactDistance` is the brute-force reference used by the tests.
// eval() mutates caches: not thread safe, like the diagnostics counters of SdfGrid. Not used unless asked for.
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "failoring/collider.hpp"
#include "failoring/math.hpp"
#include "failoring/sdf_grid.hpp"

namespace fl {

struct SdfLazyParams {
    SdfGridParams grid;       // h, coarseRatio, margin as for SdfGrid (band/seed/refine are not used)
    Real fineRadius = 0.04;   // the fine level answers where a mesh vertex is within this distance (m)
    Real coarseCap = 0.12;    // coarse samples are exact up to this distance from a mesh vertex, then saturate (m)
    Real hashCell = 0.02;     // cell size of the vertex hash (m)
    int vertexFan = 2;        // how many closest vertices contribute their triangle fans as starting points
    int warmStart = 0;        // 0: every sample searches alone; 1: inside a brick a sample walks from its neighbour's answer; 2: that answer is one more candidate next to the vertex fans
};

struct SdfLazyStats {
    int triangles = 0;
    double msPrepare = 0;           // last setBody: triangle update, vertex hash, winding rows (both levels)
    double msPose = 0, msHash = 0, msRowsFine = 0, msRowsCoarse = 0;  // parts of msPrepare
    long long fineBricks = 0;       // bricks computed since the last setBody
    long long coarseSamples = 0;    // coarse samples computed since the last setBody
    long long nearestQueries = 0;   // closest-triangle searches
    long long triangleTests = 0;    // point-triangle distance evaluations
    long long fineQueries = 0, coarseQueries = 0;
};

class SdfGridLazy {
public:
    // Sets the pose: `verts` are the vertices of the posed mesh, `tris` the (constant) triangles, consistently
    // oriented with outward normals. The topology tables are rebuilt only when `tris` changes. Invalidates all
    // computed bricks. Returns false for an empty mesh.
    bool setBody(const std::vector<Vec3>& verts, const std::vector<std::array<int, 3>>& tris, const SdfLazyParams& p = {});
    // Same contract as SdfGrid::eval.
    Real eval(const Vec3& p, Vec3* grad) const;
    // Computes the bricks the stencil of each point needs, so that later eval calls only read.
    void prefetch(const Vec3* pts, int n) const;
    bool empty() const { return !ready_; }
    const SdfLazyStats& stats() const { return stats_; }
    const SdfLazyParams& params() const { return p_; }
    // Positions and values of all fine samples computed so far in this pose (diagnostics).
    void computedSamples(std::vector<Vec3>& pos, std::vector<float>& val) const;
    // Exact distance from p to the posed mesh by brute force over all triangles (tests; slow).
    Real exactDistance(const Vec3& p) const;

private:
    struct Tri { Vec3 a, b, c; };
    // winding-number rows, see sdf_grid.cpp
    struct Rows {
        int ny = 0, nz = 0;
        std::vector<int> start;
        std::vector<std::pair<double, int>> cross;
        std::vector<int> suffix;
        void build(const std::vector<Tri>& T, const Vec3& o, Real h, int ny_, int nz_);
        int winding(double x, int j, int k) const;
    };

    double dist2(const Vec3& p, int k) const;
    // closest triangle to p within maxR (by vertices); returns false when no vertex is that close
    bool nearest(const Vec3& p, Real maxR, double& d2, int& tri, int hint = -1) const;
    bool nearestVertex(const Vec3& p, Real maxR, double& d2) const;
    float fineSample(int i, int j, int k, int& hint) const;
    float coarseSample(int i, int j, int k) const;
    const float* ensureBrick(int bi, int bj, int bk) const;
    bool evalFine(const Vec3& p, Real& v, Vec3* grad) const;
    Real evalCoarse(const Vec3& p, Vec3* grad) const;

    SdfLazyParams p_;
    mutable SdfLazyStats stats_;
    bool ready_ = false;

    // topology (constant while `tris` is)
    std::vector<std::array<int, 3>> tris_;
    std::vector<std::array<int, 3>> adj_;
    std::vector<int> vtStart_, vtList_;  // vertex -> triangles (CSR)
    std::vector<unsigned char> used_;    // vertex referenced by a triangle
    // pose
    std::vector<Vec3> V_;
    std::vector<Tri> T_;
    Vec3 hashLo_;
    Real hashInv_ = 0;
    int hx_ = 0, hy_ = 0, hz_ = 0;
    std::vector<int> hStart_, hList_;
    // fine level (same lattice as SdfGrid::bake for this pose)
    Vec3 fo_;
    Real fh_ = 0, finv_ = 0;
    int bnx_ = 0, bny_ = 0, bnz_ = 0;
    Rows fineRows_, coarseRows_;
    mutable std::uint32_t epoch_ = 0;
    mutable std::vector<std::uint32_t> brickStamp_;
    mutable std::vector<int> brickSlot_;
    mutable std::vector<unsigned char> brickNear_;  // 0 unknown (this epoch: stamp), 1 near, 2 far
    mutable std::vector<std::uint32_t> nearStamp_;
    mutable std::vector<float> pool_;
    mutable int poolUsed_ = 0;
    // coarse level
    Vec3 co_;
    Real ch_ = 0, cinv_ = 0;
    int cnx_ = 0, cny_ = 0, cnz_ = 0;
    mutable std::vector<float> coarse_;
    mutable std::vector<std::uint32_t> coarseStamp_;
};

// Collider adaptor, as GridCollider.
class LazyGridCollider : public Collider {
public:
    explicit LazyGridCollider(std::shared_ptr<const SdfGridLazy> g) : g_(std::move(g)) {}
    Real eval(const Vec3& p, Vec3& n) const override {
        Vec3 gr;
        const Real d = g_->eval(p, &gr);
        const Real l = length(gr);
        n = l > 1e-9 ? gr / l : Vec3(0, 1, 0);
        return d;
    }

private:
    std::shared_ptr<const SdfGridLazy> g_;
};

}  // namespace fl
