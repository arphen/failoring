# Tee (FreeSewing Teagan): where it stands and what to do next

Written at the 2026-10-13 checkpoint so the work can be picked up cold. Everything here was measured on this repository's
tools; the diagnostic scripts are in `tools/dev/` (see "How to look at it").

## State

* The tee is the weakest garment. Skirt and chinos are stable (standing and walking, six presets; `docs/STATUS.md`).
* Standing and walking: curvy and athletic tees slip off one shoulder, on petite and average a sleeve rides up, tall is fine
  since the half-sewn shoulder seam was fixed (`8fbd0f7`). Seam lists are complete (shoulder seams, side seams, sleeve cap to
  armhole, underarm), so the failure is the **start pose**, not missing seams.
* The experiment of this checkpoint (sleeve cap wrap and calibration, `opt.sleeveCap`, `SLEEVECAP=1` for `tools/fs_plan.js`)
  is **off by default**. With it off the tee plans are byte-identical to commit `6e2b8ff` on all six presets (same raw
  files), so nothing about the tee changed in the checkpoint; the code is kept for the next attempt.

## What the start pose looks like (frame 0, petite, experiment on; front, side and 45 degree views: `docs/images/tee_start_petite.png`)

* The front and back pieces cover the torso like a tank top. The **shoulders are bare**: the shoulder seam sits on the neck
  and trapezius slope and ends about 5 cm medial of the shoulder tip; the deltoid is not covered.
* The sleeve is a short tube round the middle of the upper arm; its top is well below the armhole edge. A shoulder cap that
  should reach over the deltoid to the shoulder tip is missing.
* With the experiment on, the cap edge is still 77 to 85 mm (mean) from the armholes, up to 143 mm at the cap top.
  Start positions of the petite cap edge and the armhole it is sewn to, in cm (x, y, z; `tools/dev/seampos.js`):

  ```
  sleeve-L cap edge                                     front armhole
  #26 (13.5,107.7,3.6)->(13.1,106.5,2.3)                #23 (14.3,111.4,4.0)->(14.7,110.4,3.8)
  #20 (25.7,120.7,-0.3)->(22.8,120.1,4.1)               #29 (13.6,129.8,-2.8)->(13.3,126.7,-2.4)
  #22 (19.6,118.4,5.7)->(17.8,115.6,5.9)                #27 (15.4,123.6,-2.9)->(14.8,120.5,-0.2)
  ```

  The lower cap edge is within 1 to 4 cm of the armhole; the upper part is about 10 cm too lateral and 9 cm too low.

## Why (root cause)

1. **The wrap hull leaves out the shoulders.** `B.sections` (`app/src/body.js`) is the convex hull of the horizontal section of
   the *torso triangle set*, "arms and hands excluded" (`M.sets.section`). `makeWrapRows` mode `fit` lays the front and back
   rows on that hull by fraction (a common scale `kk = per / P`), so the armhole edges end at the hull silhouette. Petite, left
   side, `node tools/dev/shape.js petite` (cm):

   | y | torso hull max abs x | arm vertices x |
   | --- | --- | --- |
   | 120 | 15.4 | 10.7 to 21.3 |
   | 122 | 16.8 | 11.2 to 21.2 |
   | 126 | 14.1 | 13.4 to 19.0 |
   | 128 | 12.8 | 10.9 to 17.5 |
   | 130 | 10.1 | 12.2 to 12.6 |

   Landmarks: acromion (19.0, 126.3, 1.3), shoulder joint (15.9, 122.9, 0.6), cervicale y 132.0. At the acromion height the hull is
   14.1 cm wide, 4.9 cm short of the shoulder tip, and the pattern's shoulder tip (about half the shoulder width, 19 cm from the centre front) lands on the
   hull silhouette at x of about 13.6.
2. **The sleeve tube starts at the shoulder joint, not at the shoulder tip.** `armPos` wraps the sleeve round the arm axis
   (`body.arms[side]`, shoulder joint to elbow to hand). The cap peak lands on the lateral top of that tube (x about 23 to 26,
   y about 120), while the armhole top is at the neck-side shoulder slope (x about 13 to 15, y 120 to 130).
3. **The default Teagan neckline is wide** (neckline point about 11 cm from the centre front: `necklineWidth` 30 percent,
   `necklineDepth` 25, `backNeckCutout` 8), so the shoulder seams sit on the steep outer shoulder and the garment slides
   sideways. `OPTS='{"necklineWidth":0.12}'` (a normal crew neck) helped curvy, plus, average and tall standing, not petite and
   athletic. It is not adopted yet (`DESIGN_OPTS.teagan` in `tools/freesewing/export.mjs` would take it).

## Tried

* Top rows keep their row extents (`5329486`): the shoulder lines no longer cross at the neck.
* Mate chains of near-equal length sewn end to end (`8fbd0f7`): the tall tee's half-sewn shoulder seam.
* Lowering the shoulder line by 2 to 4 cm: helped tall, made athletic worse (not kept).
* `wrap.cap` (`shared.js`, `armPos`): rows above the underarm line use the underarm row's angular scale, so the cap climbs the arm
  instead of each narrow row closing into a full ring, and the axis may run above the joint (negative `s`). With it, a per-body
  search of `sTop` (-0.14 to 0.06 m) minimises the mean distance from the cap edge to the armhole polylines. Result: no
  big improvement (above). Kept behind `opt.sleeveCap`.
* Arm clearance in the gait (`ARM_GAP`, `armAbduct`; curvy 0.202, plus 0.333, others 0.14): arms stay 2 cm clear of the torso hulls
  below the armpit. Needed so the sleeves are not squeezed against the torso; not specific to the tee.

## Done 2026-10-15: dressing in an A-pose (idea 3, Godot editor)

`godot/editor.gd`: a garment with an `arm` wrap is built and sewn on the body with the arms raised sideways
(`DRESS_ABDUCT` 0.55 rad; `BodyGen.stand_at`), held 26 frames (the seams close in 20), then the arms come down over 18
frames to the standing pose (the collider is rebaked each of those frames). `FL_DRESS=0` turns it off. Measured on
the six presets at preview quality (upper-arm skin with cloth within 15 mm, both arms; frame dumps):

| | standing, frame 59 | walking, frame 111 |
| --- | --- | --- |
| arms down (before) | 84.4 % | 87.1 % |
| A-pose dressing | 88.7 % | 88.7 % |

Seam gaps stay at 1-4 mm. By eye (6-preset montages): standing, petite and tall sleeves are attached where they were
torn; walking, tall, curvy and plus are clean; petite and average still crumple one sleeve. The rest is ideas 1 and 2
(the cap rows themselves). Also note: much of the "skin through the sleeve" seen in plain renders was cloth lying on the
arm at ~1 mm (z-fighting); the editor now draws the body 2.5 mm inside its collision surface, which hides it.

## Ideas, in the order I would try them

1. **Give the shoulder rows a hull that includes the shoulder.** For `fit` rows from the armpit up to the neck use the union of the
   torso section and the arm vertices above the armpit (blend in over about 6 cm so there is no jump at the armpit), and map
   those rows by arc length (`kk` of 1) instead of the garment-wide scale, so the armhole edge ends at the front or back of the
   arm root and the shoulder tip at the acromion. Then switch the sleeve experiment on again (`SLEEVECAP=1`) and look at
   `START=1 node tools/dev/gaps0.js`: the goal is a mean cap to armhole gap well under 2 cm on all six presets.
2. **Drive the start positions of the cap edge from the armhole.** Plan-time control points (`wrap.pull`: pattern point, 3D
   offset) with a falloff of about 8 cm along the sleeve, so the cap starts on the armholes and the rest stays on the arm
   tube. Only works if idea 1 stops the pulled cap from sitting inside the deltoid.
3. **Dress in an A-pose.** Start the body with the arms abducted (the gait's `armAbduct` up to 0.5), sew, then lower the arms over
   about 40 frames (the worker already updates the collider per frame in walking runs). This is how CLO and Marvelous Designer
   users do it.
4. **Normal neckline** (`necklineWidth` 0.12) for Teagan, once the shoulders rest properly; regenerate the six Teagan raw files.
5. Check the sleeves do not ride up on petite and average, with `cotton_jersey` (stretch 250/100 N/m, friction 0.6/0.45).

## How to look at it

Paths assume the repo at `/home/claude/failoring` and a scratch directory `$SK` (default `/tmp/failoring-sk`); the JavaScript
tools hard-code the repo path, adjust with sed when it differs.

```
node tools/dev/shape.js petite                      # torso hull half-widths vs arm vertex extents, landmarks
node tools/fs_plan.js app/assets/freesewing/raw_teagan_petite.json petite plan.json        # add SLEEVECAP=1 for the experiment
STAND=1 FABRIC=cotton_jersey PLANFILE=plan.json SETTLE=60 DUMP=dump.json node tools/walk_sim.js file petite 3 out
POS0=1 node tools/render_dump.js dump.json petite start.png front,side,a45 0.85,1.35 420   # start pose
START=1 node tools/dev/gaps0.js dump.json           # seam gaps by piece pair at frame 0
node tools/dev/seampos.js dump.json sleeve-L front start    # seam edge endpoints in cm
node tools/dev/shl2.js dump.json petite             # shoulder seam and armhole heights, start and end
tools/dev/zz.sh <planprefix> <tag>                  # six standing sims + montage (FABRIC, YR, FW, VIEWS, PICK env)
tools/dev/ps6.sh <planprefix> <tag> [pieceIdx]      # six walking sims with per-piece statistics
```
