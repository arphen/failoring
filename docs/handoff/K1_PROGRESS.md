# K1 progress log (compliant skin + contact pressure)

## Session 2 (this agent)

### Latest results (read this first)
- **Final full ctest on commit ee659db: 6 of 6 pass** (test_basics 0.6 s, test_cantilever 5.5 s, test_drape 401 s, test_garment 45 s, test_sdf_grid 10 s, test_soft_skin 145 s). The test_drape standalone run also passed (RESULT ok).
- The earlier "4 pass / 2 fail" below is superseded by this run.
- Open items still listed under "Still open".

- Full `ctest` run (default per-test timeout 1500 s): **4 pass / 2 fail of 6**.
  - test_basics: Passed (0.85 s). test_cantilever: Passed (13.7 s). test_garment: Passed (237 s). test_sdf_grid: Passed (51 s).
  - test_drape: **Timeout at my 1500 s limit, not a failure.** The baseline run in the old build dir (`build/Testing/Temporary/LastTest.log`) shows test_drape passing after 32 min. It was rerun standalone (see below).
  - test_soft_skin: Failed on ONE check: the ring pressure check compared area-weighted pressure to k times an unweighted mean of depth. This was a test-definition error. Fixed by adding an area-weighted depth (`deltaW`) to the same test; the tolerance is unchanged (1e-6 relative). After the fix, the `ring` part: RESULT ok.
  - test_soft_skin, all parts (earlier full run, before the fix): disabled ok, scales ok, standing ok (soft layer settles: 0.0001 mm/frame vs rigid 0.044 mm/frame over last 10 frames, KE 6.9e-13 J vs 1.2e-7 J, 0 line-search failures), ring: 1 check failed (fixed above).
- Ring numbers (measured): expected delta 2.909 mm, simulated 2.933 mm (0.8 %); p_soft 433.6 Pa vs Laplace 436.3 Pa (-0.6 %); baked-mesh collider within 1.3 % of expected; rigid-force estimate 1222 Pa vs Laplace 1237 Pa (-1.2 %). Stiff skin (k=1.5e7) delta 0.089 mm vs 0.081 mm expected (+10 %, inside the 15 % tolerance).
- Standing test numbers, see above. These come from a single tube geometry; no cloth or tissue data was used.

### Still open
- Done-criterion 2 (finite-difference gradient/Hessian check of the skin energy): NOT written. The variational energy is private in `ClothSolver`, so a clean test needs a small accessor. Not done in this session.
- The `disabled` part's "layer on" sanity check reports the drop shape differs by 0.166 m (k=1e5, dmax=10 mm). That is large and unexplained. It is only a `> 1e-5` check, so it passes, but it should be investigated.
- The skin defaults are 0 (off). The k values in the tests (1.5e5, 2e5, 1e5, 4e4 Pa/m) are TEST values, not tissue measurements.



- Started from the WIP commit c69a74f. Build dir `build_k1` (not committed).
- **Build**: `cmake -S . -B build_k1 -DCMAKE_BUILD_TYPE=Release && cmake --build build_k1 -j` succeeds, no warnings seen in the log.
- **Tests run so far** (build_k1, Release):
  - test_basics, test_cantilever: Passed (same as before, no changes to them).
  - test_soft_skin `disabled` part: RESULT ok. Layer off gives identical positions (max diff 0 m for k>0,dmax=0 and k=0,dmax>0).
  - test_soft_skin `scales` part: RESULT ok. Sheet on soft/stiff faces: delta 0.0368 / 0.0092 mm, equals sigma/(k*s) to 4 digits. Mean soft pressure 1.471 / 1.472 Pa vs weight 1.472 Pa. Region map labels correct.
  - test_drape, test_garment, test_sdf_grid, test_soft_skin `ring` and `standing`: NOT YET RECORDED (the full ctest run was still in progress; the machine has 2 cores shared with other agents, so the full run is slow).
- **Not yet done** (done-criteria from the handoff):
  - Item 2, finite-difference gradient/Hessian check for the skin energy: not written.
  - Item 3, the ring test, the standing (wedge-like) comparison: code exists in test_soft_skin.cpp, results not yet recorded.
  - Item 4, the pre-existing ctest tests passing: pending the full run.
  - The skin defaults are 0 (off). The k = 2e5 Pa/m and k = 1.5e5 Pa/m values in the tests are test values, NOT tissue measurements.
- **Doubts**:
  - The `disabled` part's "layer on" sanity check reports a drop difference of 0.166 m. That is large, and it is not explained yet. Needs a look before trusting the layer-on behaviour.
  - Docs reference `docs/agent_notes/K1_compliant_skin.md`, which does not exist in this worktree.
