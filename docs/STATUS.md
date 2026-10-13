# Status, 2026-10-12 (main)

One page on where the project stands. `docs/WIP_HANDOFF.md` is an older checkpoint note (2026-10-12 afternoon); its
solver "blocker" is resolved: the core default is `tensionStiffness = false` (ctest unchanged) and the app turns it on.

## What exists

* **Browser app**: `failoring.html`, one offline file (2.45 MB). `node app/build.js` rebuilds it; the rebuild is
  byte-identical to the committed file. Needs only node.
* **Body**: MakeHuman CC0 parametric mannequin (sliders, presets), signed-distance-grid collider, subdivided display
  mesh, walking gait. Numbers in `docs/NUMBERS.md`, design in `docs/BODY_DESIGN.md`.
* **Garments**: the built-in patterns plus three real FreeSewing patterns drafted offline per preset and fitted to the
  body by `app/src/fsimport.js`: tee (teagan), pencil skirt (penelope), chinos (charlie). Pattern tab in
  `app/src/info.js` (cutting list, seams, sewing order). One-click Demo in `app/src/demo.js`.
* **Native**: `cloth_core` C++ library, GDExtension wrapper in `gdextension/`, Godot project in `godot/` (asset exporter
  `tools/export_godot_assets.js`, `godot/walk_demo.gd`). Run headless in Godot 4.3 on Linux x86_64 and on macOS arm64
  (`godot/bin/libfailoring.dylib`); see "Checked".
* **Licences**: AGPL-3.0 for the app; third-party material and checks in `THIRD_PARTY.md` (the AGPL text was compared
  word for word with the SPDX copy).

## Checked

* `node tools/smoke_browser.js`: 16 pass, 0 fail at commit 35208d2 (headless Chromium, software renderer, 88 s). Covers
  the three FreeSewing garments, walking and the photo shoot.
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
* Godot with a real window: both Godot runs were headless, so the cloth was never looked at.
* Timing: all ms/frame figures were measured on a loaded 2-core host. The checkpoint figures in `WIP_HANDOFF.md`
  (legacy pencil about 100-106 ms/frame, A-line about 176; new about 90-110 and 180-200) have not been re-measured.
* Demo mode with the FreeSewing garments (Next outfit, Next body, stall watchdog).

## Open problems

1. Pencil skirt: the sideways twist is fixed (the waistband and skirt top are held at their edge girth, `holdTop` in
   `app/src/fsimport.js`; all six presets stable standing). The earlier "curvy twists" was not reproduced.
2. Pencil skirt hem is baggy; at draft mesh quality the FreeSewing skirt has a 94.6 mm seam gap.
3. Chinos now have a waistband and sit at the natural waist on all six presets (standing): Charlie is drafted with
   `waistHeight` 1, the band is sewn to the front/back top edges, and leg pieces share their row extents above
   `wrap.vcap` so the slanted top edge no longer collapses. Still baggy at the hips/thighs, the slant pocket has no
   pocket bag, and the back-right top edge only partly seams to the band.
4. Walking, checked 2026-10-13 (130 frames, one frame per body viewed): skirt and chinos stay on and move plausibly on
   all six presets; the tee is the weak one. Fixed: the tall tee had one shoulder seam only half sewn (a mate chain of
   near-equal length is now sewn end to end) and now stays on standing and walking. Still failing standing and walking:
   curvy and athletic tees slip off one shoulder, and on petite/average a sleeve rides up. Seam lists are complete there,
   so it is the neckline/shoulder rest in the start pose (lowering the shoulder line by 2 to 4 cm helped tall but made
   athletic worse). Not yet: Demo mode with the FreeSewing garments.
5. Godot wrapper: 13 of the 14 review findings in `docs/NATIVE_NOTES.md` are fixed (2026-10-12); only 13 (the
   synchronous body bake) remains.
6. Native solver was not faster than wasm in the one comparison; threading is off; a `-march=native` build gives a
   different solver checksum (undiagnosed). The walk demo takes about 0.26 s per frame on the Mac (0.5 s in the Linux
   container), with the same clearance and seam gap on both. The demo garment and quality differ from the browser's, so
   this is not a like-for-like comparison with it.
7. Small gaps: `tools/walk_sim.js` prints NaN/0 averages; the Demo bar overlaps the lower body; `plan.info` is lost on
   save/open; trouser pieces overlap on the cutting table.

## Maintenance

A scheduled task ("Failoring browser build upkeep", daily 06:47 Europe/Berlin) rebuilds `failoring.html`, runs the smoke
test and pushes any fix to a branch `upkeep/YYYY-MM-DD`, never to main. Merge such a branch yourself after a look.
