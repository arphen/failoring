# Failoring

A tailoring tool, "KSP for sewing": cut pattern pieces on a 2D table, mark seams, and watch the cut cloth drape on a
parametric body with physically based fabric behaviour. C++17 cloth core compiled to WebAssembly; the UI is one
self-contained `failoring.html` (`node app/build.js`).

* Architecture and status: `docs/ARCHITECTURE.md`; body model design: `docs/BODY_DESIGN.md`.
* **Work in progress** (parametric MakeHuman-based mannequin + signed-distance-grid collider): see `docs/WIP_HANDOFF.md`.
  Known: `test_cantilever` currently fails with the new tension-stiffness solver option (details in the handoff).
* Licence: AGPL-3.0 (`LICENSE`); third-party material in `THIRD_PARTY.md`.
