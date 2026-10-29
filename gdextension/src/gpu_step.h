#pragma once
// The whole implicit substep on the GPU (Godot RenderingDevice compute; docs/GPU_SOLVER_NOTES.md, "The GPU substep").
//
// Runs the fast policy of fl::ClothSolver::stepImplicit for a whole frame in one submission: per substep the
// prediction, body contact, lagged friction, the Hessian and gradient (gathered, no float atomics), the filtered
// right-hand side, block-Jacobi CG for a fixed budget (stops on the CPU's tolerance through a GPU-side flag), the
// line search on the variational energy, strain limiting, the body push-out, self collision (spatial hash, Jacobi
// corrections) and the velocity update. The CPU uploads
// the state at the start of the frame and reads it back at the end; the model (constraints, pattern) and the body's
// distance grid are uploaded only when they change.
//
// Precision: float32. Positions are kept as the substep's start position XP plus the displacement U within the
// substep, so friction slip, inertia and the velocity come from U (precise to float rounding of U itself), not from
// differences of absolute positions near 1 m (6e-8 m resolution, above the friction's stick threshold).
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <godot_cpp/variant/rid.hpp>

#include "failoring/cloth.hpp"
#include "failoring/sdf_grid.hpp"

namespace godot {
class RenderingDevice;
}

class GpuStep {
public:
    // What the cloth collides with this frame. Anything else (capsules, ellipsoids, cylinders) sets `unsupported`.
    struct Scene {
        std::shared_ptr<const fl::SdfGrid> grid;      // the mesh body (may be null)
        std::vector<fl::Vec3> planes;                 // point, normal per plane
        std::vector<fl::Real> spheres;                // centre xyz, radius per sphere
        std::vector<fl::Real> boxes;                  // origin, three axes, half extents: 15 per box
        std::string unsupported;
    };
    ~GpuStep();
    // Creates the device and compiles the kernels on the first call (call it on the thread that steps). False when
    // there is no GPU device or a kernel does not compile; error() says why.
    bool ready();
    // One frame of the solver on the GPU. False (and nothing changed) when the GPU cannot run it: no device, the
    // solver uses something the GPU does not implement (reason()), or an upload failed (error()).
    bool stepFrame(fl::ClothSolver& s, const Scene& scene);
    const std::string& error() const { return error_; }
    const std::string& reason() const { return reason_; }   // last reason a frame stayed on the CPU
    std::string deviceName() const;
    double lastMs() const { return lastMs_; }               // wall time of the last GPU frame (uploads, GPU, read-back)
    double lastGpuMs() const { return lastGpuMs_; }         // of which submit to sync
    double lastUploadMs() const { return lastUploadMs_; }   // ... model, grid, parameters, state, uniform sets
    double lastRecordMs() const { return lastRecordMs_; }   // ... recording the compute list
    double lastReadMs() const { return lastReadMs_; }       // ... read-back and handing the state to the solver
    long long frames() const { return frames_; }
    int lastCgIterations() const { return lastCgIts_; }     // CG iterations summed over the substeps of the last frame
    int lastLsRejects() const { return lastLsRejects_; }    // substeps of the last frame whose line search failed
    int dispatchesPerFrame() const { return lastDispatches_; }
    // The solver was replaced: upload its model on the next frame.
    void invalidate() { modelOf_ = nullptr; }

private:
    struct Buf { godot::RID rid; size_t bytes = 0; char type = 'f'; };
    struct Kernel {
        std::string name;
        std::vector<std::string> bufs;   // binding order
        godot::RID shader, pipeline, set;
        int setVersion = -1;
    };
    bool init();
    bool compile(Kernel& k, const std::string& body);
    void freeAll();
    bool buf(const std::string& name, size_t bytes, char type);   // (re)creates when too small
    bool upload(const std::string& name, const void* data, size_t bytes);
    bool uploadModel(fl::ClothSolver& s);
    bool uploadGrid(const fl::SdfGrid& g);
    bool bindSet(Kernel& k);
    void dispatch(int64_t cl, Kernel& k, int items, int a = 0, int b = 0, int c = 0);

    godot::RenderingDevice* rd_ = nullptr;
    bool tried_ = false;
    std::string error_, reason_;
    std::unordered_map<std::string, Buf> bufs_;
    int bufVersion_ = 0;   // bumps when any buffer is recreated (uniform sets are rebuilt)
    std::unordered_map<std::string, Kernel> k_;
    const fl::ClothSolver* modelOf_ = nullptr;
    int modelVersion_ = -1;
    fl::GpuModel model_;
    std::shared_ptr<const fl::SdfGrid> gridHeld_;   // the uploaded grid (held, so its address is not reused)
    int groupsMax_ = 0;     // workgroups of the widest partial-sum kernel (PART holds 2 floats per group per region)
    int hashCap_ = 0;       // HITEM capacity (27 cells per triangle)
    double lastMs_ = 0, lastGpuMs_ = 0, lastUploadMs_ = 0, lastRecordMs_ = 0, lastReadMs_ = 0;
    long long frames_ = 0;
    int lastCgIts_ = 0, lastLsRejects_ = 0, lastDispatches_ = 0, dispatches_ = 0;
};
