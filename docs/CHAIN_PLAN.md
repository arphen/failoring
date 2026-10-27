# Chain stitch: the plan to finish it

Read this first. `docs/YARN_SIM_HANDOFF.md` is the background (what was tried, the solver facts, the tools); this file is the
order of work, the rules, and the gates. When the two disagree, this file wins.

## 1. The goal and what "done" means

A row of chain stitches made by real physics, starting from the slip knot, that looks like a real chain and plays in the editor. The
hook moves, the left hand holds the work and feeds the yarn, and the yarn does what the solver makes it do. The only things allowed to
act on the yarn are the hook's body, the yarn itself, and the hand (section 3.1).

Done is all of these, at the default settings, with every guide at zero (`SIM_ASSIST=0`, section 4.6), and still true when each main
knob moves one step either way (section 4.3):

1. `yarn_sim --chains 10` from the slip knot makes 10 stitches, each passing `judgeStitch`.
2. The render reads as a chain: a row of V shapes on the front face, each loop about one hook diameter wide, a ridge of bumps on the back,
   one working loop sitting on the hook's full shaft. Not a knot or a tangle at the head.
3. Contact over the whole run: the yarn is never closer to itself than 0.95 yarn widths, and never more than 0.4 mm into the hook.
4. `test_crochet_yarn_sim` is green with a chain test (3 chains is enough for the test; keep it to a few seconds).
5. The editor's Chain button plays the simulated chain; `godot/tests/yarn_view_check.gd` reports 0 failed.

## 2. Why the last runs stalled (do not repeat it)

- **Physics.** Every loop the hook made was about 16 mm of yarn. A loop that sits on the 2.5 mm shaft needs at least 20.4 mm of
  centreline. The old hook only reached its full radius 13 mm behind the tip (a 0.5R neck capsule from 2.05R to 4.2R, a taper to 5.4R),
  so loops settled on the skinny neck. Their opening was narrower than the next head and bight, and the next stitch went beside the
  old loop instead of through it. This is a geometry problem, and motion levers can't fix it.
- **Guides doing the work.** The yarn over is held by a hard pin carried with the hook (`gripThroat`), a ring at the throat
  (`setThroatRing`) and a spring (`catchK`). The old loop is held by pins (`PREP_HOLD`, `SEAT_HOLD`). With these on, a result says
  little about whether the hook and the motion would work for real. They hid the missing hook geometry.
- **Method.** The run added about 30 environment levers, one per idea, all left off by default, and changed the judge as it went. It
  tuned a lever until one run passed, then found the pass was a measuring artefact. Section 3 forbids this.

## 3. Rules (these override your own judgement)

From the repo owner:
- Branch `crochet/main`. Never merge into `main`. Never commit `godot/bin/libfailoring.dylib`.
- Run only the tests tied to the change. Run long grids in the background. Don't kill processes you didn't start.
- Commit messages: plain English, ending with the `Co-Authored-By:` line the session gives you. Match the code style around you (long
  plain-English comments, `Real`, `Vec3`, namespace `fl`).

For this plan:
- **The judge is frozen after Phase 0b.** Never loosen, reword or add a fallback to `judgeStitch`, its fixtures or the harness verdict.
  If you think the judge is wrong, stop and write down why (section 6). Do not work around it.
- **One hypothesis, one lever.** Before an experiment, write one line in the run log (`build/chain_runs/LOG.md`): the hypothesis, the
  single change, and the result that would disprove it. If the lever doesn't move the verdict toward pass, delete it in the same
  session. Don't leave dead levers off by default.
- **Budgets.** Each phase has an experiment budget: a run of the harness grid or one micro-rig sweep counts as one experiment. When the
  budget is spent and the gate isn't met, follow section 6 and stop that phase. Don't start a side quest.
- **Look before you claim.** A gate counts as passed only after you have opened the PNGs (front, oblique, top) and described in the log
  what you see, compared with the picture in section 1.2. Numbers alone never pass a gate.
- **Renders go in `build/chain_runs/<run>/`** (ignored by git), never the repo root.
- **Commit at every passed gate,** with the numbers and the file names of the renders in the message body.

### 3.1 What may act on the yarn

| Kind | Examples | Allowed |
| --- | --- | --- |
| The hook's body | SDF contact with friction | always |
| The hook's motion | scripted poses; aiming the hook from what it sees of the yarn (`YO_SERVO`) | always: a crocheter watches the yarn |
| The left hand | grips on the finished work at least 5 mm below the hook, the index finger the working yarn runs over, the hand's pull and feed at the ball end, the feedback hand (Phase 2) | always |
| The tail | pinned or held | always |
| **Guides** | `gripThroat`, `setThroatRing`, `catchK`, pins on loop particles (`PREP_HOLD`, `SEAT_HOLD`, any grip on the working or old loop), `reelForce`, any new force or pin on chosen particles | only scaled by `SIM_ASSIST` (section 4.6), and only on the ladder in section 5 |

A new helper that pushes, pins or rings yarn particles is a guide. It must join the `SIM_ASSIST` scaling and the guide report on the
day it is written. Unscaled guides are forbidden.

### 3.2 How to write the log, the commits, the handoff and your final message

- Say what happened and what changed, in positive statements: "chain 2's new loop lies beside the old one (`through` 0)", not "chain 2
  is not linked through".
- No double negatives ("not unlinked", "doesn't fail to"). No lists of things you left alone ("no defaults were changed", "nothing else
  was touched", "the judge was not loosened"). The diff shows what changed; say only that.
- A failed experiment is one line: hypothesis, change, result with the number, deleted or kept. Don't make it sound like progress or
  reassurance.
- No celebrating or validating. If nothing moved, say so in one sentence ("Phase 2: 30/30 spent, no stitch passes `onShaft`; stuck, see
  below") and stop. Keep the final message short: the gate reached, the numbers, the render paths, the next decision.

## 4. Phase 0: clean up, judge, harness (done 2026-10-27, commit f675371)

What exists: `judgeStitch` with the columns `onHook fresh newYarn sequential oldOff through onShaft contact`, `discCrossings`, and
`tools/chain_check.sh` with the seven-point grid (`HOLD_BELOW` ±1, `--damp` ±3, `SIM_SEAT` ±1). Phase 0b finishes it.

### 4.3 The grid (unchanged)
Baseline plus one step either way on each main knob, one at a time: `HOLD_BELOW` ±1 mm, `--damp` ±3, `SIM_SEAT` ±1. That is 7 points.
**A setting works when all 7 points pass.** Phase 2 may add the feedback hand's gain (±50%), making 9.

## 4b. Phase 0b: finish and harden Phase 0 (budget: code only, no experiments; do this before any more Phase 1 work)

Phase 0 left four gaps. In order:

### 4.4 Checkpoints, stop at the first failure (not built yet)
Measured on 2026-10-27 (Release build): the slip knot is 684 frames and takes 14 s; each chain takes about 10 s. Every run rebuilds the
slip knot and every earlier chain, so trying stitch 5 costs about 64 s, of which 54 s repeats known work.

1. **`--save <file>` / `--load <file>`, `--save-after knot|k`.** Write the whole state. The rod system's per-particle and per-segment
   arrays (positions, previous positions, velocities, inverse masses, rest lengths; for the Cosserat segments: orientations, angular
   velocities, rest Darboux vectors and bend memory). The frozen masses (`savedW_`, `savedWq_`). Rings, including where each ring sits
   along the yarn. SDF bodies, including the previous pose used to move them smoothly over substeps. Friction anchors or caches, if the
   solver keeps any between frames. The ends (`tail`, `ball`, `pulled_`, `reeled_`, `caught_`, `catchPin_`, `catchOffset_`). The grips,
   finger, `hand`, `home`/`homeSet`, `fixtures`, the throat ring index, `chains`, `lastLink*` and `frame`. Leave `onFrame` out.
   The header holds the git hash, every `HookShape` and `YarnSimParams` field, `SIM_ASSIST`, and the environment knobs that are set.
   `--load` refuses a file whose header doesn't match.
2. **Bit-exact check.** Run knot + 2 chains straight. Then run knot → save → load in a new process → the same 2 chains. Compare
   positions with `memcmp`. If they differ, print the first differing particle and frame, and find the missing state. The usual culprits
   are the list in 1, in that order. The particle count changes as the hand feeds, so save the arrays with their sizes.
3. **Stop at the first failed stitch** in `--chains N`: print its verdict line, render the end, exit with status 1.
4. **Exploring vs gates.** While you work on stitch k, load "stitch k−1 passed" and simulate stitch k only. A gate is always a full run
   from the empty yarn over the whole grid: a motion change also changes the earlier stitches, so a checkpoint can't prove a gate.
5. **Share the knot across the grid.** `chain_check.sh` makes each distinct slip-knot setting once (only knobs that act during the
   knot, such as `--damp`, need their own), saves it, and loads it for every grid point that uses it. It runs grid points as parallel
   processes, 4 at a time (4 performance cores). `--every 0`; render only the end and the failing stitch.

### 4.5 Judge fixes (the last time the judge changes)
Four holes in today's `judgeStitch`:

1. **Contact is measured only at the end.** A stitch whose yarn passes through itself mid-pull and recovers passes. Fix: `YarnSim`
   keeps a running `worstGap` and `worstPen`, reset by `prepareChain`, updated in `step()` every frame (every 2nd frame if that costs
   too much; say which in the comment). `contact` uses the running values.
2. **`through ≥ 1` counts any crossing.** The disc is fanned from the old loop's centroid and closed with a chord from its last
   particle to its first. A working yarn that crosses once on its way to the hand passes. Fix:
   - Split the yarn after the old loop into the new loop's two legs: from `oldHi+3` to the middle of L (`legIn`), and from the middle
     of L to `hi+6` (`legOut`). Skip segments within 2 particles of the old loop's ends, where the disc's edge is.
   - Count crossings of each leg separately, with their sign along the disc normal.
   - In a chain both legs of the new loop pass through the old one. `legIn` starts on the old loop's own edge, so its crossing can fall
     inside the skipped particles. `through` therefore passes when `legOut` has at least one crossing; `legIn` is printed as a
     diagnostic. The yarn beyond `hi+6`, running to the finger, never counts.
   - Print `through=in/out` with the signs.
   - When the old loop's ends are more than 3 yarn widths apart, its disc is not a disc. `through` then reads `?`, and `?` fails.
3. **No upper limit on the loop.** A 33 mm loose loop passed `onShaft` (Phase 0 judge test A, chain 3). Fix: `onShaft` also needs
   `length ≤ 1.35·L*`, with `L* = 2π(R + 0.6·r) + 2 mm` (about 22 mm on the 5 mm hook, so ≤ 30 mm).
4. **The judge has never said PASS on a known real stitch.** Every PASS so far is unverified. Fix: the fixtures in 4.5.1.

#### 4.5.1 Judge fixtures (a new, fast test: `test_chain_judge`)
Build yarn polylines directly (no simulation), set them into a `YarnSim` with the hook posed, fill a `ChainStart`, and call
`judgeStitch`. The geometry is analytic and in the hook's frame (z along the axis from the tip, the throat toward +x). R = 2.5 mm,
r = 1.1 mm, 1 mm segments.

- **True chain stitch (must PASS).**
  - The new loop N is a circle round the shaft at z = 12 mm, radius R + 0.6r + 0.2 mm, closing at its lowest point (−y).
  - Its two legs run from there down, 1.5 mm apart in x, to y = −9 mm.
  - The old loop O is a small loop of 13–15 mm of yarn round both legs at y = −8 mm. Its plane is square to y, and it is cinched
    (gap ≥ 2r to each leg).
  - The yarn order is: the earlier work below O, then O, then N's first leg up through O, N, N's second leg back down through O,
    then on to the finger at (+20, −5, 40) mm.
  - Render it with the CLI renderer (add `--fixture <name>` to the render path) and look: it must read as one V below a loop on the
    shaft. If it doesn't, fix the fixture before trusting the judge.
- **Negatives (each must FAIL, on the column named):**
  1. N's legs pass beside O, 4 mm to the side → `through`.
  2. N's legs pass beside O, and the yarn beyond N crosses O's disc on its way to the finger → `through`.
  3. O is still round the shaft at z = 8 mm, with N in front of it → `oldOff`.
  4. N is a 16 mm loop on a neck of radius 1.25 mm → `onShaft` (radius and length).
  5. N is 35 mm long, loose → `onShaft` (length).
  6. The true stitch with one N particle moved 1.5 mm into a neighbouring strand → `contact`.
  7. The true stitch with N built from O's own particles (`fresh` ≥ 0.25) → `fresh`.
- Also re-run the two simulated judge tests from Phase 0 (`CH_PREV=2` chain 2 must fail on `through`; the old neck loop must fail on
  `onShaft`), and render chain 3 of test A (which passed the old judge) and say in the log what it is.

The fixtures run in under a second and join CTest. From now on they are part of the frozen judge.

### 4.6 Guides become measurable and scalable
1. **Inventory.** List every place in `yarn_sim.cpp` that pins, rings, pushes or grips yarn particles, with a file:line, a one-line
   description, and its class from 3.1. Write the table into this file under 4.6. Expected guides: `gripThroat` (a hard pin carried
   rigidly with the hook), `setThroatRing` (a slide ring, stiffness `throatStiffness`), `catchK`, the `PREP_HOLD` and `SEAT_HOLD` pins,
   `reelForce`, and any grip on the old or working loop. Check `PU_FIX` and `SEAT_HAND` and classify them. The finger eyelet ring is part
   of the hand (allowed). Say so in the table.
2. **Turn hard pins into springs.** A pin can't be scaled. Replace `gripThroat`'s rigid carry and the loop pins with springs of
   stiffness `k·α` to the same target (the throat point in the hook frame, the pinned position). Pick `k` so that at α = 1 the result
   matches today's pinned behaviour within the test's tolerances; check on the three-chain test.
3. **One knob per guide, one default for all.** Each guide in the inventory gets its own scale, `SIM_ASSIST_<NAME>` (for example
   `SIM_ASSIST_GRIP`, `SIM_ASSIST_RING`, `SIM_ASSIST_CATCH`, `SIM_ASSIST_PINS`, `SIM_ASSIST_REEL`). Each one defaults to `SIM_ASSIST=α`
   (default 1 for now). The scale multiplies that guide's strength: spring stiffness, `catchK`, ring stiffness, or `reelForce`. At 0 the
   guide is off, and the code takes the same path as if it had never been switched on, so no ring is created and no pin is made. A
   guide's scale is the number the gates read; `SIM_ASSIST` alone is shorthand for setting them all.
4. **Guide report.** Each guide adds up the impulse it delivers (|F|·dt for springs and forces; the constraint's |λ| for rings). Each
   stitch's verdict line gets a suffix with each guide's scale and impulse: `assist grip 1.0:0.8 ring 0.5:0.3 catch 0:0 pins 0.25:0.1
   mN·s`. The hook body's contact impulse goes
   on the same line, so the two can be compared. The judge ignores this suffix; the gates in section 5 read it.

**Gate 0b:** the fixtures behave as stated; the simulated judge tests behave as stated; the bit-exact save/load check passes; `chain_check.sh`
runs with shared knots and stops at first failures; the guide table is in this file; `SIM_ASSIST=1` reproduces today's three-chain test
result, and `SIM_ASSIST=0` runs (expect failures). Commit.

## 5. The phases

The guide ladder: each phase's gate has a highest scale any guide may have.

| Phase | Highest guide scale at the gate |
| --- | --- |
| 1 (hook micro-rigs) | 0 for every guide |
| 2 (one chain from the rig) | ≤ 1, then the removal order below is run and recorded |
| 3 (five, ten from the rig) | ≤ 0.25 |
| 4 (from the slip knot) | ≤ 0.1, then 0 |
| Done | 0 for every guide |

**Removal order: one guide at a time, least-used first.** Lowering all guides together hides which one carries the stitch. At the
baseline, sort the guides by the impulse each delivers per stitch, smallest first. Then take them out one at a time, in that order:

1. Take the next guide down the ladder 1 → 0.5 → 0.25 → 0.1 → 0. The guides already taken out stay at 0; the rest stay where they
   are.
2. Run the stitch at each step, and log which steps pass, the impulse of every guide, and the hook's own contact impulse.
3. If the guide reaches 0 and still passes, it was scaffolding. Leave it at 0 and take the next one.
4. If a step fails, this guide is load-bearing. The failure names the missing physics:
   - `grip` or `catch`: the throat doesn't hold the yarn over. Go back to micro-rig A (1.3).
   - `pins`: the old loop is pulled forward with the hook instead of staying put. Look at the seat and the hand's grip on the work.
   - `ring`: the yarn leaves the throat sideways. Look at the mouth's depth and the turn of the throat.
   - `reel`: the hand doesn't feed or take in the right amount. Look at the feedback hand.
   Fix that physics (hook shape, motion, hand), not the guide. Then re-run this guide's ladder from its last passing step.

Expect a cliff, not a slope: contact and friction switch, so a guide often passes at 0.25 and fails outright at 0.1. A cliff is a
finding, not noise. Log the scale where it happens and the guide's impulse just before.

When the motion is re-tuned at a lower scale, re-check the guides already at 0: a re-tune can quietly start depending on a guide that
was taken out. They stay at 0, so the stitch will fail if it does.

### Phase 1: a realistic hook, proven on micro-rigs (budget 25)

The hook alone must do its two jobs at α = 0: hold a yarn over while it is drawn back, and let a loop slide on and off. Prove both on
small rigs that take seconds, before any stitch. The opencode Phase 1 run started on the shape below the old plan described; keep what
fits, and re-check it against 1.1.

#### 1.1 The target shape
A 5 mm inline hook (R = 2.5 mm), in the hook frame (z from the tip, the mouth toward +x). These numbers are approximate. If the user
supplies a photo or calipers of a real hook, theirs win.

| Part | Target |
| --- | --- |
| Head | a round dome, radius 0.85–1.0R, from the tip to its back at z ≈ 2.0R |
| Throat (mouth) | a groove cut toward −x, about one diameter long (z ≈ 2.0R to 4.0R), deepest at about z = 2.8R. Its depth must hold the squashed yarn: at least 0.6R below the shaft surface (the yarn's contact diameter is 2·0.6r = 1.32 mm = 0.53R). |
| Spine behind the mouth | a half-cylinder (flat face toward the mouth), not a thin rod. Cross-section perimeter at the deepest point ≥ 0.75 × the shaft's (2πR) |
| Back of the throat | rises back to full radius by z ≈ 3.5–4.0R, a ramp with no step |
| Shaft | radius R from there on |

Write the SDF as a smooth union of primitives, as the comment above `hookSdfRaw` asks. A half-cylinder is the rounded intersection of
the shaft cylinder with a half-space (`smax`); an intersection of convex parts is safe, unlike the `max()` slot the comment warns
about. Every new number is a `HookShape` field. Don't use environment variables.

#### 1.2 Hook checks (seconds; no yarn)
Add `--hookcheck` to the CLI. It prints:
- **A profile table**, every 0.25R from z = 0 to 6R: the cross-section perimeter, the area, the depth of the mouth below the shaft
  surface, and the thinnest feature (radius of the narrowest primitive there).
- **The gradient norm** `|∇sdf|` sampled on the shell at the yarn's contact distance (0.6r) around the hook: min, max, and where.
- **The render** (`SIM_HOOKSHOT=1`) from the side, from the mouth and end-on.

Pass:
- Every feature is at least 0.3R thick. A barb of radius 0.18R (0.45 mm) is thinner than the yarn's contact radius, and the 3-point
  segment sampling lets yarn pass through it. Make barbs ≥ 0.3R or drop them.
- `|∇sdf|` stays within 0.8–1.2 on the shell.
- The profile meets 1.1.
- The render looks like an inline hook. Write what you see in the log.

#### 1.3 Micro-rig A: carry (α = 0)
A straight 80 mm yarn, both ends held with 0.15 N.
1. Lay it across the mouth at the throat's deepest point, wrapping the hook by an angle θ.
2. Turn the throat down (as in the stitch) and draw the hook back 25 mm along its axis, at the stitch's pull speed.

Measure where the yarn sits at the end: whether the wrapped part is still within the throat's z range, and how much yarn slid through
the throat. Sweep θ = 90°, 135°, 180°, hook friction 0.15/0.25/0.35, and tension 0.08/0.15/0.25 N.

Pass: the yarn stays in the throat at every point with θ ≥ 135° (a yarn over wraps about 180°), with contact clean (≤ 0.4 mm into the
hook, self-gap ≥ 0.95·2r).

#### 1.4 Micro-rig B: loop off (α = 0)
1. Build a closed-ish loop of L* = 22 mm round the full shaft at z = 15 mm. Its two ends run down to a grip 10 mm below (the hand
   holding the work).
2. Turn the throat down and draw the hook back 30 mm, with no yarn over.

Pass: the loop rides forward over the throat and head and comes off. The hand's grip never needs more than 0.3 N to hold it (log the
peak). It never snags on the head's back edge (log the hook's contact impulse: no spike more than 3× the median). Contact stays clean.
Sweep friction as in A.

#### 1.5 Micro-rig C: seat (α = 0)
1. Start from a 16 mm loop sitting in the throat: today's failure state.
2. The hand grips the work 10 mm below and feeds the working yarn at 0.15 N.
3. Push the hook forward 15 mm along its axis.

Pass: the loop climbs the back of the throat onto the shaft. It ends at mean radius ≥ R + 0.6r and length ≥ 20 mm, with yarn drawn from
the hand (log how much), at friction 0.15/0.25/0.35. This is the motion Phase 2 needs. If it fails here it will fail in the stitch.

#### 1.6 Micro-rig D: A and B together (α = 0)
B's loop on the shaft, plus A's yarn laid in the throat in front of it (the yarn over), both ends to the hand side. Draw back as in B.

Pass: the caught yarn goes through the loop (use the judge's `through` on the two strands), and the loop comes off over it. This is the
stitch's core, made by the hook alone.

#### 1.7 Gate 1
A, B, C and D pass at α = 0 on the new hook. The new shape is the default. The carry test and the slip-knot test in
`test_crochet_yarn_sim` pass at whatever α they need (record it). The slip-knot loop ends on the full shaft (mean radius ≥ R + 0.6r).
Renders of each rig are in the log. Commit.

#### 1.8 Expected failures and what each means

| Symptom | Likely cause | Next step |
| --- | --- | --- |
| A: the yarn leaves the throat sideways | the mouth is shallow, or the spine's flat face lets it roll out | deepen the mouth toward 0.8R; round the mouth's edges inward |
| A: the yarn slides out toward the tip | the head's back is too low, or friction too low to hold at the wrap | raise the head's back edge (a lip, ≥ 0.3R thick), not the barb |
| A: the yarn passes into the hook (penetration > 0.4 mm) | a feature thinner than the contact radius, or a gradient spike at a smooth-union seam | 1.2's thickness and gradient checks; widen the blend radius |
| B: the loop snags on the head's back | the lip from row 2 is too high, or the head is wider than the shaft | the lip may only rise on the mouth side; head ≤ 1.0R; this is the trade-off with A |
| B: the hand needs > 0.3 N | friction locking on the shaft (capstan) | check the yarn–hook friction against wool on aluminium (0.15–0.25); don't drop below 0.15 |
| C: the loop stays in the throat | the throat's back is a step, not a ramp, or the hand can't feed | a longer ramp; check the yarn is flowing in at the hand (log the feed) |
| C: the loop opens but jumps back into the throat when the push stops | the shaft is a cone, not a cylinder, there | the shaft must be cylindrical from the end of the ramp |
| D: the yarn over leaves the throat as it enters the loop | the loop squeezes the throat (the loop is too tight) | check C's loop length; the hand's pull during D |
| Everything is sensitive to friction | stick–slip | slower motions (minimum jerk is already used); more substeps only as a last resort (cost) |

The A/B conflict (hold the yarn over, release the loop) is the hook's real design problem. Real hooks solve it with the turn of the
throat: down while drawing back, so the yarn over is pressed into the throat and the loop slides over the smooth back of the head. If
no shape passes both A and B within budget, stop and report it (section 6), with the best A-passing and the best B-passing shapes and
their numbers.

### Phase 2: one chain from the rig (budget 30)
`SIM_RIG=1` (`buildLoopRig`) starts from one clean loop, so there are fewer things to go wrong than with the slip knot.

The stitch: seat the old loop on the full shaft → yarn over → catch in the throat → turn the throat down → draw back through the old loop
(it rides off over the head) → turn back → push the hook forward with the work held, so the new loop climbs onto the shaft (micro-rig
C) → release. Check first what `finishChain` already does, and log it.

**Gate 2:** `chain_check.sh p2 1 rig` passes at all 7 grid points with every guide ≤ 1. Then run the removal order (section 5) at the
baseline, and record each guide's lowest passing scale and the guide report at every step. The render shows one V hanging from a loop on the shaft. Commit.

**The feedback hand: allowed once the motion is right and the loop size is still wrong.** Use it when half the Phase 2 budget is spent
and the failures are about loop size: `onShaft` fails because the loop collapses onto the neck, or the loop is loose and tangles. It
replaces the fixed 0.15 N hand pull with what a crocheter does: feel the loop on the hook and let yarn out or draw it in. It is part of
the hand (3.1), so it is allowed at α = 0.

- **Sensor.** Every 4 frames, measure the working loop on the hook: the length of the `hookLoop` stretch (L), and its mean distance
  from the axis.
- **Target.** L* = 2π(R + 0.6·r) + 2 mm, about 22 mm on the 5 mm hook. The number comes from the hook and the yarn; don't tune it.
- **Actuator.** The hand's tension only: `T = clamp(T0 + k·(L − L*), 0.03, 0.40)` N, changing by at most 0.02 N per frame. A loop
  that's too long gets a firmer pull; one that's too short gets slack paid out. The hand position, the grips and the particles are
  left to the solver; the controller sets only `T`.
- **When it acts.** During the seat, the push forward after the pull, and the snug. Turn it off during the catch and the pull through.
- **Bookkeeping.** The controller counts as one lever. Its gain `k` joins the grid: baseline ±50% become 2 extra grid points, so
  9 in all. Log the tension trace (frame, L, T) for the baseline run, and check it settles rather than oscillating.
- If the controller can't hold L near L* (the loop stays short at the 0.03 N floor), that is the "hand can't feed" case in section 6:
  stop and report it.

Phases 3 and 4 keep the feedback hand if Phase 2 needed it.

### Phase 3: five, then ten, from the rig (budget 30)
Repeat the same motion with nothing tuned per stitch. Each stitch's old loop is the previous stitch's final loop (`hookLoop`, never
indices). Explore from checkpoints (4.4).

**Gate 3:** at α ≤ 0.25, `chain_check.sh p3 5 rig` passes 5/5 at all 7 points. Then 10 at the baseline and at least 8 in a row at every
other point. The render must show the row of V's from section 1.2. Commit.

### Phase 4: from the slip knot, and the guides to zero (budget 25)
**Gate 4a:** `chain_check.sh p4 10 slip` meets the Gate 3 numbers at α ≤ 0.1. If the slip-knot loop is the problem, fix the knot: snug
it with the hook pushed in to the full shaft, as a crocheter does. Don't special-case the first stitch's motion unless one lever, logged
as in section 3, shows that's what's needed.

**Gate 4b:** the same at α = 0, the slip knot included. Then delete the guide code paths that no gate uses any more, and make
`SIM_ASSIST` default 0. Commit.

### Phase 5: test and editor (budget 15)
1. Replace the chain part of `test_crochet_yarn_sim` with 3 slip-knot chains at α = 0, judged by `judgeStitch` with running contact. It
   must run in a few seconds. Use fewer frames per move if needed, then re-run Gate 4b to show the shorter motion still works.
2. `YarnSimSession::record("chain", n)` and the Chain button in `godot/crochet/yarn_view.gd`. Run `yarn_view_check.gd`: 0 failed.
   Take an editor screenshot (`FL_SHOT_SIM` / `crochet_shot.tscn`, see the handoff) and look at it.
3. Update `YARN_SIM_HANDOFF.md` sections 1, 4, 5 and 7 to the new truth: short, with numbers. Commit.

## 6. When a phase is stuck

When the budget is spent, or the same failure comes back for three different hypotheses:
1. Stop experimenting.
2. Write a section "Phase N: stuck" at the end of this file. Include: the failing check (which judge column or rig criterion), the
   numbers at the baseline, the lowest α reached and the guide report there, the render paths and what they show, each hypothesis you
   tried and what disproved it, and the physical reason as far as you can tell.
3. Delete the levers that didn't help, commit, and **end the session**. Don't move to a later phase, and don't loosen the judge.
   Choosing what to do next is the user's decision.

Known decision points the user will want to hear about, not decide around:
- Phase 1: no hook shape passes both carry (A) and loop-off (B).
- Phase 1/2: the loop won't climb onto the shaft (C), because the hand can't feed fast enough or friction locks it.
- Any phase: a guide stays load-bearing after its physics was worked on within budget. Report the guide, its lowest passing scale,
  its impulse there, and what was tried.
- Any phase: the solver lets yarn pass through itself or the hook when the motion is fast enough to make the stitch.

## 7. Progress log

(append below: date, phase, gate result, commit hash)

- 2026-10-27 Phase 0 (judge and harness): Gate 0 passes. `judgeStitch` gives a closure-free verdict per stitch. The CH_PREV=2 second
  chain (old 135..197, final 202..235) reads `through=0` and fails on that column alone; the same run's third chain reads `through=1` and
  passes. Today's 17 mm loop fails `onShaft` at 4.3/17.0 mm. `tools/chain_check.sh p0 3 slip` runs the seven grid points on today's
  defaults: every point reads 0/3 in a row. Renders in `build/chain_runs/p0/base/`. Commit f675371.
- 2026-10-27 plan review: Phase 0 left out section 4.4 (checkpoints, stop at the first failure). The judge's `contact` reads only the end
  state, `through ≥ 1` counts any crossing, and the 33 mm loop of test A's chain 3 passed `onShaft`. Phase 0b added.
