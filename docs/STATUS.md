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

## Editor layout and CAD table (2026-10-14 evening, opus/editor-layout + opus/cad-table)

* Layout (`godot/layout.gd`): the four panes are nested splits (drag the gaps), each can be hidden (pane bar, Ctrl+1..4);
  table and 3D view can be stacked (Ctrl+5) or swapped; Focus 3D; saved in settings.cfg [layout]. The table bar wraps:
  its single row set a minimum width that pushed the inspector and timeline off a 2000 px window. Slider tracks were 0 px
  tall (theme): now visible.
* Cutting table: construction points and lines (`godot/guides.gd`, saved with the pattern; centre of mass with holes
  subtracted, edge middles, corners, hole centres, free points; lines through two points or at an angle), object snap,
  Point and Line tools, right-click menus on every kind of thing, holes picked and dragged, Delete on picked guides, holes
  and seams. Splitting a sewn edge keeps the seam (cut in two at the new corner). Seam links on the table are drawn for
  the piece under the pointer by default (View menu), edge lengths only where they fit.
* 3D: Move panels mode in the 3D view (`godot/place_view.gd`, M): pick a panel, drag it, drag an axis arrow, or drag a
  ring to turn it; the move is stored per piece (`godot/place3d.gd`, `p.place`, saved with the pattern) and applied to
  the simulator's start positions. The Piece tab has the same as sliders (cm, degrees) and Reset.
* Checks: godot/tests/cad_check (36), place_check (25), and the full Godot suite headless. Screens rendered under Xvfb
  with Godot's OpenGL renderer (llvmpipe), not on a GPU.

## Contact, friction and a living mannequin (2026-10-14 night, opus/contact-friction)

Measured on walking tee and chinos (average body, preview quality, frame dumps analysed against the exact body mesh):
cloth vertices stay out of the body (deepest about 0.5-3 mm), so the skin seen through the cloth came from the display:
* coarse cloth triangles (tee 475 vertices, chinos 722 at preview): flat 5-7 cm triangles cut into knees, shins, chest
  and arms between their corners (up to 16-20 mm); at fine quality (2.8 cm) the same walk shows 5x less. Now the display
  mesh is split twice (16 display triangles per cloth triangle, `display_levels`, C++ setup_display) and every display
  vertex is pushed out of the body (iterated: the body's distance field is exact only within 6 mm of the skin);
* soft seams: the two sides of a seam sit 5-6 mm apart while walking and skin showed in the crack along every seam;
  now drawn closed (each sewn vertex on the point it is sewn to);
* the drawn body is 2.5 mm inside its collision surface (material grow), which hides the remaining sub-3 mm specks where
  cloth lies on skin (body edges crossing the cloth: ~400 -> ~140 per frame, the rest inside sleeves and legs).
Skin friction: the solver's default friction was effectively off (test_friction: a patch on a 15 degree slope slid
290 mm in 0.5 s, Coulomb says 0). The editor now uses friction inside the Newton solve (mode 2): cloth sliding over the
skin while walking 4.4 -> 1.1 mm/frame (chinos), 3.2 -> 1.6 (tee); the tee sags 18 -> 9 mm over the walk; standing
settles (tee 2.1 -> 0.3 mm/frame). The C++ default stays 0 (core tests); `FL_FRICTION=0` restores it in the editor.
Living mannequin (`godot/soft_tissue.gd`): breasts, seat, belly, thighs and upper arms are damped springs driven by
their own acceleration, weighted by the body's morph targets; a breath; stride and arm swing vary a little per cycle.
Body tab: Alive > Soft tissue (0 = the old rigid body). Frequencies and damping are estimates (see the file).

* Tee sleeves (2026-10-15, opus/tee-sleeve): garments with sleeves are dressed in an A-pose and the arms lowered before
  the walk; upper-arm coverage 84 -> 89 % standing over the six presets (`docs/TEE_NOTES.md`). Petite and average still
  crumple one sleeve while walking.

* Arrangement points and "Will it sew?" (2026-10-15, opus/arrange): CLO-style dots round the body to put pieces on
  (Arrange, A), a sewing check of the start pose per piece pair (Seams tab, coloured stitch lines), and the tee's
  shoulders and sleeve caps placed so its sleeves attach (upper-arm coverage standing 92 %, walking 90 %).

## Buried skin and a faster solver (2026-10-15, opus/vbd)

**Skin through the shoulders and under the sleeves.**

*Cause.* The body's distance field measured the distance to every body triangle, including skin buried inside another
part of the body. On the average body standing there are 236 such triangles: where the lowered arm folds into the
deltoid at 1.2-1.3 m, plus the feet and the head. Cloth inside the shoulder next to the buried skin read as on the
surface, so it rested there. At the shoulder tips it sat about 14 mm inside the visible skin while the field said
-1 mm.

*Fix.* `SdfGrid` now leaves buried triangles out of the distances (`SdfGridParams::dropBuried`, on). A triangle counts
as buried when two or more of four points 1 mm outside it have a positive winding number. Buried triangles still count
for the inside/outside sign. The bake goes from 233 to 247 ms (7 mm grid, 26756 triangles).

*Result.* Fine tee, average body, standing, frames 50-59, measured against the exact body mesh:

| | body vertices showing through the cloth | deeper than 2 mm | deeper than 5 mm |
|---|---|---|---|
| before | 2.37% | 1.17% | 0.125% |
| after | 1.21% | 0.32% | 0.020% |

Display points more than 2.5 mm inside the visible skin (the drawn body is shrunk by 2.5 mm) at frame 59: 4, at
most 2.9 mm.

*Also measured.* The 7 mm grid reads visible skin as +0.46 mm (median) and up to +3 mm (fingers) outside: the quadratic
B-spline rounds curved skin. A 4 mm grid halves that, but the bake costs 3x as much (it is baked every walking frame).

**Fast solver.** `FL_FAST=1` uses 10 substeps, one linearised solve each, block-Jacobi CG capped at 100 iterations,
and a 3% strain limit: `docs/GPU_SOLVER_NOTES.md`. The fine tee runs 1013 -> 138 ms/frame with the same drape. Its
sleeve hems crumple more than Newton's. It is the GPU candidate and is opt-in.

## Maintenance

A scheduled task ("Failoring browser build upkeep", daily 06:47 Europe/Berlin) rebuilds `failoring.html`, runs the smoke
test and pushes any fix to a branch `upkeep/YYYY-MM-DD`, never to main. Merge such a branch yourself after a look.
