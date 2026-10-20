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

- **Loop count** = `sim.loopsOnHook(front)`: signed crossings of the half-plane from the hook's axis toward the crocheter (`+z`). Expected
  sequence for a chain: `1` → `2` after the yarn over → `1` after the pull → `1` at the end, with more yarn drawn from the hand.
- Every trial logs a stage string, e.g. `[p1 p1 p1 p1 yo:2t caug:2t turn:2t pull:1t turn:1t snug:1t rele:1]` (`t` = the throat holds yarn).
- The first chain from the slip knot is made in many settings; the second and later ones are not reliable.
- **Key finding: the simulation is chaotic at damping 20–40.** Changing an irrelevant timing (`HOLD_T` 0.50 → 0.52) flips made/failed. At
  damping 60 results are repeatable, but the hook no longer catches the yarn. This, not the choice of motions, is the main problem.
- The yarn over is the fragile step. What is known:
  - The working yarn must cross behind the hook at the throat's level for a backward hook move to catch it; the finger should be behind,
    slightly *below* hook height, and moved along the hook until the crossing is at the throat (`YO_*` aiming loop, now damped).
  - The throat hold point (`throatPoint`, `throatX`) on the capsule hook lies on the axis, so rolling the hook just spins a caught yarn; an
    off-axis hold winds it but has not produced a second loop yet.
  - Pushing the hook tip-first to seat a loop spears the chain hanging below; slide the work along the hook instead (`SEAT_HAND=1`).
  - The finger must travel with the hook during the pull (`PU_FOLLOW`), and the throat must turn back the way it came (`PU_BACK=2`).

## 6. In progress when handed over: a real hook

`HookShape::sdf` (default **false**, so the passing slip knot keeps the capsule hook):
- `hookSdf`: round-ended shaft with a notch across the throat side down to the axis, its front face leaning back over the notch (the barb).
- `RodSystem::bodies` / `SdfBody` / `solveBodies`: SDF collider sampled along each segment (3 points), Coulomb friction relative to the body's
  motion, smooth motion over substeps.
- `hookMesh`: marching tetrahedra for drawing (CLI `SIM_HOOKSHOT=1 ./yarn_sim --out x` renders the hook alone).
- With `sdf = true` the slip knot and the carry test **fail**: the yarn does not stay in the new throat. The slip-knot script was tuned for
  the capsule hook. Next: make the carry test pass with the SDF hook (barb depth/lean `grooveA/grooveB/lipEnd`, `squash`, hook friction,
  throat point under the barb), then re-tune `makeSlipKnot`, then the chain.

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

## 8. Tools in the CLI (`build/yarn_sim`)

- `--chains N` runs N chains after the slip knot; `SIM_RIG=1` starts from the clean loop rig instead.
- `SIM_PULLTEST="NAME=v1,v2;NAME2=..."` sweeps environment knobs over a whole chain (prepare + finish) from one saved start; prints the result
  and the stage log per combination. `SIM_PRECHAINS=n` runs n chains first (sweep knobs apply only to the trial).
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
