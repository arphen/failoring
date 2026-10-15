# Faster cloth: where the time goes and what can go on the GPU

Measurements from the opus/vbd branch (October 2026). The machine is a shared 2-core Xeon cloud container at 2.8 GHz,
so absolute times are this machine's and only the ratios carry over. Nothing here was measured on a GPU or on the M3 Pro.

## Where the time goes now (implicit Newton + sparse Cholesky)

gprof on `test_garment` (672 vertices, 4 substeps, 40 frames):

| function | share |
|---|---|
| `SparseCholesky::factor` (incl. `updateBlock`) | 85% |
| `SparseCholesky::solve` | 6% |
| `assembleEnergy` | 4% |
| everything else | 5% |

- There are 43 factorisations per frame: 4 substeps × 10.8 Newton iterations; the cap is 12 and is often reached.
- The factorisation is supernodal with geometric nested-dissection ordering. Factor-only benchmark
  (`fbench`, a cloth-like 3x3-block 2-ring stencil):
  - 900 vertices: 61 MFLOP in 17 ms;
  - 3025 vertices: 483 MFLOP in 124 ms;
  - 3.6–3.9 GFLOP/s throughout, about a third of this CPU's peak without AVX.
- A register-tiled update kernel (`updateBlock`, 4x4 tiles, no intermediate buffer) gave the same factor to the last bit
  and about 10% speed. The kernel is not the problem; the number of factorisations is.

### Newton tolerance

`newtonTolerance` is 1e-6 m, i.e. the largest vertex update must fall below one micrometre.

`test_garment` per frame:

| tolerance | Newton/substep | ms/frame | hem height |
|---|---|---|---|
| 1e-6 | 9.95 | 494 | 156.4 mm |
| 1e-5 | 4.99 | 255 | 156.2 mm |
| 1e-4 | 6.43 | 338 | 162.4 mm |

The predicted hem height is 161.2 mm.

At 1e-5 the frame cost halves with the same result. The settled state jitters a little more (the test's KE < 1e-6 settle
check trips at frame 247 instead of 160), so the total run time only drops from 79 s to 63 s.

### Reusing a stale factor (CholeskyPCG)

Hanging 0.8 m patch, 900 vertices: 8.3 factorisations per frame instead of 20, but each PCG iteration is a full pair of
triangular solves (118 per frame), so the frame costs the same (332 vs 347 ms). No win.

## GPU-shaped solvers on real cotton (6000/4000 N/m tensile)

The question for a GPU port is how many cheap parallel iterations replace one Newton step.

### Newton + conjugate gradient

Same answer as Cholesky. Iterations per frame on the hanging 0.8 m patch:

| vertices | SSOR (sequential) | block-Jacobi (GPU-parallel) |
|---|---|---|
| 441 | 1106 | 2081 |
| 900 | 2115 | 5193 |
| 1681 | 2664 | 5551 |

Each iteration is a few kernels plus two global reductions, so thousands per frame on a GPU is roughly 10k–20k
dispatches. Not attractive without a multigrid preconditioner (Tamstorf et al. 2015), which is a project of its own.

### Vertex Block Descent (Chen et al. 2024), `Integrator::VBD`

Per-vertex 3x3 Newton over graph colours.

- At 2 substeps it needs about 200 sweeps to match Newton on the hanging 0.4 m patch.
- With 20–40 sweeps it is not merely soft: it keeps 1.9 J of stretch energy after 20 sweeps, where the converged state
  stores almost none. That energy returns as motion, and the patch swings up past its pins.
- The global energy falls on every sweep (checked with `FL_VBD_DEBUG=<frame>`), so this is slow convergence of long
  stretch modes, not an energy bug.
- Chebyshev acceleration (rho 0.9) did not help at 40 sweeps.
- The adaptive initialisation (paper section 3.5) did not fix it at 2 substeps either.

**Small steps work.** Many substeps with one sweep each, for the same total sweeps (hanging 0.4 m patch, frame 119):

| configuration | lowest point | KE | strain p95 / max while moving | strain p95 / max at f119 |
|---|---|---|---|---|
| Newton, 2 substeps | 0.600 m | 3.6e-3 J | 0.12% / 2.5% | 0.18% / 0.95% |
| VBD 40 substeps × 1 sweep | 0.610 m | 1.4e-3 J | 1.3–1.7% / 7.7% | 0.28% / 1.6% |
| VBD 60 × 1 | 0.709 m | 2.1e-4 J | 0.6% / 3.7% | 0.21% / 1.7% |
| VBD 20 × 2 | 0.601 m | 3.9e-3 J | 1.4% / 19% | 0.42% / 5.8% |

So small-step VBD reaches the right shape. It stretches about 10× more than Newton while the cloth is moving, which
shows as rubberiness in fast motion.

### On the garment (`test_garment`, tube on an ellipsoid, VBD 40 substeps × 1 sweep)

| configuration | result |
|---|---|
| friction mode 2, self-collision off | settles: KE 6.9e-6 J, seam gap 2.4 mm, clearance 1.65 mm, hem 139.7 mm (Newton 156.4, predicted 161.2) |
| friction off | blows up: KE 18 J, seams 59 mm apart |
| self-collision on | blows up |

Self-collision is the post-solve vertex-triangle projection. At small h its position corrections become large velocities.

## Inexact linearised solve + strain limiting (the Baraff-Witkin / commercial-tool policy)

One Newton iteration per substep, conjugate gradient with block-Jacobi preconditioning capped at a fixed iteration
budget (not run to tolerance), then a strain-limiting pass (Provot 1995) that shortens any edge longer than
(1 + strainLimit) x rest.

New options:
- `SolverParams::strainLimit`, `strainLimitPasses`;
- `cgWarmStart` (it did not help here).

On the hanging 0.4 m patch, 5 substeps x 1 linearised solve x 50 CG iterations already matches Newton without a strain
limit:

| | lowest point | strain p95 / max at f119 | time |
|---|---|---|---|
| this policy | 0.599 m | 0.22% / 2.5% | 4.3 ms/frame |
| exact | 0.600 m | 0.18% / 0.95% | 22 ms/frame |

On larger hanging patches (900+ vertices) 50 iterations cannot carry the weight to the two pinned corners. Without a
strain limit it blows up; with one it stays stable but hangs wrong. Hanging from two points is the worst case for an
iteration budget, because every load has to travel across the whole mesh.

`test_garment` (tube resting on a body, the real use case), 300 frames:

| configuration | KE | seam gap | clearance | hem (predicted 161.2) | ms/frame |
|---|---|---|---|---|---|
| Newton + Cholesky (default) | 9.3e-7 | 0.56 mm | 1.65 mm | 156.4 mm | 494 |
| 4 substeps, CG 50, no limit | blows up | | | | |
| 5 substeps, CG 50, limit 3% | 9.4e-3 | 5.33 mm | 1.65 mm | 111.2 mm | 18 |
| 5 substeps, CG 200, limit 3% | 3.1e-4 | 3.26 mm | 1.65 mm | 135.0 mm | 49 |
| 10 substeps, CG 50, limit 3% | 2.9e-4 | 2.33 mm | 1.65 mm | 138.9 mm | 37 |
| **10 substeps, CG 100, limit 3%** | 1.6e-4 | 1.49 mm | 1.65 mm | 146.6 mm | 57 |

The last two pass every threshold of the test (KE < 5e-4, gap < 4 mm, clearance > -1 mm, hem within 40 mm). That is
9-13x faster than the current solver on the CPU. Every operation in the loop is data-parallel:
- assembling the 3x3 blocks;
- the sparse block matrix-vector product;
- the 3x3 block-Jacobi inverse;
- dot products;
- strain limiting (Jacobi or coloured on the GPU).

On the CPU the CG cost is the matrix-vector product (callgrind: ~63% of instructions), about 30 ns per unknown per
iteration on this machine (double precision, 2-ring 3x3 blocks, single thread).

### In the editor (`FL_FAST=1`, plan_loader.gd)

Tee, average body, standing, 60 frames; wall time per frame including the editor:

| quality | vertices | Newton (default) | fast | speed-up |
|---|---|---|---|---|
| preview | 475 | 187 ms | 67 ms | 2.8x |
| fine | 1131 | 1013 ms | 138 ms | 7.3x |

Front and back renders at frame 59 show the same drape, hem and sleeve shape. The fine fast run has one fold at the right
neckline. The sleeve-hem artifacts are present in both.

## What this means for the GPU plan

0. **Recommended (now built, see "The GPU solver" below):** the inexact linearised solve above (10 substeps, CG 100, strain limit 3%) as the GPU solver. It
   uses the same energies, matrix and contact handling as the current Newton path, so the editor keeps its behaviour.
   It passes the garment test today on the CPU. The GPU work is the matrix-vector product, the block-Jacobi
   preconditioner, the dot-product reductions and the strain-limit pass. The cost per frame is
   substeps x CG iterations x (a few kernels), so fusing each CG iteration into 2-3 dispatches matters.
1. Small-step VBD is the other GPU-shaped method that converges on stiff cotton. Its cost is substeps × colours dispatches.
   - Per-dispatch overhead on the M3 Pro through Godot/MoltenVK is **unmeasured**.
   - That number decides whether it is fast.
2. Before porting, VBD needs:
   - self-contact as an energy inside the sweep (as in the VBD paper), not the post-solve projection;
   - the frictionless blow-up understood;
   - a stretch fix for fast motion (more sweeps, or a strain-limiting pass).
3. Meanwhile the CPU path has cheap wins with no change in result: `newtonTolerance` 1e-5 (2× per frame), threads over
   independent subtrees of the nested-dissection tree, and Apple Accelerate's sparse Cholesky on macOS (unmeasured here).

## The GPU solver (done 2026-10-15)

`gdextension/src/gpu_cg.{h,cpp}`: the block-Jacobi conjugate gradient of the fast solve as one Godot compute shader
(GLSL, compiled by Godot to SPIR-V; Vulkan on Linux and Windows, Metal through MoltenVK on macOS).

**Design.** The whole CG solve is **one dispatch of one 256-thread workgroup**. It loops over the iterations with
workgroup barriers between the steps (matrix-vector product, reductions, updates). There are no per-iteration dispatches
or read-backs. Per substep the CPU still assembles the matrix (3x3 blocks) and the right-hand side, uploads them, runs
the dispatch, waits, and reads back the step. A cloth system of a few thousand vertices fits one compute unit. Splitting
each CG step into its own dispatch would mean ~4000-6000 launches per frame, so launch overhead would dominate.

**Wiring.**
- `ClothSolver::setExternalLinearSolver` (cloth_core) hands every CG solve to an outside solver through
  `LinearSystemView`.
- `FailoringSim.set_param("gpu", 1)` attaches the GPU solver; `FL_FAST=1 FL_GPU=1` in the editor.
- When there is no GPU device (headless, Compatibility renderer), every solve falls back to the CPU CG and
  `get_solver_stats()["gpu_error"]` says why.

**Threading.** With the GPU on, the editor steps on the main thread. With the device used from the editor's worker
thread while the main thread drew, Godot 4.3 crashed (SwiftShader, SIGSEGV in the main loop). Creating the device on
the main thread was not enough on its own. A device shared with the renderer's queue is the likely reason (unverified).

**Correctness.** `cgBlockJacobiFloat` / `cgBlockJacobiDouble` (cloth_core/src/linear_ext.cpp) are the same algorithm on
the CPU in single and double precision. With `set_param("gpu_verify", 1)` every GPU solve is redone on the CPU for the
same number of iterations in both precisions. In single precision this CG is sensitive to summation order on garment
systems: float vs double, same iterations, differs by up to 3.4% of the solution (`test_garment` fast settings). So the
check is that the GPU is as close to the double result as the CPU's own float run.
`godot/tests/gpu_check.tscn`, on SwiftShader (software Vulkan, see below):

| scene | worst solve vs double: GPU | CPU float | worst per-solve GPU / CPU-float error ratio | after 90 frames vs the double CPU run |
|---|---|---|---|---|
| hanging patch (479 vertices) | 1.96e-4 | 1.97e-4 | 1.47 | 0.27 cm average, 2.5 cm at most |
| drape on a sphere (949 vertices) | 2.85e-3 | 2.42e-3 | 1.33 | 1.33 cm average, 6.7 cm at most (folds) |

The same iteration counts in every solve. Solves whose largest step is below 1 nm are skipped by the check: a cloth at
rest solves for 1e-17 m steps, where the GPU flushes tiny floats to zero (one such solve showed a ratio of 419 for an
error of 1e-20 m).

The fine tee in the editor on the GPU path (60 frames, 600 GPU solves) shows no skin through the cloth (poke.py:
0.00%), and its front and back renders look like the CPU fast solve. Both have the fast solve's ragged sleeve hem.

**Speed: not measured on a real GPU.** SwiftShader runs the shader on this machine's 2 CPU cores, about 60-150 ms per
solve, so its timings say nothing. The number to measure on the M3 Pro is `gpu_last_ms` (upload, dispatch, wait,
download per solve) for the fine tee: the CPU CG costs about 10 ms per solve there (138 ms/frame over 10 substeps,
assembly included).

### Running it on the Mac

```
tools/build_gdext.sh                                   # rebuilds godot/bin/libfailoring.dylib
godot --path godot res://tests/gpu_check.tscn          # needs a window (Forward+ renderer); prints GPU lines, ms/frame
FL_FAST=1 FL_GPU=1 godot --path godot                  # the editor with the GPU solve
```

`get_solver_stats()` reports `gpu_ready`, `gpu_device`, `gpu_last_ms`, `gpu_solves`, `gpu_error`. If `gpu_last_ms` is
dominated by upload and wait rather than the shader, the next steps are:
- assemble the matrix on the GPU too (no per-substep upload);
- keep the whole substep on the GPU.

### Software Vulkan for testing here (SwiftShader)

No GPU in the cloud container. SwiftShader (github.com/google/swiftshader) built from source with its Vulkan driver
only (`-DREACTOR_BACKEND=Subzero`, tests off). It has no Xlib surface, which Godot 4.3 needs on X11, so a local patch adds
`vkCreateXlibSurfaceKHR` on top of its XCB surface (`XGetXCBConnection` from libX11-xcb, loaded at runtime). Then:

```
VK_ICD_FILENAMES=.../build-ss/Linux/vk_swiftshader_icd.json xvfb-run -a godot --rendering-driver vulkan --path godot res://tests/gpu_check.tscn
```

SwiftShader allows 4 descriptor sets, so Godot's 3D scene shaders fail to compile (the 3D view stays empty). The compute
shader uses one set and runs.

## Tools (scratch, not in the repo)

- `probe.cpp`: hanging patch, Newton vs VBD; env SUB, CAP, TOL, EVERY.
- `scale.cpp`: Cholesky vs CG(SSOR) vs CG(block-Jacobi) vs CholeskyPCG at a given spacing.
- `fbench.cpp`: factor-only benchmark.
- `tg.cpp`: `test_garment` with env TOL, SUB, NOSELF, FR, KC, INIT, NF.
