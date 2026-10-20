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
  it also counts the chain's legs hanging beside the hook. For a first chain the usual sequence is `1` → `2` after the yarn over → `1`
  after the pull; a carried chain can start at 2 or 3 crossings. These counts are diagnostics; the "made" judge is the linking stretch below.
- Every trial logs a stage string, e.g. `[p1 p1 p1 p1 yo:2t caug:2t turn:2t pull:1t turn:1t snug:1t rele:1]` (`t` = the throat holds yarn).
- **SDF hook (default).** The open-groove SDF hook carries the yarn and the slip knot. Under the strict stitch judge the rig's first chain
  is linked and new only at `HOLD_T=0.50` and `0.51` (2/4); at `0.52` and `0.53` the half-plane count is one but `hookLoop` cannot close a
  stretch, so those are not counted as stitches. The older 4/4 result used the half-plane fallback.
- **The stitch judge is one new linked loop.** `loopsOnHook(front)` also sees the chain's legs hanging beside the hook; `hookLoop` (linking
  number 1 with the hook's axis) is the stitch. The final linking stretch must overlap the old loop's particles by less than half its
  length, and more yarn must be drawn from the hand. The crossing count after the yarn over is printed as a diagnostic but no longer decides
  the stitch: a leg can leave while a new loop is made, and the half-plane count can rise while the old loop stays. A chain line prints
  `(front count N)` when the counts disagree. A loose stitch that `loopRange` cannot close on itself is found by `hookLoop` before prepare
  gives up. The judge also prints the linking stretch's mean/max distance from the hook's axis (`r mean/max`). A stretch with mean radius
  above about `R + 3r` (11.1 mm) is a large loop around the hook's line, not a stitch on the shaft; this `snug` check is reported but not
  yet part of `made`, because the current motions do not keep it true.
- **Slip knot: five linked loops, two stitches on the shaft.** `HOLD_BELOW=12 HOLD_LATE=1 HOLD_T_LATE=0.05 YO_L=-15 YO_MODE=2 YO_ROLLMAX=1.2`,
  `--damp 15` (also `--chains 5`) makes five linked, mostly-new loops by the new-loop judge. The late rule holds the work 1 mm farther below
  and 0.05 s longer for chain 5 (13 mm, 0.55 s). The slip-knot finish pulls until the old loop's own yarn no longer winds round the shaft;
  the rig keeps the half-plane pull (below). Only this late setting has been run, so this is one point, not yet a range. The linking
  stretch is snug on the shaft only for chains 3 and 4 (mean 6.0 and 7.3 mm); chains 1, 2 and 5 link through large work loops (mean 15.1,
  19.5 and 24.5 mm). The render (`SIM_SHOTYARN=1`) shows loops piling around the hook, not a row of V stitches.
- **Rig: four linked loops, three on the shaft.** `SIM_RIG=1 PU_HOLDREL=0 HOLD_BELOW=9 YO_L=-5 YO_MODE=2 YO_ROLLMAX=1.2`, `--damp 15` makes
  chains 1, 3, 4 and 5 by the new-loop judge; chain 5 uses the late rule (11 mm, 0.55 s). Chain 2 ends with a linked stretch that is 61–65%
  the old loop's particles, so the old loop is not off and it is not counted. The rig's finish keeps the half-plane pull; the slip knot's
  old-winding pull breaks the rig's chain 5. The linking stretch is snug for chains 1, 3 and 4 (mean 3.5, 4.8, 7.1 mm); chain 5 is a large
  work loop (mean 15.9 mm) and is not a stitch on the shaft. The missing stitches are chain 2 (old loop) and chain 5 (loose).
- **Contact.** The three-chain slip test reaches a closest self-gap of 2.20 mm (yarn 2.20 mm) and 0.01 mm hook penetration; it passes.
  The five-chain runs are not clean: slip has a 2.15 mm gap and 1.38 mm hook penetration; the rig has 1.92 mm and 2.35 mm. The goal is no
  strand through itself or the hook; the five-chain runs still violate it. The 5-chain render shows the strands crossing and piling,
  consistent with the penetration numbers.
- **First chain / five-in-a-row.** By the new-loop judge, five from the slip knot at one setting and four from the rig. By the snug
  on-shaft check, two from the slip knot (chains 3, 4) and three from the rig (chains 1, 3, 4). Neither is five across a range, and the
  rig's first chain is linked only 2/4. A half-back head (flat only on the throat side) passes carry/slip but loses the first chain's yarn
  over (`yo:1` vs 2), so the full-width flat back is kept for now.
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

**Status after the SDF hook:** steps 1 and 2 are done. Step 3 is still open: the half-plane first-chain result is 4/4 only at damping 15, and
under the strict new-loop judge it is 2/4 even there. Step 4 has five linked slip-knot loops at one late-hand setting, but only two lie snug on
the shaft; the rig has four linked loops, three snug (chain 2 keeps its old loop, chain 5 is a large work loop). Step 5 has `testChains` for
three slip chains; it passes loops, self-gap (2.20 mm) and hook clearance (0.01 mm), but only its third linking stretch is snug on the shaft.
The five-chain runs still press into the hook (slip 1.38 mm, rig 2.35 mm), beyond what the three-chain test covers. The renders show loops
piling around the hook rather than a crochet chain, so this is not complete.

## 8. Tools in the CLI (`build/yarn_sim`)

- `--chains N` runs N chains after the slip knot; `SIM_RIG=1` starts from the clean loop rig instead.
- `SIM_PULLTEST="NAME=v1,v2;NAME2=..."` sweeps environment knobs over a whole chain (prepare + finish) from one saved start; prints the result
  and the stage log per combination. `SIM_PRECHAINS=n` runs n chains first (sweep knobs apply only to the trial).
- `SIM_JOBS=8` runs the combinations in parallel as child processes from that same saved start (default 1, capped at 16); the result lines
  stay in the serial order. `SIM_PULLTEST_VERBOSE=1` keeps each child's stage prints instead of sending them to `/dev/null`. A `SIM_HMU`
  sweep is applied to each child copy; other solver parameters are read only at startup.
- `SIM_STAGES=1` prints loop counts at every stage; `SIM_ROLLDBG=1` during the roll; `YO_DUMP=1` the yarn's path in the hook's frame.
  `SIM_CHAINDBG=1` prints the old and final linking stretches and their overlap for every judge.
- The fifth-chain hand: `HOLD_LATE` mm (default 2) and `HOLD_T_LATE` s (default 0.05) are added after four completed chains. The slip-knot
  five-chain run uses `HOLD_LATE=1`; the rig uses the default 2.
- Finish: the slip knot pulls until the old loop's own yarn no longer winds (`PU_OLDWIND`, default 0.5 turns); the rig keeps the half-plane
  pull (`sim.rig`). `PU_HOLDREL=1` keeps the throat holding 0.3 s before the turn-back.
- Pictures: `--every N` (snapshots), `SIM_FOLLOW=1` (camera follows the hook), `SIM_CHAINSHOT=1` (after each chain),
  `SIM_PHASESHOT=f` (at every phase change after frame f), `--close a-b/step` with `SIM_CFIX=1 SIM_CDIST=0.06` (close-ups).
  `SIM_SHOTYARN=1` frames all the yarn (not the origin); `SIM_SHOTDIST` and `SIM_SHOTTARGET=x,y,z` override the camera.
  Contact sheets: `python3 <scratch>/sheet.py dir kind` (or any PIL montage).
- Knobs are read with `envOr("NAME", default)` in `yarn_sim.cpp`. When a value proves itself, make it the default and remove the knob.

## 9. Pitfalls

- Don't trust a single run; sweep neighbours. Don't trust index-based loop tracking. Don't judge only from one camera angle.
- `zsh`: split `a:b:c` configs with `IFS=: read`, not `set --`.
- The CLI's slip knot uses `onFrame`; `buildLoopRig` settles before `onFrame` is set (no frames are recorded for it).
- Report honestly to the user: what was verified, what was not, with numbers and screenshots.
