#include "gpu_cg.h"

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/typed_array.hpp>

using namespace godot;

namespace {

// The kernel. Buffers are flat float/int arrays (no vec3 in std430: 16-byte alignment). 3x3 blocks are row-major, as
// fl::Mat3. Work buffer: r, z, p, Ap (3 floats per vertex each), then the block-Jacobi inverses (9 per vertex).
// Every thread owns the same vertices (i = tid, tid + 256, ...) for the whole solve, so r, z, x and Ap are only read by
// their owner; p is read by the neighbours in the matrix product and gets a buffer barrier after each update.
// The CG loop is uniform across the workgroup: every branch depends only on workgroup sums.
const char* kShader = R"GLSL(
#version 450
layout(local_size_x = 256) in;
layout(set = 0, binding = 0, std430) restrict readonly buffer RowStart { int rowStart[]; };
layout(set = 0, binding = 1, std430) restrict readonly buffer Col { int col[]; };
layout(set = 0, binding = 2, std430) restrict readonly buffer Diag { int diagIdx[]; };
layout(set = 0, binding = 3, std430) restrict readonly buffer Val { float val[]; };
layout(set = 0, binding = 4, std430) restrict readonly buffer Mass { float massTerm[]; };
layout(set = 0, binding = 5, std430) restrict readonly buffer Filt { float filt[]; };
layout(set = 0, binding = 6, std430) restrict readonly buffer Rhs { float rhs[]; };
layout(set = 0, binding = 7, std430) restrict buffer X { float xo[]; };
layout(set = 0, binding = 8, std430) restrict coherent buffer Work { float w[]; };
layout(set = 0, binding = 9, std430) restrict writeonly buffer Stats { int stats[]; };
layout(push_constant, std430) uniform Params { int n; int maxIt; float tol; int pad; } pc;

shared float red[256];

float wsum(uint tid, float v) {
    red[tid] = v;
    barrier();
    for (uint s = 128u; s > 0u; s >>= 1u) {
        if (tid < s) red[tid] += red[tid + s];
        barrier();
    }
    float r = red[0];
    barrier();
    return r;
}

vec3 mulVal(int k, vec3 v) {
    int b = 9 * k;
    return vec3(val[b] * v.x + val[b + 1] * v.y + val[b + 2] * v.z, val[b + 3] * v.x + val[b + 4] * v.y + val[b + 5] * v.z,
                val[b + 6] * v.x + val[b + 7] * v.y + val[b + 8] * v.z);
}
vec3 mulFilt(uint i, vec3 v) {
    uint b = 9u * i;
    return vec3(filt[b] * v.x + filt[b + 1u] * v.y + filt[b + 2u] * v.z, filt[b + 3u] * v.x + filt[b + 4u] * v.y + filt[b + 5u] * v.z,
                filt[b + 6u] * v.x + filt[b + 7u] * v.y + filt[b + 8u] * v.z);
}
vec3 mulPinv(uint base, vec3 v) {
    return vec3(w[base] * v.x + w[base + 1u] * v.y + w[base + 2u] * v.z, w[base + 3u] * v.x + w[base + 4u] * v.y + w[base + 5u] * v.z,
                w[base + 6u] * v.x + w[base + 7u] * v.y + w[base + 8u] * v.z);
}
vec3 ld(uint off, uint i) { uint b = off + 3u * i; return vec3(w[b], w[b + 1u], w[b + 2u]); }
void st(uint off, uint i, vec3 v) { uint b = off + 3u * i; w[b] = v.x; w[b + 1u] = v.y; w[b + 2u] = v.z; }

void main() {
    uint tid = gl_LocalInvocationID.x;
    uint n = uint(pc.n);
    uint R = 0u, Z = 3u * n, P = 6u * n, AP = 9u * n, PI = 12u * n;
    float lrz = 0.0, lrr = 0.0;
    for (uint i = tid; i < n; i += 256u) {
        // block-Jacobi inverse of (K_ii + m/h^2 I), adjugate as fl::cgBlockJacobiFloat
        int d = 9 * diagIdx[i];
        float m = massTerm[i];
        float a0 = val[d] + m, a1 = val[d + 1], a2 = val[d + 2], a3 = val[d + 3], a4 = val[d + 4] + m, a5 = val[d + 5];
        float a6 = val[d + 6], a7 = val[d + 7], a8 = val[d + 8] + m;
        float c00 = a4 * a8 - a5 * a7, c01 = a5 * a6 - a3 * a8, c02 = a3 * a7 - a4 * a6;
        float det = a0 * c00 + a1 * c01 + a2 * c02;
        float id = det != 0.0 ? 1.0 / det : 0.0;
        uint q = PI + 9u * i;
        w[q] = c00 * id; w[q + 1u] = (a2 * a7 - a1 * a8) * id; w[q + 2u] = (a1 * a5 - a2 * a4) * id;
        w[q + 3u] = c01 * id; w[q + 4u] = (a0 * a8 - a2 * a6) * id; w[q + 5u] = (a2 * a3 - a0 * a5) * id;
        w[q + 6u] = c02 * id; w[q + 7u] = (a1 * a6 - a0 * a7) * id; w[q + 8u] = (a0 * a4 - a1 * a3) * id;
        vec3 r = mulFilt(i, vec3(rhs[3u * i], rhs[3u * i + 1u], rhs[3u * i + 2u]));
        vec3 z = mulFilt(i, mulPinv(q, r));
        st(R, i, r); st(Z, i, z); st(P, i, z);
        xo[3u * i] = 0.0; xo[3u * i + 1u] = 0.0; xo[3u * i + 2u] = 0.0;
        lrz += dot(r, z);
        lrr += dot(r, r);
    }
    float rz = wsum(tid, lrz);
    float bnorm = sqrt(wsum(tid, lrr));
    memoryBarrierBuffer();
    barrier();
    int it = 0;
    if (bnorm > 1e-30) {
        for (; it < pc.maxIt; ++it) {
            float lpap = 0.0;
            for (uint i = tid; i < n; i += 256u) {
                vec3 acc = massTerm[i] * ld(P, i);
                for (int k = rowStart[i]; k < rowStart[i + 1u]; ++k) acc += mulVal(k, ld(P, uint(col[k])));
                vec3 ap = mulFilt(i, acc);
                st(AP, i, ap);
                lpap += dot(ld(P, i), ap);
            }
            float pap = wsum(tid, lpap);
            if (!(pap > 0.0)) break;
            float alpha = rz / pap;
            float lr2 = 0.0;
            for (uint i = tid; i < n; i += 256u) {
                vec3 p = ld(P, i);
                xo[3u * i] += alpha * p.x; xo[3u * i + 1u] += alpha * p.y; xo[3u * i + 2u] += alpha * p.z;
                vec3 r = ld(R, i) - alpha * ld(AP, i);
                st(R, i, r);
                lr2 += dot(r, r);
            }
            float rr = wsum(tid, lr2);
            if (sqrt(rr) <= pc.tol * bnorm) { ++it; break; }
            float lrzn = 0.0;
            for (uint i = tid; i < n; i += 256u) {
                vec3 r = ld(R, i);
                vec3 z = mulFilt(i, mulPinv(PI + 9u * i, r));
                st(Z, i, z);
                lrzn += dot(r, z);
            }
            float rzn = wsum(tid, lrzn);
            float beta = rzn / rz;
            rz = rzn;
            for (uint i = tid; i < n; i += 256u) st(P, i, ld(Z, i) + beta * ld(P, i));
            memoryBarrierBuffer();
            barrier();
        }
    }
    if (tid == 0u) stats[0] = it;
}
)GLSL";

PackedByteArray bytesOf(const void* p, size_t n) {
    PackedByteArray b;
    b.resize(static_cast<int64_t>(n));
    if (n) std::memcpy(b.ptrw(), p, n);
    return b;
}

}  // namespace

GpuCg::~GpuCg() {
    if (!rd_) return;
    freeBuffers();
    if (pipeline_.is_valid()) rd_->free_rid(pipeline_);
    if (shader_.is_valid()) rd_->free_rid(shader_);
    memdelete(rd_);
    rd_ = nullptr;
}

std::string GpuCg::deviceName() const {
    if (!rd_) return "";
    const String s = rd_->get_device_name();
    return std::string(s.utf8().get_data());
}

bool GpuCg::ready() {
    if (!tried_) {
        tried_ = true;
        if (!init()) {
            if (rd_) { memdelete(rd_); rd_ = nullptr; }
            return false;
        }
    }
    return rd_ != nullptr;
}

bool GpuCg::init() {
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) { error_ = "no RenderingServer"; return false; }
    rd_ = rs->create_local_rendering_device();
    if (!rd_) { error_ = "no local RenderingDevice (headless, or the Compatibility renderer: needs Forward+ or Mobile)"; return false; }
    Ref<RDShaderSource> src;
    src.instantiate();
    src->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
    src->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, String(kShader));
    Ref<RDShaderSPIRV> spirv = rd_->shader_compile_spirv_from_source(src, false);
    if (spirv.is_null()) { error_ = "shader compile returned nothing"; return false; }
    const String err = spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!err.is_empty()) { error_ = std::string("shader compile: ") + err.utf8().get_data(); return false; }
    shader_ = rd_->shader_create_from_spirv(spirv, "failoring_cg");
    if (!shader_.is_valid()) { error_ = "shader_create_from_spirv failed"; return false; }
    pipeline_ = rd_->compute_pipeline_create(shader_);
    if (!pipeline_.is_valid()) { error_ = "compute_pipeline_create failed"; return false; }
    return true;
}

void GpuCg::freeBuffers() {
    if (!rd_) return;
    if (set_.is_valid()) rd_->free_rid(set_);
    for (RID* r : {&bRow_, &bCol_, &bDiag_, &bVal_, &bMass_, &bFilt_, &bRhs_, &bX_, &bWork_, &bStats_})
        if (r->is_valid()) { rd_->free_rid(*r); *r = RID(); }
    set_ = RID();
    capN_ = capNnz_ = 0;
}

bool GpuCg::ensureBuffers(int n, int nnz) {
    if (n <= capN_ && nnz <= capNnz_ && set_.is_valid()) return true;
    freeBuffers();
    capN_ = n + n / 4 + 16;
    capNnz_ = nnz + nnz / 4 + 64;
    auto make = [&](size_t bytes) { return rd_->storage_buffer_create(static_cast<uint32_t>(bytes)); };
    bRow_ = make(4 * (capN_ + 1));
    bCol_ = make(4 * capNnz_);
    bDiag_ = make(4 * capN_);
    bVal_ = make(36 * static_cast<size_t>(capNnz_));
    bMass_ = make(4 * capN_);
    bFilt_ = make(36 * static_cast<size_t>(capN_));
    bRhs_ = make(12 * capN_);
    bX_ = make(12 * capN_);
    bWork_ = make(4 * 21 * static_cast<size_t>(capN_));
    bStats_ = make(16);
    TypedArray<RDUniform> us;
    const RID ids[10] = {bRow_, bCol_, bDiag_, bVal_, bMass_, bFilt_, bRhs_, bX_, bWork_, bStats_};
    for (int b = 0; b < 10; ++b) {
        if (!ids[b].is_valid()) { error_ = "storage_buffer_create failed"; return false; }
        Ref<RDUniform> u;
        u.instantiate();
        u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
        u->set_binding(b);
        u->add_id(ids[b]);
        us.push_back(u);
    }
    set_ = rd_->uniform_set_create(us, shader_, 0);
    patternVersion_ = -1;  // new buffers hold no pattern
    if (!set_.is_valid()) { error_ = "uniform_set_create failed"; return false; }
    return true;
}

int GpuCg::solve(const fl::LinearSystemView& s, std::vector<fl::Vec3>& x) {
    if (!ready()) return -1;
    const int n = s.n, nnz = s.rowStart[n];
    if (n <= 0) return -1;
    const auto t0 = std::chrono::steady_clock::now();
    if (!ensureBuffers(n, nnz)) return -1;
    if (patternVersion_ != s.patternVersion) {
        rd_->buffer_update(bRow_, 0, 4 * (n + 1), bytesOf(s.rowStart, 4 * (n + 1)));
        rd_->buffer_update(bCol_, 0, 4 * nnz, bytesOf(s.col, 4 * static_cast<size_t>(nnz)));
        rd_->buffer_update(bDiag_, 0, 4 * n, bytesOf(s.diag, 4 * static_cast<size_t>(n)));
        patternVersion_ = s.patternVersion;
    }
    std::vector<float> f(static_cast<size_t>(9) * std::max(nnz, n));
    for (int k = 0; k < nnz; ++k) for (int q = 0; q < 9; ++q) f[9 * k + q] = static_cast<float>(s.val[k].m[q]);
    rd_->buffer_update(bVal_, 0, 36 * nnz, bytesOf(f.data(), 36 * static_cast<size_t>(nnz)));
    for (int i = 0; i < n; ++i) for (int q = 0; q < 9; ++q) f[9 * i + q] = static_cast<float>(s.filt[i].m[q]);
    rd_->buffer_update(bFilt_, 0, 36 * n, bytesOf(f.data(), 36 * static_cast<size_t>(n)));
    for (int i = 0; i < n; ++i) f[i] = static_cast<float>(s.massTerm[i]);
    rd_->buffer_update(bMass_, 0, 4 * n, bytesOf(f.data(), 4 * static_cast<size_t>(n)));
    for (int i = 0; i < n; ++i) { f[3 * i] = static_cast<float>(s.rhs[i].x); f[3 * i + 1] = static_cast<float>(s.rhs[i].y); f[3 * i + 2] = static_cast<float>(s.rhs[i].z); }
    rd_->buffer_update(bRhs_, 0, 12 * n, bytesOf(f.data(), 12 * static_cast<size_t>(n)));
    struct { int32_t n, maxIt; float tol; int32_t pad; } pc{n, s.maxIterations, static_cast<float>(s.tolerance), 0};
    const PackedByteArray pcb = bytesOf(&pc, sizeof(pc));
    const int64_t cl = rd_->compute_list_begin();
    rd_->compute_list_bind_compute_pipeline(cl, pipeline_);
    rd_->compute_list_bind_uniform_set(cl, set_, 0);
    rd_->compute_list_set_push_constant(cl, pcb, sizeof(pc));
    rd_->compute_list_dispatch(cl, 1, 1, 1);
    rd_->compute_list_end();
    rd_->submit();
    rd_->sync();
    const PackedByteArray xb = rd_->buffer_get_data(bX_, 0, 12 * n);
    const PackedByteArray sb = rd_->buffer_get_data(bStats_, 0, 4);
    if (xb.size() < 12 * n || sb.size() < 4) { error_ = "buffer_get_data returned too little"; return -1; }
    const float* xf = reinterpret_cast<const float*>(xb.ptr());
    x.resize(n);
    for (int i = 0; i < n; ++i) x[i] = fl::Vec3(xf[3 * i], xf[3 * i + 1], xf[3 * i + 2]);
    int it;
    std::memcpy(&it, sb.ptr(), 4);
    lastMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    ++solves_;
    if (verify_) {
        // the same iterations (no tolerance stop) in single precision on the CPU (fl::cgBlockJacobiFloat) and in double:
        // the GPU must be about as close to the double result as the CPU's own single-precision run. CG in single
        // precision is sensitive to summation order on these systems (float vs double, same iterations: up to a few
        // percent of the solution on a garment), so the GPU is not compared with the float run directly.
        fl::LinearSystemView same = s;
        same.maxIterations = it;
        same.tolerance = 0;
        std::vector<fl::Vec3> xf, xd;
        const int itRef = fl::cgBlockJacobiFloat(same, xf);
        fl::cgBlockJacobiDouble(same, xd);
        double eg = 0, ef = 0, rmax = 0;
        for (int i = 0; i < n; ++i) {
            eg = std::max(eg, static_cast<double>(fl::length(x[i] - xd[i])));
            ef = std::max(ef, static_cast<double>(fl::length(xf[i] - xd[i])));
            rmax = std::max(rmax, static_cast<double>(fl::length(xd[i])));
        }
        // a cloth at rest solves for steps of 1e-17 m: there the squares in the dot products reach the float range a
        // GPU flushes to zero (and the CPU does not); errors of 1e-20 m are no measure of anything
        if (rmax > 1e-9) {
            verifyMaxRel_ = std::max(verifyMaxRel_, eg / rmax);
            verifyRefRel_ = std::max(verifyRefRel_, ef / rmax);
            const double ratio = eg / (ef + 1e-5 * rmax);
            // FL_GPU_DUMP=<file>: write the first solve whose GPU error is far above the CPU float error (debugging)
            static const char* dump = std::getenv("FL_GPU_DUMP");
            static bool dumped = false;
            if (dump && !dumped && ratio > 50) {
                dumped = true;
                if (FILE* f = std::fopen(dump, "wb")) {
                    const int hdr[4] = {n, nnz, it, s.patternVersion};
                    std::fwrite(hdr, 4, 4, f);
                    std::fwrite(s.rowStart, 4, n + 1, f);
                    std::fwrite(s.col, 4, nnz, f);
                    std::fwrite(s.diag, 4, n, f);
                    std::fwrite(s.val, sizeof(fl::Mat3), nnz, f);
                    std::fwrite(s.massTerm, sizeof(fl::Real), n, f);
                    std::fwrite(s.filt, sizeof(fl::Mat3), n, f);
                    std::fwrite(s.rhs, sizeof(fl::Vec3), n, f);
                    std::fwrite(x.data(), sizeof(fl::Vec3), n, f);
                    std::fclose(f);
                }
            }
            verifyRatio_ = std::max(verifyRatio_, ratio);
        }
        verifyItDiff_ = std::max(verifyItDiff_, std::abs(itRef - it));
    }
    return it;
}
