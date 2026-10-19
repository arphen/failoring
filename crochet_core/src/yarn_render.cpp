// Yarn mode: the tube mesh along a rod chain and a small software renderer. See include/failoring/crochet/yarn_render.hpp.
#include "failoring/crochet/yarn_render.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "failoring/crochet/snapshot.hpp"

namespace fl {

namespace {

Vec3 catmull(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, Real t) {
    const Real t2 = t * t, t3 = t2 * t;
    return 0.5 * ((2.0 * p1) + (p2 - p0) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 + (3.0 * p1 - p0 - 3.0 * p2 + p3) * t3);
}

Vec3 catmullTangent(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, Real t) {
    const Real t2 = t * t;
    return 0.5 * ((p2 - p0) + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * (2.0 * t) + (3.0 * p1 - p0 - 3.0 * p2 + p3) * (3.0 * t2));
}

Quat nlerp(const Quat& a, Quat b, Real w) {
    if (a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z < 0) b = {-b.w, -b.x, -b.y, -b.z};
    return normalized(Quat{a.w * (1 - w) + b.w * w, a.x * (1 - w) + b.x * w, a.y * (1 - w) + b.y * w, a.z * (1 - w) + b.z * w});
}

}  // namespace

YarnMesh buildYarnMesh(const RodSystem& rs, const YarnMeshOptions& o) {
    YarnMesh m;
    const int sides = std::max(3, o.sides), sub = std::max(1, o.subdiv);
    for (size_t ci = 0; ci < rs.chains.size(); ++ci) {
        const RodChain& ch = rs.chains[ci];
        const int n = ch.particles, S = ch.segments;
        if (S < 1) continue;
        auto P = [&](int i) {
            if (ch.closed) return rs.x[ch.firstParticle + ((i % n) + n) % n];
            return rs.x[ch.firstParticle + std::clamp(i, 0, n - 1)];
        };
        auto Q = [&](int i) {
            if (ch.closed) return rs.q[ch.firstSegment + ((i % S) + S) % S];
            return rs.q[ch.firstSegment + std::clamp(i, 0, S - 1)];
        };
        Real arc = 0;
        Vec3 prevPos;
        bool havePrev = false;
        const int firstRing = (int)m.pos.size();
        const int samples = S * sub + (ch.closed ? 0 : 1);
        int ringsBuilt = 0;
        for (int k = 0; k < samples; ++k) {
            const int i = std::min(k / sub, S - 1);
            const Real t = (k == S * sub) ? 1.0 : (Real)(k % sub) / sub;
            const Vec3 pos = catmull(P(i - 1), P(i), P(i + 1), P(i + 2), t);
            Vec3 T = catmullTangent(P(i - 1), P(i), P(i + 1), P(i + 2), t);
            if (length2(T) < 1e-24) T = P(i + 1) - P(i);
            T = normalized(T);
            // the material frame: blend the frames of the segments either side of the sample, then square it to the tangent
            Quat qs;
            if (t < 0.5) qs = nlerp(Q(i - 1), Q(i), 0.5 + t);
            else qs = nlerp(Q(i), Q(i + 1), t - 0.5);
            Vec3 d1 = rotate(qs, Vec3{1, 0, 0});
            d1 = d1 - T * dot(d1, T);
            if (length2(d1) < 1e-18) d1 = cross(T, Vec3{0, 1, 0});
            d1 = normalized(d1);
            const Vec3 d2 = cross(T, d1);
            if (havePrev) arc += length(pos - prevPos);
            prevPos = pos;
            havePrev = true;
            const int seg = ch.firstSegment + std::min(i, S - 1);
            const Vec3 base = (!o.segmentColour.empty() && seg < (int)o.segmentColour.size()) ? o.segmentColour[seg] : o.colour;
            const Real r = rs.radius[seg] * o.radiusScale;
            for (int j = 0; j < sides; ++j) {
                const Real th = 2.0 * kPi * j / sides;
                const Vec3 radial = d1 * std::cos(th) + d2 * std::sin(th);
                m.pos.push_back(pos + radial * r);
                m.nrm.push_back(radial);
                const Real phase = o.plies * (th + 2.0 * kPi * arc / o.plyPitch);
                const Real tone = 1.0 - o.plyContrast + o.plyContrast * (0.5 + 0.5 * std::cos(phase));
                m.col.push_back(base * tone);
            }
            ++ringsBuilt;
        }
        for (int k = 0; k + 1 < ringsBuilt + (ch.closed ? 1 : 0); ++k) {
            const int a = firstRing + k * sides, b = firstRing + ((k + 1) % ringsBuilt) * sides;
            for (int j = 0; j < sides; ++j) {
                const int j1 = (j + 1) % sides;
                m.tri.push_back({a + j, b + j1, b + j});
                m.tri.push_back({a + j, a + j1, b + j1});
            }
        }
    }
    return m;
}

std::vector<unsigned char> renderYarnRgb(const YarnMesh& mesh, const YarnCamera& cam, int width, int height, const Vec3& background) {
    const int SS = 2, W = width * SS, H = height * SS;
    std::vector<float> depth((size_t)W * H, std::numeric_limits<float>::infinity());
    std::vector<float> img((size_t)W * H * 3);
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        img[3 * i] = (float)background.x;
        img[3 * i + 1] = (float)background.y;
        img[3 * i + 2] = (float)background.z;
    }
    const Vec3 toCam = normalized(cam.dir);
    const Vec3 fwd = -toCam;
    const Vec3 right = normalized(cross(fwd, cam.up));
    const Vec3 up = cross(right, fwd);
    const Vec3 eye = cam.target + toCam * cam.distance;
    const Real f = 0.5 * H / std::tan(0.5 * cam.fovDeg * kPi / 180.0);
    const Vec3 light = normalized(right * 0.35 + up * 0.6 + toCam * 1.0);
    const Vec3 fill = normalized(right * -0.7 + up * -0.2 + toCam * 0.6);
    const size_t nv = mesh.pos.size();
    std::vector<Vec3> cp(nv);  // camera space: x right, y up, z forward
    std::vector<Real> sx(nv), sy(nv);
    for (size_t i = 0; i < nv; ++i) {
        const Vec3 d = mesh.pos[i] - eye;
        cp[i] = Vec3{dot(d, right), dot(d, up), dot(d, fwd)};
        const Real z = std::max(cp[i].z, 1e-6);
        sx[i] = 0.5 * W + f * cp[i].x / z;
        sy[i] = 0.5 * H - f * cp[i].y / z;
    }
    for (const auto& t : mesh.tri) {
        const int a = t[0], b = t[1], c = t[2];
        if (cp[a].z <= 1e-4 || cp[b].z <= 1e-4 || cp[c].z <= 1e-4) continue;
        const Real area = (sx[b] - sx[a]) * (sy[c] - sy[a]) - (sx[c] - sx[a]) * (sy[b] - sy[a]);
        if (std::fabs(area) < 1e-9) continue;
        const int x0 = std::max(0, (int)std::floor(std::min({sx[a], sx[b], sx[c]}))), x1 = std::min(W - 1, (int)std::ceil(std::max({sx[a], sx[b], sx[c]})));
        const int y0 = std::max(0, (int)std::floor(std::min({sy[a], sy[b], sy[c]}))), y1 = std::min(H - 1, (int)std::ceil(std::max({sy[a], sy[b], sy[c]})));
        const Real za = 1.0 / cp[a].z, zb = 1.0 / cp[b].z, zc = 1.0 / cp[c].z;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const Real px = x + 0.5, py = y + 0.5;
                const Real w0 = ((sx[b] - px) * (sy[c] - py) - (sx[c] - px) * (sy[b] - py)) / area;
                const Real w1 = ((sx[c] - px) * (sy[a] - py) - (sx[a] - px) * (sy[c] - py)) / area;
                const Real w2 = 1.0 - w0 - w1;
                if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
                const Real iz = w0 * za + w1 * zb + w2 * zc;  // 1/z is linear in screen space
                const float z = (float)(1.0 / iz);
                float& zbuf = depth[(size_t)y * W + x];
                if (z >= zbuf) continue;
                zbuf = z;
                Vec3 n = normalized(mesh.nrm[a] * w0 + mesh.nrm[b] * w1 + mesh.nrm[c] * w2);
                if (dot(n, toCam) < 0) n = -n;
                const Vec3 col = mesh.col[a] * w0 + mesh.col[b] * w1 + mesh.col[c] * w2;
                const Real diff = std::max(0.0, dot(n, light)) * 0.62 + std::max(0.0, dot(n, fill)) * 0.18;
                const Vec3 h = normalized(light + toCam);
                const Real spec = std::pow(std::max(0.0, dot(n, h)), 24.0) * 0.12;
                const Real shade = 0.30 + diff;
                float* o = &img[((size_t)y * W + x) * 3];
                o[0] = (float)std::min(1.0, col.x * shade + spec);
                o[1] = (float)std::min(1.0, col.y * shade + spec);
                o[2] = (float)std::min(1.0, col.z * shade + spec);
            }
    }
    std::vector<unsigned char> out((size_t)width * height * 3);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            for (int c = 0; c < 3; ++c) {
                float s = 0;
                for (int dy = 0; dy < SS; ++dy)
                    for (int dx = 0; dx < SS; ++dx) s += img[(((size_t)(y * SS + dy)) * W + (x * SS + dx)) * 3 + c];
                out[((size_t)y * width + x) * 3 + c] = (unsigned char)std::clamp(s / (SS * SS) * 255.0f + 0.5f, 0.0f, 255.0f);
            }
    return out;
}

bool renderYarnPng(const std::string& path, const YarnMesh& mesh, const YarnCamera& cam, int width, int height, const Vec3& background) {
    return writePngRgb(path, renderYarnRgb(mesh, cam, width, height, background), width, height);
}

}  // namespace fl
