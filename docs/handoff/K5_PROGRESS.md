# K5 progress log

## Session 2 (resume of the unreviewed WIP)

- Found the WIP already registered in CMakeLists.txt (cloth_barrier.cpp added) and already wired into the Newton loop
  (cloth_implicit.cpp, ClothSolver barrier mode, SolverParams::barrierContact default false).
- Build (build_k5, Release): configure and build succeed with no errors.
- Tests run so far:
  - test_barrier: RESULT ok. FD gradient/Hessian errors 2.2e-10 / 3.7e-09; distance vs sampling 6.5e-12 (PT), 1.2e-14 (EE);
    CCD known-TOI cases all conservative (ccd t below toi); randomized CCD: missed 0, unsafe 0.
  - test_sdf_grid: RESULT ok.
  - test_basics: RESULT ok.
  - test_cantilever: RESULT ok.
  - test_drape: still running at time of this commit (long).
  - test_garment: not yet run at time of this commit.
- Not yet verified: the edge-edge passthrough scenario (done-criterion b), the layered sheet test (d), cost comparison (e),
  and whether test_garment passes with barrierContact on.
- The ctest suite takes longer than 10 minutes in one call; run the binaries in the background.

## Done-criterion (b): edge-edge passthrough (added in cloth_core/tests/test_passthrough.cpp, registered in CMake)
- Scene: strip A (2 cm wide, pinned flat) and a coarse 10 cm sheet B falling so its bottom edge crosses A's strip.
  Corners of B are 4 cm from A. Exact triangle-triangle intersection count each frame, 120 frames, substeps 2.
- Measured: vertex-triangle mode (selfCollision) -> intersecting frames 5, max 3 pairs.
  barrier mode (barrierContact) -> 0 intersecting frames.
- Test: RESULT ok (old mode must show intersections, new mode must show none).
- Caveats: B has no floor, so its lowest point keeps falling while it drapes over A (the metric is not used). The
  "min self distance at end" is 14.142 mm in both modes; not investigated (it may reflect the rest geometry). The
  scene was chosen by hand, not by a search, and it is a single configuration.

## Done-criterion (d): layered sheet-on-sheet (cloth_core/tests/test_layers.cpp, registered in CMake)
- Scene: pinned 10 cm plate at y = 0; 6 cm cotton sheet 1 cm above it, centred, falling under gravity; 120 frames, substeps 2.
- Result, barrier mode: rests on the plate, min layer gap 0.688 mm (frame 50: 0.693 mm), top mean height 0.771 mm,
  0 intersecting frames. RESULT ok.
- Result, vertex-triangle mode (old): falls through the plate; 5 intersecting frames (max 18 pairs), then falls to about -13 m.
- Earlier attempt used a 10 cm top sheet directly over the plate: the top layer slid sideways off the plate in BOTH modes.
  Cause (our reading): the top sheet's edges sat over the plate's edges, so edge-edge contact pushed it outward.
  A lone sheet with no plate falls with no lateral drift (checked). The 6 cm top sheet avoids the coincident edges.
- Metric caveat: ClothSolver::minSelfDistance also measures pairs inside a single sheet (flat sheets are 1.4 cm apart
  inside, which is why it returned a constant 14.142 mm in the earlier passthrough and layer runs). The layer gap in this
  test is therefore measured directly (lowest top vertex over the plate footprint). The passthrough test does not use it
  for pass/fail, but the value it prints is not meaningful.

## Cost (e): same two scenes, measured on this machine (shared with other agents' jobs, so noisy)
| scene | mode | ms/frame | Newton iterations (per substep) | factorisations | pattern rebuilds | result |
|---|---|---|---|---|---|---|
| layers, 120 frames | vertex-tri | 2.2 | 560 (2.33) | 560 | 0 | falls through (not comparable physics) |
| layers, 120 frames | barrier | 264.7 | 1730 (7.21) | 1730 | 3 | rests, 0 intersections |
- Passthrough cost was not recorded separately yet.
- Factorisations equal Newton iterations: the default solver refactors every iteration (reuseFactorization is off).
- Not compared: the vertex-triangle run's physics is different, so the iteration counts are not like-for-like.

## Still open
- (c) test_garment with barrierContact on: not run.
- test_drape (main ctest suite, mode off) still running at the time of this note; it runs for a long time on this machine.
- Passthrough cost, and a cost comparison on a case where both modes produce the same physics.
- Newton tolerance untouched (1e-6). No test was weakened.
