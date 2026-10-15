# Visual setup for the Godot editor, ported from the browser app (app/src/view3d.js for the
# 3D scene, app/src/style.css for the UI). All visual constants live here; editor.gd calls these
# functions at setup time, and cloth_shade.gd builds the per-vertex cloth data. No solver logic here.
extends RefCounted

const CLOTH_SHADER := preload("res://shaders/cloth.gdshader")
const CONTACT_SHADER := preload("res://shaders/contact.gdshader")

# ---- 3D scene: a photo studio (studio.gd builds the cove and the lighting rig; the lamps are listed there)
const BG := Color("#0c0d0f")            # behind the cove (above its wall): the studio's dark ceiling
const EXPOSURE := 1.15                  # filmic tone mapping (ACES pushed bright skin to orange where lamps overlap)
const AMBIENT := 0.55                   # diffuse light from the studio sky (walls, floor bounce, softboxes)
const FOG := Color("#151617")           # a faint haze so the far wall of the cove recedes
const FOG_DENSITY := 0.008
const GLOW := 0.32                      # bloom round the glowing diffusers (a lens sees them as very bright)
const CAM_FOV := 36.0                   # vertical fov, PerspectiveCamera(36) in view3d.js
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
#   msaa / taa / fxaa   Viewport.MSAA (0 off, 2 = 4x): 4x at medium and high, none with FXAA at low; TAA on top at high
#   ssao / ssao_q / ssao_half  SSAO on Forward+ only (ignored elsewhere); ssao_q is RenderingServer.EnvironmentSSAOQuality
#   weave          procedural weave normals on cloth
#   ao             per-vertex contact and fold occlusion on the cloth (CPU, computed from body distance)
#   heavy_every    the strain, curvature and AO terms are recomputed every n-th cloth update (0 = never)
#   sss            screen-space subsurface scattering on the body (Forward+ only, extra pass; high only)
const QUALITY_LEVELS := ["low", "medium", "high"]
const QUALITY := {
	"low": {"shadow_map": 2048, "shadow_filter": 1, "msaa": 0, "taa": false, "fxaa": true, "glow": false,
		"ssao": false, "ssao_q": 1, "ssao_half": true, "weave": false, "ao": false, "heavy_every": 0, "sss": false, "skin_detail": 0.6},
	"medium": {"shadow_map": 4096, "shadow_filter": 3, "msaa": 2, "taa": false, "fxaa": false, "glow": true,
		"ssao": true, "ssao_q": 1, "ssao_half": true, "weave": true, "ao": true, "heavy_every": 3, "sss": false, "skin_detail": 1.0},
	"high": {"shadow_map": 4096, "shadow_filter": 4, "msaa": 2, "taa": true, "fxaa": false, "glow": true,
		"ssao": true, "ssao_q": 3, "ssao_half": false, "weave": true, "ao": true, "heavy_every": 2, "sss": true, "skin_detail": 1.0},
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
	# The 3D viewport is transparent and the BG colour is drawn behind it (see apply_ui): it only shows above the cove's
	# wall. The light comes from studio.gd: its lamps, and its sky (the studio seen from the mannequin) for the ambient
	# light and the reflections.
	e.background_mode = Environment.BG_CLEAR_COLOR
	e.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	e.ambient_light_sky_contribution = 1.0
	e.ambient_light_energy = AMBIENT
	e.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	e.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	e.tonemap_exposure = EXPOSURE
	e.tonemap_white = 6.0
	e.fog_enabled = true
	e.fog_light_color = FOG
	e.fog_density = FOG_DENSITY
	e.fog_sky_affect = 0.0
	e.glow_enabled = false          # set per quality level by apply_quality
	e.glow_intensity = GLOW
	e.glow_strength = 1.0
	e.glow_bloom = 0.0
	e.glow_hdr_threshold = 1.2
	e.glow_blend_mode = Environment.GLOW_BLEND_MODE_SCREEN
	e.ssao_enabled = false          # set per quality level by apply_quality
	e.ssao_radius = AO_RADIUS
	e.ssao_intensity = 1.4
	e.ssao_power = 1.5
	e.ssao_light_affect = 0.15      # a little contact darkening in direct light too (armpits, under the bust)
	e.ssil_enabled = false          # no screen-space indirect light: too costly on integrated GPUs


# Applies one quality level to the shared scene pieces. key: the studio's key light, the only lamp that casts shadows
# (the rims, lit from behind, drew dark lines across the front of the body with shadows on in the OpenGL renderer).
# SSAO, TAA and SSS need a RenderingDevice (Forward+ or Mobile) and are left off otherwise.
static func apply_quality(level: String, env: Environment, key: Light3D, vp: SubViewport, body_mat: Material) -> void:
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
	vp.msaa_3d = int(q["msaa"])
	vp.use_taa = bool(q["taa"]) and has_rd  # TAA is a RenderingDevice (Forward+ or Mobile) feature
	vp.screen_space_aa = Viewport.SCREEN_SPACE_AA_FXAA if bool(q["fxaa"]) else Viewport.SCREEN_SPACE_AA_DISABLED
	if body_mat is ShaderMaterial:
		(body_mat as ShaderMaterial).set_shader_parameter("sss", 0.35 if (bool(q["sss"]) and has_rd) else 0.0)
		(body_mat as ShaderMaterial).set_shader_parameter("detail", float(q["skin_detail"]))
	if key != null:
		key.shadow_enabled = true


static func camera(cam: Camera3D) -> void:
	cam.fov = CAM_FOV
	cam.near = CAM_NEAR
	cam.far = CAM_FAR


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
	vp.transparent_bg = true
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
