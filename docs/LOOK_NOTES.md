# Look: the studio, the skin and the eyes (Godot editor)

What the 3D view is made of since 2026-10-15, and why. Look only: none of this reaches the solver, the collision body
or a test number. Files: `godot/studio.gd`, `godot/skin.gd`, `godot/eyes.gd`, `godot/look.gd`, `godot/shaders/skin.gdshader`,
`eye.gdshader`, `studio_sky.gdshader`, `cloth.gdshader`; checks `godot/tests/look_check.gd` (headless) and
`godot/tests/look_shot.gd` (renders, needs a window).

## The studio (`studio.gd`)

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

* `godot --headless --path godot --script res://tests/look_check.gd`: 33 checks (attribute sizes and ranges, where the
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
