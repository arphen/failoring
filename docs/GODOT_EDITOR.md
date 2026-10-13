# Godot editor

The browser app with the native solver: cut pattern pieces on a 2D table, mark seams,
pick a body (preset or sliders) and a plan, watch the pieces fly to the body, sewn and
drape on the timeline, read the pattern info, take studio photos, run a demo. Presets
and sliders both regenerate the body live; plans resolve per state.

## Run

Needs `godot/bin/libfailoring.dylib` on macOS (`libfailoring.so` on Linux); build it with
the commands in `docs/NATIVE_NOTES.md`. Open `godot/` in Godot 4.3 and run `editor.tscn`
(it is the project's main scene). First launch imports assets (`.godot/`, `*.import`;
untracked, see `godot/.gitignore`).

* Left tabs: Library (garment cards with a 2D pattern preview; double-click or Add puts a
  garment on the mannequin; save the garment on the table, rename, delete) and Controls
  (plan selector, re-draft, mesh quality, motion, open/save pattern JSON, demo, live sim stats).
  See "Garment library and outfit" below.
* Cutting table: Select/Sew tools, Fit, snap 5 mm; drag pieces and corners,
  double-click an edge to add a corner, Del removes, wheel zooms, V/S/F/Esc work.
* 3D: orbit drag, wheel zoom; timeline scrub through cut, move, sew, drape.
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

Re-draft: drafted garments follow the live body (as before). FreeSewing and saved garments keep
their outline until Re-draft is pressed, which drafts them for the current preset. Locking a garment
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
sew-edge highlights. Undo/redo (60), snap 5 mm, keyboard as in the browser.
Deviations: no grain hatch (arrow only), no per-edge thick sew highlight on the table
(the sew edge still highlights in the edge labels row).

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
  `docs/NATIVE_NOTES.md` first. `godot/walk_demo.gd` also fails to parse under Godot 4.3
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
