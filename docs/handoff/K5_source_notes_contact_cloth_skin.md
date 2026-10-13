# Cloth-cloth and cloth-body/skin contact: implementable ideas for Failoring

Research date: 2026-10-13. Budget was about 15 tool calls, so coverage is partial. Items marked "[from memory, not re-verified]" come from my background knowledge and were not opened this session. Nothing below is an invented citation, but unverified items are flagged and should be checked before being relied on.

## 1. Minimal IPC-style penetration-free cloth-cloth and cloth-body contact on a CPU Newton solver

### Takeaway
A minimal version is feasible: keep the existing implicit Euler / Newton / sparse Cholesky loop, add point-triangle AND edge-edge barrier (or constraint) terms against both cloth and a mesh version of the body, and add a CCD-limited step size. The open ipc-toolkit (MIT) supplies the barrier, CCD, adaptive stiffness and friction pieces. Newer 2025-2026 work (barrier-free augmented Lagrangian, cubic barrier) targets exactly the ill-conditioning and "TOI locking" that a sparse Cholesky Newton solver would suffer from.

### Cited Findings
- ipc-toolkit is MIT licensed. It provides the IPC barrier and derivatives, an adaptive barrier stiffness algorithm, broad- and narrow-phase CCD for linear and nonlinear trajectories, and smooth and lagged dissipative friction potentials with derivatives. Dependencies are fetched by CMake (so not dependency-free; port the maths rather than link it). — [ipc-toolkit](https://github.com/ipc-sim/ipc-toolkit)
- Codim-IPC (Li, Kaufman, Jiang, SIGGRAPH 2021) source is Apache-2.0, covers cloth, hair, discrete shells, strain limiting and CCD; it needs Boost, Eigen, GMP, pybind11, so it is a reference to read, not to link. — [Codim-IPC](https://github.com/ipc-sim/Codim-IPC)
- Ando, "A Cubic Barrier with Elasticity-Inclusive Dynamic Stiffness", ACM TOG 43(6), 2024 (SIGGRAPH Asia 2024). Implemented in ppf-contact-solver, Apache-2.0, maintained by Ryoichi Ando; shells, solids, rods, rigid bodies, sand; strain limits 1-10 percent; fabric presets calibrated to real measurements; the repo claims zero penetration after runs in CI. It requires an NVIDIA GPU with CUDA 12.8 and single precision, so it is a reference for the maths only. — [ppf-contact-solver](https://github.com/st-tech/ppf-contact-solver), [ACM DL](https://dl.acm.org/doi/abs/10.1145/3687908)
- Zheng, Luo, Li, "Robust and Efficient Penetration-Free Elastodynamics without Barriers", arXiv 2512.12151 (v3 2026-05-28; DOI 10.1145/3811035, so an ACM publication; venue not stated on the arXiv page). Replaces log barriers with a second-order constrained optimisation using a custom augmented Lagrangian; contact pairs from CCD; all required pairs added immediately to avoid time-of-impact (TOI) locking; adaptive multipliers instead of rising penalty stiffness; active-set filtering/decay; up to 103x speedup over GIPC (GPU). Code at the project page. The page text gave no explicit statement on cloth or friction. — [arXiv](https://arxiv.org/abs/2512.12151), [project](https://simulation-intelligence.github.io/barrier-free)
- Lan et al., "Efficient GPU Cloth Simulation with Non-distance Barriers and Subspace Reuse" (Mil2), arXiv 2403.19272 (2024): barrier based on the virtual life span of a collision rather than distance, inside projective dynamics. No code link found. — [arXiv](https://arxiv.org/abs/2403.19272)
- libuipc is Apache-2.0, GPU-only (CUDA 12.8), simulates rigid, soft, cloth and thread with "penetration-free and accurate frictional contact". — [libuipc](https://github.com/spiriMirror/libuipc)
- Chen, Hsu, Ayman, Macklin, "Divide and Truncate", arXiv 2604.15513 (2026-04-16, cs.GR, venue not stated): partitions space into exclusive regions and truncates displacements to stay inside; Planar-DAT only constrains motion toward nearby surfaces; solver-agnostic, can be a post-process after any iterative optimiser; couples rigid, volumetric soft, shells, rods. No code link on the page. — [arXiv](https://arxiv.org/abs/2604.15513)
- Foundational, not re-opened: Li et al., "Incremental Potential Contact", ACM TOG 39(4), SIGGRAPH 2020; Lan et al., "Affine Body Dynamics", TOG 2022; Huang et al., GIPC (GPU Gauss-Newton IPC), TOG 2024 [from memory, not re-verified]. I could not verify what "Gaia" and "Stiffness-adaptive" refer to; no source found, so treat as unverified.

### Inferences
- IDEA A1 "Edge-edge + point-triangle IPC barrier, CPU, lagged": the smallest guaranteed-intersection-free upgrade. Outline: (1) add edge-edge candidate pairs to the existing spatial hash; (2) add a smoothly clamped barrier b(d, dhat) per pair with offset d0 (thickness) so the barrier acts on d - thickness; (3) add barrier gradient/Hessian (project each 12x12 block to PSD, the Gauss-Newton style Failoring already uses) into the assembled system; (4) before each line-search step, compute a CCD max step alpha and cap at 0.9 alpha; (5) adaptive kappa as in ipc-toolkit. Payoff: high for thin tangles passing and for cloth-cloth. Difficulty: weeks. Risks: contact Hessian adds fill to the supernodal Cholesky (pattern changes each iteration, so symbolic factorisation must be redone or contact pairs pre-registered); log barrier ill-conditioning near dhat; CCD robustness (needs conservative CCD, e.g. Tight-Inclusion-style; ipc-toolkit has it, MIT, portable).
- IDEA A2 "Barrier-free augmented-Lagrangian active-set contact": directly attacks wedged-garment jitter and TOI locking; constraints are enforced by multipliers which are well matched to a quasi-static solve and to the author's "feel the cloth" goal (multipliers are contact forces, giving pressure readouts for free). Payoff: high; difficulty: weeks to months (new paper, formulation more complex, I have only the abstract-level view); risk: authors' code is GPU-oriented; cloth and friction behaviour not confirmed from the page.
- IDEA A3 "Cubic barrier with elasticity-inclusive stiffness": replaces the per-step adaptive kappa heuristic with a stiffness derived from the local elastic Hessian, so contact stiffness scales with the cloth stiffness. Payoff: medium-high (fewer line-search failures, no hand tuning); difficulty: weeks; risk: only the repo summary was read, not the paper maths; paper PDF is on a Box link.
- IDEA A4 "Divide-and-truncate as a safety post-process": cheap guard on top of the current projection collider and the vertex-triangle self-contact to guarantee no tunnelling without changing the Newton system. Payoff: medium; difficulty: weeks; risk: preprint only, no code seen, unverified behaviour on dense cloth; truncation can cause damping/deadlock (Planar-DAT claims to reduce that).
- Cost with sparse Cholesky: contact Hessian blocks are local, so for cloth at Failoring scale (tens of thousands of vertices) a direct factorisation per Newton iteration is likely fine; the real cost is refactoring when the contact set changes. In a quasi-static workflow this is acceptable.

### Gaps
- Exact iteration counts and timings of IPC-style CPU Newton on garment-scale meshes were not found.
- Did not open the Ando paper PDF, the Huang GIPC paper, or the Gaia reference; "Gaia" and "Stiffness-adaptive" not identified.
- Friction inside IPC (lagged vs implicit) read only at the level of the ipc-toolkit feature list.

## 2. Layered cloth and thickness

### Takeaway
Thickness-aware codimensional contact and per-layer offsets are well supported in IPC (barrier offset d0), but naive thickness on fine meshes produces spurious contact "locking". Oct 2025 Kaufman-group work addresses exactly this for shells.

### Cited Findings
- Gomez-Nogales, Chen, Martin, Garces, Kaufman, "Unlocking Thickness Modeling for Codimensional Contact Simulation", arXiv 2510.10256 (2025-10-11, preprint, no venue stated): thickened codim models lock as the mesh is refined because neighbouring elements generate spurious contact forces; prior fixes coarsen the mesh or cull pairs (which allows tunnelling); the paper proposes a contact-processing model that is mesh-resolution independent and aims for guaranteed locking-free, intersection-free results with rod and shell models and IPC barrier. Page mentions no layered cloth and no code. — [arXiv](https://arxiv.org/abs/2510.10256)
- Variational r-adaptive cloth with frictional contact: Wen, Chen, Barbic, Kaufman, arXiv 2608.17833 (2026-08-18, venue not stated): first r-adaptive cloth method for dynamics and statics with frictional contact, regulariser against element collapse, 3-6x faster trust-region solves. No code link found. — [arXiv](https://arxiv.org/abs/2608.17833)
- Mil2's page text did not mention many-layer cloth; no verified claim of layer scaling. — [arXiv](https://arxiv.org/abs/2403.19272)

### Inferences
- IDEA B1 "Per-layer thickness and contact offsets in the barrier": give each panel/garment a thickness t_i from fabrics.json and use d - (t_i + t_j)/2 inside the barrier, with a floor so a seam allowance (stack of 2-4 plies) is thinner than the naive sum. Payoff: medium-high for waistband over trousers and sleeve vs torso; difficulty: days once A1 exists; risk: mesh-resolution locking, see the 2510.10256 paper, so implement its remedy or restrict dhat to be small relative to edge length.
- IDEA B2 "Layer ordering as a prior, not a constraint": at the start of a session, record each garment's layer index (underwear < shirt < trousers < belt < jacket) and initialise by inflating outer layers radially; contact is then resolved by IPC from an intersection-free start. Payoff: medium; difficulty: days; risk: IPC cannot untangle an already-intersecting start, so the initial state must be clean (a known IPC requirement).
- IDEA B3 "Seam allowance as a two-ply thick strip": instead of point-to-segment springs only, add a local contact exclusion between the two plies sharing a seam (ignored in the barrier) to avoid fighting with the seam spring. Payoff: low-medium; difficulty: days.

### Gaps
- No paper found specifically on garment-over-garment contact ordering with layer-aware resolution in 2023-2026; none verified.
- Layer-count scaling of IPC for 3+ layers not found.

## 3. Friction models for cloth

### Takeaway
Little verified quantitative data was obtained. Measured fabric-skin friction exists in textile literature and is strongly humidity and fabric dependent; within IPC, lagged smoothed Coulomb friction is available in the open toolkit.

### Cited Findings
- ipc-toolkit provides smooth and lagged dissipative friction potentials with derivatives. — [ipc-toolkit](https://github.com/ipc-sim/ipc-toolkit)
- Textile-skin friction literature: a 2011 J. Fiber Bioengineering and Informatics study (vol. 4, no. 4, pp. 349-357) measured friction between 20 woven fabrics and human skin under 6 wet conditions with a custom tester; the full numbers were paywalled, so no coefficients can be quoted. — [JFBI](https://global-sci.com/article/86811/experimental-method-and-characterization-of-friction-properties-between-woven-fabric-and-human-skin-in-wet-condition)
- A JKU Linz contribution on skin-to-textile friction measurement exists (listing only, not opened). — [JKU](https://research.jku.at/en/publications/a-contribution-to-the-measurement-of-skin-to-textile-friction/)

### Inferences
- IDEA C1 "Move friction into the Newton solve (lagged, per-iteration)": today Coulomb friction is applied after the solve, which causes jitter on wedged cloth because normal force and friction are inconsistent. Lagged friction (freeze normal force and tangent basis from the previous Newton iteration or step, include the smoothed friction potential and its PSD-projected Hessian) is standard in IPC. Payoff: high for the jitter problem; difficulty: weeks; risk: lagged friction converges only to a fixed point so a few outer iterations are needed.
- IDEA C2 "Per-surface-pair mu table": body skin vs fabric, fabric vs fabric, via a small matrix of mu stored in fabrics.json; optionally an anisotropic mu along warp/weft. Payoff: medium; difficulty: days; risk: no verified coefficient values, must be calibrated or exposed as a slider.
- Stick-slip and velocity-dependent friction: only relevant for offline motion sequences; deprioritise for the quasi-static tailoring loop.

### Gaps
- No verified numeric fabric-skin or fabric-fabric friction coefficients; no verified anisotropic cloth friction paper in 2023-2026.

## 4. Cloth vs soft body (flesh) coupling

### Takeaway
No verified 2024-2026 paper directly fits "tight cloth on a precomputed rigid mannequin". The searches returned only older skin-tight clothing work. The practical route for Failoring is a compliance layer on the collider, driven by the contact pressure the solver already knows.

### Cited Findings
- Computational design of skintight clothing (SIGGRAPH 2020 TOG, listed on ACM DL; not opened this session). — [ACM DL](https://dl.acm.org/doi/abs/10.1145/3386569.3392477)
- Divide and Truncate couples volumetric soft bodies with shells in a material-agnostic way (preprint, 2026). — [arXiv](https://arxiv.org/abs/2604.15513)
- Libuipc handles soft body and cloth coupling with FEM soft bodies on GPU. — [libuipc](https://github.com/spiriMirror/libuipc)
- Dyna / SoftSMPL / STAR soft-tissue models exist [from memory, not re-verified]; I did not open any of them and cannot cite URLs.

### Inferences
- IDEA D1 "Compliant skin layer on the SDF (Winkler foundation)": replace the hard surface at distance 0 by a stiff-then-soft contact law: if the cloth is at signed distance s < 0 below the skin surface, the body pushes with pressure p = k_region * penetration depth up to a maximum depth (soft-tissue thickness, e.g. a few mm to a cm depending on region); beyond that the surface is rigid. This is a nonlinear spring layer in the Newton solve (energy 0.5 k s^2 for s<0 with smooth transition), with k and max depth from a per-region flesh stiffness map painted on the MakeHuman UV. It directly fixes "plus-size never settles" because the cloth can sink the planned 0.6 mm without a constraint fight, and it gives physical pressure output (cloth pressure on body in kPa). Payoff: high; difficulty: days; risk: calibration values unverified (no cited tissue stiffness in this research); the compliance must be stiff relative to the garment tension to avoid unrealistic indentation; inter-thigh flesh would need self-collision of the body (see D2).
- IDEA D2 "SDF that moves with load (two-way, one-way baked)": after a settle, compute the displacement field of the flesh layer u(x) = -p(x)/k and warp the SDF values by u (cheap because the SDF is only used near the surface) or add the displacement to the mannequin's vertices and re-bake only the narrow band. For belly/thigh squeeze, apply a volume-preservation correction by laterally bulging neighbouring regions (a simple Laplacian smoothing of u plus a divergence term). Payoff: medium; difficulty: weeks; risk: bulging has no verified source; visual only.
- IDEA D3 "Quasi-static FEM flesh shell": a thin tetrahedral or shell-spring layer under the skin, solved in the same Newton system (full two-way coupling). Payoff: high realism but difficulty: months; risk: needs a flesh mesh and tissue parameters (none verified), and increases system size.
- Feasibility with a precomputed rigid mannequin: D1 is fully compatible; D2 needs a partial re-bake of the SDF; D3 needs new data.

### Gaps
- No 2024-2026 paper found for cloth on soft body specifically (waistband/bra band/socks); no verified flesh stiffness numbers; Dyna/SoftSMPL/STAR not verified this session.

## 5. Collider representation and faster baking

### Takeaway
The only verified 2025-2026 SDF items are Lipschitz Pruning (CGF 2025) for primitive-based analytic SDFs and a 2026 SDF interpolation method. For Failoring, the biggest win may be avoiding re-baking at all by using a mesh-based barrier (IPC) against the skinned body.

### Cited Findings
- Barbier, Sanchez, Paris, Michel, Lambert, Boubekeur, Paulin, Thonat, "Lipschitz Pruning: Hierarchical Simplification of Primitive-Based SDFs", Computer Graphics Forum 2025, DOI 10.1111/cgf.70057: computes local pruned trees using the Lipschitz property, supports hard and smooth CSG operators, real-time sphere tracing with thousands of primitives and dynamic animation on GPU; the authors note it could speed up SDF discretisation. Code availability not stated on the page. — [EG DL](https://diglib.eg.org/handle/10.1111/cgf70057)
- Chen, Mupparaju, Batty, Sellan, Stein, "Greed for the Spheres: A Signed Distance Interpolation Method", arXiv 2605.01919 (2026-05-03): consistent SDF interpolation from discrete samples via hard geometric constraints and a greedy algorithm, also repairs pseudo-SDFs (e.g. from CSG). The page does not mention collision; relevance is my inference. — [arXiv](https://arxiv.org/abs/2605.01919)

### Inferences
- IDEA E1 "Rigid body: bake once, transform the query": for a rigid mannequin or a rigidly posed limb, never re-bake; keep the rest-pose SDF and transform query points by the inverse rigid transform. For per-bone pieces use one SDF per bone region with a blend (min or smooth-min) at the joints. Payoff: high for the 185-390 ms walking cost; difficulty: days; risk: skinning blend in joint regions makes distances inexact (Lipschitz bound > 1 near joints); mitigate with a safety margin.
- IDEA E2 "Narrow-band, incremental re-bake": only re-evaluate the grid cells within about 3 cloth-thicknesses of the garment (cloth-adjacent bricks), lazily, with a cache. Payoff: medium-high; difficulty: days.
- IDEA E3 "Analytic-capsule proxy for far field, mesh SDF for near": use Lipschitz-pruning-style culling of the bone capsules to get a conservative lower-bound distance in the far field; refine near the surface only. Payoff: medium; difficulty: weeks; risk: for a mannequin with a mesh-defined detail the analytic proxy is approximate.
- IDEA E4 "Mesh-based body barrier (IPC) instead of SDF": point-triangle and edge-edge between cloth and the skinned body mesh with the barrier from A1. No bake at all; guarantees no penetration including edge-edge. Cost: more contact pairs, and the body is a kinematic obstacle with a prescribed per-frame displacement (CCD uses linear trajectories). Payoff: high; difficulty: weeks (given A1); risk: surface roughness vs 7 mm SDF smoothness, body mesh resolution must be fine enough, may need a smoothed offset surface for friction stability.

### Gaps
- Neural SDFs and incremental SDF-update papers were not searched; nothing verified.

## 6. Why a wedged garment jitters, and remedies

### Takeaway
Plausible causes (my inference, not directly sourced): projection plus a projected line search plus after-the-fact friction creates an inconsistent active set; the hard constraint switches on and off between iterations. Remedies in the literature are a smooth barrier with adaptive stiffness, augmented Lagrangian with persistent multipliers, or an active-set method with TOI locking avoided.

### Cited Findings
- Barrier-free paper: log barriers are ill-conditioned and TOI locking limits progress; their fix is an augmented Lagrangian with adaptive multipliers, adding all required contact pairs immediately and an active-set filter. — [arXiv 2512.12151](https://arxiv.org/abs/2512.12151)
- Cubic barrier with elasticity-inclusive dynamic stiffness is intended to prevent penetration by construction. — [ppf-contact-solver](https://github.com/st-tech/ppf-contact-solver)
- ipc-toolkit ships an adaptive barrier stiffness algorithm. — [ipc-toolkit](https://github.com/ipc-sim/ipc-toolkit)

### Inferences
- IDEA F1 "Persistent multipliers across Newton iterations/steps with a small contact compliance": cheap variant of A2 on the existing body projection: replace hard projection by an augmented Lagrangian term where lambda is updated after each converged Newton iteration; the lambda persistence removes on/off chatter. Payoff: high for jitter; difficulty: days; risk: slower convergence of lambda for tight wedges, combine with D1 (a compliant layer is the same math with a finite stiffness).
- IDEA F2 "Convergence criterion on the contact residual, not the energy": for a quasi-static solve, iterate to a tight gradient norm including contact forces and use a damped-static (artificial viscosity) continuation instead of real time stepping. Payoff: medium; difficulty: days; risk: none known.

### Gaps
- Didn't find a source that diagnoses jitter of wedged cloth in a projection-collider; the explanation above is an inference.

## 7. Open-source availability and licences (verified this session)

### Takeaway
All verified relevant repos are permissive (MIT or Apache-2.0), so reading and porting is compatible with AGPL-3.0; most solvers are GPU/CUDA.

### Cited Findings
- ipc-toolkit: MIT, CPU, CCD/barrier/friction. — [GitHub](https://github.com/ipc-sim/ipc-toolkit)
- Codim-IPC: Apache-2.0, C++ with Python bindings, depends on Boost/Eigen/GMP. — [GitHub](https://github.com/ipc-sim/Codim-IPC)
- ppf-contact-solver (Ando cubic barrier): Apache-2.0, CUDA 12.8, single precision. — [GitHub](https://github.com/st-tech/ppf-contact-solver)
- libuipc: Apache-2.0, CUDA 12.8, GPU. — [GitHub](https://github.com/spiriMirror/libuipc)
- Barrier-free elastodynamics: code at project page, licence not checked. — [project](https://simulation-intelligence.github.io/barrier-free)
- Newton physics engine (NVIDIA Warp based) exists, with cloth examples in Isaac Lab; I did not open licence text. — [NVIDIA blog](https://developer.nvidia.com/blog/train-a-quadruped-locomotion-policy-and-simulate-cloth-manipulation-with-nvidia-isaac-lab-and-newton)

### Inferences
- Apache-2.0 and MIT code can be incorporated into an AGPL-3.0 project with attribution (Apache-2.0 is one-way compatible with GPLv3/AGPLv3). The author should still check each file's header.

### Gaps
- Not checked: PolyFEM licence, GPU-IPC (Huang) repo licence, Gaia, Taichi samples, Newton/Warp licence text, Mil2 code.

## Suggested ordering for Failoring (inference)
1. D1 compliant skin layer plus F1 persistent multipliers (days; fixes plus-size settling and wedged jitter).
2. E1/E2 stop re-baking the SDF per frame (days).
3. C1 friction into the Newton solve (weeks).
4. A1 edge-edge + point-triangle barrier with CCD, then B1 per-layer thickness (weeks), reading ipc-toolkit and the Kaufman thickness paper.
5. E4 mesh-based body barrier, A2/A3 as research upgrades once A1 works.
