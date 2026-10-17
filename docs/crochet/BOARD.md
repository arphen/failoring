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
| T1.1 | Refactor finalize() to per-triangle triRest_/triMat_ (no new API) | T0.1 | done | default | 1 | 7dc34d2 (merged dddb4ea) | ★ test_garment bit-identical: PASS |
| T1.2 | IntrinsicMesh, addIntrinsicPanel, extraMass, strain-limit edge rest lengths, barrier guard | T1.1 | done | default | 1 | cc20eee (merged cbfc587) | ★ §2.7 test 1: PASS (max diff 0 m) |
| T1.3 | Signed hinge with rest angle | T1.2 | done | default | 1 | 8369c65 (merged a63db24) | ★ §2.7 tests 3, 4: PASS (rel err 4.5e-08; settled 0.5000 rad); 2 mutations caught |
| T1.4 | J-curve stretch (+ JSON keys) | T1.1 | done | default | 1 | 1eaa92c (merged 3fafe40) | ★ §2.7 test 5: PASS (C 0.199503 vs analytic 3e-12); 1 mutation caught |
| T1.5 | addPointSeam, SolverParams::dampingScale | T1.1 | done | default | 1 | 98b8dbe (merged 204d1b3) | §2.7 test 6: PASS (max pair dist 7.9e-07 m); 2 mutations caught (1 weak-seam mutation is non-detecting by design of test 6) |
| T1.6 | Contact force accessors, RoundedBoxCollider | T1.1 | in_progress | default | 1 | | box SDF + resting patch force balance |
| T2.1 | materials/yarns.json + loader (crochet_core target created here) | T0.1 | done | default | 1 | 225d0ab (merged 9b3a0d0) | loads; ids unique; fibres exist |
| T3.1 | Stitch model | T2.1 | done | default | 1 | ac0e14a (merged 66ef507) | §4.5 tests: PASS |
| T4.0 | CrochetPARADE spike + decision A/B/C | T0.1 | done | default | 2 | 3c45316 (merged b736625) | decision B (see Decisions); r2 fixed cp_export pipe flush |
| T4.1 | Bridge: exporter + C++ importer into StitchGraph + `#!` extension-line reader (B) | T4.0 | done | default | 1 | a066c9d (merged b31c3f8) | §5.4-ready: 110 PASS lines; 10/10 exports byte-identical; 2 mutations caught; docs/CROCHET_DSL.md pins the `#!` syntax |
| T4.2 | Facing/RS, point seams, count checks (B) | T4.1 | in_progress | default | 1 | | ★ §5.4 tests; graph.cpp facing pass + `#! count` |
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

* 2026-10-16 — `toFabric` (T3.1, §4.1) sets the FabricMaterial fields that exist today and carries the
  J-curve lock terms in `StitchMech.lockWale/lockCourse/lockRatio`; mapping them into
  `FabricMaterial.lockU/lockV/ratio` happens in T5.2, once T1.4 has added those fields.
* 2026-10-16 — `StitchMech` gains `compressionRatio` (the §4.4 reference list has it; §4.1's struct
  listing omits it): starter value 0.2, no scaling formula, documented in the header.
* 2026-10-16 — §2.1 step 8 (T1.2) is implemented as: a comment on `BendC::rest` stating that the implicit
  solver reads it (from T1.3 on) while XPBD/VBD keep the unsigned positional angle and ignore it. No
  `// TODO` marker (the review laziness scan rejects them and the behaviour is deliberate, not pending).
* 2026-10-16 — J-curve material JSON keys are `lock_strain_warp`, `lock_strain_weft`,
  `lock_stiffness_ratio` (snake_case like the existing `tensile_warp_N_m`), optional, defaults 0 / 0 / 1;
  validation lock >= 0 and ratio >= 1. The TriC u axis is warp, v is weft (T5.2 maps crochet
  wale -> warp when it builds materials from yarns).
* 2026-10-16 — T4.1 test data: the ten `cp_export.mjs` outputs are committed under
  `patterns/cp/exported/*.json` (~6.6 MB deterministic JSON) so `ctest` is hermetic (no node, no
  clone needed at test time); regenerate them with
  `CP_DIR=<repo>/third_party/crochetparade node tools/crochet/cp_export.mjs --out patterns/cp/exported/<name>.json patterns/cp/<name>.txt`
  whenever a fixture changes. Extension lines are appended at the end of each fixture file so the
  exports' `line` fields stay stable.
* 2026-10-16 — **§5.0 decision: B.** CrochetPARADE is the pattern front end: vendored clone
  `third_party/crochetparade` @ `06e987b13ccfc94cfdfcebc1fe4664d37d2c6687` (GitHub; newer than the
  Codeberg head `be11c8b0`; gitignored, fetch steps in `docs/crochet/T4.0_crochetparade.md` §8 and in
  `tools/crochet/cp_export.mjs`). `patterns/cp/*.txt` replace the `.fcro` fixtures. The two structures
  CP cannot express (second targets of through-both-layers stitches and strap-end joins, optionally
  piece names) go into `#!` extension lines that CP ignores (comment lines do not shift rows) and our
  importer reads; the exact syntax is pinned in `docs/CROCHET_DSL.md` (T4.1). Fold and surface-slst
  placement are T5.3 layout concerns, not grammar gaps. T4.1 = exporter + C++ importer into
  `StitchGraph` + extension reader; T4.2 = RS/WS facing, point seams from `links`, count checks.
  The 66 s bag layout must run on a worker/cache in T7.1; the tube radius drift gets another look in
  T5.3.
* 2026-10-16 — T1.2 must keep panel hinge alphas *bit-identical*: the §2.1 step 4 harmonic mean is only
  computed when the two triangles' Beffs differ (`Beff1 == Beff2` -> keep `Beff1`), because
  `2/(1/B + 1/B)` can differ from `B` by 1 ulp. Verified by the test_garment dump.
* 2026-10-17 — T1.6: `rigidContactForces()`/`contactNormals()` exactly as §2.5 (N per vertex, last
  Newton iteration; normals valid where the force > 0), plus an audit comment in cloth_implicit.cpp
  proving the invariant (contactN_ is written for every w != 0 vertex whenever a collider exists, and
  a positive force requires both). `RoundedBoxCollider` mirrors the gdextension `OrientedBoxCollider`
  structure: rotation convention `world = center + R * local` (row-major Mat3, axes are the columns),
  core half extents `H = half - r`, `d = length(max(|l| - H, 0)) + min(max_i(|l_i| - H_i), 0) - r`,
  exact normals (outside: normalize(max(q,0)); inside the core: the nearest face). Acceptance: SDF
  spot checks + central-difference gradient agreement on a rotated box, then a resting patch whose
  vector force balance `sum(N_i n_i)` equals `(0, m g, 0)` within 1%.
* 2026-10-17 — T4.2: facing/RS is resolved at import time (§5.5); a `turn` recorded on a chains-only
  foundation line does not flip row 1 ("row 1 is RS by default"); on a WS row `blo`/`flo` swap into the
  RS-relative value. Count checks are the new `#! count [row] N` extension keyword (N = stitch nodes in
  the row, turning chains and join sl sts included), with correct per-row count lines appended to every
  fixture (mechanically derived from the committed exports / T4.0's verified counts). `fl::PointSeam`
  (new `graph.cpp`) resolves each `Link` for T5.2's `addPointSeam`: `stitch` = the seam stitch whose
  width `w_s` is the share, `partner` = the second target.
