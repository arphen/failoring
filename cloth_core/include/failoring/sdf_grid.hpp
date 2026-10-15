#pragma once
// Signed distance to a closed triangle mesh (a body scan, the parametric mannequin, any collider mesh), baked
// into a two-level grid and evaluated with a quadratic B-spline:
//
//   fine level    spacing h, sparse 2x2x2 bricks covering a band of `band` + stencil reach around the surface;
//                 samples hold exact signed distances (closest-triangle propagation from a seeded band).
//   coarse level  spacing coarseRatio*h, dense over the mesh bounding box + margin; same construction.
//
// Queries use the fine level wherever all 27 stencil samples exist (always within `band` of the surface) and the
// coarse level elsewhere (deep inside, far outside). Value and gradient are continuous (C1) within each level; the
// switch between levels happens only farther than `band` from the surface, where the cloth solver only looks at
// the sign. The sign comes from winding numbers (signed ray crossings), which stay correct where the mesh
// overlaps itself (the thighs of a full figure); the magnitude is the distance to the nearest triangle, so a
// buried surface inside such an overlap shows up as a shallow negative valley, never as a false surface.
// The B-spline smooths the field by about h^2/8 times its Laplacian (the mean curvature near the surface), and it
// smooths the faceting of the mesh: on the mannequin (h = 7 mm) the collider surface is within 0.15 mm (median) /
// 0.7 mm (95th percentile) of the polygons on trunk, arms and legs, 0.5-0.9 mm / 2 mm on hands, feet and face.
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "failoring/collider.hpp"
#include "failoring/math.hpp"
#include "failoring/skin.hpp"

namespace fl {

struct SdfGridParams {
    Real h = 0.007;       // fine sample spacing (m)
    Real band = 0.006;    // the fine level is valid for every point within this distance of the surface (m)
    int coarseRatio = 6;  // coarse spacing = coarseRatio * h
    Real margin = 0.08;   // the coarse grid extends this far beyond the mesh bounding box (m)
    Real seed = 1.0;      // exact distances are seeded within seed * spacing of every triangle
    bool refine = true;   // near the surface, walk to closer edge-adjacent triangles (exact when the mesh is finer than the grid)
    // Skin buried inside another body part (an arm overlapping the torso) is left out of the distances (it still
    // counts for inside/outside): a triangle is buried when points buriedOffset outside it are inside the body.
    bool dropBuried = true;
    Real buriedOffset = 0.001;
};

struct SdfGridStats {
    int triangles = 0, bricks = 0, fineSamples = 0, coarseSamples = 0;
    long long distanceEvals = 0;
    int buried = 0;  // triangles left out of the distances (dropBuried)
    double msSetup = 0, msFine = 0, msCoarse = 0, msTotal = 0;
    double bytes = 0;  // memory of the baked grid (values + brick table)
};

class SdfGrid {
public:
    // Bakes the grid. Triangles must be consistently oriented with outward normals (counter-clockwise seen from
    // outside). Returns false for an empty mesh.
    bool bake(const std::vector<Vec3>& verts, const std::vector<std::array<int, 3>>& tris, const SdfGridParams& p = {});
    // Signed distance (negative inside); writes the gradient (unit length near the surface) when grad != nullptr.
    Real eval(const Vec3& p, Vec3* grad) const;
    bool empty() const { return coarse_.empty(); }
    const SdfGridStats& stats() const { return stats_; }
    const SdfGridParams& params() const { return p_; }
    // Positions and values of all fine samples (tests and diagnostics).
    void fineSamples(std::vector<Vec3>& pos, std::vector<float>& val) const;
    // Which level answered the last queries (diagnostics only; not thread safe).
    mutable long long fineQueries = 0, coarseQueries = 0;

private:
    bool evalFine(const Vec3& p, Real& v, Vec3* grad) const;
    Real evalCoarse(const Vec3& p, Vec3* grad) const;

    SdfGridParams p_;
    SdfGridStats stats_;
    // fine level: sample (i,j,k) at fo_ + fh_*(i,j,k); brick (bi,bj,bk) holds samples 2bi..2bi+1 etc.
    Vec3 fo_;
    Real fh_ = 0, finv_ = 0;
    int bnx_ = 0, bny_ = 0, bnz_ = 0;  // bricks per axis (samples per axis = 2 * bricks)
    std::vector<int> brickOf_;          // brick table, -1 = not allocated
    std::vector<float> fine_;           // 8 samples per allocated brick, x fastest
    std::vector<int> brickXYZ_;         // brick coordinates (3 per allocated brick)
    // coarse level: dense
    Vec3 co_;
    Real ch_ = 0, cinv_ = 0;
    int cnx_ = 0, cny_ = 0, cnz_ = 0;
    std::vector<float> coarse_;
};

// Collider adaptor: the cloth solver's interface (signed distance + outward unit normal).
class GridCollider : public Collider {
public:
    explicit GridCollider(std::shared_ptr<const SdfGrid> g) : g_(std::move(g)) {}
    Real eval(const Vec3& p, Vec3& n) const override {
        Vec3 gr;
        const Real d = g_->eval(p, &gr);
        const Real l = length(gr);
        n = l > 1e-9 ? gr / l : Vec3(0, 1, 0);
        return d;
    }
    const SdfGrid& grid() const { return *g_; }

    // K1 compliant skin: per-region stiffness. `map` labels the space near the body surface (see skin.hpp);
    // `scale[label]` is the relative stiffness of that region (1 = SolverParams::skinStiffness, 0 = rigid). Points
    // with an unknown label or a label beyond the table use the collider's default skinScale_.
    void setRegionStiffness(std::shared_ptr<const RegionMap> map, std::vector<Real> scale) {
        regions_ = std::move(map);
        regionScale_ = std::move(scale);
    }
    Real skinScale(const Vec3& p) const override {
        if (regions_) {
            const unsigned l = regions_->label(p);
            if (l < regionScale_.size()) return regionScale_[l];
        }
        return skinScale_;
    }

private:
    std::shared_ptr<const SdfGrid> g_;
    std::shared_ptr<const RegionMap> regions_;
    std::vector<Real> regionScale_;
};

}  // namespace fl
