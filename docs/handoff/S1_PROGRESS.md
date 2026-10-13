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
- test_drape: see the final results below.
- test_garment (pass) took 745 s wall on the shared machine.

## Still unfinished
- Explain the silk and cotton drape-coefficient gap: check the dynamic reference's final kinetic energy (is it settled?)
  and whether a longer dynamic run moves its DC toward the static value. Do not relax the tolerance.
- Commit and push the progress to date; the test_drape result is still pending.
- Nothing in this log has been verified for the done-criteria yet.
