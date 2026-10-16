# Crochet mode: ticket board

Single source of truth for progress (docs/CROCHET_PLAN.md §14). The orchestrator updates this
after every state change and commits (`board: T1.3 -> review`). Statuses: `todo` →
`in_progress` → `review` → (`changes_requested` → `in_progress` …) → `done`, or `blocked`
(with the reason and what would unblock it).

Branch: `crochet/main`. Never merged into `main` without the user saying so (§14.9.3).

## Environment (checked 2026-10-16)

* Base commit: `main` @ f5a0a98. `crochet/main` branched here.
* Baseline ctest: `docs/crochet/baseline_ctest.txt` — 10 of 13 pass. **Pre-existing failures:
  test_layers, test_wedge, test_static** (reproduced twice, on the unmodified baseline). They must
  not get worse; do not try to fix them in crochet tickets. "All old tests pass" in a ticket means
  *no regression against this baseline*.
* Regression dump: `docs/crochet/baseline_garment_positions.txt` (672 vertices, written by
  `FL_DUMP=<path> ./build/test_garment`; bit-identical is the T1.1 bar).
* Godot 4.3 binary: `/Users/swozny/toolchain/godot-4.3-stable/Godot.app/Contents/MacOS/Godot`
  (`make run` opens the editor). GDExtension rebuild: `tools/build_gdext.sh`.
* Implementer worktrees: `.worktrees/<ticket>` (gitignored), branch `crochet/<ticket>`.
  Build inside the worktree with `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`.
* C++ build+test: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
  (test_drape ~2 min, test_static ~3 min on this machine).
* Godot headless test: `<godot> --headless --path <worktree>/godot res://tests/<name>.tscn` (prints PASS/FAIL).

## Tickets

| Ticket | Title | Depends on | Status | Owner model | Round | Commit | Notes |
|---|---|---|---|---|---|---|---|
| T0.1 | Branch, baseline ctest, board, status, \`FL_DUMP\` regression dump | – | done | orchestrator | 1 | (this commit) | baseline has 3 pre-existing failures, see above |
| T1.1 | Refactor finalize() to per-triangle triRest_/triMat_ (no new API) | T0.1 | in_progress | default | 1 | | ★ test_garment bit-identical to the T0.1 dump |
| T1.2 | IntrinsicMesh, addIntrinsicPanel, extraMass, strain-limit edge rest lengths, barrier guard | T1.1 | todo | | 0 | | ★ §2.7 test 1 (< 1e-9 m) |
| T1.3 | HingeMod, signed hinge with rest angle | T1.2 | todo | | 0 | | ★ §2.7 tests 3, 4; old tests unchanged |
| T1.4 | J-curve stretch (+ JSON keys) | T1.1 | todo | | 0 | | ★ §2.7 test 5 |
| T1.5 | addPointSeam, SolverParams::dampingScale | T1.1 | todo | | 0 | | §2.7 test 6 |
| T1.6 | Contact force accessors, RoundedBoxCollider | T1.1 | todo | | 0 | | box SDF + resting patch force balance |
| T2.1 | materials/yarns.json + loader (crochet_core target created here) | T0.1 | done | default | 1 | 225d0ab (merged 9b3a0d0) | loads; ids unique; fibres exist |
| T3.1 | Stitch model | T2.1 | todo | | 0 | | §4.5 tests |
| T4.0 | CrochetPARADE spike + decision A/B/C | T0.1 | in_progress | default | 1 | | docs/crochet/T4.0_crochetparade.md, 7 steps |
| T4.1 | Bridge (A/B) or DSL parser→AST (C) | T4.0 | todo | | 0 | | format fixed by T4.0 decision |
| T4.2 | Facing/RS, point seams, count checks (A/B) or compiler (C) | T4.1 | todo | | 0 | | ★ §5.4 tests |
| T5.1 | Faces (BFS base path, zipper), rest shapes, orientation | T4.2, T1.2 | todo | | 0 | | ★ §6.10 test 4 hygiene, all fixtures |
| T5.2 | Materials, hinge mods, seams → IntrinsicMesh | T5.1, T3.1, T1.3, T1.5 | todo | | 0 | | §6.10 test 6 (mass) |
| T5.3 | Layouts (flat, fold, revolve, attached, arc; CP positions if A/B) | T5.2 | todo | | 0 | | layout PNGs under docs/crochet-shots/layout/ |
| T5.4 | Relaxation + mesh tests | T5.3 | todo | | 0 | | ★ §6.10 tests 1,2,3,5 (circle is the key one) |
| T6.1 | Scenes hang_pin, hang_peg, settle loop | T5.4 | todo | | 0 | | §7.4 test 3 for the pouch |
| T6.2 | RigidLoad: insert, release, fit check | T6.1, T1.6, T1.4 | todo | | 0 | | ★ §7.4 tests 1, 2, 4 |
| T6.3 | Metrics | T6.1 | todo | | 0 | | §7.5 keys; 90° fold reads 90±1 |
| T6.4 | crochet_cli + PNG snapshots | T6.3 | todo | | 0 | | all fixtures via CLI; PNGs in docs/crochet-shots/ |
| T6.5 | Five-variant report (USER GATE §14.9) | T6.2, T6.4 | todo | | 0 | | table + snapshots in CROCHET_STATUS.md |
| T6.6 | Real-bag fixture (user measurements) | T6.5, user data | blocked | | 0 | | blocked: waiting for user data (asked at gate 1) |
| T7.1 | GDExtension CrochetPattern, CrochetScene | T6.4 | todo | | 0 | | crochet_api_check passes headless |
| T7.2 | Crochet scene skeleton: panes, CodeEdit live compile, worker thread | T7.1 | todo | | 0 | | headless: load pouch, compile, run 200 frames |
| T7.3 | Yarn/hook panel, metrics tab, scene/load menus, editor entry button | T7.2 | todo | | 0 | | headless test drives each menu once |
| T7.4 | Surface shader, double-sided thickness | T7.2 | todo | | 0 | | shader compiles; screenshot if possible |
| T8.1 | Chart view | T7.2 | todo | | 0 | | headless: one symbol per stitch for the pouch |
| T8.2 | Selection chart ↔ 3D ↔ pattern line | T8.1 | todo | | 0 | | select stitch 100 → vertex + line |
| T8.3 | Lego overrides | T8.2 | todo | | 0 | | override gains a line, recompiles, hinges present |
| T8.4 | Compare + five-variant run | T8.3 | todo | | 0 | | five runs, table with five corner angles |
| T9.1 | Calibration wizard + records | T7.3 | todo | | 0 | | record changes stitchMech; UI says "calibrated" |
| T9.2 | Hinge auto-fit (bisection) | T9.1 | todo | | 0 | | recovers a known bendScale within 5 % |

## Decisions

* (none yet)
