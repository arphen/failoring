// Orthographic software renderer + PNG writer (see crochet/snapshot.hpp).
#include "failoring/crochet/snapshot.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace fl {

namespace {

std::uint32_t crc32(const unsigned char* data, size_t n, std::uint32_t crc = 0) {
    static std::uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

void put32(std::vector<unsigned char>& v, std::uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<unsigned char>((x >> s) & 0xff));
}

void chunk(std::vector<unsigned char>& out, const char* type, const std::vector<unsigned char>& data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<unsigned char> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32(body.data(), body.size()));
}

// RGB, 8 bit; stored (uncompressed) deflate blocks, which every PNG reader accepts.
std::vector<unsigned char> encodePng(const std::vector<unsigned char>& rgb, int w, int h) {
    std::vector<unsigned char> raw;
    raw.reserve(static_cast<size_t>(h) * (3 * w + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + static_cast<size_t>(y) * 3 * w, rgb.begin() + static_cast<size_t>(y + 1) * 3 * w);
    }
    std::vector<unsigned char> z{0x78, 0x01};
    for (size_t pos = 0; pos < raw.size();) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(static_cast<unsigned char>(n & 0xff));
        z.push_back(static_cast<unsigned char>(n >> 8));
        z.push_back(static_cast<unsigned char>(~n & 0xff));
        z.push_back(static_cast<unsigned char>((~n >> 8) & 0xff));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    std::uint32_t a = 1, b = 0;
    for (const unsigned char c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    put32(z, (b << 16) | a);
    std::vector<unsigned char> png{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<unsigned char> ihdr;
    put32(ihdr, static_cast<std::uint32_t>(w));
    put32(ihdr, static_cast<std::uint32_t>(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});
    return png;
}

struct Camera {
    Vec3 forward, right, up;  // forward points from the camera into the scene
};

Camera makeCamera(const Vec3& towardCamera, const Vec3& up) {
    Camera c;
    c.forward = normalized(-towardCamera);
    c.right = normalized(cross(c.forward, up));
    c.up = cross(c.right, c.forward);
    return c;
}

}  // namespace

bool writeSnapshotPng(const std::string& path, const std::vector<Vec3>& pos, const std::vector<std::array<int, 3>>& tris,
                      const std::vector<SnapshotColour>& colours, int tile) {
    if (pos.empty() || tris.empty()) return false;
    Vec3 lo(1e300, 1e300, 1e300), hi(-1e300, -1e300, -1e300);
    for (const Vec3& p : pos) {
        lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
        hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
    }
    const Vec3 centre = (lo + hi) * 0.5;
    const Real radius = std::max(0.5 * length(hi - lo), 1e-6);
    const Camera cams[4] = {makeCamera(Vec3(0, 0, 1), Vec3(0, 1, 0)), makeCamera(Vec3(1, 0, 0), Vec3(0, 1, 0)),
                            makeCamera(Vec3(0, 1, 0), Vec3(0, 0, -1)), makeCamera(Vec3(0.8, 0.6, 1.0), Vec3(0, 1, 0))};
    const int W = tile * 4, H = tile;
    std::vector<unsigned char> img(static_cast<size_t>(W) * H * 3, 235);
    const Real scale = 0.46 * tile / radius;
    const Vec3 light = normalized(Vec3(0.3, 0.6, 1.0));
    for (int v = 0; v < 4; ++v) {
        const Camera& cam = cams[v];
        std::vector<float> depth(static_cast<size_t>(tile) * tile, std::numeric_limits<float>::infinity());
        for (size_t t = 0; t < tris.size(); ++t) {
            const Vec3 p[3] = {pos[tris[t][0]], pos[tris[t][1]], pos[tris[t][2]]};
            const Vec3 n = normalized(cross(p[1] - p[0], p[2] - p[0]));
            const bool front = dot(n, cam.forward) < 0;  // winding normal toward the camera = right side
            const Real shade = 0.45 + 0.55 * std::fabs(dot(n, light));
            SnapshotColour base = colours.empty() ? SnapshotColour() : colours[colours.size() == tris.size() ? t : 0];
            Real rr = base.r, gg = base.g, bb = base.b;
            if (!front) {  // wrong side: tint toward blue-grey
                rr = 0.4 * rr + 0.6 * 90;
                gg = 0.4 * gg + 0.6 * 120;
                bb = 0.4 * bb + 0.6 * 170;
            }
            Real sx[3], sy[3], sz[3];
            for (int k = 0; k < 3; ++k) {
                const Vec3 q = p[k] - centre;
                sx[k] = 0.5 * tile + scale * dot(q, cam.right);
                sy[k] = 0.5 * tile - scale * dot(q, cam.up);
                sz[k] = dot(q, cam.forward);
            }
            const int x0 = std::max(0, static_cast<int>(std::floor(std::min({sx[0], sx[1], sx[2]})))),
                      x1 = std::min(tile - 1, static_cast<int>(std::ceil(std::max({sx[0], sx[1], sx[2]})))),
                      y0 = std::max(0, static_cast<int>(std::floor(std::min({sy[0], sy[1], sy[2]})))),
                      y1 = std::min(tile - 1, static_cast<int>(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
            const Real area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
            if (std::fabs(area) < 0.5) {  // edge-on: draw its edges as depth-tested lines so the surface is not invisible
                for (int k = 0; k < 3; ++k) {
                    const int a = k, b = (k + 1) % 3;
                    const int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::fabs(sx[b] - sx[a]), std::fabs(sy[b] - sy[a])))));
                    for (int i = 0; i <= steps; ++i) {
                        const Real f = static_cast<Real>(i) / steps;
                        const int x = static_cast<int>(sx[a] + (sx[b] - sx[a]) * f), y = static_cast<int>(sy[a] + (sy[b] - sy[a]) * f);
                        if (x < 0 || y < 0 || x >= tile || y >= tile) continue;
                        const float z = static_cast<float>(sz[a] + (sz[b] - sz[a]) * f);
                        float& zb = depth[static_cast<size_t>(y) * tile + x];
                        if (z >= zb) continue;
                        zb = z;
                        unsigned char* o = &img[(static_cast<size_t>(y) * W + v * tile + x) * 3];
                        o[0] = static_cast<unsigned char>(std::min(255.0, rr * shade * 0.8));
                        o[1] = static_cast<unsigned char>(std::min(255.0, gg * shade * 0.8));
                        o[2] = static_cast<unsigned char>(std::min(255.0, bb * shade * 0.8));
                    }
                }
                continue;
            }
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const Real px = x + 0.5, py = y + 0.5;
                    const Real w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area;
                    const Real w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area;
                    const Real w2 = 1 - w0 - w1;
                    if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
                    const float z = static_cast<float>(w0 * sz[0] + w1 * sz[1] + w2 * sz[2]);
                    float& zb = depth[static_cast<size_t>(y) * tile + x];
                    if (z >= zb) continue;
                    zb = z;
                    unsigned char* o = &img[(static_cast<size_t>(y) * W + v * tile + x) * 3];
                    o[0] = static_cast<unsigned char>(std::min(255.0, rr * shade));
                    o[1] = static_cast<unsigned char>(std::min(255.0, gg * shade));
                    o[2] = static_cast<unsigned char>(std::min(255.0, bb * shade));
                }
        }
        for (int y = 0; y < tile; ++y) {  // tile separator
            unsigned char* o = &img[(static_cast<size_t>(y) * W + v * tile) * 3];
            o[0] = o[1] = o[2] = 120;
        }
    }
    const std::vector<unsigned char> png = encodePng(img, W, H);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    std::fclose(f);
    return ok;
}

}  // namespace fl
