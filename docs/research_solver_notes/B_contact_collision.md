# Contact, friction and collision handling for interactive garments on a skinned body (10-50k vertices, Apple Silicon M3, Metal/MoltenVK, no CUDA)

Scope note: about 28 tool calls (over the 15 target). Everything under "Cited Findings" was read on the linked page. Items from memory with no page read are only in Inferences or Gaps and are marked UNVERIFIED. Effort scale: S <= 1 wk, M <= 1 mo, L <= 3 mo, XL more. Effort ratings are my own estimates for a small team with a C++ triangle-FEM core, not from sources. Product failure IDs: (3) contact quality, (1) never comes to rest / armpit flicker.

## Q1. IPC family: IPC, Codim-IPC, GIPC, StiffGIPC, ABD, barrier-augmented Lagrangian, 2025-26 work; per-frame cost at 10-50k vertices; any non-CUDA GPU?

### Takeaway
Every GPU IPC implementation I found is CUDA-only and was benchmarked on an RTX 4090 in the seconds-per-step regime, so none is interactive on an M3. The one 2025 method that is close to IPC-grade robustness at interactive cost is Offset Geometric Contact (OGC) with VBD. Its published GPU code path is NVIDIA Warp on a 4090, and no code release was confirmed. Codim-IPC and ipc-toolkit are the usable CPU references, and ipc-toolkit ships macOS ARM64 wheels.

### Cited Findings
- **IPC (original)**: Li, Ferguson, Zhou, Jiang, Kaufman, "Incremental Potential Contact: Intersection- and Inversion-free Large Deformation Dynamics", SIGGRAPH 2020 / ACM TOG. Code: [github.com/ipc-sim/ipc](https://github.com/ipc-sim/ipc). Paper page: [Adobe Research](https://research.adobe.com/publication/incremental-potential-contact-intersection-and-inversion-free-large-deformation-dynamics). Licence not read for this repo.
- **Codim-IPC / C-IPC**: Minchen Li, Danny Kaufman, Chenfanfu Jiang, SIGGRAPH 2021 (arXiv May 2021). [arXiv 2012.04457](https://arxiv.org/abs/2012.04457). It adds a C2 strain-limiting barrier, a thickness model and a new CCD (ACCD). Mid-surfaces are kept at least a set thickness apart, using the average of the two elements' thicknesses, and a separate d-hat adds an elastic layer. Thin-cloth stacks at 1, 5 and 10 mm are modeled consistently, whereas ARCSim and ARGUS struggle at small thicknesses. Friction is IPC's smoothed semi-implicit friction with one lagged iteration by default. Timings: the hanging-cloth test took 6.5 min at h=0.04 s versus 87 min for ARCSim at h=0.001 s, and ACCD took about 229.6 ms per Newton iteration on the "Kick" example versus 262.3 ms for IPC's floating-point CCD. This is a CPU-scale cost, not interactive. Code: [github.com/ipc-sim/Codim-IPC](https://github.com/ipc-sim/Codim-IPC), Apache-2.0, C++ with Python scripts, includes a macOS build script (`build_Mac.py`), but only 5 commits visible and activity is unclear.
- **ipc-toolkit (library, not a solver)**: [PyPI ipctk](https://pypi.org/project/ipctk), MIT licence, version 1.6.1 released 22 Oct 2026. Provides the IPC barrier, adaptive barrier stiffness, broad and narrow-phase CCD, smooth and lagged friction potentials with derivatives. It ships macOS 11+ ARM64 wheels and contains no solver. Repo: [github.com/ipc-sim/ipc-toolkit](https://github.com/ipc-sim/ipc-toolkit). Actively maintained as of Oct 2026.
- **GIPC**: Kemeng Huang, Floyd Chitalu, Huancheng Lin, Taku Komura, "GIPC: Fast and stable Gauss-Newton optimization of IPC barrier energy", ACM TOG 2024. [arXiv 2308.09400](https://arxiv.org/abs/2308.09400). It uses analytic eigensystems for the barrier Hessian. On RTX 4090 + i9-12900K the GPU method averages 46.7x faster than the CPU reference (11.3x to 95.7x). Scenes run from about 8k to 518k vertices, including a cloth example of about 15k triangles. Code: [github.com/KemengHuang/GPU_IPC](https://github.com/KemengHuang/GPU_IPC), MPL-2.0, CUDA only (requires CUDA >= 11, default arch 86), 39 commits, no releases, described as a benchmark for GPU IPC research.
- **StiffGIPC**: Huang, Lu, Lin, Komura, Minchen Li, arXiv 2411.06224 v4 (May 2025), venue not stated on the page. [arXiv](https://arxiv.org/abs/2411.06224). Combines a connectivity-enhanced multilevel additive Schwarz preconditioner, a C2 cubic inexact strain-limiting energy for stiff cloth, and ABD with hash-based Hessian reduction. Claims up to 10x over GIPC. Example cost: a 265K-triangle London bus scene takes 1.56 s per 5 ms step. 24-core i9-13900KF + RTX 4090, double precision. No code link found on the page read.
- **Barrier-Augmented Lagrangian (BAL)**: Dewen Guo, Minchen Li, Yin Yang, Sheng Li, Guoping Wang, [arXiv 2407.00046](https://arxiv.org/abs/2407.00046). GPU inexact Newton-PCG with adaptively updated constraint sets and a fully implicit friction model updated per Newton iteration. Up to 3.8x faster than GIPC and 19x to 82x faster than IPC on the table scenes (tet scenes 64K to 2.2M tets, RTX 4090). No cloth results presented, and no code link on the page read.
- **AGIPC (2026)**: Xuan Wang, Zhaofeng Luo, Minchen Li, Taku Komura, Kemeng Huang, SIGGRAPH '26. [arXiv 2605.04773](https://arxiv.org/abs/2605.04773). In-solve adaptive coarsening, up to about 3x over StiffGIPC on RTX 4090. Code: [github.com/KemengHuang/Adaptive-GIPC](https://github.com/KemengHuang/Adaptive-GIPC) (licence not read). The page notes that a Table 1 speedup entry does not match its own timing columns.
- **YASPS (2026)**: Tang, Huang, Bernstein, Li, Li, "A Symbolic Framework for Extensible, High-Performance IPC Simulation", [arXiv 2605.23088](https://arxiv.org/abs/2605.23088), DOI 10.1145/3811327. Python front end, JIT to CUDA. Cloth-on-bunny, 200 frames: 39.6k vertices 139 s total (about 0.7 s/frame), 60k vertices 329 s (about 1.6 s/frame), versus GIPC 199 s and 586 s. This is the best direct data point for IPC cost in the 10-50k vertex band: about 0.7 s per frame on a 4090, offline.
- **Barrier + projective dynamics cloth (Mil2)**: Lei Lan et al., [arXiv 2403.19272](https://arxiv.org/abs/2403.19272) (v4 Sep 2024). Non-distance barriers inside projective dynamics, GPU solvers with subspace reuse. Claims at least an order of magnitude faster than existing fast cloth simulators, with no triangle intersections. The page does not give hardware, scene size, friction support or code.
- **Non-CUDA status**: Every GPU IPC code above is CUDA (GIPC repo, YASPS "JIT to CUDA kernels"). Newton (Warp-based) lists macOS as CPU only, with no mention of Metal: [github.com/newton-physics/newton](https://github.com/newton-physics/newton).

### Inferences
- At 10-50k vertices IPC-class solvers cost roughly 0.1 to 2 s per frame on a 4090 (YASPS/GIPC), so an M3 (much lower fp32 throughput and, as far as I know, no fp64-rich GPU) would likely be 10x or more slower. Full IPC is therefore an offline or "settle to rest" tool for Failoring, not a per-frame interactive one. (Inference.)
- Cheapest way to use IPC ideas now: link ipc-toolkit (MIT, macOS ARM64) for barrier, ACCD and friction derivatives inside the existing CPU Newton solve. Effort M. Risk: cost per Newton iteration from CCD plus line search; Failoring already has an experimental version of this.
- Porting GIPC/StiffGIPC to Metal would be XL: CUDA-specific warp-level hashing and CUB reductions, double precision, and Metal GPUs have no native fp64 (UNVERIFIED, from memory).

### Gaps
- No confirmed Metal, Vulkan or SYCL port of any IPC solver found.
- StiffGIPC and BAL code availability and licences not found.
- No per-frame numbers for IPC on Apple Silicon found.

## Q2. Cheaper robust alternatives: SDF contact with friction, impulses, Bridson, untangling, VBD, penalty with sub-stepping, "IPC on the cheap"

### Takeaway
The strongest 2025 candidate is OGC (offset-based contact with per-vertex displacement bounds, no CCD), designed to run with VBD or Newton and reported 100x+ faster than IPC. VBD with penalty contact is cheap but cannot guarantee non-penetration. The classics (Bridson 2002, implicit Coulomb solvers, untangling) remain the right references for friction and for recovering from penetration.

### Cited Findings
- **OGC**: Anka He Chen, Jerry Hsu, Ziheng Liu, Miles Macklin, Yin Yang, Cem Yuksel, "Offset Geometric Contact", ACM TOG 44(4), SIGGRAPH 2025, DOI 10.1145/3731205. [Project page](https://graphics.cs.utah.edu/research/projects/ogc/), [paper PDF](https://graphics.cs.utah.edu/research/projects/ogc/Offset_Geometric_Contact-SIGGRAPH2025.pdf).
  - Method: each face is offset along its normal to build a volumetric contact shape, and a two-stage (quadratic then log) activation function gives a C2 normal energy. Penetration is prevented by per-vertex displacement bounds that truncate solver steps, so CCD is avoided. Contact detection reruns only when vertices reach their bounds. Works with Newton, gradient descent and block coordinate descent.
  - Friction: IPC-style lagged friction with a stability modification from Chen et al. 2024, tested at mu 0.1 to 0.5.
  - Cost: cloth on a body, 24 s per frame for IPC versus 0.24 s (CPU, Ryzen 7950X) and 9.6 ms (GPU, RTX 4090, Warp). VBD-OGC on CPU is about 128x faster than IPC. A 13.8K-vertex robot and T-shirt scene averages 1.8 ms per step on GPU, and 50 layers of cloth (246K vertices) average 6.3 ms.
  - Limits: in the body-cloth test, body penetration can still occur because body motion is prescribed (directly relevant to Failoring's skinned body). Saddle vertices have empty blocks. Guarantees assume an intersection-free start, and the authors halt if intersections occur after a frame. Code: the paper says the C++ and Warp versions are planned for release, but the project page lists no code and no licence.
- **VBD**: Anka He Chen et al., "Vertex Block Descent", SIGGRAPH 2024, [arXiv 2403.06321](https://arxiv.org/pdf/2403.06321). Vertex-wise Gauss-Seidel on implicit Euler, with a fixed iteration budget. Contact is a quadratic penalty on penetration depth. Edge-edge uses CCD, and vertex-triangle uses CCD or DCD. Friction follows the IPC model with a smooth static/dynamic transition. Hardware: RTX 4090 for the solver and Embree on CPU for collision detection. Tearing cloth on CPU ran about 11.2 ms per step. Limits stated: cannot guarantee penetration-free results, poor convergence at a 1:10,000 stiffness ratio, and self-collision energy for codimensional objects is hard. Cloth penalty tuning is therefore the risk.
- **AVBD (augmented VBD)**: SIGGRAPH 2025, [Utah project PDF](https://graphics.cs.utah.edu/research/projects/avbd/Augmented_VBD-SIGGRAPH25_RTL.pdf) and [Yuksel PDF](https://cemyuksel.com/research/papers/Augmented_VBD-SIGGRAPH25.pdf), DOI 10.1145/3731195 (via [Crossref](https://api.crossref.org/works/10.1145%2F3731195)). Adds an augmented Lagrangian for hard constraints (suggested alpha 0.95, beta 10, gamma 0.99). Demos are rigid blocks and joints: a 110,000-block pile at 3.5 ms per frame on a 4090, "stable stacking" with 4 iterations per frame. The page read does not say it handles cloth, and I found no licence.
- **Bridson, Fedkiw, Anderson (2002)**: "Robust Treatment of Collisions, Contact and Friction for Cloth Animation", SIGGRAPH 2002, [project page](https://graphics.stanford.edu/papers/cloth-sig02/), [PDF](https://www.cs.ubc.ca/~rbridson/docs/cloth2002.pdf). Works with any internal dynamics, models cloth thickness, and adds collision-aware subdivision for rendering. The mechanism details (repulsion springs, rigid impact zones, friction) are in the PDF, which I did not read.
- **Implicit Coulomb friction for garments**: Li, Daviet, Narain, Overby, Boissieux et al., "An implicit frictional contact solver for adaptive cloth simulation", SIGGRAPH 2018 (year from the [SIGGRAPH history entry](https://history.siggraph.org/?p=100711), which does not state it explicitly). Exact Coulomb friction for cloth-cloth and cloth-body contact, velocity-based formulation without the Delassus operator, adaptive node refinement. This is the best-fit classic for friction realism on a body (failure 3), and the CPU-friendly route. Code/licence not found.
- **Untangling**: Ye and Zhao, "The Intersection Contour Minimization Method for Untangling Oriented Deformable Surfaces", SCA 2012, DOI 10.2312/SCA/SCA12/311-316, [EG digital library](https://diglib.eg.org/items/7cf03072-dbcd-4705-8f12-b77760e392ae). History-free, adds repulsive normals, also handles mesh-vs-capsule. This is the Volino and Magnenat-Thalmann style approach (the original 2006 paper's page was not read, see Gaps).
- **GPU two-way collision handling**: Wang, Chen, Li, Liu, Huamin Wang, Zhou, "Fast GPU-Based Two-Way Continuous Collision Handling", [arXiv 2211.04045](https://arxiv.org/pdf/2211.04045). Impact-zone-style backward step plus a forward step with distance-based safe step sizes (5 to 7x faster than CCD-based schemes). Bow knot, 71K vertices: 0.034 s per step average on an RTX 2080 Ti with CUDA 11.2/CUB. No code. The sample count of the paper includes cloth, hair and sand.
- **I-Cloth** (Tang et al., incremental collision handling for GPU cloth, 2018): only listed in search results ([SIGGRAPH history](https://history.siggraph.org/?p=197988)); the page was not read, so no claims on cost.

### Inferences
- For Failoring the highest value/effort pairing is probably "OGC-style per-vertex displacement bounds, plus lagged IPC-style friction, inside the existing CPU Newton solve". It removes CCD from the inner loop, which was the main per-iteration cost in IPC. Effort M to L, risk: no reference code yet, and the paper's own caveat that prescribed body motion can still produce body penetration. (Inference.)
- Implicit Coulomb friction (Li 2018) is relevant to sliding on skin and to friction realism, but the Delassus-free formulation is a larger rewrite than lagged-friction barrier contact. Effort L. (Inference.)
- A rigid-impact-zone fallback (Bridson style) is useful as a last-resort untangler for the rare case a step still ends in penetration. Effort S to M. (Inference.)

### Gaps
- Original Volino and Magnenat-Thalmann 2006 paper, Baraff-Witkin-Kass 2003 global intersection analysis, Harmon et al. 2008 "Robust treatment of simultaneous collisions", Ando "continuous collision with SDF", and any "IPC on the cheap" named paper: not located or read in this session. UNVERIFIED: that a paper with exactly that title exists.
- No source read that evaluates penalty contact with sub-stepping for cloth against SDFs specifically.

## Q3. GPU self-collision broad phase, multi-layer garments, thickness, pinch points, body-penetration recovery

### Takeaway
Evidence I could read covers broad-phase approaches only indirectly (grid hashing in the two-way paper, Embree on CPU in VBD, hash-based reductions in StiffGIPC). Layered cloth and thickness are best documented by Codim-IPC (explicit thickness and thin-cloth stacks) and OGC (50 stacked layers at 6.3 ms/step on a 4090).

### Cited Findings
- Grid-based spatial hashing proximity search reused across steps: [arXiv 2211.04045](https://arxiv.org/pdf/2211.04045).
- VBD runs collision detection on the CPU with Intel Embree (BVH) while parallel solver loops run on the GPU: [arXiv 2403.06321](https://arxiv.org/pdf/2403.06321).
- OGC: 50 cloth layers, 246K vertices, 475K triangles, GPU average 6.3 ms per step (CPU 0.21 s); no separate thickness model, the offset radius acts as the contact radius: [OGC paper](https://graphics.cs.utah.edu/research/projects/ogc/Offset_Geometric_Contact-SIGGRAPH2025.pdf).
- Codim-IPC thickness: mid-surfaces held at a set thickness with an extra elastic layer d-hat, consistent from 1 mm to 10 mm stacks: [arXiv 2012.04457](https://arxiv.org/abs/2012.04457).
- Mil2 targets no-intersection high-resolution garments on GPU via non-distance barriers (lifespan-based adaptive weights): [arXiv 2403.19272](https://arxiv.org/abs/2403.19272).
- Garment stacks and pinching are shown only as scene examples in the above; no source I read gives a dedicated armpit or crotch pinch-point method.

### Inferences
- The armpit flicker (failure 1) is most plausibly cloth squeezed between two colliders (arm and torso SDF regions) where penalty/Coulomb contact alternates between active sets. Smooth barrier or offset energies (IPC, OGC with C2 activation) remove the on/off switch, which is the standard explanation for why they rest cleanly. The mechanism diagnosis for Failoring is my inference and has not been tested.
- A practical remedy independent of the solver: keep the cloth rest-offset (thickness) larger than the minimum arm-torso gap, or locally cap the SDF's inflation in pinch regions. UNVERIFIED engineering suggestion.
- A GPU broad phase on Metal can be hash-grid based (simple, no CUDA dependency). LBVH refit is also portable but more effort. Effort S to M. (Inference; no Metal-specific source found.)

### Gaps
- No source located on LBVH refit vs hashing cost for 10-50k vertex garments, or on production pinch-point handling.
- Body-penetration recovery for garments: only the generic untangling literature (ICM) and OGC's stated limitation were found; no garment-specific recovery paper read.

## Q4. Contact against animated skinned bodies: SDF update cost, tunnelling, friction on moving skin

### Takeaway
I found no source read this session that benchmarks per-frame SDF baking for a skinned body, so Failoring's current SDF-per-frame design is unverified against the literature here. Both IPC and OGC treat the body as a prescribed-motion collider, and OGC's paper explicitly warns that cloth can still end up inside a prescribed-motion body.

### Cited Findings
- OGC: in the body-cloth test, body penetration can still occur because body motion is prescribed: [OGC paper](https://graphics.cs.utah.edu/research/projects/ogc/Offset_Geometric_Contact-SIGGRAPH2025.pdf).
- IPC on the cloth-on-body scene costs 24 s per frame versus 9.6 ms for GPU OGC (same source).
- Two-way CCD handles fast-moving blades with distance-based safe steps (no CCD): [arXiv 2211.04045](https://arxiv.org/pdf/2211.04045).
- ipc-toolkit provides CCD usable for moving colliders: [PyPI ipctk](https://pypi.org/project/ipctk).

### Inferences
- Tunnelling risk is driven by body vertex speed per substep. Sub-stepping the body pose (interpolate the skin between frames) and clamping the SDF-contact displacement bound per vertex are the common mitigations. (Inference.)
- Friction on moving skin needs the relative tangential velocity (cloth minus body surface velocity at the contact point), which a baked SDF does not provide unless the body velocity field is also sampled. (Inference; check whether Failoring already samples it.)

### Gaps
- No located source on SDF update cost, narrow-band updates, or analytic skinned-body capsule-based colliders in games.
- No article on friction against moving skin read.

## Q5. Which methods produce truly still resting contact (no jitter)?

### Takeaway
Methods with smooth, C2 contact energy plus an implicit solve to convergence (IPC, Codim-IPC, OGC with a converging solver) are the ones with evidence of stable resting. Fixed-iteration or penalty methods (VBD, AVBD with few iterations) can show residual motion.

### Cited Findings
- OGC uses a two-stage C2-continuous activation and states that forces cannot be both orthogonal and continuous on a discrete surface: [OGC paper](https://graphics.cs.utah.edu/research/projects/ogc/Offset_Geometric_Contact-SIGGRAPH2025.pdf).
- IPC barrier family: smooth barrier plus lagged friction in ipc-toolkit: [PyPI ipctk](https://pypi.org/project/ipctk).
- AVBD demo: a pile "balanced by frictional contacts alone", with 4 iterations per frame: [AVBD PDF](https://graphics.cs.utah.edu/research/projects/avbd/Augmented_VBD-SIGGRAPH25_RTL.pdf). This is a rigid-body result, not cloth.
- VBD cannot guarantee penetration-free results and some penetration always remains with penalty contact: [arXiv 2403.06321](https://arxiv.org/pdf/2403.06321).

### Inferences
- Because lagged friction uses the previous iterate's normal force and sliding direction, it can still create micro-slip at rest unless friction is iterated to convergence. A smoothed friction with a nonzero static velocity threshold (as in IPC) gives creep, not perfect rest. Adding a sleep rule (freeze vertices below a velocity and force threshold) is a common product-level safeguard. UNVERIFIED as a source claim, offered as engineering advice.

### Gaps
- No quantitative resting-jitter metric from any source read.

## Q6. Open-source implementations: licence, backend, maintenance

### Takeaway
Usable now: ipc-toolkit (MIT, macOS ARM64) and Codim-IPC (Apache-2.0). GPU codes are CUDA only. OGC and Mil2 code was not confirmed.

### Cited Findings
- ipc-toolkit: MIT, v1.6.1 on 22 Oct 2026, macOS ARM64 wheels — [PyPI](https://pypi.org/project/ipctk).
- Codim-IPC: Apache-2.0, C++/Python, Ubuntu and macOS build scripts, only 5 commits — [GitHub](https://github.com/ipc-sim/Codim-IPC).
- GIPC: MPL-2.0, CUDA only, 39 commits, no releases — [GitHub](https://github.com/KemengHuang/GPU_IPC).
- AGIPC: [GitHub](https://github.com/KemengHuang/Adaptive-GIPC) (licence not read; CUDA is likely given the 4090-only evaluation, inference).
- Newton (NVIDIA Warp based, Linux Foundation): Apache-2.0 code, macOS CPU only, MuJoCo Warp primary backend, has "Cloth Style3D" and "VBD" examples — [GitHub](https://github.com/newton-physics/newton).
- OGC: code "planned" per the paper, no release or licence on the project page — [project page](https://graphics.cs.utah.edu/research/projects/ogc/).

### Inferences
- A Metal port of the OGC + VBD approach is the most promising "own GPU path" because the algorithms are local per-vertex kernels (graph colouring, no global sparse factorisation). Effort L. Not verified on Metal. Risks: no reference code, penalty tuning, VBD convergence at high stiffness.
- Rough ranking for Failoring (my judgment):
  1. ipc-toolkit barrier and friction in the existing Newton solve — effort M.
  2. OGC-style displacement bounds on CPU — effort M to L.
  3. Rigid impact-zone or ICM untangling fallback — effort S to M.
  4. Metal VBD+OGC — effort L to XL.
  5. Port of GIPC-class solvers — XL, not recommended.

### Gaps
- Apple/Metal: no source located showing any of these running on Metal, MoltenVK or Apple GPUs. Newton/Warp macOS is CPU only per its README. Treat Apple GPU as unsupported by all published code.
- Maintenance dates for Codim-IPC and GIPC repos not retrievable from the pages read.
