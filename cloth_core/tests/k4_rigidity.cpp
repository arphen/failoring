// K4 diagnostic: how rigid is the skinned mannequin locally? For every vertex, the best-fit rigid motion (Horn's
// quaternion method) of its rest-pose neighbours within r to their posed positions, and the largest residual.
// A per-part rest-pose SDF (design B) or a warped previous field (design C) can only be as good as this residual.
// usage: k4_rigidity <asset dir> <radius mm> [frame stride]
#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>

#include "k4_assets.hpp"
#include "k4_compare.hpp"

using namespace fl;

// largest eigenvector of the symmetric 4x4 matrix N (Jacobi iterations)
static void topEigen4(double N[4][4], double q[4]) {
    double A[4][4], V[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) A[i][j] = N[i][j];
    for (int sweep = 0; sweep < 30; ++sweep) {
        double off = 0;
        for (int i = 0; i < 4; ++i) for (int j = i + 1; j < 4; ++j) off += A[i][j] * A[i][j];
        if (off < 1e-24) break;
        for (int p = 0; p < 4; ++p)
            for (int r = p + 1; r < 4; ++r) {
                if (std::fabs(A[p][r]) < 1e-300) continue;
                const double th = (A[r][r] - A[p][p]) / (2 * A[p][r]);
                const double t = (th >= 0 ? 1 : -1) / (std::fabs(th) + std::sqrt(th * th + 1));
                const double c = 1 / std::sqrt(t * t + 1), s = t * c;
                for (int k = 0; k < 4; ++k) { const double akp = A[k][p], akr = A[k][r]; A[k][p] = c * akp - s * akr; A[k][r] = s * akp + c * akr; }
                for (int k = 0; k < 4; ++k) { const double apk = A[p][k], ark = A[r][k]; A[p][k] = c * apk - s * ark; A[r][k] = s * apk + c * ark; }
                for (int k = 0; k < 4; ++k) { const double vkp = V[k][p], vkr = V[k][r]; V[k][p] = c * vkp - s * vkr; V[k][r] = s * vkp + c * vkr; }
            }
    }
    int b = 0;
    for (int i = 1; i < 4; ++i) if (A[i][i] > A[b][b]) b = i;
    for (int i = 0; i < 4; ++i) q[i] = V[i][b];
}

static Vec3 rotate(const double q[4], const Vec3& v) {  // q = (w,x,y,z)
    const double w = q[0], x = q[1], y = q[2], z = q[3];
    const Vec3 u(x, y, z);
    return v * (w * w - dot(u, u)) + u * (2 * dot(u, v)) + cross(u, v) * (2 * w);
}

int main(int argc, char** argv) {
    std::vector<Vec3> V;
    k4::Tris T;
    std::vector<std::vector<Vec3>> F;
    if (argc < 3 || !k4::loadObj(std::string(argv[1]) + "/body.obj", V, T) || !k4::loadWalk(std::string(argv[1]) + "/walk.bin", V, F)) { std::printf("usage / assets missing\n"); return 2; }
    const double r = std::atof(argv[2]) * 1e-3;
    const int fstep = argc > 3 ? std::atoi(argv[3]) : 8;
    const double cell = r;
    std::unordered_map<long long, std::vector<int>> hash;
    auto key = [&](int x, int y, int z) { return (static_cast<long long>(x + 1000) * 4000 + (y + 1000)) * 4000 + (z + 1000); };
    for (size_t i = 0; i < V.size(); ++i) hash[key(int(std::floor(V[i].x / cell)), int(std::floor(V[i].y / cell)), int(std::floor(V[i].z / cell)))].push_back(static_cast<int>(i));
    std::vector<std::vector<int>> nb(V.size());
    for (size_t i = 0; i < V.size(); ++i) {
        const int cx = int(std::floor(V[i].x / cell)), cy = int(std::floor(V[i].y / cell)), cz = int(std::floor(V[i].z / cell));
        for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) for (int dz = -1; dz <= 1; ++dz) {
            auto it = hash.find(key(cx + dx, cy + dy, cz + dz));
            if (it == hash.end()) continue;
            for (int j : it->second) if (length(V[static_cast<size_t>(j)] - V[i]) <= r) nb[i].push_back(j);
        }
    }
    std::vector<float> res;
    std::vector<float> resTorso;  // vertices between the hip and shoulder line (y in 0.9..1.45 of 1.68)
    for (size_t f = 0; f < F.size(); f += static_cast<size_t>(fstep)) {
        const auto& P = F[f];
        for (size_t i = 0; i < V.size(); ++i) {
            const auto& N = nb[i];
            Vec3 cr(0, 0, 0), cp(0, 0, 0);
            for (int j : N) { cr += V[static_cast<size_t>(j)]; cp += P[static_cast<size_t>(j)]; }
            cr = cr / double(N.size()); cp = cp / double(N.size());
            double S[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
            for (int j : N) {
                const Vec3 a = V[static_cast<size_t>(j)] - cr, b = P[static_cast<size_t>(j)] - cp;
                const double av[3] = {a.x, a.y, a.z}, bv[3] = {b.x, b.y, b.z};
                for (int u = 0; u < 3; ++u) for (int v = 0; v < 3; ++v) S[u][v] += av[u] * bv[v];
            }
            double Nm[4][4] = {{S[0][0] + S[1][1] + S[2][2], S[1][2] - S[2][1], S[2][0] - S[0][2], S[0][1] - S[1][0]},
                               {S[1][2] - S[2][1], S[0][0] - S[1][1] - S[2][2], S[0][1] + S[1][0], S[2][0] + S[0][2]},
                               {S[2][0] - S[0][2], S[0][1] + S[1][0], -S[0][0] + S[1][1] - S[2][2], S[1][2] + S[2][1]},
                               {S[0][1] - S[1][0], S[2][0] + S[0][2], S[1][2] + S[2][1], -S[0][0] - S[1][1] + S[2][2]}};
            double q[4];
            topEigen4(Nm, q);
            double worst = 0;
            for (int j : N) worst = std::max(worst, length(rotate(q, V[static_cast<size_t>(j)] - cr) + cp - P[static_cast<size_t>(j)]));
            res.push_back(static_cast<float>(worst * 1e3));
            if (V[i].y > 0.9 && V[i].y < 1.45 && std::fabs(V[i].x) < 0.2) resTorso.push_back(static_cast<float>(worst * 1e3));
        }
    }
    auto frac = [](const std::vector<float>& v, float t) { return 100.0 * std::count_if(v.begin(), v.end(), [&](float x) { return x < t; }) / v.size(); };
    std::printf("local rigid fit, neighbours within %.0f mm, every %dth frame of %zu: residual (max over the neighbourhood) in mm\n", r * 1e3, fstep, F.size());
    for (auto* s : {&res, &resTorso}) {
        std::printf("  %-10s n=%zu median %.3f p95 %.3f max %.3f | below 0.1 mm: %.1f%%  0.3 mm: %.1f%%  1 mm: %.1f%%\n", s == &res ? "all" : "trunk", s->size(), k4::percentile(*s, 0.5), k4::percentile(*s, 0.95), k4::maxOf(*s),
                    frac(*s, 0.1f), frac(*s, 0.3f), frac(*s, 1.0f));
    }
    return 0;
}
