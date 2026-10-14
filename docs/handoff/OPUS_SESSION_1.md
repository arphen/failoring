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

## Open questions
- Should `test_garment` assert zero self intersections? It would fail today in default mode (87 pairs). Not changed.
- Should the core default become `tensionStiffness = true` (the app already uses it)? Not done: the brief says new
  behaviour stays off. Turning it on would change test_drape's numbers (not measured with it).
- Body friction is nearly absent in the implicit path (budget from post-solve penetration). Making it real (e.g. lagged
  friction inside Newton, using the held normal force) is a physics change; not done, needs your call.
