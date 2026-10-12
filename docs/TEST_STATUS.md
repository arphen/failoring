# Test status

Release build, full ctest run on branch `worktree-agent-a87c53063bacf27cc`.

## Commands

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

## Results

| Test | Result | Duration |
|------|--------|----------|
| test_basics | Passed | 1.22 sec |
| test_cantilever | Passed | 16.20 sec |
| test_drape | Passed | 835.85 sec |
| test_garment | Passed | 78.28 sec |
| test_sdf_grid | Passed | 15.26 sec |

Summary: 100% tests passed, 0 tests failed out of 5. Total ctest time 946.83 sec (real).

Build: configure succeeded; `cmake --build build -j2` exited 0 with no errors.

## Notes

- Debug build not run (Release only, per task).
- The `tensionStiffness` option (default false in core) was not toggled in this run; these results cover the default configuration only. The earlier test_cantilever failure with tension on was not re-checked here.
- test_drape dominates runtime on a 2-core shared host.
