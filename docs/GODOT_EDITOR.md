# Godot editor

The browser app's core loop with the native solver: pick a body preset, pick a garment,
watch it drape and walk, read the pattern info. Presets only (no body sliders yet, see below).

## Run

Needs `godot/bin/libfailoring.dylib` on macOS (`libfailoring.so` on Linux); build it with the
commands in `docs/NATIVE_NOTES.md`. Open `godot/` in Godot 4.3 and run `editor.tscn`
(it is the project's main scene).

* Left panel: body preset (petite, average, tall, curvy, athletic, plus), garment
  (Tee, Pencil skirt, Chinos), fabric (from `materials/fabrics.json`, defaults to the
  plan's fabric), Pause/Resume, Restart, Walking on/off, Speed 0.25-2.
* Right panel: pattern info from `plan.info`, like the browser Pattern tab (cutting list,
  seams with lengths and ease, suggested sewing order, measurements).
* Bottom of the left panel: live sim stats (verts, KE, seam gap, clearance, bake/step ms).

Environment (headless checks, shots, matrix runs):

* `FL_AUTO=preset,garment,frames` runs that many sim frames, saves
  `godot/docs-shots/<preset>_<garment>.png` and quits. Needs a real window for pixels;
  headless it still simulates and exits (the PNG is blank).
* `FL_STAND=1` stays standing (no walk), for drape-only checks.
* `FL_PRESET=name` selects the preset for `tests/wrap_check.tscn`.

Behaviour mirrors the browser worker (`app/src/worker.js`): Standard quality
(edge 0.04 m, 2 substeps, 8 Newton, 48 settle frames), seam stiffness 20000, 20 ramp
frames, tension stiffness on, floor plane at y = 0, gravity faded in over the ramp,
then the stride walk (64-frame cycle, 24-frame ease-in) looped until paused. Standing
mode stops after 96 frames. Rebuild on any UI change.

## Assets (`godot/assets/<preset>/`)

Written by two exporters; never hand-edit, re-run instead:

* `node tools/export_godot_assets.js [preset ...]` (existing): `body.obj` (standing mesh,
  the collider), `skeleton.json`, `walk.bin` (24 baked stride-1 gait frames as float16
  offsets from the standing mesh, `FLWALK01`), `plan_<garment>.json`.
* `node tools/export_godot_plans.js [--out godot/assets] [preset ...]` (new): `plan_*.json`
  (what `FL.fsimport.plan` produces: pieces, seams, info - same call as `tools/fs_plan.js`),
  `sections.json` (body section hulls/legs plus arm tubes for the wrap port),
  `wrap_ref.json` (spot values from the real `FL.makeWrap` for the port check).
  Re-run after any `app/src/fsimport.js` change (the pencil-skirt `holdTop` fix lands in
  the plans this way) and after any `app/src/shared.js` wrap change (then re-run the
  wrap check below, which compares the port against the fresh values).

Formats are also described in `docs/GODOT_ASSETS.md`.

## Wrap port (`godot/wrap.gd`)

`FL.makeWrapRows` + `FL.sectionCurve` ported to GDScript. Start positions are evaluated at
the solver's own mesh vertices, exactly like the worker. `godot/tests/wrap_check.tscn`
compares the port against the JS values (`FL_PRESET` selects the preset, default average):
all six presets pass below 0.01 mm (fit below 0.001, leg/arm a few microns of libm
difference). Reference rows narrower than 1e-9 m are skipped: the wrap angle there is
trig garbage in any implementation (identical formula, different `sin` reduction).

Precision note: the port keeps outlines as `[x, y]` float pairs and uses
`PackedFloat64Array` tables throughout, building a `Vector3` only for the returned
position. Godot's `Vector2`/`Vector3` are 32-bit, and single precision flips the
scanline crossing tests for vertices lying exactly on the scanline.

## Files

* `godot/editor.tscn`, `godot/editor.gd`: scene and UI/loop.
* `godot/plan_loader.gd`: builds the sim from plan + sections + body (the worker-build mirror).
* `godot/info.gd`: pattern info formatting (mirrors `app/src/info.js`).
* `godot/scripts/body_loader.gd`: `FailoringBody` mesh node (OBJ + walk sheet player);
  the editor drives poses itself via `show_pose()` so display and collider stay in sync.
* `godot/tests/`: `api_smoke.tscn` (wrapper checks), `wrap_check.tscn` (port check).

## Not ported (yet)

Body sliders. The browser regenerates the body in `body.js` (blend shapes + tape-measure
fit); the editor uses the six exported presets. A port to the extension means: FLBODY01
asset loading in C++ (raw DEFLATE plus quantized/delta-coded mesh, targets, skin,
joints, landmarks, masks - needs an inflate implementation), blend-shape application
with macro interpolation, the tape measures (plane slicing + hull perimeters), the
calibration sweeps, `generate()` (finite-difference Jacobian, cached; active-set Newton
fit; placement; sections), all verified against the JS (residuals <= 0.4 mm on presets,
regen 15-50 ms), plus slider UI. Estimate: several days, not hours. Presets only until
decided otherwise.

## Known issues (see also `docs/STATUS.md`)

* The pencil skirt twists on tall/curvy/athletic and the chinos waistband comes out
  1.2 m long: upstream pattern issues, visible in the editor as-is.
* The knit tee settles about 2 mm inside the SDF surface (browser and native agree);
  the display margin covers it visually.
* First editor import writes `.godot/` and `*.import` files (untracked, see
  `godot/.gitignore`); a fresh checkout needs one editor run before headless scenes
  resolve the global script classes.

## Verification (2026-10-12, macOS arm64, Godot 4.3)

Headless `editor.tscn` for every preset and garment (60 frames each): exit 0 throughout.
Windowed `FL_AUTO` shots below (800x450). Frame-60 stats (bake+step, KE, gap, clearance):

| preset / garment | verts | bake ms | step ms | KE (J) | gap mm | clear mm |
|---|---|---|---|---|---|---|
| (filled after the runs) | | | | | | |
