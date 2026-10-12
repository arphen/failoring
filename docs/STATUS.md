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
  `tools/export_godot_assets.js`, `godot/walk_demo.gd`). Nothing has been run inside Godot.
* **Licences**: AGPL-3.0 for the app; third-party material and checks in `THIRD_PARTY.md` (the AGPL text was compared
  word for word with the SPDX copy).

## Checked

* `node tools/smoke_browser.js`: 16 pass, 0 fail at commit 35208d2 (headless Chromium, software renderer, 88 s). Covers
  the three FreeSewing garments, walking and the photo shoot.
* ctest 5/5 in Release (`docs/TEST_STATUS.md`), run on an agent branch before the merge, not re-run on merged main.

## Not verified

* Real GPU, Safari and Firefox (only headless Chromium on a software renderer).
* Anything in Godot: no scene load, no `FailoringSim` call, no `walk_demo.gd` run.
* Timing: all ms/frame figures were measured on a loaded 2-core host. The checkpoint figures in `WIP_HANDOFF.md`
  (legacy pencil about 100-106 ms/frame, A-line about 176; new about 90-110 and 180-200) have not been re-measured.
* Demo mode with the FreeSewing garments (Next outfit, Next body, stall watchdog).

## Open problems

1. Pencil skirt twists sideways on tall, curvy and athletic bodies (it holds on average, petite, plus). Next suspects:
   the 2.9 cm vent overlap counted twice in the target girth `w.P`, or the waistband.
2. Pencil skirt hem is baggy; at draft mesh quality the FreeSewing skirt has a 94.6 mm seam gap.
3. Chinos have no waistband (the band piece comes out about 1.2 m long, cause unknown).
4. Walking was checked for the tee on the average body only. Skirt and chinos were never walked; the tee was not walked
   on the other five presets.
5. Godot wrapper: 14 review findings in `docs/NATIVE_NOTES.md` (3 high, 3 medium, 5 minor or unverified, 3 missing
   features). None fixed.
6. Native solver was not faster than wasm in the one comparison; threading is off; a `-march=native` build gives a
   different solver checksum (undiagnosed).
7. Small gaps: `tools/walk_sim.js` prints NaN/0 averages; the Demo bar overlaps the lower body; `plan.info` is lost on
   save/open; trouser pieces overlap on the cutting table.

## Maintenance

A scheduled task ("Failoring browser build upkeep", daily 06:47 Europe/Berlin) rebuilds `failoring.html`, runs the smoke
test and pushes any fix to a branch `upkeep/YYYY-MM-DD`, never to main. Merge such a branch yourself after a look.
