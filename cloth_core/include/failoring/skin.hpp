#pragma once
// K1 compliant skin: region label map.
//
// A RegionMap answers "which body region (trunk, neck, head, arms, hands, legs, feet, ...) is nearest to this
// point?" so the skin stiffness can vary over the body. It is built from the same closed mesh that is baked into
// the SDF grid plus one small integer label per triangle (for MakeHuman-derived meshes: the part label of the
// triangle's vertices; a triangle takes the label of its first vertex or the majority, the caller decides).
//
// Storage: a uniform voxel grid (default 1 cm) over the mesh bounding box + reach; each voxel stores the label of the
// triangle nearest to the voxel centre, for voxels within `reach` of the surface (others: label 255 = unknown, queries
// then use the nearest known neighbour or the default). Resolution of region borders is therefore about one cell.
#include <array>
#include <cstdint>
#include <vector>

#include "failoring/math.hpp"

namespace fl {

class RegionMap {
public:
    // triLabels[t] is the region id (0..254) of triangle t. Returns false for empty input.
    bool bake(const std::vector<Vec3>& verts, const std::vector<std::array<int, 3>>& tris,
              const std::vector<std::uint8_t>& triLabels, Real cell = 0.01, Real reach = 0.05);
    // Region id of the nearest baked surface to p, or 255 when p is farther than `reach` from every triangle.
    std::uint8_t label(const Vec3& p) const;
    bool empty() const { return lab_.empty(); }
    static constexpr std::uint8_t kUnknown = 255;

private:
    Vec3 o_;
    Real h_ = 0, inv_ = 0;
    int nx_ = 0, ny_ = 0, nz_ = 0;
    std::vector<std::uint8_t> lab_;
};

}  // namespace fl
