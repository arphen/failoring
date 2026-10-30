# C1: Garment-specific simulation pipeline (arrangement, sewing, gathers, thickness, adaptive remeshing, wrinkle enrichment)

Scope note: about 17 tool calls were used. Several key classics (Volino/Magnenat-Thalmann seaming, Berthouzoz 2013, Muller-Chentanez Wrinkle Meshes, Pfaff 2014, Narain 2013) were NOT fetched or verified, and are listed under Gaps. Effort estimates and "inferences" are mine, for a small team with a C++ triangle-FEM core, and are not from sources.

## Q1. How do CLO / Style3D / Marvelous arrange and sew patterns, and what do they publish or patent?

### Takeaway
CLO's patents describe a conventional pipeline: 2D patterns are meshed, arranged around an avatar (via per-garment "pattern blocks" holding position information), then draped. Auto-sewing by block edges and auto-grading of border lengths are claimed. Style3D documents an "Auto Arrange (Beta)" that chains pieces from a manually frozen reference piece along sewing relationships. I found no public detail on seam strength ramps, seam springs vs constraints, or thickness handling in any of the three products. Those are black boxes.

### Cited Findings
- CLO patent US12371830B2 (Clo Virtual Fashion; inventor In Yong Jeon; priority 2016-09-28 KR 10-2016-0125040; parent US 15/474,091 filed 2017-03-30; granted 2025-07-29). Claims a database of "pattern blocks" matched to 2D patterns, with edges carrying automatic sewing information. Edges can be marked as not sewn (e.g. open bodice hem). Claim 7 auto-grades pattern sizes so joined border lengths match. The description says a block can carry position information for arranging the 2D pattern around the 3D model. The claims do not themselves recite pre-positioning. — [Google Patents US12371830B2](https://patents.google.com/patent/US12371830B2/en)
- The same patent's description states the general pipeline builds on Volino and Magnenat-Thalmann (2006): convert 2D patterns to meshes, arrange them around an avatar, detect collisions, apply collision-removal forces, run draping. Its background says prior methods needed manual seam selection and manual arrangement before draping. — [Google Patents US12371830B2](https://patents.google.com/patent/US12371830B2/en)
- CLO patent US11461967B2 "Method and apparatus for simulating clothes" (inventors Hohyun Lee, Yeji Kim; priority 2019-08-19 KR; filed 2020-09-21; granted 2022-10-04; continuation US12026836B2). Claims a shirring (gathering) method. The user sets a shirring area (two parallel lines). Perpendicular inner lines are generated at regular intervals (example: 30 mm height, 5 mm interval). Mesh polygons are realigned so one side lies on each inner line, then inner lines are removed and draping runs. Reducing mesh size in the shirring area is said to help pleat detail. The background argues that conventional simulation handles shirring poorly because it only models sewing a long fabric to a short line. — [Google Patents US11461967](https://patents.google.com/patent/US11461967)
- US11461967 models cloth as a mass-spring system (vertices are point masses; triangle sides are springs for stretch, shear, bending). It does not detail the solver or integrator. — [Google Patents US11461967](https://patents.google.com/patent/US11461967)
- Other CLO-family patents surfaced but were NOT read: US11410355B2 "creating digital clothing", US12125162B2 "guide for combining pattern pieces", US12376638B2 "duplicating pattern piece in 3D space". — [search result list](https://patents.google.com/patent/US11410355)
- Style3D Studio "Arrange Auto (Beta)": automatically arranges patterns in the 3D window "based on sewing relationships". The user sets the avatar pose between A-pose and T-pose, defines sewing relationships, manually places and freezes the top-most piece as reference, selects all, and uses Move To > Auto Arrange on the frozen piece. Works best on regular, symmetric garments. — [Style3D help](https://help.style3d.com/studio/en/c2c8/5869/0792/ebff/13f8/12d5)
- Style3D blog claims its stitching simulation models seam types (flat-felled, French, lapped), stitch density, seam allowance (mm), thread tension and sewing order, with outputs of stress (kPa) and strain %. Marketing page; no gather, thickness or ramp content, no algorithm. Treat as unverified marketing. — [Style3D blog](https://www.style3d.com/blog/?p=13118)
- Style3D Research (CSO Huamin Wang) had two SIGGRAPH Asia 2023 papers announced: an analytically PSD-projected dihedral-angle bending model for stability, and a GPU cloth method combining subspace integration with Jacobi projective dynamics (reduced-space LBFGS). The press release gives no titles or authors, and nothing on sewing or remeshing. — [TechNode press release](https://technode.global/prnasia/style3d-research-shines-the-show-with-additional-two-papers-accepted-at-siggraph-asia-2023-on-cloth-simulation-innovations/)
- CLO help pages exist for "Reset 3D Arrangement (Selected/All)" and arrangement points (not read in detail). — [CLO support](https://support.clo3d.com/hc/en-us/articles/115012381208-Reset-3D-Arrangement-Selected)
- Marvelous Designer: searched, but only community posts and a 2013 DigitalProduction article surfaced; nothing readable on arrangement/sewing internals. A neural paper states MD particle spacing of 30 mm (low-res) vs 10 mm (high-res) for its training data, which shows MD exposes particle distance as the resolution knob. — [GDSR paper](https://arxiv.org/html/2412.06285v1)

### Inferences
- Pre-positioning in commercial tools is template-driven: per-garment-type "blocks" (cylinder-like enclosures around the body) plus a per-piece position/rotation. This matches the current cylinder/cross-section start. The weak point is gathered or unusual pieces, where the block gives no good start.
- CLO's shirring patent is effectively "mesh edges aligned to pleat lines, then fine triangles there". That is a rest-state trick and not a new solver. A cheap analogue for a small team is to remesh gather zones with edges aligned to the stitch direction.
- The Style3D "freeze a reference piece, propagate through sewing relationships" scheme is a graph traversal over the seam graph and is easy to copy (S).

### Gaps
- Seam constraint vs spring formulation, ramp-in schedule, and thickness in CLO/MD/Style3D: not found in any public doc or patent I read. Browzwear, Optitex and Lectra: not researched in this pass.
- Rest of US11461967 and US12371830 (last ~8k and ~20k characters) unread.

## Q2. Research on sewing: Volino/Magnenat-Thalmann, Berthouzoz, GarmentCode, rest-shape seams, GPU seams

### Takeaway
GarmentCode is the best open reference for how a programmatic pipeline places and stitches panels (6D panel placement relative to an SMPL body, edge-to-edge stitches with vertices inserted to equalise edge counts, gathers as stitches between unequal-length interfaces). Its published paper does not specify the simulator's stiffness, ramp or mesh resolution. Classical seaming papers were not verified in this session.

### Cited Findings
- GarmentCode (Korosteleva and Sorkine-Hornung, ACM TOG / SIGGRAPH Asia 2023, DOI 10.1145/3618351): abstract stitches are flattened into edge-to-edge stitches; vertices are added so edge counts match; each edge belongs to at most one stitch. — [arXiv 2306.03642](https://arxiv.org/html/2306.03642v2)
- GarmentCode gathers are handled by stitching interfaces of unequal length (Regency dress example), with edge lengths expressed as fractions of interface length. The paper says sharp folds have limited support and pleats/smocking need more tools. — [arXiv 2306.03642](https://arxiv.org/html/2306.03642v2)
- GarmentCode initial placement: each panel has a 6D placement (rotation and translation) relative to an SMPL female average body; helper aligns components by interface centres of mass; panel normals flipped so the right side faces outward. — [arXiv 2306.03642](https://arxiv.org/html/2306.03642v2)
- GarmentCode listed limitations relevant to simulation: no internal loops (holes), no multi-layer stitching, no fold lines, and a suggestion to merge fragmented panels to reduce stitch count. — [arXiv 2306.03642](https://arxiv.org/html/2306.03642v2)
- Code: github maria-korosteleva/GarmentCode, MIT licence per the repo sidebar. The repo links a "Running Data Generation (warp)" document and an "old Maya+Qualoth" path; scripts pattern_data_sim.py and test_garment_sim.py are named. Warp as the simulator is my inference from link text. Paper text itself is CC BY-SA 4.0. — [GitHub](https://github.com/maria-korosteleva/GarmentCode)
- GarmentCodeData (3D made-to-measure garments with patterns, dataset paper): located but not read. — [arXiv 2405.17609](https://arxiv.org/pdf/2405.17609)
- Volino and Magnenat-Thalmann (2006) is cited by CLO's patent as the base draping pipeline (arrange, collide, remove collisions by force, drape). — [Google Patents US12371830B2](https://patents.google.com/patent/US12371830B2/en)
- A SIGGRAPH history entry titled "Automatic Digital Garment Initialization From Sewing Patterns" exists. Only the title was seen and it is directly relevant to product failure (4). — [SIGGRAPH history](https://history.siggraph.org/?p=164201)
- Berthouzoz et al., "Parsing Sewing Patterns into 3D Garments" (SIGGRAPH 2013) page surfaced in search but was not read. — [Berkeley VIS mirror](https://backiee.wasmer.app/http_vis_berkeley_edu/papers/clopat)

### Inferences
- Effort for failure (4), for a small team: (a) seam-graph propagation of initial placement (Style3D-style): S. (b) Gather-aware start: place the short-edge panel flat/curved, and start the long edge as a uniformly compressed (rest-length-scaled) seam with edge-aligned fine mesh: M. (c) Seam-length matching by inserting vertices as GarmentCode does: S.
- A "shrinking rest-length" seam ramp (animate the seam rest length from current gap to zero over many frames, with clamped force) is a standard way to avoid energy spikes. Not sourced here; mark as engineering suggestion, not a located method.

### Gaps
- Could not verify: Volino/Magnenat-Thalmann seam formulation, Berthouzoz 2013 details, Wang et al. sewing pattern adjustment (a history entry "Rule-free sewing pattern adjustment with precision and efficiency" exists but was not read), Sensitive Couture, GPU seam constraints, GarmentCode's actual stiffness/ramp.

## Q3. Gathering / pleating / shirring, thickness, seam allowance, interfacing

### Takeaway
The only sourced gathering method is CLO's shirring patent (edge-aligned meshing in the gather zone). Thickness, seam allowance and interfacing modelling were not located in any technical source.

### Cited Findings
- CLO shirring patent: see Q1 (US11461967B2). — [Google Patents](https://patents.google.com/patent/US11461967)
- GarmentCode: gathers via unequal-length stitches; no support for pleats and smocking. — [arXiv 2306.03642](https://arxiv.org/html/2306.03642v2)
- Style3D blog lists seam allowance (mm) as an input to its stitching simulation, with no mechanism described. — [Style3D blog](https://www.style3d.com/blog/?p=13118)

### Inferences
- Interfacing/stiffeners are most cheaply represented as a per-region material override (higher bending stiffness, optional extra thickness for collision offset). Unsourced.

### Gaps
- Physical gathering/shirring papers, thickness modelling, layered collision offset: not found.

## Q4. Adaptive remeshing (ARCSim and successors)

### Takeaway
ARCSim is the reference implementation of adaptive anisotropic remeshing for cloth. Its licence is non-profit only, so it is usable for study but not for a commercial product. No GPU or interactive variant was located.

### Cited Findings
- ARCSim (Berkeley, O'Brien group): v0.2.1 folding version, v0.3.1 adds tearing; features anisotropic remeshing aligned with wrinkles/creases, physics-aware projection for stiff materials, strain limiting, bending plasticity, a fabric database, and SELF-CCD collision. Licence: free for non-profit use, citation required; full terms in the included licence file. The page does not mention GPU support. — [ARCSim page](https://graphics.berkeley.edu/resources/ARCSim/index.html)
- Papers cited by ARCSim: Narain, Samii, O'Brien 2012 (adaptive anisotropic remeshing, SIGGRAPH Asia); Narain et al. 2013 (folding and crumpling); Pfaff et al. 2014 (tearing/cutting). — [ARCSim page](https://graphics.berkeley.edu/resources/ARCSim/index.html), [SIGGRAPH history entry for 2012 paper](https://history.siggraph.org/?p=207980)
- Forks of ARCSim exist on GitHub (e.g. tomrunia/arcsim, Wajov/arcsim-0.3.1), whose licences were not checked. — [search results](https://github.com/tomrunia/arcsim)

### Inferences
- Effort to reimplement anisotropic remeshing (metric from curvature/velocity gradient, edge flip/split/collapse, resampling of state): XL for an interactive-quality version in a cloth core with implicit solver and collision. A reduced version (isotropic refine on seam zones and high-curvature, static refine-then-settle during the initial drape only) is M. The reduced version suits the "drape once, then dress" use case better than continuous interactive remeshing.
- Risk: remeshing mid-simulation breaks persistent contact/seam topology, and mesh-dependent sparse factorisations (the existing sparse_chol) need re-analysis each topology change.

### Gaps
- Not verified: reported speedups of ARCSim, practicality interactive, Marvelous's adaptive mesh behaviour, GPU/lightweight adaptive variants.

## Q5. Wrinkle enrichment from coarse simulation

### Takeaway
Three tiers exist: procedural/geometric post-process (Rohmer 2010), learned linear upsampling (Kavan 2011, patented), and neural super-resolution (GDSR 2024, code released). For a static drape product, geometry/normal-map enrichment driven by stretch/compression is the cheapest; learned methods need training data from a fine simulation.

### Cited Findings
- Rohmer, Popa, Cani, Hahmann, Sheffer, "Animation Wrinkling", ACM TOG 29(5), SIGGRAPH Asia 2010. Post-process; uses the coarse sim's stretch (compression) tensor to place wrinkles; space-time placement for temporal coherence; curve-based implicit deformers; one user parameter for fabric. Qualitative performance claims only. No code listed. — [project page](https://imagine.inrialpes.fr/people/Damien.Rohmer/documents/publications/10_sigasia_wrinkle/10_sigasia_wrinkle.html)
- Kavan, Gerszewski, Bargteil, Schroeder, "Physics-Inspired Upsampling for Cloth Simulation in Games", SIGGRAPH 2011 (ACM TOG 30(4), DOI 10.1145/2010324.1964988). Learns linear, context-specific upsampling operators from coarse/fine pairs with regularisation; adds high-frequency detail via oscillatory modes; the teaser quotes 0.8 ms on a single CPU thread. — [paper page](https://users.cs.utah.edu/~ladislav/kavan11physics/kavan11physics.html)
- Kavan et al. is patented: US8831919B2 "Physics-inspired up sampling for cloth simulation" (assignee not checked). Relevant for freedom to operate. — [Google Patents](https://patents.google.com/patent/US8831919)
- Neural Garment Dynamic Super-Resolution (Zhang and Li, SIGGRAPH Asia 2024): Mesh-Graph-Net plus implicit wrinkle residual per coarse triangle; trained on Marvelous Designer sims at 30 mm vs 10 mm particle spacing; reported about 3x (14.5k verts) to 5x (43k verts) faster than physics-based simulation by total time (the paper's table; ratios are the fetch tool's calculation); about 65 MB network; limits: single fabric, no friction wrinkles, multi-layer collisions unresolved, unoptimised PyTorch. Code: github MengZephyr/Neural-Garment-Dynamic-Super-resolution (licence not checked). — [arXiv 2412.06285](https://arxiv.org/html/2412.06285v1)
- FNOpt (Chen, Tran, Parashar; arXiv 2512.05762, Dec 2025): self-supervised cloth simulator with Fourier-neural-operator-parameterised optimiser, trained on coarse grids and claimed to generalise to finer meshes with fine wrinkles. Generic cloth, not garments. Only the abstract was seen. — [arXiv 2512.05762](https://arxiv.org/abs/2512.05762v1)
- A 2021 J. Comput. Sci. Technol. paper "Multi-Feature Super-Resolution Network for Cloth Wrinkle Synthesis" exists. Not read. — [JCST](https://jcst.ict.ac.cn/EN/Y2021/V36/I3/478)

### Inferences
- For failure (2) in a static-drape viewer: a stretch/compression-driven displacement or normal map layer in the Rohmer style could be M, and independent of the solver. Ruffle/gather-specific wrinkles are not covered by any located source.
- Neural options: L to XL (data generation, training, runtime); weak fit for arbitrary user patterns and fabrics given single-fabric limits.

### Gaps
- Muller and Chentanez "Wrinkle Meshes" (2010): search returned only unrelated medical results; not located. Lahey 2020 "NeuralWrinkles" not located (may be misnamed). Procedural/normal-map fold synthesis products: not located. Other 2025-2026 garment-specific work: not found.

## Q6. Pre-folded starts, relaxation, static settle

### Takeaway
No located source addressed quasi-static settling or damping ramps directly. Only one related data point appeared: neural methods initialise garments by skinning, which causes heavy self-intersection.

### Cited Findings
- ContourCraft (Grigorev et al., SIGGRAPH 2024) initialises garments with linear blend skinning from the first pose; this creates self-penetrations. Intersecting triangle pairs per frame dropped from 8,796 (HOOD) to 150; a perceptual study rated it 4.15 vs CLO3D 4.29. Fails if garments start entangled with unrecoverable layer order. Code promised upon acceptance, not verified as released. — [arXiv 2405.09522](https://arxiv.org/html/2405.09522v2)

### Inferences
- Layered outfits need a deterministic layer order at start; ContourCraft's failure case supports keeping explicit layer ordering in the start placement.
- Common engineering practice (not sourced here): heavy global damping for the first N frames then released; seam rest-length ramp; coarse-to-fine settle (drape on a coarse mesh, prolong to fine mesh, short polish). The last one pairs naturally with Q4/Q5 for failure (2).

### Gaps
- Quasi-static / static-equilibrium cloth solver papers, ramp heuristics in commercial tools, Browzwear/Optitex/Lectra behaviour: not researched or not found.
