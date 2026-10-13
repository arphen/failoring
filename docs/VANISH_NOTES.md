# Vanishing garments: the strapless A-line dress (DOGFOOD_REPORT P0-2)

Branch `fix-vanish/godot-editor`, from `origin/godot/editor-v3` (bfbb35a). Date 2026-10-13.

## Summary

* The dress does not fall because of a port bug. It falls because the pattern cannot be sewn onto this body at
  these heights. The top seam is still 190 to 200 mm open at rest. When the body starts to walk, that seam is pulled
  shut in about two frames, the cloth is thrown off the body, and it lands on the floor. The browser solver does the
  same thing with a different settle count.
* The editor hid it. The status said "done", the ease readout showed +0.0%, and nothing looked wrong until the
  mannequin had no visible garment.
* Changes: per-garment health checks in the editor (`godot/garment_health.gd`) that show a clear message, tint the
  offending pieces red, and keep the problem listed until a "Reset to start" button is pressed. Nothing in the solver
  changed.

## Reproduction

The dogfood report did not include the plan JSON, so the plan was rebuilt from its description: two rectangles
turned into trapezoids, outline `[[-0.31,0],[0.31,0],[0.2,0.8],[-0.2,0.8]]` (62 cm at the bottom, 40 cm at the top,
80 cm tall), grain 90 degrees, Height 45 cm, Front at Around 0, Back at Around 180. Edges are numbered 0 bottom,
1 right side, 2 top, 3 left side. Seams: Front 1 to Back 3, Front 3 to Back 1, Front 2 to Back 2, all `reverse: true`
(the sew tool always writes that). Variant A has Back at 0 degrees and seams Front 1 to Back 1 and Front 3 to Back 3
(the first attempt in the report).

Scratch only, not committed: the GDExtension was built against godot-cpp `godot-4.3-stable` in a scratch tree,
and the Godot project was copied to scratch with the new library. The committed `godot/bin/libfailoring.so` is still
the stale binary; it was not touched.

Browser (wasm, `tools/walk_sim.js file average ...`, average preset, walking, standard quality):

| run | settle | result |
|---|---|---|
| A (Back 0 deg, wrong pairing) | 40 | ends on the floor at x 0.09 to 0.27, z 3.78 to 3.99 m (about 3.8 m from the body) |
| B (Back 180, correct pairing) | 40 | stays on the body, y 0.41 to 1.15 m, seam gap about 193 mm from frame 1 |
| B | 48 | the seam snaps shut around frame 120, cloth ends on the floor at z -0.67 to -0.46 m |

Godot editor (headless, the real `editor.tscn`, `_on_open_file` and Play, standard quality, walking):

| run | KE at the end | cloth at the end | report |
|---|---|---|---|
| A | 0.0282 J | on the floor, 3 to 4 m away | 0.028 J |
| B | 0.508 J | on the floor, x -4.39 to -4.12, z 2.89 to 3.17 m | 0.508 J, 4.4 m |
| B, standing (no walk) | 5.7e-5 J | on the body | not run |
| Pencil skirt (`plan_penelope.json`), walking | 0.0043 J | on the body, seams 40 mm | the healthy control |
| Tee (`plan_teagan.json`), walking | 0.0078 J | on the body | the healthy control |

The Godot numbers match the report to the digit, so the reconstructed plans are faithful.

## Root cause

1. **The pattern does not fit the body (B).** Measured on the average body (`godot/assets/average/body.obj`), the
   chest section at 1.25 m has a convex-hull perimeter of about 110 cm. The two top edges total 80 cm, so the top
   seam cannot close. In Godot the top seam is 190 to 200 mm apart at rest, and the side seams close (3 to 4 mm).
   The browser solver shows the same open seam (about 193 mm). The port is consistent with the browser, so this is
   not a port bug.
2. **The solver throws the cloth off (B, walking).** The open seam closes in about two frames when the body starts to
   walk (Godot: frame 65 gap 183 mm, KE 0.13 J; frame 73 gap 2 mm, KE 1.4 J; frame 81 KE 3.0 J, 179 mm off the body).
   After that the cloth goes to the floor. The seam ramp (20 frames) and stiffness (20000 kN/m) are fixed per
   garment, so nothing limits the pull on a seam that starts open. The browser survives with settle 40 and fails with
   settle 48, so the outcome depends on timing.
3. **Wrong placement (A).** Both panels start at Around 0, so the second panel begins on top of the first, goes
   through the body, and is ejected. This is a plan error, and the editor gave no hint.

Not the cause, checked: the wrap start positions (bounding boxes of the start cloth match the browser exactly),
the body collider (same 0.007 m grid in both), and the walking pose (the same plan walks cleanly when it fits, as the
pencil skirt shows).

Physically, a strapless dress that cannot be sewn closed should gape or hang. It should not fly 4 m. The editor
should say that a seam does not close, and the solver should not fling cloth. The first half is done here. The
second is a solver change and is listed under follow-ups.

## What changed

* `godot/garment_health.gd` (new, `FailoringGarmentHealth`). One instance per simulation build. `check()` runs after
  every solver frame in `editor.gd` `_collect()`, on the main thread with no step in flight. It reads the cloth and
  `sim.body_eval` (no stepping). Per garment it reports:
  * falling off: mean distance from the body beyond the start more than 0.15 m (message: "Garment is falling off:
    check seams and placement (<name>: ...)");
  * on the floor (lowest vertex below 3 cm, started above 10 cm);
  * below hip height (top below 0.5 m, started above 0.8 m);
  * invalid numbers (NaN);
  * a seam still more than 100 mm apart on the body (measured with the same pairing as the stitch lines), reported
    once per garment with its gap;
  * a whole-scene energy spike above 0.25 J after the first four frames.
  Problems are latched for the build: a garment that flew off and came back is still reported until Reset.
  The start baseline is measured from the start positions. Pieces named by a problem are returned as `hot`.
* `godot/editor.gd`: the check is built in `rebuild()` and run in `_collect()`. The text goes above the stats line
  in `_render_stats()`, so "done" is no longer the only status. New "Reset to start" button next to Restart. It
  calls `_on_reset_to_start()`: stop, time to 0, `rebuild()`. Unlike Restart it does not re-draft drafted garments.
* `godot/table.gd`: a flagged piece is drawn with a red 4 px outline (a few lines).

Results on the scratch harness (`_on_open_file` plus Play, headless):

* Dress B: "Garment is falling off: check seams and placement (Strapless A-line (dogfood): on the floor)",
  "Energy spike (0.51 J)", "a seam does not close (199 mm apart)". The stats line keeps "done" below the warnings.
* Dress A: "Garment is falling off ... on the floor", "Energy spike (0.27 J)".
* Reset to start on dress B after the fall: frames go to 0, the fall message clears, and the seam warning returns
  as the cloth re-simulates (the fit is still wrong).
* No false positives on the pencil skirt (no message) or the tee (no message).

## Not verified

* Only headless. The red outline, the new button, and the message layout were not looked at. No screenshot.
* Only the average preset and three garments were used to set the thresholds. The fall thresholds (0.15 m mean,
  0.5 m max, 3 cm floor) and the seam limit (100 mm) need checking on tall, petite and curvy bodies, and on a
  flared skirt or jacket.
* The interactive click path (a real mouse on the button) was not run.
* "Parameter m is null" errors appear in the healthy pencil skirt run too, so they are pre-existing. Not investigated.
* The browser app does not get this message yet. Its worker reports `gap` and `ke`, so the same checks are possible.
* The committed `godot/bin/libfailoring.so` is still stale. No binaries were committed.

## Follow-ups

1. Solver (not in this branch): do not let a seam with a large open gap snap shut in a few frames. A longer pull-in,
   or a cap on the seam force while the gap is above a limit, would give a garment that gapes instead of flying.
2. Sew tool: warn when the sewn perimeter does not match the body (the top seam here: 80 cm against 110 cm), before
   Play. This is P1-2 in the report.
3. Copy the health check and message to the browser app.
4. Restart re-drafts drafted garments (`regen_and_rebuild`). That is the P0-1 path and was left alone.
