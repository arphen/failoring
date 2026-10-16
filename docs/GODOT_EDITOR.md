# Godot editor

The browser app with the native solver: cut pattern pieces on a 2D table, mark seams,
pick a body (preset or sliders) and a plan, watch the pieces fly to the body, sewn and
drape on the timeline, read the pattern info, take studio photos, run a demo. Presets
and sliders both regenerate the body live; plans resolve per state.

## Run

Needs `godot/bin/libfailoring.dylib` on macOS (`libfailoring.so` on Linux); build it with
`tools/build_gdext.sh` (the committed copy is stale; see `docs/NATIVE_NOTES.md`). Open `godot/` in Godot 4.3 and run `editor.tscn`
(it is the project's main scene). First launch imports assets (`.godot/`, `*.import`;
untracked, see `godot/.gitignore`).

* Left tabs: Library (garment cards with a 2D pattern preview; double-click or Add puts a
  garment on the mannequin; save the garment on the table, rename, delete) and Controls
  (plan selector, re-draft, mesh quality, motion, open/save pattern JSON, demo, live sim stats).
  See "Garment library and outfit" below.
* Cutting table: Select/Sew tools, Fit, snap 5 mm; drag pieces and corners,
  double-click an edge to add a corner, Del removes (a corner, or a whole piece with its seams: the status
  line says so and Ctrl+Z restores it), wheel zooms, V/S/F/Esc work. Space plays and pauses the timeline; the
  table pans with the middle or right mouse button only. Splitting a seamed edge or re-sewing an edge drops the
  seam it replaces: the status line warns and Ctrl+Z restores it.
* 3D: orbit drag, wheel zoom, arrow keys orbit, Page Up/Down zoom, F / Home / double-click fit; the view chips along the bottom of the pane
  glide to Front, 3/4, sides, Back, Face, Torso, Legs, the Atelier, the fitting Mirror and the cutting Table (docs/LOOK_NOTES.md). The window
  starts 3D-first (the Atelier layout); Library, Table and Inspector are one click or Ctrl+1..4 away. Timeline scrub through cut, move, sew, drape.
* Right tabs: Outfit, Piece, Seams, Body, Fabric, Pattern, Shoot.

## Garment library and outfit (`godot/library.gd`, `library_panel.gd`, `outfit.gd`)

The library holds the garments the editor can draft or load: built-in plans, FreeSewing
(tee, pencil skirt, chinos, for the nearest preset), drafted garments (trousers, tank, shirt)
and saved garments in `user://library/` (JSON, format `failoring-library/1`, category tops,
bottoms, skirts or custom). Each card shows the pattern outlines and the preset it is fitted for.

The outfit is the list of garments on the mannequin, inner first. Each entry has a fabric, a
colour, a visibility, a lock, and a layer (its index). The Outfit tab lists them: Show, lock,
Up/Down, Remove, fabric, colour, and Re-draft when the outfit's body preset changed. Clicking a
name makes that garment the active one: the cutting table edits only the active garment.

The shown garments are merged into one plan (`outfit.gd`) and simulated together:

* ids get a garment prefix (`g<uid>_<piece>`), seams keep their edge numbers, and each piece keeps
  its fabric;
* each layer further out adds 8 mm to the pieces' wrap clearance, so the outer garment starts
  outside the inner one (tee under chinos: the chinos wrap 8 mm further out);
* the solver's self collision is turned on when two or more garments are shown. It is a global
  vertex-triangle test over all cloth triangles (`cloth_core` `solveSelfCollision`), so garments
  collide with each other; seams never join two garments.

Re-draft: drafted garments (Trousers, Tank, T-shirt) follow a body change while they are unedited. Once
the pattern is edited on the table, the garment keeps its pieces through every rebuild and body change (the
status line says so); Re-draft fits it to the body again. FreeSewing and saved garments keep their outline until
Re-draft is pressed, which drafts them for the current preset. Plan, Re-draft, Open, and removing a garment ask
for confirmation first when the garment has unsaved edits. Locking a garment
freezes its pattern: no table edits, no re-draft, no plan change; its fabric and visibility can still change.

Limitations: no seams across garments; the table shows the active garment only; "Save pattern"
writes the active garment only; a saved garment's outline is not re-drafted unless it was drafted
from a FreeSewing or drafted source; collisions rely on the clearance steps and the solver's
self collision (no garment-specific friction or pressure).
* Bottom: timeline (|<< < play > >>, speed, stage buttons, scrub, +60 frames, restart).

Environment (checks, shots, matrix runs):

* `FL_AUTO=preset,plan,frames[,quality][,motion]` runs that many sim frames at T=4,
  saves `godot/docs-shots/<preset>_<plan>.png` and quits (needs a real window for
  pixels; headless it still simulates and exits, PNG blank).
* `FL_MEAS='{"bust":1.0}'` merges measurements over the preset (slider path).
* `FL_STAND=1` stays standing. `FL_T=0..4` starts the timeline there.
* `FL_TAB=name` opens a side tab (screenshots). `FL_UISHOT=path` saves the full window.
* `FL_PRESET=name` selects the preset for `tests/wrap_check.tscn` and `body_check.tscn`.
* `FL_SHOOT=1` with `FL_AUTO` runs the photo shoot, then quits. `FL_DEMO=n` runs the
  demo for n outfits, then quits.

Behaviour mirrors the browser worker (`app/src/worker.js`): quality edge/substeps/Newton
(draft/standard/fine/photo), seam stiffness, ramp frames, tension stiffness on, floor at
y = 0, gravity faded in over the ramp, then the stride walk (64-frame cycle, 24-frame
ease-in). Standing stops at twice the settle frames; walking loops the walk until
paused. Rebuild on any structural/body/fabric change (250 ms debounce).

## Cutting table (`godot/table.gd`)

Port of `app/src/editor2d.js` plus the piece edits of `app/src/state.js`: grid, piece
fill with selection and validity tint, fabric tint, grain arrow, name and edge labels
(index + cm), vertex handles, seam edge pairs with match ticks, scale bar, hover and
sew-edge highlights. Snap 5 mm, keyboard as in the browser (undo and redo: see below).

## Layout (`godot/layout.gd`)

Library | (Table | 3D view) | Inspector, in nested split containers: drag the gaps. The bar above them shows or hides each
pane (Ctrl/Cmd+1..4), stacks the table above the 3D view (Ctrl/Cmd+5), swaps their order, focuses the 3D view, or resets
the layout. The layout is kept in `user://settings.cfg` [layout]; `FL_LAYOUT_DEFAULT=1` ignores it (tests, screenshots).

## Construction geometry and right-click menus (`godot/guides.gd`, `godot/table.gd`)

Construction points and lines are kept per piece in `p.guides` and saved with the pattern; they are drawn, picked,
dragged and snapped to, never cut. A point is free (`x`, `y`) or follows a feature: the centre of mass of the cloth
(holes subtracted), the middle of an edge, a corner, the centre of a hole. Inserting or deleting corners keeps derived
points on their feature, or turns them into free points where the feature disappears; dragging a derived point frees it.
A line runs through two points, or through one at an angle (across, up). Tools: Point, Line; the Piece tab lists the
piece's construction geometry. Object snap (corner drags, construction point drags, the Point tool):
corners, edge middles, centres of mass, hole centres and construction points within 10 px, then construction lines,
then the 5 mm grid. The Hole tool snaps to construction points and lines only (then the grid), so a hole drawn near an
existing one does not jump onto its centre. Right-click (a right drag still pans) opens a menu for what is under the pointer: a construction
point (hole centred here, lines through it, line to another point, make free, delete), a corner (point on it, delete), an
edge (corner here or in the middle, point in the middle, sew to..., unsew), a hole (point at its centre, delete), a
piece (point at the centre of mass or here, hole here, duplicate, mirror, delete), a seam link (flip, delete). Holes are
picked and dragged with the mouse (a hole that would leave the piece goes back). Delete removes the picked construction
geometry, holes and seams first, then the corner or piece as before.

A poncho from scratch: Add shape > Square; right-click the square > Construction point at the centre of mass;
right-click the point > Cut a hole centred here.

Splitting a sewn edge (Split tool, double click, the edge menu, Curve) keeps its seam: the seam is cut at the new corner
and its partner's range at the same fraction of the seam (from the other end when the seam is reversed).

## Moving panels in 3D (`godot/place_view.gd`, `godot/place3d.gd`)

The 3D view's Move panels button (or M over the 3D view) shows the start positions (stage 3, before the drape) and a gizmo
on the selected panel: drag the panel to move it in the plane facing the camera, an arrow (red across, green up, blue
forward) to move along it, a ring to turn about that axis through the panel's centre; Shift snaps to 1 cm and 15°. The
release is one undoable action, stored in the piece as `p.place = {t, q}` (applied by plan_loader.gd after the wrap:
x' = q (x - c) + c + t, c the panel's centre) and saved with the pattern. Reset panel drops it. The Piece tab has the same
as Move across/up/forward (cm) and Tilt/Turn/Roll (degrees).

## Arrangement points and "Will it sew?" (`godot/arrange.gd`, `godot/sew_check.gd`)

As in CLO and Marvelous Designer: the 3D view's Arrange button (A over the 3D view) shows dots round the body the
garment starts on: on the torso at the shoulder, bust, waist and hip heights (front, back, sides, and between them), on
each arm (shoulder, upper arm, forearm) and each leg (thigh, knee, shin). Pick a piece (on the table or in the 3D view),
hover a dot to see a wire ghost of the piece there, click to put it there (one undoable action; its free 3D move is
dropped; the mirror twin follows). A torso dot sets the around-the-body placement and height; an arm or leg dot wraps the
piece round that limb. A set-in sleeve on an arm dot gets a cap that climbs the arm to the shoulder (wrap.cap, ported
from shared.js) and slides along the arm to where its seams start closest to the armholes (`Arrange.fit_sleeve`). A
sleeve that was never arranged gets the same at build time (only the simulated copy).

The Seams tab starts with "Will it sew?": for each pair of sewn pieces, on the start pose, green (the edges start close
and line up), yellow (3-8 cm apart: it sews but may crumple), red (8 cm or more, twisted, or the seam line runs through
the body). The 3D view draws the stitch lines in the same colours before the drape.

The dressing pose (garments with sleeves start with the arms raised, `BodyGen.stand_at`) uses torso sections that enclose
the shoulders (the arm triangles above the armpit are sliced with the torso), so a top's armholes reach the shoulder tip.

## Partial seams (`godot/seams.gd`)

A seam can join a part of an edge to a part of another edge, so a hole stays open where the two are not sewn: an armhole,
a vent, a slit. A side's range is stored as `[piece, edge, t0, t1]` (fractions along the edge); a whole edge is still
`[piece, edge]`. Ranges on one edge may not overlap, and a range is at least 0.8 cm long.

* **Seams tab:** each side of a seam shows its edge, the start and end in cm from the edge's first corner (type and
  Enter), and a **Whole edge** button. The seam's ease and warning use the range lengths.
* **Range handles:** a partial side has a small square at each end of its range on the table, in the seam's colour.
  Drag one along its edge (snaps to 5 mm when snap is on). A corner under the pointer wins over a handle.
* **Sew** still joins whole edges. A new seam is whole and is shortened afterwards with the Seams tab or the handles.
  (Choosing the range with the Sew clicks themselves is not done.)
* **Splitting an edge** that has a seam drops the seam, as before, with a message. Ranges on the edges after it move on.
* **Simulator:** the rebuild runs `Seams.expand` after merging the shown garments. It splits each outline at the range
  ends and rewrites every side as a whole sub-edge, so the solver sees only whole edges. The 3D view's stitches and stage
  poses use the split pieces too.
* Mirror and the clockwise to counter-clockwise normalisation map ranges with the edge (t becomes 1 - t).

Not done: the Pattern tab's seam summary and the garment health check still describe whole edges (a part seam is
listed with its whole edges). The Seams tab's numbers are the place to read a part seam.

Tests: `godot/tests/seams_check.tscn` (the range model, the expansion, and the drape: a tube sewn down both sides with the
top sewn all the way closes to 0.2 mm; sewn half way, its open half is 37 mm apart), `godot/tests/seam_edit_check.tscn`
(the Seams tab's function, refusals, the rebuild, and a handle drag with undo).

## Edit tools (`godot/cut_geom.gd`, `godot/table.gd`, the Piece tab)

* **Corners.** Drag a corner, type its x and y (Piece tab), double-click an edge or use the **Split** tool (press on an
  edge) to add one, Delete removes the selected corner. Seams on a split edge are dropped with a message, as before.
* **Curves.** The Piece tab's edge rows have **Curve**: the edge becomes 8 segments on a quadratic Bezier that bows 12 %
  of its length outward (positive bulge = outward for a counter-clockwise outline). The curve is stored as its corners
  plus a record `{a, b, bulge, n}` on the piece; the bulge field re-bends it, **Straighten** removes the record and keeps
  the corners. A curve whose end corners were moved can no longer be re-bent (the message says so).
* **Rotate.** The Rotate tool turns the picked pieces (or the one pressed) about the centre of the group; the grain turns
  with them. Shift snaps to 15 degrees. The Piece tab's **Turn by** field turns by a typed angle.
* **Scale.** The Piece tab's **Scale to** field scales the picked pieces about the group's centre (per cent of size).
  Refused, with a message, when an edge would get shorter than 0.8 cm.
* **Align and distribute.** With two or more pieces picked, the last picked piece is the reference: Left, Right, Top,
  Bottom, Centre across and Centre up move the others to it. With three or more, Distribute across or up keeps the two
  outermost and spaces the rest evenly.
* **Measure.** The Measure tool: two clicks; the distance in cm, across and up, shows in the status line and as a line
  on the table. Esc clears it.
* **Mirror** is per piece (the piece's own Mirror button, flipping it in place). A selection is not mirrored as a group.
* Every one of these is one named action in the History tab, and undo and redo work on each.

Not done: a selection-wide mirror, grading, and a rotation handle on the piece itself (the Rotate tool turns by
dragging anywhere on the piece).

## Shapes, holes and the Poncho (`godot/shapes.gd`, `godot/table.gd`)

**Add shape** (table bar menu, or the Piece tab's buttons) opens a size dialog in cm: Rectangle (width, height),
Square, Circle or ellipse (width, height; a 24-point outline), Triangle (base, height), Trapezoid (top and bottom
width, height) and Poncho (side 140 cm, neck hole 24 cm). OK adds the piece centred on the middle of the table view,
as one action ("Add rectangle" and so on). Shapes come from `shapes.gd` and are in metres, counter-clockwise, with the
bounding box at the origin.

**Holes.** A piece has `holes: [outline, ...]` (see docs/GODOT_ASSETS.md). Three ways to add one:
* the piece's **Add hole...** dialog: a circle (diameter) or a rectangle (width, height) at a centre typed in cm from
  the piece's bottom-left corner;
* the **Hole** tool (table bar; the shape list next to it picks circle or rectangle): press inside a piece and drag.
  A circle is centred on the press point with its edge at the pointer; a rectangle has the press point and the pointer
  as opposite corners. Esc cancels;
* nothing else creates holes. Each hole has a Delete button in the Piece tab.

A hole is refused, with a message in the status line, when it does not lie inside the piece or touches another hole.
A click inside a hole does not pick the piece. The table draws holes dark with an outline. The simulator cuts the
cloth out of each hole (`cloth_core` `meshPolygon` with holes, `FailoringSim.add_polygon_panel_with_holes`), and its
edges are free: holes take no seams. Mirror and symmetric twins mirror holes with the piece.

**Poncho** (library, Plan dropdown): one 140 cm square, 24 cm neck hole at its centre, no seams, cotton poplin. Its
wrap height is 0.80 m, so the neck hole sits at neck height: on the average mannequin the neck ring settles at 1.46 m
(the neck base is at 1.43 m, the shoulders at 1.39 m), the hem at 0.71 m, and the poncho stays off the arms. At 1.00 m
the neck ring slid down the arms, which is why the height is 0.80 m. `godot/tests/poncho_check.tscn` checks these.

**Known limitation, not fixed:** the upper half of the square does not fall forward and back over the shoulders. The
wrap places the whole square as one band around the body, so the upper half of the square stays above the neck and
lies over the head like a hood (`FL_AUTO=average,poncho,60` under a virtual display shows it). The neck hole is at the
neck and nothing falls off, but the look is a hood, not a poncho over the shoulders. A real poncho needs a front and
a back panel (or a cape wrap mode) in the wrap model; that is a wrap change, not done here.

Tests: `godot/tests/shapes_check.tscn` (the generators, the holes rule, placement, mirroring, the mesher through the
simulator), `godot/tests/poncho_check.tscn` (drape on the average mannequin), `godot/tests/cut_tools_check.tscn` (the
editor's Add shape, Add hole, Delete hole and the Hole tool, each one undoable). The C++ mesher test is in
`cloth_core/tests/test_basics.cpp` (a 14 cm square with a 24-point hole: area, boundary, no cloth inside the hole).

## Actions, history and selection (`godot/history.gd`, `godot/selection.gd`)

Every pattern edit is a named action. The edit calls `ed.push_undo("Move corner")` before it
changes the pattern, and the `changed("structure")` (or `"table-done"` after a piece move) that
ends it records the action: `{name, before, after}`, both as pattern snapshots. An edit that
leaves the pattern as it was is dropped. A drag records once, on release; a slider drag records
once on release, a keyboard step records once. Body, fabric and outfit changes are not actions.

Each garment has its own history (`history.gd`, kept in its outfit entry), so a rebuild, a body
change or a debounced regen never clears it; a garment switch shows that garment's line; a new
plan, a re-draft or opening a file starts a new line. Undo and redo move a cursor along the line
(state 0 is the start, state s the snapshot after action s). Ctrl+Z undoes, Ctrl+Shift+Z or Ctrl+Y
redoes, and the **History** tab (right) lists the start and every action by name: a click jumps
to that state, and the rows after the cursor are the undone ones. An edit made after an undo drops
the undone actions. The history keeps the last 200 actions.

`selection.gd` is the one model for what is picked and hovered: pieces, edges (`[piece, edge]`),
corners (`[piece, corner]`) and seams (seam index), as sets kept in pick order, plus one hover item.
The last piece picked is the primary piece (`ed.sel`, the Piece and Seams tabs), and its picked
corner is `ed.sel_v`. Both are views of the selection, so existing code keeps working. The table
and the panels read it, and it emits `changed` and `hover_changed`; the 3D view can connect to the
same signals. `prune` drops picks that an edit removed, after every structure change and undo.
The history and the selection are not stored in the saved pattern file.
Deviations: no grain hatch (arrow only), no per-edge thick sew highlight on the table
(the sew edge still highlights in the edge labels row).

Numeric entry (Piece tab, Enter applies, one undo step each): the piece's width and height
(cm, scaled about its left and bottom edges); each edge's length in cm (the corner after the
edge moves along it); a selected corner's x and y in cm from the piece's origin. Any change
that leaves an edge under 8 mm is refused with a message in the status line.

Placement: Front, Back, Left side and Right side set Around to 0, 180, 90 and 270 degrees.
Duplicate and Mirror offer a flip to the other side (Around +180) when the new placement
covers the old one.

Sewing: the first edge and the candidate edge are highlighted while the Sew tool is active.
The hint line names the next click and, once an edge is picked, shows both lengths. A pair
that differs by more than 5 % or 1 cm is flagged (the drape eases the difference). Seams
tab rows list both pieces and their lengths.

Copy and paste: Ctrl+C copies the selected piece, Ctrl+V pastes it to the right of the table.
Mirror twin: "Make a mirror twin" adds the mirror image of a piece beside it, linked both ways.
With Symmetric edits on, a change to either piece (corners, edges, size, grain, placement) is
copied to the other. The twin needs the same number of corners; a twin that is out of step is
left alone and the Piece tab says so. Seam allowance is not implemented.

## Timeline

`T` in [0, 4]: cut, move, sew, drape; scrub slider, stage and ±0.1 step buttons, speed
0.5/1/2, +60 frames, restart. Stages 0-2 pose the pieces from the table mat onto the
body (port of `V.pose`, needs `flat_point`); stage 3 shows the wrapped start pose;
stage 4 scrubs stored sim frames (display + cloth + body + stats per frame). Stitch
match lines and seam edges draw with stage reveal. Sim frames after the target stop;
+60 extends. Frame store is bounded by the target (a few hundred frames).

Perf notes: the bake+sim step runs on the main thread, at most one per physics tick and
per rendered frame. A step costs 0.1-0.25 s, so the sim trails the playhead by that much
per frame, but playback time keeps running and the UI hitches once per step instead of
four times. Display rebuilds only when the stage or sim frame index changes; the cloth
ArrayMesh, colours, display indices and standing-body Vector3s are cached per build.
Dragging a piece or vertex does not rebuild the tabs (they refresh on release).
A worker-thread sim is the next step if the stepping rate is still too low.

## Body tab

Preset buttons plus the browser's sliders: 5 primary measurements, 6 secondary with
auto (showing the fitted values), muscle/cup/belly, fit note with residuals and regen
time. Bodies regenerate live (`godot/body_gen.gd`, ~0.5 s) and the sim rebuilds.
Drafted plans re-draft on the live body; FreeSewing plans use the nearest preset's
geometry on the live sections (outlines are per-preset, placement is exact).

Look (2026-10-15): Skin (seven drawn tones, plus any photographic MakeHuman skin in `godot/assets/skin/photo/`, fetched
with `tools/fetch_makehuman_skins.sh`), Hair (none, or a short crop in five colours) and Eyes (five iris colours). Look
only: no rebuild, nothing reaches the solver. Kept in `user://settings.cfg` [look]. How the skin, eyes and studio are
made: `docs/LOOK_NOTES.md`.

## Assets (`godot/assets/<preset>/`, `godot/assets/body_data.json`)

Written by exporters; never hand-edit, re-run instead:

* `node tools/export_godot_assets.js [preset ...]` (existing): `body.obj`, `skeleton.json`,
  `walk.bin` (24 baked stride-1 gait frames).
* `node tools/export_godot_plans.js [--out godot/assets] [preset ...]` (new): `plan_*.json`
  (what `FL.fsimport.plan` produces: pieces, seams, info - same call as `tools/fs_plan.js`),
  `sections.json` (hulls/legs/arms for the wrap port), `wrap_ref.json` (spot values from
  the real `FL.makeWrap`), `draft_ref.json` (draft outlines).
  Re-run after any `app/src/fsimport.js` change (the pencil-skirt `holdTop` fix lands in
  the plans this way) and after any `app/src/shared.js` wrap change (then re-run the
  wrap check below, which compares the port against the fresh values).
* `node tools/export_godot_body.js [--out godot/assets]` (new): `body_data.json` (decoded
  mannequin: base, tris, parts, skin, 60 targets, bones, joints, landmarks, masks) and
  `body_ref.json` spot values. Re-run if `tools/convert_makehuman.py` changes the asset.

The editor loads `body_data.json` once and generates/stands/poses bodies live; the
exported `body.obj`/`walk.bin` remain as reference data (the checks read them).

## Ports and checks

* `godot/wrap.gd`: `FL.makeWrapRows` + `FL.sectionCurve` + generic envelope + flatPoint.
  `godot/tests/wrap_check.tscn` compares against the JS values on all presets and
  garments (fit below 0.001 mm, all below 0.01 mm).
* `godot/body_gen.gd`: `app/src/body.js` (blend, measures, fit, sections) plus gait
  rotations/skinning/stand/arms. `godot/tests/body_check.tscn`: generate matches to
  0.000 mm, walk poses below 0.2 mm, all presets.
* `godot/draft.gd`: `app/src/draft.js` (measures, pants, tank, shirt).
  `godot/tests/draft_check.tscn`: every outline point below 0.001 mm, all presets.
* `godot/info.gd`: `app/src/info.js` (cutting list, seam groups, ease, sewing order;
  plus a hand-drafted branch for plans without info).
* `godot/table.gd`, timeline/stages, tabs, shoot, demo: ported behaviour, verified by
  runs and screenshots (no JS to diff against at runtime; stage poses and info text
  were checked against the browser's own expectations where they exist).

Precision note: the ports keep outlines as `[x, y]` float pairs and use
`PackedFloat64Array` tables throughout, building a `Vector3` only for returned
positions. Godot's `Vector2`/`Vector3` are 32-bit, and single precision flips the
scanline crossing tests for vertices lying exactly on the scanline. Reference rows
narrower than 1e-9 m are skipped in the wrap check: the wrap angle there is trig
garbage in any implementation.

## Files

* `godot/editor.tscn`, `godot/editor.gd`: scene, UI, state, sim loop, timeline, shoot, demo.
* `godot/table.gd`: cutting table + piece edits.
* `godot/plan_loader.gd`: sim build from plan + sections + body (worker-build mirror),
  plan normalize (counter-clockwise + seam remap, like `App.loadPattern`).
* `godot/info.gd`, `godot/wrap.gd`, `godot/body_gen.gd`, `godot/draft.gd`,
  `godot/builtin_plans.gd`: ports above.
* `godot/scripts/body_loader.gd`: `FailoringBody` mesh node (OBJ + walk sheet player,
  `show_pose`/`use_custom` for live bodies).
* `godot/library.gd`, `godot/library_panel.gd`, `godot/garment_preview.gd`: garment library
  (catalogue, saved garments, the Library tab and its 2D previews).
* `godot/outfit.gd`: merges the shown garments into one plan (ids, fabrics, layer clearance).
* `godot/tests/`: `api_smoke.tscn` (wrapper), `wrap_check.tscn`, `body_check.tscn`,
  `draft_check.tscn`, `outfit_check.tscn` (tee + chinos merged, stepped, and the editor's outfit
  actions: add, move, fabric, re-draft, lock, save/rename/delete, remove).

## Known issues (see also `docs/STATUS.md`)

* The tracked `godot/bin/libfailoring.so` predates the current `FailoringSim` API (`add_seam`
  returns void there), so the editor does not compile against it: rebuild the extension with
  `tools/build_gdext.sh` first (details in `docs/NATIVE_NOTES.md`). `godot/walk_demo.gd` also fails to parse under Godot 4.3
  (an untyped `var idx` at line 136); it is not used by the editor.
* Outfit: the merged sim is checked headless (`tests/outfit_check.tscn`) and the layout was
  looked at in a virtual display. Dragging, the 3D view and the cards' double-click were
  not exercised by a script.
* The pencil skirt twists on tall/curvy/athletic and the chinos waistband comes out
  1.2 m long: upstream pattern issues, visible in the editor as-is. The A-line slides
  down like in the handoff notes.
* The knit tee settles about 2 mm inside the SDF surface (browser and native agree);
  the display margin covers it visually.
* Rebuilds block the UI for seconds on large garments (wrap curves evaluate in
  GDScript; the browser hides this in a worker thread). Slider drags debounce 250 ms.
* The photo shoot captures raw viewport frames (no bokeh/vignette/grain/runway
  compositing, no caption bar).
* First editor import writes `.godot/` and `*.import` files (untracked); a fresh
  checkout needs one editor run before headless scenes resolve new script classes.

## Verification (2026-10-12/13, macOS arm64, Godot 4.3)

Headless `editor.tscn` for every preset and garment: exit 0 throughout. Wrap/body/draft
checks green on all presets. Windowed runs for table, stages, tabs, shoot, demo.
Frame-60 stats (bake+step, KE, gap, clearance) and shots per combo:

| preset / garment | verts | bake ms | step ms | KE (J) | gap mm | clear mm |
|---|---|---|---|---|---|---|
| petite / tee | 563 | 74 | 55 | 0.0083 | 1.7 | -1.6 |
| petite / skirt | 597 | 74 | 77 | 0.0134 | 59.4 | 1.8 |
| petite / chinos | 672 | 72 | 64 | 0.0109 | 5.4 | 1.6 |
| average / tee | 635 | 80 | 74 | 0.0076 | 0.7 | -2.7 |
| average / skirt | 349 | 87 | 19 | 0.0113 | 40.6 | 1.8 |
| average / chinos | 773 | 80 | 80 | 0.0117 | 5.9 | 1.6 |
| tall / tee | 398 | 88 | 28 | 0.0032 | 0.2 | 1.8 |
| tall / skirt | 754 | 88 | 112 | 0.0288 | 189.6 | 1.8 |
| tall / chinos | 839 | 87 | 88 | 0.0260 | 10.6 | 1.6 |
| curvy / tee | 699 | 91 | 110 | 0.0134 | 0.4 | 1.5 |
| curvy / skirt | 749 | 85 | 104 | 0.0176 | 183.0 | 1.8 |
| curvy / chinos | 465 | 85 | 27 | 0.0331 | 0.1 | 1.6 |
| athletic / tee | 669 | 85 | 87 | 0.0122 | 1.2 | -2.1 |
| athletic / skirt | 710 | 84 | 107 | 0.5405 | 62.6 | 1.8 |
| athletic / chinos | 815 | 87 | 96 | 0.0085 | 11.8 | 1.6 |
| plus / tee | 774 | 98 | 142 | 0.0098 | 0.2 | 1.8 |
| plus / skirt | 877 | 93 | 121 | 0.0788 | 96.0 | -3.4 |
| plus / chinos | 910 | 90 | 123 | 0.0195 | 1.6 | 1.6 |

Skirt gaps on tall/curvy (183–190 mm, blown open) and the high chinos line-search
counts are the known upstream pattern issues, not Godot bugs; the tee's −2 mm
clearance matches the browser exactly (tight knit, display margin covers it).
