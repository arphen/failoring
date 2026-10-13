# Visual setup for the Godot editor, ported from the browser app (app/src/view3d.js for the
# 3D scene, app/src/style.css for the UI). All visual constants live here; editor.gd calls these
# functions at setup time and in the cloth colour/normal path. No logic lives here.
extends RefCounted

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
const BODY := Color("#b3a699")          # mannequin, matte
const CLOTH_ROUGH := 0.78
const CLOTH_SHEEN := 0.3                # approximated with Godot's rim term (no sheen in StandardMaterial3D)
const CLOTH_RIM_TINT := 0.5

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


# ---------------------------------------------------------------- 3D
static func environment(e: Environment) -> void:
	e.background_mode = Environment.BG_COLOR
	e.background_color = BG
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = SKY
	e.ambient_light_energy = HEMI
	e.tonemap_mode = Environment.TONE_MAPPER_ACES
	e.tonemap_exposure = EXPOSURE
	e.fog_enabled = true
	e.fog_light_color = BG
	e.fog_density = FOG_DENSITY
	e.glow_enabled = false
	e.ssao_enabled = false


# key light: one directional shadow caster, one orthogonal split, soft filter
static func key_light(l: DirectionalLight3D) -> void:
	l.light_color = KEY_COLOR
	l.light_energy = KEY_ENERGY
	l.shadow_enabled = true
	l.shadow_blur = 2.0
	l.shadow_bias = 0.05
	l.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
	l.directional_shadow_max_distance = 12.0


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


static func body_material() -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = BODY
	m.roughness = 0.52
	m.metallic = 0.0
	m.specular = 0.4
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


# double-sided matte fabric, vertex colours as albedo (browser MeshPhysicalMaterial, side DoubleSide)
static func cloth_material() -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.vertex_color_use_as_albedo = true
	m.cull_mode = BaseMaterial3D.CULL_DISABLED
	m.roughness = CLOTH_ROUGH
	m.metallic = 0.0
	m.specular = 0.35
	m.rim_enabled = true
	m.rim = CLOTH_SHEEN
	m.rim_tint = CLOTH_RIM_TINT
	return m


# Fabric colour for vertex colours. Vertex colours are used as linear values, so the
# sRGB hex is converted first (the browser's THREE.Color does the same).
static func cloth_base(hex: String) -> Color:
	return Color(hex).srgb_to_linear()


# Smooth per-vertex normals (area-weighted), needed because the cloth mesh is rebuilt from
# positions every frame and carries no normals otherwise.
static func cloth_normals(verts: PackedVector3Array, idx: PackedInt32Array) -> PackedVector3Array:
	var n := PackedVector3Array()
	n.resize(verts.size())
	for t in range(0, idx.size() - 2, 3):
		var a := idx[t]
		var b := idx[t + 1]
		var c := idx[t + 2]
		var fn := (verts[b] - verts[a]).cross(verts[c] - verts[a])
		n[a] += fn
		n[b] += fn
		n[c] += fn
	for i in n.size():
		n[i] = n[i].normalized()
	return n


# ---------------------------------------------------------------- viewport and UI
# Called once with the editor root and the 3D SubViewport.
static func apply_ui(root: Control, vp: SubViewport) -> void:
	vp.msaa_3d = Viewport.MSAA_4X
	RenderingServer.set_default_clear_color(UI_TABLE_BG)
	root.theme = make_theme()


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
	var tab_sel := _box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, 10.0, 8.0, 0)
	tab_sel.border_width_bottom = 2
	tab_sel.border_color = UI_CHALK
	t.set_stylebox("tab_selected", "TabContainer", tab_sel)
	t.set_stylebox("tab_unselected", "TabContainer", _box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, 10.0, 8.0, 0))
	t.set_stylebox("tab_hovered", "TabContainer", _box(UI_HOVER, Color(0, 0, 0, 0), 0, 10.0, 8.0, 0))
	t.set_stylebox("tab_disabled", "TabContainer", _box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, 10.0, 8.0, 0))
	t.set_stylebox("tab_focus", "TabContainer", _box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, 10.0, 8.0, 0))
	t.set_color("font_selected_color", "TabContainer", UI_INK)
	t.set_color("font_unselected_color", "TabContainer", UI_DIM)
	t.set_color("font_hovered_color", "TabContainer", UI_INK)
	t.set_color("font_disabled_color", "TabContainer", Color(UI_DIM, 0.5))
	return t
