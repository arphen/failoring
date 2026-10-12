# Failoring - architecture and status

A tailoring tool in the spirit of "KSP + AutoCAD for sewing": cut pattern pieces on a table, say which
edges are sewn together, and see the exact cut cloth drape on a parametric body with physically based
fabric behaviour - then use the same pieces as the real pattern.

This repository is the **drape prototype** (milestone 1): pattern pieces + seams + fabric presets +
body collider -> simulated garment, verified headless and exposed to Godot.

## Layout

```
cloth_core/            C++17 library, no dependencies, no Godot  (the simulator)
  include/failoring/   math, material, collider, mesher, mesh_sizing, cloth, sparse_chol, metrics
  src/
  tests/               test_basics, test_cantilever, test_drape, test_garment  (ctest)
materials/fabrics.json fabric presets (SI units, grain-aware)
gdextension/           godot-cpp wrapper -> class FailoringSim (GDScript API)
godot/                 demo project: tube dress on an ellipsoid mannequin, fabric switcher
third_party/godot-cpp  (cloned, godot-4.3-stable)
wasm/                  flat C API over the core (wasm_api.cpp) + built failoring.wasm (tools/build_wasm.sh, wasi-sdk 24)
app/                   the browser application: src/*.js, build.js -> failoring.html (single self-contained file)
docs/                  this file
```

Build and test: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build`
Godot module: `cmake -S gdextension -B build-gd && cmake --build build-gd -j` (writes `godot/bin/libfailoring.so`).

## Simulation model

| Aspect | Choice |
| --- | --- |
| Discretisation | Triangle meshes per pattern panel, panel rest coordinates in a **grain-aligned** frame (warp = u, weft = v) |
| In-plane | Per-triangle warp stretch, weft stretch, shear (Baraff-Witkin style) - independent stiffness per direction (N/m), reduced compression stiffness (`compression_ratio`) so cloth buckles instead of locking |
| Bending | Dihedral hinge energy, directional rigidity by mixing compliance `1/B(psi) = cos^4/Bu + sin^4/Bv + 2 sin^2 cos^2 / sqrt(Bu Bv)`, mesh factor calibrated against beam theory |
| Time integration | Implicit Euler written as a variational problem; Newton with Gauss-Newton Hessian, backtracking line search on the variational energy, sparse Cholesky (nested-dissection ordering). CG+SSOR fallback. XPBD kept as `Integrator::XPBD` (too soft for stiff fine meshes) |
| Contact with body | SDF colliders (sphere, capsule, cylinder, plane, ellipsoid, union). Handled **inside** the solve: per-vertex projection + motion filters, projected line search; Coulomb friction after the solve |
| Self contact | Vertex-triangle with spatial hash, thickness `selfThickness` |
| Seams | Arc-length matched point-to-segment springs (exact quadratic energy). Unequal edge lengths are allowed = **ease**. Optional stiffness ramp (`seamRampFrames`) to pull panels together gently |
| Units | SI throughout: N/m for in-plane stiffness, N*m for bending, kg/m^2 for density |

### Mesh resolution (Zhang, Wang et al., SIGGRAPH 2025)

`mesh_sizing` implements the paper's estimator: wrinkle wavelength from `B/E`,
`c1 r^6 + c2 r^4 = B/E` (c1 = 32.08 m^-3, c2 = -1.876e-4 m^-1), anisotropic warp/weft resolution, 2.5 mm
floor. Reproduces the paper's Table 1 to 0.57 % (test_basics). Not yet implemented: sizing *maps*
(transition distance, Poisson-disk sampling) - the mesher currently takes one edge length per panel.

## Verification (ctest)

| Test | What it proves | Result |
| --- | --- | --- |
| `test_basics` | Material parsing; mesher quality (disc area error 0.07 %, min angle 29.7 deg, polygon boundary complete); hinge gradient vs finite differences (1e-10); estimator vs paper Table 1 | pass |
| `test_cantilever` | Euler-Bernoulli cantilever deflection `rho g L^4/(8B)` within 1-2 % on fine hex meshes (30 % / 8 % / 8 % tolerance at 7 / 3.5 / 2.5 mm); warp/weft grain contrast | pass |
| `test_drape` | Cusick-style drape coefficient for all presets: fluid < crisp < stiff, stable, no penetration | see below |
| `test_garment` | Two panels sewn into a tube, lodging on an ellipsoid mannequin. The hem must stop where the body girth equals the tube circumference (computed independently): predicted 161 mm, simulated 158 mm; seams within 0.2 mm; no penetration | pass |

Drape coefficients (30 cm disc on 18 cm pedestal, 10 mm mesh): silk 36 %, chiffon 38 %, jersey 28 %,
poplin 64 %, wool 65 %, denim 81 %, linen 82 %, canvas 93 %, leather 91 %. Textile-literature ranges:
fluid ~30-50 %, cotton/wool ~50-70 %, stiff 70-100 % - the ordering and magnitudes are plausible.
**These are not validated against measured cloth** (see limitations).

## Lessons that shaped the solver (so nobody re-learns them)

* Position-based dynamics could not hold stiff, finely meshed cloth within a frame budget -> implicit Euler.
* Newton **must** converge tightly: stopping at a loose tolerance or iteration cap turns the integrator
  partly explicit and stiff fabrics (linen, wool, denim) slowly blow up. Default: up to 12 iterations,
  exit at `newtonTolerance = 1e-6`.
* Contact must be part of the Newton iteration. Applying it after the solve made the cloth explode
  (drape coefficient 297 %). A step that carries a vertex through the body, then gets projected back,
  makes Newton cycle forever; the line search therefore evaluates energy on projected (feasible) points.
* Seam energy is exactly quadratic; use the exact Hessian `k w_a w_b I`. A Gauss-Newton rank-1 Hessian
  leaves the tangential direction with no stiffness once contact removes the normal one.
* Contact projection injects energy when a ring is wedged on a convex body; 4 substeps keep the
  `test_garment` ring within 2 % strain (1 substep let it stretch 12 %).
* Test geometry matters: a tube of circumference C cannot be put around a belly with girth > C, and flat
  panels placed beside the body collapse into a pillow. `test_garment` pre-wraps the panels onto a
  cylinder so seams start closed.

## Performance (single core, Release)

Cholesky factorisation dominates. The numeric kernel is a left-looking **supernodal** factorisation
(dense blocks; 1.6-2.1x faster than the original up-looking code, cross-checked to 1e-16 in test_basics;
it now runs at ~9 GFLOP/s, i.e. compute-bound). ~1500 vertices: ~0.37 s per 60 Hz frame; ~670 vertices
with 4 substeps: ~0.4 s per frame. **This is not real time.**

Tried and rejected: chord Newton (reusing one factorisation across Newton iterations - diverges,
`reuseFactorization`, off), and a stale-Cholesky-preconditioned CG (`LinearSolver::CholeskyPCG`,
experimental: the Hessian and contact set change too much between iterations, so it needs a refresh
nearly every solve and ends up 2x slower than the direct solve).

What is left, in order of expected payoff: fewer unknowns (adaptive resolution from the paper - refine
only where wrinkles form), fewer Newton iterations (better contact handling so Newton converges in
~3 instead of up to 12), multithreaded / GPU dense kernels, and a "settle then freeze" preview with a
low-resolution live mode while editing.

## Known limitations

* **Fabric data is a starter set.** Values in `materials/fabrics.json` are KES-style estimates from the
  literature (conversions documented in the file), not measurements. Replace with measured data
  (Kawabata/FAST) or fitted data sets (e.g. Wang et al. 2011, Clyde et al. 2017) before trusting absolute
  numbers.
* Linear-elastic per direction: no knit/leather non-linearity (J-shaped stress-strain, hysteresis),
  no stretch-dependent bending, no plastic creasing.
* Strongly orthotropic bending (> ~1:5 warp/weft) is under-represented by the hinge model.
* Alternating-diagonal grid meshes are over-stiff on diagonals; the mesher produces hex-ish meshes
  for that reason. User-drawn polygons with features thinner than the edge length may fail
  (`boundaryComplete == false`).
* Seams are springs, not geometry: no seam allowance thickness, no topstitch, no gathers beyond
  unequal edge lengths.
* Godot wrapper compiled but **not run inside Godot** in the build environment (no Godot binary);
  `godot/demo.gd` is untested.
* A garment wedged on a convex body keeps jittering at ~1e-4 J (about one line-search failure per frame)
  instead of reaching exactly zero kinetic energy; positions are stable to ~1 mm.
* Self-collision is vertex-triangle only (no edge-edge) - thin tangles can pass.

## Browser application (failoring.html)

One self-contained file (about 1.6 MB: three.js r149, the 650 KB WebAssembly build of the core, the worker, fabrics). Open it in a
current Chrome / Edge / Firefox / Safari; no server, no network. Rebuild with `node app/build.js` (needs `wasm/failoring.wasm`,
`tools/build_wasm.sh` makes it).

```
cutting table (2D canvas)  <->  App state (pieces, seams, fabric, body)  ->  Web Worker { wasm cloth core }
3D view (three.js)         <-  streamed frames { positions } <------------'
```

* **Cutting table** (`editor2d.js`): pan/zoom, drag pieces, drag corners (5 mm snap), double-click an edge to add a corner,
  Delete removes a corner or piece, Ctrl+Z / Ctrl+Shift+Z undo/redo. **Sew** tool: click an edge, then the edge it joins; the seam
  list shows both lengths and the ease. Grain line and hatch show the warp direction.
* **3D view** (`view3d.js`): parametric mannequin (the same primitives the solver collides with, drawn 4 mm smaller so cloth resting
  1.6 mm off the surface never z-fights), cutting-table mat, cloth mesh with per-piece tint, sew-lines between matched seam vertices.
* **Assembly timeline** (T from 0 to 4; the scrubber, stage buttons, play, step and speed control it):
  1. pieces lie flat on the table; 2. staggered, each piece lifts, rotates *rigidly* (quaternion slerp) and stands upright around the
  body, then bends onto the wrap surface; 3. sew-lines appear and tighten; 4. the recorded drape frames play. Stitch lines are faint guides
  while pieces move and solid during stage 3.
* **Live editing**: any edit (corner, grain, placement, seam, fabric, body, mesh quality) cancels the running job and starts a new one
  after a 120-260 ms debounce. The preview follows the newest frame, so it is low frame rate but live. Timing on this container
  (single core, no SIMD-capable GPU): about 65-200 ms per frame at 400-700 vertices.
* **Plans** (`patterns.js`): pencil skirt (2 shaped panels), A-line skirt (4 gores), blank. They are original drafts, not copies of
  commercial patterns. Both are 80 cm at the top edge so they hang from the waist: an unshaped tube bigger than the waist slides over
  the hips and falls. This is real behaviour (it is why skirts have darts or waistbands); the same happens when you pick the "plus"
  body for a pattern drafted for the average one - patterns are deliberately not auto-scaled.
* **Pattern file** (`*.failoring.json`, Save / Open):

```json
{ "format": "failoring-pattern/1", "name": "...", "notes": "...", "fabric": "wool_suiting",
  "meas": { "height": 1.68, "bust": 0.92, "waist": 0.74, "hip": 0.98, "shoulder": 0.40 },
  "pieces": [ { "id": "gore1", "name": "Gore 1",
                "outline": [[-0.25,0],[0.25,0],[0.10,0.62],[-0.10,0.62]],   // metres, edge i = outline[i] -> outline[i+1]
                "grain": 1.5708,                                               // warp direction, radians from the piece x axis
                "table": { "x": 0.35, "y": 0 },                                // position on the cutting table (display only)
                "wrap":  { "theta": 45, "y": 0.42, "clear": 0.012, "ring": 4 } } ],   // placement on the body, see below
  "seams":  [ { "a": ["gore1", 1], "b": ["gore2", 3], "reverse": true } ] }
```

  `wrap.theta` is the angle round the body (0 = front), `wrap.y` the height of the piece's bottom edge, `clear` the initial gap to the
  body, `ring` lets a piece wider than the body open up (A-line). Outlines are converted to counter-clockwise on load and seam edge
  indices are remapped. The wrap is only the *starting pose* of the simulation; the solver finds the final drape.
* **Photo shoot** (`shoot.js`): seven curated shots (full length, three-quarter, profile, low-angle hero, waist/seam detail, back
  three-quarter, hem detail) of the latest simulated frame. Long lenses (fov 17-30 degrees), a key light placed relative to each camera,
  fill and two rims, a glossy runway, then a 2D composite: gradient backdrop with bokeh discs, warm/cool grade, vignette, film grain,
  caption. Output is PNG (800x1000, 1200x1500 or 1600x2000).

### Catwalk walk-by (designed, only partly built)

The shots currently use the standing pose and only turn the model a few degrees. The walk itself is the part to come back to:

* `FL.bodyPrims(meas, phase)` already produces a walk cycle (thigh swing, knee flex, arm swing, bob, sway) and the worker accepts
  `spec.walk = { settle, cycle }`, which re-sends the collider every frame after `settle` frames. **Not validated**: a body moving at
  walking speed through an implicit solver needs more substeps or CCD, and the garment must be pre-settled first.
* Plan: settle the garment standing, then run one walking cycle at a reduced speed (slow cycle = few cloth frames per step), record
  the frames, and pick curated moments of the recorded cycle for the cameras ("as long as the model isn't so fast").
* Cheaper fallback that already works: rigid translation of the finished drape along the runway, which is what a different camera
  position gives you today.

### Performance ideas not yet done (from the 2025-26 papers)

1. Static solve: the preview only needs the settled drape. Ramp gravity in, run Newton to equilibrium, warm-start from the previous
   result after an edit (small edits move the answer little). Expected to replace 36-60 timesteps by a handful of solves. Unmeasured.
2. Distance-grid collider (mesh or SDF baked to a voxel grid): O(1) lookup per vertex, any body shape (Inigo the dragon, scans).
   Lipschitz pruning (Barbier et al., CGF 2025) is the way to bake a detailed primitive-based body quickly when a slider moves.
3. Reduced handle subspace (PhySkin, Srivastava et al. 2026): about 128 skinned handles, Newton in the subspace, full mesh by
   skinning. The paper's trained network is per garment/body family (32 h training) so not usable for arbitrary edits; the subspace
   structure without the network might be.

## Roadmap

1. **Performance** (above) - prerequisite for everything interactive.
2. **Cutting table** (2D CAD editor in Godot): polygon/curve drawing with exact dimensions, grain line,
   notches, seam allowance, fabric roll with width and nesting, tools (cut, mirror, fold, dart,
   grade). Output = `PatternPiece` JSON (outline, grain, material, edge ids) -> `add_polygon_panel`.
3. **Sewing tools**: seam picking UI with length-mismatch (ease) readout; stitch types (plain, serged,
   topstitched, French seam) mapped to stiffness/thickness/width; gathers/shirring; darts; hems;
   interfacing as a laminated material.
4. **Pattern import**: SVG/DXF/PDF (AAMA/ASTM-DXF) -> pieces. Free pattern sources and licences must be
   checked per pattern before bundling anything.
5. **Bodies**: parametric model with measurement sliders + presets (bust/waist/hip, height, shoulder,
   arm length...). Candidates and licence notes (**verify before shipping**): MakeHuman (CC0 output
   assets, AGPL application code), MB-Lab (AGPL), SMPL-X (research / non-commercial licence -
   likely unusable for a product). Pipeline: mesh -> signed-distance voxel grid -> `Collider`
   (a mesh collider backend replaces the analytic ellipsoid). Blender for authoring body shapes and
   exporting measurement-driven morph targets.
6. **Fabric library**: ingest measured data, uncertainty ranges, per-fabric thickness/drag; an editor to
   fit a preset from a hanging-strip + cantilever + drape-coefficient test the user performs on a real
   swatch.
7. **Make it real**: print-ready tiled/1:1 patterns with seam allowances, cutting layout, yardage
   estimate, assembly order.
8. Adaptive mesh resolution (sizing maps) from the 2025 paper; anisotropic warp/weft meshes.

## Scheduled follow-ups worth checking

Fit validation against a real swatch (draw a 30 cm disc, measure its drape coefficient with a
photographed shadow, compare to the simulated one for the same fabric) is the single most valuable
reality check for the whole project.
