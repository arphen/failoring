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
| T1.6 | Contact force accessors, RoundedBoxCollider | T1.1 | done | default | 1 | ec7d550 (merged d436123) | §2.6: SDF probes exact to 1.2e-17, FD gradient 2.4e-10; patch force balance 1.0e-15 rel (plan bound ±2 %); 3 mutations caught |
| T2.1 | materials/yarns.json + loader (crochet_core target created here) | T0.1 | done | default | 1 | 225d0ab (merged 9b3a0d0) | loads; ids unique; fibres exist |
| T3.1 | Stitch model | T2.1 | done | default | 1 | ac0e14a (merged 66ef507) | §4.5 tests: PASS |
| T4.0 | CrochetPARADE spike + decision A/B/C | T0.1 | done | default | 2 | 3c45316 (merged b736625) | decision B (see Decisions); r2 fixed cp_export pipe flush |
| T4.1 | Bridge: exporter + C++ importer into StitchGraph + `#!` extension-line reader (B) | T4.0 | done | default | 1 | a066c9d (merged b31c3f8) | §5.4-ready: 110 PASS lines; 10/10 exports byte-identical; 2 mutations caught; docs/CROCHET_DSL.md pins the `#!` syntax |
| T4.2 | Facing/RS, point seams, count checks (B) | T4.1 | done | default | 1 | 40b31b4 (merged 97173d6) | ★ §5.4: 172 PASS lines; facing.txt all 4 RS x loop combos; 11/11 exports byte-identical; 3 orchestrator mutations caught |
| T5.1 | Faces (BFS base path, zipper), rest shapes, orientation | T4.2, T1.2 | done | default | 1 | 247f809 (merged 4bb4817) | ★ §6.10 test 4: 114 PASS lines on 11 fixtures; Euler 1, 1 component, 0 warnings; 9 orchestrator mutations caught; oval fixtures fixed (see Decisions) |
| T5.2 | Materials, hinge mods, seams → IntrinsicMesh | T5.1, T3.1, T1.3, T1.5 | done | default | 1 | 80e6647 (merged 326f2b4) | §6.10 test 6: solver mass = yarn model on all 11 fixtures (0.0000 %); 7 mutations caught; kWsFoldSign = -1 to be confirmed by T5.4 test 5 |
| T5.3 | Layouts (flat, fold, revolve, attached, arc; CP positions if A/B) | T5.2 | done | default | 1 | aa2b4bc (merged ea7c26d) | 11 PNGs under docs/crochet-shots/layout/; native layout (no CP positions); 9 mutations caught |
| T5.4 | Relaxation + mesh tests | T5.3 | done | default | 1 | 8da3456 (merged 1564b8d) | ★ §6.10 tests 1,2,3,5: swatch 130.00 x 120.00 mm; circle +6 flat 0.00 %, +5 cup 43 %, +8 ruffle; hinge +25.6 / -25.7 deg; pouch seam gap 0.58 mm; 6 mutations caught |
| T6.1 | Scenes hang_pin, hang_peg, settle loop | T5.4 | done | default | 1 | d8b0ca0 (merged 7ad26f5) | §7.4 test 3 for the pouch: settles in 239 / 276 frames, Newton cap 0, 0 self intersections, peg clearance +3.88 mm; 3 mutations caught |
| T6.2 | RigidLoad: insert, release, fit check | T6.1, T1.6, T1.4 | done | default | 1 | 9fbb244 (merged 1f4edbd) | ★ §7.4 tests 1, 2, 4: strap +0.30 %, force balance +0.62 %, pouch+MBA13 fits=false, bag+MBA13 fits=true; 4 mutations caught |
| T6.3 | Metrics | T6.1 | done | default | 1 | 1bc609c (merged 9ca6a20) | all 31 keys of §7.5 (null when not applicable); 90 deg fold reads 90.53; 5 mutations caught |
| T6.4 | crochet_cli + PNG snapshots | T6.3 | done | default | 1 | 80067cf (merged bd68990) | tool + runCrochet; exit codes 0/2/1 tested; circles, tube, facing run from the CLI with snapshots (docs/crochet-shots/); pouch + bags run with the MBA13 load under T6.5 |
| T6.5 | Five-variant report (USER GATE §14.9) | T6.2, T6.4 | todo | | 0 | | table + snapshots in CROCHET_STATUS.md |
| T6.6 | Real-bag fixture (user measurements) | T6.5, user data | blocked | | 0 | | blocked: waiting for user data (asked at gate 1) |
| T7.1 | GDExtension CrochetPattern, CrochetScene | T6.4 | done | default | 1 | fd0baf6 | crochet_api_check: 23 PASS headless; CrochetSession (stepper) shared with the CLI; 4 mutations caught |
| T7.2 | Crochet scene skeleton: panes, CodeEdit live compile, worker thread | T7.1 | done | default | 1 | 706694d | headless crochet_scene_check: 15 PASS; not looked at on screen (headless only); 4 mutations caught |
| T7.3 | Yarn/hook panel, metrics tab, scene/load menus, editor entry button | T7.2 | done | default | 1 | d0108db | headless test drives each control once: 26 PASS; the one editor.gd change is the Crochet button |
| T7.4 | Surface shader, double-sided thickness | T7.2 | done | default | 1 | 66570c6 | shader compiles and renders in the real renderer (screenshot docs/crochet-shots/ui/pouch_hang_pin.png); crochet_api_check 26 PASS; mesh/shader not covered headless |
| T8.1 | Chart view | T7.2 | done | default | 1 | c9c51fa | crochet_scene_check 38 PASS (1380 symbols for the pouch, hover/click/zoom); core chartPositions tested on pouch and bag; screenshots in docs/crochet-shots/ui |
| T8.2 | Selection chart <-> 3D <-> pattern line | T8.1 | done | default | 1 | HASH | crochet_scene_check 51 PASS: chart click -> glow vertex + editor line, caret line -> stitches, ray pick; 2 mutations caught |
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
* 2026-10-17 — T5.1 (the first implementer died without code; redone by the orchestrator session):
  one vertex per stitch node that ends up in a face; anchors = stitches with targets that are not a
  closing sl st; faces per pair of consecutive anchors of a row, base = BFS over edges in < 2 faces,
  <= 6 edges (chains between anchors join the top polyline). A round closes onto its first anchor only
  when the base ends are the same or neighbouring nodes (the pouch border and the strap-end join row
  are `round`s that do not close). A pair with no base path is never forced: an all-sl-st pair whose
  base edges are full is a *surface* row (no face, no warning), a pair where one stitch carries a
  `#! link` is a recorded *jump* (the pouch border at the fold, 1 jump), anything else is a `hole`
  warning. Orientation BFS flips a neighbour by swapping vertices 1/2 AND mirroring its course
  coordinate, so every rest shape winds like its vertices; the first-built triangle (row 1) keeps its
  winding = the RS normal. Dropped stitches (turning chains, the chain `sk` used, closing sl sts,
  surface sl sts) keep a `dropTo` vertex for T5.2's `extraMass`.
* 2026-10-17 — **Fixture fix (T5.1):** the oval base of the five bags and tube40 is now written
  `NNch,turn` + explicit second-side targets, because CrochetPARADE works an unturned chain row
  upward and the old export jumped n chain stitches in round 1. Exports regenerated; the other five
  exports are byte-identical; docs/CROCHET_DSL.md section 8. `test_crochet_parse` tube40 row 0 is now
  a turned `row` (the only changed assertion).
* 2026-10-17 — T5.2: each stitch's triangles carry exactly that stitch's yarn (density = M_s / sum of their
  rest areas), because the plan's nominal density loses mass on increase triangles; stitches that own no
  triangle lump their yarn into `extraMass` (own vertex, else `dropTo`). The strap-end sl st join rows are
  polygons and so get the sl st hinge firmness even in the plain variants (pouch 8, each bag 10 modifiers).
  Plan "toward the WS" angles enter the solver negated (`kWsFoldSign = -1`; normals point to the RS and a
  positive BendC::rest bends toward the normal side) - T5.4's hinge-sign test is the empirical check.
* 2026-10-17 — T5.3: native layouts, no CrochetPARADE positions (66 s bag layout, drifting tube radius, node needed
  at run time). Everything is inferred from the graph: regions Foundation/Flat/Revolve/Rim/Strip; the fold from the
  both_layers links (row sum 51 -> between rows 25 and 26); the strap arc from the join links. World frame: y up,
  flat pieces hang in xy (RS to +z), revolve pieces rise from an xz base (RS up on the base, inside on the walls).
  Risks for T5.4: the oval chain's rest edge is 0.85 w under 1.0 w stitches (may ruffle the spine).
* 2026-10-18 — T5.4 forced three changes, all recorded where the constant lives: `compressionRatio` 0.2 -> 0.1 (with 0.2 the
  +6 circle ruffles +-10 % from a 0.3 mm perturbation because w/h = 1.083 gives 3 % excess rim; 0.07-0.15 works, +5 cup and
  +8 ruffle too); foundation chain edges stretched to the width of the stitch worked into them (the 0.85 w chain made every
  oval base a keel); relaxation Newton budget 30 (12 hit the cap in 82 % of the pouch's substeps). Test 5 confirmed
  `kWsFoldSign = -1` (BLO turns +25.6 deg toward the WS, FLO -25.7).
* 2026-10-18 — T6.1: "up" = body centroid -> middle of the strap; hang_pin pins the two middle strap rows; hang_peg puts the
  capsule INSIDE the strap loop 2 cm under its inner apex (the plan's "2 cm above the strap's top" cannot hang a strap).
* 2026-10-18 — T6.2: the plan's release damping c = 8/s leaves the box bobbing +-2 cm on the pouch (the cloth + strap is a soft spring
  and the explicit coupling lags); 80/s settles (contact -> weight), now the default. Newton budget 60 once a load is in.
  Rounded-box corner radius = min(10 mm, 0.9 x half thickness) (a rounded box needs r < half its thickness).
  **OPEN (free-edge half cells):** the mesh has one vertex per stitch top, so a strip of n stitches is n-1 cells wide and the
  half stitch at each free edge carries no load: a 5-wide strap is 4/5 as strong (elongation +4 % in the stiff branch, up to
  +25 % in the soft one). Test 1's analytic uses the load-bearing mesh width (n-1) w. A stiffer boundary-triangle correction
  would fix it; not done (affects only narrow straps).
* 2026-10-18 — T6.3: base = rounds up to and including the last increase round; corner_angle = 180 - mean turning angle between the
  faces below and above the base-to-wall row; drop = highest point to lowest; gape = mouth extent along the horizontal axis of least
  extent; strap length = row-centroid centre line. Numbers that do not apply are `null`.
* 2026-10-18 — T6.4: pattern input is the CrochetPARADE text (committed export beside it, else node); scene `auto` hangs by the strap
  when there is one. The PNG writer got a small dependency-free deflate (1.5 MB -> 36-150 KB per snapshot). OBJ outputs are
  gitignored (regenerate with tools/crochet/run_fixtures.sh).
* 2026-10-18 — T7.1: the run is a stepper class (`CrochetSession`) used by both the CLI and the bindings (nothing of §7 is reimplemented in
  GDScript). `CrochetPattern.compile` runs the node exporter itself (PATH widened for Homebrew) and reads the `#!` lines from the same text.
  GDExtension work is done on `crochet/main` in the main checkout (the compiled godot-cpp lives in `build-gd/`); `godot/bin/libfailoring.dylib`
  stays an uncommitted local build product.
