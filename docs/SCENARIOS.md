# Showcase: scenarios, pre-rendering and playback (Godot editor)

The Showcase tab of the Godot editor (`godot/`) plays body motions with the garment on: Catwalk, Idle breathing,
Arms up to A-pose, Pick up a box, Sit on a chair. A render runs the cloth simulation for the whole scenario in the
background and is cached; playing a cached render replays its frames with no solver. This document is the format,
the runtime rules, and how the Sew button should be wired to it (not done yet).

Spec: `docs/EDITOR_V2_SPEC.md`, section Modes, item 3 (Showcase). The walking playback of the editor (Walking motion,
Play on the timeline) is unchanged and still there.

## Files

| file | what |
|---|---|
| `tools/export_scenarios.js` | writes `godot/assets/scenarios.json` (the scenarios, from the browser's FL.Gait and FL.Body, plus IK) and checks it |
| `godot/assets/scenarios.json` | the data (371 KB, committed; generated from the `average` preset) |
| `godot/scenarios.gd` | `ShowcaseScenarios`: loader, sampling, pose evaluation (`Pose`), the carried box, chair parts |
| `godot/prerender.gd` | `ShowcasePrerender`: the render (one thread), progress and cancel, cache key, cache file |
| `godot/scenario_panel.gd` | `ShowcasePanel`: the tab (dropdowns, Render, Cancel, renders list, Play, Pause, timeline, speed, loop) |
| `godot/editor.gd` | hooks only: the tab in the list, `_showcase`, `drive()` in `_physics_process`, `shutdown()` in `_exit_tree`, `showcase_inputs()` |
| `gdextension/src/failoring_sim.{h,cpp}` | box props (`add_box_prop`, `set_box_prop`, `clear_box_props`), skinning (`set_skin`, `skin`), float16 (`encode_half`, `decode_half`) |
| `godot/tests/scenario_check.*` | 31 checks of the library, the pose, the floor, the box and chair, the native props, the render (box, chair) and the cache |
| `godot/tests/showcase_check.*` | the editor with the Showcase tab: render, cache, playback, solver not stepped, scrub, back to editing |

## Scenario data (`godot/assets/scenarios.json`)

    { "format": "failoring-scenarios", "version": 1, "reference": "average", "bones": [23 joint names],
      "store_fps": 60, "scenarios": [
        { "id", "name", "duration" (s), "frames", "fps" (60), "loop",
          "props": [ {"type": "box", "id", "half": [x,y,z], "center": [x,y,z] (on the floor), "attach": [grip_s, release_s], "grip_offset"} ]
                  | {"type": "chair", "id", "parts": [ {"half", "center"} ... ] (static boxes) } ],
          "rot": [frames][3 * 23 floats]   per bone [rx, ry, rz] in the bone order (FL.Body.fk), radians,
          "root": [frames][3]              pelvis translation before the floor shift, metres,
          "checks": {...}, "reference": {"frame", "joints"} } ] }

Sample k is the pose at time k / 60 s. The player interpolates linearly between samples (looping scenarios wrap).

| id | duration | loop | what |
|---|---|---|---|
| `catwalk` ("Walk on the spot") | 1.067 s (one gait cycle) | yes | FL.Gait.rotations at 1.2 x the walk's stride and arms 1.15, pelvis swing 1.4 x, root sway 3 cm; on the spot |
| `runway` ("Catwalk (down the runway)", first in the list) | 4.08 s | no | the catwalk gait walking 3.2 m down the runway (z -0.45 to 2.75): stand, ease in, walk, ease out, stand. The body glides at the gait's mean speed (1.62 m/s at full stride, 1.42-1.79 m/s with the IK's give); the feet are IK targets, a foot on the ground held where it touched down (export check: the contact point slides at most 2 mm per frame; 1.8 mm). Render mode follows it with the camera, from the side |
| `idle` | 4 s | yes | standing; spine, neck and clavicles breathe (under 1 degree) |
| `dressing` | 6 s | no | shoulders: 42 deg from vertical (A-pose), arms up (170), T (90), back to A |
| `box` | 7 s | no | IK: reach to a box on the floor (0.34 x 0.30 x 0.24 m, back face 0.24 m in front of the feet), grip at 1.8 s, lift to the chest (2.2 to 3.4 s), hold, lower (4.2 to 5.4 s), release at 5.8 s, stand |
| `chair` | 7 s | no | walk in place (stride eases to 0 by 1.6 s), IK: hip joints to 0.60 m high and 0.07 m back, feet and soles held, then stand up. Seat top 0.46 m, under the hips |

### The IK (`export_scenarios.js`)

Levenberg-Marquardt over the browser's FK, 28 parameters (root translation, pelvis, spine, legs, shoulders, elbows),
warm-started frame to frame. Residuals: both ankles and toes (and the sole's lowest and forward-most vertex of each foot)
at their standing positions, hands on the grip (weight 3), hip joints on the seat, and a weak pull to the stand. Joint
limits are clamped. The solve is staged by the warm start: the box reach is continuous over 1.2 s, so it converges.

The exporter re-evaluates every stored frame the way the player does and fails unless all of these hold:

| check | limit | box | chair |
|---|---|---|---|
| feet off their standing height (IK part, before the floor shift) | 1.5 cm | 0.0 mm | 0.3 mm |
| body below the floor after the shift | -0.1 mm | 0 | 0 |
| hands from the box surface while carried | 1.2 cm | 0.01 mm | - |
| hand IK residual (during transitions) | 2.0 cm | 0.2 mm | - |
| feet inside the box at rest | 5 mm | 4.7 cm clear | - |

Run it with `node tools/export_scenarios.js` (writes `godot/assets/scenarios.json`; `--out` for another file; it prints
the report and exits 1 on a failed check). The output is deterministic (two runs are byte-identical). About 60 s.

## Runtime pose (`ShowcaseScenarios.Pose.evaluate`)

For any mannequin (the same `M` and body as the editor):

1. `BodyGen.fk(M, body, rot, root)`: the bone matrices, as `FL.Body.fk`.
2. `FailoringSim.skin(bone_mats)`: linear blend skinning of the body's A-pose vertices (`body.allPos`), with the skin
   weights of `M` (C++, under 1 ms).
3. The floor shift: the lowest foot vertex goes to y = 0 (the same rule as `FL.Gait.pose`), added to every vertex and joint.

So the feet are planted for any body (the scenario's pelvis height is only a starting point). The pelvis translation in
the data is before the shift.

### Props

- **Box.** Before the grip it rests where the data puts it (the floor). From the grip to the release it moves with the
  midpoint of the two wrist joints: the offset is taken once, at the grip, so the box is held exactly where it was
  picked up. After the release it stays where it was put down. The box's size is the data's `half`. The check
  (`scenario_check`) measures the wrists' distance to the box surface on the grip to the release: 0.006 mm on the
  average body.
- **Chair.** Static boxes (seat, four legs, back), the same at every time.
- Props are colliders of the solver (`FailoringSim.add_box_prop`). A moved prop passes its rigid motion to the cloth
  within 2 cm of it for the next step, so the friction sees the motion (the body's motion elsewhere is unchanged).

## The render (`ShowcasePrerender`)

`start(job)` runs `work(job)` on a thread. `job = {inputs, scenario}`; `inputs` is what `editor.gd showcase_inputs(qkey)`
returns (the plan and seams of the last build, copied; the fabric; the body; the quality; the collision flag).

1. Build the garment exactly as the editor does (`PlanLoader.build`) on the standing body.
2. Settle: the quality's settle frames (48 at Preview), gravity ramped in over the ramp frames. This is the Sew
   settle. The render settles its own copy of the garment; it does not reuse the editor's sim.
3. Props: added to the sim.
4. Lead-in: 0.5 s, the body moves from the standing body to the scenario's first pose (blend of the vertices).
5. The scenario: one solver step per 1/60 s. Before each step: the body's vertices (`set_body_positions`, the body
   collision and the friction motion), the moved box (`set_box_prop`). After it: the display positions and normals.
6. Progress (settle 25 %, run 75 %) and cancel (checked each step; the result is dropped).

Timing (2 vCPU, Draft): catwalk, 94 steps, 12 s. Preview is slower (roughly 2x). Not measured on the Mac.

## The cache

- Key: sha256 of JSON `{v, plan, fabric, quality, body (the measurements), collide, seam stiffness, ramp, scenario id,
  duration, frames}`. Any edit to the garment or body changes it.
- File: `user://showcase/<key>.bin`. Layout: `"FLSHOW01"`, u32 header length, header JSON (format, version, key, scenario,
  quality, frames, vertices, triangles, lead, fps, created), int32 triangle indices, then per frame: positions float16
  (3 per display vertex), normals float16, box centre float32. Little endian.
- Precision: float16 positions. Half a unit in the last place is 0.5 mm for coordinates in [1, 2) m; the test's worst
  error over a render's frame was 0.49 mm.
- Memory: a render's frames stay in RAM for the session (`_mem`), as float32 (not measured; an estimate from the display
  vertex count is tens of megabytes for a 7 s render). Renders on disk are listed from their headers only.
- Versions: `ShowcasePrerender.VERSION` and the file's version. Bump them when the solver setup or the data change.

## Playback (`ShowcasePanel`)

- Play replays the cached frames: cloth through the editor's own `_set_cloth_mesh` (same shading, same topology: the
  render's key equals the current inputs, so the display indices match), body through `body_node.show_pose` (the pose
  is evaluated per frame from the data: 0.7 ms on this host, `scenario_check` prints it), boxes as `BoxMesh` nodes under
  the editor's stage.
- While the view is on, the editor skips its own display (`drive()` returns true, `_physics_process` returns early),
  so the cloth and body are not overwritten. The orbit camera is updated before that return and keeps working.
- Speed 0.25x to 2x, loop, timeline (scrub from the lead-in to the end), Pause. Back to editing restores the editor's
  display (`_show_current`). While the showcase view is on, the editor's own timeline Play is suspended (its display is
  not drawn); leave the showcase view first.
- The playhead is in scenario seconds; a render plays only for the garment, body and quality it was made from. Other
  renders are listed and need a render to play.

## Wiring the Sew button (for the next agent)

Not done here (the spec: "a later agent wires the Sew button"). The hooks are:

1. Sew: when the garment is settled (the editor's frames reach the settle target on the stand body), call
   `_showcase.request_render(scenario_id, quality_key)`. This is a new method in `scenario_panel.gd` that does what
   the Render button does (`_on_render`, with the scenario and quality passed in, and the status).
2. Avoid the second settle: the render's settle (step 2 above) repeats the Sew. To reuse the Sew result, put the
   settled positions into the job (`sim.get_positions()` of the editor's sim, after the settle) and have
   `ShowcasePrerender.work` call `sim.set_positions(...)` after the build instead of settling. The job format is
   `{inputs, scenario, settled_positions}`; without it the render settles itself, which gives the same state.
3. The Sew result's playback: when the render is done, the editor can offer Play on the Showcase tab (already: the
   list shows it).
4. Resuming to edit: `_exit_view()` restores the editor's display; the editor's walking playback is not used by the
   Showcase.

Walking is no longer the continuation of the settle step: the timeline stages and the walking playback are untouched
and still work.

## Limits

- The walk-in of the chair is in place (the walking mannequin does not translate); the chair is where it is.
- The box and chair are placed for the average body. For other bodies the IK rotations are the same, the feet are
  planted by the floor shift, and the box follows the wrists (so it is still held), but the grip height and the seat
  height move by the body's proportions (a few centimetres).
- The body is not a collider of the props (the props are fixed to the scenario, the body is not checked against them).
  The chair's back and seat may touch the body by a centimetre or two; not checked.
- The props' friction is an approximation (rigid motion within 2 cm); the cloth's contact with the body's motion is as
  before.
- Renders are per garment and body: any edit makes the old render a stale cache entry (listed, not playable).
- Only the average preset is exported as the reference; `--preset` exists but the IK targets were tuned for average.

## What was verified and what was not

Verified (headless, Godot 4.3 stable, Linux x86_64, dummy renderer):

- `scenario_check`: 31 checks pass (library; native skinning against GDScript (0 m); the exporter's joints (0.007 mm);
  no vertex below the floor and feet on it, all five scenarios, every 1/60 s; the chair's feet planted (0.3 mm); the box's
  hands within 1.2 cm on the grip to release (0.006 mm); the native box rests the cloth and carries it (cloth rises
  from 0.31 to 0.72 m with the box); float16 round trip; a short render's frames, header, cache file and listing).
  It also prints the pose cost: 0.7 ms per frame.
- `showcase_check`: the editor with the Showcase tab. It renders the catwalk at Draft for the booted garment (12 s),
  keeps it in memory and on disk, plays it (the playhead advances, the editor's sim is not stepped), scrubs, pauses and
  hands the view back. It renders the box scenario at Draft (54 s): the box is lifted to 1.00 m and put back at 0.15 m,
  the cloth stays finite. It renders the catwalk at Preview (23 s, the panel's default). Its renders are deleted at the end.
  The shutdown messages "Parameter m is null" appear after the result; the editor alone (no showcase) shows them too.
- Existing tests, same branch, all exit 0: api_smoke (23 PASS), body_check, camera_check, draft_check, draft_edit_check,
  editor_edit_check (47 ok), edit_check, outfit_check (33 ok), render_check (`--script`), step_thread_check (STEP OK),
  wrap_check. skirt_cover_check: exit 0 (8 cases: the four penelope cases are gated, the four pencil cases are reported
  as known; it takes about 10 minutes here).
- The native library: rebuilt with `tools/build_gdext.sh` in this worktree (godot-cpp from a sibling checkout).

Not verified:

- The look of the playback and the orbit camera during playback (no GPU; not seen).
- Playback speed (the frames per second) on a real machine. The pose is 0.7 ms per frame here; the cloth mesh update and
  the drawing were not timed.
- The Mac build (`libfailoring.dylib`): `_Float16` needs clang 15+ on arm64 and x86; not compiled there.
- The Preview and Standard renders (only Draft and the test's 8-step run were executed).
- The Sew wiring (not built).
