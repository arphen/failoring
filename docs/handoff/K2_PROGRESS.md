# K2 progress log

Branch `agent/k2-contact-multipliers`, worktree `.claude/worktrees/agent-a84a84cfb5df049c4`.

## State at resume

* The previous agent's AL code is present and committed: `cloth_core/src/cloth_contact_al.cpp` (diagnostics, AL
  energy, `alAssemble`, `stepImplicitAL`, `solveBodyAL`) and the `SolverParams::contactAL` switch in `cloth.hpp`.
  `ClothSolver::step()` calls `stepImplicitAL()` only when `contactAL` is true (default off).
* Uncommitted before this session: diagnostic counters in `cloth.cpp` and `cloth_implicit.cpp`, one extra source line in
  `CMakeLists.txt`.
* The header comment cites `docs/agent_notes/K2_contact_multipliers.md`. That file did not exist. It is not written yet.

## Done this session

1. Configured and built in `build_k2` (Release). The whole tree compiles with the AL code and the diagnostics.
2. Added `cloth_core/tests/wedge_scenes.hpp` (scene 0: tube on the ellipsoid, scene 1: tube on two overlapping
   ellipsoids). Both were untracked before this session and had not been compiled.
3. Added `cloth_core/tests/test_wedge.cpp`: runs a scene with `contactAL` off and on, 240 frames, and records per-frame
   KE, line-search failures, contact-active vertices per substep, minimum body clearance, max edge stretch, hem height.
   Writes `out/wedge_<scene>_<mode>.csv`. Registered in the `foreach` in `CMakeLists.txt`.

## Test results

* Build (Release, build_k2): compiles, 0 errors. Ran with `contactAL` off by default.
* test_basics: exit 0. test_cantilever: exit 0. test_drape, test_garment, test_sdf_grid: still running at this checkpoint
  (the machine has 2 cores and is shared with other agents' test runs). Exact counts pending.

## Wedge numbers

Scene 0 (tube on ellipsoid, = test_garment setup with 240 frames), contactAL OFF, 4 substeps, 672 vertices:
* KE final 9.7e-8 J; mean KE over last 60 frames 7.4e-7 J; max over last 60 frames 1.2e-6 J.
* Line-search failures: 0 in total, 0 in the last 60 frames.
* Contact-active vertices per substep (last 60 frames, mean): 68.2.
* Minimum body clearance 1.647 mm (no penetration); max edge stretch 1.1151 over the run, 1.0485 at the end; hem 156.7 mm.
* Projection path: 14236 in-Newton projections, 1169 after-solve projections.

This does NOT reproduce the handoff's "~1e-4 J, about one line-search failure per frame" for this scene. Caveat: the
handoff's number may refer to a different case (two bodies, or a different run length). Scene 0 AL-on and scene 1 are
still to be measured.

## Unfinished

* Measurement of the projection baseline (in progress).
* Comparison off vs on, with the done-criteria in `K2_HANDOFF.md`.
* `docs/agent_notes/K2_contact_multipliers.md` (referenced from the code, missing).

## Unsure about

* The AL code has not been run before this session, so its correctness is unverified.
* Machine has 2 cores; the full ctest run takes longer than 10 minutes, so it is run in the background.
