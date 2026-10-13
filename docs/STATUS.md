# Status, 2026-10-13 (main)

One page on where the project stands. `docs/WIP_HANDOFF.md` is an older checkpoint note (2026-10-12 afternoon); its
solver "blocker" is resolved: the core default is `tensionStiffness = false` (ctest unchanged) and the app turns it on.

## What exists

* **Browser app**: `failoring.html`, one offline file (2.72 MB). `node app/build.js` rebuilds it; the rebuild is
  byte-identical to the committed file. Needs only node.
* **Body**: MakeHuman CC0 parametric mannequin (sliders, presets), signed-distance-grid collider, subdivided display
  mesh, walking gait. Numbers in `docs/NUMBERS.md`, design in `docs/BODY_DESIGN.md`.
* **Garments**: the built-in patterns plus three real FreeSewing patterns drafted offline per preset and fitted to the
  body by `app/src/fsimport.js`: tee (teagan), pencil skirt (penelope), chinos (charlie, with waistband). Pattern tab in
  `app/src/info.js` (cutting list, seams, sewing order). One-click Demo in `app/src/demo.js`. How the trousers are fitted
  is in `docs/ARCHITECTURE.md` ("Garment sources").
* **Headless tools**: `tools/fs_plan.js` (raw FreeSewing parts to a plan), `tools/walk_sim.js` (the real worker in node:
  standing or walking sim; prints settle metrics and the worst clearance and seam gap of the run; `DUMP=` saves the
  positions), `tools/render_dump.js` with `tools/render_lib.js` (z-buffer renderer for those dumps, contact sheets by
  piece colour), `tools/freesewing/` (offline FreeSewing export).
* **Native**: `cloth_core` C++ library, GDExtension wrapper in `gdextension/`, Godot project in `godot/` (asset exporter
  `tools/export_godot_assets.js`, `godot/walk_demo.gd`). Run headless in Godot 4.3 on Linux x86_64 and on macOS arm64
  (`godot/bin/libfailoring.dylib`); see "Checked".
* **Licences**: AGPL-3.0 for the app; third-party material and checks in `THIRD_PARTY.md` (the AGPL text was compared
  word for word with the SPDX copy).

## Checked

* `node tools/smoke_browser.js`: 16 pass, 0 fail at commit 4dd9400 (headless Chromium, software renderer, 101 s). Covers
  the three FreeSewing garments, walking and the photo shoot.
* `node tools/test_demo.js failoring.html <dir>` at 4dd9400: Demo starts, the camera orbits, Next outfit moves on, a
  simulation error skips to the next garment, Stop returns to editing (the one console error is the injected test error).
  The chinos were also run through Demo on the average and the plus body (133 frames walking each, no console errors).
* Chinos, 130-frame walking sims at draft mesh quality on all six presets (2026-10-13): trousers stay on and follow the
  stride, the band stays at the waist (on the plus preset the trousers settle under the belly, as real ones do); worst
  clearance -1.2 to -1.4 mm (plus -4.1 mm in frame 1, 1.6 mm at the end), seam gap at the end 5.0 to 7.4 mm (plus 11.6).
  Standing: seam gap 4.7 to 7.5 mm, band top edge within 2.2 cm of the natural waist on every preset.
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
  `godot/assets/average/` were regenerated for the new chinos but not run in Godot.
* Timing: all ms/frame figures were measured on a loaded 2-core host. The checkpoint figures in `WIP_HANDOFF.md`
  (legacy pencil about 100-106 ms/frame, A-line about 176; new about 90-110 and 180-200) have not been re-measured.
* Demo mode with Next body, and with the tee and the skirt (only the chinos were run through it).

## Open problems

1. Pencil skirt: the sideways twist is fixed (the waistband and skirt top are held at their edge girth, `holdTop` in
   `app/src/fsimport.js`; all six presets stable standing). The earlier "curvy twists" was not reproduced. The skirt is
   now drafted with the seat and waist measured on the mannequin.
2. Pencil skirt hem is baggy; at draft mesh quality the FreeSewing skirt has a 94.6 mm seam gap.
3. Chinos (fixed 2026-10-13): the back leg was mirrored (`origSide` `'L'` instead of `'R'`), so its seams never met the
   front's (gaps 44 to 107 mm); the waistband now follows the trousers' top edge and is sewn to every top-edge chain with
   equal lengths; the draft uses the measured seat and cross seam and `waistHeight` 1. Left: the plus preset does not
   settle (about 2 mm per frame, cloth about 3 mm into the body; the skirt does the same there, so it is the body, not the
   garment: probably cloth squeezed between the thighs and under the belly against a rigid collider); petite and
   athletic move about 1 mm per frame; a small V dip of the band at the centre back on athletic and curvy; a small band
   tab at the side seam on tall; thin cracks at the top of the centre back seam on plus; no pocket bag (the slant is
   sewn shut); dart notches stay open.
4. Walking, chinos and skirt: stay on and move plausibly on all six presets (130 frames; the chinos were redone after the
   back-leg fix, see Checked). The tee is the weak one. Fixed: the tall tee had one shoulder seam only half sewn (a mate
   chain of near-equal length is now sewn end to end) and now stays on standing and walking. Still failing standing and
   walking: curvy and athletic tees slip off one shoulder, and on petite/average a sleeve rides up. Seam lists are
   complete there, so it is the neckline/shoulder rest in the start pose (lowering the shoulder line by 2 to 4 cm helped
   tall but made athletic worse).
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

## Maintenance

A scheduled task ("Failoring browser build upkeep", daily 06:47 Europe/Berlin) rebuilds `failoring.html`, runs the smoke
test and pushes any fix to a branch `upkeep/YYYY-MM-DD`, never to main. Merge such a branch yourself after a look.
