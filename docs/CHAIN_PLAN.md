# Chain stitch: the plan to finish it

Read this first. `docs/YARN_SIM_HANDOFF.md` is the background (what was tried, the solver facts, the tools); this file is the
order of work, the rules, and the gates. When the two disagree, this file wins.

## 1. The goal and what "done" means

A row of chain stitches made by free physics (the hook moves, the left hand holds and feeds, the yarn does what the solver makes it do;
no placed geometry), starting from the slip knot, that looks like a real chain and plays in the editor.

Done is all of these, at the default settings, and still true when each main knob moves one step either way (section 4.3):

1. `yarn_sim --chains 10` from the slip knot makes 10 stitches, each passing the judge in section 4.
2. The render reads as a chain: a row of V shapes on the front face, each loop about one hook diameter wide, a ridge of bumps on the back,
   one working loop sitting on the hook's full shaft. Not a knot or a tangle at the head.
3. Contact: the yarn is never closer to itself than 0.95 yarn widths, and never more than 0.4 mm into the hook.
4. `test_crochet_yarn_sim` is green with a chain test (3 chains is enough for the test; keep it to a few seconds).
5. The editor's Chain button plays the simulated chain; `godot/tests/yarn_view_check.gd` reports 0 failed.

## 2. Why the last run stalled (do not repeat it)

- **Physics.** Every loop the hook makes is about 16 mm of yarn. A loop that sits on the 2.5 mm shaft needs at least 20.4 mm of
  centreline. The hook only reaches its full radius 13 mm behind the tip (`hookSdfRaw`: head to `z=2.05R`, a 0.5R neck capsule at
  `x=-0.5R` from 2.05R to 4.2R, a taper to 5.4R). So loops settle on the skinny neck, their opening is narrower than the next head and
  bight, and the next stitch goes beside the old loop instead of through it. This is a geometry problem; motion levers cannot fix it.
- **Method.** The run added about 30 environment levers, one per idea, all left off by default, and changed the judge as it went. It
  tuned a lever until one run passed, then found the pass was a measuring artefact. Section 3 forbids this.

## 3. Rules (these override your own judgement)

From the repo owner:
- Branch `crochet/main`. Never merge into `main`. Never commit `godot/bin/libfailoring.dylib`.
- Run only the tests tied to the change. Run long grids in the background. Don't kill processes you didn't start.
- Commit messages: plain English, ending with the `Co-Authored-By:` line the session gives you. Match the code style around you (long
  plain-English comments, `Real`, `Vec3`, namespace `fl`).

For this plan:
- **The judge is frozen after Phase 0.** Never loosen, reword or add a fallback to `judgeStitch` or the harness verdict. If you think the
  judge is wrong, stop and write down why (section 6). Do not work around it.
- **One hypothesis, one lever.** Before an experiment, write one line in the run log (`build/chain_runs/LOG.md`): the hypothesis, the
  single change, and the result that would disprove it. If the lever doesn't move the verdict toward pass, delete it in the same
  session. Don't leave dead levers off by default.
- **Budgets.** Each phase has an experiment budget: a run of the harness grid counts as one experiment. When the budget is spent and the
  gate isn't met, follow section 6 and stop that phase. Don't start a side quest.
- **Look before you claim.** A gate counts as passed only after you have opened the PNGs (front, oblique, top) and described in the log
  what you see, compared with the picture in section 1.2. Numbers alone never pass a gate.
- **Renders go in `build/chain_runs/<run>/`** (ignored by git), never the repo root.
- **Commit at every passed gate,** with the numbers and the file names of the renders in the message body.

How to write the log, the commits, the handoff and your final message:
- Say what happened and what changed, in positive statements: "chain 2's new loop lies beside the old one (`through` 0)", not "chain 2
  is not linked through".
- No double negatives ("not unlinked", "doesn't fail to"). No lists of things you left alone ("no defaults were changed", "nothing else
  was touched", "the judge was not loosened"). The diff shows what changed; say only that.
- A failed experiment is one line: hypothesis, change, result with the number, deleted or kept. Don't make it sound like progress or
  reassurance.
- No celebrating or validating. If nothing moved, say so in one sentence ("Phase 2: 30/30 spent, no stitch passes `onShaft`; stuck, see
  below") and stop. Keep the final message short: the gate reached, the numbers, the render paths, the next decision.

## 4. Phase 0: clean up, freeze the judge, build the harness (budget: no experiments, just code)

### 4.1 Clean up
1. Commit the current uncommitted work in `yarn_sim.cpp`, `yarn_sim.hpp`, `cli/yarn_sim.cpp` and `YARN_SIM_HANDOFF.md` as one commit:
   "record the slip-gauge/full-ring/previous-loop experiments (all off by default)". Do not stage the dylib.
2. Delete the 279 `sim_*.png` files from the repo root (they are untracked experiment output) and the stray `.DS_Store`.
   Leave the untracked `Makefile` alone: it's the user's `make run`.

### 4.2 `judgeStitch` (new; leave `judgeChain` alone for the old test)
Add `StitchVerdict judgeStitch(const YarnSim& sim, const ChainStart& c)` next to `judgeChain`. It must not depend on how a loop is closed
with a chord. It passes only if every one of these holds:

| Check | Definition | Pass |
| --- | --- | --- |
| `onHook` | `hookLoop` finds the final loop L (linking number 1 with the hook's axis) | found |
| `fresh` | particles of L shared with the old loop / length of L | < 0.25 |
| `newYarn`, `sequential` | as in `ChainResult` today | true |
| `oldOff` | the old loop's own winding round the hook's axis (`oldWind`) | < 0.25 turns |
| `through` | build the old loop's spanning disc: its particles as a polygon, fanned from its centroid. Count segments of the yarn from the old loop's end (`oldHi`) to L's end that cross the disc. | ≥ 1 |
| `onShaft` | L's mean distance from the axis, and L's length | ≥ R + 0.6·r (3.2 mm) and ≥ 19 mm |
| `contact` | closest non-neighbour self-gap over the whole stitch; deepest point into the hook | ≥ 0.95·2r; ≤ 0.4 mm |

Print one line per stitch: `stitch k PASS|FAIL onHook fresh=.. newYarn sequential oldOff=.. through=n onShaft=r/len contact=gap/depth`.
Keep the existing `inOld` / `crossClean` prints as diagnostics only; they never decide.

Test the judge before using it. It must say FAIL with `through` = 0 on the `CH_PREV=2` chain 2 described in the handoff (new loop beside
the old one), and FAIL on `onShaft` for today's 16 mm loops. Write those two runs in the log. If the judge passes either, fix it now:
this is the only phase in which the judge may change.

### 4.3 The harness
Add `tools/chain_check.sh <run-name> <chains> [rig|slip] [extra env...]`. It runs `build/yarn_sim --chains N` over a fixed grid, in
parallel with `SIM_JOBS`, writing to `build/chain_runs/<run-name>/<grid-point>/`. The grid is the baseline plus one step either way on
each main knob, one knob at a time:
- `HOLD_BELOW` baseline ±1 mm
- `--damp` baseline ±3
- `SIM_SEAT` baseline ±1

That is 7 points. It prints a table: grid point × stitch → PASS/FAIL, and the count of stitches passed in a row from the first.
**A setting works when all 7 points pass.** At the end it prints the paths of the final front/oblique/top PNGs for the baseline point.

### 4.4 Stop re-simulating what is already known
Measured on 2026-10-27 (Release build): the slip knot is 684 frames and takes 14 s; each chain takes about 10 s. Every run rebuilds the
slip knot and every earlier chain, so trying stitch 5 costs about 64 s, of which 54 s repeats known work.

1. **Checkpoints.** Add `--save <file>` and `--load <file>` to `yarn_sim`. They write and read the whole `YarnSim` state: the rod
   system (positions, velocities, rest data, inverse masses, frozen masses), ends and hand, hook pose and shape, grips and rings,
   `chains`, `lastLink*`, and frame. Leave `onFrame` out. Save after the slip knot (`--save-after knot`) and after each stitch that passes
   (`--save-after k`). The header stores the git hash, the `HookShape` and `YarnSimParams` values, and the env knobs set. `--load` refuses
   a file whose header doesn't match the current build and settings, so a stale checkpoint can't be used.
2. **Prove it is exact.** Run knot plus 2 chains straight through. Then run knot, save, load, and the same 2 chains. The final particle
   positions must match bit for bit. If they don't, find the state that wasn't saved; don't continue until they match.
3. **Stop at the first failed stitch.** In `--chains N`, stop after the first stitch `judgeStitch` fails, print its verdict line, and
   write the end renders. Later stitches on a failed row tell you nothing.
4. **Exploring vs passing a gate.** While you work on stitch k, load the checkpoint "stitch k−1 passed" and simulate stitch k only
   (about 10 s per try). A gate is always a full run from the empty yarn, over the whole grid. A change to the motion also changes how the
   earlier stitches came out, so a checkpoint can't prove a gate.
5. **Share the knot across the grid.** In `chain_check.sh`, make each distinct slip-knot setting once, save it, and load it for every
   grid point that uses it. Only knobs that act during the knot (for example `--damp`) need their own knot. Run the grid points as
   parallel processes (4 performance cores: use `SIM_JOBS=4`).
6. Harness runs use `--every 0`, and render only the end and the failing stitch.

Gate 0: the judge tests in 4.2 behave as stated, a save/load round trip matches bit for bit, and the harness runs on today's defaults
(expect all FAIL). Commit.

## 5. The phases

### Phase 1: a realistic hook (budget 20)
A real 5 mm hook has a head about as wide as the shaft, a throat cut into the shaft about one diameter long, and the full shaft very
soon behind it. The loop sits on the full shaft, so its size is set by the hook.

1. Change `hookSdfRaw` so the throat is a groove cut into the shaft, not a thin spine. Leave the spine behind the mouth at least 0.8R
   thick (today 0.5R). Move the full shaft radius forward to about 3.5R (today 5.2R). Keep the head at 0.85–1.0R. Keep it a smooth union
   of primitives, not `max()` cuts (the comment above `hookSdfRaw` explains why). Add the new numbers as `HookShape` fields; don't use
   environment variables.
2. Render the hook alone (`SIM_HOOKSHOT=1`) and compare it with a real inline hook. Log what you see.
3. Re-run the carry test (`SIM_CARRY=1`) and the slip-knot test. The first yarn over may be lost with a slimmer head (the handoff says so).
   Re-tune the throat catch and `makeSlipKnot` for the new head if needed.
4. **Gate 1:** the slip-knot test passes on the new hook, and the slip-knot loop sits on the full shaft: mean radial ≥ 3.2 mm, measured as
   in `onShaft`. Render and look. Make the new shape the default. Commit.

If the slip knot cannot be made on the new hook within budget, keep both shapes (a `HookShape` field picks one), continue Phase 2 on the
rig with the new hook, and record the slip-knot problem for Phase 4.

### Phase 2: one chain from the rig, with the loop opened on the shaft (budget 30)
`SIM_RIG=1` (`buildLoopRig`) starts from one clean loop, so there are fewer things to go wrong than with the slip knot.

The missing motion is very likely the last one a crocheter makes. After the new loop is pulled through, the left hand holds the work and
the hook pushes forward, so the new loop slides off the throat onto the full shaft. That opens it to shaft size, drawing yarn from the
hand. Check first whether `finishChain` already does this, and log what it does. Then:

1. Make the stitch: seat the old loop on the full shaft → yarn over → catch in the throat → turn the throat down → draw back through the
   old loop (it rides off over the head) → turn back → push the hook forward with the work held, so the new loop climbs onto the shaft →
   release.
2. **Gate 2:** `chain_check.sh p2 1 rig` passes at all 7 grid points, and the render shows one V hanging from a loop on the shaft. Commit.

**The feedback hand: allowed once the motion is right and the loop size is still wrong.** Use it when half the Phase 2 budget is spent
and the failures are about loop size: `onShaft` fails because the loop collapses onto the neck, or the loop is loose and tangles. It
replaces the fixed 0.15 N hand pull with what a crocheter does: feel the loop on the hook and let yarn out or draw it in. It is still
free physics, because the controller acts only through the hand.

- **Sensor.** Every 4 frames, measure the working loop on the hook: the length of the `hookLoop` stretch (L), and its mean distance
  from the axis.
- **Target.** L* = 2π(R + 0.6·r) + 2 mm, about 22 mm on the 5 mm hook. The number comes from the hook and the yarn; don't tune it.
- **Actuator.** The hand's tension only: `T = clamp(T0 + k·(L − L*), 0.03, 0.40)` N, changing by at most 0.02 N per frame. A loop
  that's too long gets a firmer pull; one that's too short gets slack paid out. The hand position, the grips and the particles are
  left to the solver; the controller sets only `T`.
- **When it acts.** During the seat, the push forward after the pull, and the snug. Turn it off during the catch and the pull through,
  where the throat needs the fixed pull to hold the yarn over.
- **Bookkeeping.** The controller counts as one lever. Its gain `k` joins the grid: baseline ±50% become 2 extra grid points, so
  9 in all. Log the tension trace (frame, L, T) for the baseline run, and check it settles rather than oscillating.
- If the controller can't hold L near L* (the loop stays short at the 0.03 N floor), that is the "hand can't feed" case in section 6:
  stop and report it.

Phases 3 and 4 keep the feedback hand if Phase 2 needed it.

### Phase 3: five, then ten, from the rig (budget 30)
Repeat the same motion with nothing tuned per stitch. Each stitch's old loop is the previous stitch's final loop (`hookLoop`, never
indices).

**Gate 3:** `chain_check.sh p3 5 rig` passes 5/5 at all 7 points. Then 10 at the baseline and at least 8 in a row at every other point.
The render must show the row of V's from section 1.2. Commit.

### Phase 4: from the slip knot (budget 25)
**Gate 4:** `chain_check.sh p4 10 slip` meets the Gate 3 numbers. If the slip-knot loop is the problem, fix the knot (snug it with the
hook pushed in to the full shaft, as a crocheter does). Don't special-case the first stitch's motion unless one lever, logged as in
section 3, shows that's what's needed. Commit.

### Phase 5: test and editor (budget 15)
1. Replace the chain part of `test_crochet_yarn_sim` with 3 slip-knot chains judged by `judgeStitch` plus contact. It must run in a few
   seconds. Use fewer frames per move if needed, then re-run Gate 4 to show the shorter motion still works.
2. `YarnSimSession::record("chain", n)` and the Chain button in `godot/crochet/yarn_view.gd`. Run `yarn_view_check.gd`: 0 failed.
   Take an editor screenshot (`FL_SHOT_SIM` / `crochet_shot.tscn`, see the handoff) and look at it.
3. Update `YARN_SIM_HANDOFF.md` sections 1, 4, 5 and 7 to the new truth: short, with numbers. Commit.

## 6. When a phase is stuck

When the budget is spent, or the same failure comes back for three different hypotheses:
1. Stop experimenting.
2. Write a section "Phase N: stuck" at the end of this file. Include: the failing check (which column of the judge), the numbers at the
   baseline grid point, the render paths and what they show, each hypothesis you tried and what disproved it, and the physical reason as
   far as you can tell.
3. Delete the levers that didn't help, commit, and **end the session**. Don't move to a later phase, and don't loosen the judge.
   Choosing what to do next (a different hook, a guided motion instead of free physics, a judge change) is the user's decision.

Known decision points the user will want to hear about, not decide around:
- Phase 1: the new hook can't catch the first yarn over at any head shape.
- Phase 2: the loop won't climb onto the shaft even with the work held, because the hand can't feed fast enough or friction locks it.
- Any phase: the solver lets yarn pass through itself or the hook when the motion is fast enough to make the stitch.

## 7. Progress log

(append below: date, phase, gate result, commit hash)

- 2026-10-27 Phase 0 (judge and harness): Gate 0 passes. `judgeStitch` gives a closure-free verdict per stitch. The CH_PREV=2 second
  chain (old 135..197, final 202..235) reads `through=0` and fails on that column alone; the same run's third chain reads `through=1` and
  passes. Today's 17 mm loop fails `onShaft` at 4.3/17.0 mm. `tools/chain_check.sh p0 3 slip` runs the seven grid points on today's
  defaults: every point reads 0/3 in a row. Renders in `build/chain_runs/p0/base/`.
