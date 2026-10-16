# Crochet mode: status log

Human-readable log of the crochet work (docs/CROCHET_PLAN.md §1.1). One entry per phase
acceptance, with the measured numbers. Honest numbers; "not measured" where not measured.

## Phase checklist (§13)

| # | Phase | Done when | Status |
| --- | --- | --- | --- |
| 1 | Solver extensions (§2) | old ctest unchanged, bit-identical regression, 6 new checks pass | todo |
| 2 | Yarn data (§3) | yarns.json loads, unit tests | todo |
| 3 | Stitch model (§4) | gauge sanity and monotonicity tests | todo |
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
