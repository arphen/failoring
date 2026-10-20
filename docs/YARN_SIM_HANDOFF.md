# Yarn simulator: handoff plan

Read this first, then `docs/YARN_MODE_PLAN.md` section 14 (the process simulator) and `docs/YARN_SC_SWATCH_SPEC.md` (the target motions).

## 1. Goal (the user's words)

"A real simulator that pulls the yarn through loops and we can see the geometry as it really is in real life." One strand of yarn, never
passing through itself or the hook; the hook moves; the shape is whatever the physics makes. No placed geometry. The user explicitly chose
**free physics** (option 1) over a design-guided simulation, and wants it to look like the photographs (real hooks, real chains, real sc).

Order of work: chain stitch (repeatable, many in a row) -> single crochet into the chain -> turn -> rows -> wire the editor's stitch buttons to
the simulator. Today only the slip knot works end to end.

## 2. Rules of the repo (from the user)

- Branch `crochet/main`. Never merge into `main`. Never commit `godot/bin/libfailoring.dylib`.
- Run only tests tied to the change; long runs in the background. Don't kill/restart runner processes the user started.
- Commit with a plain-English message ending in `Co-Authored-By: ...` lines (see `git log`).
- Code style: match the surrounding code (long explanatory comments in plain English, `Real`, `Vec3`, `fl::` namespace).
- Judge results against pictures of the real thing, not only numbers; render and look (the CLI writes PNGs).
- Don't claim a stitch works unless the loop counting below says so across a *range* of settings.

## 3. Where things are

| What | Where |
| --- | --- |
| Rod solver (Cosserat XPBD, contact, friction, pins, statics, rings, SDF bodies) | `crochet_core/include/failoring/crochet/rod.hpp`, `crochet_core/src/crochet_rod.cpp` |
| Process simulator (hook, hand, grips, slip knot, chain) | `crochet_core/include/failoring/crochet/yarn_sim.hpp`, `crochet_core/src/yarn_sim.cpp` |
| CLI driver / experiment harness | `crochet_core/cli/yarn_sim.cpp` (binary `build/yarn_sim`) |
| Test | `crochet_core/tests/test_crochet_yarn_sim.cpp` (binary `build/test_crochet_yarn_sim`) |
| Godot binding (records a procedure, plays it back) | `gdextension/src/yarn_sim_bindings.{h,cpp}` (`YarnSimSession`) |
| Editor (Yarn mode; opens on the simulated slip knot) | `godot/crochet/yarn_view.gd` (`start_sim`, `_show_sim_frame`, `close_sim`) |
| Godot view check | `godot/tests/yarn_view_check.gd` (headless; must stay 0 failed) |

Build: `cd build && make yarn_sim test_crochet_yarn_sim test_crochet_rod`. Godot extension: `cmake --build build-gd -j8`.
Godot binary: `/Users/swozny/toolchain/godot-4.3-stable/Godot.app/Contents/MacOS/Godot`; `make run` starts the app.

## 4. What works

- **Slip knot** (`makeSlipKnot`): ring laid, left hand pinches the crossing, hook in through the ring, catches the working yarn in its throat,
  draws a bight out, swings round so the hook goes through it, snugs. Test passes: never closer than 0.98 yarn widths to itself, ≤ 0.2 mm into
  the hook, one loop on the shaft. Plays in the editor.
- Solver facts learned the hard way (keep them):
  - Yarn must be limp: `EI = 1e-8` N m² (stiffer and the hook cuts it).
  - The left hand is a local feed point (`End.hand`, `feedHand`): the last segment is free (`slack = 1e9`), particles are added/removed there.
  - Paid-out yarn must be straight at rest: `rs.restBendMemory = 0` (otherwise it curls into crumples).
  - 12 substeps × 4 iterations (fewer and strands squeezed against the hook pass through each other).
  - Rings (`SlideRing`) are applied *before* contact so they can't drag yarn through yarn.
  - The yarn slides through loops, so **particle indices do not identify a loop over time**. Count loops geometrically.

## 5. Chain stitch: current state

Code: `prepareChain` (find the loop, left hand grips the work below it, seat, snug, yarn over) and `finishChain` (turn throat down, pull through,
turn back, snug, release). `judgeChain` decides "made".

- **Stage loop count** = `sim.loopsOnHook(front)`: signed crossings of the half-plane from the hook's axis toward the crocheter (`+z`);
  it also counts the chain's legs hanging beside the hook. Expected sequence for a chain: `1` → `2` after the yarn over → `1` after the
  pull → `1` at the end, with more yarn drawn from the hand. The "made" judge uses the linking stretch below.
- Every trial logs a stage string, e.g. `[p1 p1 p1 p1 yo:2t caug:2t turn:2t pull:1t turn:1t snug:1t rele:1]` (`t` = the throat holds yarn).
- **SDF hook (default).** The open-groove SDF hook carries the yarn and the slip knot; `test_crochet_yarn_sim` passes. The first chain from
  `buildLoopRig` is made for `HOLD_T=0.50..0.53` at damping 15 (4/4), also with `SIM_SEAT=9`.
- **Two chains from the slip knot.** The first is made at `HOLD_BELOW=12, YO_L=-15` (one point; `SIM_SEAT` 5, 7 and 9 all make it). The
  second is made across a range of the local hand placement: `YO_RADL` 6–12 mm, `YO_L2` −10…+5 mm. After a chain the finger goes close in
  on the far side of the shaft from the old loop's back end, so the yarn crosses the shaft at the throat whatever way the hook has rolled;
  the defaults are 10 mm and −5 mm.
- **The stitch judge is the linking stretch.** `loopsOnHook(front)` also sees the chain's legs hanging beside the hook; `hookLoop` (linking
  number 1 with the hook's axis) is the stitch. A chain line prints `(front count N)` when the two disagree. A loose stitch that
  `loopRange` cannot close on itself is found by `hookLoop` before prepare gives up.
- **Chain 3 is not made.** With the default finish the old loop ends in front of the tip; with `PU_BACK=0` it stays on the shaft at
  7–14 mm, but the yarn over adds no loop (`yo:1`). The working yarn crosses the throat level but comes within 2.0 mm of the throat point;
  raising `YO_CATCHR` to 2.2 makes the throat hold it, yet the roll still does not add a loop. Longer rolls, the opposite roll and hook
  friction 0.05–0.5 did not fix it. One trial at catch 2.2, roll 2.4 turns, roll +1 printed `MADE`, but it ended with three half-plane
  loops; that point is not trusted.
- **Five in a row.** The first two; chain 3 blocks. A half-back head (flat only on the throat side) passes carry/slip but loses the first
  chain's yarn over (`yo:1` vs 2), so the full-width flat back is kept for now.
- **Key finding: the simulation is chaotic at damping 20–40.** Changing an irrelevant timing (`HOLD_T` 0.50 → 0.52) flips made/failed. At
  damping 60 results are repeatable, but the hook no longer catches the yarn. This, not the choice of motions, is the main problem.
- The yarn over is the fragile step. What is known:
  - The working yarn must cross behind the hook at the throat's level for a backward hook move to catch it; the finger should be behind,
    slightly *below* hook height, and moved along the hook until the crossing is at the throat (`YO_*` aiming loop, now damped).
  - The throat hold point (`throatPoint`, `throatX`) on the capsule hook lies on the axis, so rolling the hook just spins a caught yarn; an
    off-axis hold winds it but has not produced a second loop yet.
  - Pushing the hook tip-first to seat a loop spears the chain hanging below; slide the work along the hook instead (`SEAT_HAND=1`).
  - The finger must travel with the hook during the pull (`PU_FOLLOW`), and the throat must turn back the way it came (`PU_BACK=2`).

## 6. The SDF hook

`HookShape::sdf` is **true** by default. The hook is one smooth body:
- `hookSdf`: a round head from the tip to a flat back at `z=2.05R`, a full-width barb along that back edge (`x=0.85R`, `y=±1R`, `z=2R`,
  radius `0.20R`), a neck on the far side (`x=-0.50R`, from `z=2.05R` to `4.2R`), a three-capsule taper and the shaft. The groove is open
  toward `+x`; the yarn is laid in from the mouth and held between the neck and the barb.
- `RodSystem::bodies` / `SdfBody` / `solveBodies`: SDF collider sampled along each segment (3 points), Coulomb friction relative to the body's
  motion, smooth motion over substeps.
- `hookMesh`: marching tetrahedra for drawing (CLI `SIM_HOOKSHOT=1 ./yarn_sim --out x` renders the hook alone).
- `SIM_SDF=0 ./yarn_sim ...` switches back to the capsule hook for comparison; in code, `sim.shape.sdf = false`.
- The carry test and the slip knot pass with `sdf=true`; `test_crochet_yarn_sim` reports the yarn staying in the throat over the 25 mm pull
  and the slip knot leaving one loop round the shaft.
- The flat back that catches the first yarn over also keeps an old loop from sliding off after a stitch. A half-back head (rounded on the far
  side) lets the loop go but loses the first yarn over, so the full-width back is kept until the chain motions can work around it (section 5).

## 7. Plan, step by step

1. **SDF hook carries yarn.** `SIM_CARRY=1 ./yarn_sim` with `sdf=true` until it reports the yarn staying in the throat over a 25 mm pull, for a
   range of barb leans and frictions. Render it. Then make the test pass with `sdf=true` and flip the default.
2. **Slip knot on the SDF hook.** Re-tune `makeSlipKnot` (it uses the throat for the catch). Test must pass; screenshot in Godot
   (`FL_SHOT_SIM=574 FL_SHOT_YARN=ch0 ... res://tests/crochet_shot.tscn`).
3. **Make the simulation deterministic before tuning motions.** Measure chaos with
   `SIM_RIG=1 HOLD_BELOW=9 SIM_PULLTEST="HOLD_T=0.50,0.51,0.52,0.53" ./yarn_sim --out /tmp/x --every 0 --damp D`. A setup is only usable when
   these four agree. Levers to try: damping, mass scale (`--mass`), substeps/iterations (`SIM_SUB`, `SIM_IT`), hook friction (`SIM_HMU`),
   yarn–yarn friction, slower hook motions, and a stronger, physically motivated catch (the barb).
4. **Chain from the rig, then from the slip knot.** `buildLoopRig` gives a clean single loop with the yarn over the finger. Get one chain made
   repeatably (same answer over a sweep of 3–4 neighbouring values for each main knob), then 5 in a row. Then from the slip knot.
5. **Test.** Add `testChains` to `test_crochet_yarn_sim.cpp`: 3 chains from the slip knot, each `1 → 2 → 1` loops, yarn drawn from the hand,
   no strand closer than 0.95 × yarn width, ≤ 0.4 mm into the hook. Keep the runtime reasonable (now ~20 s for 5 chains; a test should be a
   few seconds; consider fewer frames per move).
6. **Editor.** Extend `YarnSimSession::record` with `"chain"` (and a count); add a button or make the Chain button play the simulated chain.
   Keep `yarn_view_check.gd` at 0 failed.
7. **Single crochet** into the 2nd chain from the hook (insert, yarn over, pull up a loop → 2 loops, yarn over, pull through both → 1),
   following `docs/YARN_SC_SWATCH_SPEC.md`. Same loop-count stages.

**Status after the SDF hook:** steps 1 and 2 are done; step 3 has a 4/4 result only at damping 15; step 4 has the first chain from the rig
and two chains from the slip knot, but not three in a row; step 5 is not added.

## 8. Tools in the CLI (`build/yarn_sim`)

- `--chains N` runs N chains after the slip knot; `SIM_RIG=1` starts from the clean loop rig instead.
- `SIM_PULLTEST="NAME=v1,v2;NAME2=..."` sweeps environment knobs over a whole chain (prepare + finish) from one saved start; prints the result
  and the stage log per combination. `SIM_PRECHAINS=n` runs n chains first (sweep knobs apply only to the trial).
- `SIM_JOBS=8` runs the combinations in parallel as child processes from that same saved start (default 1, capped at 16); the result lines
  stay in the serial order. `SIM_PULLTEST_VERBOSE=1` keeps each child's stage prints instead of sending them to `/dev/null`. A `SIM_HMU`
  sweep is applied to each child copy; other solver parameters are read only at startup.
- `SIM_STAGES=1` prints loop counts at every stage; `SIM_ROLLDBG=1` during the roll; `YO_DUMP=1` the yarn's path in the hook's frame.
- Pictures: `--every N` (snapshots), `SIM_FOLLOW=1` (camera follows the hook), `SIM_CHAINSHOT=1` (after each chain),
  `SIM_PHASESHOT=f` (at every phase change after frame f), `--close a-b/step` with `SIM_CFIX=1 SIM_CDIST=0.06` (close-ups).
  Contact sheets: `python3 <scratch>/sheet.py dir kind` (or any PIL montage).
- Knobs are read with `envOr("NAME", default)` in `yarn_sim.cpp`. When a value proves itself, make it the default and remove the knob.

## 9. Pitfalls

- Don't trust a single run; sweep neighbours. Don't trust index-based loop tracking. Don't judge only from one camera angle.
- `zsh`: split `a:b:c` configs with `IFS=: read`, not `set --`.
- The CLI's slip knot uses `onFrame`; `buildLoopRig` settles before `onFrame` is set (no frames are recorded for it).
- Report honestly to the user: what was verified, what was not, with numbers and screenshots.
