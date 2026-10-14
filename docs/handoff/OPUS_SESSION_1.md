# Opus session 1 - running log

Branch `opus/session-1`, from main @ 842609f. Machine: Linux container, 2 cores, Release build
(`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2`). Times below are wall / user seconds,
one binary at a time unless stated.

## Phase 0: sanity (2026-10-14)

### Build

Configure and build of main succeed; 0 warnings in the build log.

### Every test binary, run once (main @ 842609f, nothing changed)

CMake registers 9 test binaries (the ARCHITECTURE layout line lists only 5). All pass.

| Binary | Result | Wall s | User s | Key numbers |
| --- | --- | --- | --- | --- |
| test_basics | ok | 0.27 | 0.22 | hinge gradient vs FD 1.02e-10; estimator vs paper 0.57 %; supernodal Cholesky 2.2x, diff 2.8e-16 |
| test_cantilever | ok | 3.75 | 3.63 | hex 7 / 3.5 / 2.5 mm: +22.6 / +1.9 / +0.7 %; grid 3.5 mm +16.1 %; 0 line-search failures; grain ratio 1.86 |
| test_sdf_grid | ok | 9.09 | 8.92 | ellipsoid grid vs analytic within 5 mm: 0.304 mm max; 0 wrong signs of 4000 |
| test_barrier | ok | 0.87 | 0.84 | CCD conservative on all known-TOI cases; BVH 0 missing pairs |
| test_passthrough | ok | 0.13 | 0.13 | vertex-tri: 5 intersecting frames; barrier: 0 |
| test_layers | ok | 6.86 | 6.76 | barrier: min layer gap 0.688 mm, 0 intersecting frames, 56.7 ms/frame, 7.21 Newton/substep |
| test_garment | ok | 40.3 | 39.7 | 672 verts, 153 frames, KE 9.3e-07 J, seam gap 2.35 mm, clearance 1.65 mm, hem 156.7 vs predicted 161.2 mm |
| test_soft_skin | ok | 131.3 | 129.3 | ring delta 2.9325 vs 2.9086 mm (0.82 %); standing soft 0.0001 mm/frame, KE 6.9e-13, ls-fail 0 |
| test_drape | ok | 306.4 | 301.4 | table below |

Notes:
- test_cantilever's 7 mm hex case is +22.6 % (its tolerance is 30 %); ARCHITECTURE's "within 1-2 %" holds for 3.5 and 2.5 mm only.
- test_soft_skin `disabled` part still prints "with the layer on the drop differs by up to 1.66e-01 m" (K1 open item, not
  investigated in this session).
- test_passthrough and test_layers print "lowest B point" / layer gap of -13.7 m and -13.2 m for the vertex-tri mode:
  the sheet falls through and keeps falling; those values are not meaningful (known from the K5 notes).

### test_drape (all nine presets, 10.0 mm mesh each)

| Fabric | DC % | Frames | KE (J) | Clearance mm | s |
| --- | --- | --- | --- | --- | --- |
| cotton_poplin | 65.0 | 300 | 3.6e-07 | 1.65 | 42.0 |
| denim_12oz | 80.7 | 214 | 9.3e-09 | 1.90 | 30.9 |
| linen_medium | 81.2 | 300 | 8.1e-08 | 1.70 | 40.5 |
| wool_suiting | 64.8 | 125 | 8.8e-09 | 1.85 | 17.3 |
| silk_charmeuse | 46.6 | 300 | 2.2e-07 | 1.60 | 39.2 |
| polyester_chiffon | 43.1 | 300 | 1.9e-07 | 1.55 | 38.8 |
| canvas_heavy | 92.8 | 179 | 8.4e-09 | 1.90 | 24.9 |
| cotton_jersey | 27.6 | 223 | 8.9e-09 | 1.80 | 29.8 |
| lambskin_leather | 90.9 | 300 | 5.9e-08 | 1.85 | 42.9 |

Matches the user's numbers exactly. Five presets stop at the 300-frame cap (KE above the 1e-8 J early-stop), so their DC
is the frame-300 value. `docs/ARCHITECTURE.md` updated with this table, the mesh size and the date.

### K1 + K5 together (new test `test_skin_barrier`)

Scene: the test_garment tube (cotton poplin, two panels sewn into a ring, pre-wrapped on the ellipsoid's upper dome)
on a 3 cm mesh (308 verts), 4 substeps, 200 frames. Skin k = 2e5 Pa/m, depth 8 mm (test_soft_skin's standing values,
test values, not tissue data). Barrier as test_garment's `FL_BARRIER` switch (barrierContact on, selfCollision off,
default BarrierParams).

| Mode | s | KE end (J) | ls-fail | Newton/substep | Factorisations | Clearance min (end) mm | Self-isect frames (max pairs) | Seam gap mm | Hem mm |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| rigid | 13.5 | 9.17e-08 | 0 | 7.76 | 6205 | 1.64 (1.65) | 182 (98) | 1.99 | 159.6 |
| skin | 10.4 | 9.73e-11 | 4 | 5.64 | 4515 | -3.76 (0.46) | 178 (49) | 0.34 | 153.0 |
| barrier | 31.4 | 1.51e-05 | 0 | 4.99 | 3995 | 1.65 (1.65) | 0 (0) | 0.29 | 156.7 |
| skin + barrier | 29.5 | 1.93e-10 | 1 | 5.21 | 4169 | -3.74 (0.36) | 0 (0) | 0.32 | 154.2 |

Answer: the combination runs, settles (KE 1.9e-10 J), keeps the seams closed and has no self intersections in any of
the 200 frames. "Penetration-free" needs a qualifier: with the skin on, the cloth sinks up to 3.74 mm into the 8 mm soft
layer during the drop, which is what the layer is for; it never reaches the rigid stop, and at the end it sits 0.36 mm
outside the offset surface. Convergence: 1 line-search failure in 200 frames (800 substeps) with both on; the skin
alone gives 4. Pass limits in the test are test_garment's (KE < 5e-4 J, seam gap < 4 mm, penetration < 1 mm, or
< depth + 1 mm with the skin); no existing test tolerance was touched.

Observations to carry into Phase 1 (not acted on):
- Barrier alone ends with KE 1.5e-05 J, about 160x the rigid mode and 1e5x the skin modes. The skin settles the ring far
  better than anything else here (9.7e-11 J).
- The skin cuts Newton iterations per substep from 7.76 to 5.64.

### Finding: the default self contact lets the tube intersect itself

The rigid and skin-only rows above show self intersections in about 90 % of the frames. Checked with a probe
(`tools/dev/probe_isect.cpp`, counts every non-adjacent intersecting pair and says where it is):
- 3 cm tube: frame 0 has 132 crossings, all across the seams (pre-wrap overlap; the counter skips them as seam-linked).
  From frame 20 most counted pairs are within one panel and up to 120 mm from any seam.
- 2 cm tube = the test_garment scene (default mode, a passing test): 87 counted pairs at frame 150, about 15 of them
  across panels; the rest within a panel, up to 172 mm from a seam.
So this is real, not a counting artefact. Where on the tube the folds are was not looked at (no render). Not fixed; it
is the existing "vertex-triangle only" limitation, now with a number, added to ARCHITECTURE "Known limitations".

### Not done / unverified in Phase 0
- wasm and Godot builds (the K5 sources were added to their build files without building): not attempted.
- ctest as one call: not used; binaries run one by one (above).

## Phase 1: K2, the contact jitter (2026-10-14)

### Instrumentation (no behaviour change)
- `SolverParams::contactDiagnostics` (off) fills `ClothSolver::contactDiag()`: Newton iterations and iteration-cap hits,
  held (motion-filtered) vertices and how often the held set changes (between Newton iterations; between substeps),
  in-Newton projections (and how many happen after the first iteration, i.e. the Newton step itself went into the body),
  step kinetic energy after Newton, after the body pass and after self collision. `setNewtonTraceFrame(f)` prints every
  Newton iteration of one frame. Read-only accessors `velocities()`, `heldFlags()`, `collider()`, `vertexMass()`.
- Diagnostic switches, defaults = old behaviour: `bodyFrictionMode` (1 = no body friction), `bodyPushAfterSolve`.
- `test_wedge` (new; `wedge_scenes.hpp` taken unchanged from agent/k2-contact-multipliers; the AL code was NOT taken).
  Scene 0 = test_garment's tube on the ellipsoid; scene 1 = the same tube on two overlapping ellipsoids. 672 vertices,
  4 substeps, 240 frames. Per-frame CSV in `out/`. `test_wedge <scene> <overrides> <frames>` for experiments.
- Check that nothing changed: the default runs reproduce the K2 agent's numbers to the digit (scene 0 KE final 9.69e-8,
  tail mean 7.35e-7; scene 1 tail mean 3.13e-5, max 1.36e-4, min clearance 1.649 mm, max stretch 1.1530).

### Baseline (default solver, last 60 of 240 frames, per substep)

| | Scene 0 (quiet) | Scene 1 (jitter) |
| --- | --- | --- |
| Mean / max KE | 7.35e-7 / 1.19e-6 J | 3.13e-5 / 1.36e-4 J |
| Newton iterations, cap hits | 8.98, 0 of 240 | 12.00, **240 of 240** |
| Line-search failures | 0 | 0 |
| Held vertices; flips between iterations / substeps | 68.2; 23.4 / 0.12 | 71.3; 32.4 / 5.68 |
| Projections in Newton (after 1st iteration) | 81.1 (10.6) | 164.9 (89.5) |
| Step KE after Newton / after self collision | 2.48e-5 / 7.4e-7 J | 6.31e-5 / 3.28e-5 J |

"About one line-search failure per frame" (ARCHITECTURE) does not reproduce in either scene: 0. It does reproduce with
`contactBand = 0` (240 of 240 substeps fail), so the old doc figure probably predates the contact band (my reading,
not checked in history). Friction acted in 2872 vertex-substeps over the run, against ~68,000 held vertex-substeps: the
post-solve friction budget is mu x (penetration left after the solve), which is ~0 because Newton already projected
the vertices, so body friction is nearly absent (see open questions).

### One cause at a time (scene 1, last 60 frames, mean KE; default 3.13e-5 J)

| Change | Mean KE (J) | Cap hits | Line-search failures | Reading |
| --- | --- | --- | --- | --- |
| self collision off | 3.64e-5 | 240/240 | 0 | not the cause |
| friction off (`fric=1`) | 1.70e-5 | 240/240 | 0 | not the cause (friction is ~absent anyway) |
| no push-out after the solve | 1.55e-5 | 240/240 | 0 | not the cause; penetrates 0.36 mm |
| contact band 1 mm (10x) | 2.65e-5 | 240/240 | 0 | not the cause; flips double (47/substep) |
| contact band 0 | 7.65e-5 | 0 | **240/240** | the band protects; nothing is held |
| 30 Newton iterations | 4.50e-7 | 240/240 | 0 | non-convergence drives it; still never reaches 1e-6 m |
| **tension term on** | **2.67e-7** | **0** | 0 | **the cause** |
| tension on + self collision off | 3.10e-7 | 0 | 0 | 6.3 instead of 11 Newton iterations; max stretch 1.03 instead of 1.14 |

Newton trace (frame 200, scene 1, default): every iteration the largest update (1.0-1.8 cm proposed, 1/8 accepted by
the line search, never shrinking) is on hem vertices v9-v13 and v343-v349, which are NOT held and sit 0.5-20 mm off the
body: the hem span that bridges the crease between the two ellipsoids. Where the kinetic energy sits at the end (frame
240): default, 91 % in those free bridging vertices (0.10-0.24 m/s, mostly along the normal); held vertices 9 %. With
the tension term: total 1.4e-7 J, held vertices 4.7e-9 J, the rest in the free top edge 10-14 cm from the body (5 mm/s).

Diagnosis the data supports: the jitter is the free span over the crease fluttering because Newton never converges
there. The Gauss-Newton Hessian drops the tension (geometric) stiffness of a taut span, so Newton has no stiffness
across the yarn and overshoots; the line search cuts the step and the 12-iteration cap is hit every substep, leaving a
residual that becomes velocity. Of the candidate causes in the brief, the one that holds is "the Hessian lacks a
curvature term", but it is the elastic tension term, not the contact curvature. Active-set chatter, the band, friction
after the solve and projection are not supported as the cause (each leaves the jitter within ~2x). The K2 branch's
augmented-Lagrangian multipliers target a cause the data does not support; not taken.

### Fix
`SolverParams::tensionStiffness` already exists (the PSD tension part of the stretch Hessian, kept only where the yarn
is stretched), off in the core, on in the app (`worker.js`). No new solver code is needed; it stays off by default.
Opt-in switches `FL_TENSION=1` added to test_garment and test_skin_barrier (default runs unchanged).

| | Tension off | Tension on |
| --- | --- | --- |
| Scene 1, 240 frames: mean / max KE last 60 frames | 3.13e-5 / 1.36e-4 J | 2.67e-7 / 7.7e-7 J |
| Scene 1, 480 frames: mean KE last 60 frames | 2.38e-5 J (cap hit every substep) | **2.66e-9 J** (final 1.2e-9, still falling) |
| Scene 0, 240 frames: mean KE last 60 frames | 7.35e-7 J | 6.57e-7 J (33 cap hits of 240, see below) |
| test_garment | pass, 153 frames, KE 9.3e-7, seam gap 2.35 mm, hem 156.7 mm | pass, 177 frames, KE 9.9e-7, seam gap 0.49 mm, hem 157.0 mm |
| test_garment + barrier | pass (K5 notes) | pass, 213 frames, KE 9.8e-7 |
| 3 cm tube, barrier only (test_skin_barrier) | 1.51e-5 J | 5.95e-6 J (only 2.5x: the barrier's residual is mostly something else, not looked at) |

Verdict: in the wedge scene the jitter is **eliminated** with the tension term, in the sense that the kinetic energy
keeps decaying (2.7e-9 J at 8 s, no floor seen) instead of sitting at 2-3e-5 J; and Newton converges in every substep.
It is not eliminated by default, because the option stays off.

`test_wedge` default run now asserts: scene 1 with tension mean tail KE < 1e-6 J and at least 100x below the same run
with tension off, 0 cap hits and 0 line-search failures in the tail; scene 0 with tension mean tail KE < 5e-6 J; safety
checks (finite, penetration < 1 mm, seam gap < 4 mm, test_garment's limits) on all three runs.

### Full suite on the Phase 1 tree (all default runs unchanged)
All 11 binaries pass: test_basics 0.24 s, test_cantilever 3.2 s, test_sdf_grid 9.0 s, test_barrier 0.83 s,
test_passthrough 0.09 s, test_layers 6.5 s, test_garment 39.2 s, test_soft_skin 121.8 s, test_skin_barrier 75.5 s,
test_wedge 258.1 s (new: scene 1 off 3.13e-5 J vs tension 2.67e-7 J, 117x; scene 0 tension 6.57e-7 J), test_drape
302.3 s. test_garment and test_skin_barrier output is identical to Phase 0 (diffed, wall times aside); the test_drape
table is identical to the Phase 0 table. `FL_TENSION=1 test_garment`: pass (177 frames, KE 9.9e-7 J, seam gap
0.49 mm). No test tolerance changed; newtonTolerance untouched (1e-6).

### Side findings (not fixed)
- Self collision as a hidden damper: in scene 0, turning self collision off (with tension on) exposes 2e-5 J; the KE
  is in FREE cloth 5-7 cm off the body (upper back rows, ~6 cm/s): the unsupported top of the tube folding in on itself.
  With self collision on, the post-solve vertex-triangle projection cancels 97-99.99 % of Newton's per-substep motion in
  every scene, even when settled (scene 1 tension 480 frames: step KE after Newton 2.7e-5 J, after self collision
  2.7e-9 J). Newton and the projection undo each other every substep. Linked to the self intersections found in Phase 0.
- Scene 0 with tension: Newton converges linearly (x0.42 per iteration, full steps), needs 11-12 iterations, so 33 of
  240 tail substeps hit the cap. Slowest vertex is free cloth in the collapsed top of the tube (v647, 3.9 cm off the
  body), not contact. Plausible cause (unverified): the compressive part of the stretch Hessian, which the
  Gauss-Newton model drops on purpose.
- Within one substep a group of ~10 held vertices is released at iteration 1 and taken back over the next iterations
  (held 123 -> 113 -> 118 -> 121 ...), with the line search cutting those steps to 1/8-1/32. It slows Newton but did not
  cause jitter here.

## Between phases: the user's decisions (2026-10-14, 11:04-11:23)
The user answered yes to making the tension term the default, yes to real body friction, and then lifted the brief's
restrictions ("I need my clothing to look good ... I don't make any restrictions"). Working rule from then on: optimise
for garments that look right; keep tests where they measure something visible; change a test only with a stated reason.

### Tension term as the default: tried, and it breaks the cantilever
With `tensionStiffness = true` in the core, test_cantilever fails 5 of 5 checks (hex 7 / 3.5 / 2.5 mm: +30.6 / +39.8 /
+95.7 %, grid +67.4 %, grain ratio 1.29) and runs to the 600-frame cap. Probe (3.5 mm strip): Newton hits the
12-iteration cap in every frame, full steps accepted, the update never shrinks (0.5-1 mm), the tip wanders 7.0-7.4 mm
instead of 5.15 mm. Reading: in a bending-dominated, nearly unstretched strip the tension-only geometric term (kept for
C > 0, dropped for C < 0) makes the Newton model much stiffer than the energy in the bending mode. This matters for
clothing: it is "does a stiff fabric hold its shape". The app runs with this term on (worker.js), so stiff cloth in the
app probably sags too much (unverified in the app).
Tried and dropped (code removed, numbers kept):
- Tension term for both signs (exact stretch Hessian, fall back if the factorisation fails): 3.5 mm strip settles at
  5.34 mm (wrong), 2.5 mm at 7.84 mm; cap hit every frame.
- Exact elastic Hessian (stretch both signs, exact shear, bending theta*Hess(theta) by central differences of the
  analytic hinge gradient): strip 6.5 / 7.9 mm, cap hit every frame; the trace shows a two-cycle with 1 cm steps on
  alternating vertex groups (a near-singular soft mode). Not pursued.

### hessianAuto (new default)
Each failure has a signature: Gauss-Newton too soft = the line search keeps cutting the step (wedge); tension term too
stiff = full steps that do not shrink (strip). `SolverParams::hessianAuto` (default true; overrides tensionStiffness)
starts each substep with the model that last converged (Gauss-Newton at first), switches once per substep after three
cut steps (GN) or three full steps without halving (tension), with a fresh iteration budget, keeps the new model only if
it converged, and on a line-search failure retries from the same point with the other model (counted as recovered).
Thresholds found by measurement, in this order: a "not halved over two iterations" rule fired falsely on Gauss-Newton's
non-monotone first iterations; restricting to n >= 4 fixed the strip but the wedge ping-ponged (780 switches); the
signature rule plus "keep only if converged" fixed both.

| Scene | Gauss-Newton (old core) | Tension term (app) | hessianAuto |
| --- | --- | --- | --- |
| Wedge scene 1, mean tail KE | 3.1e-5 J, cap every substep | 2.7e-7 J | 1.9e-7 J, 0 cap hits, 77 switches in 960 substeps |
| Wedge scene 0, mean tail KE | 7.4e-7 J | 6.6e-7 J | 2.9e-7 J (2 of 240 tail substeps at the cap) |
| Cantilever 7 / 3.5 / 2.5 mm | 5.287 / 5.152 / 5.318 mm, 6 it | 5.63 / 7.06 / 10.34 mm, cap | 5.287 / 5.152 / 5.318 mm, 6 it, 2 switches |
| Soft-skin standing tube | ls-fail 0 | (the test's own setting) | ls-fail 0 after the recovery rule (2 before it) |
| Drape disc, 1 substep | cap ~every frame | cap ~every frame | cap ~every frame (needs substeps, below) |

### Body friction inside Newton (bodyFrictionMode 2; NOT the default)
New `cloth_friction.cpp`: smoothed Coulomb potential mu*lambda*f0(|u_t|) (IPC's C1 smoothing), normal force and normal
lagged from the previous substep, gradient + PSD Hessian in the Newton system, the same energy in the line search,
applied only to vertices touching the body in the current iteration. New `test_friction` (cotton patch on an incline,
0.5 s, prediction with the material's velocity damping):

| Incline | Old mode (post-solve) | Mode 2 | Coulomb |
| --- | --- | --- | --- |
| 10 deg (sticks) | 194.9 mm | 0.18-0.27 mm | 0 |
| 15 deg (sticks) | 290.5 mm | 0.54 mm | 0 |
| 25 deg | 474.4 mm | 126.2 mm | 117.3 mm |
| 30 deg | 561.3 mm | 228.3 mm (+4.2 %) | 219.0 mm |
| 40 deg | 721.5 mm | 428.1 mm | 416.8 mm |

The old body friction is effectively zero (it slides like frictionless cloth). Mode 2 runs a near-constant 9-11 mm ahead
of Coulomb when sliding (first substeps have no normal force yet; ~1 % comes from the discrete damping, the old mode
shows the same 1 % against the frictionless prediction). Wedge with mode 2: scene 1 tail KE 2.1e-10 J (no flips, no
cap hits), scene 0 4.3e-8 J with the hem held at 148.5 mm (below the frictionless lodging height; friction holds the
ring where it slid to). test_garment passes with it (hem 148.2 mm, KE 3.6e-8 J).
Why it is not the default: test_soft_skin's standing scene. Slip velocity 1e-3 m/s: the soft layer creeps 0.0146
mm/frame (fails < 1e-6 m). 1e-5: soft passes barely (0.0009 mm/frame) but the RIGID tube shows growing stick-slip
bursts (0.009 mm/frame at frame 100, 0.12-0.17 mm at frame 300-400). 1e-4: both bad. Restricting friction to current
contacts did not change it. Reading (unverified): the rigid hold's normal-force estimate is noisy (vertices toggle
held/free), and lagging it into friction gives stick-slip; IPC takes lambda from a smooth barrier, and with the soft
skin (smooth k*delta) it behaves. Next step: friction with the skin or barrier as the normal-force source.
Default stays 0; slip velocity 1e-5 m/s (best for the skin pairing).

## Phase 2: S1, static equilibrium solve (2026-10-14)

### Merge
`agent/s1-static-settle` merged (conflict only in the CMake test list). Reproduced on the old defaults: silk static 38.0
vs dynamic 46.6, cotton 60.5 vs 65.0, denim 80.0 vs 80.7, 10-24x fewer factorisations. Exactly the branch's numbers.
Added: a guard (`StaticResult::unsupported` with the K1 skin or K5 barrier on; it would solve a different problem).

### The drape "reference" was not converged
test_drape's dynamic loop (1 substep, 12 Newton iterations) hits the iteration cap in practically every frame for silk,
cotton and denim, with or without the tension term (last update 0.06-0.5 mm, newtonTolerance 1e-6 m). With 40
iterations, silk drifts 45 -> 42 % and is still moving at frame 300. With 4 substeps Newton converges (8-14 cap hits in
200 substeps after frame 50) and the run settles by itself:

| Fabric | Static (cold) | Dynamic, 4 substeps (converged) | test_drape (1 substep) |
| --- | --- | --- | --- |
| silk_charmeuse | 38.0 %, 5 folds, E -5.03e-4 J | 39.5 %, 8 folds, E -5.72e-4 J (149 frames) | 46.6 % |
| cotton_poplin | 60.5 %, 5 folds, E -6.48e-4 J | 62.3 %, 7 folds, E -6.36e-4 J (234 frames) | 65.0 % |
| denim_12oz | 80.0 %, 5 folds, E -1.2865e-3 J | 80.7 %, 6 folds, E -1.2851e-3 J (246 frames) | 80.7 % |

(Measured with the hessianAuto core and old friction; folds = local maxima of the outline radius, 72 bins, >= 3 mm
prominence; scratch probe.) Against converged dynamics the static drape coefficient is within 2 points for all three
(the S1 branch's own criterion). The "different metastable equilibrium" reading was mostly an unconverged reference.
What remains: the static shape has fewer folds (5 vs 6-8) and for silk a 13 % higher energy, so it is a shallower
equilibrium; the fold pattern is not reliable. Random kicks (2 mm, keep if lower energy) lower the silk energy step by
step (-5.31e-4 -> -5.49e-4 J over 6 kicks, 3 kept, 1509 iterations): a flat landscape with many nearby states.
The 9-fabric table in ARCHITECTURE comes from test_drape's single-substep runs; those values are not converged.
With friction mode 2 the converged silk drape moves to 41.0 %, 3 points from the frictionless static solve.

### Static self collision off
On test_garment's tube the static solve did not converge (residual 1.2e4, hem 110 mm, energy 1.56 J vs 0.09 J): the
vertex-triangle self-collision projection after each step is outside the energy the line search checks, and at static
step sizes it scrambles the ring. Without it the solve converges (538 iterations, 4.3 s, hem 160.9 mm). Now
`StaticParams::selfCollision = false` by default (the static solve ignores self contact), maxIterations 400 -> 1000.

### test_static (now four parts; the drape part has a converged 4-substep dynamic reference)
| Part | Result (on the final defaults: see the suite below) |
| --- | --- |
| cantilever, 4 meshes | static = dynamic to 0.000 mm, beam-theory error as test_cantilever (22.6 / 1.9 / 0.7 / 16.1 %), 55-78 Newton it vs 320-386 |
| garment tube | static hem 160.9 mm vs analytic lodging height 161.2 mm; energy 0.0828 J below the dynamic settle's 0.0909 J (dynamic hem 157.5 mm, held 3.7 mm low); 538 it, 4.3 s vs 49.5 s cpu |
| warm start after +10 % bending | warm 7 it vs cold 70 it, same shape (0.000 mm), no new symbolic analysis |
| drape disc | within 2 points of converged dynamics (silk 1.5, cotton 1.8, denim 0.7), 20-98x fewer factorisations |

Garment check changed from "within 2 mm of the dynamic hem" (S1's draft) to "within 2 mm of the analytic lodging height
and energy no higher than the dynamic settle", because the dynamic settle is not the reference there (lower hem, higher
energy).

### When the static solve can be trusted (the deliverable)
- Unique equilibria (bending-dominated pieces, a ring at its girth): yes. Cantilever = dynamic to 0.000 mm; the tube
  reaches the analytic equilibrium, at lower energy than the dynamic settle.
- After a small, topology-preserving edit, warm started: yes; ~10x fewer iterations than cold, same answer.
- Drape of fold-dominated cloth: the drape coefficient within ~2 points of converged dynamics, the fold pattern not
  (fewer folds, a shallower state). Use it as a fast preview (20-98x fewer factorisations on the drape disc); the
  dynamic run (with enough substeps to converge) is the reference.
- Not trusted / not modelled: anything friction holds in place (the static solve finds the frictionless equilibrium;
  with friction the dynamic drape differs by 3 points and the tube by 12 mm), self contact (ignored), K1 skin and K5
  barrier (refused with `unsupported`).
- Known bug, not fixed: `checkStaticStability` with a 10 mm perturbation diverges (the cloth leaves the pedestal,
  ~200 m); 0.5-5 mm perturbations work.

### Final suite on the session's defaults (hessianAuto on, bodyFrictionMode 0, static self collision off)
All 14 binaries pass. test_basics 0.2 s, test_cantilever 3.7 s, test_sdf_grid 7.9 s, test_barrier 0.8 s,
test_passthrough 0.1 s, test_layers 6.7 s, test_garment 44.3 s (KE 8.1e-7 J, seam gap 0.47 mm (was 2.35), hem
157.5 mm), test_soft_skin 115.7 s, test_skin_barrier 87.0 s, test_wedge 232.8 s (old solver 3.13e-5 J vs default
1.85e-7 J, 169x; scene 0 2.91e-7 J), test_static 447.5 s, test_friction 0.5 s, test_drape 339.4 s (table in
ARCHITECTURE: silk 42.3, cotton 63.9, chiffon 43.6, jersey 27.7, the rest unchanged).
test_static's drape part: static vs converged dynamic silk 38.0 / 39.5, cotton 60.5 / 62.3, denim 80.0 / 80.7, with
20x / 74x / 98x fewer factorisations (4.5 / 1.9 / 1.2 s vs 90 / 143 / 142 s cpu).
No existing test tolerance was changed. newtonTolerance untouched (1e-6). Changed checks, all in tests written in this
session or never passing before: test_wedge scene 0 cap hits (<= 5 % of tail substeps instead of 0), test_static's
garment check (analytic lodging height instead of the dynamic hem) and drape reference (4 substeps instead of 1).

## Phase 3: branch audit for the Godot editor (2026-10-14, afternoon)
The user's request: make sure main has all the functionality of the "editor vX" branches; the focus is the Godot
frontend, not the browser.

### What exists
No pull requests on GitHub (open or closed): the work is in 35 branches. Commits not in main (origin/main @ 842609f):
- Editor branches (godot/editor, godot/editor-combined, godot/editor-v2..v6, and the 14 */godot-editor branches:
  camera, dogfood, fix-design, fix-robust, fix-vanish, library, look, perf, perf2, polish, render, showcase, skirt,
  table, table2, thread, ui-scale), wip/godot-native-agent, agent/k1, agent/k5: **0 commits each** (all merged).
- mac/perf-2026-10-12: 1 commit, Godot-side (walk_demo.gd per-frame stats, startup timing, FL_SHOOT screenshots,
  visible body; Mac timing notes in NATIVE_NOTES.md). **Not in main: merged here.**
- wip/fit-2026-10-13: 1 WIP commit, browser fit work; main has the finished version (599529e). Superseded, not merged.
- agent/k2 (contact multipliers, blows up), agent/k4 (lazy SDF, partial), agent/t1 (fabric bench, does not
  converge): unfinished core experiments, not editor functionality. Not merged (see their handoff notes).
- agent/s1: merged earlier in this session.

### Merged is not the same as present: line-level check
For every editor branch: files it has that main lacks: none. Lines in branch tips absent from main: 454, of which
400 were removed by ordinary later commits (deliberate rewrites) and 54 inside merge commits. Those 54 checked by hand:
all features are present in main in another form (seam labels with edge numbers, edge-length entry, field tooltips,
sewing note, History + Showcase tabs, solver status), except one: **angle-weighted cloth normals**
(render/godot-editor f836da2) became area-weighted when the normals moved to C++ in the perf2 -> editor-v6 merge
(dae3af8). Restored in FailoringSim.get_display_normals and Look.cloth_normals.

### Godot tests, run for the first time in this session (Godot 4.3 headless, Linux; library built from source)
Baseline = main's sources (library built from them), branch = this branch. 23 checks (22 scenes + render_check).
| Check | main | branch (before fixes) |
| --- | --- | --- |
| 20 others | pass | pass |
| outfit_check | FAIL ("showing the tee again restores both garments") | same |
| wrap_check | FAIL (max error 268 mm against the browser's wrap) | same |
| skirt_cover_check | pass | FAIL: penelope/petite back coverage 0.673 (my hessianAuto change) |

Findings and fixes:
- outfit_check: a stale literal (8 pieces; the chinos gained a waistband, the outfit has 9). Test fixed to count.
- skirt_cover_check: the editor sets tension_stiffness = 1, which hessianAuto overrides; with the editor's 8 Newton
  iterations the "keep the switched model only if it converged" rule kept throwing the tension model away. New rule:
  keep it unless it shows its own stall signature. penelope/petite back coverage 0.995 (main 1.000); test_wedge scene 1
  1.28e-7 J, 0 cap hits; cantilever unchanged.
- wrap_check: two causes. (1) The Godot assets were stale: only the "average" preset had been re-exported after the
  browser fit work, so for petite, tall, curvy, athletic and plus the editor had the OLD garment plans (no chinos
  waistband, old skirt/tee drafts), the old gait (walk.bin), and for curvy/plus the old body and skeleton (arms before
  the arm-clearance fix); body_ref.json, draft_ref.json and wrap_ref.json were stale too. All regenerated with
  tools/export_godot_assets.js, export_godot_plans.js, export_godot_body.js (exporters are deterministic: average came
  out byte-identical). (2) wrap.gd lacked the browser's waistband `tilt` (the band follows the trousers' sloping top
  edge); ported (_fit_at, _tilt_at). Average preset: max error 0.000000 mm.
  Then also `lift` and `vcap` (rows of sloping trouser top edges measured on the lifted outline): the 3x3 reference
  grid did not sample those rows, so the exporter now adds a 4x5 grid over the upper rows of lift/vcap pieces (151
  references per preset); without the lift port that grid fails by 72 mm. All six presets: 0.000000 mm.
  Not ported: the experimental sleeve cap (`cap`, off by default in the browser).
- body_check (after the regenerated body_ref.json): curvy and plus failed (stand 26 / 81 mm, walk 31 / 98 mm): the
  Godot body port lacked the browser's arm clearance (gait.js G.stand: raise the arms until they hang 2 cm clear of the
  torso hulls). Ported to body_gen.gd (stand() returns armAbduct); the editor's walk and body_check pass it to pose().
  6/6 presets, stand 0.001 mm, walk 0.13-0.17 mm.
- Other browser modules with Godot ports checked for later changes: draft.js changed after draft.gd (seat, back
  widths, cross seam measures), but those only feed the offline FreeSewing drafting; draft_check matches all six presets.

### Final runs (this branch, 336862b + docs)
- C++ core: 14/14 pass (test_static 635 s, test_drape 343 s, test_wedge 349 s on a busy 2-core machine).
- Godot (headless 4.3, library built from this branch): 23/23 pass (main: 21/23, outfit_check and wrap_check failed).
  skirt_cover_check: every case ok or "known, not gated" as on main; penelope/petite walk_min 1.000.
- The rebuilt godot/bin/libfailoring.so is not committed (tools/build_gdext.sh: a local build product). Run
  tools/build_gdext.sh before opening the editor; the committed .so/.dylib predate this session.

## Phase 4: K2 contact multipliers, finished (2026-10-14, evening)
The user asked to continue agent/k2-contact-multipliers and merge it once good. Its original purpose (the wedge jitter)
was already met by hessianAuto (Phase 1/2), so "good" was defined by the K2 handoff's own criteria: option off by
default, the wedge test off vs on with the on-case no worse on penetration and stretch, test_garment passing with it on,
everything else unchanged.

### Why the branch's version blew up (read from its code, then confirmed)
- The penalty rho was 1000 x the vertex's elastic stiffness (1e6-1e7 N/m): one micrometre of residual penetration after
  an unconverged solve became a ~10 N multiplier on a vertex that weighs ~1e-4 N; a large multiplier keeps pushing until
  g = lambda/rho (it "reaches out"). The recorded failure: lambda 2e8 N, 54 mm penetration.
- It was a separate copy of the whole Newton loop (stepImplicitAL), predating the skin, barrier, hessianAuto and friction.

### Re-implementation (not a port): cloth_contact_al.cpp + the Newton loop in cloth_implicit.cpp
`SolverParams::contactAL` (off), rho_i = contactALStiffness * m_i/h^2 (inertia-scaled), multipliers warm-started across
substeps, PHR energy term in the unprojected line search, an outer loop (solve, lambda <- max(0, lambda - rho g),
repeat while deeper than contactALTolerance = 10 um, at most 4 solves), no hold, no push-out (self-collision pushes are
projected back). Friction mode 2 takes the multiplier as its normal force. Requires skin and barrier off.
Bugs found on the way (each measured):
1. refactor() adds a 1e5 x diagonal normal lock for every "contact-active" vertex (the hold); in AL mode that froze the
   very motion the multiplier pushes for: lambda grew 17 N per frame on the incline. Excluded in AL mode.
2. A hysteresis band that kept rho n n^T in the Newton model beyond the contact switch made a vertex leaving the body
   crawl (each step ~1/(1+kappa) of the needed one): cap hit in every substep. Removed (exact Gauss-Newton model).
3. kappa = contactALStiffness: 100 lets the contact oscillate (8-14x the kinetic energy of the default), 10000 makes
   Newton hit its cap in half the substeps (36 line-search failures in the tail); 1000 is the default.

### Results (kappa 1000)
| test_wedge, last 60 frames | Scene 1 default | Scene 1 contactAL | Scene 0 default | Scene 0 contactAL |
| --- | --- | --- | --- | --- |
| Mean / max KE | 1.28e-7 / 4.76e-7 J | 1.13e-7 / 2.88e-7 J | 8.7e-7 / 2.2e-6 J | 1.48e-6 / 2.7e-6 J |
| Max stretch (final) | 1.178 (1.075) | 1.170 (1.095) | 1.102 (1.041) | 1.117 (1.021) |
| Newton iterations per substep, cap hits | 9.23, 0 | 9.02, 0 | 9.80, 0 | 10.31, 0 |
| Min clearance / deepest after a solve | 1.648 mm / - | 1.634 mm / 0.009 mm | 1.650 mm / - | 1.641 mm / 0.010 mm |
| Solves per substep | 1 | 1.02 | 1 | 1.02 |
Plainly: K2 does not improve the look over today's default (scene 1 slightly better, scene 0 slightly worse); it is
equal-not-better on jitter. What it adds: contact forces that are exact (incline: sum of multipliers / normal load =
1.00000 at 15 and 30 degrees, while sticking and sliding), and contact without projection. That is the basis for
pressure / fit readouts. test_garment with FL_AL=1 (and friction mode 2): pass (KE 4.7e-8 J, seam gap 1.46 mm, no
penetration, hem 147.9 mm: friction holds the ring where it slid to).

### Friction stick-slip is not a K2 problem
Hypothesis from Phase 2 (the rigid hold's noisy normal force causes friction stick-slip) is wrong: the narrow-waist tube
(test_soft_skin standing scene, rigid) shows the same bursts with the multipliers as with the hold (0.10 mm/frame in the
last 10 frames, 0.17 at frame 300); without friction both creep ~0.04 mm/frame. The cause is the friction smoothing
itself: slip velocity 1e-3 m/s creeps (soft test 0.0146 mm/frame = that speed), 1e-5 m/s makes the friction term so
stiff (~1e4 N/m per vertex) that stick and slip alternate. Friction stays opt-in; a better friction model (e.g. an
exact stick constraint with a slip release, or a time-filtered lambda) is the next step, not part of K2.

### Final runs (branch opus/k2-multipliers, merged to main)
C++: 13/13 test binaries pass (test_wedge now also asserts the contactAL run: 1.13e-7 J vs old solver 3.13e-5 J;
test_friction asserts the multipliers carry the normal load). Godot headless 4.3, library rebuilt: 23/23 pass
(contactAL is off by default, so the editor is unchanged).

### Not done
- contactAL with the K1 skin or K5 barrier (refused: the flag is ignored there).
- The static solve does not use the multipliers.
- Pressure readouts from the multipliers (vertexContactPressure still reads the hold's force estimate in this mode
  via rigidForce_, which is set to the multiplier force; not separately tested).

## Open questions
- Should `test_garment` assert zero self intersections? It would fail today in default mode (87 pairs). Not changed.
- The app (worker.js) runs Standard quality with 2 substeps and at most 8 Newton iterations, plus the tension term. The
  measurements here say that does not converge on drapes and makes stiff cloth too limp. A rebuilt wasm with
  hessianAuto, and a look at substeps in the presets, is the most direct "looks better in the app" step. Not done:
  wasi-sdk is not installed here, so the wasm was not rebuilt (the core changes are not in failoring.html yet).
- test_drape's 9-fabric table is unconverged (single substep). Making it 4 substeps triples its runtime; not done.
- Body friction with a smooth normal force (skin or barrier) as the default: next step.
