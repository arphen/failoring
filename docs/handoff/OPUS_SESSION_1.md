# Opus session 1 - running log

Branch `opus/session-1`, from main @ 842609f. Machine: Linux container, 2 cores, Release build
(`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2`). Times below are wall / user seconds,
one binary at a time unless stated.

## Phase 0: sanity (2026-10-14)

### Build

Configure and build of main succeed; 0 warnings in the build log.

### Every test binary, run once (main @ 842609f, nothing changed)

CMake registers 9 test binaries (the ARCHITECTURE layout line lists only 5). All pass.

| Binary | Result | Wall s | User s | Key numbers |
| --- | --- | --- | --- | --- |
| test_basics | ok | 0.27 | 0.22 | hinge gradient vs FD 1.02e-10; estimator vs paper 0.57 %; supernodal Cholesky 2.2x, diff 2.8e-16 |
| test_cantilever | ok | 3.75 | 3.63 | hex 7 / 3.5 / 2.5 mm: +22.6 / +1.9 / +0.7 %; grid 3.5 mm +16.1 %; 0 line-search failures; grain ratio 1.86 |
| test_sdf_grid | ok | 9.09 | 8.92 | ellipsoid grid vs analytic within 5 mm: 0.304 mm max; 0 wrong signs of 4000 |
| test_barrier | ok | 0.87 | 0.84 | CCD conservative on all known-TOI cases; BVH 0 missing pairs |
| test_passthrough | ok | 0.13 | 0.13 | vertex-tri: 5 intersecting frames; barrier: 0 |
| test_layers | ok | 6.86 | 6.76 | barrier: min layer gap 0.688 mm, 0 intersecting frames, 56.7 ms/frame, 7.21 Newton/substep |
| test_garment | ok | 40.3 | 39.7 | 672 verts, 153 frames, KE 9.3e-07 J, seam gap 2.35 mm, clearance 1.65 mm, hem 156.7 vs predicted 161.2 mm |
| test_soft_skin | ok | 131.3 | 129.3 | ring delta 2.9325 vs 2.9086 mm (0.82 %); standing soft 0.0001 mm/frame, KE 6.9e-13, ls-fail 0 |
| test_drape | ok | 306.4 | 301.4 | table below |

Notes:
- test_cantilever's 7 mm hex case is +22.6 % (its tolerance is 30 %); ARCHITECTURE's "within 1-2 %" holds for 3.5 and 2.5 mm only.
- test_soft_skin `disabled` part still prints "with the layer on the drop differs by up to 1.66e-01 m" (K1 open item, not
  investigated in this session).
- test_passthrough and test_layers print "lowest B point" / layer gap of -13.7 m and -13.2 m for the vertex-tri mode:
  the sheet falls through and keeps falling; those values are not meaningful (known from the K5 notes).

### test_drape (all nine presets, 10.0 mm mesh each)

| Fabric | DC % | Frames | KE (J) | Clearance mm | s |
| --- | --- | --- | --- | --- | --- |
| cotton_poplin | 65.0 | 300 | 3.6e-07 | 1.65 | 42.0 |
| denim_12oz | 80.7 | 214 | 9.3e-09 | 1.90 | 30.9 |
| linen_medium | 81.2 | 300 | 8.1e-08 | 1.70 | 40.5 |
| wool_suiting | 64.8 | 125 | 8.8e-09 | 1.85 | 17.3 |
| silk_charmeuse | 46.6 | 300 | 2.2e-07 | 1.60 | 39.2 |
| polyester_chiffon | 43.1 | 300 | 1.9e-07 | 1.55 | 38.8 |
| canvas_heavy | 92.8 | 179 | 8.4e-09 | 1.90 | 24.9 |
| cotton_jersey | 27.6 | 223 | 8.9e-09 | 1.80 | 29.8 |
| lambskin_leather | 90.9 | 300 | 5.9e-08 | 1.85 | 42.9 |

Matches the user's numbers exactly. Five presets stop at the 300-frame cap (KE above the 1e-8 J early-stop), so their DC
is the frame-300 value. `docs/ARCHITECTURE.md` updated with this table, the mesh size and the date.

### K1 + K5 together (new test `test_skin_barrier`)

Scene: the test_garment tube (cotton poplin, two panels sewn into a ring, pre-wrapped on the ellipsoid's upper dome)
on a 3 cm mesh (308 verts), 4 substeps, 200 frames. Skin k = 2e5 Pa/m, depth 8 mm (test_soft_skin's standing values,
test values, not tissue data). Barrier as test_garment's `FL_BARRIER` switch (barrierContact on, selfCollision off,
default BarrierParams).

| Mode | s | KE end (J) | ls-fail | Newton/substep | Factorisations | Clearance min (end) mm | Self-isect frames (max pairs) | Seam gap mm | Hem mm |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| rigid | 13.5 | 9.17e-08 | 0 | 7.76 | 6205 | 1.64 (1.65) | 182 (98) | 1.99 | 159.6 |
| skin | 10.4 | 9.73e-11 | 4 | 5.64 | 4515 | -3.76 (0.46) | 178 (49) | 0.34 | 153.0 |
| barrier | 31.4 | 1.51e-05 | 0 | 4.99 | 3995 | 1.65 (1.65) | 0 (0) | 0.29 | 156.7 |
| skin + barrier | 29.5 | 1.93e-10 | 1 | 5.21 | 4169 | -3.74 (0.36) | 0 (0) | 0.32 | 154.2 |

Answer: the combination runs, settles (KE 1.9e-10 J), keeps the seams closed and has no self intersections in any of
the 200 frames. "Penetration-free" needs a qualifier: with the skin on, the cloth sinks up to 3.74 mm into the 8 mm soft
layer during the drop, which is what the layer is for; it never reaches the rigid stop, and at the end it sits 0.36 mm
outside the offset surface. Convergence: 1 line-search failure in 200 frames (800 substeps) with both on; the skin
alone gives 4. Pass limits in the test are test_garment's (KE < 5e-4 J, seam gap < 4 mm, penetration < 1 mm, or
< depth + 1 mm with the skin); no existing test tolerance was touched.

Observations to carry into Phase 1 (not acted on):
- Barrier alone ends with KE 1.5e-05 J, about 160x the rigid mode and 1e5x the skin modes. The skin settles the ring far
  better than anything else here (9.7e-11 J).
- The skin cuts Newton iterations per substep from 7.76 to 5.64.

### Finding: the default self contact lets the tube intersect itself

The rigid and skin-only rows above show self intersections in about 90 % of the frames. Checked with a probe
(`tools/dev/probe_isect.cpp`, counts every non-adjacent intersecting pair and says where it is):
- 3 cm tube: frame 0 has 132 crossings, all across the seams (pre-wrap overlap; the counter skips them as seam-linked).
  From frame 20 most counted pairs are within one panel and up to 120 mm from any seam.
- 2 cm tube = the test_garment scene (default mode, a passing test): 87 counted pairs at frame 150, about 15 of them
  across panels; the rest within a panel, up to 172 mm from a seam.
So this is real, not a counting artefact. Where on the tube the folds are was not looked at (no render). Not fixed; it
is the existing "vertex-triangle only" limitation, now with a number, added to ARCHITECTURE "Known limitations".

### Not done / unverified in Phase 0
- wasm and Godot builds (the K5 sources were added to their build files without building): not attempted.
- ctest as one call: not used; binaries run one by one (above).

## Open questions
- Should `test_garment` assert zero self intersections? It would fail today in default mode (87 pairs). Not changed.
