# Crochet mode: status log

Human-readable log of the crochet work (docs/CROCHET_PLAN.md §1.1). One entry per phase
acceptance, with the measured numbers. Honest numbers; "not measured" where not measured.

## Phase checklist (§13)

| # | Phase | Done when | Status |
| --- | --- | --- | --- |
| 1 | Solver extensions (§2) | old ctest unchanged, bit-identical regression, 6 new checks pass | done (T1.1-T1.6, 2026-10-17) |
| 2 | Yarn data (§3) | yarns.json loads, unit tests | done (T2.1, 2026-10-16) |
| 3 | Stitch model (§4) | gauge sanity and monotonicity tests | done (T3.1, 2026-10-16) |
| 4 | DSL + graph (§5) | all fixtures compile with the stated counts, error-message tests | done (T4.0-T4.2, 2026-10-17) |
| 5 | Mesh + layout + relax (§6) | flat/cup/ruffle circle, tube, swatch size, hinge sign, mass | done (T5.1-T5.4, 2026-10-18) |
| 6 | Scenes + load + metrics + CLI (§7) | strap analytic, force balance, settle, fit checks; PNG snapshots of all fixtures in docs/crochet-shots/ | in progress (T6.1, T6.2 done) |
| 7 | Godot bindings + scene (§8) | crochet_api_check passes; the user can open, edit, run, and see the pouch hang on their Mac | todo |
| 8 | Chart, lego, compare (§9) | five-variant comparison runs from one click; the table shows different corner angles | todo |
| 9 | Calibration (§10) | a swatch record changes the result and the UI says "calibrated" | todo |
| 10 | Yarn-level look (§12) | optional | not started |

## Log

### 2026-10-16 — T0.1 setup

* Branch `crochet/main` from `main` @ f5a0a98 (dirty files: modified `godot/bin/libfailoring.dylib`,
  untracked `Makefile` — user's local files, left alone; the dylib was rebuilt earlier today and is a
  local build product).
* Baseline build and ctest saved to `docs/crochet/baseline_ctest.txt`.
  **10 of 13 pass; test_layers, test_wedge and test_static fail on the unmodified baseline**
  (reproduced twice). Crochet tickets must not regress relative to this; the three failures are
  out of scope.
* Regression dump: `docs/crochet/baseline_garment_positions.txt`, 672 vertices, written with
  `FL_DUMP=<path> ./build/test_garment` (new env hook in `cloth_core/tests/test_garment.cpp`).
  test_garment itself passes: KE 1.0e-06 J, seam gap 0.45 mm, hem 165.6 mm vs predicted 161.2 mm.
* Board: `docs/crochet/BOARD.md`.
* Metrics of the baseline failures (for the record):
  * test_layers: barrier mode top layer ends below the plate plane (line 98).
  * test_wedge: scene 1 default KE reduction 38x (want >100x), 58/240 tail substeps at the Newton cap;
    scene 0 173/240 cap hits; contactAL tail KE 2.06e-06 J (want <1e-06).
  * test_static: drape disc silk static 37.96 vs dynamic 40.80 (want <2.0 difference).

### 2026-10-16 — phases 2 and 3 accepted (T2.1, T3.1)

* T2.1 (yarns.json + loader, `225d0ab`): file is byte-identical to the plan's §3 block; loader tests
  pass. Review: clean rebuild 11/14 (same 3 baseline failures, FAIL lines identical), mutation check
  (duplicate-id guard disabled -> test fails) caught. Merged `9b3a0d0`.
* T3.1 (stitch model, `ac0e14a`): worsted sc on a 5 mm hook gives w 6.500 mm, h 6.000 mm
  (15.38 sts and 16.67 rows per 10 cm), density 0.4363 kg/m2 (10x10 cm swatch 4.363 g) — inside the
  §4.2/§4.3 sanity ranges. Monotonicity, fibre ordering (cotton > acrylic > wool bending), toFabric
  warp=wale mapping, all 35 type/insertion combos and the §10 calibration semantics have tests.
  Review: clean rebuild 12/15 (same 3 baseline failures, FAIL lines identical), mutation check
  (fibre bend multiplier ignored -> fibre-ordering test fails) caught. Merged `66ef507`.
* Suite size is now 15 tests: 12 pass, the 3 baseline failures unchanged.

### 2026-10-16 — T4.0 CrochetPARADE spike: decision B

* All §11 fixtures are written in CrochetPARADE syntax (`patterns/cp/*.txt`); the vendored clone is
  `third_party/crochetparade` @ `06e987b` (GPL-3.0-or-later, licence filed in `LICENSES/`; the clone
  itself is gitignored). Counts verified per row (pouch 1380 stitches, bags 5798/5912, circles
  217/189/273, tube 512); 0 export errors on all ten fixtures.
* Parse times: pouch 0.09 s, bag 0.67 s (bar: < 2 s — met). Layout (cold, single-thread): pouch
  3.5 s, bag 66 s → must run on a worker/cache in T7.1.
* Layout shapes: +6 flat (plane rms 0.83), +5 cup (cone slope −0.58, residual 0.26), +8 ruffle
  (rim ring rms 2.90); deterministic and idempotent. Tube axis straight (1.1 %) but radius drifts
  2.07→4.90 — initial shape only; revisit in T5.3.
* Missing constructs are exactly the single-target attachment: second targets for the side seams and
  strap-end joins → `#!` extension line (decision B; syntax fixed in `docs/CROCHET_DSL.md`, T4.1).
* Review found and round 2 fixed a defect in `tools/crochet/cp_export.mjs`: piped stdout truncated
  at the pipe buffer (`process.exit` before flush); pipe and `--out` outputs are now byte-identical.
* Phase 4 (DSL + graph) stays in progress: T4.1 (bridge + importer + extension reader) and T4.2
  (facing/RS, seams, count checks) remain.

### 2026-10-16 — T1.3 signed hinges with a rest angle (phase 1, item 3 of 6)

* `detail::hingeAngleSigned`: signed dihedral angle with consistently oriented normals,
  `theta_s = atan2(<cross(n2,n1), e_hat>, <n1,n2>)` (the cross order is the global-sign choice
  §2.2 tells the implementer to fix by finite differences), Bridson 2003 §4.1 gradients
  (index-mapped and sign-verified by FD: kHingeSignedGradSign = +1), degenerate → false + zero
  gradients. The unsigned `hingeAngle` is untouched.
* `assembleEnergy`/`variationalEnergy`: `rest == 0` runs the exact pre-change code (bit-identical
  by the FL_DUMP check, 672 vertices); `rest != 0` uses C = theta_s − rest with the same
  Gauss-Newton `add`. XPBD (`solveBends`) and VBD keep the unsigned positional angle (deliberate).
* Test 3: 20 seeded random configurations — |theta_s| == unsigned theta to 4.4e-16 rad, worst
  E-gradient relative error vs central FD 4.5e-08 (< 1e-6), gradient sum 8.0e-16, flat 0,
  folded ±pi/2. Test 4: strip with rest +0.5 rad settles at 0.5000 rad in 42 Newton iterations
  with the free vertex +9.59 mm on triangle 1's +normal side.
* Review: clean rebuild 13/16 (the 3 baseline failures, FAIL lines identical), dump bit-identical,
  two mutations caught (gradient sign flip; rest sign flip in assembleEnergy). Merged.
* Phase 1 remains in progress: T1.4 (J-curve, test 5), T1.5 (point seams, test 6) and T1.6
  (contact readback, RoundedBoxCollider) remain.

### 2026-10-16/17 — T1.4 J-curve strain-stiffening (phase 1, item 4 of 6)

* `FabricMaterial` gains optional `lockStrainWarp`/`lockStrainWeft` (0 = off) and
  `lockStiffnessRatio` (>= 1); JSON keys `lock_strain_warp`/`lock_strain_weft`/
  `lock_stiffness_ratio`; `TriC` gains `lockU`/`lockV`/`ratio` (u = warp, v = weft).
* One shared helper `detail::jCurveStretch` (e, dE/dC, GN kappa = r*k) feeds the locked branches of
  `assembleEnergy` (gradient `g·dedc` via `add(..., dedc/kappa, kappa)`, tension term `dedc/lu`) and
  `variationalEnergy` (`jc.e`). The unlocked arithmetic is byte-for-byte the pre-T1.4 code.
* Test 5a: C1 at the lock exactly; central-difference dE/dC worst relative error 2.5e-11.
  Test 5b: single-triangle pinned-weight rig settles at C = 0.199503 (above the 0.15 lock) —
  measured vs analytic piecewise answer and vs an independent bisection both 3.0e-12
  (< 0.5 %); `StaticResult.energy` matches `jc.e − work` to 0 J; the v-axis mirror (lockV 0.12)
  the same. Loader validation (ratio < 1, negative lock) throws.
* Review: clean rebuild 13/16 (baseline failures, FAIL lines identical), dump bit-identical,
  mutation (dedc ignores ratio) caught six ways. Merged. (The implementer session died to a
  harness rate limit after committing; the review re-ran everything from the commit.)
* Phase 1 remains: T1.5 (point seams, test 6) and T1.6 (contact readback, RoundedBoxCollider).

### 2026-10-17 — T4.1 bridge: CrochetPARADE export importer + `#!` extension reader (decision B)

* New bridge in crochet_core: `fl::StitchGraph` (graph.hpp: Stitch/Row/Link) and
  `importPattern`/`importPatternFile` (pattern.hpp, parse.cpp) reading the committed exports plus
  `#! link [row,pos] [row,pos] both_layers|join` extension lines, with strict schema/UTF-8/address
  validation and errors naming the line, stitch or address. JSON `positions` is reserved for T5.3;
  CrochetPARADE's own `errors` abort the import.
* Ten deterministic exports committed under `patterns/cp/exported/` (~6.6 MB) so ctest is hermetic
  (no node, no clone): pouch 1380 st / 133 rows / 55 links (25+25 both_layers by physical-column
  parity across the fold + 5 strap joins), bags 5798 / 146 / 6 join (surface 5912 / 147), circles
  189 / 217 / 273, tube 512 / 13; border rims 25..39 and 65..80 stay unlinked. `docs/CROCHET_DSL.md`
  pins the format, the `#!` grammar, worked pouch examples and the regeneration command.
* Review: clean rebuild 14/17 (baseline FAIL lines identical), `test_crochet_parse` 110 PASS lines
  ("RESULT: ok"), 10/10 exports byte-identical to fresh `cp_export.mjs` runs, DUMP bit-identical,
  laziness clean, scope exactly 23 files, gdextension configure done. 2 mutations caught (kind
  demoted -> 6 failures; duplicate-link check off -> 2 failures). Merged.
* T4.2 unblocked: RS/WS facing from `loop`, point seams from the `Link`s, count checks.

### 2026-10-17 — T1.5 point seams + dampingScale (phase 1, item 5 of 6)

* `addPointSeam(a, b, share)` (§2.4): stores validated requests, `finalize()` rebuilds one `SeamC`
  per request (`b0 = b1 = b`, `t = 0`, `alpha = 1/(seamStiffness*share)`) plus the bidirectional
  `noCollide_` entries; repeated `finalize()` is idempotent; the same `finalized_` gate as
  `addSeam`. `SolverParams::dampingScale` (default 1) multiplies `damp_` after the per-vertex
  fill, guarded so the default path is bit-exact (§6.9's x5 relaxation).
* Test 6: two 0.10 m squares, facing edges 5 cm apart, 3 point-seamed pairs, `seamRampFrames 30`,
  gravity 0 — after 120 frames the pair distances are 7.47e-07 / 5.93e-07 / 7.91e-07 m
  (< 0.5 mm). `dampingScale` 1 vs 5 after 10 frames: KE 5.775926e-05 vs 3.721010e-05 J.
* Review: clean rebuild 13/16 (baseline FAIL lines identical), dump bit-identical, laziness
  clean, scope exactly three files. Mutations: no point seams built -> 2 failures (distance stays
  5.000e-02 m); `dampingScale` ignored -> 1 failure (KE x5 == x1). A 1e6-weaker seam is NOT
  detected (zero-g free panels: any positive stiffness closes in 120 frames) — test 6 pins seam
  presence, not stiffness, matching the plan's acceptance. Merged.
* Phase 1 remaining: T1.6 (contact force readback, RoundedBoxCollider).


### 2026-10-17 — T1.6 contact readback + RoundedBoxCollider (phase 1 complete)

* §2.5: `ClothSolver::rigidContactForces()` / `contactNormals()` (N per vertex of the last
  implicit Newton iteration; normals valid exactly where the force > 0) + the audit, written at
  the `contactN_` write site: contactN_ is rewritten for every vertex with `w != 0` whenever a
  collider exists; a positive force needs a collider-written `contactPhi_` (band test or the AL
  multiplier `f > 0`); the final `rigidForce_` assignment gives pinned and no-collider vertices
  exactly 0. Claim by claim verified in review.
* §2.6: `RoundedBoxCollider(center, Mat3 rotation, halfExtents, cornerRadius)` + `setPose` in
  `collider.hpp` — convention `world = center + R*local` (axes are R's columns), core
  `H = half - r`, `d = length(max(q, 0)) + min(max q, 0) - r`, exact normals (two branches
  mirroring the gdextension's `OrientedBoxCollider`), `r = 0` = the sharp box, `FL_THROW`
  validation. The gdextension class is untouched (T7.x).
* Test 7: 21 analytic probes on a rotated box (corner/edge distances -1.2e-17 / -1.7e-17 m),
  central difference vs the returned normals 2.4e-10, `setPose`, `r = 0`, validation throws;
  then a 0.12 m cotton patch resting on a rounded box for 200 frames: `sum f_i n_i` =
  (0, +1.695168e-02, 0) N vs (0, m g, 0), 1.0e-15 relative (plan bound ±2 %), >= 20 contacts,
  normals unit and upward.
* Review: clean rebuild 14/17, FAIL lines byte-identical (562 s), dump bit-identical, scope
  exactly 4 files (301 insertions), laziness clean. Mutations: forget `- r` -> 19 failures;
  drop the inside face sign -> 4; halve `rigidForce_` -> balance off by exactly 0.5. Merged
  (`ec7d550`, merge `d436123`). Phase 1 (T1.1-T1.6) is complete; next programming tickets are
  T5.1/T5.2 once T4.2 merges.


### 2026-10-17 — T4.2 facing/RS, point seams from links, count checks

* `resolveFacing` (import-time, `graph.cpp`): `rs(0) = true`; a chains-only foundation row 0 does
  not flip row 1, a worked row 0 does; for `r >= 2` a row is WS iff `rows[r-1].turned`.
  `loopRs` swaps `blo`/`flo` only on WS rows (RS-relative insertion, §5.5). Verified per fixture:
  pouch alternates RS row 1 .. WS row 50, border round 51 RS, strap 52 RS .. 131 WS, join 132 RS;
  bag wall rounds 1-44 RS, strap 45 RS .. 144 WS, join 145 RS; circles/tube all RS.
* `pointSeams`: every `#! link` in written order -> `PointSeam{stitch, partner, kind}` for T5.2
  (pouch 55 = 50 both_layers border scs + 5 strap-end joins; bags 6 joins each; circles/tube 0),
  validated with errors naming the ids. `#! count [row] N` (N = stitches in the row, turning
  chains and join sl sts included) with per-row count lines appended to all 11 fixtures; N is
  checked against the imported graph and a mismatch names line, row and both numbers.
* New `facing.txt` fixture (29 stitches, all four RS/WS x blo/flo combinations, 10 swapped
  bodies) + export. DSL doc updated (sections 2-7).
* Review: clean rebuild 14/17, FAIL lines byte-identical (539 s), `test_crochet_parse` 172 PASS
  ("RESULT: ok"), 11/11 fixtures regenerate byte-identical, dump bit-identical, gdextension
  configure done, scope exactly 20 files, laziness clean, independent python read-back of every
  count line. Mutations: facing turn logic off -> 10 failures; RS-relative swap off -> 3;
  PointSeam ends swapped -> 4. Merged (`40b31b4`, merge `97173d6`). T5.1 unblocked.


### 2026-10-17 — T5.1 faces, rest shapes, orientation (phase 5, item 1)

* `buildMeshTopology` (crochet_core/src/crochet_mesh.cpp) on all 11 fixtures: Euler characteristic 1
  and one connected component everywhere, 0 warnings, every edge in <= 2 faces, orientation
  consistent across every manifold edge, no degenerate triangle (smallest rest area 6.8 mm^2 vs the
  0.39 mm^2 bar). Pouch 1250 vertices / 2204 faces / 1 jump (the border at the fold); each bag
  5654 vertices / 10990 faces; circle +5/+6/+8 335/384/482 faces (+6 matches the hand count: round k >= 2
  with N stitches = 2N - 6 triangles); tube40 499 / 956; facing 25 / 32. Dropped stitches (turning
  chains, the chain `sk` used, closing sl sts, 114 surface sl sts in the surface bag) hand their
  yarn mass to the nearest kept stitch (`dropTo`).
* Found and fixed on the way: the oval-base fixtures jumped `n` chain stitches in round 1 (CP works an
  unturned chain row upward); now `NNch,turn` with explicit second-side targets (docs/CROCHET_DSL.md
  section 8). Five exports byte-identical, six regenerated; one assertion follows (tube40 row 0).
* Regression: clean `build/`, 15 of 18 pass, the three baseline failures unchanged (FAIL lines
  byte-identical, 530 s), dump bit-identical, `cloth_core/` untouched. Nine mutations caught.
  Merged `4bb4817`. The first implementer died without code; the orchestrator session redid it.

### 2026-10-17 — T5.2 materials, hinge modifiers, point seams (phase 5, item 2)

* `buildCrochetModel` (crochet_core/src/crochet_model.cpp): the solver's total vertex mass equals the
  yarn model's on all 11 fixtures (pouch 22.0948 g, bags 105.87 / 105.02 (slst) / 107.14 g (surface),
  circles 3.21 / 3.68 / 4.64 g, tube 8.44 g, facing 0.409 g; relative difference 0.0000 %; plan bound 0.5 %).
  Needed a change from the plan's density: a stitch's triangles carry its whole yarn (an increase triangle
  has density 1.04 vs nominal 0.44 kg/m^2). 2-6 materials per fixture, J-curve lock terms mapped.
* Hinge modifiers per plan 4.4 (UNVERIFIED starters, calibration-overridable): blo 0.30 / flo 0.35 on the
  114 edges of round 4, slst 1.5 (base + top), surface 0.70 on 113 edges with 226 stiffened faces. Point
  seams: pouch 55, each bag 6, share = stitch width. Seven mutations caught.
* Open: the WS fold enters the solver as a negative angle (kWsFoldSign = -1); T5.4 test 5 measures it.

### 2026-10-17 — T5.3 layouts and layout snapshots (phase 5, item 3)

* `layoutModel` (crochet_core/src/crochet_layout.cpp): flat rows (tridiagonal smoothing), revolve rounds
  (plan rule), the pouch fold, rim rows, the strap arc; 11 snapshots in `docs/crochet-shots/layout/`.
  Pouch: fold after row 25, back layer at z = -4.9 mm, strap 480.0 mm = 80 rows x 6.00 mm, seam gaps
  <= 8.4 mm, 14 of 3159 neighbouring normals disagree (the fold crease). Bags: strap loop 561 mm of arc,
  189 mm above the mouth, seam gaps <= 8.0 mm. Circles: +6/+8 flat, +5 a 21 mm cup, ring radii N w/2pi within 2 %.
  Edge length / rest length: median 1.00 (circles +8: 1.26), p95 <= 1.63, worst 2.34 (bag oval caps / strap start).
  Nine mutations caught. Native layouts: no CrochetPARADE positions.
* Next: T5.4 relaxation + the §6.10 tests (the circle flat / cup / ruffle test is the key one).

### 2026-10-18 — T5.4 relaxation and the geometry tests (phase 5 accepted)

* `relaxModel` (gravity 0, seams ramped over 60 frames, damping x5, self collision, Newton budget 30): every
  fixture tested relaxes before the frame cap, Newton cap hits 0.
  * Test 1: 20 x 20 swatch 130.00 mm x 120.00 mm = 20 w x 20 h (0.00 % / 0.00 %).
  * Test 2 (the key one): circle +6 sc flat (out-of-plane 0.00 % of the 46.5 mm radius), +5 a cup 43.3 % deep (93.5 % of
    the vertices on one side of the rim plane), +8 ruffles (rim sign changes 4, deviation 74 %).
  * Test 3: tube40 circumference of rounds 4 / 8 / 12 within +0.11 % / -0.08 % / -1.61 % of 40 w.
  * Test 5: BLO on a RS row turns the top part +25.6 deg toward the WS, FLO -25.7 deg (want +-25 +- 3).
  * Pouch: 192 frames, KE 1.0e-9 J, largest seam gap 0.580 mm. Bag (not in ctest): 175 frames, 180 s, seam gap 17 um.
* Tuned constants (before -> after, reason): `compressionRatio` 0.2 -> 0.1 (the +6 circle ruffled with 0.2), foundation
  chain edge rest length 0.85 w -> the stitch width, relaxation Newton iterations 12 -> 30. See reports/T5.4.md.
* Regression: 17 of 20, the three baseline failures unchanged (FAIL lines byte-identical), dump bit-identical,
  `cloth_core/` untouched. Six mutations caught. Merged. **Phase 5 is done.**

### 2026-10-18 — T6.1 hang_pin / hang_peg scenes (phase 6, item 1)

* `CrochetScene`: the pouch hangs by a pin (middle two strap rows) and from a peg (capsule inside the loop, friction
  mode 2) and settles in 239 / 276 frames (KE < 1e-8 J for 30 frames), Newton cap 0 of 478 / 552 substeps, 0 self
  intersections, 3.88 mm clearance to the peg, 38.8 cm from the strap apex to the lowest point (strap 24 + body 15).
  The hung pouch stays centred on the peg (z = -0.1 cm). Snapshots in docs/crochet-shots/scenes/. Three mutations caught.

### 2026-10-18 — T6.2 RigidLoad and the fit check (phase 6, item 2)

* Pouch hung by the pin with a 70 x 140 x 9 mm, 0.3 kg box: inserted in 175 frames, released and settled in 638 frames
  (|F_net| 0.014 m g), pin reactions 3.1793 N vs (cloth 0.0221 + box 0.300 kg) g = 3.1598 N (+0.62 %, bar 2 %), 0 self
  intersections, Newton cap 0 of 2104. Strap 5 x 60 sc under 1 kg: elongation 63.74 mm vs analytic 63.55 mm (+0.30 %).
  Fit check: pouch + MBA13 `fits: false` (20.7 cm too wide), bag + MBA13 `fits: true` (93 % of the 34.5 x 23.0 cm inside).
* Tuned (before -> after): release damping 8/s -> 80/s (8 and 30 leave the box bobbing), rounded-box corner radius
  10 mm -> min(10 mm, 0.9 half thickness). Open: free-edge half cells (narrow straps ~4-25 % too soft), see BOARD.md.
