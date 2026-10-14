# Status, 2026-10-13 (main, checkpoint)

One page on where the project stands. `docs/WIP_HANDOFF.md` is an older checkpoint note (2026-10-12 afternoon); its
solver "blocker" is resolved: the core default is `tensionStiffness = false` (ctest unchanged) and the app turns it on.
The tee, the weak garment, has its own page with the root cause and next steps: `docs/TEE_NOTES.md`.

## What exists

* **Browser app**: `failoring.html`, one offline file (2.73 MB). `node app/build.js` rebuilds it; the rebuild is
  byte-identical to the committed file. Needs only node.
* **Body**: MakeHuman CC0 parametric mannequin (sliders, presets), signed-distance-grid collider, subdivided display
  mesh, walking gait (arms hang clear of the torso: `armAbduct`, see `docs/ARCHITECTURE.md`). Numbers in `docs/NUMBERS.md`,
  design in `docs/BODY_DESIGN.md`.
* **Garments**: the built-in patterns plus three real FreeSewing patterns drafted offline per preset and fitted to the
  body by `app/src/fsimport.js`: tee (teagan), pencil skirt (penelope), chinos (charlie, with waistband). Pattern tab in
  `app/src/info.js` (cutting list, seams, sewing order). One-click Demo in `app/src/demo.js`. How the trousers are fitted
  is in `docs/ARCHITECTURE.md` ("Garment sources").
* **Headless tools**: `tools/fs_plan.js` (raw FreeSewing parts to a plan), `tools/walk_sim.js` (the real worker in node:
  standing or walking sim; prints settle metrics and the worst clearance and seam gap of the run; `DUMP=` saves the
  positions; `PIECESTAT=<piece index>` prints a piece's height, edge stretch and, for a band, its top edge against the posed
  waist landmark), `tools/render_dump.js` with `tools/render_lib.js` (z-buffer renderer for those dumps, contact sheets by
  piece colour, `POS0=1` for the start pose), `tools/freesewing/` (offline FreeSewing export), `tools/dev/` (about 60
  diagnostic scripts from the fit work: seam gaps by piece pair, seam edge positions, shoulder and armhole heights, band
  height, cross seam over the gait cycle, six-preset batch runners `zz.sh` and `ps6.sh`; paths assume the repo at
  `/home/claude/failoring`, scratch in `$SK`; `tools/dev/attic/` holds superseded copies of the importer).
* **Native**: `cloth_core` C++ library, GDExtension wrapper in `gdextension/`, Godot project in `godot/` (asset exporter
  `tools/export_godot_assets.js`, `godot/walk_demo.gd`). Run headless in Godot 4.3 on Linux x86_64 and on macOS arm64
  (`godot/bin/libfailoring.dylib`); see "Checked".
* **Licences**: AGPL-3.0 for the app; third-party material and checks in `THIRD_PARTY.md` (the AGPL text was compared
  word for word with the SPDX copy).

## Checked

* `node tools/smoke_browser.js` at this checkpoint: 16 pass, 0 fail (headless Chromium, software renderer, 114 s). Covers
  the three FreeSewing garments, walking and the photo shoot.
* `node tools/test_demo.js failoring.html <dir>` at this checkpoint: Demo starts, the camera orbits, Next outfit moves on, a
  simulation error skips to the next garment, Stop returns to editing (the one console error is the injected test error).
  The chinos were also run through Demo on the average and the plus body earlier (133 frames walking each, no console errors).
* Chinos, 130-frame walking sims at draft mesh quality on all six presets (cotton poplin, plans built from the committed
  raw files, checkpoint): the trousers stay on and follow the stride. The band's top edge stays between 3.7 cm below and 1.0 cm
  above the posed waist landmark (petite and tall within 1 cm; the others 0.7 to 3.7 cm low); band edge stretch at most 1.026
  (plus 1.043); worst clearance 0.6 to 1.5 mm (plus -2.1 mm in frame 1); seam gap at the end 5.1 to 8.6 mm (frame 1: 10.5 to
  14.2 mm, plus 21.8 mm). Standing: 70 frames from the plan's start, mean vertex step over the last 10 frames: petite, average, tall, curvy and athletic settle (0.02 to 0.06 mm per frame), seam gap 4.9 to 7.8 mm, clearance 1.6 mm; plus does not settle (1 to 4.5 mm per frame, clearance -0.6 mm, seam gap 7.6 mm).
* Pencil skirt with the matched band, the same runs: the band's top edge is 1.4 cm below to 4.6 cm above the posed waist
  landmark (the band is 3 cm finished, so its lower edge stays at the waist), band stretch at most 1.017, clearance 1.6 mm,
  seam gap at the end 1.4 to 4.8 mm (frame 1: 7.3 to 12.0 mm). Standing: all six presets settle (0.1 to 0.8 mm per frame and falling over the last 10 of 70 frames), seam gap 0.7 to 4.8 mm, clearance 1.6 mm.
* Tee: not changed in this checkpoint (plans byte-identical to commit `6e2b8ff` on all six presets); see Open problems 4.
* ctest 5/5 in Release (`docs/TEST_STATUS.md`), run on an agent branch before the merge, not re-run on merged main.
* Godot 4.3.stable, Linux x86_64, headless: `godot/tests/api_smoke.tscn` passes, and `godot/walk_demo.tscn` ran 30
  frames (no NaN, no line-search failures, no penetration, seam gap at most 3.1 mm, about 0.5 s per frame).
* Godot 4.3 on macOS arm64, headless (reported from the Mac session, logs not seen here): `libfailoring.dylib` (1.1 MB,
  Release, godot-cpp `godot-4.3-stable`) built from `gdextension/`; `api_smoke.tscn` 24 of 24 pass; `walk_demo.tscn`
  35 frames, exit 0, no errors; body bake 25 ms; about 0.26 s wall (0.24 s CPU) per frame, clearance 1.65 mm, seam gap
  3.14 mm, no line-search failures (the Mac session's corrected figures: an earlier 1.5 to 2 s per frame came from dividing
  by `--quit-after N`, which counts main-loop iterations of about 8 physics ticks each). The pushed file is a genuine
  arm64 Mach-O library.

## Not verified

* Real GPU, Safari and Firefox (only headless Chromium on a software renderer).
* Godot with a real window: both Godot runs were headless, so the cloth was never looked at. The Godot plans in
  `godot/assets/average/` were regenerated at this checkpoint (chinos 180 seams, skirt 111) but not run in Godot.
* Timing: all ms/frame figures were measured on a loaded 2-core host. The checkpoint figures in `WIP_HANDOFF.md`
  (legacy pencil about 100-106 ms/frame, A-line about 176; new about 90-110 and 180-200) have not been re-measured.
* Demo mode with Next body, and with the tee and the skirt (only the chinos were run through it).

## Open problems

1. Pencil skirt: the sideways twist is fixed (the waistband and skirt top are held at their edge girth, `holdTop` in
   `app/src/fsimport.js`; all six presets stable standing and walking). The band is now sewn along the whole top edge
   (checkpoint). The skirt is drafted with the seat and waist measured on the mannequin.
2. Pencil skirt hem is baggy.
3. Chinos (checkpoint): drafted with the rise eased by 4% (the taut crotch path is 3 to 7% longer while walking), the hem 30 mm
   above the floor (a hem under a foot dragged the trousers off the waist, tall and plus), the band's button tab trimmed
   (`waistbandFly`), and the band sewn to every top-edge chain of at least 1 cm (slivers under 4% of the band keep their share
   but are not sewn). Left: the plus preset does not settle standing (about 1 to 4 mm per frame, the cloth about 0.6 mm into the
   body; the skirt, which has no legs, now settles there, so it is probably cloth squeezed between the thighs and under the belly
   against a rigid collider); a small V dip of the
   band at the centre back on athletic and curvy; a small band tab at the side seam on tall; thin cracks at the top of the
   centre back seam on plus; no pocket bag (the slant is sewn shut); dart notches stay open.
4. Tee, the weak garment (`docs/TEE_NOTES.md` has the root cause, measurements and next steps): curvy and athletic tees slip off
   one shoulder, on petite and average a sleeve rides up (standing and walking); tall is fine. Cause: the start pose. The wrap
   hull (`B.sections`) leaves out the shoulders, so the front and back pieces end 5 cm medial of the shoulder tip and the
   deltoid is bare, and the sleeve tube's cap starts about 10 cm lateral and 9 cm below the armhole top; Teagan's default neckline
   is also wide. A sleeve cap wrap and a per-body sleeve height calibration are in the code but off (`opt.sleeveCap`, `SLEEVECAP=1`
   in `tools/fs_plan.js`): they did not shrink the cap to armhole gap (77 to 85 mm mean at frame 0).
5. Godot wrapper: 13 of the 14 review findings in `docs/NATIVE_NOTES.md` are fixed (2026-10-12); only 13 (the
   synchronous body bake) remains.
6. Native solver was not faster than wasm in the one comparison; threading is off; a `-march=native` build gives a
   different solver checksum (undiagnosed). The walk demo takes about 0.26 s per frame on the Mac (0.5 s in the Linux
   container), with the same clearance and seam gap on both. The demo garment and quality differ from the browser's, so
   this is not a like-for-like comparison with it.
7. Small gaps: the Demo bar overlaps the lower body; `plan.info` is lost on save/open; the trouser pieces overlap on the
   cutting table; at Draft mesh quality (6 cm triangles) patches of skin show through the cloth at the knees and shins
   while walking (a 6 cm triangle cuts a 3.5 cm radius shin by about 13 mm, the display mesh lifts at most 4 mm). At
   Standard quality (4 cm, the app default) the same walk, looked at close up in the browser, shows only a few specks.
   13 agent worktrees (`git worktree list`) are left to remove; `_leftovers-safe-to-delete/` on the user's side can go.

## Godot editor (merged 2026-10-13 night)

`godot/editor` (the local agent's port) plus six rounds of fixes by sub-agents are in main: camera orbit/pan/zoom, UI scaling,
garment Library and multi-garment Outfit, edit robustness (edited flag, undo, confirmations), numeric entry and placement
buttons, garment health warnings, per-fabric shading and shadows, native speedups (body bake overlapped with the solver,
normals in C++, Preview quality default), cutting-table v2 (action history and History tab, shared selection model, shapes,
holes with mesher support, Poncho, Split/Rotate/Measure/curves/scale/align, partial seams) and a Showcase system (Catwalk,
Pick up a box, Sit on a chair as baked bone sequences with box/chair colliders, pre-render on demand with a cache).
Product spec for what is wanted next: `docs/EDITOR_V2_SPEC.md`; reports: `docs/DOGFOOD_REPORT.md`, `docs/PERF_NOTES.md`,
`docs/SCENARIOS.md`, `docs/VANISH_NOTES.md`.
* Checked: all godot/tests scenes pass headless on Godot 4.3 (Linux, library rebuilt locally), cloth_core test_basics and
  test_garment pass, the browser build is unchanged.
* Not done: right-click context menu and richer hover/selection feedback on the table; 3D to 2D two-way picking and highlight;
  an always-available Sew button that pulls the arranged garment onto the mannequin and feeds the Showcase render; the
  tutorial overlay and help hovers. Not verified: anything in a real window (all runs headless, no GPU), the Mac dylib
  (`godot/bin/libfailoring.so` and the dylib are stale: run `tools/build_gdext.sh`), real frame rates (the Mac figure is an
  estimate), Forward+ effects (SSAO, TAA).
* The editor still shows the tee failing (docs/TEE_NOTES.md), and the editor's built-in pencil skirt is too small for the plus body.

## Session 2026-10-14 (opus/session-1, merged to main)

* Solver: automatic Hessian model (`hessianAuto`, default) removes the wedge jitter without making stiff cloth limp; static
  equilibrium solve merged (fast preview, see ARCHITECTURE); optional Coulomb friction inside Newton (`bodyFrictionMode 2`).
* Godot editor: all editor branches confirmed in main (line-level check); mac/perf merged; angle-weighted cloth normals
  restored; the editor's assets regenerated for all six presets (they were stale for five: old garment plans, gait, and
  the curvy/plus bodies); wrap.gd gained the browser's waistband tilt and lifted trouser rows; body_gen.gd gained the arm
  clearance. Godot tests 23/23 headless (main had 21/23). Details: `docs/handoff/OPUS_SESSION_1.md`.
* Rebuild the GDExtension (`tools/build_gdext.sh`) before opening the editor: the committed libraries are older.

## Maintenance

A scheduled task ("Failoring browser build upkeep", daily 06:47 Europe/Berlin) rebuilds `failoring.html`, runs the smoke
test and pushes any fix to a branch `upkeep/YYYY-MM-DD`, never to main. Merge such a branch yourself after a look.
