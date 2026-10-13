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

* Left: plan selector (built-in, drafted, FreeSewing), re-draft, mesh quality, motion,
  open/save pattern JSON, demo, live sim stats.
* Cutting table: Select/Sew tools, Fit, snap 5 mm; drag pieces and corners,
  double-click an edge to add a corner, Del removes, wheel zooms, V/S/F/Esc work.
* 3D: orbit drag, wheel zoom; timeline scrub through cut, move, sew, drape.
* Right tabs: Piece, Seams, Body, Fabric, Pattern, Shoot.
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
* `godot/tests/`: `api_smoke.tscn` (wrapper), `wrap_check.tscn`, `body_check.tscn`,
  `draft_check.tscn`.

## Known issues (see also `docs/STATUS.md`)

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
| (filled after the final matrix) | | | | | | |
