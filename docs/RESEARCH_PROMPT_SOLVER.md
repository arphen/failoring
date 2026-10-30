# Deep research brief: cloth solver and render fidelity/performance for a garment design app

You are doing a deep literature and state-of-the-art review for a small team building an interactive garment design
tool. The goal is a **ranked stack of concrete things to implement** to make the cloth simulation and rendering much
faster and much more faithful. Nothing is off the table: replacing the solver, the renderer, or the whole engine is
acceptable if the evidence says it pays off. Prefer depth and verified sources over breadth.

## The product

"KSP for sewing": the user cuts flat 2D pattern pieces, marks seams between edges, and watches the garment sewn and
draped on a parametric human body (MakeHuman-derived, built from the user's own measurements), then walking. The
target quality is CLO3D / Marvelous Designer / Style3D, but **interactive**: the user (or an LLM agent acting for them)
changes a pattern and expects to see the drape update live, and also wants photoreal stills and short movies.

Typical garments: T-shirts, tank tops, trousers, pencil and A-line skirts, tiered/gathered skirts and dresses with frills
and ruffles, layered outfits (top + skirt, several garments at once).

## The current stack (what exists today)

- **Engine/UI**: Godot 4.3, the editor written in GDScript; native code as a GDExtension (C++). Also a browser build
  (WASM) of an older JS version. Hardware target: Apple Silicon laptops (dev machine: M3, 16 GB); Godot renders through
  Vulkan on MoltenVK/Metal.
- **Cloth model (C++ `cloth_core`)**: triangle FEM, anisotropic woven-fabric parameters per unit width taken from
  textile measurements (Kawabata KES-F style): tensile warp/weft (N/m, ~3000), shear, bending rigidity warp/weft
  (~1e-5 N·m), in-plane compression ratio (compression softer than tension), optional strain-stiffening J-curve for knits,
  areal density, viscous damping, static/kinetic friction. Ten fabric presets (poplin, crepe, chiffon, charmeuse, linen,
  wool suiting, denim, canvas, jersey, leather).
- **Integrators present**: implicit Euler with Newton iterations and a supernodal sparse Cholesky (CPU, accurate,
  slow: ~85 % of time in factorisation); a CG / block-Jacobi variant; experimental VBD (vertex block descent), barrier
  (IPC-style) contact with CCD and self contact, an augmented-Lagrangian contact path.
- **GPU path ("fast solve")**: whole frames in compute shaders through Godot's RenderingDevice: 10 substeps, **one
  linearised Newton solve per substep**, block-Jacobi CG capped at 100 iterations, then a **3 % strain-limit projection**.
  This is what the app uses everywhere now.
- **Body collision**: the skinned body baked to an SDF grid each frame; cloth-body friction (Coulomb, inside the Newton
  solve on CPU). Self collision optional (needed for layered garments).
- **Seams**: stiff springs between paired edge vertices with a ramp-in over ~20 frames. Pieces start wrapped around the
  body (cylinders / body cross-sections at a given height), seams then pull them together. Gathering = sewing a long edge
  to a short one (ratio 1.3–2.5x); we now pre-fold gathered pieces into an accordion at the start.
- **Resolution**: triangle edge 6 cm (draft) / 5 cm (preview) / 4 / 2.8 / 2.5 / 1.5 cm (ultra). A tee at preview is ~475
  sim vertices, a tiered dress ~750; the display mesh adds two subdivision levels. Timings on the M3: preview ~20–50
  ms/frame on the GPU; ultra (1.5 cm, a four-tier dress) ~0.5 s/frame.
- **Rendering**: Godot Forward+ (PBR, SDFGI/SSR/SSAO in a studio scene), a custom cloth shader with a procedural weave,
  subsurface skin, card-based hair. Offline "render mode" re-simulates and renders frames supersampled.

## Known failures (please map research to these)

1. **Never comes to rest**: the GPU fast solve keeps a residual jitter on a standing body forever (a tee flickers at the
   armpits where cloth is pinched between arm and torso; a tiered dress keeps swinging ~14 mm per 2 s). The same policy
   on the CPU behaves the same, so it is the method (single linearised step + strain-limit projection + capped CG), not
   GPU numerics. Turning the strain limit off makes heavier garments explode; extra viscous damping only partly helps.
2. **Resolution vs speed**: gathers, ruffles and folds only look right at ~1.5 cm triangles (10–50k vertices per
   outfit), which is far from real time today. Coarse meshes crumple gathered fabric into random facets and let tiers
   "ride up" 10–15 cm.
3. **Contact quality**: pinch points (armpit, crotch, between layers), layered garments, sliding on the skin while
   walking, seam allowances/thickness, friction realism.
4. **Initial placement and sewing**: pieces start on simple wrap surfaces and seams yank them together; large start gaps
   tangle or cause energy spikes; gathered seams start badly. How do CLO/Marvelous-style systems arrange and sew?
5. **Material fidelity**: are our parameters/constitutive model enough to reproduce measured drape (Cusick drape
   coefficient, cantilever bending, KES curves)? Bending model choice, buckling/wrinkle wavelength vs mesh size,
   anisotropy, hysteresis.
6. **Rendering realism**: fabric appearance (woven/knit microstructure, sheen, fuzz, thickness, translucency of light
   fabrics), real-time vs offline; also overall look (the body/face currently reads as uncanny).

## What to research (pull all stops)

Cover each area with the best papers (SIGGRAPH / SIGGRAPH Asia / TOG / SCA / Eurographics / CGF / NeurIPS where
relevant), production/industry write-ups, and open-source implementations. For every promising method, state the
specific failure(s) above it addresses.

**A. Solvers and time integration (performance + stability at rest)**
- Vertex Block Descent and its follow-ups (e.g. augmented VBD), XPBD and small-steps, projective dynamics and its GPU
  variants, ADMM-based solvers, Chebyshev/Anderson acceleration, multigrid and multilevel preconditioners for cloth,
  GPU-friendly preconditioned CG, GPU sparse direct solvers, Newton variants with line search on GPU.
- Which methods are **dissipative and come to rest** reliably at large time steps, and how they handle stiff in-plane
  (near-inextensible) fabric without strain-limit projection (nonsmooth strain limiting, constraint-based
  inextensibility, stiff-material-specific solvers).
- Realistic numbers: vertices × substeps × ms on a laptop/consumer GPU, and whether results transfer to Apple GPUs /
  Metal.

**B. Contact, friction and collisions**
- IPC family (IPC, Codim-IPC/C-IPC, GPU IPC such as GIPC/StiffGIPC and later), barrier-augmented Lagrangian, and what
  they cost per frame at 10–50k vertices; cheaper robust alternatives (SDF contact with proper friction, projective
  friction, impulse methods, untangling methods such as global intersection analysis).
- GPU self-collision broad phase (spatial hashing, BVH refits) and multi-layer garments; handling of pinch points.

**C. Garment-specific pipeline**
- Sewing and initial arrangement (arrangement points, pre-positioning, seam-line constraints vs springs, rest-shape
  seams), gathering/pleating/shirring, seam allowance and thickness, interfacing; what CLO/Marvelous/Style3D publish or
  patent about this.
- Adaptive remeshing (ARCSim lineage) and wrinkle enrichment/upsampling so a coarse sim shows fine folds; detail
  synthesis for gathers and ruffles.
- Learned/neural approaches that could give real-time preview with physics-quality output: HOOD, SNUG, neural cloth
  simulation with graph networks, ContourCraft, GarmentCode and related; data needs, generalisation to arbitrary user
  patterns and bodies, and whether they can be a fast preview in front of a physical solver.

**D. Materials**
- Measured-material pipelines (KES/FAST/Cusick to simulation parameters), data-driven cloth models, yarn-level vs
  continuum trade-offs, bending models (discrete shells, quadratic bending, cubic shells) and their mesh dependence,
  validation methodology (Cusick drape tests, cantilever) we could automate as regression tests.

**E. Rendering fabric and people**
- Cloth appearance models (microcylinder/woven BRDFs, SpongeCake and volumetric fabric, neural/real-time approximations,
  fuzz and sheen), translucency of thin fabrics, real-time techniques usable in a rasteriser vs a path tracer.
- What it would take to get CLO-level stills: path tracing on Apple GPUs, denoising, hardware ray tracing on M3.
- Digital human realism basics relevant to an avatar built from a few selfies (skin, eyes, lighting) — briefly.

**F. Engine and platform decision**
- Should the solver and renderer leave Godot? Compare: staying in Godot (GDExtension + RenderingDevice compute),
  a custom engine on Metal directly, wgpu/WebGPU (also covers the browser), Vulkan, or libraries such as NVIDIA Warp,
  Taichi, MLX/Metal Performance Shaders, Filament/bgfx for rendering. Consider GPU compute ergonomics on Apple silicon,
  unified memory (zero-copy CPU/GPU), dispatch overhead (we measured readback/dispatch costs in Godot), debuggability,
  and the cost of rewriting the editor UI.
- Open-source solvers worth adopting or studying instead of building (code quality, licence, GPU backend, maintenance).

## Required output

1. **Executive summary**: the 5–10 changes with the best fidelity/performance gain per unit of work, in order.
2. **The stack**: a table, one row per method/paper/tool, with: name; authors, year, venue; link (DOI or arXiv) and code
   link + licence; which failures (1–6) it addresses; the gain the paper reports (with its hardware and scene size);
   implementation effort (S ≤ 1 week, M ≤ 1 month, L ≤ 3 months, XL more) for a small team that already has a C++ FEM
   cloth core; GPU/Metal suitability; risks and prerequisites.
3. **A staged roadmap**: (a) quick wins inside the current architecture, (b) a new solver core, (c) the engine question,
   with what each stage unlocks (e.g. "real-time at 1.5 cm for a single garment", "rest-stable drape", "layered outfits").
4. **Recommended solver architecture** if we rebuild: integrator, contact, self collision, seams, materials, GPU data
   layout, and how an interactive editor and an offline high-quality render share it.
5. **Benchmarks to adopt**: standard scenes and measurable metrics (rest stability, drape coefficient, frame time vs
   vertex count, contact penetration) so we can compare our solver against the literature and against CLO.
6. **Open questions and disagreements** in the literature, and where the evidence is thin.

Rules: cite only sources you have actually located; give links; mark anything inferred or unverified. Prefer 2019–2026
work but include the classics that later work builds on. Quantify wherever papers do. Note Apple/Metal-specific
caveats explicitly (no CUDA; many GPU cloth papers are CUDA-only).
