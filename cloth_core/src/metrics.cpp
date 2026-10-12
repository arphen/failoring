#include "failoring/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace fl {

Real projectedShadowArea(const std::vector<Vec3>& x, const std::vector<std::array<int, 3>>& tris, Real extent, int res) {
    std::vector<char> covered(static_cast<size_t>(res) * res, 0);
    const Real cell = 2 * extent / res;
    auto toGrid = [&](Real v) { return (v + extent) / cell; };
    for (const auto& t : tris) {
        const Vec3 &a = x[t[0]], &b = x[t[1]], &c = x[t[2]];
        const Real ax = toGrid(a.x), az = toGrid(a.z), bx = toGrid(b.x), bz = toGrid(b.z), cx = toGrid(c.x), cz = toGrid(c.z);
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({ax, bx, cx})))), x1 = std::min(res - 1, static_cast<int>(std::ceil(std::max({ax, bx, cx}))));
        const int z0 = std::max(0, static_cast<int>(std::floor(std::min({az, bz, cz})))), z1 = std::min(res - 1, static_cast<int>(std::ceil(std::max({az, bz, cz}))));
        const Real det = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
        if (std::fabs(det) < 1e-12) continue;
        for (int iz = z0; iz <= z1; ++iz)
            for (int ix = x0; ix <= x1; ++ix) {
                const Real px = ix + 0.5, pz = iz + 0.5;
                const Real l1 = ((bz - cz) * (px - cx) + (cx - bx) * (pz - cz)) / det;
                const Real l2 = ((cz - az) * (px - cx) + (ax - cx) * (pz - cz)) / det;
                const Real l3 = 1 - l1 - l2;
                if (l1 >= 0 && l2 >= 0 && l3 >= 0) covered[static_cast<size_t>(iz) * res + ix] = 1;
            }
    }
    size_t n = 0;
    for (char c : covered) n += c;
    return static_cast<Real>(n) * cell * cell;
}

bool writeObj(const std::string& path, const std::vector<Vec3>& x, const std::vector<std::array<int, 3>>& tris) {
    std::ofstream out(path);
    if (!out) return false;
    out << "# Failoring cloth export\n";
    for (const Vec3& p : x) out << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
    for (const auto& t : tris) out << "f " << t[0] + 1 << ' ' << t[1] + 1 << ' ' << t[2] + 1 << '\n';
    return static_cast<bool>(out);
}

}  // namespace fl
