# Engine/platform decision for GPU cloth solver + renderer on Apple Silicon (as of 2026-10-30)

Coverage note: ~16 tool calls. Many GitHub pages were captured without release dates (the fetch tool saw empty Release sections), the GitHub API/raw hosts were blocked by the proxy, and the Godot RenderingDevice class reference could not be retrieved in full. Items not located are in Gaps, not asserted. "Verified" = read in a fetched page; anything else is marked inferred.

## 1. Staying in Godot: Metal driver, RenderingDevice compute, readback, versions

### Takeaway
Godot has had a native Metal rendering backend (Apple Silicon only) since the 4.4 cycle, so the MoltenVK layer the team measured is no longer mandatory on M-series Macs, and the stable line is now 4.7.x. I could not verify any Godot feature that removes readback/dispatch overhead for a GDExtension-driven solver; async readback exists but has at least one user-reported failure.

### Cited Findings
- Godot 4.4 dev 1 announced a native Metal backend (GH-88199, by Stuart Carnie); previously Godot used MoltenVK. Claimed "at least as fast as" Vulkan and often much faster; Apple Silicon only (M1/M2/M3 Macs and iOS); Intel Macs stay on MoltenVK — [Godot 4.4 dev 1 article](https://godotengine.org/article/dev-snapshot-godot-4-4-dev-1/)
- Godot release list as seen on GitHub: 4.7.2-stable (18 Aug, latest, marked 2026), 4.7.1 (14 Jul), 4.7 (18 Jun), 4.6.3 (20 May), 4.6.2 (1 Apr), 4.6.1 (16 Feb), 4.6 (26 Jan), 4.5.2 (19 Mar). Page shows no years except via asset dates; so the user's "4.3" is ~4 feature releases behind — [Godot releases](https://github.com/godotengine/godot/releases)
- A Godot 4.5.1 (mono) forum post reports `BufferGetDataAsync` on a 64 MB compute buffer never fires its callback while synchronous `BufferGetData` works (unconfirmed cause, no reply seen). Confirms an async readback API exists by 4.5 — [Godot forum](https://forum.godotengine.org/t/buffergetdataasync-not-executing/135711)
- RenderingDevice is documented as "an abstraction for working with modern low-level graphics APIs such as Vulkan" — [Godot 4.5 docs](https://docs.godotengine.org/en/4.5/classes/class_renderingdevice.html) (only summary retrieved)
- Godot is MIT-licensed (not fetched in this session; widely known, treated as unverified here).

### Inferences
- Moving 4.3 -> 4.7 gets the native Metal driver for free; this is the cheapest experiment to re-measure the team's dispatch/readback overhead (the original measurement was on MoltenVK, which is no longer the default on Apple Silicon). Inferred, not measured.
- Keeping the solver output in a GPU buffer and rendering from it (no readback) is the structural fix regardless of engine; whether Godot 4.7 lets a RenderingDevice storage buffer back a mesh vertex buffer without copy is NOT verified.
- Godot's compute path is a per-call GDScript/C++ RenderingDevice API with its own barrier handling; a CG-type solver with hundreds of dispatches per frame will pay Godot's per-dispatch CPU cost. Inferred; no Godot-specific number found.

### Gaps
- Godot 4.5/4.6/4.7 release-note text on compute, buffer sharing, `buffer_get_data_async`, Metal driver compute features: not retrieved (class ref page content truncated).
- No published measurement of Godot RenderingDevice per-dispatch cost on Metal driver vs MoltenVK.
- "Godot 5" status not searched.

## 2. Custom engine / direct Metal / wgpu / WebGPU / MoltenVK / Rust

### Takeaway
wgpu (Apache-2.0/MIT, Metal+Vulkan+DX12+browser WebGPU) is the only found option that covers native Apple Silicon and the browser from one code base, but a Feb 2026 paper measured WebGPU per-dispatch API overhead on Metal at 32-71 us (24-36 us on Vulkan), which matters for many-kernel CG solvers; direct Metal avoids that layer but loses the browser.

### Cited Findings
- wgpu "runs natively on Vulkan, Metal, D3D12, and OpenGL; and on top of WebGL2 and WebGPU on wasm"; dual Apache-2.0 / MIT; ~18.4k stars; docs link points to "v30"; wgpu-native provides C bindings — [wgpu repo](https://github.com/gfx-rs/wgpu)
- Characterization paper (Maczan, arXiv:2604.02344, Feb 2026): WebGPU API-only dispatch overhead 24-36 us on Vulkan, 32-71 us on Metal; ~95 us per op including Python; implementation choice (Dawn vs wgpu-native) mattered by 2.2x on Metal; naive single-op benchmarks overestimate cost ~20x vs sequential dispatch; kernel fusion gave +53% throughput on Vulkan; dispatch overhead dominated at batch size 1 — [arXiv](https://arxiv.org/abs/2604.02344)
- Metal Performance Primitives / TensorOps: WWDC26 session 330 states TensorOps uses neural accelerators on M5 and A19 GPUs; macOS/iOS 27 add FP8/FP4-class types and cooperative tensors as direct matmul inputs — [WWDC26 330](https://developer.apple.com/videos/play/wwdc2026/330/). A DEV summary of WWDC25 "Discover Metal 4" exists — [dev.to](https://dev.to/arshtechpro/wwdc-2025-discover-metal-4-23f2) (not read in full)
- Slang (Khronos-hosted since Nov 2024): Apache-2.0 with LLVM exception; targets HLSL, SPIR-V/GLSL (supported), CUDA (compute-only C++), Metal and WGSL listed as experimental; ~5.7k stars — [Slang repo](https://github.com/shader-slang/slang), [Khronos announcement](https://www.khronos.org/news/press/khronos-group-launches-slang-initiative-hosting-open-source-compiler-contributed-by-nvidia)

### Inferences
- M3 lacks the M5 neural accelerators; TensorOps benefits likely small for sparse cloth CG on the dev machine (inferred; M3 TensorOps behaviour not verified).
- Because WebGPU overhead (~tens of us/dispatch) x several hundred dispatches per frame is already several ms, the per-frame kernel count of the solver design (persistent kernels, fused CG iterations, GPU-side convergence/indirect dispatch) matters more than the engine choice.
- A shared C++ core + wgpu-native (native) + wgpu->wasm/WebGPU (browser) is plausible; Slang->WGSL experimental status is a risk (WGSL kernels would likely be hand-written).
- Direct Metal (metal-cpp), unified-memory zero-copy, indirect command buffers, single-command-buffer multi-dispatch: no sources located this session; treat as background knowledge, not verified.

### Gaps
- No located source on Metal-native per-dispatch cost, MoltenVK per-dispatch cost, ICB behaviour, M3 Dynamic Caching/ray tracing for compute, SIMD-group matrix ops.
- Bevy / Rust ecosystem, Dawn release status, Kokkos, SYCL, Halide, MLX/MPS: not searched.
- wgpu release date and subgroup/indirect support not confirmed from page.

## 3. GPU compute libraries: Warp, Newton, Taichi/Quadrants, Genesis

### Takeaway
Warp and Newton are CUDA/CPU only (macOS = CPU), so they are reference/study material, not an Apple-GPU runtime. Taichi still lists Metal but its maintenance is unverified; Genesis (Apache-2.0) now uses a "Quadrants" compiler with Metal listed and has PBD cloth examples.

### Cited Findings
- Warp docs (v1.18.0): backends are CPU and CUDA only; "macOS uses the CPU backend"; CUDA needs Turing+ and R580+ driver — [Warp FAQ](https://nvidia.github.io/warp/stable/user_guide/faq.html)
- Newton: Apache-2.0 code, built on Warp, MuJoCo Warp primary backend; CUDA acceleration only; macOS CPU only; ~5.7k stars — [Newton repo](https://github.com/newton-physics/newton)
- Newton solvers: SolverStyle3D ("projective dynamics based cloth solver" - the only one the page labels for cloth), SolverVBD (particles and AVBD rigid), SolverXPBD, SolverMuJoCo, SolverImplicitMPM, others — [Newton solver API](https://newton-physics.github.io/newton/latest/api/newton_solvers.html)
- Newton releases seen: v1.6.0 (10 Sep, latest), v1.5.0 (11 Aug), v1.4.0 (16 Jul), v1.3.0 (11 Jun), v1.2.0 (12 May); i.e. roughly monthly cadence (year not printed; listing also shows v1.5.2 dated 11 Sep, ordering ambiguous). v1.4.0: Style3D memory fix, USD import of cloth, cloth-heavy scenes ~10-13% faster; v1.6.0: VBD compliant ALM option, NumPy fix for anisotropic Style3D. No Metal/macOS mention in any release — [Newton releases](https://github.com/newton-physics/newton/releases)
- Taichi: Apache-2.0; backends listed: CPU, CUDA, Vulkan, OpenGL 4.3+, Apple Metal, WebAssembly (experimental); 28.4k stars; 877 open issues; release date not visible — [Taichi repo](https://github.com/taichi-dev/taichi)
- Genesis: Apache-2.0; its compiler "Quadrants" targets CUDA, ROCm, Apple Metal, Vulkan, x86, ARM64; "PBD: cloth" example; 30.1k stars; Releases section empty in capture (cites May 2026 blog post) — [Genesis repo](https://github.com/Genesis-Embodied-AI/Genesis)
- Slang CUDA/Metal/WGSL status: see section 2.

### Inferences
- Newton's Style3D solver (projective dynamics) and VBD are the most relevant open GPU cloth algorithms to port to Metal compute; reading their Warp kernels is cheap, running them on a Mac is not possible on GPU (inferred from CUDA-only statement).
- Taichi's maintenance since the original company's wind-down is unverified; Genesis' fork "Quadrants" suggests the Metal backend lives on there (inferred).
- Python-based tools cannot ship into a Godot/WASM product without a C++/shader port.

### Gaps
- Any Warp Metal roadmap: search found only unrelated results (the terminal app "Warp") and a third-party "cuda-metal" project (https://github.com/Lulzx/cuda-metal/releases, not examined).
- Taichi's latest release/date and Quadrants' relationship to Taichi: not verified.
- Newton: whether Style3D solver is derived from the Style3D company's code and its exact licence: not verified.
- MLX, Kokkos, SYCL, Halide: not searched.

## 4. Rendering libraries and offline rendering

### Takeaway
Nothing was successfully researched for Filament, bgfx, Diligent, RealityKit, Hydra Storm, or Blender Cycles Metal in this pass.

### Cited Findings
- (none located)

### Inferences
- Unverified background: Blender+Cycles has a Metal backend and can render solver-exported (Alembic/USD) frames offline; Filament supports Metal and WebGL2/WebGPU. Do not rely on these without verification.

### Gaps
- All rendering-library facts (licence, Metal support, last release, WASM) remain to be checked.

## 5. Open-source cloth solvers: adoption and licensing

### Takeaway
Only tool-level facts were gathered: ipc-toolkit (MIT) provides IPC barrier/CCD primitives only (no solver, GPU not mentioned); Newton and Genesis are Apache-2.0. GIPC, ARCSim, PolyFEM, Bullet/Chrono, Blender, Style3D open-source status were not verified.

### Cited Findings
- ipc-toolkit: MIT; barrier function and derivatives, broad/narrow-phase CCD, distance and friction potentials, pybind11 bindings; "not a full simulation library"; 331 stars — [ipc-toolkit](https://github.com/ipc-sim/ipc-toolkit)
- Search for an open-source Style3D simulator returned no official repository; only unrelated GitHub repos and a "Marvelous Designer unlock tool" wiki (a piracy-adjacent page; avoid) — [search result list]( https://www.style3d.com/blog/?p=3027 shows only a Style3D blog post)
- Newton/Genesis/Taichi licences: see section 3.

### Inferences
- Licence traps flagged as unverified background: ARCSim and some academic GPU codes (e.g., GIPC) may carry non-commercial or GPL terms; check each LICENSE file before copying code. Not verified here.
- Study-only candidates: Newton Style3D/VBD kernels (Apache-2.0, safe to adapt with attribution).

### Gaps
- GIPC, ARCSim, PolyFEM, Bullet, Chrono, Blender cloth, Unreal Chaos, Roblox, Brax/MJX: not researched.
- No located evidence of anyone shipping interactive 10-50k-vertex cloth on Apple Silicon; no blog measurements found.

## Overall decision inputs (summary for the writer)
- Verified: Godot has a native Metal driver (Apple Silicon) since 4.4; current stable is 4.7.2; Warp/Newton have no Apple GPU backend; wgpu covers Metal + browser; WebGPU dispatch overhead on Metal is tens of microseconds (paper), so kernel count per frame dominates.
- Inferred recommendation (low confidence): first upgrade Godot 4.3 -> 4.7 and re-measure on the Metal driver and with fewer, fused dispatches; if still dispatch-bound, move the solver to a standalone C++ core with Metal-cpp (native) and wgpu/WebGPU (browser), keeping Godot as editor/UI shell, since an editor rewrite is far costlier than the solver port. Cost of editor UI rewrite in alternative engines was not researched.
