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
Mesh-level check, synthetic 10:1 (denim), B derived / model at 0 deg and 90 deg:
  n = 6: 0.738 / 0.786 (contrast 9.39);  n = 10: 0.826 / 0.897 (contrast 9.20);  n = 16: 0.903 / 0.972 (contrast 9.29).
The contrast deficit (about 6 to 8 %) is stable across mesh levels. The absolute offset shrinks as the mesh is
refined (0.79 -> 0.90 -> 0.97 at 90 deg), so the offset is a discretisation effect, not a fixed calibration error.
Plain answer: the hinge model under-represents the directional contrast slightly at 5:1 and 10:1 (about 7 % of
the contrast, robust to mesh level in this check). It does not fail badly at this size, so the 'under-represented
above 1:5' warning in ARCHITECTURE.md is supported only weakly, and the Bolt-style fallback is not justified by
this test alone. Limits: one real preset (1.43:1), synthetic 5:1 and 10:1 variants, cantilever only, one solver.

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

### Step 3d: drape settling check (silk, 10 mm)
- 300 frames: DC 46.6 %, KE 2.2e-7 J. 1500 frames: DC 46.3 %, KE 1.7e-7 J. The drape coefficient moves by 0.3 points,
  but kinetic energy plateaus near 1e-7 J and never reaches the 1e-8 stop threshold. So the drape values are stable
  to about 0.3 points, not "settled" by the code's own criterion. The 300-frame cap is kept for the sweep.
  (test_drape only asserts KE < 1e-4 and would not notice this.)

### Step 3e: bending check with a stated tolerance (tools/fabric_bench/check_bench.py)
- Check: |B derived / B input| <= 0.05 at the FINEST mesh level (n = 16), warp and weft. The 5 % tolerance is a choice
  made for this check (not from a textile standard). The headline (n = 10) ratio and the spread are printed too.
- Poplin (first result): finest 0.951 / 0.958 (PASS, but close to the edge), headline 0.877 / 0.881.
- The checker also prints the drape DC against docs/ARCHITECTURE.md (both numbers, no adjustment).
- Drape comparison so far: poplin bench 65.0 % vs documented 64 % (+1.0 points). Silk bench 46.6 % vs documented 36 %
  (+10.6 points). The silk gap is NOT explained yet. test_drape uses the same rig code; its per-fabric numbers were not
  captured by ctest. Likely the documented silk number is stale, but this is unverified.

## Full preset sweep (done): fabric_bench on all 9 presets, default levels (bending n = 6/10/16, drape 14/10/7 mm, 300-frame cap)
Raw outputs: docs/handoff/T1_bench_output/ (per-preset .md and .json, summary.md, check_output.txt, bias runs).
Checker: `python3 tools/fabric_bench/check_bench.py docs/handoff/T1_bench_output`.

### Bending (cantilever, elastica reference at 41.5 deg): B derived / B input
| preset | warp finest (n=16) | warp headline (n=10) | weft finest | weft headline | check at finest (|r-1| <= 0.05) |
|---|---|---|---|---|---|
| silk_charmeuse | 0.963 | 0.889 | 0.963 | 0.889 | PASS |
| polyester_chiffon | 0.965 | 0.890 | 0.965 | 0.890 | PASS |
| cotton_jersey | 0.956 | 0.881 | 0.954 | 0.879 | PASS |
| cotton_poplin | 0.951 | 0.877 | 0.958 | 0.881 | PASS (edge) |
| wool_suiting | 0.948 | 0.866 | 0.952 | 0.877 | FAIL warp, PASS weft |
| linen_medium | 0.943 | 0.860 | 0.946 | 0.873 | FAIL both |
| denim_12oz | 0.937 | 0.855 | 0.951 | 0.874 | FAIL warp, PASS weft |
| canvas_heavy | 0.926 | 0.843 | 0.930 | 0.847 | FAIL both |
| lambskin_leather | 0.921 | 0.838 | 0.921 | 0.838 | FAIL both |

8 of 18 direction checks FAIL the stated 5 % tolerance. The ratios rise steadily with mesh refinement
(n = 6 -> 10 -> 16 is about 0.75 -> 0.88 -> 0.95 for every preset), so the bending is NOT converged at the finest
level tried. I did not fit a convergence order (three levels are too few for an honest extrapolation), and I did not
loosen the tolerance. The solver's bending is low by 5 to 8 % at n = 16 and by 12 to 16 % at the headline n = 10.

### Stretch (hanging strip): derived k / input k
Warp and weft: 0.998 to 1.000 for all presets (the solver reproduces its input stiffness; this is a consistency
check, not an independent test).

### Shear (bias extension, naive formula 1/G = 4/E45 - 1/ku - 1/kv): G derived / G input
1.13 (lambskin) to 1.25 (chiffon), all presets above 1. The bias strip is therefore stiffer in shear than the input G by
12 to 25 %. NOT explained. Candidates: the clamp zones (the naive formula ignores them; the sign of their effect is not established), and the diagonal over-stiffness of the lattice mesh already listed
in docs/ARCHITECTURE.md. Neither is tested here.

### Drape (30 cm disc, 10 mm, 300-frame cap): bench DC % vs docs/ARCHITECTURE.md (both reported, nothing adjusted)
| preset | bench DC % | documented | difference (points) | folds | KE at last frame (J) |
|---|---|---|---|---|---|
| canvas_heavy | 92.8 | 93 | -0.2 | 5 | 8e-9 (settled) |
| cotton_jersey | 27.6 | 28 | -0.4 | 9 | 9e-9 (settled) |
| cotton_poplin | 65.0 | 64 | +1.0 | 7 | 4e-7 |
| denim_12oz | 80.7 | 81 | -0.3 | 6 | 9e-9 (settled) |
| lambskin_leather | 90.9 | 91 | -0.1 | 5 | 6e-8 |
| linen_medium | 81.2 | 82 | -0.8 | 6 | 8e-8 |
| polyester_chiffon | 43.1 | 38 | +5.1 | 8 | 2e-7 |
| silk_charmeuse | 46.6 | 36 | +10.6 | 7 | 2e-7 |
| wool_suiting | 64.8 | 65 | -0.2 | 6 | 9e-9 |

Six of nine agree within about 1 point. Silk and chiffon differ by 5 to 11 points, and they are the two presets whose
drape run did not reach the KE stop threshold. The drape spread over the three mesh levels is large for the light
fabrics (silk 39.7 to 46.6, chiffon 32.0 to 45.6 points), so the documented headline for those is mesh-sensitive too.
Fold counts (5 to 9) were not checked against measurement.

### Bias-angle strip (denim, real 1.43:1, and SYNTHETIC 5:1 and 10:1): see Step 3b and 3c above
Plain answer: the hinge model under-represents the directional contrast slightly for strongly orthotropic
bending (about 7 % of the 10:1 contrast, stable across n = 6/10/16). It does not fail badly, so the
ARCHITECTURE.md warning is only weakly supported by this test. The Bolt-style rewrite is not justified by this
test alone. One preset, synthetic variants, cantilever only.

## What is still unfinished (honest)
1. Bending does not meet the 5 % tolerance at the finest level tried (8 of 18). Not converged. Needs a finer mesh
   level or a proper convergence study before the calibration is trusted.
2. The drape table is not settled for silk and chiffon (KE does not reach 1e-8). The 1500-frame check on silk changed
   the value by 0.3 points, but the stop criterion itself was not met. Unexplained gap to the documented silk value.
3. The bias-extension shear is 12 to 25 % stiffer than input. Unexplained.
4. The bending check is not in ctest (it takes minutes per preset). It is run by `fabric_bench` and
   `check_bench.py`. ctest has test_fabric_bench (self-checks only).
5. test_drape's per-preset numbers are not captured in the ctest log (passing output is hidden), so the documented
   numbers could not be compared with test_drape's own output, only with the bench.
6. FABRIC_TESTING.md is a protocol document only. No real swatch has been tested. The acceptance thresholds in it are
   intentionally blank.
7. The `validated` flag and reality-gap column proposed in the T1 idea entry were NOT added to fabrics.json.
   (The handoff lists this as a T1 idea item, not a done-criterion.)
8. Uncertain: the Peirce-style formula in the code (marked UNVERIFIED); the elastica code is checked against
   its own ODE and small-load limit, not against a textbook table. The ISO 139 reference in FABRIC_TESTING.md
   is from memory.

## Final ctest (this branch head, Release, build_t1)
    1/6 Test #1: test_basics ......................   Passed    0.22 sec
    2/6 Test #2: test_cantilever ..................   Passed    3.46 sec
    3/6 Test #3: test_drape .......................   Passed  336.13 sec
    4/6 Test #4: test_garment .....................   Passed   47.40 sec
    5/6 Test #5: test_sdf_grid ....................   Passed    9.66 sec
    6/6 Test #6: test_fabric_bench ................   Passed    0.65 sec
  100 % tests passed, 0 failed out of 6.
  Note: test_drape here took 336 s (less contention than the first run, which took 1453 s). Its printed table is hidden
  by ctest; the checks it makes (DC range, KE, clearance, ordering) all pass.
