// K4 measurement: native cost of the full SDF bake on the real mannequin walking (not in ctest).
// usage: k4_measure <asset dir with body.obj and walk.bin> [frames]
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

#include "failoring/sdf_grid.hpp"
#include "k4_assets.hpp"

using namespace fl;

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "godot/assets/average";
    std::vector<Vec3> V;
    k4::Tris T;
    std::vector<std::vector<Vec3>> F;
    if (!k4::loadObj(dir + "/body.obj", V, T) || !k4::loadWalk(dir + "/walk.bin", V, F)) { std::printf("assets missing in %s\n", dir.c_str()); return 2; }
    const int nf = argc > 2 ? std::atoi(argv[2]) : static_cast<int>(F.size());
    std::printf("mesh %zu verts %zu tris, %zu walk frames\n", V.size(), T.size(), F.size());
    std::printf("frame  ms_total  seed  prop  sign  bricks_ms  fine  coarse  bricks  evals  maxdisp_mm  meandisp_mm\n");
    double sum = 0;
    for (int f = 0; f < nf; ++f) {
        const auto& P = F[static_cast<size_t>(f) % F.size()];
        SdfGrid g;
        const std::clock_t c0 = std::clock();
        g.bake(P, T);
        const double cpu = 1e3 * double(std::clock() - c0) / CLOCKS_PER_SEC;
        const SdfGridStats& s = g.stats();
        double mx = 0, mean = 0;
        if (f > 0) {
            const auto& Q = F[static_cast<size_t>(f - 1) % F.size()];
            for (size_t i = 0; i < P.size(); ++i) { const double d = length(P[i] - Q[i]); mx = std::max(mx, d); mean += d; }
            mean /= static_cast<double>(P.size());
        }
        sum += cpu;
        std::printf("%3d cpu %7.1f wall %8.1f %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f %6d %9lld %6.2f %6.2f\n", f, cpu, s.msTotal, s.msSeed, s.msPropagate, s.msSign, s.msBricks, s.msFine, s.msCoarse, s.bricks,
                    s.distanceEvals, mx * 1e3, mean * 1e3);
    }
    std::printf("mean bake CPU %.1f ms over %d frames\n", sum / nf, nf);
    return 0;
}
