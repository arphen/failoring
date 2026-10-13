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
