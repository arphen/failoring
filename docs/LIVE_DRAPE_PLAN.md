# Live drape: working on the garment on the mannequin (plan, 2026-10-29)

Goal (from the user): work on a piece while it is on the mannequin, the way one works on a dress form. Click a panel to
activate it; see its cut shape small; slide it away from and toward the body and watch it swing to a stop; grab and drag
the cloth; pin it in place or tack it to itself; make it shorter or longer with a stiff "rod" drag; link panels so the
front and back change length together. The cloth runs live on the fast GPU solver at a basic quality while the user works.

## Model

**Live mode** (3D view button "Live", key L) replaces the recorded timeline while it is on: the simulator steps every
drawn frame on the worker thread (same worker, same job/collect path), the body stands still (the pose of the frame the
mode was entered from), and only the newest frame is kept. Leaving live mode returns to the timeline; when the pattern
changed in live mode the garment is rebuilt as after any edit.

Entering from the timeline:

* stage 0-2 (pre-sew, panels hovering or fitted): seams are off (`seam_scale` 0) and each panel is **held** by its top
  edge (the highest vertices of its start pose, pinned and driven). Panels hang and swing. **Sew** ramps the seams in
  over 40 frames and releases the holds; the arms of a dressing pose come down after it as on the timeline.
* stage 3-4 (sewing, drape): seams on, no holds; the cloth continues from the shown frame.

The simulator keeps its quality (Draft is the basic setting; the GPU stepper runs whole frames).

## Tools (palette at the top of the 3D view while a panel is active)

| tool | key | gesture | effect |
|---|---|---|---|
| Grab | G | drag the cloth | the vertices within 3 cm of the hit follow the pointer in the plane facing the camera; release keeps their velocity (a throw) |
| Pin | P | click the cloth | pins the vertices within 1.5 cm where they are (a pin into the dress form); click a pin to remove it |
| Tack | T | click a point, then another | sews the two points together (a point seam, the cloth pulls them together); click a tack's end to remove it |
| Length | Y | drag on the cloth along the panel | stiff rod: the pointer bends a short rod anchored at the press; the bend sets the rate (cm/s) at which the panel grows (drag toward the hem) or shrinks (toward the top) below the press |
| Link | K | click another panel | links the two panels' lengths: a length change on one is applied to the others of the group |
| Distance | wheel with Alt, or the slider | | moves the panel's hold out from or in to the body; the hold follows a spring (critically damped is too dead: damping 0.35), so the cloth swings and settles |

The **mini pattern** (bottom-left of the 3D view) shows the active piece's cut shape at a small scale, its length, the
length line of the Length tool and its change while dragging. A Length drag on the mini pattern works like one on the cloth.

## Length change

Pattern makers' lengthen/shorten line: the panel is cut across at a height and the part below is spread or overlapped.
Live, the part below the line is stretched along the panel's length axis: for a pattern point p with s = (p - line) . a > 0
(a the unit length axis in pattern coordinates, pointing to the hem) the rest coordinate becomes p + a * s * (k - 1),
k = (L + d) / L, L the panel's extent below the line, d the change. The mesh keeps its topology; the simulator's rest
shape changes in place (`set_panel_rest_2d`), velocities are kept.

The length axis comes from the start pose: the direction in pattern coordinates along which the world height falls
fastest (least squares of world y over the panel's pattern coordinates), so it is right for any grain or rotation.

On release the change is written to the pattern (one undoable action, "Lengthen Front 3.2 cm"): outline corners below the
line move the same way, corners are inserted where the outline crosses the line (the seam on a split edge is kept, as
with the Split tool). The live simulation already has the new shape, so this edit does not rebuild the garment.

Linked panels: every panel of the group gets the same d at the same world height (the line is mapped through each panel's
own start pose: its pattern point whose world height equals the press point's).

## Persistence

* Tacks are part of the pattern (`plan.tacks`: `[{a: [piece, x, y], b: [piece, x, y]}]`, pattern coordinates), saved and
  undoable; the loader sews the nearest vertices (`add_point_seam`) so the timeline drape has them too.
* Length links are part of the pattern (`p.length_group`, a string shared by the linked pieces).
* Pins, holds and distances are views of this session (a pin into the dress form is not part of a sewing pattern).

## Native additions (cloth_core, gdextension)

* `SolverParams::seamScale` (param `seam_scale`): multiplies the seam ramp; 0 switches every seam off, live.
* `ClothSolver::addPointSeam` after the start and `removePointSeam(a, b)`: the model is rebuilt with the positions and
  velocities kept (`refinalizeLive`).
* `ClothSolver::setRestUVs(verts, uv)`: many rest coordinates, one rebuild, velocities kept.
* `ClothSolver::driveVertices(verts, pos, dt)`: kinematic targets (position set, velocity = displacement / dt).
* GDExtension: `add_point_seam`, `remove_point_seam`, `set_panel_rest_2d(panel, PackedVector2Array)` (pattern coordinates,
  the grain applied as at build), `drive_vertices`, `pin_vertices`.

The GPU stepper re-uploads its model when `modelVersion` changes (pins, seams, rest shapes), and takes the state every frame.

## Phases

1. Native additions + a cloth_core test (point seam added live keeps velocities and pulls; rest change keeps topology).
2. Live loop in the editor (`godot/live_drape.gd`): enter/leave, holds, seam switch, Sew, Grab.
3. Pin, Tack (live, then persisted in the pattern + loader).
4. Distance with spring dynamics.
5. Length tool (rod feel), commit to the pattern, mini pattern.
6. Link and linked length changes.

Checks: one cloth_core test for the native calls; `godot/tests/live_drape_check.tscn` drives the editor headless through
enter, grab, pin, tack, length, link, leave and checks the numbers (no screenshots needed for the logic; one shot of the
palette and mini pattern at the end).

## Decide and keep going

* GPU stepper refuses a frame: it runs on the CPU (same settings); live mode still works, slower.
* A rest-shape change that would flip a triangle (k <= 0.2): clamp k to [0.3, 3] per drag.
* Mapping a world height into a linked panel fails (the height is outside it): use the panel's own fraction of length.
* Editing on the cutting table during live mode rebuilds the garment as before; live mode re-enters from the new start.

## Status (2026-10-29, end of first session)

Phases 1-6 are in (`godot/live_drape.gd`, `godot/tests/live_drape_check.tscn` passes headless; `test_intrinsic` covers
the native calls). Extras found on the way:

* `damping_scale` is live (`ClothSolver::setDampingScale`): a pre-sew entry settles for 45 frames at 25x damping, so the
  panels drop onto their holds instead of swinging off the exploded pose.
* Point seams (tacks) ignore `seam_scale`: they hold while the garment is unsewn (GPU: negative alpha marks them, PF[24]).
* The GPU read-back gathers its six buffers into one (`buffer_copy`): 120 -> ~35 ms.
* Live runs the GPU solve at 5 substeps / CG 50 (845 dispatches instead of 2690).

Open:

* Speed: ~60-75 ms per frame on the M3 with the editor drawing (GPU ~25 ms, read-back ~35-40 ms). The read-back waits
  behind the editor's own rendering. Next: pipeline (submit frame n+1 before reading n) or a shared device.
* A sleeve's "top edge" hold runs along its whole length on a raised arm: it sticks out. Hold sleeves at the cap only.
* Leaving live drape rebuilds from the pattern; the live drape itself is not kept.
