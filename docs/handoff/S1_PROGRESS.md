# S1 progress log (static equilibrium solve)

Worktree: /home/claude/failoring/.claude/worktrees/agent-a0143c4107d1cdcf3, branch agent/s1-static-settle.

## Status at start of this session
- The WIP commit (5e229f4) contains a full draft: `ClothSolver::solveStatic`, `potentialEnergy`,
  `checkStaticStability`, warm-start support (`bindBlockSlots`, `patternValid_`), `test_static.cpp`.
- Note: ARCHITECTURE.md refers to `docs/agent_notes/S1_static_settle.md`, which does not exist in this branch.

## Build
- `cmake -S . -B build_s1 -DCMAKE_BUILD_TYPE=Release && cmake --build build_s1 -j`: configures and builds
  with no errors (all six test binaries, including test_static).

## Test results (existing tests, WIP code as committed)
Machine has 2 cores and is shared with other agents' test runs, so wall times are inflated; the CPU
time reported by each test is the more reliable figure.
- test_basics: pass (RESULT: ok)
- test_cantilever: pass (RESULT: ok)
- test_sdf_grid: pass (RESULT: ok)
- test_garment: pass (RESULT: ok; hem 156.7 mm vs predicted 161.2 mm, seam gap max 2.35 mm,
  clearance 1.65 mm, 153 frames, 745 s wall)
- test_static: FAIL, 2 of 3 cases (2 checks). Measured (h = 10 mm, 847 verts, 2 cores shared, CPU s):

  | fabric | dynamic DC (frames, Newton it, cpu) | static DC (its, stages, cpu) | |dDC| | factor it x | cpu x |
  |---|---|---|---|---|---|
  | silk_charmeuse | 46.6 (300 frames, 3578, 51.2 s) | 38.0 (341, 6, 5.1 s) | 8.6 | 10.5 | 10.0 |
  | cotton_poplin | 65.0 (300 frames, 3578, 50.1 s) | 60.5 (148, 6, 2.2 s) | 4.6 | 24.2 | 22.8 |
  | denim_12oz | 80.7 (214 frames, 2558, 36.0 s) | 80.0 (110, 6, 1.7 s) | 0.7 | 23.3 | 21.6 |

  Static: converged by its own criteria (residual 4.6e-4 to 7.7e-4 < 1e-3, step < 1e-5 m), 0 failed stages, 0 penetration.
  Static uses 10-24x fewer Newton (factorisation) iterations and CPU than the dynamic settle, which passes the speed checks.
  The DC tolerance of 2 points is FAILED for silk and cotton. This is NOT yet explained. Key suspicion: the dynamic
  reference is not settled. Silk and cotton ran the full 300-frame cap, and the stop test (KE < 1e-8 after frame 100)
  never fired for them, so the reference is an unfinished drape. Not verified yet. The tolerance has not been changed.
- test_drape: pass (RESULT: ok), 40 min wall, 6.4 min CPU on the shared machine. Dynamic reference at h = 10 mm:
  | fabric | DC % | frames | final KE (J) |
  |---|---|---|---|
  | cotton_poplin | 65.0 | 300 (cap) | 3.6e-07 |
  | denim_12oz | 80.7 | 214 | 9.3e-09 |
  | linen_medium | 81.2 | 300 (cap) | 8.1e-08 |
  | wool_suiting | 64.8 | 125 | 8.8e-09 |
  | silk_charmeuse | 46.6 | 300 (cap) | 2.2e-07 |
  | polyester_chiffon | 43.1 | 300 (cap) | 1.9e-07 |
  | canvas_heavy | 92.8 | 179 | 8.4e-09 |
  | cotton_jersey | 27.6 | 223 | 8.9e-09 |
  | lambskin_leather | 90.9 | 300 (cap) | 5.9e-08 |
  Five fabrics hit the 300-frame cap with KE above the 1e-8 J stop threshold, so their dynamic DC is not a converged value.
  The existing test accepts this; it is not changed.
- test_garment (pass) took 745 s wall on the shared machine.

## Diagnosis of the drape mismatch (scratch probes outside the repo, not committed)
- Dynamic silk settle run to 900 frames: DC stays between 45.5 and 46.7 from frame 100 on (jitter about +-0.6 points),
  final KE 7.9e-8 J. It never reaches the 1e-8 J stop threshold, but its DC is stable, so 46 is a usable reference.
  The static 38.0 is therefore a real 7-8 point gap, not a dynamic-settle artefact.
- Energy comparison (silk, h = 10 mm, same mesh):
  | route | DC | potential energy (J) | converged |
  |---|---|---|---|
  | dynamic, 300 frames | 46.6 | -5.828e-4 | (KE 2.2e-7) |
  | static, cold start (gravity ramp + pseudo-transient) | 38.0 | -5.034e-4 | yes, residual 7.7e-4, 341 Newton it |
  | static, warm start from the dynamic shape | 46.5 | -5.847e-4 | residual 1.0e-3, 66 it |
  The cold static solve stops at a stationary point about 14 % HIGHER in energy than the dynamic settle. It is a
  different metastable equilibrium, reached by the gravity-ramp path. From the dynamic shape it moves to a slightly lower
  energy. So the solver is not wrong about being stationary, but the continuation path picks a different equilibrium
  than the physical settle. This is the multiple-equilibria / path-dependence issue in the handoff. The 2-point DC
  criterion is the wrong test for it; an energy criterion would show the difference directly.
- Not tried: friction (the static solve has none; the dynamic path applies position-level friction to penetrating
  vertices only, probably small); slower gravity ramp; larger initial proximal term. Each is a separate experiment.

## Still unfinished (for the next agent)
- test_static.cpp implements only the drape-disc case. The garment tube, cantilever, warm-start-after-edit and
  path-dependence checks listed in its header are NOT written. The CMake entry exists; the file builds.
- The drape-coefficient check fails for silk and cotton (see the table above). Not fixed. The tolerance was not changed.
- Warm start works mechanically (66 Newton iterations from the dynamic shape, factorisation pattern reused), but it has
  not been measured against a cold start after an actual edit.
- ARCHITECTURE.md and cloth.hpp reference docs/agent_notes/S1_static_settle.md, which does not exist.
- Done-criteria status: (1) API present and builds: yes. (2) tests: partial (drape only, failing). (3) existing tests
  pass: yes, all five existing binaries, unchanged tolerances.
