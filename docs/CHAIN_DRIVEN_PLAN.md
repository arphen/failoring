# Chain stitch, driven: the plan

Written 2026-10-27. This replaces `docs/CHAIN_PLAN.md` as the active plan; that file is parked (see its top line). Read this file fully,
then `docs/YARN_SIM_HANDOFF.md` sections 3, 4 and 9 for where the code is and the solver facts.

## 1. Why the approach changes

The free-physics chain needs the yarn to slide off the hook's head and climb onto the shaft through friction and contact alone, within
about 1 mm. A real crocheter steers both moments with their fingers. Months of runs show the solver gets every other part right (the slip
knot, the yarn over, the pull, the tension, the hang) and fails at exactly those two moments: the **loop off** (the old loop rides over the
head) and the **seat** (the new loop opens into a ring round the shaft).

So the yarn's **topology comes from the hook's motion, and its shape comes from physics.** During loop off and seat, the loop involved is
*driven*: its particles follow a ring round the hook's axis that slides along the hook (loop off) or opens round the shaft (seat). That is
the real motion of the yarn at those moments, done kinematically. Everything else, before, between and after, is the solver: the hand's
tension, the work hanging and swinging, the chain tightening, contact everywhere.

The driven rings are part of the design, not guides to be removed. There is no α ladder in this plan.

## 2. What "done" means

1. 10 chains in a row from the simulated slip knot, every stitch passing the frozen `judgeStitch` (Phase 0b judge, commit 3caddd3), at
   all 7 grid points of `tools/chain_check.sh` (`HOLD_BELOW` ±1, `--damp` ±3, `SIM_SEAT` ±1).
2. The yarn never comes closer to itself than 0.95 of its squashed width (2 · 0.6 · r) and never more than 0.3 mm into the hook, over the
   whole run (running contact, measured every frame, not only at the end).
3. The render reads as a chain: a row of V's, each loop through the previous one, the working loop on the shaft, the tail hanging.
   Compare with the orange chain photograph described in `docs/YARN_MODE_PLAN.md` 13.w.
4. `test_crochet_yarn_sim` checks 3 chains with the judge and running contact; green.
5. The editor's Chain button plays a simulated chain (`YarnSimSession`), and `godot/tests/yarn_view_check.gd` reads 0 failed.

## 3. Rules

From the repo owner:
- Branch `crochet/main`. Never merge into `main`. Never commit `godot/bin/libfailoring.dylib`.
- Run only the tests tied to the change. Run long grids in the background. Don't kill processes you didn't start.
- Commit messages in plain English, ending with the `Co-Authored-By:` line the session gives you. Match the code style around you (long
  plain-English comments, `Real`, `Vec3`, namespace `fl`).

For this plan:
- **The judge stays frozen** (`judgeStitch`, `test_chain_judge`, the harness verdict). A judge change needs a fixture that proves the bug
  first (6.3).
- **Driven means driven, and only there.** Drive particles only inside the two windows (loop off, seat) and only the particles of the loop
  involved plus its two legs up to 3 mm. Outside the windows, the only things acting on the yarn are the ones `CHAIN_PLAN.md` 3.1 allows:
  the hook's body, its motion, the left hand (grips ≥ 5 mm below the hook, the eyelet, the hand's tension and feed), the tail.
- **Delete what you replace.** Once the driven chain passes Gate D2, delete the guide code paths it no longer uses (`gripThroat` springs,
  `setThroatRing`, `catchK`, `PREP_HOLD`, `SEAT_HOLD`, `reelForce`) in their own commit, with the test still green.
- **Look before you claim.** A gate passes only after you opened the front, oblique and top PNGs and wrote one line in the log about what
  they show.
- Renders go in `build/chain_runs/<run>/`. The run log is `build/chain_runs/LOG.md`.
- Commit at every passed gate, with the numbers and render paths in the message body.

Writing (log, commits, docs, your messages): state what happened and what changed, in positive statements. No double negatives. Never list
things you left unchanged. No celebrating or reassuring. A failed attempt is one line: what, the number, deleted or kept. If nothing moved,
one sentence, then the next step.

## 4. The ring driver (the one new mechanism)

### 4.1 What it does
`RingDrive` (in `yarn_sim.hpp/.cpp`) takes a contiguous stretch of particles `[a, b]` (a loop found by `hookLoop`, never by fixed indices)
and, for every substep inside its window, moves them onto a target ring and holds them there (pinned, like a grip, moved smoothly over the
substeps the way the hook's capsules are). The ring is given in the hook's frame:

- axis: the hook's axis `ax`; centre `c(t) = tip + s(t) · ax` (s = distance from the hook's tip along the axis, negative toward the handle);
- radius `ρ(t)`;
- gap: the ring is open where the two legs leave it, on the side the legs point to (`legDir`, the direction from the ring's centre to the
  midpoint of the legs' first particles beyond `a` and `b`); the gap's width is twice the yarn's squashed width.

The particles are spaced evenly along the open ring. The ring's length is `2π ρ` minus the gap. The hand feeds or takes in yarn so the
stretch's arc length equals the ring's length (the driver asks the hand for `Δ` mm; the hand adds or removes particles at its end as it
does already). Particles never jump: the first substep of a window blends from where they are to the ring over 4 frames.

At the end of the window the pins let go and the solver takes over. The loop keeps its particles; nothing else changes.

### 4.2 Clearance
`ρ(s)` must clear the hook everywhere the ring passes: `ρ(s) = sectionRadius(s) + 0.6 · r + 0.2 mm`, where `sectionRadius(s)` is the hook's
largest distance from its axis at that station (from the SDF; `--hookcheck` already samples the profile). Over the head this includes the
barb. On the shaft it is R + 0.6 r + 0.2 mm ≈ 3.36 mm. Compute the profile once per hook preset and cache it.

### 4.3 Its test (fast, no chain)
A new case in `test_crochet_yarn_sim` (`ringDrive`): a straight yarn, a 22 mm stretch in the middle driven onto a ring round the shaft at
s = −12 mm, then slid to s = +3 mm (past the tip), over 60 frames, then released for 60 frames. Pass:
- every driven particle within 0.05 mm of its target at the end of each frame of the window;
- the stretch's length within 2% of the ring's length;
- running contact: yarn to hook ≤ 0.3 mm, yarn to yarn ≥ 0.95 of the squashed width;
- after release, the yarn hangs (the stretch's centre drops at least 5 mm in 60 frames) with no NaN.
Runs on both hook presets (`legacy`, `inline`).

## 5. The phases

Each phase has a budget: one harness grid or one test sweep is one experiment. Section 6 says what to do at every fork; you never stop to
ask.

### Phase D0: park the old plan (no budget; do it first)
1. `docs/CHAIN_PLAN.md` carries the "Parked" line at its top (done with this plan). Add `CHAIN=driven|physics` to the CLI and to
   `chain_check.sh` (default `physics`), so both chain procedures run from the same harness.
2. Choose the hook for this plan: `--hook legacy` (the slip knot works on it). Write the choice in section 7. `inline` is optional later.
3. Make one slip-knot checkpoint per damping of the grid with `chain_check.sh`'s knot step, and use it for every run below. The knot is
   never re-simulated while you work on chains.
4. Commit.

### Phase D1: the ring driver (budget 10)
Build 4.1 and 4.2, then the test in 4.3. **Gate D1:** the `ringDrive` test passes on both presets. Commit.

Likely failures and what each means:
| Symptom | Cause | Fix |
| --- | --- | --- |
| Neighbours of the driven stretch stretch more than 10% | the pins move too far in one substep | blend over more frames (8), or more substeps in the window |
| Yarn to yarn contact fails at the gap | the legs cross inside the gap | widen the gap to 3 squashed widths; turn the gap toward `legDir` |
| Length drifts | the hand's feed is not asked, or asked once | ask the hand every frame for the current `Δ` |
| Hook contact fails over the barb | the profile misses the barb | sample the SDF at 64 angles per station, take the maximum |

### Phase D2: one chain from the slip knot (budget 20)
Write `makeChainDriven(sim)`, next to `makeChain`, reusing `prepareChain`'s pieces (find the loop, left hand grips the work, yarn over).
The sequence, with times at 60 frames per second:

1. **Seat the old loop O** (physics, 20 frames): hook forward, the left hand holds the work. If `onShaft` reads O's distance from the
   axis above 4.5 mm or O sits on the neck, drive O onto the shaft ring at s = −12 mm for 10 frames (this is a seat window; log that it
   was needed).
2. **Yarn over** (physics, the existing motion): the working yarn goes into the throat. Checked with `hookLoop`/the throat test the
   handoff describes: the yarn lies in the throat.
3. **Draw back** (physics): the hook draws the yarn over back toward O.
4. **Loop off window** (driven, 30 frames): drive O on a ring that slides from its seat to s = +3 mm (past the tip), its radius following
   4.2, while the hook keeps drawing back. Because the yarn over lies along the axis in the throat, O slides over the head and closes round
   the new yarn's legs. Release O: the hand's tension tightens it round them.
5. **Seat window** (driven, 30 frames): the new loop N (the yarn over, found by `hookLoop` as the stretch round the hook) is driven from the
   throat onto a ring round the shaft at s = −12 mm, while the hook pushes forward. Release N.
6. **Snug** (physics, 30 frames): hand tension, the work held. Then the left hand's grip moves to the new V (as `prepareChain` does).

**Gate D2:** `tools/chain_check.sh d2 1 slip CHAIN=driven` passes stitch 1 at all 7 points; running contact clean; the render shows one V
hanging from the loop on the shaft. Commit.

Likely failures:
| Symptom | Cause | Fix |
| --- | --- | --- |
| `through` fails: N's legs pass beside O | the yarn over is not in the throat when O slides | check step 2 ends with the yarn in the throat; slide O only after the throat test reads true |
| `oldOff` fails: O ends round the shaft again | O released before it cleared the tip | slide to s = +5 mm; release after the hook has drawn back 5 mm more |
| `onShaft` fails: N slips back to the neck | the hook drew back after the seat window | the push forward continues 10 frames past the release |
| `fresh` or `newYarn` fails | N was taken from O's yarn, not from the working yarn | find N as the hook loop *after* O's stretch along the yarn, toward the hand |
| O tightens to a knot that won't let go | tension too high at release | the hand's tension 0.08 N during the snug |

### Phase D3: ten in a row (budget 20)
Run `makeChainDriven` repeatedly, with nothing tuned per stitch. Each stitch's O is the previous stitch's N (found geometrically). Explore
from checkpoints: save after every passed stitch, and start an experiment from the last good one.

**Gate D3:** `chain_check.sh d3 10 slip CHAIN=driven` passes 10/10 at all 7 points; running contact clean over the whole run; the render
shows a row of V's. Commit.

Likely failures:
| Symptom | Cause | Fix |
| --- | --- | --- |
| Stitch 3+ fails `oldOff`, the chain twists round the hook | the work turns with the hook; the grip drifts | the grip moves to the newest V every stitch, 5 mm below the hook |
| V's grow or shrink along the chain | the snug tension or the ring length differs per stitch | one tension, one ring; check the hand's feed log per stitch |
| A late stitch fails contact | the hanging chain swings into the hook | damping up one grid step for the hanging part, or the grip holds 2 V's |

### Phase D4: test, cleanup, editor (budget 10)
1. `test_crochet_yarn_sim`: 3 driven chains from the slip knot, judged with running contact. Green.
2. Delete the guide code paths (3, "Delete what you replace") in one commit; the test stays green.
3. `YarnSimSession::record("chain", n)` uses `makeChainDriven`; the Chain button in `godot/crochet/yarn_view.gd` plays it. Run
   `yarn_view_check.gd`: 0 failed. Take an editor screenshot (`FL_SHOT_SIM` / `crochet_shot.tscn`) and look at it.
4. Update `YARN_SIM_HANDOFF.md` sections 1, 4, 5 and 7 to the new state, short, with numbers. Commit.

## 6. Decide and keep going

You run unattended. **Never stop to ask the user.** At every fork apply the rules below, write one line "Decision (agent): <fork>,
<choice>, <rule>" in section 7, and continue. Your final message comes only at Done or a hard stop (6.5).

### 6.1 Choosing between options
Take the first option that meets the earliest rule:
1. It keeps the judge and its fixtures as they are.
2. It drives fewer particles for fewer frames (more physics).
3. It moves toward the next gate (code that runs a check beats analysis).
4. It is the smaller change and easier to undo.

### 6.2 A plan number that disagrees with what you measure
A number in this plan (a time, a distance, a frame count) is a starting value. Change it when a measurement says so, write the new value
here, and log it. It never blocks a gate.

### 6.3 A judge bug
Write a fixture first: a hand-built configuration whose right verdict is clear from its picture. Show the judge gets it wrong, fix the
judge, run every fixture; every existing negative must still fail its column. Log it as "Judge amendment" with the fixture's name. A judge
change made to let a stitch pass, without such a fixture, is forbidden.

### 6.4 A spent budget
Write a short "Phase N: stuck" note (the failing column, the best numbers, the render paths, what was tried), delete dead code, commit,
and take the fallback. Each fallback brings half the phase's budget again.
- **D1:** drive with springs of stiffness 2000 N/m to the ring instead of pins.
- **D2, `through` or `oldOff`:** widen the loop-off window: drive O and the yarn over's bight together (the bight held in the throat as a
  short driven stretch of 6 mm) for the 30 frames.
- **D2, `onShaft`:** drive N for 20 frames more after the push forward starts.
- **D3:** drive the previous V (two stitches back) onto its rest position under the grip during each loop off.
- After the last fallback of a phase, go on with the best result and record the debt in section 7; D4's test then uses the number of
  stitches that pass, and the final message names the debt.

### 6.5 Hard stops (the only reasons to end and report)
- Done (section 2).
- The build or the test suite breaks in a way you can't fix in 10 attempts.
- Going on would break a rule from the repo owner (the dylib, `main`, someone else's process).

## 7. Progress log

(append below: date, phase, gate result, commit hash, decisions)
- 2026-10-27 Phase D0 (done): `--chain driven|physics` in the CLI and `CHAIN=driven|physics` in `chain_check.sh` (default physics; the
  driven branch is a stub until D2). Knots are shared per hook (`build/chain_runs/knots-legacy/`, d17, d20, d23). Hook: `--hook legacy`.
  Smoke run `tools/chain_check.sh d0 1 slip` (physics, legacy): stitch 1 fails at all 7 points, 0/1, as recorded in CHAIN_PLAN.md.
  Decision (agent): hook for this plan, legacy, rule 6.1.1 (the slip knot works on it, the inline hook loses the first yarn over).
  Decision (agent): knot store shared across chain procedures, rule 6.1.4 (one knot per damping, never re-simulated).
