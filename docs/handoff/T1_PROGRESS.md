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
