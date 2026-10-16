# Visual setup for the Godot editor, ported from the browser app (app/src/view3d.js for the
# 3D scene, app/src/style.css for the UI). All visual constants live here; editor.gd calls these
# functions at setup time, and cloth_shade.gd builds the per-vertex cloth data. No solver logic here.
extends RefCounted

const CLOTH_SHADER := preload("res://shaders/cloth.gdshader")
const CONTACT_SHADER := preload("res://shaders/contact.gdshader")
const POST_SHADER := preload("res://shaders/post.gdshader")
const Studio = preload("res://studio.gd")

# ---- 3D scene: a tailor's studio (studio.gd builds the room, the props and the lighting rig; the lamps are listed there)
const BG := Color("#0c0d0f")            # behind the room (never seen: the room is closed), and the pane's backdrop
const EXPOSURE := 0.8                   # filmic tone mapping (ACES pushed bright skin to orange where lamps overlap)
const AMBIENT := 0.2                    # diffuse light from the studio sky when the global illumination does the bounce
const AMBIENT_NOGI := 0.5               # ... and when it is off (low quality): a flat fill instead of the bounce
const GLOW := 0.16                      # bloom round the glowing diffusers (a lens sees them as very bright)
const CAM_FOV := 27.0                   # vertical fov: a 50 mm lens on a 24 mm tall sensor (2 atan(12 / 50))
const CAM_DIST_K := 1.353               # the framing distances were set for 36 degrees: 36 / 27 degrees in tangent terms
const CAM_NEAR := 0.05
const CAM_FAR := 40.0
const TABLE_SLAB := Color("#2a4640")    # cutting mat (view3d.js buildMat)
const AO_RADIUS := 0.12                 # m, SSAO sampling radius (body scale)
const SKIN_SHADER := preload("res://shaders/skin.gdshader")
const REGIONS := "res://assets/skin/regions.png"     # MPFB2 region masks (tools/pack_skin_masks.py)

# Skin tones for the mannequin (Body tab): albedo, the blood tint under it, and melanin (how much the redness shows).
# Albedos follow measured skin reflectance (light skin about 0.6 in red, dark skin about 0.1-0.15), slightly desaturated.
const SKIN_TONES := {
	"porcelain": {"label": "Porcelain", "tone": Color("#f0d5c4"), "under": Color("#e0645a"), "melanin": 0.1},
	"light": {"label": "Light", "tone": Color("#e3bea3"), "under": Color("#db5c4c"), "melanin": 0.25},
	"medium": {"label": "Medium", "tone": Color("#c99a78"), "under": Color("#c8503e"), "melanin": 0.45},
	"olive": {"label": "Olive", "tone": Color("#b48a63"), "under": Color("#b8523a"), "melanin": 0.55},
	"tan": {"label": "Tan", "tone": Color("#9a6c4c"), "under": Color("#a84a36"), "melanin": 0.7},
	"brown": {"label": "Brown", "tone": Color("#6f4a33"), "under": Color("#86402e"), "melanin": 0.85},
	"deep": {"label": "Deep", "tone": Color("#45301f"), "under": Color("#5e2e22"), "melanin": 1.0},
}
const SKIN_DEFAULT := "light"
const HAIR_COLORS := {
	"none": {"label": "None (bald)", "col": Color("#2a1d14"), "on": 0.0},
	"black": {"label": "Black crop", "col": Color("#141110"), "on": 1.0},
	"brown": {"label": "Brown crop", "col": Color("#3a2618"), "on": 1.0},
	"auburn": {"label": "Auburn crop", "col": Color("#5e2c18"), "on": 1.0},
	"blond": {"label": "Blond crop", "col": Color("#a88552"), "on": 1.0},
	"grey": {"label": "Grey crop", "col": Color("#8d8a86"), "on": 1.0},
}
const HAIR_DEFAULT := "brown"
const EYE_DEFAULT := "brown"

# ---- render quality (Low / Medium / High). Stored as [render] quality in user://settings.cfg, default medium.
#   shadow_map     positional shadow atlas of the 3D viewport (px): the key light's soft shadow, and the rims' on high
#   shadow_filter  RenderingServer.ShadowQuality for the soft filter (0 hard .. 5 ultra)
#   glow           bloom round the diffusers
#   skin_detail    pores and fine bumps on the skin (0 smooth)
#   msaa / taa / fxaa   Viewport.MSAA (0 off, 1 = 2x, 2 = 4x): 2x at medium, 4x at high, none with FXAA at low; TAA is off
#                  everywhere: on top of 4x MSAA it cost 11 ms a frame on an M3 for little extra
#   ssao / ssao_q / ssao_half  SSAO on Forward+ only (ignored elsewhere); ssao_q is RenderingServer.EnvironmentSSAOQuality
#   weave          procedural weave normals on cloth
#   ao             per-vertex contact and fold occlusion on the cloth (CPU, computed from body distance)
#   heavy_every    the strain, curvature and AO terms are recomputed every n-th cloth update (0 = never)
#   sss            screen-space subsurface scattering on the body (Forward+ only, extra pass; high only)
#   gi / gi_cascades / gi_cell   SDFGI (real-time global illumination of the room; Forward+ only): cascades, finest cell (m)
#   ssil / ssr     screen-space indirect light and reflections (floor, mirror, glossy cloth); Forward+ only
#   dof            depth of field behind the subject (camera attributes)
#   probe          a reflection probe in the room (the diffusers, windows and props in glossy surfaces and in the eyes)
const QUALITY_LEVELS := ["low", "medium", "high"]
const QUALITY := {
	"low": {"shadow_map": 2048, "shadow_filter": 1, "msaa": 0, "taa": false, "fxaa": true, "glow": false,
		"ssao": false, "ssao_q": 1, "ssao_half": true, "weave": false, "ao": false, "heavy_every": 0, "sss": false, "skin_detail": 0.6,
		"gi": false, "gi_cascades": 2, "gi_cell": 0.2, "ssil": false, "ssr": false, "dof": false, "probe": false},
	"medium": {"shadow_map": 4096, "shadow_filter": 3, "msaa": 1, "taa": false, "fxaa": false, "glow": true,
		"ssao": true, "ssao_q": 1, "ssao_half": true, "weave": true, "ao": true, "heavy_every": 3, "sss": false, "skin_detail": 1.0,
		"gi": true, "gi_cascades": 2, "gi_cell": 0.15, "ssil": false, "ssr": true, "dof": true, "probe": true},
	"high": {"shadow_map": 4096, "shadow_filter": 4, "msaa": 2, "taa": false, "fxaa": false, "glow": true,
		"ssao": true, "ssao_q": 3, "ssao_half": false, "weave": true, "ao": true, "heavy_every": 2, "sss": true, "skin_detail": 1.0,
		"gi": true, "gi_cascades": 3, "gi_cell": 0.1, "ssil": true, "ssr": true, "dof": true, "probe": true},
}

# Per-fabric look (ids from materials/fabrics.json). pattern: 0 plain, 1 twill, 2 satin, 3 knit, 4 nap, 5 smooth.
# freq: weave cycles per metre of pattern (u,v); amp: weave height in metres; rough/spec: surface gloss;
# sheen: grazing-angle light (fibre fuzz); back: light passing through the thin cloth from behind.
const FABRIC_LOOK := {
	"cotton_poplin": {"pattern": 0, "freq": 350.0, "amp": 0.00010, "rough": 0.70, "spec": 0.22, "sheen": 0.30, "back": 0.15},
	"denim_12oz": {"pattern": 1, "freq": 170.0, "amp": 0.00022, "rough": 0.80, "spec": 0.12, "sheen": 0.20, "back": 0.05},
	"linen_medium": {"pattern": 0, "freq": 240.0, "amp": 0.00016, "rough": 0.85, "spec": 0.10, "sheen": 0.20, "back": 0.20},
	"wool_suiting": {"pattern": 4, "freq": 200.0, "amp": 0.00012, "rough": 0.90, "spec": 0.08, "sheen": 0.45, "back": 0.12},
	"silk_charmeuse": {"pattern": 2, "freq": 500.0, "amp": 0.00001, "rough": 0.30, "spec": 0.60, "sheen": 0.20, "back": 0.35},
	"polyester_chiffon": {"pattern": 0, "freq": 400.0, "amp": 0.00001, "rough": 0.50, "spec": 0.30, "sheen": 0.10, "back": 0.45},
	"canvas_heavy": {"pattern": 0, "freq": 220.0, "amp": 0.00025, "rough": 0.90, "spec": 0.05, "sheen": 0.15, "back": 0.04},
	"cotton_jersey": {"pattern": 3, "freq": 200.0, "amp": 0.00014, "rough": 0.80, "spec": 0.10, "sheen": 0.35, "back": 0.10},
	"lambskin_leather": {"pattern": 5, "freq": 60.0, "amp": 0.00002, "rough": 0.40, "spec": 0.45, "sheen": 0.10, "back": 0.00},
}

# ---- UI (style.css)
const UI_BG := Color("#14171c")
const UI_PANEL := Color("#1b2027")
const UI_PANEL2 := Color("#222831")
const UI_FIELD := Color("#10131a")
const UI_LINE := Color("#2e3642")
const UI_HOVER := Color("#283040")
const UI_HOVER_LINE := Color("#4a5565")
const UI_INK := Color("#e7e3da")
const UI_DIM := Color("#8f98a6")
const UI_CHALK := Color("#ff9d4d")
const UI_THREAD := Color("#3fd3c0")
const UI_TRACK := Color("#323a46")  # slider track: lighter than the panels so it reads on them
const UI_TABLE_BG := Color("#0f1216")  # #c2d canvas background in the browser
const UI_FONT_SIZE := 13


static func quality(level: String) -> Dictionary:
	return QUALITY.get(level, QUALITY["medium"])


# ---------------------------------------------------------------- 3D
static func environment(e: Environment) -> void:
	# The room is closed, so there is no background to show: the light comes from the studio's lamps (studio.gd), from
	# the global illumination of the room (SDFGI, per quality level), and from the studio sky (the lamps seen from the
	# mannequin) for a little ambient light and the reflections.
	e.background_mode = Environment.BG_CLEAR_COLOR
	e.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	e.ambient_light_sky_contribution = 1.0
	e.ambient_light_energy = AMBIENT_NOGI
	e.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	e.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	e.tonemap_exposure = EXPOSURE
	e.tonemap_white = 6.0
	e.fog_enabled = false
	# bloom: tight levels only (a wide haze over the whole room drowned the subject), thresholded so only the diffusers,
	# the windows and the bulbs bloom
	e.glow_enabled = false          # set per quality level by apply_quality
	e.glow_intensity = GLOW
	e.glow_strength = 1.0
	e.glow_bloom = 0.0
	e.glow_hdr_threshold = 2.2
	e.glow_hdr_scale = 2.0
	e.glow_blend_mode = Environment.GLOW_BLEND_MODE_SOFTLIGHT
	for lv in 7:
		e.set_glow_level(lv, 1.0 if lv in [0, 1, 2] else 0.0)
	e.ssao_enabled = false          # set per quality level by apply_quality
	e.ssao_radius = AO_RADIUS
	e.ssao_intensity = 1.4
	e.ssao_power = 1.5
	e.ssao_light_affect = 0.15      # a little contact darkening in direct light too (armpits, under the bust)
	e.ssil_enabled = false          # set per quality level
	e.ssil_radius = 1.2
	e.ssil_intensity = 0.8
	e.ssr_enabled = false           # set per quality level
	e.ssr_max_steps = 64
	e.ssr_fade_in = 0.15
	e.ssr_fade_out = 2.0
	e.ssr_depth_tolerance = 0.2
	e.sdfgi_enabled = false         # set per quality level
	e.sdfgi_use_occlusion = false     # occlusion plus coarse cells blotched the walls
	e.sdfgi_read_sky_light = false  # a closed room: the sky is the lamps, painted for the reflections only
	e.sdfgi_bounce_feedback = 0.5
	e.sdfgi_energy = 1.0
	e.sdfgi_normal_bias = 1.1
	e.sdfgi_probe_bias = 1.1
	e.sdfgi_y_scale = Environment.SDFGI_Y_SCALE_75_PERCENT
	# a light colour grade: a hint of cool in the shadows and of warm in the highlights (per channel curves), a little
	# contrast and saturation
	e.adjustment_enabled = true
	e.adjustment_brightness = 1.0
	e.adjustment_contrast = 1.07
	e.adjustment_saturation = 1.04
	e.adjustment_color_correction = grade_curve()


# The grade as a per-channel curve (a 1D look-up table): blue lifted a touch in the shadows, red up and blue down in the
# highlights.
static func grade_curve() -> GradientTexture1D:
	var g := Gradient.new()
	g.offsets = PackedFloat32Array([0.0, 0.25, 0.5, 0.75, 1.0])
	g.colors = PackedColorArray([Color(0.0, 0.004, 0.014), Color(0.245, 0.25, 0.262), Color(0.5, 0.5, 0.5), Color(0.762, 0.748, 0.728), Color(1.0, 0.985, 0.955)])
	var t := GradientTexture1D.new()
	t.gradient = g
	t.width = 256
	return t


# Applies one quality level to the shared scene pieces. key: the studio's key light, the only lamp that casts shadows
# (the rims, lit from behind, drew dark lines across the front of the body with shadows on in the OpenGL renderer).
# SSAO, TAA and SSS need a RenderingDevice (Forward+ or Mobile) and are left off otherwise.
static func apply_quality(level: String, env: Environment, key: Light3D, vp: Viewport, body_mat: Material) -> void:
	var q := quality(level)
	vp.positional_shadow_atlas_size = int(q["shadow_map"])
	vp.positional_shadow_atlas_16_bits = false   # 32-bit depth: the key sits 3.4 m away
	# the key gets a whole quadrant (half the atlas size)
	vp.positional_shadow_atlas_quad_0 = Viewport.SHADOW_ATLAS_QUADRANT_SUBDIV_1
	vp.positional_shadow_atlas_quad_1 = Viewport.SHADOW_ATLAS_QUADRANT_SUBDIV_4
	vp.positional_shadow_atlas_quad_2 = Viewport.SHADOW_ATLAS_QUADRANT_SUBDIV_4
	vp.positional_shadow_atlas_quad_3 = Viewport.SHADOW_ATLAS_QUADRANT_SUBDIV_16
	RenderingServer.positional_soft_shadow_filter_set_quality(int(q["shadow_filter"]))
	RenderingServer.directional_soft_shadow_filter_set_quality(int(q["shadow_filter"]))
	var has_rd := RenderingServer.get_rendering_device() != null
	var ssao_on: bool = bool(q["ssao"]) and has_rd
	env.ssao_enabled = ssao_on
	if ssao_on:
		RenderingServer.environment_set_ssao_quality(int(q["ssao_q"]), bool(q["ssao_half"]), 0.5, 2, 50.0, 300.0)
	env.glow_enabled = bool(q["glow"])
	# global illumination and screen-space effects (Forward+ only)
	var gi_on: bool = bool(q["gi"]) and has_rd
	env.sdfgi_enabled = gi_on
	if gi_on:
		env.sdfgi_cascades = int(q["gi_cascades"])
		env.sdfgi_min_cell_size = float(q["gi_cell"])
	env.ambient_light_energy = AMBIENT if gi_on else AMBIENT_NOGI
	env.ssil_enabled = bool(q["ssil"]) and has_rd
	env.ssr_enabled = bool(q["ssr"]) and has_rd
	if _attr != null:
		_attr.dof_blur_far_enabled = bool(q["dof"]) and has_rd
	if _probe != null:
		_probe.visible = bool(q["probe"])
	_mirror_on = bool(q["probe"])
	if _mirror != null:
		_mirror.enabled = _mirror_on
	vp.msaa_3d = int(q["msaa"])
	vp.use_taa = bool(q["taa"]) and has_rd  # TAA is a RenderingDevice (Forward+ or Mobile) feature
	vp.screen_space_aa = Viewport.SCREEN_SPACE_AA_FXAA if bool(q["fxaa"]) else Viewport.SCREEN_SPACE_AA_DISABLED
	if body_mat is ShaderMaterial:
		(body_mat as ShaderMaterial).set_shader_parameter("sss", 0.1 if (bool(q["sss"]) and has_rd) else 0.0)
		(body_mat as ShaderMaterial).set_shader_parameter("detail", float(q["skin_detail"]))
	if key != null:
		key.shadow_enabled = true


static var _attr: CameraAttributesPractical
static var _probe: ReflectionProbe


# A 50 mm lens (CAM_FOV), with a little depth of field behind the subject (focus() keeps it on the subject).
static func camera(cam: Camera3D) -> void:
	cam.fov = CAM_FOV
	cam.near = CAM_NEAR
	cam.far = CAM_FAR
	_attr = CameraAttributesPractical.new()
	_attr.dof_blur_far_enabled = false      # per quality level
	_attr.dof_blur_far_distance = 5.0
	_attr.dof_blur_far_transition = 4.0
	_attr.dof_blur_amount = 0.07
	_attr.dof_blur_near_enabled = false
	cam.attributes = _attr
	RenderingServer.camera_attributes_set_dof_blur_quality(RenderingServer.DOF_BLUR_QUALITY_MEDIUM, true)


# Keeps the focus on the subject: `dist` is the distance from the camera to the mannequin. The far blur starts just
# behind it and builds up over four metres.
static func focus(dist: float) -> void:
	if _attr != null:
		_attr.dof_blur_far_distance = dist + 0.8
		_attr.dof_blur_far_transition = 4.5


# A reflection probe round the mannequin, captured from its chest: the diffusers, windows and props of the room show up
# in the eyes, the lips, the silk and the platform's edge. It is local on purpose: spread over the whole room it put a
# bright wavy reflection on the ceiling and a wet sheen on the floor. No box projection (the room is far compared with
# the subject: a plain cubemap puts each softbox in the right direction). Captured again for a moment after the first
# frames, once the room's lighting has settled, then frozen.
const PROBE_CENTER := Vector3(0.0, 1.2, 0.0)
const PROBE_SIZE := Vector3(2.6, 2.5, 2.6)

static func reflection_probe(parent: Node3D) -> ReflectionProbe:
	var p := ReflectionProbe.new()
	p.name = "SubjectProbe"
	p.size = PROBE_SIZE
	p.position = PROBE_CENTER
	p.box_projection = false
	p.interior = true
	p.max_distance = 20.0
	p.intensity = 1.0
	p.ambient_mode = ReflectionProbe.AMBIENT_DISABLED
	p.update_mode = ReflectionProbe.UPDATE_ALWAYS
	p.cull_mask = 1                                  # layer 1: the room and its props, not the mannequin on layer 2
	parent.add_child(p)
	_probe = p
	var t := Timer.new()
	t.one_shot = true
	t.wait_time = 1.2
	t.autostart = true
	t.timeout.connect(func() -> void:
		p.update_mode = ReflectionProbe.UPDATE_ONCE
		t.queue_free())
	p.add_child(t)
	return p


const MirrorLib = preload("res://mirror.gd")
static var _mirror_on := true
static var _mirror: Node


# The fitting mirror's planar reflection (mirror.gd). `world` is the 3D world the main camera renders. Switched off on low quality.
static func mirror_setup(parent: Node3D, cam: Camera3D, world: World3D, env: Environment) -> void:
	var glass: Array = []
	for n in parent.find_children("Glass*", "MeshInstance3D", true, false):
		glass.append(n)
	if glass.is_empty():
		return
	_mirror = MirrorLib.new()
	_mirror.name = "MirrorReflection"
	parent.add_child(_mirror)
	_mirror.setup(glass, cam, world, env)
	_mirror.enabled = _mirror_on


static func mirror_update() -> void:
	if _mirror != null:
		_mirror.update()


# Vignette and a trace of film grain, laid over the 3D view as two translucent rectangles on a canvas layer inside the
# viewport (Godot 4.3 has no built-in vignette).
static func post(vp: Node) -> void:
	if vp.get_node_or_null("Post") != null:
		return
	var layer := CanvasLayer.new()
	layer.name = "Post"
	vp.add_child(layer)
	for mode in [0, 1]:
		var r := ColorRect.new()
		r.mouse_filter = Control.MOUSE_FILTER_IGNORE
		r.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		var m := ShaderMaterial.new()
		m.shader = POST_SHADER
		m.set_shader_parameter("mode", mode)
		r.material = m
		layer.add_child(r)


# Soft dark ellipse under the feet: the key's shadow is soft and lands behind the body, so the feet would float
# without a contact shadow. Unshaded, does not cast or write depth, sits just above the floor.
static func contact_shadow() -> MeshInstance3D:
	var node := MeshInstance3D.new()
	var quad := QuadMesh.new()
	quad.size = Vector2(0.75, 0.55)
	node.mesh = quad
	node.rotation_degrees = Vector3(-90.0, 0.0, 0.0)
	node.position = Vector3(0.0, 0.0005, 0.0)
	var m := ShaderMaterial.new()
	m.shader = CONTACT_SHADER
	m.set_shader_parameter("strength", 0.45)
	node.material_override = m
	node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	return node


# The mannequin's skin (shaders/skin.gdshader). The per-vertex data comes from skin.gd through the body view.
static func body_material() -> ShaderMaterial:
	var m := ShaderMaterial.new()
	m.shader = SKIN_SHADER
	# drawn 2.5 mm inside its collision surface: where cloth lies on the skin (0.8 mm display margin over a collision
	# surface that is itself a sampled grid, about 1-2 mm off the mesh at knees and shoulders), the skin poked through in
	# specks. A 2.5 mm thinner body is not visible; measured on walking chinos, body edges crossing the cloth went from
	# ~400 to ~140 per frame (the rest are inside sleeves and trouser legs, where no camera sees them). The shader does
	# the same shrink as StandardMaterial3D's grow did (skin.gdshader vertex()).
	m.set_shader_parameter("shrink", 0.0025)
	if ResourceLoader.exists(REGIONS):
		m.set_shader_parameter("regions", load(REGIONS))
		m.set_shader_parameter("use_regions", 1.0)
	set_skin_tone(m, SKIN_DEFAULT)
	set_hair(m, HAIR_DEFAULT)
	return m


const PHOTO_DIR := "res://assets/skin/photo"           # MakeHuman photo skins (tools/fetch_makehuman_skins.sh)
const PHOTO_PREFIX := "photo:"


# Skin choices for the Body tab: the drawn tones, then any photographic skins found in PHOTO_DIR ("photo:<file>").
static func skin_choices() -> Dictionary:
	var out := SKIN_TONES.duplicate()
	var dir := DirAccess.open(PHOTO_DIR)
	if dir != null:
		var files := Array(dir.get_files())
		files.sort()
		for f in files:
			if String(f).get_extension().to_lower() in ["png", "jpg", "jpeg", "webp"]:
				var label := String(f).get_basename().trim_suffix("_diffuse").replace("_", " ").capitalize()
				out[PHOTO_PREFIX + String(f)] = {"label": "Photo: " + label}
	return out


static var _photo_cache := {}

# Loads a photo skin from the file itself (no import step needed), with mipmaps. Null when it cannot be read.
static func photo_texture(file: String) -> Texture2D:
	if _photo_cache.has(file):
		return _photo_cache[file]
	var img := Image.load_from_file(ProjectSettings.globalize_path(PHOTO_DIR + "/" + file))
	if img == null or img.is_empty():
		return null
	img.generate_mipmaps()
	var tex := ImageTexture.create_from_image(img)
	_photo_cache[file] = tex
	return tex


static func set_skin_tone(m: ShaderMaterial, key: String) -> void:
	if key.begins_with(PHOTO_PREFIX):
		var tex := photo_texture(key.trim_prefix(PHOTO_PREFIX))
		if tex != null:
			m.set_shader_parameter("photo", tex)
			m.set_shader_parameter("use_photo", 1.0)
			# the drawn tone (the fallback, and the light under the skin) follows the photo's average colour
			var img := tex.get_image()
			var small := img.duplicate() as Image
			small.resize(16, 16, Image.INTERPOLATE_BILINEAR)
			var acc := Color(0, 0, 0)
			var cnt := 0
			for y in 16:
				for x in 16:
					var c := small.get_pixel(x, y)
					if c.r + c.g + c.b > 0.15:   # the unused parts of the UV map are black
						acc += c
						cnt += 1
			var avg: Color = acc / maxf(1.0, float(cnt))
			m.set_shader_parameter("tone", Color(avg.r, avg.g, avg.b))
			m.set_shader_parameter("under", Color("#c8503e"))
			m.set_shader_parameter("melanin", clampf(1.0 - avg.get_luminance() * 1.4, 0.0, 1.0))
			return
		key = SKIN_DEFAULT
	m.set_shader_parameter("use_photo", 0.0)
	var t: Dictionary = SKIN_TONES.get(key, SKIN_TONES[SKIN_DEFAULT])
	m.set_shader_parameter("tone", t["tone"])
	m.set_shader_parameter("under", t["under"])
	m.set_shader_parameter("melanin", float(t["melanin"]))
	m.set_shader_parameter("lip_tint", Color(0.78, 0.44, 0.44).lerp(Color(0.72, 0.5, 0.5), float(t["melanin"])))


static func set_hair(m: ShaderMaterial, key: String) -> void:
	var h: Dictionary = HAIR_COLORS.get(key, HAIR_COLORS[HAIR_DEFAULT])
	var c: Color = h["col"]
	m.set_shader_parameter("hair_col", c)
	m.set_shader_parameter("hair", float(h["on"]))
	m.set_shader_parameter("brow_col", c.darkened(0.25) if key != "none" else Color("#2a1d14"))


static func table_material() -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = TABLE_SLAB
	m.roughness = 0.9
	return m


# Fabric shader for one fabric id: weave normals, sheen, backlight, and the cloth's vertex colour as albedo.
# Vertex colour alpha carries the per-vertex occlusion (cloth_shade.gd). Double-sided.
static func cloth_material(fabric_id: String) -> ShaderMaterial:
	var f: Dictionary = FABRIC_LOOK.get(fabric_id, FABRIC_LOOK["cotton_poplin"])
	var m := ShaderMaterial.new()
	m.shader = CLOTH_SHADER
	m.set_shader_parameter("weave_kind", int(f["pattern"]))
	m.set_shader_parameter("freq", float(f["freq"]))
	m.set_shader_parameter("amp", float(f["amp"]))
	m.set_shader_parameter("rough", float(f["rough"]))
	m.set_shader_parameter("spec", float(f["spec"]))
	m.set_shader_parameter("sheen", float(f["sheen"]))
	m.set_shader_parameter("back", float(f["back"]))
	return m


# Fabric colour for vertex colours. Vertex colours are used as linear values, so the
# sRGB hex is converted first (the browser's THREE.Color does the same).
static func cloth_base(hex: String) -> Color:
	return Color(hex).srgb_to_linear()


# Smooth per-vertex normals (area-weighted), the fallback for the cloth mesh when the sim gives no display
# normals (sim.get_display_normals is empty). Same weighting as the C++ version.
# Angle-weighted smooth normals (each face weighted by the angle it subtends at the vertex), the same as the C++
# FailoringSim.get_display_normals, which the editor uses when it can; this is the fallback.
static func cloth_normals(verts: PackedVector3Array, idx: PackedInt32Array) -> PackedVector3Array:
	var n := PackedVector3Array()
	n.resize(verts.size())
	for t in range(0, idx.size() - 2, 3):
		var a := idx[t]
		var b := idx[t + 1]
		var c := idx[t + 2]
		var pa := verts[a]
		var pb := verts[b]
		var pc := verts[c]
		var e1 := pb - pa
		var e2 := pc - pa
		var fn := e1.cross(e2)
		var len := fn.length()
		if len <= 1e-12:
			continue
		var u := fn / len
		n[a] += u * e1.angle_to(e2)
		n[b] += u * (pa - pb).angle_to(pc - pb)
		n[c] += u * (pa - pc).angle_to(pb - pc)
	for i in n.size():
		n[i] = n[i].normalized()
	return n


# ---------------------------------------------------------------- viewport and UI
# Called once with the editor root and the 3D SubViewport. Anti-aliasing is set by apply_quality.
static func apply_ui(root: Control, vp: SubViewport) -> void:
	RenderingServer.set_default_clear_color(UI_TABLE_BG)
	root.theme = make_theme()
	# 3D backdrop: the SubViewport is transparent, and a flat BG rect behind its container shows
	# through. The rect is plain 2D colour, so it matches the browser's scene.background exactly
	# (no tone mapping, no fog).
	# The studio is a closed room, so nothing of the pane's backdrop shows through. (A transparent viewport made the spot
	# lamps light nothing on Forward+ once the GI and the colour grade were on: the mannequin came out black.)
	vp.transparent_bg = false
	post(vp)
	var pane := vp.get_parent() as Control
	if pane != null and pane.get_node_or_null("Backdrop") == null:
		var bd := ColorRect.new()
		bd.name = "Backdrop"
		bd.color = BG
		bd.mouse_filter = Control.MOUSE_FILTER_IGNORE
		bd.show_behind_parent = true
		bd.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		pane.add_child(bd)


static func _box(bg: Color, border: Color = Color(0, 0, 0, 0), radius: int = 6, margin_h: float = 10.0, margin_v: float = 5.0, border_w: int = 1) -> StyleBoxFlat:
	var s := StyleBoxFlat.new()
	s.bg_color = bg
	s.border_color = border
	s.set_border_width_all(border_w)
	s.set_corner_radius_all(radius)
	s.content_margin_left = margin_h
	s.content_margin_right = margin_h
	s.content_margin_top = margin_v
	s.content_margin_bottom = margin_v
	return s


static func make_theme() -> Theme:
	var t := Theme.new()
	var f := SystemFont.new()
	f.font_names = PackedStringArray(["Avenir Next", "Segoe UI", "Helvetica Neue", "Arial"])
	t.default_font = f
	t.default_font_size = UI_FONT_SIZE

	# pane splits (editor layout): a wider gap that is easy to grab
	for ty in ["SplitContainer", "HSplitContainer", "VSplitContainer"]:
		t.set_constant("separation", ty, 8)
		t.set_constant("minimum_grab_thickness", ty, 10)

	# Label and text
	t.set_color("font_color", "Label", UI_INK)
	t.set_color("font_color", "RichTextLabel", UI_INK)
	t.set_color("default_color", "RichTextLabel", UI_INK)

	# buttons (Button and OptionButton share the look)
	for ty in ["Button", "OptionButton", "MenuButton"]:
		t.set_stylebox("normal", ty, _box(UI_PANEL2, UI_LINE))
		t.set_stylebox("hover", ty, _box(UI_HOVER, UI_HOVER_LINE))
		t.set_stylebox("pressed", ty, _box(UI_PANEL, UI_HOVER_LINE))
		t.set_stylebox("disabled", ty, _box(UI_PANEL2, UI_LINE))
		t.set_stylebox("focus", ty, _box(Color(0, 0, 0, 0), UI_CHALK))
		t.set_color("font_color", ty, UI_INK)
		t.set_color("font_hover_color", ty, UI_INK)
		t.set_color("font_pressed_color", ty, UI_INK)
		t.set_color("font_focus_color", ty, UI_INK)
		t.set_color("font_disabled_color", ty, Color(UI_DIM, 0.5))
		t.set_constant("h_separation", ty, 6)

	t.set_color("font_color", "CheckBox", UI_INK)
	t.set_color("font_hover_color", "CheckBox", UI_INK)
	t.set_color("font_pressed_color", "CheckBox", UI_INK)
	t.set_color("font_color", "CheckButton", UI_INK)

	# text fields and spin boxes
	for ty in ["LineEdit", "SpinBox"]:
		t.set_stylebox("normal", ty, _box(UI_FIELD, UI_LINE))
		t.set_stylebox("focus", ty, _box(UI_FIELD, UI_CHALK))
		t.set_stylebox("read_only", ty, _box(UI_FIELD, UI_LINE))
		t.set_color("font_color", ty, UI_INK)
		t.set_color("font_placeholder_color", ty, UI_DIM)
		t.set_color("caret_color", ty, UI_CHALK)
		t.set_color("selection_color", ty, Color(UI_CHALK, 0.3))

	# sliders: dark track, chalk fill
	# the track needs a height (content margins): with none it is 0 px tall and only the grabber shows
	t.set_stylebox("slider", "HSlider", _box(UI_TRACK, Color(0, 0, 0, 0), 3, 0.0, 2.0, 0))
	t.set_stylebox("grabber_area", "HSlider", _box(UI_CHALK, Color(0, 0, 0, 0), 3, 0.0, 2.0, 0))
	t.set_stylebox("grabber_area_highlight", "HSlider", _box(UI_CHALK, Color(0, 0, 0, 0), 3, 0.0, 2.0, 0))

	# panels: the side columns share the browser panel colour and a 1px line
	t.set_stylebox("panel", "PanelContainer", _box(UI_PANEL, UI_LINE, 0, 8.0, 8.0))

	# tabs: flat, chalk underline on the selected tab
	t.set_stylebox("panel", "TabContainer", _box(UI_PANEL, Color(0, 0, 0, 0), 0, 8.0, 8.0, 0))
	var tab_sel := _box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, 6.0, 8.0, 0)
	tab_sel.border_width_bottom = 2
	tab_sel.border_color = UI_CHALK
	t.set_stylebox("tab_selected", "TabContainer", tab_sel)
	t.set_stylebox("tab_unselected", "TabContainer", _box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, 6.0, 8.0, 0))
	t.set_stylebox("tab_hovered", "TabContainer", _box(UI_HOVER, Color(0, 0, 0, 0), 0, 6.0, 8.0, 0))
	t.set_stylebox("tab_disabled", "TabContainer", _box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, 6.0, 8.0, 0))
	t.set_stylebox("tab_focus", "TabContainer", _box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, 6.0, 8.0, 0))
	t.set_color("font_selected_color", "TabContainer", UI_INK)
	t.set_color("font_unselected_color", "TabContainer", UI_DIM)
	t.set_color("font_hovered_color", "TabContainer", UI_INK)
	t.set_color("font_disabled_color", "TabContainer", Color(UI_DIM, 0.5))
	return t
