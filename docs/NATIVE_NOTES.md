# Native build: benchmark, Godot wrapper review, build status

Status as of 2026-10-12. Branch `worktree-agent-a3fbad135b828ea6c`, which merges `wip/godot-native-agent`
(commit 7082b3e) and adds this file plus a benchmark change. Nothing here was run inside Godot: there is no Godot
binary in this environment, so `godot/walk_demo.gd` and the `FailoringSim` class were compiled but never executed.

## What compiled

| Target | Result |
|---|---|
| `bench_native` (cloth_core, `-DFL_BENCH=ON`, Release = `-O3 -DNDEBUG`, gcc 13.3) | builds, runs |
| `bench_native` with `-march=native` (`build-native/`) | builds, runs |
| `gdextension` (`libfailoring.so`, godot-cpp `godot-4.3-stable`, Release) | builds and links, no warnings, no source changes needed |

Build commands:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFL_BENCH=ON
cmake --build build -j2 --target bench_native
git clone --depth 1 --branch godot-4.3-stable https://github.com/godotengine/godot-cpp third_party/godot-cpp
cmake -S gdextension -B build-gd -DCMAKE_BUILD_TYPE=Release
cmake --build build-gd -j2
```

Note: `bench_native` only exists with `-DFL_BENCH=ON`, which is off by default.

The extension writes its library to `godot/bin/libfailoring.so`. That path is a tracked file from the base commit
(an older build, before this API), so the rebuilt library was not committed. It is a 1.67 MB build from this
environment; rebuild it locally rather than trusting the tracked copy.

## Benchmark (native, single thread)

Machine: x86_64, 2 cores shared with other agents. Load average was 1 to 17 during the runs, so wall-clock numbers are
inflated. The CPU-time column is the one to use. `bench_native` now prints both (added in this commit).

Bake = `SdfGrid::bake` of a UV-sphere ellipsoid, h = 7 mm, median of 5. Solve = test_garment's tube dress, 4 substeps,
100 frames, median of frames (analytic and sdf: 3 runs; analytic_fine: 1 run).

| Case | -O3 CPU | -O3 wall | -march=native CPU |
|---|---|---|---|
| bake 16,128 tris (8,066 verts) | 53.4 ms | 111 ms | 44.4 ms |
| bake 65,024 tris (32,514 verts) | 95.2 ms | 156 ms | 78.4 ms |
| solve analytic, 672 verts, 1,200 tris, 20 mm | 308.6 ms/step | 776 ms/step | 284.2 ms/step |
| solve sdf body (grid of 128x64 ellipsoid), same garment | 315.2 ms/step | 809 ms/step | 261.1 ms/step |
| solve analytic, 2,542 verts, 4,800 tris, 10 mm | 2,655 ms/step | 3,542 ms/step | 2,293 ms/step |

Earlier -O3 run before the CPU column existed (heavy load): bake 183.6 ms (wall, 16k tris), 322.3 ms (wall, 65k tris).

Checksums (the bake checksum matches exactly between -O3 and -march=native; the solver checksums do not):

* analytic 672 verts: -O3 368.827, -march=native 383.460 (4 % apart; -O3 repeated the same value twice)
* sdf 672 verts: -O3 393.549, -march=native 393.551
* analytic_fine: -O3 1393.908, -march=native 1393.906

`-march=native` enables FMA contraction, so rounding differs. A 4 % difference in the summed final positions of the
672-vertex case is larger than rounding should give after 100 frames. I did not diagnose it (for example, a Newton or
line-search path that differs). Treat the solver as flag-sensitive until someone checks.

### Threads

Not used. `FL_OPENMP` is OFF by default, and the option only enables `#pragma omp` regions, of which there are none in
cloth_core (the grep for `pragma omp`, `std::thread`, `<thread>` and `async(` returned nothing). The benchmark process
reported `Threads: 1` in /proc while running, and `libgomp` is not linked. CPU time sits below wall time under load,
which is what one descheduled thread looks like.

### Comparison with the browser (wasm, single thread)

The browser numbers come from `docs/WIP_HANDOFF.md` (bake 185-390 ms, wasm wall time) and `docs/ARCHITECTURE.md`
(solver 90-110 ms/frame pencil skirt, 180-200 ms/frame A-line, garments of 400-700 vertices).

* Bake: native is about 2 to 4 times faster (53-95 ms CPU here). Not like-for-like: the browser bakes the mannequin
  mesh, this benchmark bakes an ellipsoid of 16k or 65k triangles.
* Solver: native is not faster. The 672-vertex bench tube takes 260-310 ms/step, above the browser's 90-200 ms for
  skirts of 400-700 vertices. This is a real discrepancy but not a fair one: the browser numbers come from an earlier
  measurement on a different container, the garments differ, and I could not build the wasm target here to measure
  both on the same host. The solver numbers do not support a claim that the native build is faster.

## Godot wrapper review (gdextension/src/failoring_sim.cpp/.h, godot/walk_demo.gd vs wasm/wasm_api.cpp)

### Verified to match the browser

* Display mesh: subdivision pattern (4 triangles per cloth triangle, midpoint per edge) matches `FL.subdivide`
  in app/src/shared.js, index for index.
* `get_display_positions`: same algorithm as `fl_display_positions` (edge midpoints pushed out, triangle lift capped at
  4 mm, threshold 12 mm), default margin 0.0008 as `DISPLAY_MARGIN` in worker.js.
* `set_body_positions`: same as the browser's walkStep: bake the new pose with the stored triangles and h, then set the
  surface motion from `surfaceMotion` (nearest previous body vertex within 5 cm, 3 cm cell hash), as worker.js does.
* `set_param`: all nine wasm ids map to names; `contact_offset` is extra.
* `get_body_stats` returns the same eight values in the same order as `fl_body_stats`.
* `begin()` keeps params, materials, primitive colliders and the body, as `fl_begin` does.

### Missing from the wrapper

1. `fl_solver_stats` has no counterpart (line-search failures, Newton iterations, substeps, CG iterations). The browser
   reads it for the done message. Add a getter.
2. `fl_add_material`: the wrapper only loads materials from fabrics.json. Custom material parameters (density,
   stiffness, ...) cannot be passed in.
3. `fl_set_colliders` replaces all primitives; the wrapper appends (`add_*`) and needs `clear_colliders()` first. This
   is a semantic difference, not a bug.

### Wrong or different behaviour

4. **walk_demo.gd: the body motion persists between bakes (high).** `ClothSolver::setColliderMotion` stores the
   displacement and nothing clears it; the solver reads it on every step (cloth.cpp:399-409). The browser sets the
   motion before every `fl_step(1)`. The demo calls `set_body_positions` only every 6 frames (BAKE_EVERY), so the
   6-frame displacement is applied on all six steps: the friction sees about six times the surface motion. The body
   also jumps every 6 frames. Fix: bake every frame, or clear the motion after each step (`set_body_motion` with an
   empty array).
5. **`get_panel_vertices_2d` returns grain-rotated coordinates (high for walk_demo.gd).** The solver stores
   `restUV_ = rotate(uv, -grain)` (cloth.cpp:128). The wasm `fl_rest_uv` returns the raw pattern coordinates
   (`g.restUV` before grain). The demo pre-wraps the panels from these with grain PI/2, so the pieces start in the wrong
   place and the seams do not start closed. Fix: keep the raw `PanelMesh::verts` per panel in the wrapper, as wasm's
   `State::restUV` does.
6. **Exceptions are not caught (high).** `addPanel` and `addSeam` throw `std::logic_error` once the first step has run
   (`finalized_`, cloth.cpp:115, 146), and `addSeam` throws `out_of_range` on a bad index (wrapper pre-checks most
   cases). With no try/catch in the wrapper an uncaught exception ends the Godot process. Wrap the calls and
   `push_error`. (The wasm build traps instead, via `FL_THROW`.)
7. **`setup_display` goes stale silently (medium).** The display triangles use midpoint indices `n + e` with `n` taken
   at `setup_display()`, but `get_display_positions` uses the current vertex count. Adding a panel after `setup_display`
   shifts the midpoints, so triangles point at the wrong vertices with no error. Store the count at setup and report a
   mismatch, or document that setup must come last.
8. **`add_rect_panel` meshes differently from the browser (medium).** It calls `meshRect`; the browser uses
   `meshPolygon` for every panel, and the wrapper floors a zero edge length at 6 mm where the browser passes its value
   directly. Triangulation, and so the numbers, can differ from the browser for the same garment. Not measured.
9. **`add_polygon_panel` does not normalise winding (medium).** The browser's `FL.normalizePiece` (app/src/shared.js)
   reverses clockwise outlines before the seams index their edges. The wrapper accepts either winding without the
   reversal, so a clockwise outline gives different edge numbers from the browser, and `add_seam` joins the wrong
   edges. Reverse to counter-clockwise, as the app does, or document the mapping.
10. `body_eval` returns 1e30 when the body is disabled; wasm evaluates whenever a grid exists (`fl_body_eval` ignores
    `fl_body_enable`). Minor.
11. `set_body_motion` with a size other than the cloth vertex count is silently ignored by the solver
    (`movingCollider` check, cloth.cpp:399). Report it with `push_error`.
12. `reset_velocities()` is a no-op kept for compatibility. Remove it, or it misleads callers.
13. Performance: `set_body_positions` copies the whole cloth position array on each call and bakes synchronously. This
    is the design (the browser does the same), but at 60 fps it needs to run every few frames or off the main thread.
14. Unverified in Godot 4.3: `walk_demo.gd` passes untyped array literals to `PackedInt32Array.append_array`, which may
    need `PackedInt32Array([...])`.

## Not done

* Nothing executed in Godot: no scene load, no `FailoringSim` call, no `walk_demo.gd` run.
* The wasm build was not compiled here, so the browser and native solver numbers are not measured on the same host.
* No wrapper fixes beyond compile fixes (none were needed). The findings above are for the next change.
