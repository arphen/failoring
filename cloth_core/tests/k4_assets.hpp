#pragma once
// Loader for the exported mannequin (tools/export_godot_assets.js): body.obj and walk.bin (float16 offsets per frame).
// Used by the K4 real-mesh gate and the measurement tool; header only, test code.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "failoring/math.hpp"

namespace k4 {

using Tris = std::vector<std::array<int, 3>>;

inline bool loadObj(const std::string& path, std::vector<fl::Vec3>& V, Tris& T) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.size() > 2 && line[0] == 'v' && line[1] == ' ') {
            double x, y, z;
            if (std::sscanf(line.c_str() + 2, "%lf %lf %lf", &x, &y, &z) == 3) V.push_back(fl::Vec3(x, y, z));
        } else if (line.size() > 2 && line[0] == 'f' && line[1] == ' ') {
            int a, b, c;
            if (std::sscanf(line.c_str() + 2, "%d %d %d", &a, &b, &c) == 3) T.push_back({a - 1, b - 1, c - 1});
        }
    }
    return !V.empty() && !T.empty();
}

inline float halfToFloat(std::uint16_t h) {
    const int s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
    float v;
    if (e == 0) v = std::ldexp(static_cast<float>(m), -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else v = std::ldexp(static_cast<float>(m + 1024), e - 25);
    return s ? -v : v;
}

// Reads the per-frame vertex positions (standing mesh + float16 offsets).
inline bool loadWalk(const std::string& path, const std::vector<fl::Vec3>& stand, std::vector<std::vector<fl::Vec3>>& frames) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.size() < 12 || std::memcmp(buf.data(), "FLWALK01", 8) != 0) return false;
    std::uint32_t H;
    std::memcpy(&H, buf.data() + 8, 4);
    const std::string hdr(buf.data() + 12, H);
    auto num = [&](const char* key, size_t from) -> long long {
        const size_t p = hdr.find(key, from);
        if (p == std::string::npos) return -1;
        return std::atoll(hdr.c_str() + p + std::strlen(key));
    };
    const size_t sec = hdr.find("\"frames\":{");
    const long long nf = num("\"frames\":", 0);
    long long off = -1;
    if (sec != std::string::npos) off = num("\"offset\":", sec);
    if (nf <= 0 || off < 0) return false;
    const size_t base = 12 + H + static_cast<size_t>(off), nv = stand.size();
    if (base + static_cast<size_t>(nf) * nv * 6 > buf.size()) return false;
    frames.assign(static_cast<size_t>(nf), std::vector<fl::Vec3>(nv));
    for (long long k = 0; k < nf; ++k)
        for (size_t i = 0; i < nv; ++i) {
            std::uint16_t h[3];
            std::memcpy(h, buf.data() + base + (static_cast<size_t>(k) * nv + i) * 6, 6);
            frames[static_cast<size_t>(k)][i] = stand[i] + fl::Vec3(halfToFloat(h[0]), halfToFloat(h[1]), halfToFloat(h[2]));
        }
    return true;
}

}  // namespace k4
