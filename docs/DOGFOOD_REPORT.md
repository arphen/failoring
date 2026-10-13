# Dogfood report: designing a garment in the Godot editor

Branch `dogfood/godot-editor` (from `origin/godot/editor-v2`, commit 9b093b4). Date 2026-10-13.
Role: a tailor who has not programmed, designing a garment from scratch with the editor's own controls.
No application code was changed. Only this file was added.

## Summary

* **A dress does not work yet.** I built a strapless A-line dress from a blank rectangle (two panels, three
  seams). After Play the mannequin had no visible garment. In the one run that could be measured, the cloth ended
  4.4 m from the body on the floor. Two dress builds were tried, both invisible.
* **Drafted plans (Trousers, Tank, T-shirt) throw away edits.** Any structural edit is reverted about 0.25 s later,
  and the undo history is cleared. A FreeSewing plan (Tee) keeps the same edit.
* **No way to type a measurement.** Every length is set by dragging with a 5 mm snap. There is no tape, no numeric
  field, no grading, no seam allowance, no print or PDF export, and no import other than the editor's own JSON.
* **Placement is unexplained.** Duplicate copies the placement of the original, so a back panel lands on the front
  until "Around" is set to 180 by hand. "Height" and "Clearance" have no reference point in the UI.
* **Works well:** cutting table basics (add, drag, snap, mirror, duplicate, delete), sewing two edges, seam
  ease readout, undo and redo for pattern edits, darts built from a double-click plus sew, saving and reopening
  a pattern (identical round trip), the timeline, the fabric dropdown.

## Method and limits

* Godot 4.3 stable (Linux), run under Xvfb with OpenGL (llvmpipe) on a shared two-core VM. Timings are inflated.
* The committed `godot/bin/libfailoring.so` is stale: with it the editor cannot start (`Native class
  "FailoringSim" not found`). I rebuilt the GDExtension from this branch's `gdextension/` and `cloth_core/` in a
  scratch directory against godot-cpp 4.3. Nothing binary was committed.
* Interaction was driven with real `InputEventMouseButton`, `InputEventMouseMotion` and `InputEventKey` events on
  the table and with real Button, Slider and TabContainer signals. The Save and Open dialogs were driven by emitting
  `file_selected` on the editor's own FileDialog and then hiding it. Scripts and screenshots are in scratch, not in
  this repo.
* "run" below means exercised in the editor. "code" means read from `godot/*.gd` only.

## What I tried, step by step

### 1. First look
* Steps: open `editor.tscn`, wait for the first solve, press Fit.
* Result: the default plan is "Tee (FreeSewing)". The status line says "Teagan t-shirt (FreeSewing)". The
  dropdown and the header name the same garment differently.
* Play on the tee at Standard quality: 635 verts, bake 570 ms, step about 400 ms per frame, 144 frames.
* **Verdict: works.** The dropdown and header naming is confusing.

### 2. Blank rectangle, rename, shape a front panel
* Steps: Plan dropdown "Blank rectangle". Piece tab: name field "Front", Enter. On the table, click the piece
  to select it, then drag each corner: (0,0) to (-0.31,0), (0.5,0) to (0.31,0), (0.5,0.5) to (0.2,0.8),
  (0,0.5) to (-0.2,0.8). Piece tab: Height slider to 45 cm.
* Result: outline `[[-0.31,0],[0.31,0],[0.2,0.8],[-0.2,0.8]]` exactly, on the 5 mm grid. Size 62 x 80 cm.
* **Verdict: works, but tedious.** Four drags for one trapezoid, no way to type 62 cm, and the labels show no unit.

### 3. Add piece, Delete
* Steps: Piece tab, Add a piece, Trapezoid. Then select the new "Piece 2" and press Delete.
* Result: the trapezoid appears at the right of the others (40 x 50 cm). Delete removes it and its seams.
* **Verdict: works.** The new piece is placed on the table, not on the body, and the UI does not say so.

### 4. Duplicate for the back, and placement
* Steps: select Front, Duplicate, rename "Back".
* Result: the copy has `theta 0`, the same as Front. It sits on the same side of the body. Setting "Around" to 180
  by hand fixed the placement (`theta 180`).
* **Verdict: confusing.** A back panel is not placed on the back. Nothing warns that a duplicate lands on the front.

### 5. Sew: side, side, top
* Steps: Sew tool. Click the Front edge, then the Back edge (edge 1 with Back edge 3, then Front 3 with Back 1,
  then top with top). Select tool to finish.
* Result: three seams, all shown in the Seams tab with ease `+0.0%`. The first Sew attempt used 1 with 1 and 3 with
  3 (Back at 0 degrees, and the other attempt with Back at 180). Both gave no visible garment.
* The hint line says "Sew: click two edges". Escape cancels a half-made seam (code).
* Sewing an edge that already has a seam replaces the old one silently (code, `table.gd` sew branch).
* **Verdict: works, but the pairing is not shown.** Which edge pairs correctly depends on how the pieces are
  placed around the body. The built-in pencil skirt uses front 1 with back 5 (a side edge on the other side). The
  editor gives no hint of this, and a wrong pairing gives no warning.

### 6. Seams tab
* Steps: Seams tab after sewing.
* Result: rows read `Front #1 <-> Back #3   80.8 / 80.8 cm - ease +0.0%`. Flip (`⇄`) and delete (`✕`) buttons.
* The number after `#` is the edge index (0-based), the same as the table labels. The Piece tab calls a seam
  "(seam N)" with a different count. Two numbering schemes for different things.
* **Verdict: works, but the numbering is confusing.** The ease readout is the only tailor-facing check on
  seam lengths.

### 7. Undo and redo
* Steps: after the three seams, Ctrl+Z, then Ctrl+Shift+Z, then Ctrl+Y.
* Result: Ctrl+Z removed the last seam. Ctrl+Shift+Z restored it. Ctrl+Y with nothing to redo did nothing.
* Code: slider changes (grain, Around, Height, Clearance), body and fabric changes do not push undo. Pieces
  dragged without moving still push an undo entry.
* **Verdict: works for pattern edits.** Confusing that sliders are not undoable.

### 8. Dart on the front top edge
* Steps: Select tool. Double-click the top edge (it adds a corner at the click point). Drag the new corner from
  (0,0.8) down to (0,0.6). Sew tool: click the two dart legs (edges 2 and 3).
* Result: the outline is `... [0,0.6] ...`. The double-click silently removed the top seam to the back (edge 2
  was split, and seams on a split edge are dropped). Sewing the two legs gave a seam of `28.3 / 28.3 cm`, +0.0%.
  The sim ran (KE 0.004 J).
* **Verdict: possible, but a trap.** A dart is a notch plus a sew of its two legs, with no dart tool, no marking,
  and no seam-loss warning.

### 9. Curved hem
* Steps: double-click the Back hem to add a corner, drag it down.
* Result: one more corner, straight segments only. No curve handle. Edges shorter than 8 mm make a piece invalid
  (code: `valid_pieces` in `table.gd`), and an invalid piece is dropped from the simulation.
* **Verdict: impossible as a curve.** Only straight edges, approximated by many corners.

### 10. Snap
* Steps: uncheck "snap 5 mm", drag a corner, check the outline, re-check the box.
* Result: `[-0.203, 0.803]`, off the grid. Re-enabling snap restores the grid.
* **Verdict: works.**

### 11. Delete a corner, delete a piece, undo
* Steps: click a corner (it is selected), press Delete. Then click the piece body, press Delete.
* Result: the corner went (5 to 4 corners). Delete with the body selected removed the whole piece at once, with
  no confirmation. Ctrl+Z restored it.
* A script error appeared on that Undo: `Invalid type in function 'piece_by_id' ... Nil to String`. Undo applied
  but `_fix_selection()` aborted because nothing was selected (`sel == null`).
* **Verdict: works, but the whole-piece Delete has no guard.** The undo error is a P2 bug.

### 12. Mirror
* Steps: select a piece, press Mirror.
* Result on the FreeSewing tee: the change stayed (outline changed, undo count 2). On the drafted T-shirt: the
  change was reverted within about 0.25 s and the undo history was cleared (see shortcoming P0-1).
* Code: Mirror reflects about the piece's own bounding-box centre, in place. There is no symmetric edit of both
  halves.
* **Verdict: works on non-drafted plans, broken on drafted plans, and not symmetric.**

### 13. Grain
* Steps: Piece tab, "Bias 45°", then "Straight 90°".
* Result: grain angle goes 1.5708 to 0.7854 to 1.5708 rad. The arrow on the table turns.
* **Verdict: works.** The button labels do not say which direction is the warp or where the grain line is marked.

### 14. Dress drape (the main task)
* Steps: Front and Back as in 2 to 5, Fit, Play.
* Attempt A (Back at 0 degrees, sewn 1 with 1, 3 with 3, top with top): the Play finished (KE 0.028 J, status
  "done"). No garment is visible on the mannequin.
* Attempt B (Back at 180 degrees, sewn 1 with 3, 3 with 1, top with top): the Play finished with KE 0.508 J,
  against 0.0078 J for the tee. The cloth's bounding box was x -4.39 to -4.12 m, y 0 to 0.10 m, so it lies on the
  floor 4 m from the body. Repeated in a separate run with the same result.
* **Verdict: impossible for this dress as built.** The editor gives no sign of why. The mannequin looks empty.
  Whether the cause is the pattern (pairing or seam orientation), the solver, or the strapless top has not been
  isolated. Opening the same JSON in the browser app would separate the two.

### 15. A single unsewn panel
* Steps: a 50 x 50 cm rectangle, Height 60, 45, 100 and 120 cm.
* Result: at every height the cloth ended on the floor, 0.5 to 2 m from the body (y 0 to 0.10 m).
* This is plausible for one unsewn panel with nothing to hold it. The editor gives no explanation.
* **Verdict: confusing.** A beginner will read "the piece fell off" as the editor being broken.

### 16. Save and open
* Steps: Save pattern (dialog: choose the file, emit `file_selected`, hide). Switch plan to Tee. Open pattern,
  choose the file, hide.
* Result: the file was written as `failoring-pattern/1` JSON. After Open the pattern is identical to the saved one
  (pieces and seams), undo count 0, status "404 verts - press play".
* **Verdict: works.** Open accepts only `*.json` (the filter says "Pattern JSON").

### 17. Drafted plan edits (T-shirt, Trousers, Tank)
* Steps: plan "T-shirt (drafted)", select front, Mirror, then change Body Height to 185 cm.
* Result: the Mirror was reverted and the undo history cleared. Changing Body Height also reset the pattern to the
  fresh draft, so the Mirror did not survive that either.
* Code: `_on_debounce` calls `regen_and_rebuild()`, which for `pants`, `tank` and `shirt` calls `_resolve_plan()`
  and `_reset_working()` on every structural change, not only on an explicit Re-draft.
* **Verdict: impossible to edit a drafted plan.** This is P0-1.

### 18. Body presets and grading
* Steps: Plan "Blank rectangle", Mirror Piece 1, Body tab, "Petite" preset.
* Result: the pattern outline is unchanged. The body changes, the pattern does not.
* **Verdict: impossible to grade.** No grading rules exist. A pattern is fixed in cm and only the fit changes.

### 19. Layering
* Steps: Duplicate Piece 1, rename "Lining", set Clearance 30 mm, Play.
* Result: not completed. Duplicate worked on a later check, but I did not get a clean Play. Code: there is no
  notion of a layer. Pieces sit on the same wrap, and self-collision is off by default, so pieces pass through
  each other.
* **Verdict: not tested properly.** Assume impossible for now.

### 20. Fabric, Mesh, Motion, Pattern tab
* Steps: Fabric tab dropdown, read the note. Mesh and Motion dropdowns. Pattern tab.
* Result: the Fabric readout for the selected cotton poplin shows weight, thickness, stretch, shear and bending.
  The fabric list comes from `materials/fabrics.json`. Mesh shows Draft, Standard, Fine and Photo. Motion shows
  Standing and Walking. The Pattern tab shows a cutting list and a note that the plan is hand-drafted.
* Fabric, Mesh and Motion were set but their effect on the drape was not compared.
* **Verdict: works, with jargon.** "Seam k 6-50 kN/m", "Pull-in fr", "Bending µN·m", "Shear N/m/rad" need a glossary.

## Things I could not try, or only read in the code

* **Print or export (PDF, PLT, SVG, DXF) at 1:1.** Not present. The browser app exports JSON only too. Impossible.
* **Import a pattern from PDF, DXF or SVG.** Not present. Open accepts JSON only. Impossible.
* **Seam allowance.** No concept. The outline is the sewn line and the cut line at once. Impossible.
* **Notches, buttons and buttonholes, zips, collars, pockets, hems.** No tools for any of them. A pocket or a hem
  band can be made as an extra piece sewn on, but I did not try it.
* **Measuring tape.** Not present. Edge lengths are read from the labels.
* **Units.** Only cm. Edge labels show a number with no unit ("0: 62.0"). No inch option.
* **Symmetric editing.** Not present. Mirror is per piece.
* **Copy and paste (Ctrl+C, Ctrl+V).** Not present. Only Duplicate. (Code: only Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z are
  bound.)
* **Ease and grading to other sizes.** Ease readout works for equal lengths only (0%). Not tried with unequal
  lengths. Grading: impossible (see 18).
* **Self-collision on**, the Shoot tab, and the Demo. Not run.
* **Minimum edge rule (8 mm)**, the wrong-pairing case for seams, and undo for slider changes. Code only.
* **Space key.** Code: Space starts and stops playback and also pans the table, so a pan will toggle playback.
* **Layering with a real garment on top of another.** See 19.
* **Browser comparison.** Not done, so I cannot say whether the dress fault is in the Godot port or in the
  solver.

## Prioritised shortcomings

### P0: blocks designing a garment

**P0-1. Drafted plans lose every edit.** Any structural change to Trousers, Tank or T-shirt is reverted about 0.25 s
later, and undo is cleared (`editor.gd` `_on_debounce` to `regen_and_rebuild`, which re-drafts for `pants`, `tank`,
`shirt`). Fix: re-draft only on the explicit Re-draft button, or keep an "edited" flag per plan and skip the
re-draft when it is set. Also confirm before any re-draft. Effort: S (half a day, plus a test).

**P0-2. A garment can vanish with no explanation.** The strapless dress ended 4.4 m from the body on the floor with
KE 0.5 J, against 0.008 J for the tee. The status line only says "done". Fix: first, run the same JSON in the browser
app to see whether the solver or the port is at fault. Then show a per-garment status ("panel X not on the body",
"solver unstable, reset") and a Reset placement button. Effort: M to L, depending on the cause.

**P0-3. No way to type a measurement.** A seamstress thinks in centimetres. Fix: add an edge-length field to the Piece
tab (move the far corner along its direction to hit the length) and x/y fields for a selected corner. Effort: M.

**P0-4. Placement is not explained.** Duplicate copies the placement, and the back panel lands on the front.
"Height", "Clearance" and "Around" have no reference. Fix: buttons for Front, Back and Side that set Around to 0, 180,
90 and 270. Make Duplicate offset Around by 180 when the piece is named "back", or ask. Label Height as the hem or
top height with a small diagram. Effort: S for the buttons, M for a diagram.

### P1: painful

**P1-1. Plan dropdown, Re-draft and Open wipe edits.** `_on_plan`, `_on_redraft` and `_on_open_file` call
`_reset_working()`, which clears undo, with no confirmation. Fix: a "Discard your changes?" prompt when the pattern is
dirty, and a dirty flag on the Save button. Effort: S.

**P1-2. Sew gives no pairing guidance and no warning.** The tool does not say which edge pairs are correct around the
body, and it does not warn when two edges are very different lengths. Fix: show the matching direction (an arrow on
each edge) and a warning above 8% ease. Effort: M.

**P1-3. Splitting an edge or re-sewing silently removes seams.** Double-click on a sewn edge and the seam is gone.
Fix: keep the seam by re-attaching it to the nearer half, or show a confirmation. Effort: S to M.

**P1-4. No seam allowance and no printable output.** Without seam allowance a cut pattern is wrong, and without export
it cannot be printed. Fix: a per-edge allowance field (default 1.5 cm), an offset outline for cutting, and a 1:1
PDF export (page tiling with a scale check square). Effort: L.

**P1-5. Sliders and body changes are not undoable.** Undo only covers the pattern. Fix: push undo on slider
`drag_started`. Effort: S.

**P1-6. Whole-piece Delete is one key press with no guard.** Fix: Delete on the body asks, or needs a second
press. Effort: S.

**P1-7. No curves, no darts tool, no notches, no closures, no hems, no layers.** Fix ordering: a dart tool (mark
apex and two legs, sew them) and a curve handle (Bezier on an edge, flattened to corners for the solver) first.
Effort: L each.

**P1-8. No copy and paste, no symmetric editing.** Fix: Ctrl+C/Ctrl+V on pieces (offset), and a symmetry toggle that
mirrors every edit across the centre line. Effort: M.

**P1-9. No import of a pattern.** A PDF or DXF pattern from a book cannot be brought in. Fix: a DXF importer for
closed polylines (most pattern software exports them) first. Effort: M.

**P1-10. Build hygiene.** The committed `libfailoring.so` does not load (`FailoringSim` not registered). A fresh
checkout cannot run the editor until someone rebuilds. Fix: build it in CI or document the step at the top of
`GODOT_EDITOR.md`, and remove the stale binary. Effort: S.

### P2: polish

**P2-1. Undo after deleting the selected piece (or after a click on empty table) raises a script error.** Fix: guard
`_fix_selection()` against a null `sel`. Effort: XS.

**P2-2. Edge labels have no unit and are zero-based.** Show "62.0 cm" and 1-based numbers. Effort: XS.

**P2-3. Two numbering schemes.** The Seams tab uses edge index ("Front #1") and the Piece tab uses seam count
("(seam 1)"). Fix: one scheme, named "edge 2" and "seam 1". Effort: XS.

**P2-4. Space both plays and pans.** Fix: pan on Space-drag only, play on a Space tap. Effort: XS.

**P2-5. Plan naming.** The dropdown says "Tee (FreeSewing)", the header says "Teagan t-shirt (FreeSewing)". Use one
name. Effort: XS.

**P2-6. Label clutter.** On the FreeSewing tee the table labels overlap and hide the outlines. Show labels only for
the selected piece by default. Effort: S.

**P2-7. Jargon in the Fabric tab.** Rename "Seam k", "Pull-in fr", "Bending µN·m", "Shear N/m/rad" to plain
words with a tooltip each. Effort: XS.

**P2-8. Mirror's button label.** It mirrors in place, which a user will read as "make the other half". Label it
"Flip piece". Effort: XS.

## Unclear labels and missing explanations (for a first-time seamstress)

* **Plan dropdown:** "Blank rectangle", "Trousers (drafted)", "Tee (FreeSewing)". Nothing says what "drafted" or
  "FreeSewing" means, or that a drafted plan will reset.
* **Re-draft:** does not say that it discards every edit.
* **Mesh:** "Standard", "Fine (slow)", "Photo". Nothing says these set the cloth's fineness, nor what "Photo" costs.
* **Motion: Standing / Walking.** Walking is the default, with no hint of what it adds.
* **Piece tab:** "Angle", "Around", "Height", "Clearance", "Flare". Need a glossary.
* **Grain buttons:** "Straight 90°", "Cross 0°", "Bias 45°", "Bias 135°". The arrow shows the warp. Say "Warp along
  the length", "Cross-grain", and so on.
* **Trapezoid:** the wide edge is at the bottom of the table. Nothing says so when you add one.
* **Sew:** "Sew: click two edges" and "Now click the edge to sew it to (Esc cancels)". Not enough. Say which two edges
  and what the result will look like.
* **Seams tab:** "⇄" (flip direction) and "✕". Tooltips only. "Flip direction" means the seam's stitch order.
* **Seams tab note:** "Edge numbers are shown beside each edge length" (the numbers are shown without a unit).
* **Fabric tab:** "Seam k 2 to 50 kN/m", "Pull-in 0 to 60 fr", "Bending 12.0 / 9.0 µN·m". Need a glossary.
* **Stage buttons 1 2 3 4 and "Drape".** Unexplained. A user will not know these are cut, move, sew and drape.
* **Shoot tab and Demo.** Not explained.
* **Snap "snap 5 mm".** Does not say that it snaps corners to a grid, or that it applies to drags only.
* **Pattern tab.** Lists the cutting list in cm with no unit label in the heading.

## Evidence

Screenshots, the scripted runs and the saved JSON are in the scratch directory of this session. They are not in this
repository. The scripts can be shared on request.
