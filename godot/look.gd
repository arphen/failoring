# Visual setup for the Godot editor, ported from the browser app (app/src/view3d.js for the
# 3D scene, app/src/style.css for the UI). All visual constants live here; editor.gd calls these
# functions at setup time, and cloth_shade.gd builds the per-vertex cloth data. No solver logic here.
extends RefCounted

const CLOTH_SHADER := preload("res://shaders/cloth.gdshader")
const CONTACT_SHADER := preload("res://shaders/contact.gdshader")

# ---- 3D scene (view3d.js)
const BG := Color("#12151a")            # scene background and fog colour
const SKY := Color("#cfd8ea")           # hemisphere sky colour
const HEMI := 0.55                      # hemisphere intensity
const KEY_COLOR := Color("#fff1dc")
const KEY_ENERGY := 0.7
const RIM_COLOR := Color("#9ec3ff")
const RIM_ENERGY := 0.3
const EXPOSURE := 0.95                  # ACES filmic tone mapping, as in the browser
const FOG_DENSITY := 0.02               # light haze so the floor disc edge fades into the background
const CAM_FOV := 36.0                   # vertical fov, PerspectiveCamera(36) in view3d.js
const CAM_NEAR := 0.05
const CAM_FAR := 40.0
const FLOOR := Color("#232830")
const TABLE_SLAB := Color("#2a4640")    # cutting mat (view3d.js buildMat)
const BODY := Color("#b3a699")          # mannequin
const SHADOW_RANGE := 6.0               # m: the key light's shadow frustum ends here (Godot fits it to the camera view up to this distance)
const SHADOW_BIAS := 0.05
const SHADOW_NORMAL_BIAS := 1.5         # normal offset: removes the acne rings on body and floor seen at 0.8; hem peter-panning not measured
const AO_RADIUS := 0.12                 # m, SSAO sampling radius (body scale)

# ---- render quality (Low / Medium / High). Stored as [render] quality in user://settings.cfg, default medium.
#   shadow_map     directional shadow atlas size (px); one cascade, 2048 at medium and high
#   shadow_filter  RenderingServer.ShadowQuality for the soft PCF filter (0 hard .. 5 ultra)
#   msaa / taa / fxaa   Viewport.MSAA (0 off, 2 = 4x): 4x at medium and high, none with FXAA at low; TAA on top at high
#   ssao / ssao_q / ssao_half  SSAO on Forward+ only (ignored elsewhere); ssao_q is RenderingServer.EnvironmentSSAOQuality
#   weave          procedural weave normals on cloth
#   ao             per-vertex contact and fold occlusion on the cloth (CPU, computed from body distance)
#   heavy_every    the strain, curvature and AO terms are recomputed every n-th cloth update (0 = never)
#   sss            subsurface scattering on the body (extra pass; high only)
const QUALITY_LEVELS := ["low", "medium", "high"]
const QUALITY := {
	"low": {"shadow_map": 1024, "shadow_filter": 1, "msaa": 0, "taa": false, "fxaa": true,
		"ssao": false, "ssao_q": 1, "ssao_half": true, "weave": false, "ao": false, "heavy_every": 0, "sss": false},
	"medium": {"shadow_map": 2048, "shadow_filter": 3, "msaa": 2, "taa": false, "fxaa": false,
		"ssao": true, "ssao_q": 1, "ssao_half": true, "weave": true, "ao": true, "heavy_every": 3, "sss": false},
	"high": {"shadow_map": 2048, "shadow_filter": 4, "msaa": 2, "taa": true, "fxaa": false,
		"ssao": true, "ssao_q": 3, "ssao_half": false, "weave": true, "ao": true, "heavy_every": 2, "sss": true},
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
const UI_TRACK := Color("#0f1216")
const UI_TABLE_BG := Color("#0f1216")  # #c2d canvas background in the browser
const UI_FONT_SIZE := 13


static func quality(level: String) -> Dictionary:
	return QUALITY.get(level, QUALITY["medium"])


# ---------------------------------------------------------------- 3D
static func environment(e: Environment) -> void:
	# The 3D viewport is transparent and the BG colour is drawn behind it (see apply_ui). A
	# BG_COLOR background would go through the ACES tone mapper and come out near-black.
	e.background_mode = Environment.BG_CLEAR_COLOR
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = SKY
	e.ambient_light_energy = HEMI
	e.tonemap_mode = Environment.TONE_MAPPER_ACES
	e.tonemap_exposure = EXPOSURE
	e.fog_enabled = true
	e.fog_light_color = BG
	e.fog_density = FOG_DENSITY
	e.glow_enabled = false
	e.ssao_enabled = false          # set per quality level by apply_quality
	e.ssao_radius = AO_RADIUS
	e.ssao_intensity = 1.2
	e.ssao_power = 1.5
	e.ssil_enabled = false          # no screen-space indirect light: too costly on integrated GPUs


# key light: one directional shadow caster, a single orthogonal cascade fitted to the camera view up to
# SHADOW_RANGE. The bias pair (0.05, 1.5) removes the acne rings seen at 0.02 / 0.8. Peter-panning at the hem is not measured.
static func key_light(l: DirectionalLight3D) -> void:
	l.light_color = KEY_COLOR
	l.light_energy = KEY_ENERGY
	l.shadow_enabled = true
	l.shadow_blur = 1.0
	l.shadow_bias = SHADOW_BIAS
	l.shadow_normal_bias = SHADOW_NORMAL_BIAS
	l.shadow_opacity = 0.85
	l.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
	l.directional_shadow_max_distance = SHADOW_RANGE
	l.directional_shadow_fade_start = 0.85


# Applies one quality level to the shared scene pieces. The shadow atlas and filter are global
# (RenderingServer); SSAO is only enabled when the renderer has a RenderingDevice (Forward+ or Mobile).
static func apply_quality(level: String, env: Environment, sun: DirectionalLight3D, vp: SubViewport, body_mat: StandardMaterial3D) -> void:
	var q := quality(level)
	RenderingServer.directional_shadow_atlas_set_size(int(q["shadow_map"]), true)
	RenderingServer.directional_soft_shadow_filter_set_quality(int(q["shadow_filter"]))
	var has_rd := RenderingServer.get_rendering_device() != null
	var ssao_on: bool = bool(q["ssao"]) and has_rd
	env.ssao_enabled = ssao_on
	if ssao_on:
		RenderingServer.environment_set_ssao_quality(int(q["ssao_q"]), bool(q["ssao_half"]), 0.5, 2, 50.0, 300.0)
	vp.msaa_3d = int(q["msaa"])
	vp.use_taa = bool(q["taa"]) and has_rd  # TAA is a RenderingDevice (Forward+ or Mobile) feature
	vp.screen_space_aa = Viewport.SCREEN_SPACE_AA_FXAA if bool(q["fxaa"]) else Viewport.SCREEN_SPACE_AA_DISABLED
	if body_mat != null:
		body_mat.subsurf_scatter_enabled = bool(q["sss"])
		body_mat.subsurf_scatter_strength = 0.35
	if sun != null:
		sun.directional_shadow_max_distance = SHADOW_RANGE


# cool back-left rim light with no shadow (browser `rim`)
static func fill_light(parent: Node3D) -> void:
	var rim := DirectionalLight3D.new()
	rim.light_color = RIM_COLOR
	rim.light_energy = RIM_ENERGY
	rim.shadow_enabled = false
	# direction the light travels: from (-3, 2.5, -3) towards the origin
	rim.transform = Transform3D(Basis.looking_at(Vector3(3.0, -2.5, 3.0).normalized()), Vector3.ZERO)
	parent.add_child(rim)


static func camera(cam: Camera3D) -> void:
	cam.fov = CAM_FOV
	cam.near = CAM_NEAR
	cam.far = CAM_FAR


# Soft dark ellipse under the feet: the shadow map does not resolve the contact at this scale.
# Unshaded, does not cast or write depth, sits just above the floor disc.
static func contact_shadow() -> MeshInstance3D:
	var node := MeshInstance3D.new()
	var quad := QuadMesh.new()
	quad.size = Vector2(0.9, 0.7)
	node.mesh = quad
	node.rotation_degrees = Vector3(-90.0, 0.0, 0.0)
	node.position = Vector3(0.0, 0.0005, 0.0)
	var m := ShaderMaterial.new()
	m.shader = CONTACT_SHADER
	m.set_shader_parameter("strength", 0.6)
	node.material_override = m
	node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	return node


static func body_material() -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = BODY
	m.roughness = 0.5
	m.metallic = 0.0
	m.specular = 0.4
	m.rim_enabled = true            # soft edge light on the silhouette
	m.rim = 0.2
	m.rim_tint = 0.4
	return m


static func floor_material() -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = FLOOR
	m.roughness = 0.95
	return m


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
	t.set_stylebox("slider", "HSlider", _box(UI_TRACK, Color(0, 0, 0, 0), 3, 0.0, 0.0, 0))
	t.set_stylebox("grabber_area", "HSlider", _box(UI_CHALK, Color(0, 0, 0, 0), 3, 0.0, 0.0, 0))
	t.set_stylebox("grabber_area_highlight", "HSlider", _box(UI_CHALK, Color(0, 0, 0, 0), 3, 0.0, 0.0, 0))

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
