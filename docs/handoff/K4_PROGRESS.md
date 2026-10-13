# K4 progress log (lazy SDF collider)

## Session 2 (agent resuming the WIP branch)

### Step 1 and 2: state at start
- Worktree branch `agent/k4-lazy-sdf`, head b76b793 WIP + 763bd59 handoff notes.
- Uncommitted at start: `w64` (symlink to a /tmp scratch path under the previous agent's scratchpad, a dangling
  link on this machine, not a source file; excluded from commits), `CMakeLists.txt` (adds `test_sdf_lazy` and an
  `FL_ASSET_DIR` define pointing at `godot/assets/average`), `cloth_core/tests/k4_compare.hpp`,
  `cloth_core/tests/k4_gate.cpp` (modified), and untracked `cloth_core/tests/test_sdf_lazy.cpp`.
- `test_sdf_lazy.cpp` refers to `docs/K4_LAZY_SDF.md`, which does not exist yet.
- Build: `cmake -S . -B build_k4 -DCMAKE_BUILD_TYPE=Release` and `cmake --build build_k4 -j` both succeed (no errors).
- ctest: test_basics passed (0.93 s), test_cantilever passed (13.1 s). test_drape ran for more than 10 minutes
  with no result yet (the foreground ctest call timed out at 10 min); still running in background.

### Not yet done
- Accuracy, timing and cloth numbers from `test_sdf_lazy` (not yet confirmed to run to completion).
- Lazy collider is not wired into `ClothSolver` or the Godot wrapper; it is a standalone class, so it is off by
  default by construction. No runtime switch exists.
- Per-bone / rigid-transform variant (done-criterion 1 "and/or") is not implemented; only the narrow-band lazy
  grid is.

### test_sdf_lazy run (Release, `./test_sdf_lazy` in build_k4, exit 0, "RESULT: ok", 1 min 3 s wall, 2 cores, load ~10 from other jobs)
Numbers are as printed by the test. Timings are CPU-clock ms under heavy machine contention; treat them as rough.
- Sphere (static, 20k random points within 1.5 cm of surface): lazy vs analytic max 0.146 mm; lazy vs grid max 0.367 mm;
  normal error max 0.15 deg; sign errors 0/20000. C1 line scan: max excess jump 0; max gradient step 0.
- Synthetic deforming body (20,480 tris, 8 poses): cloth band vs re-bake median 0.0005 mm, p95 0.052 mm, p99.9 0.175 mm,
  max 0.294 mm; normal p95 0.50 deg, max 2.41 deg; 0 sign flips. Timing: full re-bake 32.0 ms/frame, lazy setBody+prefetch
  40.9 ms/frame (LAZY IS SLOWER HERE).
- Real mannequin (26,756 tris, 4 of 24 walk frames): cloth band vs re-bake median 0.000 mm, p95 0.035 mm, p99.9 0.293 mm,
  max 0.579 mm; normal p95 0.55 deg, max 24.45 deg; 1 sign flip in 17,840 samples. Timing: full re-bake 172.2 ms/frame,
  lazy setBody+prefetch 109.2 ms/frame (about 1.6x faster; 4 frames only).
- Lazy vs exact distance p95 2.39 mm; full re-bake vs exact p95 2.36 mm (both are B-spline smoothed; expected).
- Garment on moving body (256 verts, 120 frames, 2 substeps): final vertex difference lazy vs full re-bake mean 0.210 mm,
  max 0.373 mm; line-search failures 0 vs 0; collider wall time per frame full 58.9 ms, lazy 29.8 ms (includes prefetch).

### Done-criteria status
1. Lazy collider with GridCollider-like interface: DONE as a narrow-band brick lazy grid (`SdfGridLazy`,
   `LazyGridCollider`). Per-bone / rigid-transform variant: NOT DONE.
2. Accuracy vs full re-bake: DONE for the cloth band and random shell points (numbers above). NOT DONE: split of
   error at joints vs bends, and a fallback for when the rigid assumption fails (rigid variant not built).
3. Timing and cloth test: DONE for the numbers above, measured on a loaded 2-core machine, which is not a clean
   benchmark. Not measured: wasm timing; single-run quiet-machine timing.
4. Pre-existing ctest tests unchanged: test_basics and test_cantilever pass. test_drape is still running (very slow under
   load). test_garment and test_sdf_grid not yet reported. test_sdf_lazy is new and registered in CMakeLists.txt.

### Open concerns
- The lazy collider is not wired into ClothSolver or the Godot wrapper. It is off by default because nothing uses it.
- On the synthetic body, the lazy path is slower than a full re-bake. Its benefit depends on the body size and on how
  many frames the cloth samples; the mannequin number is the one that matters.
- Max gradient error on the mannequin is 24 deg at one sample (p95 0.55 deg). Not investigated.
- The test file refers to docs/K4_LAZY_SDF.md, which is not written; this log stands in for it.
- CMakeLists passes FL_ASSET_DIR = godot/assets/average. Build works, but the test silently skips the mannequin section
  if the assets are missing.
