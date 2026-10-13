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

* Build (Release, build_k2): compiles, 0 errors. contactAL off by default.
* ctest existing binaries, contactAL off: test_basics exit 0 (1 s), test_cantilever exit 0 (13 s), test_drape exit 0 (1681 s),
  test_garment exit 0 (206 s), test_sdf_grid exit 0 (43 s). 5 pass / 0 fail. (Run on a shared 2-core machine.)
* test_wedge: exit 0 but has no criteria, so it is not a test of the claim.

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

## Scene 1 (two overlapping ellipsoids), 240 frames, 672 vertices

* contactAL OFF (projection path): KE final 1.2e-5 J, mean KE last 60 frames 3.1e-5 J, max 1.4e-4 J. Line-search failures 0.
  Jitter regime is reproduced here (KE 1e-5 to 1e-4 J, matching the handoff's order of magnitude), but with zero line-search
  failures, so the handoff's "one line-search failure per frame" is not reproduced. Min clearance 1.649 mm, max stretch
  1.153 (final 1.065), hem 238.3 mm.
* contactAL ON: BLOWS UP. KE final 5.1 J, mean last 60 frames 5.8 J; line-search failures 1153 total, 307 in the last 60
  frames; min body clearance -54.4 mm (penetration); max stretch 194.7; hem 466 mm; max lambda 2.3e8 N.

## Result and stop

The AL option is NOT usable as implemented. Three attempts on the blow-up, all failing: default (rho = 1000 x local stiffness,
outer 3), outer 1, and rho scaled down to 100 (scene 0 frames 0-8). A central finite-difference check of the AL gradient
agrees with the analytic gradient to 0.3-0.5 percent, so the AL term's gradient is correct to first order. The line search
still rejects Newton directions every frame once contact starts. Plausible causes, NOT tested: (a) the Hessian drops the
curvature term -(lambda - rho g) Hess(phi) and the elastic/seam Hessian is not PSD in the tension term (tensionStiffness
option off), so the direction is not a descent direction; (b) the multiplier update (lambda <- max(0, lambda - rho g))
diverges because rho is set once per substep from the local stiffness, which can be far too small or large for the
contact geometry; (c) the energy and gradient are evaluated at a different point than the Hessian (lagged contactN_).
Next step for a stronger agent: test (a) by finite-differencing the full Hessian at a contact frame, and test (b) by
holding lambda at zero and checking that the path reduces to the projection-free penalty.

The off-versus-on criteria in K2_HANDOFF.md are NOT met: the on-case is far worse on KE, line-search failures and
penetration. Jitter is not reduced; it is made much worse. No claim of elimination.

test_wedge is registered in CMake and runs both scenes in both modes under ctest, but it has no pass or fail criteria yet
(only the finiteness check), so it passes trivially. Criteria are not coded.

## AL probes (scene 0, 8 frames unless stated; FL_AL_* env overrides in test_wedge)

* Default AL (stiffness 1000, outer 3): blows up from frame 3 (first contact). KE 1.05 J at frame 7 vs 2e-2 J off.
* Outer 1 (FL_AL_OUTER=1): still blows up, KE 3.9e-2 J at frame 8, line-search failure every frame.
* Stiffness 100 (FL_AL_STIFF=100): still blows up, KE 0.19 J at frame 8, line-search failure every frame.
* Gradient check (FL_AL_GRADCHECK=1, frame 3, first active contact): analytic gradient of the total AL energy vs central
  difference (eps 1e-7 m) agrees to about 0.3-0.5 percent at 4 contact vertices (e.g. x: 525.72 vs 524.17). So the sign
  and scale of the AL term are right. The residual is not yet attributed to finite-difference error or to the elastic
  or inertia part.

## Unfinished

* AL blow-up unresolved (see Result and stop). Wedge pass/fail criteria not coded in test_wedge.
* K3 (lagged friction) not started.
* Comparison off vs on, with the done-criteria in `K2_HANDOFF.md`.
* `docs/agent_notes/K2_contact_multipliers.md` (referenced from the code, missing).

## Unsure about

* The AL code has not been run before this session, so its correctness is unverified.
* Machine has 2 cores; the full ctest run takes longer than 10 minutes, so it is run in the background.
