# Look: the studio, the skin and the eyes (Godot editor)

What the 3D view is made of since 2026-10-15, and why. Look only: none of this reaches the solver, the collision body
or a test number. Files: `godot/studio.gd`, `godot/skin.gd`, `godot/eyes.gd`, `godot/look.gd`, `godot/shaders/skin.gdshader`,
`eye.gdshader`, `studio_sky.gdshader`, `cloth.gdshader`; checks `godot/tests/look_check.gd` (headless) and
`godot/tests/look_shot.gd` (renders, needs a window).

## 2026-10-16: tailor's studio, real lighting, idle life, face (this supersedes the cove below)

Godot 4.3 stable, Forward+ (Vulkan 1.2, MoltenVK on an Apple M3). Checks: `look_check.gd` (41), `idle_check.gd` (headless numbers for
the idle layer), `look_shot.gd` (renders), `look_bench.gd` (frame time per quality level).

**Why shadows fell "behind" the mannequin.** There was no DirectionalLight3D: every lamp was already a `SpotLight3D` at its
fixture, aimed within 4.5 degrees of the chest. Only the key cast a shadow, and the key stands front left, outside a wide
frame from the front; the three fixtures you could see (two strips and the top box, all behind the subject) had
`shadow_enabled = false`, a workaround for an artifact of the old OpenGL/llvmpipe renders. So the one shadow in the picture ran
away from the camera and the visible lamps threw none. Rims and top cast shadows again (towards the camera), the key's runs back
and right, `look_check` asserts every shadow falls away from its lamp.

**The room** (`studio.gd`, `studio_props.gd`, `shaders/studio_surface.gdshader`; no texture files). A 9.6 x 9.6 m plastered room,
3.8 m high, boarded floor, picture rail, skirting, beams, six windows; a 15-20 cm (18 cm) fitting platform whose top is
y = 0, the solver's floor, with a runway and a step; a paper backdrop sweep; cubby shelving of cloth bolts, two garment racks, a
cutting table with mat, tape, shears, spools; two dress forms; a three-panel fitting mirror; pendant lamps. Every surface is a
procedural PBR shader with several scales of variation, wear (traffic round the platform), dust, and fine detail that fades
before it can shimmer. The orbit camera stays inside the room (`Studio.max_orbit_dist`), the Shoot tab's long-lens cameras are
pulled in and their lens widened to keep the framing (`Studio.fit_camera`). The camera is a 50 mm lens (27 degrees vertical),
the framing distance scaled to match (`Look.CAM_DIST_K`).

**Lighting.** The three-point rig (key, V-flat fill, two rims, top) is kept: it already matched the spec. New: warm pendants
(2700 K) and cool window light mix with it; SDFGI (real-time GI: the room is built at run time, so no bake, no lightmap),
SSAO, SSIL (high), SSR (medium and up), glow limited to tight levels, filmic tone mapping (ACES pushed skin orange),
depth of field behind the subject (focus follows the orbit distance), a vignette and film grain (`shaders/post.gdshader`), a light
colour grade (per-channel curve plus contrast and saturation), a reflection probe round the subject.

Lessons: SDFGI occlusion plus coarse cells blotched flat walls (off; cells 0.1 m at high); a reflection probe spread over the whole
room gave the ceiling a wavy reflection and the floor a wet sheen (now a small local probe, no box projection, padded so no face
is coplanar with the floor); **a `transparent_bg` SubViewport made the spot lamps light nothing** once GI and the grade were on
(black mannequin in the editor, fine in `look_shot`): the pane's backdrop trick is gone, the room is closed; TAA cost 11 ms a
frame on top of 4x MSAA and is off.

**Frame time** (M3, window 1000 x 1250 px, vsync off, `look_bench.gd`): low 153 fps, medium 58 fps, high 44 fps; at 2000 x 1832 px
low 58, medium 22, high 17. A full-screen 3D pane on a Retina display is 4-5 Mpx: use low there, or lower `scaling_3d_scale`.
Costs at medium (ms): MSAA 2x ~2.5, glow 2.5, DoF 2, SDFGI 1.5, SSR 1.4.

**Idle life** (`idle.gd`, `tests/idle_check.gd`). Breathing at 14 /min (the rate wanders, a new depth each breath), weight shifts every
5-12 s (pelvis tilt and shift, spine counter-rotation, head follow-through, a two-bone IK keeps both feet planted), head drift,
shoulder and arm settling, wrists, blinks every 3-6 s (a double blink one time in five), saccades and a drifting gaze. The mesh is
not rebuilt: the skin vertex shader skins it again with one small transform per bone (a 24 x 3 float texture), displaces the
ribcage and belly for the breath (CUSTOM3), and swings the lids about the eyeballs for the blink; `eyes.gd` moves the eyes with the
head and the gaze. Parameters (intensity, breaths/min, depth, weight shifts, micro motion, eye motion) are sliders under Body >
Alive. **Limits:** there is no Skeleton3D (the body is CPU-posed, 23 bones, no finger, jaw or eye bones), so no AnimationTree and no
finger relaxation; the cloth is simulated against a still body and played back from cached frames, so with garments on the body
bones stay still (head, eyes and a breath under 2 mm go on), and a walk is baked, so the bones rest while walking.

**Face.** The mouth is a real opening in the mesh with a deep cavity behind the lips (it cannot open: no jaw). The skin shader
now draws it as a gradient from moist pink-red to deep red with occlusion (never black), with a hint of upper teeth painted on the
far wall (not geometry, no tongue). Lips: darker at the lip line and corners, lighter in the middle, wet (sharp oily lobe, rough 0.2),
stronger vertical lines. Lid margins: a lash line along the upper, a moist line along the lower. Skin: a soft sheen at grazing angles
and less gloss on the T-zone; SSS is gentle (0.1, high only: stronger drew a halo on the bright backdrop). Eyes: the iris is seen
through the cornea (refraction), softbox catchlights from the probe and the lamps. **The face mesh is too crude** for close-ups:
the whole body is 13k vertices, nostrils are open slots with creases, ears are pointed, the lids have no thickness and no
lashes. A better base head or a deliberate faceless dress-form look is the real fix (a decision, not a tuning job).

## 2026-10-16 (later): camera views, atelier layout, the lived-in atelier, a real mirror

**Camera** (`cam_views.gd`, `editor.gd` `_update_camera`). The earlier 50 mm change had left the framing distances of the old 36 degree
lens, so the view came out zoomed in on the legs. The camera now fits what it looks at to the pane (`CamViews.fit`: height and width
against the pane's aspect), so any pane shape shows the whole mannequin. A bar of view chips sits along the bottom of the 3D pane:
Fit, Front, 3/4 left, 3/4 right, Left, Right, Back; Face, Torso, Legs; Atelier (the room), Mirror, Table; Lamps (show the
photographer's softboxes and stands, hidden by default). Choosing one glides there (focus, yaw, pitch, lens); any mouse or key input
cancels the glide. New keys over the pane: arrows orbit, Page Up/Down or +/- zoom, F or Home or double-click = Fit.
`FL_VIEW=<id>`, `FL_VIEW2=<id>` (glide after 6 s), `FL_ZOOM`, `FL_FPS=1` are for screenshots and measuring.

**Layout.** A first run on this layout version (and the new **Atelier** button in the pane bar) shows only the 3D view; Library,
Table and Inspector are one click or Ctrl+1..4 away. Reset layout brings back the classic four panes. The saved layout is versioned:
a layout saved before this change is replaced once.

**The atelier** (`studio_atelier.gd`). An oriental rug (procedural: border, medallion, worn wool); wooden chairs; a desk with an
articulated lamp (a real warm spot light on the desk), make-up, a brush cup and a hand mirror smeared with lipstick
(`shaders/smear.gdshader`); a tea corner with a copper kettle that steams (GPU particles) and a cup; three dress forms in half-finished
garments (a blouse and trousers held with safety pins, a tape measure round the neck, customer tags with illegible scribble:
`studio_surface` kind 10); cloth half cut on the cutting table, one edge hanging over; cloth shelving either side of a tall back
window. The softboxes stay as the light and are hidden by default.

**The mirror** (`mirror.gd`, `shaders/mirror.gdshader`). A true planar mirror per panel: a camera at the viewer's eye reflected across the
panel, looking through it with an off-axis frustum that is exactly the panel, rendered to a texture drawn on the glass. The live mannequin,
the garment and the room show with the right parallax; the Mirror view stands in front of it, off-axis, so her back shows in the glass.
It renders only while a panel faces the camera and is in view, and not on low quality (a dark glass then). A reflection probe was tried
first and failed: the glass fell back to the sky's painted softboxes (pure white). `unshaded` materials ignore `EMISSION`: write `ALBEDO`.
**Measured** (M3, 1280 x 720 window, vsync off, the editor): the front, mirror and atelier views all hold the 144 Hz display cap; the
table view 90 fps; the atelier cost the bench (`look_bench.gd`, 1000 x 1250 px) 10 fps at medium (58 to 48): more lights (eleven plus the
desk lamp) and geometry. A fullscreen Retina pane is 4-5 Mpx and slower.

## 2026-10-16 (evening): Render mode, hovering panels, the 3D stitch tool

**Render mode** (`render_mode.gd`; the activities are the Showcase scenarios, `docs/SCENARIOS.md`). A bar at the top right of the 3D view:
an activity (Catwalk, Idle breathing, Arms up, Pick up a box, Sit on a chair), a "..." popup (movie size 720p / 1080p, camera: the current
view or a slow orbit, cloth simulation quality) and **Render**. Pressing it takes the whole window for the 3D view (the panes, the pane
bar and the timeline are hidden; the saved layout is untouched) and runs three steps:
1. the cloth is simulated for the whole activity in the background (the Showcase pre-render, cached by garment, body and activity);
2. every frame is rendered offline at full fidelity (high quality: 4x MSAA, SDFGI, SSR, SSAO, SSIL, SSS, the mirror) at twice the movie's
   size and scaled down, as slowly as that takes (M3: about 5 frames a second, 135 frames of 1280x720 in 31 s; a 7 s activity about
   50 s), stored as JPEG frames and cached in `user://renders/` (the same garment, body, activity, size and camera replay at once);
3. the movie plays: Play/Pause, a scrubber, speed (0.25x to 2x), Loop. The GPU rests while it plays.
**Back to editing** (or Esc) gives back the panes, the chrome, the quality and the camera. While editing, the 3D view stays the real-time
preview at the chosen quality level (medium by default, low on weak hardware). `FL_RENDER_AUTO=<id>`, `FL_RENDER_OUT`, `FL_RENDER_CAM`,
`FL_RENDER_SHOT=<prefix>`, `FL_RENDER_LEAVE=1` drive and log it for tests. Not checked headless (the frames need a window).
Pitfalls found: reading the window's texture inside the physics tick stalled `frame_post_draw` (grab after the draw instead); the idle layer
is stepped once per movie frame (`idle_paused`) so the movie is deterministic.

**Hovering panels** (`editor.gd` `_stage_pose`, `tests/staging_check.gd`). Panels no longer start on the cutting mat. Stage 1 "Arrange" shows
every panel hovering far from the body: its fitted (wrapped) pose pushed 0.95 to 1.2 m radially outward, level with where it belongs, an
exploded view with room to work out the seams (a few connection lines per seam show what joins what, in the sew check's colours). Each
panel is far or close by its own state (a view state of the session, `_stage_of`, default far): the bar's **All far**, **All close** and
**Far / close** (the selected panel, picked by clicking it in the 3D view or on the table) glide the panel there (2.2 per second, with a small
arc). Playing from the start brings every far panel in (stage 2 "Fit"), sews (stage 3), drapes (stage 4). Moving a far panel in Move
panels mode stores the same placement as moving a fitted one (a rigid move or a turn about its own centre commutes with the offset).

**3D stitch tool** (`editor.gd`, `stitching.gd`, `tests/stitch_check.gd`). **Stitch** (S over the 3D view): hover an edge (it lights up), click
it, click the edge to sew it to: the same seam, undo and warnings as the table's Sew tool (`Table.sew_edges`), with the stitch type chosen in
the bar; right-click an edge to unpick the seam that ends there. Seams join panels of one garment; a panel with partial seams must be sewn
on the table (its simulated outline is split, the edge numbers differ). The type is stored in the seam (`seam["stitch"]`, also set in the Seams
tab) and does not reach the solver.
**Needles.** In the Sew stage (timeline 2 to 3) a needle flies along every seam, one after another, drawn 3.2x life size so it reads from a
metre or two, leaving a thread in the type's colour and a fading trace: straight (white, in and out at every pair), zigzag (orange, sweeps across
the seam), overlock (teal, loops over the edge), topstitch (gold, small stitches on one side), basting (red, long hops every fourth pair).
`FL_STITCH_DEMO=1` mixes the types for screenshots. The tee's 68 simulated seams (a FreeSewing seam is cut into short ones) are 5 joins of
two panels, so **one needle per join and stitch type** (5 needles): it sews that join's seams one after the other (nearest next, each from
the end that is closer), the thread grows behind it, the trace breaks across a hop between distant seams, and a needle makes at most 28
stitches over its path however long it is, so it never outruns the screen.

## (superseded) The studio (`studio.gd`)

The mannequin stands in an infinity cove: a floor that curves up into a wall all round (the camera orbits), 11 m radius,
7 m high, matte light grey. The lamps are a fashion set-up with no budget limit, each a real light and a visible fixture:

| lamp | fixture | where (from the default camera) | light |
|---|---|---|---|
| key | 150 cm octabox on a stand | front left, about 35 degrees above the chest | the only shadow caster; soft shadows on Forward+ (`light_size` 0.9 m) |
| fill | white V-flat (two 1.2 x 2.4 m boards) | front right | weak, no shadow: the key bounced back into the shadows |
| rims | two 35 x 170 cm strip boxes | behind, left and right | a bright edge down shoulders, arms and legs |
| top | 60 cm box on a boom | above and behind the head | hair and shoulder light |

Every light is a `SpotLight3D` at its diffuser with inverse-square falloff, so the floor and the walls fall off as in a
real room (the subject is the brightest thing; the far wall is dark). The diffusers glow (emission, with a little bloom),
and the same lamps are painted into the sky (`studio_sky.gdshader`), which gives the ambient light and the reflections:
a highlight on silk, or the catchlight in an eye, is where its softbox is. Stands fade out when the camera comes within a
metre of them. Lighting ratio about 1:2.5 (key to fill), so garments stay readable.

Numbers in the table at the top of `studio.gd`; quality levels in `look.gd` (key shadow 2048 px on low, a 2048 px
quadrant of a 4096 atlas on medium and high).

Lessons from the renders (OpenGL / llvmpipe here):
* a 140 degree key beam spread its shadow map so thin that the shadow edge ran several cm into the lit side; the beam is
  now 45 degrees with a slow falloff to its edge;
* shadows on the rim lights drew dark lines across the front of the body in the OpenGL renderer; only the key casts
  shadows now;
* the OpenGL renderer has no soft-shadow filter for spot lights, so the leg-on-leg shadow is hard there. On Forward+ the
  key's `light_size` gives the penumbra a 1.5 m octabox makes (about 6 cm between the legs). Not seen here.

## The skin (`skin.gd`, `skin.gdshader`)

No photo texture is needed: everything is computed from the MakeHuman base mesh before morphs (the rest shape), so it
stays on the same patch of skin on every preset and in every pose. Per vertex (`skin.gd`, 0.35 s once at start):
* rest position and a cavity measure (concavity at two scales, relative to the local edge length, smoothed: real creases
  such as ear folds, eyelids, nostrils, between fingers; the raw measure followed the coarse limbs' edge loops);
* redness (kneecaps, elbows, hands and fingertips, feet and toes, ears), thin parts (ears, fingers, toes: light passes
  through), areolae (the body's nipple masks), nails;
* MakeHuman's own texture coordinates (`assets/body_uv.json`, `tools/export_body_uv.py`): the drawn mesh splits the
  1137 vertices on UV seams (14517 drawn for 13380 body vertices) and gathers positions and normals from the posed body.

In the shader, in order of what it does against the "plastic" look:
1. scattering under the skin: a slightly soft terminator, reddened on its lit side; light through ears and fingers when
   they are lit from behind; on Forward+ high, Godot's screen-space scattering and transmittance on top;
2. a broken highlight: pores (two scales), fine and mid-scale bumps (more on thighs and seat), all in rest space and
   faded below a pixel; two GGX lobes (a broad one and a sharp, oily one); roughness that varies (oilier T-zone, glossy
   lips and nails, matt hair);
3. colour that is not uniform: mottling, redness, darker creases, a few moles and freckles; lips, eyelids, nails and the
   inside of the mouth from MPFB2's region masks (`assets/skin/regions.png`, CC0, `tools/pack_skin_masks.py`), with the
   rest-space shapes as fallback; brows and a short hair crop drawn per pixel from rest-space landmarks (`skin.gd`).

A first attempt with a per-channel wrapped diffuse (red wrapping furthest round the terminator) drew orange lines along
the limbs wherever a second lamp lifted the shadow: the red is kept on the lit side now.

Skin tones (`look.gd` SKIN_TONES): porcelain, light, medium, olive, tan, brown, deep. Albedos follow measured skin
reflectance, slightly desaturated; `melanin` lowers how much the redness shows.

### Photographic skins

MakeHuman's photo skins (scanned, CC0, painted for the same UV layout) are what the professional avatars in the
reference notes use, and they are the biggest step left. They live on `download.tuxfamily.org`, which this build
machine cannot reach, so they are not in the repo. `tools/fetch_makehuman_skins.sh` downloads four (young and
middle-aged, light and dark, about 4 MB each) into `godot/assets/skin/photo/` (git-ignored); the Body tab then lists
them as "Photo: ...". The photo replaces the drawn colour; the pores, gloss, scattering, brows and hair stay. Checked
here only with a synthetic gradient (the mapping follows the UVs); not seen with a real skin.

## The eyes (`eyes.gd`, `eye.gdshader`)

The MakeHuman body has open sockets behind its lids. Each eye is a sphere of 10.6 mm (the lid margin is at 11-12.7 mm
from the eye centre) with a cornea bulge (a 0.78-radius sphere meeting the eye at the limbus, 38 degrees off the gaze),
placed every frame from the lid-margin vertices and a forehead group (head frame), so it follows the soft tissue, the
walk and every preset. Gaze straight ahead, slightly converged and lowered. The shader draws sclera, iris fibres, a
collarette, the limbal ring and the pupil; lamps give small crisp catchlights (the built-in light-size widening made a
softbox a blot over a third of the iris), the sky reflections give the softboxes' shapes.

## Checks

* `godot --headless --path godot --script res://tests/look_check.gd`: 41 checks (attribute sizes and ranges, where the
  masks land, UV mapping and seams, eyes in the sockets and following a turned head, looks, the studio's lamps, sky
  and cove).
* `xvfb-run godot --path godot --rendering-driver opengl3 --script res://tests/look_shot.gd -- <dir> [preset quality
  skin hair eyes]`: renders full length, face, three-quarter face, back, arm, thigh, forearm, eye and studio shots.
  `FL_SHOTS`, `FL_LIGHTS`, `FL_NOSHADOW`, `FL_SKIN_DEBUG` (1 cavity .. 6 region masks), `FL_SKIN_PARAMS`,
  `FL_SHADOW_BIAS`, `FL_SPHERE` are for look work.

## Not verified

* Forward+ (Vulkan, Metal): every render here was Godot's OpenGL renderer on llvmpipe. Soft shadows, SSAO, the
  screen-space scattering and transmittance were therefore never seen. They are standard Godot features set to
  moderate values.
* Frame time on a GPU. The skin shader evaluates seven value-noise octaves per pixel; the body view gathers 14.5k drawn
  vertices per frame in GDScript (a few ms).
* A real photographic skin (see above).
