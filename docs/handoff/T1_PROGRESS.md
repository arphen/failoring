# T1 progress log (fabric bench + swatch protocol)

## Session 2 (resumed from the unreviewed WIP commit 1365615)

### Step 1-2: read and build
- Read T1_HANDOFF.md, ARCHITECTURE.md, fabrics.json, test_drape.cpp, and the WIP tools/fabric_bench/*.
- Build (Release, build_t1): `cmake -S . -B build_t1 -DCMAKE_BUILD_TYPE=Release && cmake --build build_t1 -j` succeeds
  with no compile errors and no warnings in the fabric_bench target. The WIP draft compiles as-is.
- ctest is slow on the 2-core machine (test_drape runs for minutes). Run it in the background with a log.
  Partial result so far: test_basics pass, test_cantilever pass (11.3 s).

### Known issues found in review (to fix)
- fabric_bench.hpp and the JSON notes refer to docs/FABRIC_BENCH.md and docs/FABRIC_PROTOCOL.md, which do not exist.
  The protocol document is docs/FABRIC_TESTING.md (per the task).
- test_fabric_bench (the elastica small-angle check mentioned in the header) is not written yet.
- The Peirce and l/2 bending-length formulas are marked UNVERIFIED (from memory) in the code.

### Bench run status
- silk_charmeuse: bending and tensile run; bias extension about 24 s; drape not yet reported (in progress).


### Step 3a: self-checks that run fast (test_fabric_bench, pure math, now in ctest)
- Added tools/fabric_bench/test_fabric_bench.cpp. Result: RESULT ok (all asserts pass).
  - Elastica small-load limit: chord = lambda/8 within 0.01 % at lambda 0.1 (theory: tip deflection qL^4/8B).
  - Elastica lambda <-> chord round trips pass; overhang <-> B round trip passes.
  - Fold counter: flat disc 0 folds, 4-fold profile 4 folds, 0.5 mm ripple 0 folds.
  - Shadow raster: disc r = 0.1 m gives area 0.031417 vs pi r^2 = 0.031416 m^2.
- FINDING (numerical, not a literature claim): at 41.5 deg the exact elastica gives c = (B/rho g)^(1/3) = l/2 to
  0.1 % (B = 1.8384e-5 vs l/2 1.8394e-5 N m at l = 50 mm). The Peirce-style formula in the code,
  c = l (cos t / (8 tan t))^(1/3), gives B 15 % lower (x0.847). The code's Peirce formula is therefore likely
  mis-remembered; it stays marked UNVERIFIED. The bench uses the elastica relation as its reference.
- Sign note: the elastica helper returns the magnitude of the chord angle (the ODE is solved for the mirror
  image); the solver check uses magnitudes only.
- Doc references in the bench sources fixed (FABRIC_BENCH.md / FABRIC_PROTOCOL.md did not exist; the protocol
  document will be docs/FABRIC_TESTING.md).

### Step 4 (draft): docs/FABRIC_TESTING.md
- Written as a document only. Covers: purpose, the three tests (cantilever at 0/45/90, hanging weights, held-out
  30 cm disc drape plus folds), known limits, materials, preparation, fitting, and acceptance.
- Acceptance thresholds are deliberately NOT set; the document says they must be fixed before the first real swatch.
- Not tested on real cloth. No real-world validation is claimed.
- Unsure: the ISO 139 standard atmosphere reference and the 2025 phone-photo figures (22 cm, 980 lux) are
  reported from the research notes, not checked against the standards or the paper text.

### Step 3b: bias-angle strip check (fabric_bench --bias-strip, 0..90 deg in 15 deg steps, n = 10 elements)
Only denim_12oz is used as a real preset (no preset in fabrics.json is strongly orthotropic: the largest
bending ratio is 1.43:1). The 5:1 and 10:1 cases are SYNTHETIC variants of denim (bending_weft = bending_warp / R).

| case | B derived / model at 0 deg | at 45 deg | at 90 deg | contrast derived (0/90) | contrast model (0/90) |
|---|---|---|---|---|---|
| denim_12oz (1.43:1, real) | 0.855 | 0.870 | 0.874 | 1.40 | 1.43 |
| denim ortho 5:1 (synthetic) | 0.832 | 0.881 | 0.892 | 4.67 | 5.00 |
| denim ortho 10:1 (synthetic) | 0.826 | 0.888 | 0.897 | 9.20 | 10.00 |

Reading: the derived angular dependence follows the compliance-mixing model's shape, but it is flatter than
the model by about 2 %, 7 % and 8 % of the contrast at 1.4, 5 and 10. The bigger effect is a constant offset of
10 to 17 % in B at every angle, which is the mesh-level calibration (see the silk mesh-level spread below).
Conclusion so far: at 10:1 the hinge model does not visibly under-represent the orthotropy beyond that offset.
This is NOT yet confirmed against mesh level (an n = 6 and n = 16 run is queued).

### Step 3c: bench sweep (fabric_bench, n = 6/10/16 bending levels, drape at 14/10/7 mm, 300-frame cap)
- Silk charmeuse, first full run (see the sweep table when it finishes):
  - bending ratio by mesh level: 0.776 (n=6), 0.889 (n=10), 0.963 (n=16). NOT converged. The 20 % spread is large.
  - bending directions: warp = weft = bias for silk (isotropic preset), so the three rows agree.
  - tensile warp ratio 0.999, weft 0.999 (stretch is reproduced).
  - bias-extension stiffness: derived 72.3 vs 59.3 N/m reference (ratio 1.22). Shear from the naive formula:
    G 18.3 vs input 15 (ratio 1.22). Hypothesis (NOT tested): the alternating-diagonal lattice is over-stiff on
    diagonals (already listed as a limitation in docs/ARCHITECTURE.md). The naive bias formula also ignores clamp zones.
  - drape at 300 frames: DC 39.7 % (14 mm), 46.6 % (10 mm), 41.4 % (7 mm). Folds 7, 7, 8.
  - **Every drape run hit the 300-frame cap with KE of 1e-7 to 6e-7 J, so none of them is a settled state.**
    The stop test is KE < 1e-8. test_drape uses the same loop and only asserts KE < 1e-4, so it cannot see this.
    A longer-frame test is queued (--drape-frames 1500 at 10 mm).
  - DISCREPANCY: docs/ARCHITECTURE.md reports silk DC = 36 % at 10 mm. The bench gives 46.6 % on the same rig.
    Both are reported; the cause is not yet known (the doc may predate the current solver or drape settings).

### Test status (ctest, full run in background)
- test_basics: pass (0.76 s). test_cantilever: pass (11.3 s). test_drape: PASS (1453 s, wall time under heavy
  machine contention; output table not captured by ctest). The remaining tests are running.
