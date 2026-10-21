# Yarn simulator: handoff plan

Read this first, then `docs/YARN_MODE_PLAN.md` section 14 (the process simulator) and `docs/YARN_SC_SWATCH_SPEC.md` (the target motions).

## 1. Goal (the user's words)

"A real simulator that pulls the yarn through loops and we can see the geometry as it really is in real life." One strand of yarn, never
passing through itself or the hook; the hook moves; the shape is whatever the physics makes. No placed geometry. The user explicitly chose
**free physics** (option 1) over a design-guided simulation, and wants it to look like the photographs (real hooks, real chains, real sc).

Order of work: chain stitch (repeatable, many in a row) -> single crochet into the chain -> turn -> rows -> wire the editor's stitch buttons to
the simulator. The slip knot works end to end. The three-chain loop/contact test passes under the strict judge and the row check. The rig's
default makes four at `HOLD_BELOW` 9/10; a shorter work slide (`SIM_SEAT=4`) makes five at 10 only. The chain row is not yet a crochet chain:
the five-loop render is still a tangle at the hook, and the contact fails (section 5).

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
- **SDF hook (default).** The open-groove SDF hook carries the yarn and the slip knot. On the older `HOLD_BELOW=9` first-chain grid, under
  the strict stitch judge the rig's first chain is linked and new only at `HOLD_T=0.50` and `0.51` (2/4); at `0.52` and `0.53` the half-plane
  count is one but `hookLoop` cannot close a stretch, so those are not counted as stitches. The older 4/4 result used the half-plane fallback.
  The rig setting below uses `HOLD_BELOW=10`, where the first chain is deterministic and linked.
- **The stitch judge is one new linked loop.** `loopsOnHook(front)` also sees the chain's legs hanging beside the hook; `hookLoop` (linking
  number 1 with the hook's axis) is the stitch. The final linking stretch must overlap the old loop's particles by less than half its
  length, and more yarn must be drawn from the hand. The crossing count after the yarn over is printed as a diagnostic but no longer decides
  the stitch: a leg can leave while a new loop is made, and the half-plane count can rise while the old loop stays. A chain line prints
  `(front count N)` when the counts disagree. A linked stretch counts as one loop whichever way it winds; the sign of the front count is a
  diagnostic, not a reason by itself to reject a stitch (the rig's fifth attempt at `HOLD_BELOW=10` has `front -1` but is rejected for
  relinking, not for the sign). A loose
  stitch that `loopRange` cannot close on itself is found by `hookLoop` before prepare
  gives up. An earlier completed chain loop can pass that old-loop test when it is not the *immediate* old loop. `made` therefore also
  requires the linking stretch to reach the yarn drawn after the old loop (`hi > oldHi + 1`; `newYarn` in the result). This rejects a final
  stretch lying entirely in earlier work, but not one that starts on an earlier bight and reaches past its own old loop. `made` also
  requires `sequential`: the final stretch must end at or after the last valid chain's final particle (`lastLinkHi`). The rig's fifth at
  `HOLD_BELOW=10` passes `newYarn` (307 > 269) but stops at 307 while the fourth ends at 379; it is a relink, not a fifth stitch. The
  final stretch's start/end are in the result (`linkLo`/`linkHi`). The old loop's own yarn outside the final stretch is measured as
  `oldWind` (turns). It reads -0.1..0.1 in the rig, so the false fifth's extra winding loop is the lower work, not the old loop still on the
  hook. This does not prove that the working loop is on the shaft, and whether
  a working loop that stays open should count is still unresolved. The judge also prints the linking
  stretch's mean/max distance from the hook's axis (`r mean/max`). A stretch with mean radius above about `R + 3r` (11.1 mm) is a large
  loop around the hook's line, not a stitch on the shaft; this `snug` check is reported but not yet part of `made`, because the current
  motions do not keep it true.
- **Slip knot: five linked loops under the old judge, three on new yarn.** `HOLD_BELOW=12 HOLD_LATE=1 HOLD_T_LATE=0.05 YO_L=-15 YO_MODE=2
  YO_ROLLMAX=1.2`, `--damp 15`: with the default late throat release the original new-loop judge still makes all five. The stricter
  new-yarn check passes chains 1, 3 and 4: chain 2 links an earlier work loop (final 106..242, old loop at 271..280) and chain 5 likewise
  (112..265 vs 391..400). An earlier completed loop is a linked loop but not a row stitch: the row check rejects a final stretch that stops
  before the last valid stitch's end. The slip five has not been re-run with the row check. Chain 1 is a large new loop (mean 15.1 mm);
  chains 3 and 4 are on the shaft
  (mean 6.0 and 7.3 mm). With `PU_RELEASE_LATE=0` the throat lets go before the snug: the original judge makes five, the new-yarn check
  chains 1–4, but every loop is a large work loop (mean 15.3, 19.0, 21.5, 21.7 mm) and none is snug. The late rule holds the work 1 mm farther below and 0.05 s longer for chain 5 (13
  mm, 0.55 s). Only these two release settings have been run, so neither is a range. The five-attempt render (`SIM_SHOTYARN=1`) still shows
  loops piling around the hook, not a row of V stitches.
- **Rig: four across the hold range; a narrow five at `HOLD_BELOW=10`.** The default command `SIM_RIG=1 PU_HOLDREL=0 PU_FIX=1
  PU_RELEASE_LATE=0 HOLD_BELOW=10 YO_L=-5 YO_MODE=2 YO_ROLLMAX=1.2 ./yarn_sim --chains 5 --damp 15` (work slide `SIM_SEAT=5`) makes
  chains 1–4, finals 122..141, 164..179, 208..229 and 322..379, means 4.0, 3.1, 4.5 and 4.6 mm (max 7.4 mm), all inside the `R + 3r =
  11.1 mm` snug bound. The fifth is a relink of an earlier bight (old 240..269, final 217..307, which stops before the fourth's 379); the
  row check rejects it. The old `newYarn`-only judge accepted it because 307 > 269, which is why the earlier count said five. The first four
  are deterministic; the five-attempt output is identical at `PU_FIXMAX` 40, 50 and 60; 30 stops chain 2 on the old loop (85%).
  `HOLD_LATE=1` changes the fifth (final 213..294, mean 13.4 mm) but it still stops before the fourth; 3 and 4 give no fifth (0 and -1 loops).
  `PU_BACK=1` makes two at 10 and three at 9. The default late hold and turn-back path are the best of those ranges.
  At `HOLD_BELOW=9` chains 1–3 and 5 are sequential (the fourth, final 178..274, stops before the third's 281); 8, 9.5, 11 and 12 lose the
  first chain. A sixth attempt at 9 takes the fifth's own loop (old 78%) and is not made. So the default range is four at both 9 and 10.
  The work slide is the one motion lever that makes a five: with `SIM_SEAT=4` at `HOLD_BELOW=10`, chains 1–5 are all made and sequential, finals
  122..141, 164..179, 207..227, 251..268 and 298..311, means 4.0, 3.1, 2.7, 4.4 and 3.0 mm (max 6.5 mm), all snug. The repeat is identical,
  and `PU_FIXMAX` 40/50/60 are identical. It is a one-setting window, not a range: `SIM_SEAT` 3 makes two, 4.25 three, 4.5, 5 and 6 four,
  and 7 three; at `HOLD_BELOW=9`, 4 makes only chain 1 (chain 2 final 156..199, new 0, old 57%). The five is a motion result, not the old
  `hookLoop` fallback: the shorter slide (10 mm clearance instead of 12.5) leaves the working loop on the shaft. The default four at 9/10
  is still the only range. The `PU_FIX` only changes the final link when it is still the old loop; the shorter slide, not the fix, is
  what leaves the working loop on the shaft. The rig's finish keeps the half-plane pull; the slip knot's old-winding pull breaks the rig's
  chain 5.
- **Contact.** The three-chain slip test (now with `PU_RELEASE_LATE=0`) reaches a closest self-gap of 2.20 mm (yarn 2.20 mm) and 0.00 mm
  hook penetration; it passes. The five-attempt rig run at `HOLD_BELOW=10` (chains 1–4 sequential, the fifth a relink) reaches 1.68 mm
  (below the 0.95 × 2.20 mm = 2.09 mm limit) and
  2.18 mm into the hook. `SIM_CONTACT=1` locates them: the 1.68 mm gap is at frame 3670 in chain 4's snug; the 2.18 mm hook contact is in
  chain 3's yarn over (frame 2599 in the three-chain run, segment 297 — the carried finger path pressing the working yarn into the head), not
  the rig start. The three-chain rig run's 1.93 mm gap is chain 2's pull-through (frame 2187). The loops are held at the flat back/barb and
  the work slide bunches them; that is why the row is a knot. At `HOLD_BELOW=9` the five-attempt rig run is 1.92 mm and 2.30 mm. The slip
  five-attempt runs were 2.15 mm / 1.38 mm late and 2.05 mm / 0.06 mm early.
  The `SIM_SEAT=4` five is not clean either: 1.93 mm self-gap at chain 2's pull and 1.73 mm into the hook at chain 3's yarn over. The
  shorter slide improves the hook contact (2.18 -> 1.73 mm) but not the self-gap; both still fail (the limit is 0.95 x 2.20 = 2.09 mm self,
  0 mm in the hook). `SIM_SEAT=4.25` makes three and reaches 1.85 mm / 2.79 mm; 4.5 is the default four. The five's whole-work render
  (`SIM_SHOTYARN=1`) shows one small loop above a long tail and the work bunched at the hook, not five distinct V stitches: the five finals
  sit on top of each other at the head.
  Attempts to move the contact did not keep five sequential: `YO_RADL=14` keeps four sequential and all snug (4.0, 4.5, 7.5, 6.9 mm; the
  fifth final 278..313 lies inside the fourth's 257..319 and stops at 313 < 319) but drops the self-gap to 1.07 mm; `16` keeps three (gap
  1.88, 0.73 mm in); `12` fails after chain 3. A larger contact radius (`SIM_RSCALE=1.05`) loses the
  first chain, `1.1` makes one and `1.15` three. A slower pull (`PU_V=8`) makes three (gap 2.07, 1.73 mm in). A longer or shorter snug
  (`PU_SNUGT=0.7` or `0.3`) loses chains. Raising the solver (`SIM_IT=6`, `SIM_SUB=16`) loses the rig's first chain. So the row is a
  knife-edge: four at both 9 and 10, but no tested contact lever keeps four and a clean gap. The goal is no strand through itself or the
  hook; the rig runs still violate it. The five-attempt render at `HOLD_BELOW=10` shows a compact knot at the hook, not
  a row of V stitches, consistent with the contact numbers.
- **Row-motion results.** The work slide at `SIM_SEAT=4` is the only motion lever that has made five (above, HB10 only); it is a one-setting
  window, not a range. The other attempts did not hold. Seating the last stitch's closed `hookLoop` whenever the winding loop stopped
  before it made a five-sequential run at `HOLD_BELOW=10` (finals 122..141, 164..179, 316..334, 362..390, 414..444), but the row moved
  forward and contact fell to 1.01 mm self-gap / 1.70 mm into the hook. It is not a range: with a `2R` compactness bound the fifth was
  359..431, overlapped its old 369..406 by 52%, and was not made (contact 1.93 mm / 3.00 mm); at `HOLD_BELOW=9` the same fallback seats the
  11.1 mm chain-2 loop for chain 3 and the yarn over fails (0 loops). Restoring the loop found before the grip after the snug drifted
  (349..375) also fails (0 loops). A smaller barb (tried at 0.75 and 0.5 of the drawn lip) loses the first chain (0 and 1 chains made at
  HB10). The full-width flat back and full barb remain required. The default four at 10 and 9 is still the best range; the seat4 five is a
  narrow HB10 exception, not a replacement.
- **First chain / the row.** The default rig makes four sequential at `HOLD_BELOW=10` (chains 1–4) and four at 9 (chains 1–3 and 5); the
  `SIM_SEAT=4` five at 10 is the only five. The default fifth at 10 and fourth at 9 are relinks of an earlier bight, not stitches; a sixth
  attempt at 9 is the fifth's own loop (old 78%) and is not made. The first chain is deterministic at 10; the default four does not survive
  `HOLD_BELOW` 8, 9.5, 11 and 12 (the first chain loses its yarn over or its linked stretch), so the row is a narrow setting, not a range
  across the start height. The seat4 five does not survive 9 or adjacent slides (4.25/4.5). It also does not survive a larger contact
  radius or a slower pull (above). The slip knot's three-chain test is green under the row check; the slip five is still three (late) or
  four (early) under the new-yarn judge (the row check not re-run). A half-back head (flat only on the throat side) passes carry/slip but
  loses the first chain's yarn over (`yo:1` vs 2), so the full-width flat back is kept for now.
- **Adaptive pull and mixed finish (off by default).** `PU_HALF_CHAINS=2` uses the half-plane finish for the slip knot's first two chains
  (the rest keep old-winding). Without more pull, chain 3 ends on the old loop; with `PU_FIX=1` the finish keeps drawing until the final
  stretch is no longer the old loop, and the half-first-two run gives three made chains, but chain 1 (mean 11.4 mm) and chain 3 (mean
  12.4 mm) are large new work loops, not stitches on the shaft; only chain 2 (3.4 mm) is snug. At early release without the pull the
  old-winding slip path gives three made chains but all three loops are large (15.3, 19.0, 21.5 mm). Freeing the old loop this way does not
  put the working loop on the shaft. Both knobs default off.
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
under the strict new-loop judge it is 2/4 even there; the rig four-chain setting is deterministic once the start height is fixed, but the
start height range is narrow (9–10 mm) and the higher-solver variants lose the first chain. Step 4 has four-in-a-row: the rig, with
`PU_FIX=1`, `PU_RELEASE_LATE=0` and `HOLD_BELOW=10`, makes four new sequential linked loops, all snug (4.0, 3.1, 4.5, 4.6 mm), across
`PU_FIXMAX` 40/50/60. The fifth attempt relinks an earlier bight (217..307 before the fourth's 322..379); at `HOLD_BELOW=9` the fourth
(178..274) stops before the third (213..281) and the fifth is real. A sixth attempt at 9 is the fifth's own loop (78% old) and is not made.
The old five count was these relinks; the row check now rejects them. The slip knot still gives three (late) or four (early) by the strict
new-yarn judge. Step 5 has `testChains` for three slip chains. With `PU_RELEASE_LATE=0` it passes the strict new-loop/new-yarn and row
checks on all three chains and passes self-gap (2.20 mm) and hook clearance (0.00 mm), but all three working loops are large (mean 15.3, 19.0,
21.5 mm), so the test is a loop/contact regression, not a picture-valid chain. The rig four is not clean either: self-gap 1.68 mm, 2.18 mm
into the hook, and the render is still a compact knot at the hook rather than a chain of V stitches. Four at `HOLD_BELOW=10` pass the loop
and row judge by default; the fifth attempt, the picture and the contact do not. A shorter work slide (`SIM_SEAT=4`) makes five at 10,
repeatably and over `PU_FIXMAX` 40/50/60, but not at 9 or at adjacent slides, and its render is still a tangle. The next problem is the
row advance/contact, not another loop-count knob. Step 7 has not started.

## 8. Tools in the CLI (`build/yarn_sim`)

- `--chains N` runs N chains after the slip knot; `SIM_RIG=1` starts from the clean loop rig instead.
- `SIM_PULLTEST="NAME=v1,v2;NAME2=..."` sweeps environment knobs over a whole chain (prepare + finish) from one saved start; prints the result
  and the stage log per combination. `SIM_PRECHAINS=n` runs n chains first (sweep knobs apply only to the trial).
- `SIM_JOBS=8` runs the combinations in parallel as child processes from that same saved start (default 1, capped at 16); the result lines
  stay in the serial order. `SIM_PULLTEST_VERBOSE=1` keeps each child's stage prints instead of sending them to `/dev/null`. A `SIM_HMU`
  sweep is applied to each child copy; other solver parameters are read only at startup.
- `SIM_STAGES=1` prints loop counts at every stage; `SIM_ROLLDBG=1` during the roll; `YO_DUMP=1` the yarn's path in the hook's frame.
  `SIM_CHAINDBG=1` prints the old and final linking stretches, their overlap, the `newYarn` and `sequential` flags, the previous end, the
  mean/max radius and the old loop's own winding outside the final stretch (`oldWind`, turns) for every judge.
  `SIM_CONTACT=1` prints the phase, frame, segment and particle indices of each new worst self-gap and hook penetration; `SIM_RSCALE=x` sets
  the contact radius scale after the build (the drawn radius stays `--rad`; 1.0 is the physical radius).
- The later-chain hand: `HOLD_LATE` mm (default 2) and `HOLD_T_LATE` s (default 0.05) are added after four completed chains. The slip-knot
  five-attempt run uses `HOLD_LATE=1`; the rig uses the default 2.
- Finish: the slip knot pulls until the old loop's own yarn no longer winds (`PU_OLDWIND`, default 0.5 turns); the rig keeps the half-plane
  pull (`sim.rig`). `PU_HOLDREL=1` keeps the throat holding 0.3 s before the turn-back. `PU_RELEASE_LATE=1` (default) drops the throat ring
  after the snug; `0` drops it before, which changes which stretch is the final link (the three-chain test uses 0). `PU_HALF_CHAINS=N` uses
  the half-plane finish for the slip knot's first N chains (default 0; the rig is always half-plane). `PU_FIX=1` keeps drawing after the main
  pull while the final stretch is still the old loop, up to `PU_FIXMAX` mm (default 40); at `HOLD_BELOW=10` 40/50/60 all keep four while
  30 stops chain 2 on the old loop. On the slip knot it still leaves chain 5 on earlier work. The
  five-attempt command is: `SIM_RIG=1 PU_HOLDREL=0 PU_FIX=1 PU_RELEASE_LATE=0 HOLD_BELOW=10 YO_L=-5 YO_MODE=2 YO_ROLLMAX=1.2
  ./yarn_sim --chains 5 --damp 15`.
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
