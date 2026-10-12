# Body model - design note

Status: decided (MakeHuman CC0 assets + Failoring's own code; the app is licensed AGPL-3.0). Written before the
implementation; the measured results are in ARCHITECTURE.md ("Body"). Numbers marked *est.* were estimates.

## Problem

The mannequin is ~45 ellipsoids and capsules driven by five measurements. Cloth drapes on that union, and the
display is the same primitives shrunk by 4 mm. Fit is the point of the product, so the body has to look like a
person (or a good dress form), follow measurement sliders with plausible proportions, be the surface the cloth
collides with, regenerate in about 100 ms, and ship inside the single offline HTML file.

## Options

| | Source | Licence (checked) | Realism | Parametric | Data added | Regeneration |
|---|---|---|---|---|---|---|
| A | SMPL / SMPL-X / STAR (MPI statistical shape models) | **Non-commercial research licences.** Commercial use needs a paid licence (Meshcapade). | very high | PCA shape space; measurements need a regressor | ~1 MB quantised | fast |
| B | MB-Lab / ManuelbastioniLAB | **AGPL-3.0**, code and base data | high | yes | - | - |
| C | **MakeHuman 1.x assets**: base mesh hm08 + targets + modifiers + skeleton/weights | **CC0 1.0** for the assets since Sept 2020 (repo `LICENSE.md` section C lists "base mesh and proxies, targets and modifiers"; every `.obj`/`.target` header and `default_weights.mhw` repeat it). The application *code* is **AGPL-3.0** and is not used. | high: realistic adult anatomy, hands, feet, head | blend shapes, including 20 measurement targets (bust, underbust, waist, hips, neck, thigh, upper arm, shoulder distance, nape-to-waist, upper/lower arm length, upper/lower leg height, ...) plus weight / muscle / breast / height macros | ~240 KB deflated for the female subset *(measured)* | blend + fit ~10-30 ms in JS *(est.)*; collider bake ~100-150 ms in the worker *(est.)* |
| D | Procedural mannequin written from scratch (lofted sections + smooth implicit union) | own code | dress-form look; head, hands, feet are the risk | exact girths by construction | ~0 | 50-100 ms in wasm *(est.)* |
| E | Scan datasets (CAESAR, FAUST, ...) | commercial / non-commercial licences | high, but single subjects | no | large | - |

ANSUR II (US Army anthropometric survey, public data) would be a good source for regressing secondary
measurements from the primary ones, but its download host is outside this environment's egress allow-list, so it
is not used now.

## What a mesh body means for the solver

* **Collider: a new signed-distance grid.** The body is a closed triangle mesh (hm08's body group is a closed,
  consistently oriented 2-manifold). The worker bakes it into a two-level signed distance grid: sparse fine
  bricks in a band around the surface (exact point-triangle distances, closest-triangle propagation), a dense
  coarse level for deep-inside and far-outside queries. The sign comes from angle-weighted pseudo-normals of the
  closest feature, which stays correct where the mesh touches itself (thighs of a full figure). Lookup is a
  quadratic B-spline (27 taps), so value and gradient are continuous (C1), as the projected line search needs.
  The grid is generic, so any closed mesh works (scans, the Inigo test case).
* **Display = collision surface.** The rendered mesh is the mesh the grid was baked from; the remaining
  deviation is the grid smoothing error (about h^2/8 times the mean curvature), which is measured. The old
  4 mm display shrink existed because the low-poly primitive meshes were several mm off the analytic surfaces.
  What remains is the cloth's own chord error (a flat 4 cm cloth triangle on a 10 cm radius hip sags 2-3 mm
  between its vertices, more than the 1.65 mm contact offset); that is solved on the cloth side by lifting
  contact vertices for display by exactly the sag the collider's normals predict.
* **Cost per frame.** One grid lookup is 27 taps. The union of 45 analytic primitives costs more, so the
  collider should be no slower. Cholesky still dominates.

## What it means for wrapping (`FL.profile` / `FL.makeWrap`)

The body generator also emits a **section table**: every 1 cm of height, the convex hull of the trunk slice
(both legs together below the crotch, arms and hands excluded via skeleton weights), stored as polar radii around
the body axis. `makeWrap` then wraps each piece around the envelope of the sections it spans, plus clearance,
instead of around an ellipse. This keeps arc length = piece u, as before. Front is +z (theta = 0), back is
theta = 180 deg, as today. The pattern-file format does not change. New measurements are optional fields that
default to "auto".

## Parameters

Sliders: height, bust, waist, hip, shoulder width (always explicit); arm length, inseam, neck, thigh, upper arm,
back length (default "auto" = whatever the body type gives, becomes explicit when dragged, and can be reset).
Body type: weight follows the requested girths, muscle and bust shape come from the presets (petite, average,
tall, curvy, athletic, plus) and can be changed. A short quasi-Newton loop solves the measurement-target weights so
that each tape-measure girth or length hits its slider value within about 1 mm. Definitions: bust = horizontal
girth at the bust points (convex hull); waist = horizontal girth at the natural waist level; hip = horizontal
girth at the fullest level below the waist; neck = girth at the neck base (plane through cervicale and jugular
notch); thigh = girth just below the crotch; upper arm = girth halfway between shoulder and elbow, perpendicular
to the arm; arm length = shoulder point - elbow - wrist; inseam = crotch height above the floor; back length =
cervicale to waist level along the back; shoulder width = distance between the acromion points.

## Decision

**Option C: the MakeHuman CC0 assets, with Failoring's own code.** It is realistic, free to bundle (CC0), and its
measurement targets map one-to-one onto the requested sliders. The app as a whole is AGPL-3.0, so MakeHuman's
AGPL code could be used too, but nothing here needs it.

1. Only CC0 assets are bundled (base mesh body group, a female subset of the targets, skeleton weights for part
   labels). Blending, fitting and conversion are written fresh.
2. An offline converter (`tools/convert_makehuman.py`) turns the MakeHuman files into one compact binary embedded
   in `failoring.html`. `THIRD_PARTY.md` records source, commit, licence and copyright holders.
3. Mannequin finish: nipple detail is removed with MakeHuman's own corrective targets plus local smoothing, and
   the crotch is smoothed, giving a neutral display-mannequin surface. The face stays sculpted but untextured.

Fallback if MakeHuman-derived data is ever unwanted: option D. It would use the same grid collider, section
table and UI, so only the generator differs.
