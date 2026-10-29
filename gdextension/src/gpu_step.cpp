#include "gpu_step.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <regex>
#include <set>

#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/typed_array.hpp>

using namespace godot;

namespace {

// ---------------------------------------------------------------------------------------------------------------------
// Buffers. Every kernel declares exactly the buffers its source mentions (found by scanning the source for these names),
// so names in kernels are upper case and nothing else in a kernel is. Types: f float, i int.
const std::vector<std::pair<const char*, char>> kBuffers = {
    // state: substep start position, displacement within the substep, velocity, predicted displacement, its backup, the
    // CG solution
    {"XP", 'f'}, {"U", 'f'}, {"V", 'f'}, {"UT", 'f'}, {"UB", 'f'}, {"DX", 'f'},
    // per vertex constants
    {"W", 'f'}, {"M", 'f'}, {"DAMP", 'f'}, {"MUK", 'f'}, {"RAD", 'f'},
    // contact: normal, offset distance, normal force of the hold; friction: mu*lambda, lagged normal, on flag, body motion
    {"CN", 'f'}, {"CPHI", 'f'}, {"RF", 'f'}, {"FLAM", 'f'}, {"FN", 'f'}, {"FON", 'i'}, {"MOT", 'f'},
    // elements
    {"TRI", 'i'}, {"TPAR", 'f'}, {"BND", 'i'}, {"BALPHA", 'f'}, {"SEAM", 'i'}, {"SPAR", 'f'},
    // assembly scratch and gather lists
    {"HS", 'f'}, {"GS", 'f'}, {"BLKS", 'i'}, {"BLKI", 'i'}, {"GST", 'i'}, {"GIT", 'i'},
    // the system
    {"ROW", 'i'}, {"COLI", 'i'}, {"DIAG", 'i'}, {"K", 'f'}, {"RHS", 'f'}, {"FILT", 'f'},
    // CG
    {"PINV", 'f'}, {"R", 'f'}, {"Z", 'f'}, {"P", 'f'}, {"AP", 'f'},
    // reductions, scalars, flags, parameters
    {"PART", 'f'}, {"SC", 'f'}, {"FL", 'i'}, {"PF", 'f'}, {"PQ", 'i'},
    // colliders: primitives, the distance grid
    {"CL", 'f'}, {"BRICK", 'i'}, {"FINE", 'f'}, {"COARSE", 'f'},
    // strain limiting
    {"EA", 'i'}, {"EB", 'i'}, {"EL0", 'f'},
    // self collision: triangles per hash cell (count, start, fill cursor, items), correction accumulators (fixed point,
    // 4 ints per vertex), excluded (vertex, triangle) pairs
    {"HCNT", 'i'}, {"HST", 'i'}, {"HCUR", 'i'}, {"HITEM", 'i'}, {"ACC", 'i'}, {"NCS", 'i'}, {"NCT", 'i'},
};

// PF (float parameters) and PQ (int parameters), SC (float scalars), FL (int flags): indices as used in the kernels
//   PF: 0 h, 1 1/h^2, 2-4 gravity, 5 seam ramp scale, 6 contact band, 7 friction eps (slip per substep), 8 1/substeps,
//       9 1 + strain limit, 10 CG tolerance, 11 line search tolerance (relative), 12-14 grid fine origin, 15 fine h,
//       16 1/fine h, 17-19 coarse origin, 20 coarse h, 21 1/coarse h
//   PQ: 0 N, 1 triangles, 2 hinges, 3 seam links, 4 nnz, 5 tension term, 6 has grid, 7-9 bricks per axis, 10-12 coarse
//       samples per axis, 13 planes, 14 spheres, 15 boxes, 16 has body motion, 17 friction (mode 2), 18 push-out after
//       the solve, 19 any collider, 20 PART region stride, 21 self collision, 22 hash table size, 23 hash item capacity
//   PF (cont.): 22 1/hash cell, 23 self contact thickness
//   SC: 1 |b|
//   FL: 0 CG done, 1 line search accepted, 2 CG iterations (frame), 3 failed line searches (frame)

const char* kPrelude = R"GLSL(
#version 450
layout(local_size_x = WG) in;
layout(push_constant, std430) uniform Pc { ivec4 pc; };
#define LD3(A, i) vec3(A[3 * (i)], A[3 * (i) + 1], A[3 * (i) + 2])
#define ST3(A, i, v) { vec3 t_ = (v); A[3 * (i)] = t_.x; A[3 * (i) + 1] = t_.y; A[3 * (i) + 2] = t_.z; }
#define LDM(A, k) transpose(mat3(A[9 * (k)], A[9 * (k) + 1], A[9 * (k) + 2], A[9 * (k) + 3], A[9 * (k) + 4], A[9 * (k) + 5], A[9 * (k) + 6], A[9 * (k) + 7], A[9 * (k) + 8]))
#define STM(A, k, m) { mat3 m_ = transpose(m); A[9 * (k)] = m_[0][0]; A[9 * (k) + 1] = m_[0][1]; A[9 * (k) + 2] = m_[0][2]; A[9 * (k) + 3] = m_[1][0]; A[9 * (k) + 4] = m_[1][1]; A[9 * (k) + 5] = m_[1][2]; A[9 * (k) + 6] = m_[2][0]; A[9 * (k) + 7] = m_[2][1]; A[9 * (k) + 8] = m_[2][2]; }
// inverse by the adjugate (0 for a singular block), as fl::cgBlockJacobiFloat
mat3 inv3(mat3 a) {
    float det = determinant(a);
    if (det == 0.0) return mat3(0.0);
    return inverse(a);
}
// friction smoothing (cloth_friction.cpp)
float fr0(float y, float eps) { return y < eps ? y * y / eps - y * y * y / (3.0 * eps * eps) + eps / 3.0 : y; }
float fr1(float y, float eps) { return y < eps ? 2.0 * y / eps - y * y / (eps * eps) : 1.0; }
// unsigned dihedral angle of a hinge and its gradient (detail::hingeAngle); -1 for a degenerate hinge. The flat limit is
// cut at s < 1e-5 (the CPU's 1e-9 is below float noise: there the gradient q/s is noise over noise).
float hinge(vec3 x1, vec3 x2, vec3 x3, vec3 x4, out vec3 g0, out vec3 g1, out vec3 g2, out vec3 g3) {
    g0 = vec3(0.0); g1 = vec3(0.0); g2 = vec3(0.0); g3 = vec3(0.0);
    vec3 e = x2 - x1, p3 = x3 - x1, p4 = x4 - x1;
    vec3 c3 = cross(e, p3), c4 = cross(e, p4);
    float l3 = length(c3), l4 = length(c4);
    if (l3 < 1e-14 || l4 < 1e-14) return -1.0;
    vec3 n1 = c3 / l3, n2 = c4 / l4;
    float d = dot(n1, n2);
    float s = length(cross(n1, n2));
    float theta = atan(s, -d);
    if (s < 1e-5) return theta;
    vec3 q3 = (cross(e, n2) + cross(n1, e) * d) / l3;
    vec3 q4 = (cross(e, n1) + cross(n2, e) * d) / l4;
    vec3 q2 = -(cross(p3, n2) + cross(n1, p3) * d) / l3 - (cross(p4, n1) + cross(n2, p4) * d) / l4;
    vec3 q1 = -q2 - q3 - q4;
    float k = -1.0 / s;
    g0 = q1 * k; g1 = q2 * k; g2 = q3 * k; g3 = q4 * k;
    return theta;
}
)GLSL";

// Position of vertex i and the difference x_j - x_i (start positions and displacements subtracted separately).
const char* kPosLib = R"GLSL(
vec3 posAt(int i) { return LD3(XP, i) + LD3(U, i); }
vec3 difAt(int i, int j) { return (LD3(XP, j) - LD3(XP, i)) + (LD3(U, j) - LD3(U, i)); }
)GLSL";

// Partial sums in PART, 6 regions of PQ[20] floats (2 per workgroup): 0 p.Ap, 1-2 (r.r, r.z) ping-pong, 3 energy before
// the line search, 4-5 trial energies ping-pong. wgSum2 writes the workgroup's two sums into a region; sumPart adds a
// region up over `groups` workgroups (every workgroup computes the same sums in the same order, so they all agree; this
// replaces a separate reduction dispatch). Every thread of the group must call both.
const char* kRedLib = R"GLSL(
shared float red0[64];
shared float red1[64];
void wgSum2(int region, float a, float b) {
    uint t = gl_LocalInvocationID.x;
    red0[t] = a;
    red1[t] = b;
    barrier();
    for (uint s = 32u; s > 0u; s >>= 1u) {
        if (t < s) { red0[t] += red0[t + s]; red1[t] += red1[t + s]; }
        barrier();
    }
    if (t == 0u) { int o = region * PQ[20] + 2 * int(gl_WorkGroupID.x); PART[o] = red0[0]; PART[o + 1] = red1[0]; }
    barrier();
}
vec2 sumPart(int region, int groups) {
    int t = int(gl_LocalInvocationID.x);
    int o = region * PQ[20];
    float a = 0.0, b = 0.0;
    for (int g = t; g < groups; g += 64) { a += PART[o + 2 * g]; b += PART[o + 2 * g + 1]; }
    red0[t] = a;
    red1[t] = b;
    barrier();
    for (int s = 32; s > 0; s >>= 1) {
        if (t < s) { red0[t] += red0[t + s]; red1[t] += red1[t + s]; }
        barrier();
    }
    vec2 r = vec2(red0[0], red1[0]);
    barrier();
    return r;
}
bool lsAccept(float e, float e0) { return !isnan(e) && !isinf(e) && e <= e0 + PF[11] * abs(e0); }
)GLSL";

// Colliders: planes, spheres, the distance grid (port of fl::SdfGrid::evalFine / evalCoarse), oriented boxes; the
// nearest one wins (fl::UnionCollider), in the order the CPU builds the union.
const char* kSdfLib = R"GLSL(
void bspl(float t, out vec3 w, out vec3 d) {
    float a = 0.5 - t, b = 0.5 + t;
    w = vec3(0.5 * a * a, 0.75 - t * t, 0.5 * b * b);
    d = vec3(-a, -2.0 * t, b);
}
bool evalFine(vec3 p, out float v, out vec3 gr) {
    v = 0.0; gr = vec3(0.0);
    vec3 g = (p - vec3(PF[12], PF[13], PF[14])) * PF[16];
    ivec3 ix = ivec3(floor(g + 0.5));
    int bnx = PQ[7], bny = PQ[8], bnz = PQ[9];
    if (ix.x < 1 || ix.y < 1 || ix.z < 1 || ix.x + 1 >= 2 * bnx || ix.y + 1 >= 2 * bny || ix.z + 1 >= 2 * bnz) return false;
    vec3 wx, wy, wz, dx, dy, dz;
    bspl(g.x - float(ix.x), wx, dx); bspl(g.y - float(ix.y), wy, dy); bspl(g.z - float(ix.z), wz, dz);
    float val = 0.0, g0 = 0.0, g1 = 0.0, g2 = 0.0;
    for (int c = 0; c < 3; ++c)
        for (int b = 0; b < 3; ++b) {
            int zz = ix.z - 1 + c, yy = ix.y - 1 + b;
            int row = ((zz >> 1) * bny + (yy >> 1)) * bnx, lrow = (((zz & 1) << 1) + (yy & 1)) << 1;
            float wyz = wy[b] * wz[c], dyz = dy[b] * wz[c], wdz = wy[b] * dz[c];
            for (int a = 0; a < 3; ++a) {
                int xx = ix.x - 1 + a;
                int br = BRICK[row + (xx >> 1)];
                if (br < 0) return false;
                float s = FINE[(br << 3) + lrow + (xx & 1)];
                val += wx[a] * wyz * s;
                g0 += dx[a] * wyz * s;
                g1 += wx[a] * dyz * s;
                g2 += wx[a] * wdz * s;
            }
        }
    v = val;
    gr = vec3(g0, g1, g2) * PF[16];
    return true;
}
float evalCoarse(vec3 p, out vec3 gr) {
    float ch = PF[20], ci = PF[21];
    int cnx = PQ[10], cny = PQ[11], cnz = PQ[12];
    vec3 g = (p - vec3(PF[17], PF[18], PF[19])) * ci;
    vec3 c = clamp(g, vec3(1.0), vec3(float(cnx - 2), float(cny - 2), float(cnz - 2)));
    vec3 off = (g - c) * ch;
    ivec3 ix = ivec3(floor(c + 0.5));
    vec3 wx, wy, wz, dx, dy, dz;
    bspl(c.x - float(ix.x), wx, dx); bspl(c.y - float(ix.y), wy, dy); bspl(c.z - float(ix.z), wz, dz);
    float val = 0.0, g0 = 0.0, g1 = 0.0, g2 = 0.0;
    for (int cc = 0; cc < 3; ++cc)
        for (int b = 0; b < 3; ++b) {
            int row = ((ix.z - 1 + cc) * cny + (ix.y - 1 + b)) * cnx + (ix.x - 1);
            float wyz = wy[b] * wz[cc], dyz = dy[b] * wz[cc], wdz = wy[b] * dz[cc];
            for (int a = 0; a < 3; ++a) {
                float s = COARSE[row + a];
                val += wx[a] * wyz * s;
                g0 += dx[a] * wyz * s;
                g1 += wx[a] * dyz * s;
                g2 += wx[a] * wdz * s;
            }
        }
    float o = length(off);
    if (o > 0.0) { gr = off / o; return val + o; }
    gr = vec3(g0, g1, g2) * ci;
    return val;
}
// sh: added to p for the moving colliders (the body grid, box props), not for planes and spheres; see bodyShift
float colEval(vec3 p, vec3 sh, out vec3 n) {
    float best = 1e30;
    n = vec3(0.0, 1.0, 0.0);
    int np = PQ[13], ns = PQ[14], nb = PQ[15];
    for (int k = 0; k < np; ++k) {
        vec3 q = vec3(CL[6 * k], CL[6 * k + 1], CL[6 * k + 2]), nn = vec3(CL[6 * k + 3], CL[6 * k + 4], CL[6 * k + 5]);
        float d = dot(p - q, nn);
        if (d < best) { best = d; n = nn; }
    }
    for (int k = 0; k < ns; ++k) {
        int o = 6 * np + 4 * k;
        vec3 dd = p - vec3(CL[o], CL[o + 1], CL[o + 2]);
        float l = length(dd);
        float d = l - CL[o + 3];
        if (d < best) { best = d; n = l > 1e-12 ? dd / l : vec3(0.0, 1.0, 0.0); }
    }
    if (PQ[6] != 0) {
        float v;
        vec3 gr;
        if (!evalFine(p + sh, v, gr)) v = evalCoarse(p + sh, gr);
        if (v < best) { float l = length(gr); best = v; n = l > 1e-9 ? gr / l : vec3(0.0, 1.0, 0.0); }
    }
    for (int k = 0; k < nb; ++k) {
        int o = 6 * np + 4 * ns + 15 * k;
        vec3 org = vec3(CL[o], CL[o + 1], CL[o + 2]);
        vec3 ax0 = vec3(CL[o + 3], CL[o + 4], CL[o + 5]), ax1 = vec3(CL[o + 6], CL[o + 7], CL[o + 8]), ax2 = vec3(CL[o + 9], CL[o + 10], CL[o + 11]);
        vec3 h = vec3(CL[o + 12], CL[o + 13], CL[o + 14]);
        vec3 d = p + sh - org;
        vec3 l = vec3(dot(d, ax0), dot(d, ax1), dot(d, ax2));
        vec3 q = abs(l) - h;
        vec3 nl = vec3(0.0);
        bool outside = false;
        for (int i = 0; i < 3; ++i)
            if (q[i] > 0.0) { outside = true; nl[i] = l[i] >= 0.0 ? q[i] : -q[i]; }
        float dist;
        if (outside) { dist = length(nl); nl /= dist; }
        else {
            int m = 0;
            for (int i = 1; i < 3; ++i) if (q[i] > q[m]) m = i;
            dist = q[m];
            nl[m] = l[m] >= 0.0 ? 1.0 : -1.0;
        }
        if (dist < best) { best = dist; n = ax0 * nl.x + ax1 * nl.y + ax2 * nl.z; }
    }
    return best;
}
// The body moves within the frame: it is baked once, at the frame's end, and at substep `sub` (of PQ count, see PF[8])
// it is still short of that by the part of its motion to come. Seen from cloth vertex i, that is the vertex moved
// forward by that part of the body's motion at the vertex (MOT, over the whole frame). Without it the body jumped a
// whole frame at the first substep: a knee swinging at 4 m/s moved 7 cm, past the middle of the leg, and cloth caught
// in it was pushed out of the far side (the runway walk).
vec3 bodyShift(int i, int sub) {
    if (PQ[16] == 0) return vec3(0.0);
    float rest = 1.0 - float(sub + 1) * PF[8];
    return LD3(MOT, i) * rest;
}
)GLSL";

struct KernelSrc { const char* name; int wg; const char* body; };

const KernelSrc kKernels[] = {
    // damping, gravity, prediction (xTilde - XP = UT; the first iterate is the prediction)
    {"predict", 64, R"GLSL(
void main() {
    int i = int(gl_GlobalInvocationID.x);
    if (i >= pc.x) return;
    if (W[i] == 0.0) { ST3(V, i, vec3(0.0)); ST3(U, i, vec3(0.0)); ST3(UT, i, vec3(0.0)); return; }
    float h = PF[0];
    vec3 v = LD3(V, i) * exp(-DAMP[i] * h) + vec3(PF[2], PF[3], PF[4]) * h;
    ST3(V, i, v);
    ST3(UT, i, v * h);
    ST3(U, i, v * h);
}
)GLSL"},
    // friction normal force and normal of the end of the previous substep (frictionLag)
    {"fricLag", 64, R"GLSL(
void main() {
    int i = int(gl_GlobalInvocationID.x);
    if (i >= pc.x) return;
    float lam = max(RF[i], 0.0);
    FLAM[i] = (W[i] != 0.0 && CPHI[i] < 1e29) ? lam * MUK[i] : 0.0;
    ST3(FN, i, LD3(CN, i));
}
)GLSL"},
    // lift penetrating vertices onto the body, record distance and normal
    {"contact", 64, R"GLSL(
void main() {
    int i = int(gl_GlobalInvocationID.x);
    if (i >= pc.x) return;
    if (PQ[19] == 0 || W[i] == 0.0) { CPHI[i] = 1e30; return; }
    vec3 n;
    float d = colEval(posAt(i), bodyShift(i, pc.w), n) - RAD[i];
    if (d < 0.0) { ST3(U, i, LD3(U, i) - n * d); d = 0.0; }
    ST3(CN, i, n);
    CPHI[i] = d;
}
)GLSL"},
    // per element: gradient pieces into GS, Hessian blocks into HS (assembleEnergy)
    {"elem", 64, R"GLSL(
void addC(inout mat3 hh[16], inout vec3 gg[4], int n, vec3 g[4], float c, float kap) {
    for (int a = 0; a < n; ++a) gg[a] += g[a] * (kap * c);
    for (int a = 0; a < n; ++a)
        for (int b = 0; b < n; ++b) hh[a * n + b] += kap * outerProduct(g[a], g[b]);
}
void addT(inout mat3 hh[16], float beta[3], vec3 nrm, float s) {
    mat3 pr = mat3(1.0) - outerProduct(nrm, nrm);
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b) hh[a * 3 + b] += (s * beta[a] * beta[b]) * pr;
}
void main() {
    int e = int(gl_GlobalInvocationID.x);
    if (e >= pc.x) return;
    int nt = PQ[1], nb = PQ[2];
    mat3 hh[16];
    vec3 gg[4];
    for (int q = 0; q < 16; ++q) hh[q] = mat3(0.0);
    for (int a = 0; a < 4; ++a) gg[a] = vec3(0.0);
    vec3 g[4];
    g[3] = vec3(0.0);
    int nv, hbase, gbase;
    if (e < nt) {
        int i0 = TRI[3 * e], i1 = TRI[3 * e + 1], i2 = TRI[3 * e + 2];
        int o = 11 * e;
        float a = TPAR[o], b = TPAR[o + 1], c = TPAR[o + 2], d = TPAR[o + 3];
        float alU = TPAR[o + 4], alV = TPAR[o + 5], alS = TPAR[o + 6], comp = TPAR[o + 7];
        float lockU = TPAR[o + 8], lockV = TPAR[o + 9], ratio = TPAR[o + 10];
        bool tension = PQ[5] != 0;
        vec3 d1 = difAt(i0, i1), d2 = difAt(i0, i2);
        vec3 wu = d1 * a + d2 * c, wv = d1 * b + d2 * d;
        float lu = length(wu), lv = length(wv);
        if (lu > 1e-12) {
            vec3 nu = wu / lu;
            g[0] = -(nu * a + nu * c); g[1] = nu * a; g[2] = nu * c;
            float cc = lu - 1.0;
            float beta[3] = float[3](-(a + c), a, c);
            if (lockU > 0.0 && cc > lockU) {
                float k = 1.0 / alU, cl = cc - lockU, dedc = k * lockU + ratio * k * cl, kap = ratio * k;
                addC(hh, gg, 3, g, dedc / kap, kap);
                if (tension) addT(hh, beta, nu, dedc / lu);
            } else {
                addC(hh, gg, 3, g, cc, (cc < 0.0 ? comp : 1.0) / alU);
                if (tension && cc > 0.0) addT(hh, beta, nu, cc / (alU * lu));
            }
        }
        if (lv > 1e-12) {
            vec3 nv2 = wv / lv;
            g[0] = -(nv2 * b + nv2 * d); g[1] = nv2 * b; g[2] = nv2 * d;
            float cc = lv - 1.0;
            float beta[3] = float[3](-(b + d), b, d);
            if (lockV > 0.0 && cc > lockV) {
                float k = 1.0 / alV, cl = cc - lockV, dedc = k * lockV + ratio * k * cl, kap = ratio * k;
                addC(hh, gg, 3, g, dedc / kap, kap);
                if (tension) addT(hh, beta, nv2, dedc / lv);
            } else {
                addC(hh, gg, 3, g, cc, (cc < 0.0 ? comp : 1.0) / alV);
                if (tension && cc > 0.0) addT(hh, beta, nv2, cc / (alV * lv));
            }
        }
        {
            vec3 g1 = wv * a + wu * b, g2 = wv * c + wu * d;
            g[0] = -(g1 + g2); g[1] = g1; g[2] = g2;
            addC(hh, gg, 3, g, dot(wu, wv), 1.0 / alS);
        }
        nv = 3; hbase = 9 * e; gbase = 3 * e;
    } else if (e < nt + nb) {
        int h = e - nt;
        int i0 = BND[4 * h], i1 = BND[4 * h + 1], i2 = BND[4 * h + 2], i3 = BND[4 * h + 3];
        float th = hinge(vec3(0.0), difAt(i0, i1), difAt(i0, i2), difAt(i0, i3), g[0], g[1], g[2], g[3]);
        if (th >= 0.0) addC(hh, gg, 4, g, th, 1.0 / BALPHA[h]);
        nv = 4; hbase = 9 * nt + 16 * h; gbase = 3 * nt + 4 * h;
    } else {
        int s = e - nt - nb;
        int ia = SEAM[3 * s], ib0 = SEAM[3 * s + 1], ib1 = SEAM[3 * s + 2];
        float al = SPAR[2 * s + 1];
        float t = SPAR[2 * s], k = (al < 0.0 ? PF[24] : PF[5]) / abs(al);
        // x_a - ((1 - t) x_b0 + t x_b1), differences taken before the sum
        vec3 dd = -(difAt(ia, ib0) * (1.0 - t) + difAt(ia, ib1) * t);
        float w3[3] = float[3](1.0, -(1.0 - t), -t);
        for (int a = 0; a < 3; ++a) {
            gg[a] = dd * (k * w3[a]);
            for (int b = 0; b < 3; ++b) hh[a * 3 + b] = mat3(k * w3[a] * w3[b]);
        }
        nv = 3; hbase = 9 * nt + 16 * nb + 9 * s; gbase = 3 * nt + 4 * nb + 3 * s;
    }
    for (int q = 0; q < nv * nv; ++q) STM(HS, hbase + q, hh[q]);
    for (int a = 0; a < nv; ++a) ST3(GS, gbase + a, gg[a]);
}
)GLSL"},
    // gather the Hessian blocks
    {"gatherK", 64, R"GLSL(
void main() {
    int k = int(gl_GlobalInvocationID.x);
    if (k >= pc.x) return;
    mat3 s = mat3(0.0);
    for (int q = BLKS[k]; q < BLKS[k + 1]; ++q) s += LDM(HS, BLKI[q]);
    STM(K, k, s);
}
)GLSL"},
    // per vertex: gradient, friction (assembleFriction), right-hand side, contact hold filter
    {"vertex", 64, R"GLSL(
void main() {
    int i = int(gl_GlobalInvocationID.x);
    if (i >= pc.x) return;
    vec3 g = vec3(0.0);
    for (int q = GST[i]; q < GST[i + 1]; ++q) g += LD3(GS, GIT[q]);
    if (PQ[17] != 0) {
        FON[i] = 0;
        if (FLAM[i] > 0.0 && CPHI[i] < PF[6]) {
            FON[i] = 1;
            vec3 n = LD3(FN, i);
            vec3 u = LD3(U, i) - (PQ[16] != 0 ? LD3(MOT, i) * PF[8] : vec3(0.0));
            u -= n * dot(u, n);
            float y = length(u), eps = PF[7];
            float s = y > 1e-20 ? fr1(y, eps) / y : 2.0 / eps;
            g += u * (FLAM[i] * s);
            int dk = DIAG[i];
            mat3 blk = LDM(K, dk) + (FLAM[i] * s) * (mat3(1.0) - outerProduct(n, n));
            STM(K, dk, blk);
        }
    }
    vec3 rhs = W[i] == 0.0 ? vec3(0.0) : -((LD3(U, i) - LD3(UT, i)) * (M[i] * PF[1]) + g);
    mat3 f = mat3(0.0);
    bool act = false;
    if (W[i] != 0.0) {
        f = mat3(1.0);
        vec3 n = LD3(CN, i);
        if (CPHI[i] < PF[6] && dot(rhs, n) < 0.0) { f -= outerProduct(n, n); act = true; }
    }
    STM(FILT, i, f);
    RF[i] = act ? -dot(rhs, LD3(CN, i)) : 0.0;
    ST3(RHS, i, rhs);
}
)GLSL"},
    // CG start: block-Jacobi inverses, r = F b, z = F P r, p = z, x = 0; partial sums (r.r, r.z) into region 1.
    // The CG is the pipelined form: Ap is carried by the recurrence Ap = A z + beta Ap, so an iteration is two dispatches
    // (cgUpd, cgP) and needs no neighbour's p (fl::cgBlockJacobiFloat computes A p directly; same algorithm otherwise).
    {"cgInit", 64, R"GLSL(
void main() {
    int i = int(gl_GlobalInvocationID.x);
    float a = 0.0, b = 0.0;
    if (i < pc.x) {
        mat3 pi = inv3(LDM(K, DIAG[i]) + mat3(M[i] * PF[1]));
        STM(PINV, i, pi);
        mat3 f = LDM(FILT, i);
        vec3 r = f * LD3(RHS, i);
        vec3 z = f * (pi * r);
        ST3(R, i, r); ST3(Z, i, z); ST3(P, i, z); ST3(DX, i, vec3(0.0));
        a = dot(r, r); b = dot(r, z);
    }
    wgSum2(1, a, b);
}
)GLSL"},
    // Ap = F A p for the first iteration, partial sums of p.Ap into region 0; |b| and the empty-system stop
    {"cgAp0", 64, R"GLSL(
void main() {
    int i = int(gl_GlobalInvocationID.x);
    vec2 s = sumPart(1, pc.y);
    if (i == 0) { SC[1] = sqrt(s.x); FL[0] = sqrt(s.x) > 1e-30 ? 0 : 1; }
    float a = 0.0;
    if (i < pc.x) {
        vec3 acc = LD3(P, i) * (M[i] * PF[1]);
        for (int k = ROW[i]; k < ROW[i + 1]; ++k) acc += LDM(K, k) * LD3(P, COLI[k]);
        vec3 ap = LDM(FILT, i) * acc;
        ST3(AP, i, ap);
        a = dot(LD3(P, i), ap);
    }
    wgSum2(0, a, 0.0);
}
)GLSL"},
    // iteration pc.z: alpha = r.z / p.Ap; x += alpha p, r -= alpha Ap, z = F P r; partial sums (r.r, r.z) into the other
    // ping-pong region
    {"cgUpd", 64, R"GLSL(
void main() {
    if (FL[0] != 0) return;
    int i = int(gl_GlobalInvocationID.x);
    int q = pc.z & 1;
    float rz = sumPart(1 + q, pc.y).y;
    float pap = sumPart(0, pc.y).x;
    if (!(pap > 0.0)) { if (i == 0) FL[0] = 1; return; }
    float al = rz / pap;
    if (i == 0) FL[2] += 1;
    float a = 0.0, b = 0.0;
    if (i < pc.x) {
        ST3(DX, i, LD3(DX, i) + LD3(P, i) * al);
        vec3 r = LD3(R, i) - LD3(AP, i) * al;
        vec3 z = LDM(FILT, i) * (LDM(PINV, i) * r);
        ST3(R, i, r); ST3(Z, i, z);
        a = dot(r, r); b = dot(r, z);
    }
    wgSum2(2 - q, a, b);
}
)GLSL"},
    // iteration pc.z: stop on the tolerance; else beta = r.z new / old, p = z + beta p, Ap = F A z + beta Ap; partial p.Ap
    {"cgP", 64, R"GLSL(
void main() {
    if (FL[0] != 0) return;
    int i = int(gl_GlobalInvocationID.x);
    int q = pc.z & 1;
    vec2 nw = sumPart(2 - q, pc.y);
    if (sqrt(nw.x) <= PF[10] * SC[1]) { if (i == 0) FL[0] = 1; return; }
    float beta = nw.y / sumPart(1 + q, pc.y).y;
    float a = 0.0;
    if (i < pc.x) {
        vec3 acc = LD3(Z, i) * (M[i] * PF[1]);
        for (int k = ROW[i]; k < ROW[i + 1]; ++k) acc += LDM(K, k) * LD3(Z, COLI[k]);
        vec3 p = LD3(Z, i) + LD3(P, i) * beta;
        vec3 ap = LDM(FILT, i) * acc + LD3(AP, i) * beta;
        ST3(P, i, p);
        ST3(AP, i, ap);
        a = dot(p, ap);
    }
    wgSum2(0, a, 0.0);
}
)GLSL"},
    {"lsBegin", 64, R"GLSL(
void main() {
    int i = int(gl_GlobalInvocationID.x);
    if (i == 0) FL[1] = 0;
    if (i >= pc.x) return;
    ST3(UB, i, LD3(U, i));
}
)GLSL"},
    // variational energy (variationalEnergy + frictionEnergy), partial sums into region pc.y (3: before the line search,
    // 4-5: trials); items: vertices, triangles, hinges, seam links
    {"energy", 64, R"GLSL(
void main() {
    if (pc.y != 3 && FL[1] != 0) return;
    int e = int(gl_GlobalInvocationID.x);
    int n = PQ[0], nt = PQ[1], nb = PQ[2];
    float en = 0.0;
    if (e < pc.x) {
        if (e < n) {
            if (W[e] != 0.0) {
                vec3 du = LD3(U, e) - LD3(UT, e);
                en = 0.5 * M[e] * PF[1] * dot(du, du);
                if (PQ[17] != 0 && FON[e] != 0) {
                    vec3 nn = LD3(FN, e);
                    vec3 u = LD3(U, e) - (PQ[16] != 0 ? LD3(MOT, e) * PF[8] : vec3(0.0));
                    u -= nn * dot(u, nn);
                    en += FLAM[e] * fr0(length(u), PF[7]);
                }
            }
        } else if (e < n + nt) {
            int t = e - n;
            int i0 = TRI[3 * t], i1 = TRI[3 * t + 1], i2 = TRI[3 * t + 2];
            int o = 11 * t;
            float a = TPAR[o], b = TPAR[o + 1], c = TPAR[o + 2], d = TPAR[o + 3];
            vec3 d1 = difAt(i0, i1), d2 = difAt(i0, i2);
            vec3 wu = d1 * a + d2 * c, wv = d1 * b + d2 * d;
            float cu = length(wu) - 1.0, cv = length(wv) - 1.0, cs = dot(wu, wv);
            float lockU = TPAR[o + 8], lockV = TPAR[o + 9], ratio = TPAR[o + 10], comp = TPAR[o + 7];
            float ku = 1.0 / TPAR[o + 4], kv = 1.0 / TPAR[o + 5];
            if (lockU > 0.0 && cu > lockU) { float cl = cu - lockU; en += 0.5 * ku * lockU * lockU + ku * lockU * cl + 0.5 * ratio * ku * cl * cl; }
            else en += 0.5 * (cu < 0.0 ? comp : 1.0) * ku * cu * cu;
            if (lockV > 0.0 && cv > lockV) { float cl = cv - lockV; en += 0.5 * kv * lockV * lockV + kv * lockV * cl + 0.5 * ratio * kv * cl * cl; }
            else en += 0.5 * (cv < 0.0 ? comp : 1.0) * kv * cv * cv;
            en += 0.5 / TPAR[o + 6] * cs * cs;
        } else if (e < n + nt + nb) {
            int h = e - n - nt;
            int i0 = BND[4 * h];
            vec3 g0, g1, g2, g3;
            float th = hinge(vec3(0.0), difAt(i0, BND[4 * h + 1]), difAt(i0, BND[4 * h + 2]), difAt(i0, BND[4 * h + 3]), g0, g1, g2, g3);
            if (th >= 0.0) en = 0.5 / BALPHA[h] * th * th;
        } else {
            int s = e - n - nt - nb;
            int ia = SEAM[3 * s];
            float t = SPAR[2 * s];
            vec3 dd = -(difAt(ia, SEAM[3 * s + 1]) * (1.0 - t) + difAt(ia, SEAM[3 * s + 2]) * t);
            float al = SPAR[2 * s + 1];
            en = 0.5 * (al < 0.0 ? PF[24] : PF[5]) / abs(al) * dot(dd, dd);
        }
    }
    wgSum2(pc.y, en, 0.0);
}
)GLSL"},
    // trial pc.y of the line search (pc.z workgroups of energy sums): accept the previous trial if its energy is not above
    // the start; else x = backup + 0.5^trial dx, projected out of the body
    {"lsTrial", 64, R"GLSL(
void main() {
    if (FL[1] != 0) return;
    int i = int(gl_GlobalInvocationID.x);
    int t = pc.y;
    if (t > 0) {
        float e0 = sumPart(3, pc.z).x;
        float e = sumPart(4 + ((t - 1) & 1), pc.z).x;
        if (lsAccept(e, e0)) { if (i == 0) FL[1] = 1; return; }
    }
    if (i >= pc.x) return;
    vec3 u = LD3(UB, i) + LD3(DX, i) * exp2(-float(t));
    if (PQ[19] != 0 && W[i] != 0.0) {
        vec3 n;
        float d = colEval(LD3(XP, i) + u, bodyShift(i, pc.w), n) - RAD[i];
        if (d < 0.0) u -= n * d;
    }
    ST3(U, i, u);
}
)GLSL"},
    // after the last trial pc.y: keep it if accepted, else go back to the start of the line search
    {"lsEnd", 64, R"GLSL(
void main() {
    if (FL[1] != 0) return;
    int i = int(gl_GlobalInvocationID.x);
    float e0 = sumPart(3, pc.z).x;
    float e = sumPart(4 + (pc.y & 1), pc.z).x;
    if (lsAccept(e, e0)) { if (i == 0) FL[1] = 1; return; }
    if (i == 0) FL[3] += 1;
    if (i >= pc.x) return;
    ST3(U, i, LD3(UB, i));
}
)GLSL"},
    // one colour of strain-limit edges (limitStrain): pc.x edges from pc.y
    {"strain", 64, R"GLSL(
void main() {
    int k = int(gl_GlobalInvocationID.x);
    if (k >= pc.x) return;
    int e = pc.y + k;
    int a = EA[e], b = EB[e];
    float wa = W[a], wb = W[b];
    if (wa + wb == 0.0) return;
    vec3 d = difAt(a, b);
    float l = length(d), lmax = PF[9] * EL0[e];
    if (l <= lmax) return;
    vec3 c = d * ((l - lmax) / (l * (wa + wb)));
    ST3(U, a, LD3(U, a) + c * wa);
    ST3(U, b, LD3(U, b) - c * wb);
}
)GLSL"},
    // push-out after the solve (solveBody; friction mode 2 is inside the solve)
    {"body", 64, R"GLSL(
void main() {
    int i = int(gl_GlobalInvocationID.x);
    if (i >= pc.x || PQ[19] == 0 || PQ[18] == 0 || W[i] == 0.0) return;
    vec3 n;
    float d = colEval(posAt(i), bodyShift(i, pc.w), n) - RAD[i];
    if (d < 0.0) ST3(U, i, LD3(U, i) - n * d);
}
)GLSL"},
    // self collision (solveSelfCollision), Jacobi form: triangles are counted into the hash cells their padded box covers,
    // the counts scanned to starts, the triangles filled in; then every vertex tests the triangles of its own cell and
    // adds its corrections (and the triangle corners') into fixed-point accumulators (1e-9 m units); selfApply moves each
    // vertex by the average of what it received. Detect + apply run in rounds (selfRounds) on the same hash. The CPU
    // applies the same corrections one at a time (Gauss-Seidel).
    {"selfClear", 64, R"GLSL(
void main() {
    int i = int(gl_GlobalInvocationID.x);
    if (i >= pc.x) return;
    if (i <= PQ[22]) HCNT[i] = 0;
    if (i < PQ[0]) { ACC[4 * i] = 0; ACC[4 * i + 1] = 0; ACC[4 * i + 2] = 0; ACC[4 * i + 3] = 0; }
}
)GLSL"},
    {"selfCount", 64, R"GLSL(
void main() {
    int t = int(gl_GlobalInvocationID.x);
    if (t >= pc.x) return;
    vec3 a = posAt(TRI[3 * t]), b = posAt(TRI[3 * t + 1]), c = posAt(TRI[3 * t + 2]);
    float pad = PF[23], inv = PF[22];
    ivec3 lo = ivec3(floor((min(a, min(b, c)) - pad) * inv)), hi = ivec3(floor((max(a, max(b, c)) + pad) * inv));
    ivec3 ext = hi - lo + 1;
    if (ext.x * ext.y * ext.z > 27) return;  // a diverged triangle (the CPU skips them too)
    uint mask = uint(PQ[22] - 1);
    for (int x = lo.x; x <= hi.x; ++x)
        for (int y = lo.y; y <= hi.y; ++y)
            for (int z = lo.z; z <= hi.z; ++z)
                atomicAdd(HCNT[int(((uint(x) * 73856093u) ^ (uint(y) * 19349663u) ^ (uint(z) * 83492791u)) & mask)], 1);
}
)GLSL"},
    // exclusive scan of the counts (one workgroup): starts and fill cursors
    {"selfScan", 64, R"GLSL(
shared int part[64];
void main() {
    int t = int(gl_LocalInvocationID.x);
    int n = PQ[22];
    int chunk = (n + 63) / 64;
    int b = t * chunk, e = min(n, b + chunk);
    int s = 0;
    for (int k = b; k < e; ++k) s += HCNT[k];
    part[t] = s;
    barrier();
    if (t == 0) {
        int run = 0;
        for (int k = 0; k < 64; ++k) { int v = part[k]; part[k] = run; run += v; }
        HST[n] = run;
    }
    barrier();
    int run = part[t];
    for (int k = b; k < e; ++k) { HST[k] = run; HCUR[k] = run; run += HCNT[k]; }
}
)GLSL"},
    {"selfFill", 64, R"GLSL(
void main() {
    int t = int(gl_GlobalInvocationID.x);
    if (t >= pc.x) return;
    vec3 a = posAt(TRI[3 * t]), b = posAt(TRI[3 * t + 1]), c = posAt(TRI[3 * t + 2]);
    float pad = PF[23], inv = PF[22];
    ivec3 lo = ivec3(floor((min(a, min(b, c)) - pad) * inv)), hi = ivec3(floor((max(a, max(b, c)) + pad) * inv));
    ivec3 ext = hi - lo + 1;
    if (ext.x * ext.y * ext.z > 27) return;
    uint mask = uint(PQ[22] - 1);
    for (int x = lo.x; x <= hi.x; ++x)
        for (int y = lo.y; y <= hi.y; ++y)
            for (int z = lo.z; z <= hi.z; ++z) {
                int slot = atomicAdd(HCUR[int(((uint(x) * 73856093u) ^ (uint(y) * 19349663u) ^ (uint(z) * 83492791u)) & mask)], 1);
                if (slot < PQ[23]) HITEM[slot] = t;
            }
}
)GLSL"},
    {"selfDetect", 64, R"GLSL(
// closest point on triangle (a, b, c) to the origin (Ericson 5.1.5, as closestOnTriangle), barycentrics in bc
vec3 closest0(vec3 a, vec3 b, vec3 c, out vec3 bc) {
    vec3 ab = b - a, ac = c - a, ap = -a;
    float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) { bc = vec3(1, 0, 0); return a; }
    vec3 bp = -b;
    float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) { bc = vec3(0, 1, 0); return b; }
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) { float v = d1 / (d1 - d3); bc = vec3(1.0 - v, v, 0); return a + ab * v; }
    vec3 cp = -c;
    float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) { bc = vec3(0, 0, 1); return c; }
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) { float w = d2 / (d2 - d6); bc = vec3(1.0 - w, 0, w); return a + ac * w; }
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) { float w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); bc = vec3(0, 1.0 - w, w); return b + (c - b) * w; }
    float den = 1.0 / (va + vb + vc);
    float v = vb * den, w = vc * den;
    bc = vec3(1.0 - v - w, v, w);
    return a + ab * v + ac * w;
}
void push(int k, vec3 d) {
    ivec3 q = ivec3(round(d * 1e9));
    atomicAdd(ACC[4 * k], q.x); atomicAdd(ACC[4 * k + 1], q.y); atomicAdd(ACC[4 * k + 2], q.z); atomicAdd(ACC[4 * k + 3], 1);
}
void main() {
    int i = int(gl_GlobalInvocationID.x);
    if (i >= pc.x) return;
    float th = PF[23];
    vec3 pi = posAt(i);
    ivec3 cl = ivec3(floor(pi * PF[22]));
    uint mask = uint(PQ[22] - 1);
    int key = int(((uint(cl.x) * 73856093u) ^ (uint(cl.y) * 19349663u) ^ (uint(cl.z) * 83492791u)) & mask);
    int qe = min(HST[key + 1], PQ[23]);
    for (int q = HST[key]; q < qe; ++q) {
        int t = HITEM[q];
        int ta = TRI[3 * t], tb = TRI[3 * t + 1], tc = TRI[3 * t + 2];
        if (ta == i || tb == i || tc == i) continue;
        // corners relative to the vertex (differences of positions taken in parts, see difAt)
        vec3 a = difAt(i, ta), b = difAt(i, tb), c = difAt(i, tc);
        vec3 bc;
        vec3 cq = closest0(a, b, c, bc);
        vec3 diff = -cq;
        float dist = length(diff);
        if (dist >= th) continue;
        float wp = W[i], wa = W[ta], wb = W[tb], wc = W[tc];
        float den = wp + bc.x * bc.x * wa + bc.y * bc.y * wb + bc.z * bc.z * wc;
        if (den <= 0.0) continue;
        bool excluded = false;
        for (int k = NCS[i]; k < NCS[i + 1]; ++k) if (NCT[k] == t) { excluded = true; break; }
        if (excluded) continue;
        vec3 n;
        float amount;
        if (bc.x > 1e-6 && bc.y > 1e-6 && bc.z > 1e-6) {
            vec3 cr = cross(b - a, c - a);
            if (dot(cr, cr) == 0.0) continue;
            n = normalize(cr);
            float s = dot(diff, n);
            // which side the vertex was on at the start of the substep: keep it there
            vec3 xa = LD3(XP, ta) - LD3(XP, i), xb = LD3(XP, tb) - LD3(XP, i), xc = LD3(XP, tc) - LD3(XP, i);
            vec3 np = normalize(cross(xb - xa, xc - xa));
            vec3 qp = xa * bc.x + xb * bc.y + xc * bc.z;
            float side = dot(-qp, np) >= 0.0 ? 1.0 : -1.0;
            if (side * s >= th) continue;
            amount = side * th - s;
        } else {
            if (dist < 1e-12) continue;
            n = diff / dist;
            amount = th - dist;
        }
        float lam = amount / den;
        if (wp > 0.0) push(i, n * (wp * lam));
        if (wa > 0.0) push(ta, -n * (bc.x * wa * lam));
        if (wb > 0.0) push(tb, -n * (bc.y * wb * lam));
        if (wc > 0.0) push(tc, -n * (bc.z * wc * lam));
    }
}
)GLSL"},
    // each vertex moves by the average correction it received, the accumulators are cleared for the next round
    {"selfApply", 64, R"GLSL(
void main() {
    int i = int(gl_GlobalInvocationID.x);
    if (i >= pc.x) return;
    int cnt = ACC[4 * i + 3];
    if (cnt == 0) return;
    vec3 d = vec3(float(ACC[4 * i]), float(ACC[4 * i + 1]), float(ACC[4 * i + 2])) * (1e-9 / float(cnt));
    ST3(U, i, LD3(U, i) + d);
    ACC[4 * i] = 0; ACC[4 * i + 1] = 0; ACC[4 * i + 2] = 0; ACC[4 * i + 3] = 0;
}
)GLSL"},
    // velocity from the substep's displacement (with the 50 m/s guard), then the displacement goes into the position
    {"velocity", 64, R"GLSL(
void main() {
    int i = int(gl_GlobalInvocationID.x);
    if (i >= pc.x || W[i] == 0.0) return;
    vec3 u = LD3(U, i);
    vec3 v = u / PF[0];
    float sp = length(v);
    if (sp > 50.0) v *= 50.0 / sp;
    ST3(V, i, v);
    ST3(XP, i, LD3(XP, i) + u);
    ST3(U, i, vec3(0.0));
}
)GLSL"},
};

PackedByteArray bytesOf(const void* p, size_t n) {
    PackedByteArray b;
    b.resize(static_cast<int64_t>(n));
    if (n) std::memcpy(b.ptrw(), p, n);
    return b;
}

std::vector<float> flat3(const std::vector<fl::Vec3>& v) {
    std::vector<float> o(3 * v.size());
    for (size_t i = 0; i < v.size(); ++i) { o[3 * i] = static_cast<float>(v[i].x); o[3 * i + 1] = static_cast<float>(v[i].y); o[3 * i + 2] = static_cast<float>(v[i].z); }
    return o;
}

}  // namespace

GpuStep::~GpuStep() {
    if (!rd_) return;
    freeAll();
    memdelete(rd_);
    rd_ = nullptr;
}

void GpuStep::freeAll() {
    for (auto& kv : k_) {
        Kernel& k = kv.second;
        if (k.set.is_valid() && rd_->uniform_set_is_valid(k.set)) rd_->free_rid(k.set);
        if (k.pipeline.is_valid()) rd_->free_rid(k.pipeline);
        if (k.shader.is_valid()) rd_->free_rid(k.shader);
    }
    k_.clear();
    for (auto& kv : bufs_)
        if (kv.second.rid.is_valid()) rd_->free_rid(kv.second.rid);
    bufs_.clear();
}

std::string GpuStep::deviceName() const {
    if (!rd_) return "";
    return std::string(rd_->get_device_name().utf8().get_data());
}

bool GpuStep::ready() {
    if (!tried_) {
        tried_ = true;
        if (!init()) {
            if (rd_) { freeAll(); memdelete(rd_); rd_ = nullptr; }
            return false;
        }
    }
    return rd_ != nullptr;
}

bool GpuStep::init() {
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) { error_ = "no RenderingServer"; return false; }
    rd_ = rs->create_local_rendering_device();
    if (!rd_) { error_ = "no local RenderingDevice (headless, or the Compatibility renderer: needs Forward+ or Mobile)"; return false; }
    for (const KernelSrc& ks : kKernels) {
        Kernel k;
        k.name = ks.name;
        std::string body = ks.body;
        std::string libs;
        if (body.find("posAt") != std::string::npos || body.find("difAt") != std::string::npos || body.find("colEval") != std::string::npos) libs += kPosLib;
        if (body.find("colEval") != std::string::npos) libs += kSdfLib;
        if (body.find("wgSum2") != std::string::npos || body.find("sumPart") != std::string::npos) libs += kRedLib;
        if (!compile(k, std::string("#define WG ") + std::to_string(ks.wg) + "\n" + libs + "\n" + body)) return false;
        k_[k.name] = std::move(k);
    }
    return true;
}

bool GpuStep::compile(Kernel& k, const std::string& code) {
    // the buffers this kernel mentions, in registry order
    std::set<std::string> words;
    static const std::regex id("[A-Za-z_][A-Za-z0-9_]*");
    for (auto it = std::sregex_iterator(code.begin(), code.end(), id); it != std::sregex_iterator(); ++it) words.insert(it->str());
    std::string decl;
    for (const auto& b : kBuffers) {
        if (!words.count(b.first)) continue;
        const int binding = static_cast<int>(k.bufs.size());
        k.bufs.push_back(b.first);
        decl += "layout(set = 0, binding = " + std::to_string(binding) + ", std430) restrict buffer B_" + b.first + " { " +
                (b.second == 'i' ? "int " : "float ") + b.first + "[]; };\n";
    }
    // "#version" must come first: the prelude starts with it, the WG define and the declarations follow
    std::string pre = kPrelude;
    const size_t eol = pre.find('\n', pre.find("#version"));
    const size_t wgDef = code.find('\n');
    const std::string src = pre.substr(0, eol + 1) + code.substr(0, wgDef + 1) + pre.substr(eol + 1) + decl + code.substr(wgDef + 1);
    Ref<RDShaderSource> s;
    s.instantiate();
    s->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
    s->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, String(src.c_str()));
    Ref<RDShaderSPIRV> spirv = rd_->shader_compile_spirv_from_source(s, false);
    if (spirv.is_null()) { error_ = "kernel " + k.name + ": compile returned nothing"; return false; }
    const String err = spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!err.is_empty()) { error_ = "kernel " + k.name + ": " + std::string(err.utf8().get_data()); return false; }
    k.shader = rd_->shader_create_from_spirv(spirv, String(("fl_" + k.name).c_str()));
    if (!k.shader.is_valid()) { error_ = "kernel " + k.name + ": shader_create_from_spirv failed"; return false; }
    k.pipeline = rd_->compute_pipeline_create(k.shader);
    if (!k.pipeline.is_valid()) { error_ = "kernel " + k.name + ": compute_pipeline_create failed"; return false; }
    return true;
}

bool GpuStep::buf(const std::string& name, size_t bytes, char type) {
    bytes = std::max<size_t>(bytes, 16);
    Buf& b = bufs_[name];
    b.type = type;
    if (b.rid.is_valid() && b.bytes >= bytes) return true;
    if (b.rid.is_valid()) rd_->free_rid(b.rid);
    b.bytes = bytes + bytes / 4 + 64;
    b.bytes = (b.bytes + 15) / 16 * 16;
    b.rid = rd_->storage_buffer_create(static_cast<uint32_t>(b.bytes));
    ++bufVersion_;
    if (!b.rid.is_valid()) { error_ = "storage_buffer_create failed for " + name; return false; }
    return true;
}

bool GpuStep::upload(const std::string& name, const void* data, size_t bytes) {
    const char type = bufs_.count(name) ? bufs_[name].type : 'f';
    if (!buf(name, bytes, type)) return false;
    if (bytes) rd_->buffer_update(bufs_[name].rid, 0, static_cast<uint32_t>(bytes), bytesOf(data, bytes));
    return true;
}

bool GpuStep::bindSet(Kernel& k) {
    if (k.setVersion == bufVersion_ && k.set.is_valid()) return true;
    if (k.set.is_valid() && rd_->uniform_set_is_valid(k.set)) rd_->free_rid(k.set);
    TypedArray<RDUniform> us;
    for (size_t b = 0; b < k.bufs.size(); ++b) {
        auto it = bufs_.find(k.bufs[b]);
        if (it == bufs_.end() || !it->second.rid.is_valid()) { error_ = "kernel " + k.name + ": buffer " + k.bufs[b] + " missing"; return false; }
        Ref<RDUniform> u;
        u.instantiate();
        u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
        u->set_binding(static_cast<int32_t>(b));
        u->add_id(it->second.rid);
        us.push_back(u);
    }
    k.set = rd_->uniform_set_create(us, k.shader, 0);
    k.setVersion = bufVersion_;
    if (!k.set.is_valid()) { error_ = "kernel " + k.name + ": uniform_set_create failed"; return false; }
    return true;
}

void GpuStep::dispatch(int64_t cl, Kernel& k, int items, int a, int b, int c) {
    const int32_t pc[4] = {items, a, b, c};
    const int groups = (items + 63) / 64;
    if (groups <= 0) return;
    rd_->compute_list_bind_compute_pipeline(cl, k.pipeline);
    rd_->compute_list_bind_uniform_set(cl, k.set, 0);
    rd_->compute_list_set_push_constant(cl, bytesOf(pc, sizeof(pc)), sizeof(pc));
    rd_->compute_list_dispatch(cl, static_cast<uint32_t>(groups), 1, 1);
    rd_->compute_list_add_barrier(cl);
    ++dispatches_;
}

bool GpuStep::uploadModel(fl::ClothSolver& s) {
    if (modelOf_ == &s && modelVersion_ == s.modelVersion()) return model_.unsupported.empty();
    model_ = s.gpuModel();
    modelOf_ = &s;
    modelVersion_ = s.modelVersion();
    if (!model_.unsupported.empty()) return false;
    const fl::GpuModel& m = model_;
    const size_t N = static_cast<size_t>(m.n);
    bool ok = true;
    auto upF = [&](const char* n, const std::vector<float>& v) { bufs_[n].type = 'f'; ok = ok && upload(n, v.data(), 4 * v.size()); };
    auto upI = [&](const char* n, const std::vector<int>& v) { bufs_[n].type = 'i'; ok = ok && upload(n, v.data(), 4 * v.size()); };
    auto mk = [&](const char* n, size_t floats, char t) { ok = ok && buf(n, 4 * floats, t); };
    upF("W", m.w); upF("M", m.mass); upF("DAMP", m.damp); upF("MUK", m.muK); upF("RAD", m.radius);
    upI("TRI", m.triIdx); upF("TPAR", m.triPar); upI("BND", m.bendIdx); upF("BALPHA", m.bendAlpha);
    upI("SEAM", m.seamIdx); upF("SPAR", m.seamPar);
    upI("BLKS", m.blkStart); upI("BLKI", m.blkItem); upI("GST", m.gStart); upI("GIT", m.gItem);
    upI("ROW", m.rowStart); upI("COLI", m.col); upI("DIAG", m.diag);
    upI("EA", m.edgeA); upI("EB", m.edgeB); upF("EL0", m.edgeL0);
    upI("NCS", m.ncStart); upI("NCT", m.ncTri);
    mk("HCNT", static_cast<size_t>(m.hashTable) + 1, 'i');
    mk("HST", static_cast<size_t>(m.hashTable) + 1, 'i');
    mk("HCUR", static_cast<size_t>(m.hashTable) + 1, 'i');
    hashCap_ = 27 * std::max(1, m.nTri);
    mk("HITEM", static_cast<size_t>(hashCap_), 'i');
    mk("ACC", 4 * N, 'i');
    for (const char* n : {"XP", "U", "V", "UT", "UB", "DX", "CN", "FN", "MOT", "RHS", "R", "Z", "P", "AP"}) mk(n, 3 * N, 'f');
    for (const char* n : {"CPHI", "RF", "FLAM"}) mk(n, N, 'f');
    mk("FON", N, 'i');
    mk("FILT", 9 * N, 'f');
    mk("PINV", 9 * N, 'f');
    mk("K", 9 * static_cast<size_t>(m.nnz), 'f');
    mk("HS", 9 * (9 * static_cast<size_t>(m.nTri) + 16 * static_cast<size_t>(m.nBend) + 9 * static_cast<size_t>(m.nSeam)), 'f');
    mk("GS", 3 * (3 * static_cast<size_t>(m.nTri) + 4 * static_cast<size_t>(m.nBend) + 3 * static_cast<size_t>(m.nSeam)), 'f');
    groupsMax_ = (m.n + m.nTri + m.nBend + m.nSeam + 63) / 64 + 1;
    mk("PART", 6 * 2 * static_cast<size_t>(groupsMax_), 'f');
    mk("SC", 16, 'f');
    mk("FL", 16, 'i');
    mk("PF", 32, 'f');
    mk("PQ", 32, 'i');
    mk("CL", 16, 'f');
    if (!bufs_.count("BRICK")) { mk("BRICK", 4, 'i'); mk("FINE", 4, 'f'); mk("COARSE", 4, 'f'); }
    if (!ok) { modelOf_ = nullptr; return false; }
    return true;
}

bool GpuStep::uploadGrid(const fl::SdfGrid& g) {
    const fl::SdfGrid::Raw r = g.raw();
    bufs_["BRICK"].type = 'i';
    bufs_["FINE"].type = 'f';
    bufs_["COARSE"].type = 'f';
    return upload("BRICK", r.brickOf->data(), 4 * r.brickOf->size()) && upload("FINE", r.fine->data(), 4 * r.fine->size()) &&
           upload("COARSE", r.coarse->data(), 4 * r.coarse->size());
}

bool GpuStep::stepFrame(fl::ClothSolver& s, const Scene& scene) {
    if (!ready()) { reason_ = error_; return false; }
    if (!scene.unsupported.empty()) { reason_ = scene.unsupported; return false; }
    const fl::SolverParams& p = s.params();
    if (s.vertexCount() == 0) { reason_ = "no cloth"; return false; }
    const auto t0 = std::chrono::steady_clock::now();
    if (!uploadModel(s)) { reason_ = model_.unsupported.empty() ? error_ : model_.unsupported; return false; }
    const fl::GpuModel& m = model_;
    const int N = m.n;

    // collision scene
    if (scene.grid != gridHeld_) {
        if (scene.grid && !scene.grid->empty() && !uploadGrid(*scene.grid)) { reason_ = error_; return false; }
        gridHeld_ = scene.grid;
    }
    const bool hasGrid = scene.grid && !scene.grid->empty();
    std::vector<float> cl;
    for (size_t k = 0; k + 1 < scene.planes.size(); k += 2)
        for (const fl::Vec3& v : {scene.planes[k], scene.planes[k + 1]}) { cl.push_back(static_cast<float>(v.x)); cl.push_back(static_cast<float>(v.y)); cl.push_back(static_cast<float>(v.z)); }
    for (fl::Real v : scene.spheres) cl.push_back(static_cast<float>(v));
    for (fl::Real v : scene.boxes) cl.push_back(static_cast<float>(v));
    const int nPlanes = static_cast<int>(scene.planes.size() / 2), nSpheres = static_cast<int>(scene.spheres.size() / 4),
              nBoxes = static_cast<int>(scene.boxes.size() / 15);
    const bool anyCollider = hasGrid || nPlanes || nSpheres || nBoxes;

    // parameters
    const int S = std::max(1, p.substeps);
    const double h = p.dt / S;
    float pf[32] = {0};
    int pq[32] = {0};
    pf[0] = static_cast<float>(h);
    pf[1] = static_cast<float>(1.0 / (h * h));
    pf[2] = static_cast<float>(p.gravity.x); pf[3] = static_cast<float>(p.gravity.y); pf[4] = static_cast<float>(p.gravity.z);
    pf[5] = static_cast<float>(s.seamScaleNow());
    pf[24] = static_cast<float>(s.pointSeamScaleNow());   // point seams (tacks): not switched off by seam_scale
    pf[6] = static_cast<float>(p.contactBand);
    pf[7] = static_cast<float>(p.frictionSlipVelocity * h);
    pf[8] = static_cast<float>(1.0 / S);
    pf[9] = static_cast<float>(1.0 + p.strainLimit);
    pf[10] = static_cast<float>(p.cgTolerance);
    pf[11] = 1e-6f;
    pq[0] = N; pq[1] = m.nTri; pq[2] = m.nBend; pq[3] = m.nSeam; pq[4] = m.nnz;
    pq[5] = s.gpuTension() ? 1 : 0;
    if (hasGrid) {
        const fl::SdfGrid::Raw r = scene.grid->raw();
        pf[12] = static_cast<float>(r.fo.x); pf[13] = static_cast<float>(r.fo.y); pf[14] = static_cast<float>(r.fo.z);
        pf[15] = static_cast<float>(r.fh); pf[16] = static_cast<float>(r.finv);
        pf[17] = static_cast<float>(r.co.x); pf[18] = static_cast<float>(r.co.y); pf[19] = static_cast<float>(r.co.z);
        pf[20] = static_cast<float>(r.ch); pf[21] = static_cast<float>(r.cinv);
        pq[6] = 1; pq[7] = r.bnx; pq[8] = r.bny; pq[9] = r.bnz; pq[10] = r.cnx; pq[11] = r.cny; pq[12] = r.cnz;
    }
    pq[13] = nPlanes; pq[14] = nSpheres; pq[15] = nBoxes;
    const std::vector<fl::Vec3>& mot = s.colliderMotion();
    pq[16] = static_cast<int>(mot.size()) == N ? 1 : 0;
    pq[17] = (p.bodyFrictionMode == 2 && anyCollider) ? 1 : 0;
    pq[18] = p.bodyPushAfterSolve ? 1 : 0;
    pq[19] = anyCollider ? 1 : 0;
    pq[20] = 2 * groupsMax_;
    pq[21] = p.selfCollision ? 1 : 0;
    pq[22] = m.hashTable;
    pq[23] = hashCap_;
    pf[22] = 1.0f / m.hashCell;
    pf[23] = static_cast<float>(p.selfThickness);
    // Jacobi rounds of self collision. Two layers dropped on a sphere (gpu_step_check scene 4, 90 frames): upper-layer
    // vertices under the lower layer / kinetic energy at the end: 2 rounds 107 / -, 4 rounds 3 / 4e-4 J, 8 rounds 20 /
    // 0.026 J (jitter); summed instead of averaged corrections 1 / 0.01 J (jitter). The CPU (Gauss-Seidel): 0 / 1e-4 J.
    const int selfRounds = 4;

    // state
    std::vector<fl::Vec3> x, v, nrm;
    std::vector<fl::Real> force, phi;
    s.gpuState(x, v, force, phi, nrm);
    std::vector<float> ff(N), pp(N);
    for (int i = 0; i < N; ++i) { ff[i] = static_cast<float>(force[i]); pp[i] = static_cast<float>(std::min<fl::Real>(phi[i], 1e30)); }
    const int zeros[16] = {0};
    const float fzeros[16] = {0};
    bool ok = upload("PF", pf, sizeof(pf)) && upload("PQ", pq, sizeof(pq)) && upload("CL", cl.data(), 4 * cl.size()) &&
              upload("XP", flat3(x).data(), 12 * static_cast<size_t>(N)) && upload("V", flat3(v).data(), 12 * static_cast<size_t>(N)) &&
              upload("RF", ff.data(), 4 * static_cast<size_t>(N)) && upload("CPHI", pp.data(), 4 * static_cast<size_t>(N)) &&
              upload("CN", flat3(nrm).data(), 12 * static_cast<size_t>(N)) && upload("FL", zeros, sizeof(zeros)) &&
              upload("SC", fzeros, sizeof(fzeros));
    if (ok && pq[16]) ok = upload("MOT", flat3(mot).data(), 12 * static_cast<size_t>(N));
    if (!ok) { reason_ = error_; return false; }
    for (auto& kv : k_)
        if (!bindSet(kv.second)) { reason_ = error_; return false; }

    const auto tUp = std::chrono::steady_clock::now();
    // record the frame
    dispatches_ = 0;
    const int E = m.nTri + m.nBend + m.nSeam;
    const int gN = (N + 63) / 64, gAll = (N + E + 63) / 64;
    const int64_t c = rd_->compute_list_begin();
    for (int sub = 0; sub < S; ++sub) {
        dispatch(c, k_["predict"], N);
        if (pq[17]) dispatch(c, k_["fricLag"], N);
        dispatch(c, k_["contact"], N, 0, 0, sub);
        dispatch(c, k_["elem"], E);
        dispatch(c, k_["gatherK"], m.nnz);
        dispatch(c, k_["vertex"], N);
        dispatch(c, k_["cgInit"], N);
        dispatch(c, k_["cgAp0"], N, gN);
        for (int it = 0; it < std::max(1, p.cgMaxIterations); ++it) {
            dispatch(c, k_["cgUpd"], N, gN, it);
            dispatch(c, k_["cgP"], N, gN, it);
        }
        dispatch(c, k_["lsBegin"], N);
        dispatch(c, k_["energy"], N + E, 3);
        const int trials = 10;
        for (int t = 0; t < trials; ++t) {
            dispatch(c, k_["lsTrial"], N, t, gAll, sub);
            dispatch(c, k_["energy"], N + E, 4 + (t & 1));
        }
        dispatch(c, k_["lsEnd"], N, trials - 1, gAll);
        if (p.strainLimit > 0)
            for (int pass = 0; pass < std::max(1, p.strainLimitPasses); ++pass)
                for (size_t col = 0; col + 1 < m.colStart.size(); ++col)
                    dispatch(c, k_["strain"], m.colStart[col + 1] - m.colStart[col], m.colStart[col]);
        dispatch(c, k_["body"], N, 0, 0, sub);
        if (p.selfCollision) {
            dispatch(c, k_["selfClear"], std::max(m.hashTable + 1, N));
            dispatch(c, k_["selfCount"], m.nTri);
            dispatch(c, k_["selfScan"], 64);
            dispatch(c, k_["selfFill"], m.nTri);
            for (int round = 0; round < selfRounds; ++round) {
                dispatch(c, k_["selfDetect"], N);
                dispatch(c, k_["selfApply"], N);
            }
            dispatch(c, k_["body"], N, 0, 0, sub);
        }
        dispatch(c, k_["velocity"], N);
    }
    rd_->compute_list_end();
    // The results are gathered into one buffer and read back once: every buffer_get_data waits for the device on its
    // own, and six of them cost about 120 ms a frame on the M3 while the editor draws (live drape, 2026-10-29).
    const size_t n12 = 12 * static_cast<size_t>(N), n4 = 4 * static_cast<size_t>(N);
    const size_t oX = 0, oV = n12, oF = 2 * n12, oP = 2 * n12 + n4, oN = 2 * n12 + 2 * n4, oFl = 3 * n12 + 2 * n4;
    const size_t rbBytes = oFl + 16;
    if (!buf("RB", rbBytes, 'f')) return false;
    {
        const RID rb = bufs_["RB"].rid;
        const struct { const char* n; size_t off, bytes; } parts[] = {
            {"XP", oX, n12}, {"V", oV, n12}, {"RF", oF, n4}, {"CPHI", oP, n4}, {"CN", oN, n12}, {"FL", oFl, 16}};
        for (const auto& pt : parts)
            if (rd_->buffer_copy(bufs_[pt.n].rid, rb, 0, static_cast<uint32_t>(pt.off), static_cast<uint32_t>(pt.bytes)) != godot::OK) {
                error_ = reason_ = std::string("buffer_copy failed for ") + pt.n;
                return false;
            }
    }
    const auto t1 = std::chrono::steady_clock::now();
    // No submit() + sync(): buffer_get_data submits everything recorded and waits for it itself, so the frame costs one
    // trip to the device instead of two (each trip queues behind the editor's own drawing). lastGpuMs_ is then 0 and
    // lastReadMs_ holds the GPU time too.
    const auto t2 = t1;

    // read back
    const PackedByteArray rbd = rd_->buffer_get_data(bufs_["RB"].rid, 0, static_cast<uint32_t>(rbBytes));
    if (static_cast<size_t>(rbd.size()) < rbBytes) {
        error_ = reason_ = "buffer_get_data returned too little";
        return false;
    }
    const uint8_t* rbp = rbd.ptr();
    const float* fx = reinterpret_cast<const float*>(rbp + oX);
    const float* fv = reinterpret_cast<const float*>(rbp + oV);
    const float* fF = reinterpret_cast<const float*>(rbp + oF);
    const float* fP = reinterpret_cast<const float*>(rbp + oP);
    const float* fN = reinterpret_cast<const float*>(rbp + oN);
    for (int i = 0; i < N; ++i) {
        x[i] = fl::Vec3(fx[3 * i], fx[3 * i + 1], fx[3 * i + 2]);
        v[i] = fl::Vec3(fv[3 * i], fv[3 * i + 1], fv[3 * i + 2]);
        force[i] = fF[i];
        phi[i] = fP[i] >= 1e29f ? 1e30 : fP[i];
        nrm[i] = fl::Vec3(fN[3 * i], fN[3 * i + 1], fN[3 * i + 2]);
    }
    int fl4[4];
    std::memcpy(fl4, rbp + oFl, sizeof(fl4));
    lastCgIts_ = fl4[2];
    lastLsRejects_ = fl4[3];
    s.gpuEndFrame(x, v, force, phi, nrm);
    lastDispatches_ = dispatches_;
    lastGpuMs_ = std::chrono::duration<double, std::milli>(t2 - t1).count();
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    lastUploadMs_ = ms(t0, tUp);
    lastRecordMs_ = ms(tUp, t1);
    lastReadMs_ = ms(t2, std::chrono::steady_clock::now());
    lastMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    ++frames_;
    reason_.clear();
    return true;
}
