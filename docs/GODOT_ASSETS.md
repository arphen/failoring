# Godot assets: the mannequin and its patterns

The native Godot app (`godot/`, Godot 4.3, GDExtension solver) needs the same mannequin the browser app uses. This
document describes the files that `tools/export_godot_assets.js` writes from `app/assets/body_mh.bin`, the presets in
`FL.PRESETS` and the FreeSewing raw parts in `app/assets/freesewing/`. The GDScript loader is `godot/scripts/body_loader.gd`.

## Generate

    node tools/export_godot_assets.js                 # all six presets -> godot/assets/<preset>/
    node tools/export_godot_assets.js average         # one preset
    node tools/export_godot_assets.js --frames 32 --out /some/dir average

Each run exports, then reads every file back from disk and checks it (vertex and face counts, bounds, round trip of the
mesh, skin sums, bone topology, every walk frame against a fresh `FL.Gait.pose`, feet on the floor, plan contents). It
exits non-zero on any failure. About 7 s per preset.

Only `godot/assets/average/` is committed (about 3 MB). The other presets are generated on demand.

## Units and axes

Same as the browser app: metres, y up, the body faces +z, +x is the body's left. Feet are on y = 0. Godot's forward is
-z, so the front of the body points away from a default camera. To see the front from the default camera, rotate the
node by 180 degrees about y, or view it from +z.

## Files per preset (`godot/assets/<preset>/`)

Sizes for `average` at 24 frames: total 3.08 MB (the six presets are about the same size).

| file | size | content |
|---|---|---|
| `body.obj` | 766 KB | standing mesh: 13380 vertices, 26756 triangles (`FL.Gait.stand`, the collider's mesh) |
| `skeleton.json` | 269 KB | 23 bones, 29 joints, top-4 skin weights per vertex |
| `walk.bin` | 2039 KB | one gait cycle, 24 frames, plus the bind pose |
| `plan_teagan.json` | 18 KB | FreeSewing t-shirt plan (4 pieces, 72 seams) |
| `plan_penelope.json` | 25 KB | FreeSewing pencil skirt plan (4 pieces, 113 seams) |
| `plan_charlie.json` | 35 KB | FreeSewing chinos plan (5 pieces: two front legs, two back legs, waistband; 186 seams) |

### body.obj

Wavefront OBJ. `v x y z` lines with 4 decimals (0.1 mm), then `f a b c` lines with 1-based indices. The vertices are the
first `nb` = 13380 of the asset; the helper vertices past that (joint and landmark helpers) are not exported. Triangles
are the asset's quads split in two, with the same winding as the app. Its vertices are the reference for walk.bin.

### skeleton.json

    {
      "format": "failoring-skeleton", "version": 1, "units": "m", "axes": "...",
      "bones":  [ { "index", "name" (joint name), "parent" (bone index, -1 = root), "rest": [x,y,z], "stand": [x,y,z] } ... 23 ],
      "joints": { "<joint name>": [x,y,z] ... },          // rest (A-pose) positions of all 29 joint helpers
      "skin":   { "vertices": 13380, "influences": 4, "bone_names": [...],
                  "bone_index": [...4*13380 ints],       // bones[] indices, vertex i at 4i..4i+3
                  "weight": [...4*13380 numbers] }       // 3 decimals; renormalise on load
    }

- Bones are topologically ordered (a parent always has a lower index).
- `rest` is the bind pose: the generated A-pose body that the skin weights were fitted to. `stand` is the standing pose
  (the same joints as the standing mesh).
- Skin is linear blend skinning. Bone k rotates about its rest joint, in the parent's frame, as in `FL.Body.fk`. The bind
  vertices are the `bind` section of walk.bin (the A-pose), not body.obj.
- Weights sum to 1 within 0.001 (rounding to 3 decimals).

### walk.bin

    offset 0   8 bytes  "FLWALK01"
    offset 8   u32 LE   header length H
    offset 12  H bytes  header JSON (UTF-8)
    offset 12+H         data (all little endian)

Header (fields used by the loader):

    vertices        13380
    frames          24
    cycle_seconds   1.066667   (64 frames at 60 fps, the app's WALK.cycle)
    fps             22.5       (frames / cycle_seconds)
    sections.bind   { dtype "f32", offset 0,      count 40140 }     // A-pose positions, x0 y0 z0 x1 ...
    sections.frames { dtype "f16", offset 160560, count 963360, frame_bytes 80280 }

- `bind` is the A-pose vertex positions (float32), the rest that skinning starts from.
- `frames` is `frames` blocks of float16 offsets. Frame k is the body at phase k/24 of the left-leg gait cycle (0 = left
  heel strike). The position of vertex i is `body.obj vertex i + offset`. The offset is taken from the standing mesh,
  not the A-pose (see precision below).
- Play it as a loop. Interpolate linearly between frames when playing at the app's speed, since 22.5 fps is below the
  display rate.
- The walk is the app's walk with stride 1 and arms 1 (`FL.Gait.pose`, the default `armAbduct`). It walks in place: the
  root moves only by the small lateral sway and the vertical bob, and the lowest foot vertex is on y = 0 in every frame.

Precision: float16 rounds an offset to half an ulp, which is 0.06 mm below 0.125 m, 0.12 mm in [0.125, 0.25) m and 0.24 mm
in [0.25, 0.5) m. The largest offsets, about 0.49 m (hands and feet during the swing), give the maximum error of about
0.24 mm on the `tall` and `athletic` presets (0.12 mm on the others). The export check allows 3e-4 m.

Offsets are from the standing mesh, the same values as body.obj, so the loader needs only body.obj and walk.bin. The
largest offset is 0.486 m both from the standing mesh and from the A-pose, so the choice does not change the precision.

Why 24 frames: 32 frames gave a 3.67 MB preset, which is over the ~3 MB budget. 24 frames give 3.06 MB. Frames are
interpolated, so the loss is small.

### plan_<garment>.json

The output of `FL.fsimport.plan(raw, stand)` for the preset's standing body, with the raw input from
`app/assets/freesewing/raw_<garment>_<preset>.json` (exported by `tools/freesewing/export.mjs`, MIT, FreeSewing).
Plan format: `{ name, fabric, notes, pieces, seams, info }`, as the browser app's importer produces it. Each piece is
`{ id, name, outline, grain, table, wrap }` and each seam is `{ a, b, reverse }`. `info` has the seam summary and
coverage (debug data). Coordinates are in metres on the standing body's axes.

Garments: `teagan` (t-shirt), `penelope` (pencil skirt), `charlie` (chinos). Each is drafted for each preset.

## Loader (godot/scripts/body_loader.gd)

`FailoringBody` (class_name, extends MeshInstance3D). Set `preset`, and `assets_dir` if the files are not in
`res://assets`. Then `load_preset(name)`. It builds one ArrayMesh surface from body.obj and plays the baked walk in
`_process`. `show_standing()` stops the walk. `parse_obj` and `parse_walk` are static, for use without a node.

UNTESTED. It was written without a Godot binary. Known things to check in the editor:

- `FileAccess.get_file_as_string`, `get_file_as_bytes`, `PackedByteArray.decode_half` and `decode_u32` (Godot 4.3).
- Winding: `flip_winding` assumes the app's triangles are counter-clockwise from outside. If the body looks inside out,
  set it to false.
- Per-frame ArrayMesh rebuild costs about 13k vertices of GDScript work per frame. Fine for a check. Replace with
  RenderingServer vertex updates if it is too slow.
- `.obj` files under `res://` are read at runtime as text. The editor may also create `.import` files for them. This is
  harmless.

## Verification run (this commit)

`node tools/export_godot_assets.js average` passed: 13380 vertices and 26756 faces read back; OBJ round trip maximum
error 0.05 mm (4-decimal rounding); bounds x [-0.245, 0.245], y [0, 1.683], z [-0.115, 0.295] m; feet on the floor
within 1 µm in every walk frame; walk frame maximum error 0.12 mm; skin weight sums within 0.001 of 1; the three plans
present. The other five presets also passed on export, with a maximum walk error of 0.24 mm.

Not verified: the GDScript loader (no Godot binary); the Godot-side winding and orientation.
