#pragma once
// Conjugate gradient on the GPU (Godot RenderingDevice compute, Vulkan / Metal through MoltenVK). Solves the filtered
// implicit system of a cloth substep (fl::LinearSystemView) with block-Jacobi preconditioning: the algorithm of
// fl::cgBlockJacobiFloat, which is its CPU reference.
//
// The whole solve is one dispatch of one 256-thread workgroup that loops over the CG iterations with workgroup
// barriers between the steps. A cloth system is small (a few thousand vertices), so one compute unit has enough threads
// for it, and a dispatch per CG step (thousands per frame) would cost more in launch overhead than the arithmetic.
// Bigger meshes would want the multi-workgroup version (one dispatch per step, reductions across workgroups).
#include <string>
#include <vector>

#include <godot_cpp/variant/rid.hpp>

#include "failoring/cloth.hpp"

namespace godot {
class RenderingDevice;
}

class GpuCg {
public:
    ~GpuCg();
    // Creates the device and the pipeline on the first call; call it from the main thread (Godot 4.3 crashed with the
    // device created on a worker and freed on the main thread). False when there is no GPU device (headless,
    // Compatibility renderer) or the shader does not compile; error() says why.
    bool ready();
    // fl::ExternalLinearSolver: iterations used, or -1 (the solver then runs its CPU CG).
    int solve(const fl::LinearSystemView& sys, std::vector<fl::Vec3>& x);
    bool active() const { return rd_ != nullptr; }   // the device exists (after the first solve)
    const std::string& error() const { return error_; }
    double lastMs() const { return lastMs_; }      // wall time of the last solve (upload, GPU, download)
    long long solves() const { return solves_; }
    std::string deviceName() const;
    // verify: also solve every system on the CPU for the same number of iterations, in single precision
    // (fl::cgBlockJacobiFloat) and in double. Largest differences from the double result, as a fraction of its largest
    // vertex step: the GPU's (verifyMaxRel) and the CPU float run's (verifyRefRel); verifyRatio is the worst per-solve
    // GPU error over the CPU float error (about 1 when the GPU rounds like the CPU).
    void setVerify(bool on) { verify_ = on; }
    double verifyMaxRel() const { return verifyMaxRel_; }
    double verifyRefRel() const { return verifyRefRel_; }
    double verifyRatio() const { return verifyRatio_; }
    int verifyItDiff() const { return verifyItDiff_; }  // iterations the reference fell short of the GPU (a breakdown)

private:
    bool init();
    void freeBuffers();
    bool ensureBuffers(int n, int nnz);

    godot::RenderingDevice* rd_ = nullptr;
    bool tried_ = false;
    std::string error_;
    godot::RID shader_, pipeline_, set_;
    godot::RID bRow_, bCol_, bDiag_, bVal_, bMass_, bFilt_, bRhs_, bX_, bWork_, bStats_;
    int capN_ = 0, capNnz_ = 0;
    int patternVersion_ = -1;
    double lastMs_ = 0;
    long long solves_ = 0;
    bool verify_ = false;
    double verifyMaxRel_ = 0, verifyRefRel_ = 0, verifyRatio_ = 0;
    int verifyItDiff_ = 0;
};
