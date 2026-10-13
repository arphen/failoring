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

