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
