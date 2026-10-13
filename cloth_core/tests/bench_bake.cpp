// Timing and checksum of the body SDF bake on a real body mesh (the editor bakes one per walking frame).
//   build/bench_bake godot/assets/average/body.obj [repeats]
// Prints the median wall time of SdfGrid::bake with the editor's parameters (h = 7 mm), the level breakdown from
// SdfGridStats, and a checksum of the baked field so that optimisations can be checked to leave the numbers alone.
// Not part of ctest (FL_BENCH=ON).
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "failoring/sdf_grid.hpp"

using namespace fl;

namespace {

double nowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// Minimal OBJ reader: "v x y z" and "f a b c ..." (fan-triangulated; indices may carry /texture/normal parts).
bool readObj(const std::string& path, std::vector<Vec3>& V, std::vector<std::array<int, 3>>& T) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v") {
            double x, y, z;
            ss >> x >> y >> z;
            V.push_back(Vec3(x, y, z));
        } else if (tag == "f") {
            std::vector<int> idx;
            std::string tok;
            while (ss >> tok) idx.push_back(std::atoi(tok.c_str()) - 1);
            for (size_t k = 1; k + 1 < idx.size(); ++k) T.push_back({idx[0], idx[k], idx[k + 1]});
        }
    }
    return !V.empty() && !T.empty();
}

// Deterministic probe points, the same as bench_native's checksum.
double gridChecksum(const SdfGrid& g, const Vec3& lo, const Vec3& hi) {
    double s = 0;
    const Vec3 c = (lo + hi) * 0.5, e = (hi - lo) * 0.5;
    for (int i = 0; i < 4000; ++i) {
        const Real u = std::fmod(i * 0.6180339887, 1.0), v = std::fmod(i * 0.3247179572, 1.0), w = std::fmod(i * 0.7548776662, 1.0);
        const Vec3 p = c + Vec3((2 * u - 1) * e.x, (2 * v - 1) * e.y, (2 * w - 1) * e.z);
        Vec3 gr;
        s += g.eval(p, &gr) + 1e-3 * (gr.x + 2 * gr.y + 3 * gr.z);
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: bench_bake body.obj [repeats]\n");
        return 2;
    }
    std::vector<Vec3> V;
    std::vector<std::array<int, 3>> T;
    if (!readObj(argv[1], V, T)) {
        std::fprintf(stderr, "bench_bake: cannot read %s\n", argv[1]);
        return 1;
    }
    const int repeats = argc > 2 ? std::atoi(argv[2]) : 7;
    Vec3 lo = V[0], hi = V[0];
    for (const Vec3& v : V) {
        lo = Vec3(std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z));
        hi = Vec3(std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z));
    }
    SdfGridParams p;  // defaults: h = 7 mm, as PlanLoader.build passes
    p.h = argc > 3 ? std::atof(argv[3]) * 1e-3 : 0.007;
    if (argc > 3) {  // accuracy of a coarser grid against the 7 mm reference, at points within 3 cm of the surface
        SdfGrid ref, coarse;
        SdfGridParams r;
        r.h = 0.007;
        ref.bake(V, T, r);
        coarse.bake(V, T, p);
        for (const double band : {0.006, 0.012}) {
            double sum2 = 0, worst = 0, sumAbs = 0;
            int n = 0;
            for (int i = 0; i < 40000; ++i) {
                const Real u = std::fmod(i * 0.6180339887, 1.0), v = std::fmod(i * 0.3247179572, 1.0), w = std::fmod(i * 0.7548776662, 1.0);
                const Vec3 q = lo - Vec3(0.03, 0.03, 0.03) + Vec3(u * (hi.x - lo.x + 0.06), v * (hi.y - lo.y + 0.06), w * (hi.z - lo.z + 0.06));
                const double dr = ref.eval(q, nullptr);
                if (std::fabs(dr) > band) continue;
                const double e = coarse.eval(q, nullptr) - dr;
                sum2 += e * e;
                sumAbs += std::fabs(e);
                worst = std::max(worst, std::fabs(e));
                ++n;
            }
            std::printf("accuracy h %.1f mm vs 7 mm, points within %.0f mm of the surface: %d  rms %.3f mm  mean |e| %.3f mm  max %.3f mm\n",
                        p.h * 1000, band * 1000, n, 1000 * std::sqrt(sum2 / std::max(1, n)), 1000 * sumAbs / std::max(1, n), 1000 * worst);
        }
    }
    std::vector<double> wall, cpu, setup, fine, coarse;
    double sum = 0;
    SdfGridStats st;
    for (int r = 0; r < repeats; ++r) {
        SdfGrid g;
        const double t0 = nowMs(), c0 = std::clock();
        g.bake(V, T, p);
        wall.push_back(nowMs() - t0);
        cpu.push_back(1000.0 * (std::clock() - c0) / CLOCKS_PER_SEC);
        st = g.stats();
        setup.push_back(st.msSetup);
        fine.push_back(st.msFine);
        coarse.push_back(st.msCoarse);
        if (r == 0) sum = gridChecksum(g, lo, hi);
    }
    std::printf("bake   verts %6zu tris %6zu h %.1f mm  median %7.2f ms wall, %7.2f ms cpu (setup %6.2f, fine %7.2f, coarse %6.2f)  bricks %d  fine %d  coarse %d  distance evals %lld  MB %.2f  checksum %.12e\n",
                V.size(), T.size(), p.h * 1000, median(wall), median(cpu), median(setup), median(fine), median(coarse), st.bricks, st.fineSamples, st.coarseSamples,
                st.distanceEvals, st.bytes / 1e6, sum);
    return 0;
}
