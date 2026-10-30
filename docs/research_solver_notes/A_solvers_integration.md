# Cloth solvers and time integration for interactive garment simulation (performance AND coming to rest)

Scope note: ~20 tool calls; only pages I actually fetched are cited. Items marked [UNVERIFIED] come from my background knowledge, were not re-checked, and must be confirmed before use. Effort scale: S<=1wk, M<=1mo, L<=3mo, XL more. Effort estimates are my inference. No Apple/Metal benchmark of any method below was found (see last section).

## 1. Vertex Block Descent (VBD) and follow-ups (AVBD, JGS2, PAA, 2025-26 variants): convergence, rest behaviour, stiff cloth, cost, GPU colouring

### Takeaway
Plain VBD is cheap and GPU-friendly (vertex colouring, 3x3 solves) but its own paper admits poor convergence on high stiffness ratios and no guarantee for infinitely stiff constraints. This matches the product's inextensible-woven-cloth problem. Follow-ups fix this (AVBD: augmented Lagrangian; JGS2: Newton-like convergence through a rest-shape subspace) but are CUDA-only, mostly volumetric, and I found no cloth rest-behaviour data for any of them.

### Cited Findings
- VBD: Chen, Liu, Yang, Yuksel (Utah/Roblox), SIGGRAPH 2024 (TOG); arXiv 2403.06321 v4. Tests on Ryzen 5950X + RTX 4090, CUDA, single precision; collision on CPU (Embree). Only one cloth scene reported: tearing cloth, 2,500 vertices / 4,800 tris, StVK, h=1/300 s, 20 iterations, 11.2 ms avg per step (the paper lists this scene as CPU). Large GPU numbers are volumetric: 97K verts/100 iters 60 ms; 230K verts/120 iters 15 ms — [arXiv 2403.06321](https://arxiv.org/pdf/2403.06321)
- VBD limitations stated by authors: slower convergence for stiffer materials; 1:10,000 stiffness ratio gives poor convergence and excessive extension at 100 iterations; infinitely stiff joints not guaranteed within a fixed iteration count; information travels only ~#colours per iteration so high-resolution stiff systems may need too many iterations; penalty collisions are not penetration-free — [arXiv 2403.06321](https://arxiv.org/pdf/2403.06321)
- VBD damping: simplified per-vertex Rayleigh damping added to the 3x3 system; without damping kinetic energy persists a long time, higher k_d settles faster (tearing cloth used k_d=1e-5) — [arXiv 2403.06321](https://arxiv.org/pdf/2403.06321)
- VBD colouring: vertices (not elements) are coloured; 8 colours for a 3,891-vertex mesh vs 76 element colours for 14,802 tets; collision forces use an auxiliary buffer, Jacobi-style across same-colour vertices — [arXiv 2403.06321](https://arxiv.org/pdf/2403.06321)
- VBD vs Newton: Newton converges far better per iteration but lost wall-clock in their tests; authors expect Newton to overtake for "very stiff, high-resolution" simulations. Accelerated VBD (Chebyshev-type, rho=0.95) beats plain VBD, GD, block-Jacobi, quasi-Newton on a 15K-vertex Armadillo — [arXiv 2403.06321](https://arxiv.org/pdf/2403.06321)
- AVBD: Giles, Diaz, Yuksel, "Augmented Vertex Block Descent", ACM TOG 44(4), SIGGRAPH 2025 (also RTL demo). Augmented-Lagrangian extension for hard (infinite-stiffness) constraints and high stiffness ratios; stacking, friction, articulated bodies. Demo source: savant117/avbd-demo2d and avbd-demo3d; page states no licence ("© University of Utah"); GPU implementation exists, no numbers on the page — [Utah AVBD page](https://graphics.cs.utah.edu/research/projects/avbd/); [paper PDF](https://cemyuksel.com/research/papers/Augmented_VBD-SIGGRAPH25.pdf) (PDF itself not read)
- JGS2: Lan, Lu, Yuan, Yang, Xu, Su, Wang (Style3D), Jiang; arXiv 2506.06494 (SIGGRAPH 2025 per file name of author-hosted PDF; venue not confirmed in the text I read). Jacobi/Gauss-Seidel per-vertex local Newton plus a co-rotated rest-shape subspace + cubature. Reports Newton-like convergence: stiff Armadillo 64 iters vs Newton 58 vs VBD not converging in 10,000 iters at h=1/100; house-of-cards VBD fails even at h=1/5000. Cloth: 2M-tri tablecloth (3M DOF), IPC contact, ~42 iters, 469 ms/frame on RTX 3090; precompute 48 min on CPU. No code/licence mentioned — [arXiv 2506.06494](https://arxiv.org/html/2506.06494v1)
- Periodic Anderson Acceleration for VBD (PAA): Yang, Navkar, Deng, PACMCGIT 9(4):61, July 2026. Anderson (m=2) every K=16 iterations with Chebyshev in between, fused GPU kernel; energy down 6-49% vs VBD+Chebyshev; overhead -5..+12%. Tet meshes only (692-15,864 verts; 50-93 ms/step for beams/armadillo on RTX 4090, Taichi 1.7.4 CUDA); no cloth results; both variants diverged on an armadillo-drop contact test — [HPG/PACMCGIT PDF](https://graphics.cs.uh.edu/wp-content/papers/2026/2026-HPG-PAA-PhysicalSim.pdf)
- Newton (NVIDIA/Linux Foundation, Warp-based) includes a VBD solver, XPBD, Style3D-style cloth examples and cloth USD import. Apache-2.0. v1.4.0 (tag dated 2026-07-16) improved VBD damping "consistency", added deterministic kernel paths for XPBD/VBD, a tuning guide for diagnosing instability. GPU acceleration requires NVIDIA (CC 7.5+); macOS is CPU-only; Metal not mentioned — [Newton repo](https://github.com/newton-physics/newton); [v1.4.0 notes](https://github.com/newton-physics/newton/releases/tag/v1.4.0)
- Generalised PBD (Chaudhary, Pokhariya, Narain), arXiv 2511.23131 (Nov 2025): Gauss-Seidel on per-force implicit-Euler form lets PBD-style solvers handle non-linear forces (data-driven cloth); claims gains over Newton at high resolution; abstract-only read, no hardware/code — [arXiv 2511.23131](https://arxiv.org/abs/2511.23131v1)

### Inferences
- For a near-inextensible woven fabric (huge in-plane vs bending stiffness ratio, plus heavy garments), plain VBD is the wrong default: the ratio is exactly where VBD degrades, and 14 mm swing of a tiered dress suggests under-converged stiff modes rather than lack of damping. AVBD-style stiffness ramping / augmented Lagrangian on the stretch term is the cheapest relevant idea (small code delta on top of existing VBD experiment: M).
- JGS2 is the strongest convergence story for stiff cloth but needs a rest-shape factorisation/eigen-precompute per garment (tens of minutes in their scenes) — likely incompatible with interactive pattern editing unless the subspace can be built from coarse levels. Effort L-XL, no public code found.
- Rest: none of these papers reports a "comes to rest" metric for cloth; VBD's undamped energy persistence is documented, so rest requires explicit damping or dissipative constraint handling (see section 5).

### Gaps
- AVBD paper body (cloth results? stiff-cloth numbers?) not read; licence of demo repos unstated.
- No cloth timing for any VBD variant on GPU beyond 2.5K verts; no Metal/MoltenVK port found.
- Did not locate other 2026 VBD cloth variants beyond PAA/Newton changes.

## 2. XPBD and Small Steps (Macklin et al.), later position-based variants

### Takeaway
Small Steps (Macklin, Storey, Lu, Terdiman, Chentanez, Jeschke, Müller; SCA/EG "Small Steps in Physics Simulation", 2019) is the reference for "many substeps, one iteration" — the structure the current GPU fast-solve resembles. I could only retrieve the catalogue entry, not the paper's content, so rest/inextensibility claims are unverified.

### Cited Findings
- Paper exists as "Small Steps in Physics Simulation", Eurographics Digital Library / ACM SCA 2019 (DOI 10.1145/3309486.3340247) — [EG DL](https://diglib.eg.org/handle/10.1145/3309486-3340247)
- VBD paper reports XPBD uses only inertia-Hessian-style approximations; in their squishy-ball test XPBD 0.32 s/frame vs VBD 0.031 s/frame at equal steps/120 iterations; XPBD crushes a 1:2000 mass-ratio light cube where VBD does not — [arXiv 2403.06321](https://arxiv.org/pdf/2403.06321)
- Generalised PBD (Narain group, 2025) says classical PBD only supports linearly compliant constraints — [arXiv 2511.23131](https://arxiv.org/abs/2511.23131v1)
- Newton ships XPBD as a solver alongside VBD (Apache-2.0, CUDA-only acceleration) — [Newton v1.4.0](https://github.com/newton-physics/newton/releases/tag/v1.4.0)

### Inferences
- [UNVERIFIED background] XPBD with Gauss-Seidel in small substeps is dissipative-ish and rests well on tight constraints, but needs graph-coloured GS (many dispatches, poor Metal occupancy at 10-50k verts); Jacobi XPBD needs under-relaxation and is soft. Effort S-M as it is easy to implement; but it does not give bending/stretch physical fidelity of FEM, so it would be a second path, not a replacement.

### Gaps
- Small Steps content (damping, rest behaviour, reported numbers) not retrieved. Original XPBD (Macklin, Müller, Chentanez 2016) not fetched.
- No evidence gathered on whether XPBD "comes to rest" for garments.

## 3. Projective Dynamics, GPU variants, ADMM, Chebyshev, Anderson, quasi-Newton

### Takeaway
Anderson acceleration (Peng et al. 2018), Chebyshev with Jacobi (Wang & Yang 2016) and Lan et al. 2024 GPU PD with subspace reuse are mature options for accelerating local-global solvers; all require a constant (quadratic) system and none address rest per se.

### Cited Findings
- Anderson Acceleration for Geometry Optimization and Physics Simulation: Peng, Deng, Zhang, Geng, Qin, Liu; ACM TOG 37(4), Art. 42, SIGGRAPH 2018; m=5, safeguarded to monotone energy; matches L-BFGS iteration progress at lower cost; cloth first-frame examples; CPU OpenMP. Code: https://github.com/bldeng/AASolver (licence not checked) — [arXiv 1805.05715](https://arxiv.org/pdf/1805.05715)
- Descent methods for elastic body simulation on the GPU: Wang & Yang, SIGGRAPH Asia 2016; Jacobi preconditioning + Chebyshev acceleration, no dot products so GPU-friendly, convergence comparable to L-BFGS/nonlinear CG, "robust against very large time steps"; can complement multigrid — [SIGGRAPH history entry](https://history.siggraph.org/?p=210550)
- Lan et al., "Efficient GPU Cloth Simulation with Non-distance Barriers and Subspace Reuse" (arXiv 2403.19272): PD-based, RTX 3090; fashion show 340K verts 4.8 FPS at dt=1/200; 104K-vert knot 11.8 FPS; 40K-vert drape on sphere 25 FPS; strip bending 30K DOF >300 FPS vs <0.5 FPS PD-IPC; rest-shape low-frequency eigen subspace + aggregated Jacobi: ~20 iterations vs >1000 without. Residual forwarding carries unresolved residual into next step. Requires quadratic elasticity; no code mentioned; venue not stated in text read — [arXiv 2403.19272](https://arxiv.org/html/2403.19272v4)
- WebGPU mass-spring cloth (arXiv 2507.11794): 640K nodes at 60 fps on RTX 4070 Ti (hanging cloth), but a mass-spring/Euler scheme with atomic adds, not an inextensibility solution; no Metal results — [arXiv 2507.11794](https://arxiv.org/pdf/2507.11794)

### Inferences
- Anderson/Chebyshev on top of an existing block-Jacobi CG or VBD loop is an S-M change and the most direct "same budget, lower residual" lever. Caveat: the PAA paper (2026) indicates acceleration can diverge in contact-heavy scenes, and the VBD paper skips acceleration for colliding vertices.
- Subspace reuse (Lan 2024) is promising for a fixed garment: the low-frequency subspace gives global awareness that a 100-iter capped block-Jacobi lacks, likely the source of tiered-dress swing. Effort L; hardware is CUDA (inference: implementable in Metal compute, unverified).
- [UNVERIFIED] Projective Dynamics (Bouaziz 2014), ADMM (Overby 2017), Liu 2017 quasi-Newton PD were not fetched in this session.

### Gaps
- No Metal/Apple data; no source on ADMM-based cloth for rest behaviour.

## 4. Multigrid, domain decomposition, GPU PCG, GPU sparse direct, Newton line search on GPU

### Takeaway
Smoothed-aggregation multigrid is the cloth-specific classic (6-8x on 371K-vertex dressed character, CPU). GPU multigrid results exist but are CUDA. Nothing located gives a sparse Cholesky for Metal.

### Cited Findings
- Smoothed Aggregation Multigrid for Cloth Simulation: Tamstorf, Jones, McCormick; ACM TOG 34(6), SIGGRAPH Asia 2015; algebraic MG robust to unstructured meshes and varying materials; "prefiltered PCG" for contact constraints; 6-8x on 371K-vertex dressed character; hardware and code not stated on the page — [Disney Research](https://studios.disneyresearch.com/2015/11/04/smoothed-aggregation-multigrid-for-cloth-simulation/); [WDAS](https://www.disneyanimation.com/publications/smoothed-aggregation-multigrid-for-cloth-simulation/)
- Xian et al. "A scalable Galerkin multigrid method for real-time simulation of deformable objects", SIGGRAPH Asia 2019, DOI 10.1145/3355089.3356486 (listing only) — [Crossref](https://api.crossref.org/works/10.1145%2F3355089.3356486)
- A 2025 TOG follow-up "Fast Galerkin" exists at cg.cs.tsinghua.edu.cn/papers/TOG-2025-Fast-Galerkin.pdf but the fetch failed; title and content not verified — [search listing](https://cg.cs.tsinghua.edu.cn/papers/TOG-2025-Fast-Galerkin.pdf)
- The PAA paper cites 5-8 s per substep for GPU multigrid PBD on their meshes (their comparison, tets) — [PAA PDF](https://graphics.cs.uh.edu/wp-content/papers/2026/2026-HPG-PAA-PhysicalSim.pdf)
- GarmentDynamics (Style3D-affiliated, from RGBench, arXiv 2511.06434): GPU implicit solver; 2.7 ms min step; 5k-40k cloth vertices; 3-7x faster than Isaac Sim (26.6 ms at 40k); GPU model unspecified; to be released, licence unstated. Paper describes no explicit settling/rest procedure — [arXiv 2511.06434](https://arxiv.org/pdf/2511.06434)
- Apple-side: only third-party/GitHub community items surfaced (e.g. shoyamanishi/applenumericalcomputing, MLX sparse packages); none verified as a Metal sparse Cholesky — [search results](https://github.com/shoyamanishi/applenumericalcomputing)

### Inferences
- [UNVERIFIED] cuDSS is NVIDIA-only; on Apple, direct solves go through Accelerate (CPU) — this keeps the CPU supernodal Cholesky as the only exact option. Practical Metal path: PCG with block-Jacobi + (smoothed-aggregation) coarse correction written in compute shaders; effort L.
- Interactive pattern editing favours a static-topology AMG hierarchy rebuilt on edit, with the coarse solve on CPU Cholesky (small) — an architecture suggestion, not a cited result.

### Gaps
- Xian 2019 and Fast Galerkin 2025 contents not read; no GPU multigrid cloth numbers on Apple hardware; no Newton-line-search-on-GPU source found.

## 5. Dissipative methods, nonsmooth strain limiting, constraint-based inextensibility (to remove the 3% projection)

### Takeaway
The classical route to inextensibility is Goldenthal et al. 2007 (constraints solved via a fast projection inside implicit integration), which removes the need for ad-hoc strain limiting; its GPU/parallel follow-up exists. I could not retrieve the content, so mechanics below are unverified.

### Cited Findings
- "Efficient Simulation of Inextensible Cloth", Goldenthal, Harmon, Fattal, Bercovier, Grinspun, SIGGRAPH 2007 (project page: Columbia) — [project page](https://www.cs.columbia.edu/cg/ESIC/esic.html); [SIGGRAPH history](https://history.siggraph.org/?p=115358)
- "Parallel Simulation of Inextensible Cloth", VRIPHYS 2008 (RWTH Aachen) — [PDF](https://animation.rwth-aachen.de/media/papers/2008-VRIPHYS-Parallel_Inextensible_Cloth.pdf) (not read)
- VBD: without damping, energy persists; Rayleigh damping per vertex speeds settling — [arXiv 2403.06321](https://arxiv.org/pdf/2403.06321)
- Lan 2024 "residual forwarding": carrying unresolved residual to the next step so early termination does not leave systematic error — [arXiv 2403.19272](https://arxiv.org/html/2403.19272v4)
- Newton v1.4.0 adds damping consistency and a stability tuning guide — [release](https://github.com/newton-physics/newton/releases/tag/v1.4.0)

### Inferences
- Likely cause of the non-rest (inference): 1 linearised Newton solve per substep with a 100-iter capped block-Jacobi leaves a nonzero residual that is re-injected each substep, so the state never reaches a fixed point; the strain-limit projection then fights it, creating limit-cycle jitter at pinch points. Residual-forwarding-like handling, converging to tolerance on the final substep, or a fully implicit (energy-decreasing, line-searched) step are the principled fixes.
- Standard remedy for rest: energy-decreasing implicit step (backtracking line search guarantees monotone decrease) plus velocity damping applied after the solve (not inside force Jacobian only), plus sleep/velocity-threshold snapping. [UNVERIFIED as published practice in these sources; standard engineering.]

### Gaps
- Did not retrieve Goldenthal's algorithm details, Thomaszewski/Bender nonsmooth work, or any 2023-26 paper explicitly benchmarking "time to rest" of cloth solvers.

## 6. Realistic numbers and transfer to Apple GPU / Metal

### Takeaway
All headline GPU numbers below are NVIDIA/CUDA (RTX 3090/4090/4070 Ti). No located source reports cloth on an Apple GPU or MoltenVK. An M3 laptop GPU is far slower than a 4090 [UNVERIFIED ratio], so treat numbers as upper bounds.

### Cited Findings
- VBD: 15K-vert Armadillo 3.3 ms/step (100 iters); 2.5K-vert cloth 11.2 ms/step (20 iters, CPU row) — RTX 4090, CUDA — [arXiv 2403.06321](https://arxiv.org/pdf/2403.06321)
- Lan 2024: 40K-vert drape 25 FPS, 340K-vert 4.8 FPS (RTX 3090, CUDA) — [arXiv 2403.19272](https://arxiv.org/html/2403.19272v4)
- JGS2: 2M-tri cloth 469 ms/frame (RTX 3090) — [arXiv 2506.06494](https://arxiv.org/html/2506.06494v1)
- GarmentDynamics: 40k verts ~ several ms/step (unspecified GPU) — [arXiv 2511.06434](https://arxiv.org/pdf/2511.06434)
- WebGPU spring cloth: 60 fps to 640K nodes, RTX 4070 Ti, no Metal — [arXiv 2507.11794](https://arxiv.org/pdf/2507.11794)
- Newton: GPU acceleration NVIDIA-only; macOS CPU-only — [Newton repo](https://github.com/newton-physics/newton)

### Inferences
- Algorithms built from per-vertex 3x3 solves and SpMV map directly to Metal compute; the barriers are CUDA-specific libraries (cuDSS, cuBLAS), not the algorithms. Warp-based code (Newton) is not a drop-in; study it, reimplement kernels in MSL.

### Gaps
- No M-series timings anywhere; ask for a quick in-house benchmark of the existing VBD experiment on M3 as the calibration point.

## 7. Open-source implementations worth studying

### Cited Findings
- NVIDIA/Linux Foundation Newton: Apache-2.0, active (2,760 commits, 5.7k stars; v1.4.0 July 2026), VBD + XPBD + cloth examples; CUDA/CPU only — [repo](https://github.com/newton-physics/newton)
- AVBD demos (2D/3D): github.com/savant117/avbd-demo2d, avbd-demo3d; licence unstated — [Utah page](https://graphics.cs.utah.edu/research/projects/avbd/)
- AASolver (Anderson acceleration PD): https://github.com/bldeng/AASolver (licence not checked) — [arXiv 1805.05715](https://arxiv.org/pdf/1805.05715)
- Houdini-VBD (MysteryPancake) hobby implementation surfaced in search; licence/quality unchecked — [repo](https://github.com/MysteryPancake/Houdini-VBD)
- Taichi-based PAA code: not located.

### Gaps
- Genesis, Taichi cloth samples, and a Metal-native cloth codebase were not investigated.

## Summary table (effort estimates are inferences)

| Method | Year / venue | Addresses | Evidence | GPU | Effort |
|---|---|---|---|---|---|
| VBD + Chebyshev | 2024 TOG | cost per iteration | 2.5K cloth only | CUDA | S-M (already experimental) |
| AVBD (stiffness ramp / aug. Lagrangian) | 2025 TOG | stiff ratios, hard constraints | demos | CUDA demo | M |
| JGS2 | 2025 | stiff convergence | 2M tri cloth 469 ms 3090 | CUDA | L-XL |
| Anderson / Chebyshev on PD or block-Jacobi | 2018 / 2016 | iteration count | CPU/GPU | portable | S-M |
| PD + subspace reuse + RF (Lan) | 2024 | gathers, residual, big scenes | 40K verts 25 FPS 3090 | CUDA | L |
| Smoothed-aggregation MG | 2015 TOG | CG scaling | 6-8x, 371K verts | CPU | L |
| Goldenthal inextensible | 2007 | remove 3% hack | not read | CPU | M-L (inferred) |
| XPBD / small steps | 2016/2019 | speed, resting at tight constraints | not read | portable | S-M |
