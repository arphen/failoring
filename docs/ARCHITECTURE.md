# Failoring - architecture and status

A tailoring tool in the spirit of "KSP + AutoCAD for sewing": cut pattern pieces on a table, say which
edges are sewn together, and see the exact cut cloth drape on a parametric body with physically based
fabric behaviour - then use the same pieces as the real pattern.

This repository holds the **drape prototype** (milestone 1: pattern pieces + seams + fabric presets + body
collider -> simulated garment, verified headless and exposed to Godot) and the **browser demo** built on the same
core (`failoring.html`). Two builds exist and are kept deliberately separate; see "Two builds" below.

## Layout

```
cloth_core/            C++17 library, no dependencies, no Godot  (the simulator)
  include/failoring/   math, material, collider, mesher, mesh_sizing, cloth, sdf_grid, sparse_chol, metrics
  src/
  tests/               test_basics, test_cantilever, test_drape, test_garment, test_sdf_grid  (ctest)
materials/fabrics.json fabric presets (SI units, grain-aware)
gdextension/           godot-cpp wrapper -> class FailoringSim (GDScript API)
godot/                 demo project: tube dress on an ellipsoid mannequin, fabric switcher
third_party/godot-cpp  (cloned, godot-4.3-stable)
wasm/                  flat C API over the core (wasm_api.cpp) + built failoring.wasm (tools/build_wasm.sh, wasi-sdk 24)
app/src/               the browser application (see below): shared.js, body.js, gait.js, draft.js, fsimport.js,
                       patterns.js, worker.js, main.js, editor2d.js, view3d.js, shoot.js, state.js, style.css
app/assets/            body_mh.bin (MakeHuman CC0 mannequin, 308 KB); freesewing/ (pre-drafted FreeSewing outputs, MIT)
app/build.js           -> failoring.html (single self-contained file)
tools/                 convert_makehuman.py (MakeHuman -> body_mh.bin); freesewing/ (node export of FreeSewing parts);
                       fs_plan.js, fs_batch.sh (FreeSewing import, headless); walk_sim.js, verify_body.js, shoot_walk.js
docs/                  this file, BODY_DESIGN.md (body decision), WIP_HANDOFF.md (handoff notes)
```

Build and test: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build`
Godot module: `cmake -S gdextension -B build-gd && cmake --build build-gd -j` (writes `godot/bin/libfailoring.so`).

## Two builds

* **Browser demo** (`failoring.html`, one offline file). It shows the workflow end to end on any machine with a
  current browser, and it stays as a demo: it runs the cloth core as WebAssembly in a Web Worker, on one CPU core in
  the test container, so frame rates are low.
* **Native Godot build** (`gdextension/`, `godot/`) is the **high-fidelity target**, and the place where the drape and
  fitting work continues. Today the Godot wrapper binds only analytic colliders
  (`add_ellipsoid`, `add_sphere`, `add_capsule`, `add_cylinder`, `add_plane`); the mesh collider and the drafted
  garments are not yet exposed to it (checked in `gdextension/src/failoring_sim.h`).

## Simulation model

| Aspect | Choice |
| --- | --- |
| Discretisation | Triangle meshes per pattern panel, panel rest coordinates in a **grain-aligned** frame (warp = u, weft = v) |
| In-plane | Per-triangle warp stretch, weft stretch, shear (Baraff-Witkin style) - independent stiffness per direction (N/m), reduced compression stiffness (`compression_ratio`) so cloth buckles instead of locking |
| Bending | Dihedral hinge energy, directional rigidity by mixing compliance `1/B(psi) = cos^4/Bu + sin^4/Bv + 2 sin^2 cos^2 / sqrt(Bu Bv)`, mesh factor calibrated against beam theory |
| Time integration | Implicit Euler written as a variational problem; Newton with Gauss-Newton Hessian, backtracking line search on the variational energy, sparse Cholesky (nested-dissection ordering). CG+SSOR fallback. XPBD kept as `Integrator::XPBD` (too soft for stiff fine meshes) |
| Tension term | `SolverParams::tensionStiffness`: adds the (PSD) tension part of the stretch Hessian. **Off in the core by default**; the browser app turns it on (`fl_set_param(7, 1)` in `worker.js`) so taut seams and waistbands do not creep. Changes convergence, not the physics |
| Contact with body | Signed-distance colliders: analytic primitives (sphere, capsule, cylinder, plane, ellipsoid, union) or a baked distance grid of a closed mesh (the mannequin, see "Mannequin, collider and walking"). Handled **inside** the solve: per-vertex projection + motion filters, projected line search. `contactBand = 1e-4` m: a vertex this close to the body and pushed towards it stays held. Coulomb friction after the solve, measured **relative to the collider's own motion** (a walking body drags the cloth along) |
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
| `test_sdf_grid` | The baked distance grid (collider of the mesh mannequin). Not read or re-run for this document update | unverified here |
| `test_skin_barrier` | K1 compliant skin and K5 barrier self contact enabled together (the test_garment tube on a 3 cm mesh, four modes: neither, skin, barrier, both). Runs, settles, seams closed, no self intersections in the barrier modes, penetration within the soft layer depth. Numbers in `docs/handoff/OPUS_SESSION_1.md` | pass (2026-10-14) |

Drape coefficients from `test_drape`, measured 2026-10-14 on main @ 842609f (Release build, 2-core Linux container,
306 s for the whole test). Scene: 30 cm disc on an 18 cm pedestal, dropped from 12 mm, 1 substep per 60 Hz frame. Mesh
edge length = the Zhang et al. estimate floored at 10 mm, which is 10.0 mm for every preset. The run stops once
KE < 1e-8 J after frame 100, or at frame 300; "300" below means the cap was reached and the DC is the frame-300 value
(KE still 6e-8 to 4e-7 J, below the test's 1e-4 J pass limit).

| Fabric | DC % | Frames | KE at end (J) |
| --- | --- | --- | --- |
| cotton_jersey | 27.6 | 223 | 8.9e-09 |
| polyester_chiffon | 43.1 | 300 | 1.9e-07 |
| silk_charmeuse | 46.6 | 300 | 2.2e-07 |
| wool_suiting | 64.8 | 125 | 8.8e-09 |
| cotton_poplin | 65.0 | 300 | 3.6e-07 |
| denim_12oz | 80.7 | 214 | 9.3e-09 |
| linen_medium | 81.2 | 300 | 8.1e-08 |
| lambskin_leather | 90.9 | 300 | 5.9e-08 |
| canvas_heavy | 92.8 | 179 | 8.4e-09 |

(An earlier version of this table gave silk 36 % and chiffon 38 %; those were stale. The source of those older
numbers was not traced.) Textile-literature ranges: fluid ~30-50 %, cotton/wool ~50-70 %, stiff 70-100 % - the
ordering and magnitudes are plausible. **These are not validated against measured cloth** (see limitations).

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

## Mannequin, collider and walking

The mannequin is a MakeHuman CC0 mesh with Failoring's own blending, fitting and collision code. The decision and
the alternatives considered are in `docs/BODY_DESIGN.md`.

### Asset

* `app/assets/body_mh.bin`, 308,183 bytes, written by `tools/convert_makehuman.py` (raw DEFLATE, magic `FLBODY01`,
  little-endian header). Source: MakeHuman 1.x assets, upstream commit `a8bc2d5` (recorded in `THIRD_PARTY.md`),
  **CC0 1.0** for the assets. No MakeHuman application code is used.
* Contents, read from the header: a female young-adult base mesh of **13,676 vertices**, of which **13,380 are the
  surface** (the rest are joint helper vertices), 13,378 quads; 60 blend-shape targets; 23 bones (MakeHuman's 163
  merged into a reduced skeleton: pelvis, spine, neck, head, and per side clavicle, shoulder, elbow, hand, upper leg,
  knee, ankle, toe); **4 skin weights per surface vertex** (8-bit, from MakeHuman's default rig, merged onto the
  reduced skeleton); 11 part labels (trunk, neck, head, arms, hands, legs, feet); landmark vertex ids; region masks
  for the finish.
* In the browser build it is embedded as base64 (about 0.41 MB of the 2.24 MB file, the current build).

### Fitting to measurements (`body.js`)

* Blend shapes: the base mesh plus targets interpolated for the macros (muscle, weight, height, cup, belly, seat)
  and the **12 measurement modifiers** behind the sliders (`B.CONTROLS`: bust, waist, hips, shoulder distance, neck,
  thigh, upper arm, upper and lower arm length, upper and lower leg height, nape-to-waist). Weights beyond a target's
  range are extrapolated linearly (`B.XMAX = 1.6`, `B.WMAX = 1.5`).
* Sliders: height, bust, waist, hip, shoulder (primary) and arm, inseam, neck, thigh, upper arm, back (secondary, "auto"
  unless set). A damped Newton loop with a finite-difference Jacobian (cached per body type) solves the target weights
  until each measured value is within **0.4 mm** of its slider (`0.0004` in the loop), at most 8 iterations.
  The 0.4 mm figure is the convergence target in code; the residuals of a particular run were not re-measured here
  (`tools/verify_body.js` reports them).
* Measurements are hull perimeters of plane sections of the blended mesh (tape-measure girths), and straight or
  polyline distances between landmarks for shoulder, arm and back length. Height is set by the height macro, then the body is scaled uniformly to the
  requested height, with the feet on y = 0 and the hip section centred on z = 0.
* Finish: nipple detail removed with MakeHuman's own corrective targets, and the nipple and crotch regions smoothed
  (4 feathered Laplacian passes over the mask vertices).
* The weight macro comes from how full the requested girths are relative to the neutral body of the same height,
  so limbs and neck stay in proportion; presets (average, petite, tall, curvy, athletic, plus) live in `shared.js`.
* Regeneration on each slider move: the comment in `main.js` says 10-30 ms; not re-timed here.
* Section table: every 1 cm of height the convex hull of the trunk slice (legs split below the crotch; arms and
  hands excluded by their part labels) is kept for wrapping and for the garment drafts.

### Collider (`sdf_grid.hpp`, `wasm/wasm_api.cpp`)

* The worker bakes the mesh into a **signed distance grid**: a fine level (default spacing **h = 7 mm**, sparse 2x2x2
  bricks in a band of 6 mm around the surface, exact point-triangle distances) and a coarse level (6h, dense over the
  bounding box plus 8 cm).
* **Sign: winding numbers** (signed ray crossings along +x), so the sign stays right where the mesh overlaps itself,
  for example the thighs of a full figure. Magnitude: distance to the nearest triangle. This is not the
  pseudo-normal method.
* Lookup: quadratic B-spline over the 3x3x3 = **27 taps**; the fine level where all 27 samples exist (always within
  the band), the coarse level elsewhere. Value and gradient are C1 within a level, which the projected line search
  needs. The collider normal is the normalised gradient.
* Accuracy stated in the header comment (not re-measured here): on the mannequin at h = 7 mm, the collider surface is
  within 0.15 mm median / 0.7 mm 95th percentile of the polygons on trunk and limbs, and 0.5-0.9 mm / 2 mm on hands,
  feet and face.
* Bake cost: a static body is baked once per body key; the app re-bakes when the body changes. `docs/WIP_HANDOFF.md`
  records **185-390 ms** per bake in wasm. (The single lower figure sometimes quoted, ~180-190 ms, is the bottom of
  that range; it was not re-timed here.)
* A floor plane is added to the colliders in mesh mode.
* The grid is generic: any closed, consistently oriented mesh works (scans, the Inigo test case).

### Display (`worker.js`, `wasm_api.cpp`, `view3d.js`)

* The mannequin is drawn from the same posed mesh the collider uses (`body.pos`), with display-only eyeballs in the
  sockets. There is no shrink and no lift of the body.
* The cloth is drawn as a **subdivided mesh**: one midpoint per edge, four display triangles per cloth triangle
  (`FL.subdivide`, `fl_display_setup`). `fl_display_positions` places the midpoints on the body: a midpoint near the
  body is pushed out along the body normal until it is as far from the surface as the nearer of its two vertices, so a
  flat triangle follows the curve of the body instead of cutting across it. Where a display triangle is still closer
  than 0.8 mm (`DISPLAY_MARGIN`), its vertices are lifted along the normal by the shortfall, at most 4 mm.
* Contact radius = `contactOffset` (1.5 mm) + half the fabric thickness.

### Walking (`gait.js`, `worker.js`, `main.js`)

* Motion "Walking" in the top bar. `FL.Gait.pose(M, body, phase, opts)` drives the skinned mannequin from keyframe
  curves for the hip, knee, ankle and toe, plus pelvis rotation and obliquity, counter-rotating spine, opposite-arm
  swing and elbow flexion. The curves follow typical adult sagittal-plane gait, **plausible, not measured on a person**.
* Walk in place, as on a treadmill: the lowest foot vertex is put on y = 0 every frame, which gives the pelvis bob.
  A small lateral sway is added at the root.
* Arm clearance (`G.stand`, `ARM_ABDUCT 0.14`, `ARM_GAP 0.02`, `ARM_MAX 0.5` in `gait.js`): the shoulder abduction is raised from the
  default 0.14 rad until the arms hang at least 2 cm clear of the torso section hulls below the armpit (curvy 0.202, plus 0.333, the
  others stay at 0.14), and `body.armAbduct` carries it into the walk spec (`main.js`, `tools/walk_sim.js`) and the Godot exporter
  (`tools/export_godot_assets.js`) so the gait poses use the same arms. So the arms hang clear of the body on the wide presets and a sleeve has room between arm and torso.
* `main.js`: one gait cycle is **64 frames** (about 1.07 s at 60 Hz); the stride eases in over **24 frames** (smoothstep)
  after the garment has settled standing; the run is the settle frames plus 1.5 cycles.
* Each walk frame, the worker poses the body, **re-bakes the collider** (`fl_set_body_mesh`, paying the bake time
  quoted above on every frame), and gives the cloth the surface displacement at each vertex (nearest body vertex of the previous pose,
  cell hash, 5 cm search radius), which the solver uses for friction (`setColliderMotion`).
* The photo shoot (`shoot.js`) spreads its shots over the last gait cycle of a walking run.
* Not validated against a long run: stability of the walk over many cycles, and the friction behaviour on the
  real body, were not checked for this update.

### Garment sources

Three ways a pattern reaches the app:

1. **Plans** (`patterns.js`): pencil skirt (2 shaped panels), A-line skirt (4 gores), blank rectangle. Original drafts.
2. **Hand-drafted from the body** (`draft.js`), redrafted with the **Re-draft** button after a body change:
   * `draft.pants`: straight trousers, 4 panels (front and back per leg), wrapped with mode `leg`. No waistband,
     pockets or darts.
   * `draft.tank`: two panels, unrolled from the body's own section arc lengths plus clearance, wrapped with mode `arc`
     (arc length = pattern x). `draft.shirt`: the same with sleeves (mode `arm`).
3. **FreeSewing patterns** (MIT licence, https://freesewing.org): `fsimport.js` takes the real FreeSewing parts for
   the mannequin's measurements (`tools/freesewing/export.mjs`, pre-drafted outputs in `app/assets/freesewing/`:
   Teagan tee, Penelope pencil skirt and Charlie chinos for the six presets). It wraps the parts with mode `fit`
   (body parts and waistbands), `leg` (chinos) and `arm` (sleeves), and **finds the seams itself**: edges that lie side
   by side after the starting wrap are sewn (within 5 cm, or 10 cm for sleeve caps), so darts close and sleeves join the
   armholes. The importer is part of the browser build (`app/build.js` embeds `fsimport.js` and the pre-drafted raw parts;
   the plan list and Demo mode use them); `tools/fs_plan.js` runs it headless (see `tools/fs_batch.sh`) and writes a plan
   file for `walk_sim.js`. Details that matter for trousers:
   * The drafting side (`export.mjs`) feeds Charlie the seat, seat/waist back widths and cross seam *measured on the
     mannequin* (`draft.measures`), and `waistHeight` 1 (top edge on the natural waist). Tuning `waistHeight` so the
     finished band lands exactly on the waist line makes the top edge 10-13% longer than the waist, and the trousers
     slide below it.
   * Rise ease (`WALK_RISE`, 1.04): the two cross-seam measurements get 4% on top of the standing values. The taut crotch path
     (centre front waist, crotch, centre back waist) is 3 to 7% longer while the body walks (the pelvis tips and the thighs swing:
     minimum about +3%, maximum +4.7 to +6.8% over the gait cycle on the six presets); trousers drafted on the standing length are
     pulled down by the legs until the band is taut, ride 5 to 13 cm below the waist, and on the plus body fall to the knees. 7 to
     8% leaves the band slack and the seat baggy.
   * Hem lift (`HEM_LIFT`, 30 mm; `lengthBonus = -30 / waistToFloor`): Charlie's default (+2%) puts the hem 1.7 to 2.6 cm *below* the
     floor. A hem on the floor or under a foot is stepped on while walking and drags the whole garment off the waist (tall and plus),
     so full-length legs stop 30 mm above the floor (an ankle bone is about 70 mm up).
   * Fly trim (`raw.store.waistbandFly`, 30.5 mm on average): the band is drafted with a button tab beyond the centre front. The tab
     overlaps the other end, it does not add to the ring, so the importer clips the band at it (`y = max(y, fly)`); the band is then
     as long as the trouser top edges together (77.8 cm on average).
   * `charlie.front` and `charlie.back` are both drawn for the same leg seen from outside: the front has the fork on the
     right, the back has the centre back and crotch curve on the left. Both therefore use `origSide: 'R'`; with `'L'` the
     back piece is crossed with the front.
   * `wrap.lift` (leg mode): a flat trouser top edge slopes (front and back dip by 2-6 cm at the centre seams). The rows
     of a leg piece are measured on a lifted outline whose top edge is level, so the slope no longer collapses the top rows
     (every point of a sloping edge is the last point of its row when measured on the flat outline). Only the row extents
     use the lift; the cloth itself stays unstretched.
   * `wrap.tilt` (band): the waistband is one level ring in the pattern, but the trousers' top edge is not level on the
     body. The band's lower edge follows the upper envelope of the trousers' top edge per polar angle around the body
     (72 bins), so the band tilts naturally (higher at the back or the front as the pattern puts it).
   * Band seams: the band's upper long edge (the fold) is never sewn; the trousers' top-edge chains are sewn to its lower
     edge end to end in the order they meet it, each taking the stretch as long as it is in the pattern. The start pose only
     decides *which* chains and in which order. The band is cut on the fold, so one finished layer (half the cut width) is
     modelled (`folded: true` in the recipe; the pencil skirt's band is modelled the same way, 3 cm finished). Chains of at least
     1 cm count as top edge (`minTop`); a sliver shorter than 4% of the band chain keeps its share of the band but is not sewn
     (seam intervals merge below 3% of a chain, which produced a 1 cm against 11.4 cm seam and a 34 mm gap before the rule).
   * Judging trousers while walking: compare the band's *top edge with the posed waist landmark* (`frames[f].body` in the dump,
     `PIECESTAT` in `tools/walk_sim.js`), not with the standing waist; the walking body drops a few centimetres, which once looked like
     "pants creep 6 to 13 cm down".

`makeWrapRows` (`shared.js`) is the row-wise wrap that all of these use. Its `wrap.mode` values:

| mode | Used by | What it does |
| --- | --- | --- |
| `torso` | no caller found in the repository | each row laid on the trunk section at its height; the row width spread over `span` degrees of the section by fraction |
| `leg` | `draft.pants`, FreeSewing chinos | tubes below the crotch, half the pelvis above; blended between the two over a 10 cm band centred on the crotch. `wrap.lift` levels a sloping top edge for the row extents |
| `arc` | `draft.tank` | pattern x = arc length on the body section (no stretching) |
| `arm` | sleeves (`draft.shirt`, FreeSewing sleeves) | tube round the arm axis from the shoulder and elbow joints. Experimental, off by default (`opt.sleeveCap`): `wrap.cap` gives the cap rows the underarm row's angular scale and `sTop` is calibrated to the armholes (`docs/TEE_NOTES.md`) |
| `fit` | FreeSewing body parts and band | pattern x = arc length, on a section inflated to the girth the whole garment covers at that height. A band with `wrap.tilt` follows the trousers' top edge |

Anything without `wrap.mode` uses the envelope wrap (`FL.makeWrap`, the first pattern format).

## Browser application (failoring.html)

One self-contained file. Open it in a current Chrome / Edge / Firefox / Safari; no server, no network. Rebuild with
`node app/build.js` (needs `wasm/failoring.wasm`, `tools/build_wasm.sh` makes it). Current build in the worktree:
**2.24 MB** (three.js r149, the **731 KB** WebAssembly build of the core, the body asset, the worker, fabrics).

```
cutting table (2D canvas)  <->  App state (pieces, seams, fabric, body)  ->  Web Worker { wasm cloth core }
3D view (three.js)         <-  streamed frames { positions, body } <-------'
```

* **Cutting table** (`editor2d.js`): pan/zoom, drag pieces, drag corners (5 mm snap), double-click an edge to add a corner,
  Delete removes a corner or piece, Ctrl+Z / Ctrl+Shift+Z undo/redo. **Sew** tool: click an edge, then the edge it joins; the seam
  list shows both lengths and the ease. Grain line and hatch show the warp direction.
* **3D view** (`view3d.js`): the mannequin mesh (see "Mannequin, collider and walking"), cutting-table mat, subdivided
  cloth mesh with per-piece tint, sew-lines between matched seam vertices.
* **Assembly timeline** (T from 0 to 4; the scrubber, stage buttons, play, step and speed control it):
  1. pieces lie flat on the table; 2. staggered, each piece lifts, rotates *rigidly* (quaternion slerp) and stands upright around the
  body, then bends onto the wrap surface; 3. sew-lines appear and tighten; 4. the recorded drape frames play. Stitch lines are faint guides
  while pieces move and solid during stage 3.
* **Live editing**: any edit (corner, grain, placement, seam, fabric, body, mesh quality) cancels the running job and starts a new one
  after a 120-260 ms debounce. The preview follows the newest frame, so it is low frame rate but live. The timing figures
  (65-200 ms per frame at 400-700 vertices) were measured on this container before the mesh body; they were not re-measured.
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
  indices are remapped. Drafted pieces also carry `wrap.mode` (see "Garment sources"). The wrap is only the *starting pose* of the
  simulation; the solver finds the final drape.
* **Photo shoot** (`shoot.js`): seven curated shots (full length, three-quarter, profile, low-angle hero, waist/seam detail, back
  three-quarter, hem detail) of the latest simulated frame. Long lenses (fov 17-30 degrees), a key light placed relative to each camera,
  fill and two rims, a glossy runway, then a 2D composite: gradient backdrop with bokeh discs, warm/cool grade, vignette, film grain,
  caption. Output is PNG (800x1000, 1200x1500 or 1600x2000). With Motion set to Walking, the shots catch different moments of the gait cycle.

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
  `godot/demo.gd` is untested. The wrapper binds only analytic colliders, not the mesh collider.
* A garment wedged on a convex body keeps jittering at ~1e-4 J (about one line-search failure per frame)
  instead of reaching exactly zero kinetic energy; positions are stable to ~1 mm.
* Self-collision is vertex-triangle only (no edge-edge) - thin tangles can pass. Measured 2026-10-14: the
  `test_garment` scene (default mode, which passes) has 87 exactly intersecting triangle pairs at frame 150 (re-run in a
  scratch probe with the same setup; the counter skips pairs sharing a vertex or a seam link), most of them within one panel and
  far from the seams; with `barrierContact` (K5, off by default) the same kind of scene has none (`test_skin_barrier`).

Browser app and mannequin:

* **Real GPU and Safari not tested.**
* **Painter's renderer speckle is not a real intersection.**
* **Patterns are not graded.** Plans and drafts are not scaled to the chosen body. Plus and curvy are too small for the
  80 / 104 cm skirts.
* **FreeSewing import is limited to the tee, the pencil skirt and the chinos** (Teagan, Penelope, Charlie; the Aaron
  a-shirt recipe exists in `fsimport.js` but has no pre-drafted output).
* **Chinos**: no pocket bag (the slant pocket is sewn shut to the back's upper side edge), no fly, belt loops or hem
  turn-up; the dart notches stay open. On the plus preset the cloth is squeezed (about 3 mm into the body) and does not
  settle in 70 frames.
* **Tee neckline and sleeve slit have issues.**
* The walk is gait keyframes, not captured motion, and has not been checked over long runs.
* Attribution: the FreeSewing files are MIT-licensed and `THIRD_PARTY.md` does not yet list FreeSewing; the
  MakeHuman row there names `LICENSE.ASSETS.md`, while `convert_makehuman.py` cites `LICENSE.md` section C.
  Both need checking against the upstream repository before release.

## Performance ideas not yet done (from the 2025-26 papers)

1. Static solve: the preview only needs the settled drape. Ramp gravity in, run Newton to equilibrium, warm-start from the previous
   result after an edit (small edits move the answer little). Expected to replace 36-60 timesteps by a handful of solves. Unmeasured.
2. Distance-grid collider: **done for the mannequin** (signed-distance grid of a closed mesh, see "Collider"). Still open:
   the bake costs 185-390 ms in wasm (`docs/WIP_HANDOFF.md`), which is paid again on every walk frame. Lipschitz pruning
   (Barbier et al., CGF 2025) is the paper's method for baking a detailed primitive-based body quickly; its use for the
   mesh grid is not assessed here.
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
4. **Pattern import**: FreeSewing parts are imported (tee, pencil skirt, chinos), headless only. Next: put the importer
   in the native build, and add SVG/DXF/PDF (AAMA/ASTM-DXF). Free pattern sources and licences must be
   checked per pattern before bundling anything.
5. **Bodies**: the MakeHuman CC0 mannequin with measurement sliders is built (see "Mannequin, collider and walking").
   Next: expose the mesh collider to the Godot build, grade patterns to the body, and check the measured residuals and
   bake times again. MB-Lab (AGPL) and SMPL-X (research / non-commercial licence - likely unusable for a product) were
   considered and not used. Blender for authoring body shapes and exporting measurement-driven morph targets remains an option.
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
