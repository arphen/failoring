# K1 progress log (compliant skin + contact pressure)

## Session 2 (this agent)

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
