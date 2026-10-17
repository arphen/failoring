# Crochet mode: status log

Human-readable log of the crochet work (docs/CROCHET_PLAN.md §1.1). One entry per phase
acceptance, with the measured numbers. Honest numbers; "not measured" where not measured.

## Phase checklist (§13)

| # | Phase | Done when | Status |
| --- | --- | --- | --- |
| 1 | Solver extensions (§2) | old ctest unchanged, bit-identical regression, 6 new checks pass | todo |
| 2 | Yarn data (§3) | yarns.json loads, unit tests | done (T2.1, 2026-10-16) |
| 3 | Stitch model (§4) | gauge sanity and monotonicity tests | done (T3.1, 2026-10-16) |
| 4 | DSL + graph (§5) | all fixtures compile with the stated counts, error-message tests | todo |
| 5 | Mesh + layout + relax (§6) | flat/cup/ruffle circle, tube, swatch size, hinge sign, mass | todo |
| 6 | Scenes + load + metrics + CLI (§7) | strap analytic, force balance, settle, fit checks; PNG snapshots of all fixtures in docs/crochet-shots/ | todo |
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
