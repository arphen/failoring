> **Historical.** Checkpoint from 2026-10-12 afternoon. Current state: `docs/STATUS.md`. The "BLOCKER" about
> `tensionStiffness` below is resolved (core default off, app on).

# Mannequin task - work-in-progress handoff (checkpoint 2026-10-12 ~15:15 CEST)

Task: `attachment.txt` (realistic parametric mannequin for Failoring). Decision taken: MakeHuman **CC0 assets** +
our own code (docs/BODY_DESIGN.md); the whole app is AGPL-3.0 (user's decision, no further discussion needed).
This file is a status note for continuing the work; delete it before the final delivery.

## Restore after a container reset

```sh
mkdir -p ~/work && tar xzf failoring-wip-2026-10-12.tar.gz -C ~/work      # -> ~/work/failoring (+ wip/scratch)
# wasi-sdk 24 (GitHub release) for tools/build_wasm.sh
mkdir -p ~/toolchain && curl -L https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-24/wasi-sdk-24.0-x86_64-linux.tar.gz | tar xz -C ~/toolchain
# MakeHuman CC0 assets (only needed to re-run tools/convert_makehuman.py; app/assets/body_mh.bin is already in the tarball)
git clone --filter=blob:none --sparse https://github.com/makehumancommunity/makehuman.git ~/ext/mh && cd ~/ext/mh && \
  git checkout a8bc2d54ff0ac92e78ff71431b1023eda42bf482 && git sparse-checkout set --no-cone \
  '/LICENSE*' /makehuman/data/3dobjs/base.obj /makehuman/data/rigs/default_weights.mhw '/makehuman/data/targets/measure/*' \
  '/makehuman/data/targets/macrodetails/universal-female-young-*' /makehuman/data/targets/macrodetails/caucasian-female-young.target \
  /makehuman/data/targets/macrodetails/african-female-young.target /makehuman/data/targets/macrodetails/asian-female-young.target \
  '/makehuman/data/targets/macrodetails/height/female-young-averagemuscle-averageweight-*' \
  '/makehuman/data/targets/breast/female-young-averagemuscle-averageweight-*' '/makehuman/data/targets/breast/nipple-*' \
  '/makehuman/data/targets/buttocks/*' '/makehuman/data/targets/stomach/stomach-pregnant-*' '/makehuman/data/targets/pelvis/*'
# build + test
cd ~/work/failoring && W=~/toolchain/wasi-sdk-24.0-x86_64-linux sh tools/build_wasm.sh
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
node app/build.js                     # -> failoring.html (single file; currently ~2.1 MB)
node tools/verify_body.js [--quick] [--legacy]   # headless body/collider/drape report -> out/verify_body.json
```
Playwright: `require('/opt/npm-tools/node_modules/playwright')`, Chromium with `--use-gl=angle --use-angle=swiftshader --enable-unsafe-swiftshader`.
The untouched original source is the user's `~/projects/failoring.tar.gz` (unpack it to diff against).

## Done (implemented and checked)

* `docs/BODY_DESIGN.md` design note (needs small text updates, see TODO).
* `tools/convert_makehuman.py` -> `app/assets/body_mh.bin` (FLBODY01, raw deflate, 298 KB, 60 targets, landmarks, masks).
* `app/src/body.js` (FL.Body): load/calibrate, blend shapes + macro interpolation, tape measures (hull perimeters),
  active-set quasi-Newton fit of all sliders (residual <= 0.4 mm on all presets, ~15-50 ms, slider drags ~10-17 ms),
  section table for the wrap. New FL.PRESETS (shared.js) with muscle/cup/belly body-type fields.
* Signed-distance grid collider `cloth_core/{include/failoring,src}/sdf_grid.*` (2^3-brick sparse fine level, dense
  coarse level, winding-number sign, quadratic B-spline C1 lookup) + `test_sdf_grid`; wired into wasm (fl_set_body_mesh,
  fl_body_eval, fl_body_stats ...), CMakeLists, gdextension CMakeLists, build_wasm.sh. Accuracy vs polygons:
  trunk/limbs median ~0.13 mm / 95 % ~0.7 mm; hands/feet/head median ~0.7 / 95 % ~2 mm. Bake 185-390 ms (wasm),
  lookup 105 ns vs 584 ns for the old primitive union.
* Display: cloth drawn as a subdivided mesh whose edge midpoints follow the body (fl_display_setup / fl_display_positions),
  no 4 mm body shrink any more; rendered cloth does not penetrate fitting garments; resting gap ~1.7-1.9 mm.
* App wiring: worker.js (bake per body key, legacy primitive fallback, gravity ramp, display mesh, solver stats in 'done'),
  view3d.js (mesh mannequin), main.js (Body tab: primary + secondary sliders with "auto", body type, regen), shoot.js,
  style.css, build.js (embeds body asset as base64).
* Harnesses: `tools/verify_body.js`; scratch tools in `wip/scratch/proto/` (see below).

## In progress: the A-line "slides off" problem -> solver contact/convergence fix

Symptom: at Standard quality (2 substeps, 8 Newton its) the A-line's waist ring stretched 10-37 % and the skirt slid
down past the hips (petite fell to the floor - **also with the original primitive body**, so pre-existing).
Root cause found with per-iteration Newton logging (see wip/scratch/proto/newtonlog.js; the logging code itself was removed):
Newton never converges (always hits the iteration cap); taut waistline / seam-corner vertices get 20-38 mm Newton
steps sideways because the Gauss-Newton stretch Hessian has **no stiffness across a stretched yarn**; the line search
cuts every step and the ring ratchets down the body.

Experiments (wip/scratch/m1..m5.jsonl, tabulate with `python3 wip/scratch/proto/tab.py <files>`; runner
`wip/scratch/proto/matrix.js` + `runmatrix.sh`, run in background with nohup - 2 cores only):
* My earlier "contact-force Coulomb friction" (previous session) is **harmful**: tall/plus pencil exploded (ring x55).
  **It has been removed** (cloth_implicit.cpp was rewritten from the original).
* Tension (geometric) stiffness in the Hessian (`SolverParams::tensionStiffness`, exact PSD part for C > 0;
  finite-difference-checked correct, wip/scratch/bt/hcheck.cpp): all presets' A-lines hold, rings <= 3.5 % strain
  (except plus: 80 cm pattern on a 102 cm waist cannot close - real), line-search failures drop, no speed cost.
* Contact activation band (`SolverParams::contactBand`, was hard-coded 1e-7 m): 1e-4..5e-4 removes nearly all
  line-search failures; with or without "snapping" barely matters. Current default 1e-4, no snap.

**Current code state (as checkpointed):** cloth_implicit.cpp = original + tension stiffness (default ON) + contactBand
(1e-4) + Newton/substep counters. wasm rebuilt with it. Params 7 (tension 0/1) and 8 (band) exposed in fl_set_param.

**BLOCKER found just before the checkpoint: test_cantilever FAILS with tensionStiffness = true** (tip drop +30..96 %,
grain ratio 1.29). With it off the test passes exactly as before. Cause: in the bending-dominated cantilever Newton
needs 13-21 iterations instead of 6 with the tension term (cap 12 -> not converged -> wrong sag). Repro:
`wip/scratch/bt/cdbg.cpp` (`./cdbg <tension 0/1> <h> [newton]`, FL_NEWTON=1 prints per-substep convergence).
In the same ctest run test_basics and test_drape PASSED; test_garment and test_sdf_grid were still running (unknown).

Options to decide next:
1. Default `tensionStiffness = false` in the core (tests unchanged) and enable it from the app worker
   (`X.fl_set_param(7, 1)`), documented as a garment-on-body option; verify all ctests + app drapes.
2. Understand why the exact tension Hessian slows Newton on the cantilever (suspects: switching on/off as C crosses 0
   in bending; interaction with the GN shear term; line-search acceptance) and fix it so it can be on by default.
3. Fallback without tension stiffness: band 5e-4 alone also held the A-lines in the matrix ("band"/"bandns" rows),
   but curvy pencil ring stretched 28 % without snap.
Then: re-run full ctest, verify_body.js (all presets x plans, --legacy), and the before/after timing with the
original app (orig tree + orig wasm; `ROOT=<orig> ORIG=1 node matrix.js '[{"name":"before","legacy":true}]' ...`).

## TODO after the solver decision (task deliverables)

1. Visual check (Playwright/SwiftShader): every preset front / side / three-quarter (wip/scratch/proto/bodyshots.js),
   pencil + A-line draped on >= 3 bodies via photo shoot (wip/scratch/proto/shoot_harness.js), contact sheets.
   Build a test HTML first: `node app/build.js` (writes failoring.html).
2. Numbers: collider vs displayed deviation, regen ms per slider change, download size added (HTML 1.58 MB -> ~2.1 MB),
   sim ms/frame before/after (baseline legacy pencil ~100-106, A-line ~176 ms/frame; new ~90-110 / ~180-200).
3. Docs: update docs/ARCHITECTURE.md (body, collider, display mesh, solver changes, numbers, limitations: real GPU and
   Safari not verified, patterns not graded - plus/curvy too small for the 80/104 cm skirts); fix BODY_DESIGN.md
   (sign = winding numbers not pseudo-normals; display = subdivided cloth not vertex lift; asset 298 KB).
   Add THIRD_PARTY.md (MakeHuman CC0: repo, commit a8bc2d5..., files used, copyright holders; three.js MIT) and the
   AGPL-3.0 LICENSE text (fetch verbatim from GitHub, read before including).
4. Note for the definition of done: patterns.js has two skirts + "blank" (a rectangle) - say so; ungraded patterns do
   not fit plus (too small) - test graded copies or report.
5. Deliver to the user's computer in a new folder `~/projects/failoring-mannequin/` (tarball, failoring.html, docs,
   screenshots); never overwrite their files. Update the claude.ai Project docs.

## Scratch tools (wip/scratch/proto)
trace_aline.js (per-frame ring/strain trace; env SUB, WRAPY, FRIC/BAND/GEO no longer valid), newtonlog.js (needs the
removed fl_dbg_log - kept for reference), matrix.js/runmatrix.sh/tab.py (variant matrix), wrapprobe.js (start-pose
clearance), levels.js (waist/hip/ring-rest heights per preset), shoot_harness.js, bodyshots.js, closeup.js, probe.js,
gen_test.js, bodyview.*. wip/scratch/bt: cdbg.cpp (cantilever repro), hcheck.cpp (Hessian FD check).
