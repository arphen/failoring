# Cloth solver, adaptive resolution and quasi-static/equilibrium methods: ideas for Failoring (research as of 2026-10-13)

Method note: ~35 tool calls, mostly arXiv abstract pages, project pages and GitHub. Search engine coverage of 2025-26 cloth papers was poor; arXiv API was blocked by robots. Only the PIE paper was read in full text (via fetch summariser). Everything else is abstract/project-page level. Items from my own background knowledge are labelled "unverified (memory)" and kept in Inferences only.

## 1. Zhang et al. 2025 (PIE): what it is, what is missing in Failoring, follow-ups

### Takeaway
Actual title: "Physics-inspired Estimation of Optimal Cloth Mesh Resolution" (SIGGRAPH 2025). It is a static, precomputed sizing-map method: wavelength scaling law gives source resolution, a transition-distance law gives a power-law falloff, then Bridson Poisson-disk sampling plus Shewchuk Triangle (constrained Delaunay) builds the mesh. No dynamic remeshing. Failoring already has the first step (B/E estimator); the missing parts are all well specified and cheap to build. I found no public follow-up from Wang's group that extends it, and no public code.

### Cited Findings
- Title, authors (Diyang Zhang, Zhendong Wang, Zegao Liu, Xinming Pei, Weiwei Xu, Huamin Wang; mostly Style3D Research), SIGGRAPH Conference Papers '25, DOI 10.1145/3721238.3730619 — [PDF](https://wanghmin.github.io/publication/zhang-2025-pie/Zhang-2025-PIE.pdf), [project page](https://wanghmin.github.io/publication/zhang-2025-pie/)
- Wavelength from Cerda-Mahadevan scaling lambda ~ (B/K)^(1/4); fabric has no substrate, so in-plane compressive resistance is modelled as a virtual substrate; result is a polynomial c1 r^6 + c2 r^4 = y with y = B/E, c1 = 32.08 m^-3, c2 = -1.876e-4 m^-1, fitted for E in [2e3, 2e6] N/m^2, B in [1e-9, 1e-3] N m, r in [2.5, 50] mm; recommended minimum resolution 2.5 mm — [PDF](https://wanghmin.github.io/publication/zhang-2025-pie/Zhang-2025-PIE.pdf)
- Sizing field: each source r0 gives r(x) = r0 + x^m at distance x; final size = min over all sources and the base fabric resolution r_f — same PDF
- Sources per garment technique: shirring (effective stiffness B/(E+E_sh), shrink factor), folding (guide lines ~0.5 mm from fold, strain from fold angle/360), stitching (seam length ratio as compressive strain, transition only on the longer-seam side), down-filling, and manually labelled dynamic collision regions (e.g. elbows) — same PDF
- Transition: length over which wavelength doubles scales as lambda^2 sqrt(T/B) under tension, lambda^(3/2) under compression (after Vandeparre et al. wrinklons); power m = log r / log L_w — same PDF
- Anisotropy: separate weft and warp resolutions for orthotropic fabric (summary only; how the anisotropic sizing is realised in the mesh generator is not clear from the text I got — Triangle is isotropic, so I suspect axis-scaled sampling space, unverified)
- Meshing: Bridson fast Poisson-disk sampling, then Shewchuk Triangle for constrained Delaunay with boundary constraints — same PDF
- No dynamic remeshing; compared against Narain et al. 2012 adaptive anisotropic remeshing; authors argue remeshing causes discontinuities, instability and GPU-unfriendliness, and say their meshes could seed remeshing — same PDF
- Their solver: GPU Newton, 1/30 s step, PCG with 3x3 block-Jacobi, Baraff-Witkin stretch, stable discrete bending (Wang, Yang, Wang 2023), adaptive collision stepping — same PDF
- Validation: stretch test 3.8 mm mesh gives 6 wrinkles vs 7 at 2.5 mm, 5 mm gives 4; hanging curtain predicted transition 26.7 mm vs 25 mm measured; cantilever optimum 6.45 mm (stable around 6.5 mm); 2-10x numerical speedup and 2-4x collision speedup vs uniform 2.5 mm — same PDF
- Limitations stated: static maps cannot adapt to unexpected deformation; collision regions are hand-labelled; no viscoelasticity; future work includes learned resolution prediction and hybrid precompute plus refinement — same PDF
- No code or repository on the project page (only PDF, bib, videos) — [project page](https://wanghmin.github.io/publication/zhang-2025-pie/)
- Related Wang-group items found: "Efficient GPU Cloth Simulation with Non-distance Barriers and Subspace Reuse" (Lan, Lu, Long, Yuan, Li, He, Wang, Jiang, Yang; arXiv 2403.19272, 2024; code not mentioned) — [arXiv](https://arxiv.org/abs/2403.19272); "TailorCoPilot" (Sun et al., ..., Wang; arXiv 2608.25462, Aug 2026; pattern-making agent, not a solver paper) — [arXiv](https://arxiv.org/abs/2608.25462)

### Inferences
- Missing parts for Failoring, in order: (a) per-vertex sizing function h(x) = min(r_f, min_i (r_i + d_i(x)^m_i)); d_i is geodesic or Euclidean distance in 2D pattern space to a source feature (seam, fold line, shirring region, hand-marked contact zone), which on a 2D panel is a cheap distance transform or brute force over a few hundred boundary samples; (b) Bridson Poisson-disk with variable radius (about 100 lines; sample in a grid sized for r_min); (c) constrained Delaunay with panel boundary and seams as constraints. Failoring's mesher already takes an edge length per panel and "one edge length" is the only thing to generalise to a size field. A dependency-free constrained Delaunay with Ruppert/Chew refinement is the biggest piece (days to a couple of weeks); alternatively keep the current mesher, insert the Poisson points as Steiner points.
- Seams are the one real design issue: stitched vertices on two panels must have compatible resolutions across the seam, or seam constraints need an interpolation. PIE's rule (transition only on the longer-seam side) hints that the finer side of a seam drives the size and the other panel follows at the same edge length along the seam; check how Failoring welds seams.
- The 2.5 mm floor and the fit ranges are for their simulator and materials; Failoring should re-fit c1, c2 against its own settled-drape convergence test (cantilever and hanging-curtain tests already exist in its test suite) before trusting absolute numbers.
- For a static-drape product the "dynamic collision region" source is unnecessary or can be replaced by an iterate-and-refine pass (see section 2).

### Gaps
- Could not verify how anisotropic Poisson sampling is done; full text of the paper's Section on mesh generation should be read directly.
- Could not find any 2025-2026 paper citing PIE (search engine index too thin). No public implementation found.

## 2. Adaptive remeshing for cloth and woven anisotropy

### Takeaway
The ArcSim lineage (Narain et al. 2012) remains the reference for dynamic anisotropic remeshing; the newest relevant paper is "Variational r-Adaptive Cloth Simulation" (Aug 2026), which moves vertices rather than changing connectivity and explicitly targets statics. For a settled-drape product, a cheap "solve coarse, estimate, refine, re-solve" loop with static PIE-style sizing is the most practical route; connectivity-changing remeshing under Newton is high risk.

### Cited Findings
- ARCSim: Adaptive Refining and Coarsening Simulator, Narain, Samii, O'Brien, "Adaptive anisotropic remeshing for cloth simulation", SIGGRAPH Asia / ACM TOG 2012; sizing from curvature, velocity gradient, compression and obstacle proximity — [ARCSim page](https://graphics.berkeley.edu/resources/ARCSim/index.html), [history archive](https://history.siggraph.org/?p=207980)
- "Variational r-Adaptive Cloth Simulation", Wen, Chen, Barbic, Kaufman, arXiv 2608.17833 (Aug 2026, preprint): first r-adaptive method for cloth dynamics AND statics with frictional contact; fixes poor local minima and element collapse of variational r-adaptivity on piecewise-linear shells through a degeneracy-activated quality regulariser; trust-region solves with dynamic IPC tolerances; 3-6x faster than prior ITR; better fidelity than fixed mesh at equal vertex count and time; no code mentioned — [arXiv](https://arxiv.org/abs/2608.17833)
- PIE argues static precomputed sizing avoids remeshing artefacts, and that its meshes can seed remeshing — [PDF](https://wanghmin.github.io/publication/zhang-2025-pie/Zhang-2025-PIE.pdf)
- Learned alternatives: Pb4U-GNet (Liu et al., AAAI 2026, arXiv 2601.15110), a GNN trained at low resolution that generalises to finer meshes by adapting propagation depth; code not stated — [arXiv](https://arxiv.org/abs/2601.15110). "Neural Garment Dynamic Super-Resolution" (Zhang, Li, arXiv 2412.06285, Dec 2024) adds wrinkle residuals on coarse sims; code not stated — [arXiv](https://arxiv.org/abs/2412.06285)

### Inferences
- Wrinkle-aware sizing without remeshing under Newton: run a coarse settle, compute per-triangle indicator (dihedral angle jump, compression strain, curvature, bending-energy density), turn it into a size field, mesh anew (PIE-style Poisson + Delaunay), transfer the coarse solution by barycentric interpolation as warm start, resolve. Because only the settled state matters this is a few outer iterations, not per-frame remeshing, and the Cholesky symbolic factorisation is redone once per outer iteration (cost small versus the numeric solve). Risk: the coarse solution may sit in a different buckling mode than the fine one.
- r-Adaptive (vertex relocation, fixed connectivity) is attractive for Failoring because the symbolic ordering/supernodes can be reused, but it makes the energy non-convex in the positions of rest-state nodes and needs the quality regulariser; the paper is a preprint with no code. Treat as research reading, not a first implementation.
- Woven-fabric anisotropy: sizing in PIE is warp/weft separate; a cheap approximate implementation is to mesh in a stretched coordinate system (scale axes by the ratio of wavelengths, mesh isotropically, map back), unverified as to PIE's own method. Note that grain-aligned structured meshes avoid shear-locking differences; not researched.

### Gaps
- No paper found specifically on anisotropic meshes for woven fabric in 2023-2026 beyond PIE and ARCSim; coverage of SCA/CGF 2024-26 papers by the search tool was poor.
- Did not find learned/GPU remeshing papers for cloth with verifiable details.

## 3. Quasi-static and equilibrium solvers; getting the right drape

### Takeaway
The direct way to get a settled drape with Failoring's existing machinery is to minimise the gravitational + elastic energy directly (no inertia), using the same Newton + line search but with a pseudo-time (inertia/damping) regulariser or gravity continuation, instead of simulating 60 Hz frames. The literature gives well-known tools for this (Newton variants, trust regions, Project-on-Demand) but I found no cloth-specific 2023-26 paper on "unique drape"; path dependence and buckling bifurcation are an unavoidable property, and the practical defence is a physically motivated path (gravity ramp or damped dynamics) rather than a mathematical guarantee.

### Cited Findings
- "Pitfalls of Projection: A Study of Newton-Type Solvers for Incremental Potentials" (Longva, Loschner, Fernandez-Fernandez, Larionov, Ascher, Bender; arXiv 2311.14526, Nov 2023): projected Newton usually converges slower than classical Newton; proposes Project-on-Demand Newton (project only when needed, keeps robustness and Newton rate), Kinetic Newton, an acceleration-based convergence criterion instead of residual norm, and a parameter-free line search because Armijo backtracking can block convergence on stiff problems — [arXiv](https://arxiv.org/abs/2311.14526)
- Optimization integrators (Gast, Schroeder, Stomakhin, Jiang, Teran, "Optimization Integrator for Large Time Steps", IEEE TVCG 2015) cast implicit Euler as minimisation with line search — [PDF](https://www.math.ucdavis.edu/~jteran/papers/GSSJT15.pdf) (already Failoring's formulation)
- Variational r-Adaptive paper explicitly treats cloth statics with frictional contact using trust-region solves and notes solver trapping in poor local minima on adaptive meshes — [arXiv](https://arxiv.org/abs/2608.17833)
- PhySkin (Srivastava et al., arXiv 2603.27013, Mar 2026 preprint) is itself a "reduced-space quasi-static simulation" of garment DOFs driven by bone handles, trained self-supervised on physical energies (gravity, stretch, bend, collision) — [arXiv](https://arxiv.org/abs/2603.27013)
- Classic cloth-drape literature exists (e.g. "Stop-and-go cloth draping", Visual Computer 2007) — [record](https://doc.rero.ch/record/320699) — but I only saw titles.

### Inferences
- Idea: static solve = minimise E_elastic(x) + E_gravity(x) + (mu/2h^2)|x - x_prev|_M^2 (a proximal/pseudo-transient term, equivalent to implicit Euler with huge h and no velocity). Large mu early gives well-conditioned, globally convex-ish steps; decrease mu geometrically toward zero (pseudo-transient continuation). Failoring's current step is already this with mu = M/h^2, so the change is mainly: drop the velocity term, adapt mu by Levenberg-Marquardt-style rule from line-search success, and stop on gradient norm or the acceleration criterion of Longva et al. instead of per-frame time.
- Gravity ramp (g scaled 0..1 over 10-30 stages, warm-started each stage) follows the physical loading path, so it picks the branch a real fabric would take when draped slowly. Add a small damped-dynamics kick or random perturbation at the end to test for stability of the final state (compare energy after perturbation; a saddle will drop).
- Uniqueness: thin shells under compression have multiple equilibria (buckling modes of the wrinkles); there is no unique drape, only a stable one reachable from a loading path. Practical check: run the same garment from 2-3 perturbed initial states and compare energy; report variance as a confidence cue. Unverified (memory) that arc-length/path-following is overkill for this; I searched for graphics treatments and found only engineering FEA slides.
- Warm start after edits: previous solution with the edited vertices' rest positions changed, then a short gravity-and-stiffness continuation from 90% of the strain; reuse the symbolic factorisation when connectivity is unchanged (only values change). Local pattern edits that change connectivity break this; fall back to interpolation.
- Contact-aware equilibrium: barrier-based (IPC) contact fits Newton + line search with filtered step length; ipc-toolkit is MIT licensed C++ (AGPL-compatible).

### Gaps
- No verified 2023-26 cloth paper on homotopy/continuation or L-BFGS for statics specifically; no verified source for dynamic relaxation or pseudo-transient continuation in cloth graphics.
- No source found about uniqueness/bifurcation for cloth in graphics.

## 4. Faster linear algebra and alternative solvers

### Takeaway
For a precision-first, non-real-time setting, Failoring's direct Newton remains the strongest baseline; VBD/AVBD, projective dynamics and GPBD are real-time/GPU approaches that trade accuracy for speed, and the most relevant new idea is exact global factorisation inside a domain-decomposed scheme (Lu et al. 2025) - which is what Failoring already does. The practical wins are cheaper per-iteration factorisation (reuse across Newton iterations and across ramp stages) and mixed precision, not a new solver family.

### Cited Findings
- Vertex Block Descent (Chen, Liu, Yang, Yuksel; SIGGRAPH 2024 / ACM TOG 43(4); arXiv 2403.06321): block coordinate descent on implicit Euler's variational form, vertex-level Gauss-Seidel, claims unconditional stability and numerical convergence; reference implementation inside Anka He Chen's "Gaia" engine, licence not stated on the page — [arXiv](https://arxiv.org/abs/2403.06321), [project](https://graphics.cs.utah.edu/research/projects/vbd/)
- Augmented VBD (Giles, Diaz, Yuksel; ACM TOG 44(4), SIGGRAPH 2025, DOI 10.1145/3731195): augmented Lagrangian for hard constraints and high stiffness ratios; targets rigid/articulated contact; demos at github.com/savant117/avbd-demo2d and avbd-demo3d (licence not stated on the page); the page does not mention cloth — [project](https://graphics.cs.utah.edu/research/projects/avbd/)
- "High-performance CPU Cloth Simulation Using Domain-decomposed Projective Dynamics" (Lu, Liu, Lan, Wang, Ishiwaka, Jiang, Wu, Yang; ACM TOG 44(4), Art. 51, Aug 2025): partitions garment into domains with a primal-dual global solve; forward/backward substitutions in parallel, small corner-DOF and dual systems sequential; exact pre-factorised global solve (CHOLMOD/MKL); comparable to GPU methods, >=10x faster than other CPU methods; limited to PD-compatible materials; collisions are the stated CPU limitation — [PDF](https://www-old.cs.utah.edu/docs/misc/YYang/Paper_3_SIGGRAPH25_CGP-Cloth.pdf)
- "Efficient GPU Cloth Simulation with Non-distance Barriers and Subspace Reuse" (arXiv 2403.19272, 2024): subspace reuse assuming low-frequency strain is nearly orthogonal to high-frequency collision deformation — [arXiv](https://arxiv.org/abs/2403.19272)
- "Towards Generalized Position-Based Dynamics" (Chaudhary, Pokhariya, Narain; arXiv 2511.23131, venue not stated): GPBD generalises PBD to nonlinear energy-based forces with per-element low-dimensional Newton solves; reported better than a Newton baseline at high resolution; code not mentioned — [arXiv](https://arxiv.org/abs/2511.23131)
- "Fast But Accurate: A Real-Time Hyperelastic Simulator with Robust Frictional Contact" (Zeng et al., arXiv 2503.15078, Mar 2025): local-global with complementarity conditions, GPU-friendly — [arXiv](https://arxiv.org/abs/2503.15078)
- "Second-order Stencil Descent for Interior-point Hyperelasticity" (Lan, Li, Jiang, Wang, Yang; SIGGRAPH 2023) exists as a title only — [history archive](https://history.siggraph.org/?p=132466)
- "Geometric Contact Potential" (Huang, Paik, Ferguson, Panozzo, Zorin; arXiv 2402.00719, TOG 2025 DOI 10.1145/3731142) a discretisation-independent drop-in for IPC barriers, reducing resolution dependence — [arXiv](https://arxiv.org/abs/2402.00719)
- Style3D's own cloth solver uses PCG with 3x3 block-Jacobi preconditioning on GPU (from PIE paper) — [PDF](https://wanghmin.github.io/publication/zhang-2025-pie/Zhang-2025-PIE.pdf)

### Inferences
- VBD and AVBD: iterative, convergence slows on stiff, high-aspect meshes (stated limitation is not on the pages; this is memory from the VBD paper, unverified). Not better than direct Newton for settled, precision-first solves; might help as a cheap pre-smoother to get the initial guess for Newton on first load. Low payoff.
- Cholesky reuse tricks (unverified/background knowledge, no 2023-26 source found): (1) across Newton iterations within a stage, Gauss-Newton Hessian changes mostly by the nonlinear rotation of stretch directions; refactor every k iterations and use the stale factor only as a fallback (author already rejected chord Newton and stale-PCG); (2) for contact, add contact stiffness as a low-rank block and use Woodbury / Schur complement over the contact DOFs instead of refactoring when the contact set is small; (3) for small pattern edits, supernodal numeric refactorisation limited to the elimination subtree that touches the changed rows (partial refactor via the elimination tree). All three are standard sparse-direct technology; payoff depends on measurement, mostly in the edit loop.
- Mixed precision: factorise in float32 and run 1-2 steps of iterative refinement or PCG in float64 (preconditioner role) - classic; ~2x on bandwidth/SIMD for a compute-bound code; keep a double fallback for ill-conditioned stiff-shear cases. Unverified for cloth in 2023-26 sources.
- Domain-decomposition (Lu et al. 2025): Failoring panels are natural subdomains (seams = interface); a Schur complement on seam DOFs lets each panel factor independently and in parallel, and re-factor only the edited panel. This is the most structurally natural speedup for a tailoring loop and ties into warm starts after edits. Cost: seam interface can be large; PD-specific tricks in the paper do not transfer, but the primal-dual machinery does.
- Neural/learned preconditioners: no verified cloth-specific 2023-26 source found.

### Gaps
- Could not verify any 2025-26 "Second-order Projective Dynamics", L-BFGS-PD or BDDC cloth paper; searches returned nothing relevant.
- VBD/AVBD reference licences unconfirmed; AGPL compatibility unknown.

## 5. Reduced and learned models as preview

### Takeaway
Neural cloth models (HOOD, ContourCraft) are open and MIT-licensed but are designed for garments on a posed body, rely on a trained mesh-graph prior and (HOOD) SMPL, and are unlikely to match Failoring's anisotropic-fabric, user-edited-pattern settled drape. PhySkin is the one closest to the user's idea (reduced-space quasi-static), but it is body-bone-handle-driven, a March 2026 preprint with no code.

### Cited Findings
- PhySkin: Physics-based Bone-driven Neural Garment Simulation (Srivastava, Chen, Goldade, Herholz, Jiang, Lin, Yang, Sarafianos, Stuyck, Larionov; arXiv 2603.27013, 27 Mar 2026): garment DOFs mapped to bone handles, self-supervised training on physical energies, lightweight net modulates handles by body shape and pose, microsecond-per-frame CPU inference, zero-shot and mesh-topology independence; no venue named, no code link on page — [arXiv](https://arxiv.org/abs/2603.27013)
- HOOD (Grigorev, Black, Hilliges, CVPR 2023, arXiv 2212.07242): hierarchical graph network, self-supervised physics loss; official code MIT licence, needs PyTorch 1.13-era stack, PyG, PyTorch3D, SMPL models (separate licence) — [paper](https://arxiv.org/abs/2212.07242), [repo](https://github.com/Dolorousrtur/HOOD)
- ContourCraft (SIGGRAPH 2024; learns to resolve intersections in multi-garment sims): official code MIT, built on HOOD, includes garment-import notebook and fine-tuning — [repo](https://github.com/Dolorousrtur/ContourCraft), [project](https://is.mpg.de/ps/en/projects/learning-to-resolve-intersections-in-neural-multi-garment-simulations)
- Pb4U-GNet (AAAI 2026) and Neural Garment Dynamic Super-Resolution (2024) as above.

### Inferences
- Unusable for arbitrary user-edited patterns with orthotropic materials: training distributions are body-draped T-shirts/dresses with isotropic-ish materials; HOOD takes material parameters but I did not verify anisotropic fabric support. A preview for the tailoring loop would instead be Failoring's own coarse-mesh static solve (e.g. 4x coarser by PIE rule, then upsample), which is faithful by construction and far cheaper than ML integration.
- Subspace idea for edits (PhySkin spirit, without ML): choose handle DOFs (vertices along seams/fixed points), build a reduced basis from linearised modes at the previous equilibrium, solve the reduced Newton step to predict the new drape after a small edit, then polish with full Newton. Risk: buckling changes break the basis.
- Mixed value: use PhySkin only for avatar-walking offline renders if the author later wants quick animation; not for the tailoring loop.

### Gaps
- Did not verify HOOD licence compatibility of dependencies (PyTorch3D, SMPL licence are not AGPL-compatible for bundling); not relevant if used only offline.
- No code for PhySkin found; GAT/NCS/ClothFormer not checked.

## 6. Differentiable and inverse approaches (brief)

### Takeaway
Papers exist for pattern/material inversion from a target shape, but none provides public code in the pages I checked; most differentiate through dynamics rather than equilibrium. With a static solver the clean approach is the implicit function theorem on the equilibrium (one extra solve with the already-factored Hessian).

### Cited Findings
- DiffAvatar (Li, Chen, Larionov, Sarafianos, Matusik, Stuyck; arXiv 2311.12194, Nov 2023): joint optimisation of body and garment geometry via differentiable cloth simulation, material estimation; no code, project page at people.csail.mit.edu/liyifei/publication/diffavatar/ — [arXiv](https://arxiv.org/abs/2311.12194)
- Dress Anyone (Chen, Larionov, Kavan, Lin, Roble, Sorkine-Hornung, Stuyck; arXiv 2405.19148, May 2024): refit 3D garment and 2D pattern to new bodies with differentiable simulation; no code mentioned — [arXiv](https://arxiv.org/abs/2405.19148)
- Inverse Garment and Pattern Modeling with a Differentiable Simulator (Yu, Cordier, Seo; arXiv 2403.06841, 2024): adjusts 2D template pattern and material parameters to match a target shape; symmetry enforced; no code mentioned — [arXiv](https://arxiv.org/abs/2403.06841)

### Inferences
- With a Newton equilibrium solver, dE/dp (p = pattern/material parameters) follows from the implicit function theorem: dx*/dp = -H^-1 d²E/(dx dp), so each gradient costs one back-substitution with the existing factor. This enables "adjust pattern to hit target silhouette" cheaply; payoff for a fit-adjustment feature, difficulty weeks (needs derivative of rest shape with respect to pattern parameters). Unverified in the cited papers; standard technique.

### Gaps
- DiffCloth, ARCSim-diff, Liang et al. not fetched.

## 7. Open code and licences (project is AGPL-3.0)

### Takeaway
Permissive options: ipc-toolkit (MIT), NVIDIA Warp (Apache-2.0, with a NVIDIA-licence caveat for libmathdx), Newton (Apache-2.0), HOOD and ContourCraft (MIT). VBD/AVBD licences were not stated on the pages; PIE and the r-adaptive paper have no code.

### Cited Findings
- ipc-toolkit: MIT, C++ with pybind11 Python bindings — [repo](https://github.com/ipc-sim/ipc-toolkit)
- NVIDIA Warp: Apache-2.0; libmathdx downloaded at build/in PyPI wheels under NVIDIA Software License Agreement — [repo](https://github.com/NVIDIA/warp)
- Newton physics engine (Linux Foundation; initiated by Disney Research, Google DeepMind, NVIDIA): Apache-2.0 code, CC-BY-4.0 docs, built on Warp; cloth examples include a Style3D cloth example and VBD examples (solver details not confirmed on the page) — [repo](https://github.com/newton-physics/newton)
- HOOD MIT — [repo](https://github.com/Dolorousrtur/HOOD); ContourCraft MIT — [repo](https://github.com/Dolorousrtur/ContourCraft)
- AVBD demos: savant117/avbd-demo2d and avbd-demo3d; VBD in Gaia; licences not stated on project pages — [AVBD](https://graphics.cs.utah.edu/research/projects/avbd/), [VBD](https://graphics.cs.utah.edu/research/projects/vbd/)
- Other pointers seen in search (not fetched): Houdini VBD implementation — [repo](https://github.com/MysteryPancake/Houdini-VBD); Isaac Lab VBD tuning doc — [doc](https://isaac-sim.github.io/IsaacLab/develop/source/how-to/solver_tuning_vbd.html)
- Lipschitz Pruning (Barbier, Sanchez, Paris, Michel, Lambert, Boubekeur, Paulin, Thonat; Computer Graphics Forum 2025, DOI 10.1111/cgf.70057): hierarchical pruning of primitive-based SDF trees for fast sphere tracing, mentions SDF discretisation/polygonisation as a possible use; code not mentioned on the page — [EG DL](https://diglib.eg.org/handle/10.1111/cgf70057)

### Inferences
- Permissive licences (MIT, Apache-2.0) can be incorporated into an AGPL-3.0 project; Apache-2.0 is compatible with GPLv3/AGPLv3 (memory, standard). Do not bundle SMPL or the libmathdx binaries.
- Lipschitz pruning for collider baking: applies if the colliders are analytic primitive-based SDF trees (e.g. mannequin from blended capsules/ellipsoids): local pruned subtrees accelerate repeated evaluation on a grid (octree bake). Not applicable to mesh colliders. Payoff only matters if baking time is a pain point.

### Gaps
- Licences for Gaia/VBD, avbd-demo*: not confirmed.

## 8. Implementable ideas, ranked (the deliverable)

### Takeaway
Best value for a settled-drape-only product: (1) static pseudo-transient/gravity-ramp solve, (2) PIE sizing maps with Poisson-disk meshing, (3) solve-estimate-refine loop, (4) panel-wise Schur/domain decomposition with partial refactorisation, (5) mixed precision. Skip learned models and VBD-type solvers.

### Inferences (ideas; no new facts here, sources in sections above)

1. Static equilibrium via pseudo-transient continuation plus gravity ramp
 - Outline: replace the time-stepping loop with a sequence of stages: g scaled 0.05..1; each stage solves min E_el + E_g(s) + (mu/2)|x - x_prev|_M^2 by existing Newton and line search; mu is cut by a factor after each successful stage and raised when the line search fails; stop when the acceleration-style criterion (|grad|/M-scaled, Longva et al.) is below tolerance and mu is small. Warm start from the previous stage's x.
 - Changes: new top-level driver around the existing Newton loop; no velocity state; removes the 60 Hz frame framing. Add a perturb-and-compare stability probe at the end.
 - Payoff: high. Difficulty: days. Risks: a long ramp costs many Newton solves (each with a Cholesky, ~0.37 s at 1500 verts), so wall time may exceed a few dynamic seconds; ramp stage count needs tuning; final state is a stable equilibrium but not unique. Sources: [Longva et al.](https://arxiv.org/abs/2311.14526), [Gast et al.](https://www.math.ucdavis.edu/~jteran/papers/GSSJT15.pdf).

2. PIE sizing map and Poisson-disk mesher
 - Outline: implement h(x) = min(r_f, r_i + d_i(x)^m_i) per source (seams under compression, fold lines, shirring regions, user-marked zones), fit c1, c2 from Failoring's own cantilever/curtain tests, Bridson variable-radius Poisson-disk plus (constrained) Delaunay with panel boundary and seam constraints; anisotropy via axis-scaled space.
 - Changes: mesher takes a size function, not an edge length; seam vertices matched across panels.
 - Payoff: high on vertex count at equal drape quality (paper reports 2-10x solve speedup versus uniform finest, on their GPU simulator). Difficulty: weeks (Delaunay robustness and seam compatibility are the long pole). Risks: absolute constants may not transfer; interplay with the wrinkle onset under static-only loading is not validated. Sources: [PIE PDF](https://wanghmin.github.io/publication/zhang-2025-pie/Zhang-2025-PIE.pdf).

3. Solve-estimate-refine loop (static adaptive refinement)
 - Outline: coarse static solve; compute per-triangle indicator from bending energy density and compression strain; derive size field (min with PIE prior); remesh; barycentric transfer; re-solve; stop when the drape metric (e.g. Hausdorff distance between consecutive levels) is below tolerance. Doubles as automatic mesh-convergence check.
 - Changes: mesh transfer routine, indicator code, outer loop; symbolic factorisation redone per level.
 - Payoff: medium-high. Difficulty: weeks (after idea 2). Risks: coarse mesh locks onto a different buckling mode than the fine mesh; coarse level may under-resolve and hide wrinkles that then appear only after refinement (indicator blind). Sources: [ARCSim](https://graphics.berkeley.edu/resources/ARCSim/index.html), [Wen et al. 2026](https://arxiv.org/abs/2608.17833), [PIE](https://wanghmin.github.io/publication/zhang-2025-pie/Zhang-2025-PIE.pdf).

4. Panel-wise domain decomposition with Schur complement on seams and partial refactorisation
 - Outline: treat each panel as a subdomain; eliminate interior DOFs via per-panel Cholesky; assemble and factor the reduced system on seam/pin DOFs. After an edit to one panel only that panel's factor (and the reduced system) is rebuilt. Parallelisable across panels.
 - Changes: assembly layout and factorisation code; nested-dissection ordering is replaced by panel-first ordering (can still use ND inside panels).
 - Payoff: medium for the edit loop (interactive tweaks of one panel), low-medium for first solve. Difficulty: weeks. Risks: large seam interfaces make the reduced system dense-ish; gains vs a good global ND ordering are unclear at only ~1500 DOFs; unverified in cloth context beyond Lu et al.'s domain-decomposed PD. Sources: [Lu et al. 2025](https://www-old.cs.utah.edu/docs/misc/YYang/Paper_3_SIGGRAPH25_CGP-Cloth.pdf).

5. Mixed-precision factorisation with iterative refinement
 - Outline: single-precision supernodal Cholesky (2x SIMD width, half the bandwidth), double-precision residual, 1-3 refinement steps (or float factor as PCG preconditioner for exact Newton systems). Fall back to double if refinement stagnates.
 - Changes: template the factor kernels on scalar type.
 - Payoff: medium (up to ~2x per solve if compute-bound and vectorised). Difficulty: days. Risks: stiff-weft/shear conditioning exceeds float range at high stiffness ratios; line search already tolerates inexact steps but the author rejected stale-Cholesky PCG, so check this one with the same test suite. No cloth-specific 2023-26 source found; standard numerical linear algebra.

6. Warm start and incremental re-solve after edits
 - Outline: keep x*, factor structure, and last mu; after an edit apply a rest-shape change continuation (blend old and new rest shape in 3-5 steps, Newton each) or a reduced-handle prediction (idea 7). Reuse the symbolic factorisation when connectivity is unchanged.
 - Payoff: high for UX, difficulty: days to weeks. Risks: stays in the old buckling mode after the edit (hysteresis); offer a "re-settle from scratch" button and run the gravity ramp in the background for the authoritative answer.

7. Reduced handle-subspace predictor (PhySkin idea without ML)
 - Outline: build basis from smallest generalised eigenmodes of the Hessian at the current equilibrium plus rigid/seam handles; predict new drape for small edits via reduced Newton; then full Newton polish.
 - Payoff: low-medium. Difficulty: weeks. Risks: bases invalid after buckling changes; eigen-solve costs. Source for the concept: [PhySkin](https://arxiv.org/abs/2603.27013), [Lan et al. subspace reuse](https://arxiv.org/abs/2403.19272).

8. Implicit-function-theorem gradients for pattern fitting
 - Outline: because the settled state is a minimum, dx*/dp = -H^-1 d(grad E)/dp; one back-substitution per parameter or one adjoint solve per loss. Drive pattern edits toward a target silhouette/fit measure.
 - Payoff: medium (new feature), difficulty: weeks to months. Risks: nonsmooth contact and bifurcations; gradients are branch-local. Sources: [Yu et al.](https://arxiv.org/abs/2403.06841), [DiffAvatar](https://arxiv.org/abs/2311.12194), [Dress Anyone](https://arxiv.org/abs/2405.19148).

9. Barrier contact for the equilibrium (IPC) using ipc-toolkit, Geometric Contact Potential as option
 - Outline: use MIT ipc-toolkit for the distance barriers and CCD-filtered line search; GCP is a discretisation-independent potential that reduces resolution dependence (relevant once meshes are adaptively sized).
 - Payoff: medium; difficulty: weeks. Risks: barrier stiffness conditions the Hessian; the existing collider is a baked SDF (not mesh), so ipc-toolkit's primitives do not apply directly. Sources: [ipc-toolkit](https://github.com/ipc-sim/ipc-toolkit), [GCP](https://arxiv.org/abs/2402.00719).

10. Lipschitz-pruned collider baking
 - Outline: only if the mannequin is a primitive-based SDF tree; apply hierarchical pruning when baking to an octree/grid.
 - Payoff: low; difficulty: days to weeks. Source: [Barbier et al. 2025](https://diglib.eg.org/handle/10.1111/cgf70057).

Not recommended: switching to VBD/AVBD or GPBD (iterative, aimed at real time; AVBD not demonstrated on cloth on its page); learned simulators (HOOD/ContourCraft) as a preview (body-draped garments, SMPL dependency); r-adaptive dynamics (preprint, no code, research risk).

### Gaps
- The key claims about cost savings in ideas 4, 5, 6, 7 are my own estimates, not from sources.
- No public source found for any 2025-26 follow-up of PIE; the Wang group's latest cloth-adjacent items found are the Lan/Lu papers above and TailorCoPilot.
