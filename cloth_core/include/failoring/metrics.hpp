#pragma once
#include <array>
#include <string>
#include <vector>

#include "failoring/math.hpp"

namespace fl {

// Area of the shadow cast on the horizontal (x,z) plane by a triangle mesh,
// rasterised on a res x res grid covering [-extent, extent]^2.
Real projectedShadowArea(const std::vector<Vec3>& x, const std::vector<std::array<int, 3>>& tris, Real extent, int res = 800);

// Wavefront OBJ export (open directly in Blender). Returns false on I/O error.
bool writeObj(const std::string& path, const std::vector<Vec3>& x, const std::vector<std::array<int, 3>>& tris);

}  // namespace fl
