// K4 diagnostic: error of the lazily computed fine samples (and of the full bake's samples) against the exact
// brute-force distance, on the real mannequin. usage: k4_samples <asset dir> <frame>
#include <cstdio>
#include <string>

#include "k4_assets.hpp"
#include "k4_compare.hpp"

using namespace fl;

static void stats(const char* name, std::vector<float>& e) {
    std::printf("  %-28s n=%6zu median %.4f p95 %.4f p99 %.4f max %.4f mm; >0.3mm: %.2f%%  >1mm: %.2f%%\n", name, e.size(), k4::percentile(e, 0.5), k4::percentile(e, 0.95), k4::percentile(e, 0.99), k4::maxOf(e),
                100.0 * std::count_if(e.begin(), e.end(), [](float x) { return x > 0.3f; }) / e.size(), 100.0 * std::count_if(e.begin(), e.end(), [](float x) { return x > 1.f; }) / e.size());
}

int main(int argc, char** argv) {
    std::vector<Vec3> V;
    k4::Tris T;
    std::vector<std::vector<Vec3>> F;
    if (!k4::loadObj(std::string(argv[1]) + "/body.obj", V, T) || !k4::loadWalk(std::string(argv[1]) + "/walk.bin", V, F)) { std::printf("assets missing\n"); return 2; }
    const auto& P = F[static_cast<size_t>(std::atoi(argv[2])) % F.size()];
    SdfGridLazy lz;
    SdfLazyParams lp;
    if (const char* e = std::getenv("K4_WARM")) lp.warmStart = std::atoi(e) != 0;
    if (const char* e = std::getenv("K4_FAN")) lp.vertexFan = std::atoi(e);
    lz.setBody(P, T, lp);
    const auto nrm = k4::vertexNormals(P, T);
    const auto Q = k4::clothProxy(P, nrm, 3, 0.0015, 0.015);
    lz.prefetch(Q.data(), static_cast<int>(Q.size()));
    std::vector<Vec3> pos;
    std::vector<float> val;
    lz.computedSamples(pos, val);
    SdfGrid ref;
    ref.bake(P, T);
    std::vector<Vec3> rpos;
    std::vector<float> rval;
    ref.fineSamples(rpos, rval);
    std::printf("lazy samples %zu, ref samples %zu\n", pos.size(), rpos.size());
    std::vector<float> eL, eR, eLR;
    const size_t step = std::max<size_t>(1, pos.size() / 3000);
    for (size_t i = 0; i < pos.size(); i += step) {
        const Real ex = lz.exactDistance(pos[i]);
        if (ex > 0.04) continue;
        eL.push_back(static_cast<float>(std::fabs(std::fabs(val[i]) - ex) * 1e3));
    }
    const size_t stepR = std::max<size_t>(1, rpos.size() / 3000);
    for (size_t i = 0; i < rpos.size(); i += stepR) {
        const Real ex = lz.exactDistance(rpos[i]);
        if (ex > 0.04) continue;
        eR.push_back(static_cast<float>(std::fabs(std::fabs(rval[i]) - ex) * 1e3));
    }
    stats("lazy sample vs exact", eL);
    stats("full-bake sample vs exact", eR);
    return 0;
}
