# Godot editor performance: measurements and what remains

Branch `perf2/godot-editor`, from `origin/godot/editor-v5` (0acc58c). Baseline = that commit, built from a `git archive`
of it. Everything below was measured in this container, not on the Mac: 2 vCPU shared with other agents (load between
0.2 and 5 during the runs), Linux x86_64, gcc 13.3 Release (`-O3 -DNDEBUG`, no `-march`), Godot 4.3 stable headless
(dummy renderer). The Mac numbers quoted in the task (bake about 80 ms, solve 70-150 ms) were not re-measured here.

## How it was measured

`godot/tests/bench_step.gd` (run with `godot --headless --path godot res://tests/bench_step.tscn`) drives the editor's
own functions one frame after another (`_take_job`, `_run_step`, `_collect`, `_set_cloth_mesh`), so it is not paced by
playback. Garments: Tee (teagan) + Pencil skirt (penelope) + Chinos (charlie) on the average body, 48 settle frames and
24 walking frames; the per-frame figures are means over the 24 walking frames. Options: `BENCH_TEE_ONLY`,
`BENCH_QUALITY`, `BENCH_BODY_H`, `BENCH_DUMP`, `BENCH_NO_LOOKAHEAD` (see the file header).

Columns: **bake** = the body SDF grid bake of the frame (its own time; on the new build it runs on another thread, see
below); **solve** = `sim.step(1)`; **worker** = wall time of one frame on the worker (what limits playback, the sim
frames per second is 1000 / worker); the table shows the lowest of two interleaved passes per configuration, since the
shared host swings by about 2x between passes.

| configuration | bake ms | solve ms | worker ms/frame | sim frames/s |
|---|---:|---:|---:|---:|
| baseline, 3 garments, standard (7 mm body grid) | 167 | 548 | 718 | 1.4 |
| new, 3 garments, standard | 179 | 525 | 529 | 1.9 |
| new, same binary, prefetch off (`BENCH_NO_LOOKAHEAD`), standard | 173 | 545 | 721 | 1.4 |
| new, 3 garments, **Preview (new default)** | 99 | 298 | 301 | 3.3 |
| baseline, tee alone, standard | 166 | 174 | 342 | 2.9 |
| new, tee alone, standard | 175 | 176 | 180 | 5.6 |
| new, same binary, prefetch off, tee alone, standard | 171 | 182 | 355 | 2.8 |
| new, tee alone, **Preview (new default)** | 104 | 113 | 116 | 8.7 |
| baseline, tee alone, Draft (7 mm body grid) | 170 | 26 | 198 | 5.1 |
| new, tee alone, Draft (10 mm body grid) | 90 | 27 | 53 | 18.8 |

Mesh sizes: standard 635 verts (tee), 1757 (three garments); Preview 473 (tee), 1337 (three); Draft 381 (tee), 1109
(three). The baseline's checksums and the new build's standard checksums are identical (275849788 for the three
garments, 2514424465 for the tee): the prefetch and the C++ normals change no numbers.

Main-thread cost per walking frame (not in the worker figure, but it runs while the worker solves): `job` (the walk
pose from `BodyGen.pose` and the vertex conversion, GDScript) about 30 ms; `collect` 1-3 ms; the mesh update
(`_set_cloth_mesh`, dummy renderer) 3.2 ms before, 0.2-0.5 ms after.

Body bake alone (`cloth_core/tests/bench_bake.cpp`, average body, 13,380 vertices, 26,756 triangles, median of 7,
CPU time): 7 mm about 160 ms; 10 mm about 90 ms; 12 mm about 83 ms. Accuracy of the 10 mm grid against the 7 mm grid,
at points within 12 mm of the surface (3,532 points): RMS 0.28 mm, mean 0.17 mm, maximum 2.1 mm; 12 mm: RMS 0.49 mm.

## What changed

1. **Body bake overlapped with the solver** (exact: identical numbers). `FailoringSim::prefetch_body_positions` starts the
   SDF bake of the next frame's body on a `std::async` thread; `set_body_positions` takes the grid when the vertices
   match exactly, and bakes as before otherwise. The editor plans one job ahead (`_take_job`) so the next body is known
   during the current step; `_run_step` prefetches it before `sim.step(1)`. The surface motion (which depends on the
   cloth) is still computed when the step starts. Verified before the default changed (item 3): `step_thread_check`
   gives identical hashes for the baseline, this branch synchronous, and this branch threaded; the benchmark checksums
   match the baseline at standard. After the default change, synchronous and threaded still match each other.
2. **Display normals in C++** (exact up to float rounding order; the same rule as `look.gd` `cloth_normals`).
   `FailoringSim::get_display_normals`, computed on the worker with the display positions, so the main thread never
   reads `sim` while a step is in flight. `look.gd` is unchanged and still used for the stage-1 display before the sim.
3. **Quality presets with a body grid per preset** (`body_h` in `QUALITIES`). Standard, Fine and Photo keep 7 mm, so their
   numbers are unchanged. Draft and the new Preview use 10 mm. The editor's Mesh label is now "Simulation quality".
   The default is **Preview** (edge 50 mm, 2 substeps, 8 Newton iterations, 10 mm grid), chosen for the tee: 8.7
   sim frames/s here. On the Mac the bake was about 80 ms in the task brief against about 165 ms here, so the Mac is about
   2x faster; that puts the tee at roughly 17 frames/s there. This is an estimate, not a measurement.
4. **Draft preview while editing** (option, off by default). `draft_while_editing`: an edit (debounced rebuild) builds the
   sim at Draft; Play rebuilds at the chosen quality before the walk. `godot/tests/draft_edit_check.gd` checks the flow.

Intended numerical changes (documented per the brief): the default quality is now Preview, so the default outputs differ
from the baseline (for example `step_thread_check` hashes); Draft and Preview use the 10 mm body grid. Standard and
above are unchanged.

## Position comparisons are not meaningful for the walking garments

A 10 micrometre change of the body grid spacing (7.00 to 7.01 mm) moves the final tee positions by 60 mm RMS (the
garment slides differently); a 10 mm grid moves them by 47 mm RMS. The health statistics stay in the same range
(kinetic energy 0.015-0.021 J, seam gap 0.4-1.1 mm, body clearance -2.2 to -3.2 mm; at frame 60 of the tee at standard,
7.00, 7.01 and 10 mm). So the walking solve is chaotic at the level of the drape: a checksum or a hash changes with any
numerical change, and fit numbers should be compared as statistics over several runs, not position by position.
Solver-only tests on a static tube show the opposite: an ordering change at rounding level moves it by 0.0005 mm RMS
after 20 frames, so the chaos comes from the contact and the walk, not from rounding.

## Investigated and not adopted

* **Solver profile.** `gprof` on a 672-vertex tube (20 frames): 87 % in `SparseCholesky::factor`, 5 % in `solve`, 5 % in
  `assembleEnergy`. About 7.8 ms per factorisation for 2016 unknowns, 194k non-zeros in L, 24.5 Mflop (about 3.1 GFLOP/s
  effective). On the garments the Newton loop hits its cap every substep: 16 iterations per walking frame at standard
  (8 per substep), so about 16 factorisations per frame.
* **Chord Newton** (`reuseFactorization`): no fewer iterations (39.8 vs 38.6 per frame on the tube) and no faster. Matches
  `docs/ARCHITECTURE.md`.
* **Looser Newton tolerance** (1e-5 instead of 1e-6): 30 instead of 39 iterations per frame, but a 4.0 mm RMS change of the
  tube after 20 frames. The iterations stop before convergence, so this is a physics change. Not adopted.
* **Minimum-degree ordering** (instead of nested dissection): -22 % flops at 672 vertices, but +7 % at 2542 vertices. Not
  adopted.
* **`restrict` on the factor's inner loops**: bitwise identical results, 15 % slower. Not adopted.
* **Nested-dissection leaf size** (8, 24, 64, 150): no measurable change.
* **Surface-motion hash map and the topology rebuild in the bake** (`buildMesh`): a few ms each; not changed.

## What remains (in order of payoff)

1. **Factorisation.** It is 87 % of the solver and is compute-bound at about 3 GFLOP/s effective. A deterministic
   tree-parallel supernodal factor (independent subtrees on threads, fixed accumulation order) is the next step on a
   multi-core Mac. Not done: it cannot be verified on this 2-vCPU host.
2. **Newton iterations.** Every substep hits the cap of 8 at standard; the notes in `ARCHITECTURE.md` point at contact
   handling (the active set changes between iterations). Fixing that would make Newton converge in fewer steps without
   a numerical compromise. Not done.
3. **Main thread.** `BodyGen.pose` (GDScript skinning of the mannequin, about 25 ms) and the vertex conversion in `_to_v3a`
   per walking frame. Moving them into C++ touches `body_gen.gd`, which was outside this change.
4. **Render side not measured.** The numbers above are the simulation side with the dummy renderer. Shadow map size,
   MSAA and SSAO were not timed (`xvfb-run` is available here, but not run for this report).
5. **Overlap needs free cores.** On this 2-vCPU host the prefetch thread competes with the solver and the main thread,
   which is why the three-garment gain (-26 %) is smaller than the tee gain (-47 %). The effect on a Mac with more free
   cores is expected to be larger but is not measured.
