# Body model - design note

Status: decided and implemented (MakeHuman CC0 assets + Failoring's own code; the app is licensed AGPL-3.0). The
written design came first; the implemented and measured results are in ARCHITECTURE.md ("Mannequin, collider and
walking"). Numbers marked *est.* are estimates, and those not marked were read from the code or a file header.

## Problem

The first mannequin was ~45 ellipsoids and capsules driven by five measurements. Cloth drapes on that union, and the
display was the same primitives shrunk by 4 mm. Fit is the point of the product, so the body has to look like a
person (or a good dress form), follow measurement sliders with plausible proportions, be the surface the cloth
collides with, regenerate in about 100 ms, and ship inside the single offline HTML file.

## Options

| | Source | Licence (checked) | Realism | Parametric | Data added | Regeneration |
|---|---|---|---|---|---|---|
| A | SMPL / SMPL-X / STAR (MPI statistical shape models) | **Non-commercial research licences.** Commercial use needs a paid licence (Meshcapade). | very high | PCA shape space; measurements need a regressor | ~1 MB quantised | fast |
| B | MB-Lab / ManuelbastioniLAB | **AGPL-3.0**, code and base data | high | yes | - | - |
| C | **MakeHuman 1.x assets**: base mesh + targets + modifiers + skeleton/weights | **CC0 1.0** for the assets since Sept 2020 (the upstream asset licence file, cited in `THIRD_PARTY.md`; every `.obj`/`.target` header and `default_weights.mhw` repeat it). The application *code* is **AGPL-3.0** and is not used. | high: realistic adult anatomy, hands, feet, head | blend shapes, including 12 measurement modifiers behind the sliders (bust, waist, hips, shoulder, neck, thigh, upper arm, arm and leg lengths, nape-to-waist), plus weight / muscle / breast / height macros | **308 KB** asset (308,183 bytes, measured) with 60 targets and 13,380 surface vertices | blend + fit 10-30 ms in JS (comment in `main.js`, not re-timed); collider bake 185-390 ms in wasm (`docs/WIP_HANDOFF.md`) |
| D | Procedural mannequin written from scratch (lofted sections + smooth implicit union) | own code | dress-form look; head, hands, feet are the risk | exact girths by construction | ~0 | 50-100 ms in wasm *(est.)* |
| E | Scan datasets (CAESAR, FAUST, ...) | commercial / non-commercial licences | high, but single subjects | no | large | - |

ANSUR II (US Army anthropometric survey, public data) would be a good source for regressing secondary
measurements from the primary ones, but its download host is outside this environment's egress allow-list, so it
is not used now.

## What a mesh body means for the solver

* **Collider: a signed-distance grid of the mesh.** The body is a closed triangle mesh (the MakeHuman body is a
  closed, consistently oriented surface). The worker bakes it into a two-level signed distance grid: sparse fine
  bricks in a band around the surface (exact point-triangle distances, closest-triangle propagation), a dense
  coarse level for deep-inside and far-outside queries. **The sign comes from winding numbers** (signed ray
  crossings along +x), not from pseudo-normals, so it stays right where the mesh overlaps itself (the thighs of a
  full figure). The magnitude is the distance to the nearest triangle. Lookup is a quadratic B-spline (27 taps), so
  value and gradient are continuous (C1), as the projected line search needs. The grid is generic, so any closed
  mesh works (scans, the Inigo test case).
* **Display = the collision surface.** The mannequin is drawn from the same posed mesh the collider uses. The
  remaining deviation is the grid smoothing error (about h^2/8 times the mean curvature), which is measured in the
  header of `sdf_grid.hpp`. The old 4 mm display shrink existed because the low-poly primitive meshes were several
  mm off the analytic surfaces; it is gone.
* **The cloth is drawn as a subdivided mesh whose midpoints follow the body.** A flat 4 cm cloth triangle on a
  10 cm radius hip sags 2-3 mm between its vertices, more than the contact offset. So the display mesh puts each edge
  midpoint on the body surface (pushed out along the body normal to the clearance of its nearer vertex) and lifts
  only the display triangles that still come too close to the body (at most 4 mm). The body vertices themselves are
  not lifted.
* **Cost per frame.** One grid lookup is 27 taps. The union of 45 analytic primitives costs more, so the
  collider should be no slower. Cholesky still dominates. The grid is re-baked when the body moves (walking), which
  is now the cost to watch (see ARCHITECTURE.md, "Performance ideas").

## What it means for wrapping (`FL.profile` / `FL.makeWrap` / `FL.makeWrapRows`)

The body generator also emits a **section table**: every 1 cm of height, the convex hull of the trunk slice
(both legs together below the crotch, each leg separately below it; arms and hands excluded by their part labels),
stored as polar radii around the body axis. The garment drafts and the row-wise wrap (`makeWrapRows`, modes `fit`,
`leg`, `arc`, `arm`) read the same table, so a piece's arc length is its pattern length. Front is +z (theta = 0),
back is theta = 180 deg. Pieces without a `wrap.mode` still use the envelope wrap (`makeWrap`), which wraps each piece
around the envelope of the sections it spans, plus clearance. The pattern-file format did not change.

## Parameters

Sliders: height, bust, waist, hip, shoulder width (always explicit); arm length, inseam, neck, thigh, upper arm,
back length (default "auto" = whatever the body type gives, becomes explicit when dragged, and can be reset).
Body type: weight follows the requested girths, muscle and bust shape come from the presets (petite, average,
tall, curvy, athletic, plus) and can be changed. A short damped Newton loop solves the measurement-target weights (12
measurement modifiers, finite-difference Jacobian) so that each tape-measure girth or length hits its slider value
within **0.4 mm** (the convergence threshold in `body.js`; the residuals of a given run are reported by
`tools/verify_body.js`, not re-measured for this note). Definitions: bust = horizontal
girth at the bust points (convex hull); waist = horizontal girth at the natural waist level; hip = horizontal
girth at the fullest level below the waist; neck = girth at the neck base (plane through cervicale and jugular
notch); thigh = girth just below the crotch; upper arm = girth halfway between shoulder and elbow, perpendicular
to the arm; arm length = shoulder point - elbow - wrist; inseam = crotch height above the floor; back length =
cervicale to waist level along the back; shoulder width = distance between the acromion points.

## Decision

**Option C: the MakeHuman CC0 assets, with Failoring's own code.** It is realistic, free to bundle (CC0), and its
measurement targets map onto the requested sliders. The app as a whole is AGPL-3.0, so MakeHuman's AGPL code could
be used too, but nothing here needs it.

1. Only CC0 assets are bundled (base mesh, a subset of the targets, skin weights for the posing skeleton and part
   labels). Blending, fitting, conversion, posing and the grid collider are written fresh.
2. An offline converter (`tools/convert_makehuman.py`) turns the MakeHuman files into one compact binary embedded
   in `failoring.html` (`app/assets/body_mh.bin`). `THIRD_PARTY.md` records source, commit, licence and copyright
   holders.
3. Mannequin finish: nipple detail is removed with MakeHuman's own corrective targets plus local smoothing, and
   the crotch is smoothed, giving a neutral display-mannequin surface. The face stays sculpted but untextured.

Fallback if MakeHuman-derived data is ever unwanted: option D. It would use the same grid collider, section
table and UI, so only the generator differs. The app keeps the old primitive mannequin as a fallback when the body
asset is missing.
