# Godot editor: the browser app's core loop with the native solver. Pick a plan and a body
# (preset or sliders), watch it drape and walk, read the pattern info, edit on the table.
#   godot --path godot res://editor.tscn
# Env: FL_AUTO=preset,plan,frames (run, shoot, quit), FL_STAND=1 (stay standing),
# FL_PRESET=name (wrap_check preset select).
extends Node3D

const PlanLoader = preload("res://plan_loader.gd")
const BodyView = preload("res://scripts/body_loader.gd")
const BodyGen = preload("res://body_gen.gd")
const Draft = preload("res://draft.gd")
const Wrap = preload("res://wrap.gd")
const Info = preload("res://info.gd")
const Builtin = preload("res://builtin_plans.gd")
const Table = preload("res://table.gd")
const Look = preload("res://look.gd")
const ClothShade = preload("res://cloth_shade.gd")
const Library = preload("res://library.gd")
const Outfit = preload("res://outfit.gd")

const QUALITIES := {
	"draft": {"label": "Draft (fast)", "edge": 0.060, "sub": 1, "newton": 6, "frames": 36},
	"standard": {"label": "Standard", "edge": 0.040, "sub": 2, "newton": 8, "frames": 48},
	"fine": {"label": "Fine (slow)", "edge": 0.028, "sub": 2, "newton": 10, "frames": 60},
	"photo": {"label": "Photo", "edge": 0.025, "sub": 3, "newton": 12, "frames": 60, "self": true},
}
const FS_GARMENTS := [["Tee", "teagan"], ["Pencil skirt", "penelope"], ["Chinos", "charlie"]]
const CYCLE := 64.0
const RAMP := 24.0

var sim: FailoringSim
var body_node: BodyView
var cloth_node: MeshInstance3D
var render_q := "medium"                 # Look.QUALITY_LEVELS: low, medium, high (settings.cfg [render] quality)
var render_env: Environment
var render_sun: DirectionalLight3D
var _shade := ClothShade.new()           # cloth normals, UVs, strain, contact occlusion, fabric surfaces
var cam_pivot: Node3D
var cam: Camera3D
var debounce: Timer

# state (mirrors App.S pieces of it)
var M: Dictionary = {}
var meas := {}
var prev_body = null
var body_rest = null
var body_stand = null
var plan_key := "fs-teagan"
var plan: Dictionary = {}
var plan_name := ""
var plan_notes := ""
var fabric_id := "cotton_poplin"
var quality := "standard"
var motion := "walk"
var seam_stiffness := 20000.0
var ramp_frames := 20
var self_collision := false

# Outfit: the garments on the mannequin, inner first (outfit.gd merges the shown ones into one simulation). Entry:
# {uid, key, redraft, name, notes, fabric, color, visible, locked, fitted, plan, pieces, seams}. The globals above
# (plan*, fabric_id, pieces, seams) are the ACTIVE garment's working copy: _store_active() copies them back into its
# entry, and _activate() loads an entry into them. Anything that reads the outfit calls _store_active() first.
var outfit: Array = []
var active := -1
var _next_uid := 0
var _need_outfit := true       # (re)start the outfit from plan_key on the next regen (boot, demo, FL_AUTO)
var _sim_pieces: Array = []    # merged pieces/seams of the last build: stage poses, stitches, colours, wraps
var _sim_seams: Array = []
var _piece_uid := {}           # merged piece id -> garment uid
var _active_label: Label
var _fabric_list: Array = []
var sel = null
var sel_v := -1
var health: FailoringGarmentHealth = null  # per-garment checks of the current build (garment_health.gd)
var health_text := ""                       # the warning above the stats line (empty when every garment is fine)
var health_hot: Array = []                  # piece ids the cutting table tints red
var sew_a = null
var pieces: Array = []
var seams: Array = []
var tool := "select"
var snap := true
var undo: Array = []
var redo: Array = []
var _notice := ""        # warning shown in the status line after an edit (cleared by the next edit or undo)
var _meas_key := ""      # the body measurements the drafted garments were last drafted for
var table_dirty := true
var table: Control
var stage: Node3D
var sub_view: SubViewport

var frame := 0
var walk_k := 0.0
var playing := false
var bake_ms := 0.0
var step_ms := 0.0

# Background step: one worker thread runs bake + sim.step + display copy for the NEXT frame while the main thread draws
# the previous one (see _submit_step). Only the worker calls into `sim` while a step is in flight. The main thread calls
# sim only after _drain(), and takes results only through _result under _mutex.
var _worker: Thread
var _mutex := Mutex.new()
var _work_sem := Semaphore.new()   # main -> worker: a job is posted, or quit is requested
var _done_sem := Semaphore.new()   # worker -> main: the posted job is finished and _result holds it
var _job := {}
var _result := {}
var _quit := false
var _in_flight := false
var _use_worker := false
var _solver_stats := {}            # cached after each collected step (sim must not be queried mid-step)
var _vcount := 0

# timeline (mirrors the browser footer + V.T stages 0-4)
var tl_t := 0.0
var tl_speed := 1.0
var target := 0
var frames: Array = []       # each {pos, cloth, body, ke, gap, clear, ms, bake}
var meta := {}
var wraps := {}
var mesh_edges := PackedInt32Array()
var stitch_nodes := []       # {node, pairs, si}
var seam_nodes := []         # {node, seg, si}
var mat_node: MeshInstance3D
var tl_play_btn: Button
var tl_range: HSlider
var tl_stage_label: Label
var tl_sim_label: Label

var yaw := 0.3
var pitch := -0.12
var dist := 2.4
var zoom := 1.0

# 3D view camera (user input). yaw/pitch/zoom are the user's orbit, pan_off moves the orbit target from the
# auto-framing target. Pitch is negative when the camera is above the pivot (see _update_camera).
const CAM_YAW0 := 0.3
const CAM_PITCH0 := -0.12
const CAM_PITCH_MIN := -1.5   # browser view3d.js clamps elevation to -0.15..1.5, i.e. pitch -1.5..0.15
const CAM_PITCH_MAX := 0.15
const CAM_ZOOM_MIN := 0.25
const CAM_ZOOM_MAX := 3.0
const CAM_PAN_MAX := 3.0      # metres the target may wander from its framed position
const CAM_IDLE_MS := 3000     # auto-orbit resumes this long after the last camera input
var pan_off := Vector3.ZERO
var view_dist := 2.9          # distance used by the last _update_camera (scales pan)
var user_cam_until := 0       # ms timestamp: auto-orbit is paused until then
var pointer_over_view := false

# UI scale (see _apply_ui_scale). ui_scale_pref 0 means auto, from the window size; otherwise a fixed factor.
var ui_scale_pref := 0.0
var _ui_scale := 1.0          # effective factor currently applied to the window
var _scale_key := ""          # window size + screen the scale was last computed for
var scale_opt: OptionButton
var view_pane: Control
var view_image: TextureRect

var plan_opt: OptionButton
var quality_opt: OptionButton
var motion_opt: OptionButton
var status_label: Label
var _fold_open := true      # Piece tab "Edges" section, kept across tab rebuilds
@onready var tabs: TabContainer = $UI/Root/Main/Right/Tabs
@onready var library_panel = $UI/Root/Main/Left/Library  # library_panel.gd (untyped: calls its own methods)


func _ready() -> void:
	table = $UI/Root/Main/TablePane/Table
	table.ed = self
	stage = $UI/Root/Main/ViewPane/View/Stage
	sub_view = $UI/Root/Main/ViewPane/View
	_load_settings()
	_apply_ui_scale()
	Look.apply_ui($UI/Root, sub_view)
	M = BodyGen.load_data(PlanLoader.load_json("res://assets/body_data.json"))
	meas = (BodyGen.PRESETS["average"] as Dictionary).duplicate()
	_build_scene()
	_apply_render_quality()
	_apply_auto_env()
	_build_ui()
	_build_table_bar()
	_bind_view_input()
	_show_view_texture()
	if OS.has_environment("FL_STAND"):
		motion = "stand"
	debounce = Timer.new()
	debounce.one_shot = true
	debounce.wait_time = 0.25
	debounce.timeout.connect(_on_debounce)
	add_child(debounce)
	# Headless runs and tests keep the synchronous path (no threads, identical results). FL_THREAD=1 forces the worker
	# under headless (for tests); FL_SYNC=1 forces the synchronous path.
	_use_worker = (DisplayServer.get_name() != "headless" or OS.has_environment("FL_THREAD")) and not OS.has_environment("FL_SYNC")
	if _use_worker:
		_worker = Thread.new()
		_worker.start(_worker_main)
	regen_and_rebuild()
	if OS.has_environment("FL_DEMO"):
		_demo_toggle()


func _apply_auto_env() -> void:
	if not OS.has_environment("FL_AUTO"):
		return
	var parts := OS.get_environment("FL_AUTO").split(",")
	if parts.size() >= 3:
		meas = (BodyGen.PRESETS.get(parts[0], BodyGen.PRESETS["average"]) as Dictionary).duplicate()
		plan_key = parts[1] if parts[1].begins_with("fs-") or Builtin.all().has(parts[1]) or parts[1] in ["pants", "tank", "shirt"] else "fs-" + parts[1]
	if parts.size() >= 4 and QUALITIES.has(parts[3]):
		quality = parts[3]
	if parts.size() >= 5 and (parts[4] == "stand" or parts[4] == "walk"):
		motion = parts[4]
	if OS.has_environment("FL_MEAS"):
		var extra = JSON.parse_string(OS.get_environment("FL_MEAS"))
		if extra is Dictionary:
			for k in (extra as Dictionary).keys():
				meas[k] = extra[k]


# ------------------------------------------------------------------ UI scale
# The UI is designed at UI_SCALE_BASE (the 1280x720 layout). The window draws the 2D canvas stretched to fit
# (canvas_items, expand; see project.godot), and content_scale_factor is set so the total factor is exactly the
# effective scale E. Fonts, margins, minimum sizes and the theme all scale with E, so text and buttons keep their
# proportions on a 4K or Retina window. The 3D SubViewport is sized from the container's on-screen pixels, so it is
# rendered at native resolution and stays sharp at any scale.
const UI_SCALE_BASE := Vector2i(1280, 720)
const UI_SCALE_STEPS := [0.75, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0, 2.5]
const UI_SCALE_MIN := 0.75
const UI_SCALE_MAX := 2.5
const UI_MIN_VIEW := Vector2(1000, 600)   # the panels need this much room in design units; E shrinks below it
const UI_AUTO_GAIN := 0.6                 # auto: a window twice the base size gets E = 1.6 (not 2.0)
const SETTINGS_PATH := "user://settings.cfg"
var scale_values: Array = []              # option index -> stored value (0.0 is auto)


func _load_settings() -> void:
	var cf := ConfigFile.new()
	if cf.load(SETTINGS_PATH) == OK:
		ui_scale_pref = clampf(float(cf.get_value("ui", "scale", 0.0)), 0.0, UI_SCALE_MAX)
		var rq := String(cf.get_value("render", "quality", "medium"))
		render_q = rq if Look.QUALITY_LEVELS.has(rq) else "medium"
	# a missing or unreadable file keeps the defaults (auto scale, medium render quality)
	if OS.has_environment("FL_RENDER") and Look.QUALITY_LEVELS.has(OS.get_environment("FL_RENDER")):
		render_q = OS.get_environment("FL_RENDER")


func _save_settings() -> void:
	var cf := ConfigFile.new()
	cf.load(SETTINGS_PATH)  # keep any other sections already in the file
	cf.set_value("ui", "scale", ui_scale_pref)
	var err := cf.save(SETTINGS_PATH)
	if err != OK:
		push_warning("could not save %s (error %d)" % [SETTINGS_PATH, err])


# Render quality (Look.QUALITY): applies to the scene at once and is kept in [render] quality.
func _apply_render_quality() -> void:
	if render_env == null:
		return
	Look.apply_quality(render_q, render_env, render_sun, sub_view, body_node.material_override as StandardMaterial3D)
	_shade.set_quality(render_q)


func _cycle_render_quality() -> void:
	var i := Look.QUALITY_LEVELS.find(render_q)
	render_q = Look.QUALITY_LEVELS[(i + 1) % Look.QUALITY_LEVELS.size()]
	_apply_render_quality()
	var cf := ConfigFile.new()
	cf.load(SETTINGS_PATH)
	cf.set_value("render", "quality", render_q)
	if cf.save(SETTINGS_PATH) != OK:
		push_warning("could not save %s" % SETTINGS_PATH)
	notify("Render quality: " + render_q.capitalize())


# Effective factor for a window of win physical pixels: the user's fixed choice, or auto from the window size.
func _scale_for_window(win: Vector2i) -> float:
	var want := ui_scale_pref
	if want <= 0.0:
		var fit := minf(float(win.x) / UI_SCALE_BASE.x, float(win.y) / UI_SCALE_BASE.y)
		want = 1.0 + (fit - 1.0) * UI_AUTO_GAIN
	var room := minf(float(win.x) / UI_MIN_VIEW.x, float(win.y) / UI_MIN_VIEW.y)
	return clampf(want, UI_SCALE_MIN, maxf(UI_SCALE_MIN, minf(UI_SCALE_MAX, room)))


func _apply_ui_scale() -> void:
	var w := get_window()
	var win := w.size
	if win.x <= 0 or win.y <= 0:
		return  # minimised or not laid out yet
	var stretch := minf(float(win.x) / UI_SCALE_BASE.x, float(win.y) / UI_SCALE_BASE.y)
	var e := _scale_for_window(win)
	w.content_scale_mode = Window.CONTENT_SCALE_MODE_CANVAS_ITEMS
	w.content_scale_aspect = Window.CONTENT_SCALE_ASPECT_EXPAND
	w.content_scale_size = UI_SCALE_BASE
	# the canvas stretch already applies the window-size fit; this factor makes the total exactly e
	w.content_scale_factor = e / maxf(stretch, 0.01)
	_ui_scale = e
	_scale_key = "%s@%d" % [win, w.current_screen]
	_sync_scale_opt()


# Called every frame: a resize or a move to another screen (which changes the window's screen) recomputes the scale.
func _watch_window() -> void:
	var w := get_window()
	if "%s@%d" % [w.size, w.current_screen] != _scale_key:
		_apply_ui_scale()
	_sync_view_px()


# The 3D SubViewport renders at on-screen pixels: the pane's design size times the window's total scale, so the
# TextureRect shows it 1:1 and the 3D view is not upscaled (blurry) at a scale above 1. Skipped while a shot resizes
# the viewport (see _shoot).
func _sync_view_px() -> void:
	if view_pane == null or shoot_busy:
		return
	var k := get_window().get_final_transform().get_scale()
	var px := Vector2i(maxi(1, roundi(view_pane.size.x * k.x)), maxi(1, roundi(view_pane.size.y * k.y)))
	if sub_view.size != px:
		sub_view.size = px


func _set_ui_scale_pref(v: float) -> void:
	ui_scale_pref = v
	_apply_ui_scale()
	_save_settings()


# Ctrl/Cmd +/- moves to the next fixed step above or below the current scale, Ctrl/Cmd 0 returns to auto.
func _step_ui_scale(dir: int) -> void:
	var cur := _ui_scale
	var pick := -1.0
	if dir > 0:
		for s in UI_SCALE_STEPS:
			if s > cur + 0.01:
				pick = s
				break
	else:
		for i in range(UI_SCALE_STEPS.size() - 1, -1, -1):
			if UI_SCALE_STEPS[i] < cur - 0.01:
				pick = UI_SCALE_STEPS[i]
				break
	if pick > 0.0:
		_set_ui_scale_pref(pick)


func _scale_label(v: float) -> String:
	return "Auto" if v <= 0.0 else "%d%%" % roundi(v * 100.0)


func _add_scale_opt(box: VBoxContainer) -> void:
	scale_values = [0.0] + UI_SCALE_STEPS
	var labels := []
	for v in scale_values:
		labels.append(_scale_label(v))
	_add_opt(box, "UI size", "ScaleOpt", labels, _scale_label(ui_scale_pref))
	scale_opt = box.get_node("ScaleOpt")
	scale_opt.item_selected.connect(func(i: int) -> void: _set_ui_scale_pref(scale_values[i]))
	_sync_scale_opt()


# Shows the stored choice in the option button (the closest entry when the file holds an unlisted value).
func _sync_scale_opt() -> void:
	if scale_opt == null or scale_values.is_empty():
		return
	var best := 0
	for i in scale_values.size():
		if absf(scale_values[i] - ui_scale_pref) < absf(scale_values[best] - ui_scale_pref):
			best = i
	scale_opt.select(best)


# ------------------------------------------------------------------ scene
func _build_scene() -> void:
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	Look.environment(env.environment)
	stage.add_child(env)
	render_env = env.environment
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	Look.key_light(sun)
	stage.add_child(sun)
	render_sun = sun
	stage.add_child(Look.contact_shadow())
	Look.fill_light(stage)
	var floor_node := MeshInstance3D.new()
	var disc := CylinderMesh.new()
	disc.top_radius = 9.0
	disc.bottom_radius = 9.0
	disc.height = 0.02
	floor_node.mesh = disc
	floor_node.material_override = Look.floor_material()
	floor_node.position = Vector3(0, -0.011, 0)
	stage.add_child(floor_node)
	cam_pivot = Node3D.new()
	cam_pivot.position = Vector3(0, 0.9, 0)
	stage.add_child(cam_pivot)
	cam = Camera3D.new()
	Look.camera(cam)
	cam.position = Vector3(0, 0, dist)
	cam_pivot.add_child(cam)
	body_node = BodyView.new()
	body_node.material_override = Look.body_material()
	stage.add_child(body_node)
	cloth_node = MeshInstance3D.new()
	cloth_node.extra_cull_margin = 0.5   # one material per fabric surface: see cloth_shade.gd
	stage.add_child(cloth_node)


# ------------------------------------------------------------------ UI
func _build_ui() -> void:
	var box: VBoxContainer = $UI/Root/Main/Left/Controls/VBox
	for c in box.get_children():
		c.queue_free()
	_add_opt(box, "Plan", "PlanOpt", _plan_entries().map(func(e: Array) -> String: return e[0]), _plan_title(plan_key))
	var rb := Button.new()
	rb.name = "RedraftBtn"
	rb.text = "Re-draft"
	rb.pressed.connect(_on_redraft)
	box.add_child(rb)
	var qlabels := []
	for k in QUALITIES.keys():
		qlabels.append(QUALITIES[k]["label"])
	_add_opt(box, "Mesh", "QualityOpt", qlabels, QUALITIES[quality]["label"])
	_add_opt(box, "Motion", "MotionOpt", ["Standing", "Walking"], "Walking" if motion == "walk" else "Standing")
	var open := Button.new()
	open.name = "OpenBtn"
	open.text = "Open pattern..."
	open.pressed.connect(_on_open)
	box.add_child(open)
	var demo := Button.new()
	demo.name = "DemoBtn"
	demo.text = "Demo"
	demo.pressed.connect(_demo_toggle)
	box.add_child(demo)
	var save := Button.new()
	save.name = "SaveBtn"
	save.text = "Save pattern"
	save.pressed.connect(_on_save)
	box.add_child(save)
	var st := Label.new()
	st.name = "StatusLabel"
	st.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	st.text = "starting"
	box.add_child(st)
	plan_opt = box.get_node("PlanOpt")
	quality_opt = box.get_node("QualityOpt")
	motion_opt = box.get_node("MotionOpt")
	status_label = box.get_node("StatusLabel")
	_build_timeline_bar()
	plan_opt.item_selected.connect(_on_plan)
	quality_opt.item_selected.connect(_on_quality)
	motion_opt.item_selected.connect(_on_motion)
	_add_scale_opt(box)
	_build_tabs()
	library_panel.setup(self)
	library_panel.add_requested.connect(_on_library_add)
	library_panel.save_requested.connect(_on_library_save)


func _add_opt(box: VBoxContainer, label_text: String, opt_name: String, items: Array, current: String) -> void:
	var l := Label.new()
	l.text = label_text
	box.add_child(l)
	var o := OptionButton.new()
	o.name = opt_name
	for it in items:
		o.add_item(String(it))
	for i in o.item_count:
		if o.get_item_text(i) == current:
			o.select(i)
	box.add_child(o)


func _plan_entries() -> Array:
	var out := [["Pencil skirt", "pencil"], ["A-line skirt", "aline"], ["Blank rectangle", "blank"],
		["Trousers (drafted)", "pants"], ["Tank (drafted)", "tank"], ["T-shirt (drafted)", "shirt"]]
	for g in FS_GARMENTS:
		out.append(["%s (FreeSewing)" % g[0], "fs-" + g[1]])
	return out


func _plan_title(key: String) -> String:
	for e in _plan_entries():
		if e[1] == key:
			return e[0]
	return key


# The name a garment carries: the Plan dropdown's name for built-in plans, so the dropdown, the header and the status
# line agree. Opened patterns and saved garments keep their own name.
func _display_name(key: String, p: Dictionary) -> String:
	var t := _plan_title(key)
	return t if t != key else String(p.get("name", key))


func _build_tabs() -> void:
	for tab in ["Outfit", "Piece", "Seams", "Body", "Fabric", "Pattern", "Shoot"]:
		var sc := ScrollContainer.new()
		sc.name = tab
		sc.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
		var vb := VBoxContainer.new()
		vb.name = "VBox"
		vb.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		sc.add_child(vb)
		tabs.add_child(sc)
	if OS.has_environment("FL_TAB"):
		for i in tabs.get_tab_count():
			if tabs.get_tab_title(i) == OS.get_environment("FL_TAB"):
				tabs.current_tab = i
	_build_body_tab()
	_build_fabric_tab()
	_build_pattern_tab()
	_build_piece_tab()
	_build_seams_tab()
	_build_shoot_tab()
	_refresh_outfit_tab()



func _build_pattern_tab() -> void:
	var text := RichTextLabel.new()
	text.name = "InfoText"
	text.bbcode_enabled = true
	text.scroll_active = true
	text.size_flags_vertical = Control.SIZE_EXPAND_FILL
	text.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_tab_vbox("Pattern").add_child(text)


func _tab_vbox(tab: String) -> VBoxContainer:
	return tabs.get_node("%s/VBox" % tab)


func _build_seams_tab() -> void:
	_refresh_seams_tab()


func _refresh_seams_tab() -> void:
	var vb := _tab_vbox("Seams")
	if vb == null:
		return
	for c in vb.get_children():
		c.queue_free()
	var h := Label.new()
	h.text = "Seams"
	vb.add_child(h)
	if seams.is_empty():
		var none := Label.new()
		none.text = "No seams yet."
		vb.add_child(none)
	var si := 0
	for s in seams:
		si += 1
		var a = Table.piece_by_id(pieces, String(s["a"][0]))
		var b = Table.piece_by_id(pieces, String(s["b"][0]))
		if a == null or b == null:
			continue
		var row := HBoxContainer.new()
		var sw := ColorRect.new()
		sw.color = Color(Table.SEAM_COLORS[(si - 1) % Table.SEAM_COLORS.size()])
		sw.custom_minimum_size = Vector2(12, 12)
		row.add_child(sw)
		var la: float = Table.edge_len(a["outline"], int(s["a"][1]))
		var lb: float = Table.edge_len(b["outline"], int(s["b"][1]))
		var ease: float = (lb - la) / la if la > 0.0 else 0.0
		var chk := Table.sew_check(la, lb)
		var t := Label.new()
		t.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		t.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		t.text = "Seam %d: %s edge %d (%.1f cm) ↔ %s edge %d (%.1f cm)\nease %s%.1f%%" % [si, String(a.get("name", a["id"])), int(s["a"][1]) + 1, la * 100.0, String(b.get("name", b["id"])), int(s["b"][1]) + 1, lb * 100.0, "+" if ease >= 0.0 else "", ease * 100.0]
		if chk["warn"]:
			t.text += "\nWarning: %.1f cm apart (%.1f%%), the drape eases the difference" % [chk["diff"], chk["pct"]]
		row.add_child(t)
		var flip := Button.new()
		flip.text = "⇄"
		flip.tooltip_text = "Flip direction"
		flip.pressed.connect(func() -> void: push_undo(); s["reverse"] = not bool(s.get("reverse", false)); changed("structure"))
		row.add_child(flip)
		var del := Button.new()
		del.text = "✕"
		del.tooltip_text = "Delete seam"
		del.pressed.connect(func() -> void: push_undo(); seams.erase(s); notify("Deleted seam %d. Ctrl+Z restores it." % si); changed("structure"))
		row.add_child(del)
		vb.add_child(row)
	var note := Label.new()
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	note.text = "Pick the Sew tool on the cutting table, click one edge, then the edge it joins. Edge numbers (from 1) are shown beside each edge length. Lengths are measured on the cut outline; a warning means the drape will ease the difference."
	vb.add_child(note)
	_lock_editing(vb)


# ------------------------------------------------------------------ demo mode
var demo_active := false
var demo_bodies := []
var demo_outfits := []
var demo_body := 0
var demo_outfit := 0
var demo_failures := 0
var demo_start := 0
var demo_pause_until := 0
var demo_max := 0
var demo_count := 0
var demo_last_count := 0
var demo_last_tick := 0
var demo_bar: HBoxContainer
var demo_info: Label


func _demo_plan_keys() -> Array:
	var out := []
	for e in _plan_entries():
		out.append(e[1])
	return out


func _demo_toggle() -> void:
	if demo_active:
		_demo_stop()
	else:
		demo_max = int(OS.get_environment("FL_DEMO")) if OS.has_environment("FL_DEMO") else 0
		demo_count = 0
		demo_active = true
		demo_bodies = ["petite", "average", "tall", "curvy", "athletic", "plus"]
		demo_outfits = _demo_plan_keys()
		demo_body = maxi(0, demo_bodies.find("average"))
		demo_outfit = 0
		demo_failures = 0
		_show_demo_bar(true)
		_demo_load()


func _show_demo_bar(on: bool) -> void:
	if on and demo_bar == null:
		demo_bar = HBoxContainer.new()
		demo_info = Label.new()
		demo_info.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		demo_bar.add_child(demo_info)
		var nb := Button.new()
		nb.text = "Next body"
		nb.pressed.connect(_demo_next_body)
		demo_bar.add_child(nb)
		var no := Button.new()
		no.text = "Next outfit"
		no.pressed.connect(_demo_next_outfit)
		demo_bar.add_child(no)
		var st := Button.new()
		st.text = "Stop demo"
		st.pressed.connect(_demo_stop)
		demo_bar.add_child(st)
		($UI/Root as VBoxContainer).add_child(demo_bar)
	if demo_bar != null:
		demo_bar.visible = on
		if on:
			_update_demo_label()


func _update_demo_label() -> void:
	if demo_info != null:
		demo_info.text = "%s body - %s" % [demo_bodies[demo_body % demo_bodies.size()], _plan_title(demo_outfits[demo_outfit % demo_outfits.size()])]


func _demo_load() -> bool:
	if not demo_active:
		return false
	demo_count += 1
	if demo_max > 0 and demo_count > demo_max:
		_demo_stop()
		if OS.has_environment("FL_DEMO"):
			get_tree().quit()
		return false
	demo_outfit = ((demo_outfit % demo_outfits.size()) + demo_outfits.size()) % demo_outfits.size()
	meas = (BodyGen.PRESETS[demo_bodies[demo_body % demo_bodies.size()]] as Dictionary).duplicate()
	motion = "walk"
	_set_single_garment(demo_outfits[demo_outfit % demo_outfits.size()])
	_rebuild_body_tab()
	demo_start = Time.get_ticks_msec()
	demo_pause_until = 0
	demo_last_count = 0
	demo_last_tick = demo_start
	tl_t = 0.0
	playing = true
	regen_and_rebuild()
	_update_demo_label()
	return true


func _demo_skip(why: String) -> void:
	if not demo_active:
		return
	demo_failures += 1
	status_label.text = "%s - trying the next outfit" % why
	if demo_failures >= maxi(1, demo_outfits.size()):
		status_label.text = "Demo: no garment could be simulated"
		_demo_stop()
		return
	demo_outfit += 1
	_demo_load()


func _demo_next_outfit() -> void:
	if demo_active:
		demo_failures = 0
		demo_outfit += 1
		_demo_load()


func _demo_next_body() -> void:
	if demo_active:
		demo_failures = 0
		demo_body = (demo_body + 1) % demo_bodies.size()
		_demo_load()


func _demo_stop() -> void:
	if not demo_active:
		return
	demo_active = false
	_show_demo_bar(false)
	playing = false
	tl_t = 0.0
	_reset_view()
	motion = "stand"
	regen_and_rebuild()


func _demo_tick() -> void:
	if not demo_active:
		return
	_auto_orbit_step()
	if OS.has_environment("FL_DEMO") and not playing and tl_t >= 4.0 and frames.size() >= target and target > 0:
		if demo_count >= demo_max:
			print("EDITOR demo finished %d outfits, quitting" % demo_count)
			_demo_stop()
			get_tree().quit()
			return
		_demo_next_outfit()
		return
	if not playing and tl_t >= 4.0 and frames.size() >= target and target > 0:
		if demo_pause_until == 0:
			demo_pause_until = Time.get_ticks_msec() + 2500
		elif Time.get_ticks_msec() >= demo_pause_until:
			demo_pause_until = 0
			tl_t = 0.0
			playing = true
			_sync_tl_ui()
	if sim == null and Time.get_ticks_msec() - demo_start > 5000:
		_demo_skip("simulation error")
		return
	if frames.size() != demo_last_count:
		demo_last_count = frames.size()
		demo_last_tick = Time.get_ticks_msec()
		demo_failures = 0
	elif frames.size() > 0 and Time.get_ticks_msec() - demo_last_tick > 300000:
		_demo_skip("simulation stalled")


func _placeholder_tab(tab: String, text: String) -> void:
	var l := Label.new()
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.text = text
	_tab_vbox(tab).add_child(l)


# ------------------------------------------------------------------ photo shoot
const SHOTS := [
	{"id": "front", "name": "Full length - front", "cam": [0.0, 1.0, 6.8], "tgt": [0.0, 0.86, 0.0], "fov": 18.0, "yaw": 0.0},
	{"id": "three", "name": "Three-quarter pass", "cam": [2.7, 1.12, 5.6], "tgt": [0.0, 0.87, 0.0], "fov": 20.0, "yaw": 0.12},
	{"id": "profile", "name": "Profile", "cam": [6.6, 1.02, 0.3], "tgt": [0.0, 0.87, 0.0], "fov": 20.0, "yaw": 0.0},
	{"id": "low", "name": "Low-angle hero", "cam": [0.9, 0.28, 3.8], "tgt": [0.0, 0.92, 0.0], "fov": 30.0, "yaw": -0.1},
	{"id": "waist", "name": "Waist and seams detail", "cam": [0.9, 1.0, 2.6], "tgt": [0.0, 0.86, 0.0], "fov": 17.0, "yaw": 0.2},
	{"id": "back", "name": "Back three-quarter", "cam": [-2.5, 1.1, -5.4], "tgt": [0.0, 0.87, 0.0], "fov": 20.0, "yaw": 0.0},
	{"id": "hem", "name": "Hem detail", "cam": [1.5, 0.46, 2.2], "tgt": [0.0, 0.46, 0.0], "fov": 24.0, "yaw": 0.35},
]
const SHOOT_SIZES := {"preview": [800, 1000], "standard": [1200, 1500], "high": [1600, 2000]}

var shoot_enabled := {}
var shoot_size := "standard"
var shoot_same_moment := false
var shoot_busy := false
var shoot_done := false
var gallery: Window
var gallery_grid: GridContainer


func _build_shoot_tab() -> void:
	var vb := _tab_vbox("Shoot")
	var h := Label.new()
	h.text = "Shots"
	vb.add_child(h)
	for s in SHOTS:
		shoot_enabled[s["id"]] = true
		var chk := CheckBox.new()
		chk.text = s["name"]
		chk.button_pressed = true
		chk.toggled.connect(func(on: bool) -> void: shoot_enabled[s["id"]] = on)
		vb.add_child(chk)
	vb.add_child(_h4("Resolution"))
	var o := OptionButton.new()
	o.add_item("Preview 800 x 1000", 0)
	o.add_item("Standard 1200 x 1500", 1)
	o.add_item("High 1600 x 2000", 2)
	o.select(1)
	o.item_selected.connect(func(i: int) -> void: shoot_size = ["preview", "standard", "high"][o.get_item_id(i)])
	vb.add_child(o)
	var same := CheckBox.new()
	same.text = "Same moment for every shot"
	same.toggled.connect(func(on: bool) -> void: shoot_same_moment = on)
	vb.add_child(same)
	var go := Button.new()
	go.text = "Take photos"
	go.pressed.connect(take_shots)
	vb.add_child(go)
	var note := Label.new()
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	note.text = "Each shot renders the finished drape with a long lens. With Walking motion the shots spread over the last gait cycle unless Same moment is ticked."
	vb.add_child(note)


func take_shots() -> void:
	if shoot_busy or sim == null or frames.is_empty():
		status_label.text = "Nothing to photograph yet - wait for the drape to appear"
		return
	var list := []
	for s in SHOTS:
		if shoot_enabled.get(s["id"], false):
			list.append(s)
	if list.is_empty():
		status_label.text = "Pick at least one shot"
		return
	shoot_busy = true
	status_label.text = "Shooting..."
	var kept_sel = sel
	sel = null
	_last_shown = ""
	await get_tree().process_frame
	var size: Array = SHOOT_SIZES[shoot_size]
	var saved_view := sub_view.size
	var saved_cam := [cam.position, cam.fov, cam_pivot.position, cam_pivot.rotation]
	var saved_vis := [_stitch_mesh.visible, _seam_mesh.visible]
	_stitch_mesh.visible = false
	_seam_mesh.visible = false
	sub_view.size = Vector2i(size[0], size[1])
	var last: int = frames.size() - 1
	var h: float = float(meas.get("height", 1.68)) / 1.68
	var shots := []
	for i in list.size():
		var s: Dictionary = list[i]
		status_label.text = "Shooting %d / %d: %s" % [i + 1, list.size(), s["name"]]
		var fi: int = last if shoot_same_moment or motion != "walk" else maxi(0, last - int(round(64.0 * (1.0 - float(i) / float(list.size())))))
		var fr: Dictionary = frames[mini(fi, last)]
		_set_cloth_mesh_frozen(fr["pos"])
		cur_cloth = fr["cloth"]
		body_node.show_pose(fr["body"])
		_body_key = ""
		var cc: Array = s["cam"]
		var tt: Array = s["tgt"]
		cam_pivot.position = Vector3.ZERO
		cam_pivot.rotation = Vector3(0, float(s["yaw"]), 0)
		cam.position = Vector3(float(cc[0]), float(cc[1]) * h, float(cc[2]))
		cam.fov = float(s["fov"])
		cam.look_at(Vector3(float(tt[0]), float(tt[1]) * h, float(tt[2])))
		await RenderingServer.frame_post_draw
		await RenderingServer.frame_post_draw
		var img := sub_view.get_texture().get_image()
		if img != null and not img.is_empty():
			shots.append({"name": s["name"], "id": s["id"], "img": img})
	_show_gallery(shots, size)
	sub_view.size = saved_view
	cam.position = saved_cam[0]
	cam.fov = saved_cam[1]
	cam_pivot.position = saved_cam[2]
	cam_pivot.rotation = saved_cam[3]
	_stitch_mesh.visible = saved_vis[0]
	_seam_mesh.visible = saved_vis[1]
	sel = kept_sel
	_last_shown = ""
	_show_current()
	shoot_busy = false
	status_label.text = "%d shots" % shots.size()


func _set_cloth_mesh_frozen(disp: PackedVector3Array) -> void:
	_set_cloth_mesh(disp)


func _show_gallery(shots: Array, size: Array) -> void:
	if gallery == null:
		gallery = Window.new()
		gallery.title = "Photo shoot"
		gallery.size = Vector2i(900, 600)
		gallery_grid = GridContainer.new()
		gallery_grid.columns = 3
		var scroll := ScrollContainer.new()
		scroll.set_anchors_preset(Control.PRESET_FULL_RECT)
		scroll.add_child(gallery_grid)
		gallery.add_child(scroll)
		var save_all := Button.new()
		save_all.text = "Save all..."
		save_all.pressed.connect(_on_gallery_save_all)
		var bar := HBoxContainer.new()
		bar.add_child(save_all)
		gallery.add_child(bar)
		add_child(gallery)
	for c in gallery_grid.get_children():
		c.queue_free()
	for i in shots.size():
		var cell := VBoxContainer.new()
		var tex := TextureRect.new()
		tex.texture = ImageTexture.create_from_image(shots[i]["img"])
		tex.custom_minimum_size = Vector2(260, 260 * float(size[1]) / float(size[0]))
		tex.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		tex.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		cell.add_child(tex)
		var cap := Label.new()
		cap.text = shots[i]["name"]
		cell.add_child(cap)
		var sv := Button.new()
		sv.text = "Save PNG"
		sv.pressed.connect(func() -> void: _on_gallery_save_one(shots[i]))
		cell.add_child(sv)
		gallery_grid.add_child(cell)
	gallery.popup_centered()


func _gallery_dir(cont: Callable) -> void:
	var dlg := FileDialog.new()
	dlg.file_mode = FileDialog.FILE_MODE_OPEN_DIR
	dlg.dir_selected.connect(cont)
	add_child(dlg)
	dlg.popup_centered(Vector2(600, 400))


func _on_gallery_save_all() -> void:
	_gallery_dir(func(dir: String) -> void:
		var cells := gallery_grid.get_children()
		for i in cells.size():
			var tex: TextureRect = cells[i].get_child(0)
			(tex.texture.get_image() as Image).save_png("%s/shot-%02d.png" % [dir, i + 1])
		status_label.text = "saved %d shots to %s" % [cells.size(), dir])


func _on_gallery_save_one(shot: Dictionary) -> void:
	_gallery_dir(func(dir: String) -> void:
		(shot["img"] as Image).save_png("%s/%s.png" % [dir, String(shot["id"])])
		status_label.text = "saved " + String(shot["name"]))


func _piece() -> Variant:
	return Table.piece_by_id(pieces, sel) if sel != null else null


const MIN_EDGE_M := 0.008   # edit guard: an edit that leaves an edge shorter than this is refused (table.gd MIN_SIM_EDGE is the sim gate)
const SIDE_NAMES := [[0.0, "front"], [90.0, "left side"], [180.0, "back"], [270.0, "right side"]]
var clip = null             # Ctrl+C: the copied piece (a dictionary), pasted by Ctrl+V
var _offer := {}            # {"pid", "text", "button"}: a copy or mirror that covers its original's place on the body


func _build_piece_tab() -> void:
	_refresh_piece_tab()


func _refresh_piece_tab() -> void:
	var vb := _tab_vbox("Piece")
	if vb == null:
		return
	for c in vb.get_children():
		c.queue_free()
	var h := Label.new()
	h.text = "Selected piece"
	vb.add_child(h)
	var p = _piece()
	if p == null:
		var none := Label.new()
		none.text = "Click a piece on the cutting table."
		vb.add_child(none)
	else:
		_piece_rows(vb, p)
	var add_h := Label.new()
	add_h.text = "Add a piece"
	vb.add_child(add_h)
	var add_row := HBoxContainer.new()
	for kind in [["Rectangle", "rect"], ["Trapezoid", "trap"], ["Triangle", "tri"]]:
		var b := Button.new()
		b.text = kind[0]
		b.pressed.connect(func() -> void: push_undo(); Table.add_piece(self, kind[1]); table.fit(); changed("structure"))
		add_row.add_child(b)
	vb.add_child(add_row)
	_lock_editing(vb)


func _piece_rows(vb: VBoxContainer, p: Dictionary) -> void:
	var name_row := HBoxContainer.new()
	var nl := Label.new()
	nl.custom_minimum_size = Vector2(92, 0)
	nl.text = "Name"
	name_row.add_child(nl)
	var ne := LineEdit.new()
	ne.text = String(p.get("name", p["id"]))
	ne.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	ne.text_submitted.connect(func(t: String) -> void: push_undo(); p["name"] = t; changed("structure"))
	name_row.add_child(ne)
	vb.add_child(name_row)
	# size of the whole piece, typed in cm: width keeps the left edge, height keeps the bottom edge
	var bb := Table.bbox_of(p["outline"])
	vb.add_child(_h4("Size of the piece (cm), Enter to apply"))
	var sz := HBoxContainer.new()
	sz.add_child(_caption("Width"))
	var sw := _line_edit("SizeW", "%.1f" % ((bb[2] - bb[0]) * 100.0))
	sw.text_submitted.connect(func(t: String) -> void: _on_size(p, 0, t, sw))
	sz.add_child(sw)
	sz.add_child(_caption("Height"))
	var sh := _line_edit("SizeH", "%.1f" % ((bb[3] - bb[1]) * 100.0))
	sh.text_submitted.connect(func(t: String) -> void: _on_size(p, 1, t, sh))
	sz.add_child(sh)
	vb.add_child(sz)
	var area := Label.new()
	area.text = "Area %.0f cm²" % (absf(Table.signed_area(p["outline"])) * 1e4)
	vb.add_child(area)
	vb.add_child(_h4("Grain (warp direction)"))
	_slider_row(vb, "Angle", 0.0, 180.0, 1.0,
		func() -> float: return _grain_deg(),
		func(v: float) -> void: _sel_wrap_set("grain_deg", v),
		func(v: float) -> String: return "%d°" % int(v),
		"Direction of the threads, shown as the arrow on the table.", true)
	var gb := HBoxContainer.new()
	for g in [["Straight 90°", 90.0], ["Cross 0°", 0.0], ["Bias 45°", 45.0], ["Bias 135°", 135.0]]:
		var b := Button.new()
		b.text = g[0]
		b.pressed.connect(func() -> void: push_undo(); p["grain"] = float(g[1]) * PI / 180.0; _sym_sync(p); changed("structure"))
		gb.add_child(b)
	vb.add_child(gb)
	# corners and edges: typed values
	var n := (p["outline"] as Array).size()
	if sel_v >= 0 and sel_v < n:
		vb.add_child(_h4("Corner %d (x, y in cm)" % sel_v))
		_corner_row(vb, p, sel_v)
	vb.add_child(_edge_fold(p))
	# placement on the body, with plain labels
	vb.add_child(_h4("Placement on the body"))
	var pb := HBoxContainer.new()
	for pl in [["Front", 0.0], ["Back", 180.0], ["Left side", 90.0], ["Right side", 270.0]]:
		var b := Button.new()
		b.name = "Place" + String(pl[0]).replace(" ", "")
		b.text = pl[0]
		b.tooltip_text = "Puts the piece on the %s of the body (Around %d°)." % [String(pl[0]).to_lower(), int(pl[1])]
		b.pressed.connect(func() -> void: _place(p, float(pl[1])))
		pb.add_child(b)
	vb.add_child(pb)
	if not _offer.is_empty() and String(_offer.get("pid", "")) == String(p["id"]):
		var ol := Label.new()
		ol.text = String(_offer["text"])
		ol.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		vb.add_child(ol)
		var orow := HBoxContainer.new()
		var fb := Button.new()
		fb.name = "FlipOffer"
		fb.text = String(_offer["button"])
		fb.pressed.connect(func() -> void: _flip_placement(p))
		orow.add_child(fb)
		var kb := Button.new()
		kb.text = "Keep it here"
		kb.pressed.connect(func() -> void: _offer = {}; changed("ui"))
		orow.add_child(kb)
		vb.add_child(orow)
	_slider_row(vb, "Around", 0.0, 360.0, 1.0,
		func() -> float: return float((p["wrap"] as Dictionary).get("theta", 0.0)),
		func(v: float) -> void: _wrap_set(p, "theta", v),
		func(v: float) -> String: return "%d°" % int(v),
		"Where the piece sits around the body: 0° front, 90° the body's left side, 180° back, 270° right side.", true)
	_slider_row(vb, "Height from floor", 0.0, 150.0, 1.0,
		func() -> float: return float((p["wrap"] as Dictionary).get("y", 0.5)) * 100.0,
		func(v: float) -> void: _wrap_set(p, "y", v / 100.0),
		func(v: float) -> String: return "%d cm" % int(v),
		"Height of the piece's lowest edge above the floor, in cm.", true)
	_slider_row(vb, "Clearance to body", 0.0, 40.0, 1.0,
		func() -> float: return float((p["wrap"] as Dictionary).get("clear", 0.012)) * 1000.0,
		func(v: float) -> void: _wrap_set(p, "clear", v / 1000.0),
		func(v: float) -> String: return "%d mm" % int(v),
		"Gap between the fabric and the body surface, in mm. Each layer over another garment adds 8 mm on its own.", true)
	_slider_row(vb, "Flare", 0.0, 6.0, 0.1,
		func() -> float: return float((p["wrap"] as Dictionary).get("ring", 0.0)),
		func(v: float) -> void: _wrap_set(p, "ring", v),
		func(v: float) -> String: return "%.1f" % v,
		"How far the piece flares out from the body: 0 hugs it, higher values open it out, like a skirt.", true)
	_twin_section(vb, p)
	var btns := HBoxContainer.new()
	var mb := Button.new()
	mb.text = "Mirror"
	mb.tooltip_text = "Flips the piece over its own centre line, in place."
	mb.pressed.connect(func() -> void: _mirror(p))
	btns.add_child(mb)
	var db := Button.new()
	db.text = "Duplicate"
	db.tooltip_text = "Adds a copy beside this piece."
	db.pressed.connect(func() -> void: _duplicate(p))
	btns.add_child(db)
	var del := Button.new()
	del.text = "Delete"
	del.tooltip_text = "Deletes the piece and any seams on it. Ctrl+Z restores it."
	del.pressed.connect(func() -> void:
		push_undo()
		var gone := Table.remove_piece(self, String(p["id"]))
		notify("Deleted %s%s. Ctrl+Z restores it." % [String(p.get("name", p["id"])), (" and its %d seam(s)" % gone) if gone > 0 else ""])
		changed("structure"))
	btns.add_child(del)
	vb.add_child(btns)
	var cbtns := HBoxContainer.new()
	var cp := Button.new()
	cp.text = "Copy"
	cp.tooltip_text = "Copies the piece (Ctrl+C). Paste with Ctrl+V or the Paste button."
	cp.pressed.connect(func() -> void: copy_piece())
	cbtns.add_child(cp)
	var pa := Button.new()
	pa.text = "Paste"
	pa.tooltip_text = "Pastes a copy of the copied piece to the right of the table (Ctrl+V)."
	pa.disabled = clip == null
	pa.pressed.connect(func() -> void: paste_piece())
	cbtns.add_child(pa)
	vb.add_child(cbtns)


func _caption(text: String) -> Label:
	var l := Label.new()
	l.text = text
	l.custom_minimum_size = Vector2(36, 0)
	return l


func _line_edit(node_name: String, value_text: String) -> LineEdit:
	var le := LineEdit.new()
	le.name = node_name
	le.text = value_text
	le.custom_minimum_size = Vector2(44, 0)
	le.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	le.select_all_on_focus = true
	return le


# A typed number (commas allowed). NAN when it is not one; _parse_cm also rejects zero and below.
static func _parse_num(t: String) -> float:
	var s := t.replace(",", ".").strip_edges()
	return s.to_float() if s.is_valid_float() else NAN


static func _parse_cm(t: String) -> float:
	var v := _parse_num(t)
	return v if v > 0.0 else NAN


func _corner_row(vb: VBoxContainer, p: Dictionary, vi: int) -> void:
	var q: Array = p["outline"][vi]
	var row := HBoxContainer.new()
	row.add_child(_caption("x"))
	var fx := _line_edit("CornerX", "%.1f" % (float(q[0]) * 100.0))
	var fy := _line_edit("CornerY", "%.1f" % (float(q[1]) * 100.0))
	var on_enter := func(_t: String) -> void: _on_corner(p, vi, fx, fy)
	fx.text_submitted.connect(on_enter)
	fy.text_submitted.connect(on_enter)
	row.add_child(fx)
	row.add_child(_caption("y"))
	row.add_child(fy)
	vb.add_child(row)


# Numeric entry. A change is checked before anything moves: every edge it touches must stay at least 8 mm.
# An accepted entry is one undo step; a rejected one puts the old text back and says why in the status line.
func _try_outline(p: Dictionary, outline: Array, edges: Array, why: String) -> bool:
	if not _outline_ok(outline, edges):
		status_label.text = why
		return false
	push_undo()
	p["outline"] = outline
	_sym_sync(p)
	changed("structure")
	return true


func _outline_ok(o: Array, edges: Array) -> bool:
	for e in edges:
		if Table.edge_len(o, int(e)) < MIN_EDGE_M:
			return false
	return true


# Edge length: the corner after the edge moves along the edge's own direction; the near corner stays put.
func _on_edge_len(p: Dictionary, e: int, t: String, le: LineEdit) -> void:
	var o: Array = p["outline"]
	var cur := Table.edge_len(o, e)
	var restore := "%.1f" % (cur * 100.0)
	var cm := _parse_cm(t)
	if is_nan(cm) or cur < 1e-9:
		status_label.text = "Edge %d: type a length in cm, for example 62.5" % (e + 1)
		le.text = restore
		return
	if absf(cm / 100.0 - cur) < 1e-6:
		le.text = restore
		return
	var n := o.size()
	var bi := (e + 1) % n
	var a: Array = o[e]
	var b: Array = o[bi]
	var ux := (float(b[0]) - float(a[0])) / cur
	var uy := (float(b[1]) - float(a[1])) / cur
	var out: Array = o.duplicate(true)
	out[bi] = [float(a[0]) + ux * cm / 100.0, float(a[1]) + uy * cm / 100.0]
	if not _try_outline(p, out, [e, bi], "Edge %d: %.1f cm would leave an edge shorter than 0.8 cm" % [e + 1, cm]):
		le.text = restore


func _on_corner(p: Dictionary, vi: int, fx: LineEdit, fy: LineEdit) -> void:
	var o: Array = p["outline"]
	var q: Array = o[vi]
	var x := _parse_num(fx.text)
	var y := _parse_num(fy.text)
	if is_nan(x) or is_nan(y):
		status_label.text = "Corner %d: type x and y in cm" % vi
		fx.text = "%.1f" % (float(q[0]) * 100.0)
		fy.text = "%.1f" % (float(q[1]) * 100.0)
		return
	var out: Array = o.duplicate(true)
	out[vi] = [x / 100.0, y / 100.0]
	var n := o.size()
	if not _try_outline(p, out, [(vi - 1 + n) % n, vi], "Corner %d would make an edge shorter than 0.8 cm" % vi):
		fx.text = "%.1f" % (float(q[0]) * 100.0)
		fy.text = "%.1f" % (float(q[1]) * 100.0)


# Width (axis 0) or height (axis 1) of the whole piece, scaled about its left or bottom edge.
func _on_size(p: Dictionary, axis: int, t: String, le: LineEdit) -> void:
	var b := Table.bbox_of(p["outline"])
	var cur: float = (b[2] - b[0]) if axis == 0 else (b[3] - b[1])
	var restore := "%.1f" % (cur * 100.0)
	var cm := _parse_cm(t)
	if is_nan(cm) or cur < 1e-9:
		status_label.text = "Type the %s in cm, for example 40" % ("width" if axis == 0 else "height")
		le.text = restore
		return
	var k := (cm / 100.0) / cur
	var out: Array = (p["outline"] as Array).duplicate(true)
	var all := []
	for i in out.size():
		all.append(i)
		var q: Array = out[i]
		if axis == 0:
			q[0] = b[0] + (float(q[0]) - float(b[0])) * k
		else:
			q[1] = b[1] + (float(q[1]) - float(b[1])) * k
	if not _try_outline(p, out, all, "That size would make an edge shorter than 0.8 cm"):
		le.text = restore


# Edges section: one row per edge with a cm field. Open by default.
func _edge_fold(p: Dictionary) -> VBoxContainer:
	var o: Array = p["outline"]
	var body := VBoxContainer.new()
	body.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var tip := Label.new()
	tip.text = "Type a length and press Enter. The corner after the edge moves along it."
	tip.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	body.add_child(tip)
	for i in o.size():
		var r := HBoxContainer.new()
		var l := _caption("Edge %d" % (i + 1))
		l.custom_minimum_size = Vector2(60, 0)
		r.add_child(l)
		var le := _line_edit("EdgeLen_%d" % i, "%.1f" % (Table.edge_len(o, i) * 100.0))  # node name is 0-based; the label is 1-based
		le.text_submitted.connect(func(t: String) -> void: _on_edge_len(p, i, t, le))
		r.add_child(le)
		r.add_child(_caption("cm"))
		var partner := _seam_partner(p, i)
		if partner != "":
			var sl := _caption(partner)
			sl.custom_minimum_size = Vector2(0, 0)
			sl.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
			sl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			r.add_child(sl)
		body.add_child(r)
	return _fold_box("Edges (%d)" % o.size(), body, o.size())


func _seam_partner(p: Dictionary, e: int) -> String:
	var pid := String(p["id"])
	for si in seams.size():
		var s: Dictionary = seams[si]
		for pair in [["a", "b"], ["b", "a"]]:
			if String(s[pair[0]][0]) == pid and int(s[pair[0]][1]) == e:
				var other = Table.piece_by_id(pieces, String(s[pair[1]][0]))
				var oname := String(other.get("name", "?")) if other != null else "?"
				return "Seam %d: sewn to %s edge %d" % [si + 1, oname, int(s[pair[1]][1]) + 1]
	return ""


func _fold_box(title: String, body: Control, rows: int) -> VBoxContainer:
	var box := VBoxContainer.new()
	var head := Button.new()
	head.toggle_mode = true
	head.button_pressed = _fold_open
	head.alignment = HORIZONTAL_ALIGNMENT_LEFT
	head.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.text = ("[-] " if _fold_open else "[+] ") + title
	box.add_child(head)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.custom_minimum_size = Vector2(0, 26.0 * minf(float(rows), 8.0) + 8.0)
	scroll.visible = _fold_open
	scroll.add_child(body)
	box.add_child(scroll)
	head.toggled.connect(func(on: bool) -> void:
		_fold_open = on
		head.text = ("[-] " if on else "[+] ") + title
		scroll.visible = on)
	return box


# ------------------------------------------------------------------ placement
func _place(p: Dictionary, theta: float) -> void:
	if String(_offer.get("pid", "")) == String(p["id"]):
		_offer = {}
	push_undo()
	(p["wrap"] as Dictionary)["theta"] = fposmod(theta, 360.0)
	_sym_sync(p)
	changed("structure")


func _flip_placement(p: Dictionary) -> void:
	_place(p, float((p["wrap"] as Dictionary).get("theta", 0.0)) + 180.0)


func _wrap_set(p: Dictionary, key: String, v: float) -> void:
	(p["wrap"] as Dictionary)[key] = v
	_sym_sync(p)
	changed("structure-drag")


static func _angle_gap(a: float, b: float) -> float:
	var d := fposmod(absf(a - b), 360.0)
	return minf(d, 360.0 - d)


static func _side_name(theta: float) -> String:
	var best := "front"
	var bd := 1e9
	for s in SIDE_NAMES:
		var d := _angle_gap(theta, float(s[0]))
		if d < bd:
			bd = d
			best = String(s[1])
	return best


# vertical extent of a piece on the body: from its height from the floor to that plus its height
func _band(p: Dictionary) -> Array:
	var b := Table.bbox_of(p["outline"])
	var y0 := float((p["wrap"] as Dictionary).get("y", 0.5))
	return [y0, y0 + (float(b[3]) - float(b[1]))]


# two placements cover the same part of the body: within 45 degrees round, and overlapping in height
func _placed_over(a: Dictionary, ta: float, b: Dictionary, tb: float) -> bool:
	if _angle_gap(ta, tb) >= 45.0:
		return false
	var ya := _band(a)
	var yb := _band(b)
	return float(ya[0]) < float(yb[1]) and float(yb[0]) < float(ya[1])


# after a copy or a mirror: if the piece now covers its original's place, offer to flip it to the other side
func _offer_flip(a: Dictionary, ta: float, b: Dictionary, tb: float, what: String) -> void:
	if not _placed_over(a, ta, b, tb):
		return
	var target := fposmod(tb + 180.0, 360.0)
	_offer = {"pid": String(b["id"]),
		"text": "%s covers the same part of the body as the original (Around %d°)." % [what, int(round(tb))],
		"button": "Flip to the %s (Around %d°)" % [_side_name(target), int(round(target))]}


func _duplicate(p: Dictionary) -> void:
	var theta := float((p["wrap"] as Dictionary).get("theta", 0.0))
	push_undo()
	var q := Table.duplicate_piece(self, p)
	_offer_flip(p, theta, q, float((q["wrap"] as Dictionary).get("theta", 0.0)), "The copy")
	changed("structure")


func _mirror(p: Dictionary) -> void:
	var before := float((p["wrap"] as Dictionary).get("theta", 0.0))
	push_undo()
	Table.mirror_piece(p, seams)
	_sym_sync(p)
	_offer_flip(p, before, p, float((p["wrap"] as Dictionary).get("theta", 0.0)), "The mirrored piece")
	changed("structure")


# ------------------------------------------------------------------ copy, paste, symmetric twin
func copy_piece() -> void:
	var p = _piece()
	if p == null:
		return
	clip = JSON.parse_string(JSON.stringify(p))
	clip.erase("sym")
	status_label.text = "Copied %s. Ctrl+V pastes a copy." % String(p.get("name", p["id"]))


func paste_piece() -> void:
	if clip == null or active_locked():
		return
	push_undo()
	var q: Dictionary = JSON.parse_string(JSON.stringify(clip))
	q["id"] = Table.new_id(pieces, "p")
	q["name"] = String(clip.get("name", "?")) + " copy"
	q.erase("sym")
	var b := Table.bbox_of(q["outline"])
	var x := 0.0
	if not pieces.is_empty():
		var right := -1e9
		for other in pieces:
			var ob := Table.bbox_of(other["outline"])
			right = maxf(right, float(ob[2]) + float((other.get("table", {}) as Dictionary).get("x", 0.0)))
		x = right + 0.12 - float(b[0])
	q["table"] = {"x": x, "y": 0.0}
	pieces.append(q)
	sel = q["id"]
	sel_v = -1
	changed("structure")


func _sym_sync(p: Dictionary) -> void:
	Table.sync_twin(self, p)


func _twin_of(p: Dictionary):
	var sy = p.get("sym", null)
	if not (sy is Dictionary):
		return null
	return Table.piece_by_id(pieces, String(sy.get("twin", "")))


func _set_link_on(p: Dictionary, on: bool) -> void:
	if p.get("sym") is Dictionary:
		(p["sym"] as Dictionary)["on"] = on


func _make_twin(p: Dictionary) -> void:
	push_undo()
	var b := Table.bbox_of(p["outline"])
	var pid := String(p["id"])
	var tid := Table.new_id(pieces, "p")
	var tw: Dictionary = JSON.parse_string(JSON.stringify(p))
	tw["id"] = tid
	tw["name"] = String(p.get("name", pid)) + " (mirror)"
	tw.erase("sym")
	pieces.append(tw)
	var axis := (float(b[0]) + float(b[2])) * 0.5
	var dx := (float(b[2]) - float(b[0])) + 0.1
	p["sym"] = {"twin": tid, "axis": axis, "dx": dx, "on": true}
	tw["sym"] = {"twin": pid, "axis": axis, "dx": dx, "on": true}
	Table.sync_twin(self, p)
	changed("structure")


func _set_sym_on(p: Dictionary, on: bool) -> void:
	var tw = _twin_of(p)
	if tw == null:
		return
	push_undo()
	_set_link_on(p, on)
	_set_link_on(tw, on)
	if on:
		_sym_sync(p)
	changed("structure")


func _unlink_twin(p: Dictionary) -> void:
	push_undo()
	var tw = _twin_of(p)
	p.erase("sym")
	if tw != null:
		tw.erase("sym")
	changed("structure")


func _twin_section(vb: VBoxContainer, p: Dictionary) -> void:
	vb.add_child(_h4("Mirror twin"))
	var tw = _twin_of(p)
	if tw == null:
		var mt := Button.new()
		mt.name = "MakeTwin"
		mt.text = "Make a mirror twin"
		mt.tooltip_text = "Adds the mirror image of this piece beside it. With Symmetric edits on, a change to either piece is copied to the other."
		mt.pressed.connect(func() -> void: _make_twin(p))
		vb.add_child(mt)
		return
	var tl := Label.new()
	tl.text = "Twin: %s" % String(tw.get("name", tw["id"]))
	vb.add_child(tl)
	var cb := CheckBox.new()
	cb.name = "SymToggle"
	cb.text = "Symmetric edits"
	cb.tooltip_text = "Copies every change (corners, edge lengths, size, grain, placement) to the twin, as a mirror image."
	cb.button_pressed = bool((p["sym"] as Dictionary).get("on", false))
	cb.toggled.connect(func(on: bool) -> void: _set_sym_on(p, on))
	vb.add_child(cb)
	if (tw["outline"] as Array).size() != (p["outline"] as Array).size():
		var warn := Label.new()
		warn.text = "Out of step: the two pieces have different numbers of corners, so changes are not copied."
		warn.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		vb.add_child(warn)
	var ul := Button.new()
	ul.text = "Unlink twin"
	ul.pressed.connect(func() -> void: _unlink_twin(p))
	vb.add_child(ul)


func _grain_deg() -> float:
	var p = _piece()
	if p == null:
		return 0.0
	return fposmod(float(p.get("grain", 0.0)) * 180.0 / PI, 180.0)


func _sel_wrap_set(which: String, v: float) -> void:
	var p = _piece()
	if p == null:
		return
	if which == "grain_deg":
		p["grain"] = v * PI / 180.0
		_sym_sync(p)
	changed("structure-drag")


# tip: hover text for the label and slider. undo_step: each drag, or each key step, is one undo step. Piece sliders pass true; body and fabric sliders do not.
func _slider_row(parent: VBoxContainer, label_text: String, minv: float, maxv: float, step: float, get: Callable, set: Callable, fmt: Callable, tip: String = "", undo_step := false) -> HSlider:
	var h := HBoxContainer.new()
	var l := Label.new()
	l.custom_minimum_size = Vector2(92, 0)
	l.text = label_text
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.tooltip_text = tip
	h.add_child(l)
	var s := HSlider.new()
	s.min_value = minv
	s.max_value = maxv
	s.step = step
	s.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	s.value = get.call()
	h.add_child(s)
	var o := Label.new()
	o.custom_minimum_size = Vector2(64, 0)
	o.text = fmt.call(s.value)
	h.add_child(o)
	var dragging := [false]
	if undo_step:
		s.drag_started.connect(func() -> void:
			dragging[0] = true
			push_undo())
		s.drag_ended.connect(func(_c: bool) -> void: dragging[0] = false)
	s.value_changed.connect(func(v: float) -> void:
		o.text = fmt.call(v)
		if undo_step and not dragging[0]:
			push_undo()  # keyboard step
		set.call(v))
	s.tooltip_text = tip
	parent.add_child(h)
	return s


func _build_body_tab() -> void:
	_rebuild_body_tab()


func _rebuild_body_tab() -> void:
	var vb := _tab_vbox("Body")
	for c in vb.get_children():
		c.queue_free()
	_build_body_tab_content()


func _build_body_tab_content() -> void:
	var vb := _tab_vbox("Body")
	var h := Label.new()
	h.text = "Preset"
	vb.add_child(h)
	var grid := GridContainer.new()
	grid.columns = 3
	vb.add_child(grid)
	for p in ["petite", "average", "tall", "curvy", "athletic", "plus"]:
		var b := Button.new()
		b.text = p.capitalize()
		b.pressed.connect(func() -> void: _on_preset_button(p))
		grid.add_child(b)
	vb.add_child(_h4("Measurements"))
	for spec in [["height", "Height", 140.0, 200.0], ["bust", "Bust", 70.0, 140.0], ["waist", "Waist", 55.0, 130.0], ["hip", "Hip", 75.0, 145.0], ["shoulder", "Shoulder", 30.0, 48.0]]:
		_slider_row(vb, spec[1], spec[2], spec[3], 0.5,
			func() -> float: return float(meas.get(spec[0], 0.0)) * 100.0,
			func(v: float) -> void: meas[spec[0]] = v / 100.0; _schedule(),
			func(v: float) -> String: return "%.1f cm" % v)
	vb.add_child(_h4("More measurements"))
	for spec in [["arm", "Arm length", 44.0, 68.0], ["inseam", "Inseam", 62.0, 96.0], ["neck", "Neck", 28.0, 48.0], ["thigh", "Thigh", 40.0, 85.0], ["upperarm", "Upper arm", 20.0, 45.0], ["back", "Back length", 30.0, 48.0]]:
		var row := HBoxContainer.new()
		var l := Label.new()
		l.custom_minimum_size = Vector2(92, 0)
		l.text = spec[1]
		row.add_child(l)
		var s := HSlider.new()
		s.min_value = spec[2]
		s.max_value = spec[3]
		s.step = 0.5
		s.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(s)
		var o := Label.new()
		o.custom_minimum_size = Vector2(64, 0)
		row.add_child(o)
		var auto := Button.new()
		auto.text = "auto"
		row.add_child(auto)
		vb.add_child(row)
		_sync_sec_row(spec[0], s, o, auto)
		s.value_changed.connect(func(v: float) -> void: meas[spec[0]] = v / 100.0; _schedule(); _sync_sec_row(spec[0], s, o, auto))
		auto.pressed.connect(func() -> void: meas[spec[0]] = null; _schedule(); _sync_sec_row(spec[0], s, o, auto))
	vb.add_child(_h4("Body type"))
	_slider_row(vb, "Muscle", 0.0, 100.0, 1.0,
		func() -> float: return float(meas.get("muscle", 0.5)) * 100.0,
		func(v: float) -> void: meas["muscle"] = v / 100.0; _schedule(),
		func(v: float) -> String: return "%d%%" % int(v))
	for spec in [["cup", "Bust shape"], ["belly", "Belly"]]:
		_slider_row(vb, spec[1], -100.0, 100.0, 1.0,
			func() -> float: return float(meas.get(spec[0], 0.0)) * 100.0,
			func(v: float) -> void: meas[spec[0]] = v / 100.0; _schedule(),
			func(v: float) -> String: return ("+" if v > 0.0 else "") + str(int(v)))
	var note := Label.new()
	note.name = "BodyNote"
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vb.add_child(note)


func _h4(text: String) -> Label:
	var l := Label.new()
	l.text = text
	return l


# Collapsed-by-default section: a toggle header and a scroll box that shows about six rows when
# open. Long lists (one row per edge) stay available without stretching the side panel. The
# open/closed state survives the tab rebuilds that happen on every edit.
func _sync_sec_row(key: String, s: HSlider, o: Label, auto: Button) -> void:
	var v = meas.get(key, null)
	var is_auto: bool = v == null
	var disp: float = float(v) * 100.0 if not is_auto else (float(body_stand["m"].get(key, 0.0)) * 100.0 if body_stand != null else 0.0)
	s.set_value_no_signal(disp)
	o.text = "auto" if is_auto else ("%.1f cm" % disp)
	auto.text = "auto" if is_auto else "↺"


func _build_fabric_tab() -> void:
	var vb := _tab_vbox("Fabric")
	var l := Label.new()
	l.text = "Fabric"
	vb.add_child(l)
	var o := OptionButton.new()
	o.name = "FabricOpt"
	var fab: Dictionary = PlanLoader.load_json("res://materials/fabrics.json")
	for f in fab.get("fabrics", []):
		o.add_item(String(f["name"]))
		o.set_item_metadata(o.item_count - 1, String(f["id"]))
	vb.add_child(o)
	o.item_selected.connect(func(i: int) -> void: _set_garment_fabric(active, String(o.get_item_metadata(i))))
	vb.add_child(_h4("Seams and solver"))
	_slider_row(vb, "Seam k", 2.0, 50.0, 1.0,
		func() -> float: return seam_stiffness / 1000.0,
		func(v: float) -> void: seam_stiffness = v * 1000.0; _schedule(),
		func(v: float) -> String: return "%d kN/m" % int(v))
	_slider_row(vb, "Pull-in", 0.0, 60.0, 1.0,
		func() -> float: return float(ramp_frames),
		func(v: float) -> void: ramp_frames = int(v); _schedule(),
		func(v: float) -> String: return "%d fr" % int(v))
	var chk := CheckBox.new()
	chk.name = "SelfCheck"
	chk.text = "self collision (slower)"
	chk.button_pressed = self_collision
	chk.toggled.connect(func(on: bool) -> void: self_collision = on; _schedule())
	vb.add_child(chk)
	var note := Label.new()
	note.name = "FabricNote"
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vb.add_child(note)
	var plan_note := Label.new()
	plan_note.name = "PlanNote"
	plan_note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vb.add_child(plan_note)
	_refresh_fabric_tab()


func _refresh_fabric_tab() -> void:
	var vb := _tab_vbox("Fabric")
	var o: OptionButton = vb.get_node("FabricOpt")
	for i in o.item_count:
		if String(o.get_item_metadata(i)) == fabric_id:
			o.select(i)
	var fab: Dictionary = PlanLoader.load_json("res://materials/fabrics.json")
	var f := {}
	for ff in fab.get("fabrics", []):
		if String(ff["id"]) == fabric_id:
			f = ff
	var note: Label = vb.get_node("FabricNote")
	if f.is_empty():
		note.text = ""
		return
	var pr: Dictionary = f["params"]
	note.text = "%s\nWeight %d g/m²\nThickness %.2f mm\nStretch %d / %d N/m\nShear %d N/m/rad\nBending %.1f / %.1f µN·m" % [
		String(f.get("notes", "")), int(pr["density_kg_m2"] * 1000.0), float(pr["thickness_m"]) * 1000.0,
		int(pr["tensile_warp_N_m"]), int(pr["tensile_weft_N_m"]), int(pr["shear_N_m_rad"]),
		float(pr["bending_warp_Nm"]) * 1e6, float(pr["bending_weft_Nm"]) * 1e6]
	(vb.get_node("PlanNote") as Label).text = "%s - %s" % [plan_name, plan_notes]


# ------------------------------------------------------------------ state changes
func _schedule() -> void:
	_store_active()
	debounce.start()


func snapshot() -> String:
	return JSON.stringify({"pieces": pieces, "seams": seams})


func push_undo() -> void:
	_notice = ""
	undo.append(snapshot())
	if undo.size() > 60:
		undo.pop_front()
	redo.clear()


func do_undo() -> void:
	if undo.is_empty() or active_locked():
		return
	_notice = ""
	redo.append(snapshot())
	var o = JSON.parse_string(undo.pop_back())
	pieces = o["pieces"]
	seams = o["seams"]
	_fix_selection()
	changed("history")


func do_redo() -> void:
	if redo.is_empty() or active_locked():
		return
	_notice = ""
	undo.append(snapshot())
	var o = JSON.parse_string(redo.pop_back())
	pieces = o["pieces"]
	seams = o["seams"]
	_fix_selection()
	changed("history")


# Shows a warning in the status line. The next edit or undo clears it.
func notify(msg: String) -> void:
	_notice = msg
	if status_label != null:
		status_label.text = msg


func _status_suffix() -> String:
	return "" if _notice == "" else "\n" + _notice


func _fix_selection() -> void:
	if sel == null or Table.piece_by_id(pieces, String(sel)) == null:
		sel = null
	sel_v = -1
	sew_a = null


# kind: load (new plan), structure (a pattern edit by the user: marks the garment edited, rebuild sim),
# structure-drag (the same, debounced), history (undo/redo), outfit (layers, visibility: rebuild, not an edit),
# table/table-done (table moved), ui (redraw), body/fabric (regen + rebuild)
func changed(kind: String) -> void:
	table_dirty = true
	_store_active()
	if kind == "structure" or kind == "structure-drag" or kind == "table-done":  # table-done: a piece was moved
		if active >= 0 and active < outfit.size():
			outfit[active]["edited"] = true  # later rebuilds keep the user's pieces (see _redraft_drafted)
	if kind == "load":
		_drain()  # the worker may be stepping the old sim: let it finish before the sim is dropped
		sim = null
		playing = false
		_notice = ""
		undo.clear()
		redo.clear()
		table.fitted = false
		table.fit()
		_refresh_garment_tabs()
		regen_and_rebuild()
	elif kind in ["structure", "body", "fabric", "outfit", "history"]:
		_refresh_garment_tabs()
		_schedule()
	elif kind == "structure-drag":
		# fires on every drag motion: no tab rebuild here (it frees the controls being dragged).
		# The debounced regen refreshes the tabs once the drag pauses; a vertex drag ends in "structure".
		_schedule()
	elif kind == "table":
		pass  # piece moved on the mat: table_dirty is enough, tabs refresh on "table-done"
	elif kind == "table-done" or kind == "ui":
		_refresh_garment_tabs()


func _refresh_garment_tabs() -> void:
	_refresh_piece_tab()
	_refresh_seams_tab()
	_refresh_outfit_tab()


# ------------------------------------------------------------------ outfit
func active_uid() -> int:
	return int(outfit[active]["uid"]) if active >= 0 and active < outfit.size() else -1


func active_locked() -> bool:
	return active >= 0 and active < outfit.size() and bool(outfit[active]["locked"])


func _index_of_uid(uid: int) -> int:
	for i in outfit.size():
		if int(outfit[i]["uid"]) == uid:
			return i
	return -1


# Copies the working copy (pieces, seams, plan, fabric, name) into the active garment's entry.
func _store_active() -> void:
	if active < 0 or active >= outfit.size():
		return
	var e: Dictionary = outfit[active]
	e["key"] = plan_key
	e["name"] = plan_name
	e["notes"] = plan_notes
	e["plan"] = plan
	e["pieces"] = pieces
	e["seams"] = seams
	e["fabric"] = fabric_id


static func _redraft_key(key: String, p: Dictionary) -> String:
	if key in Library.DRAFTED or key.begins_with("fs-"):
		return key
	if Library.is_user(key):
		return String(p.get("from", ""))  # a saved garment remembers what it was drafted from
	return ""


# A new entry (uid not yet set) for plan p, fabric and colour from the plan.
func _entry_from_plan(key: String, p: Dictionary) -> Dictionary:
	var fab := String(p.get("fabric", "cotton_poplin"))
	if fab == "":
		fab = "cotton_poplin"
	return {"uid": -1, "key": key, "redraft": _redraft_key(key, p), "name": _display_name(key, p), "edited": false,
		"notes": String(p.get("notes", "")), "fabric": fab, "color": String(Table.FABRIC_COLORS.get(fab, "#9fb7d6")),
		"visible": true, "locked": false, "fitted": _fs_preset(meas), "plan": p,
		"pieces": (p.get("pieces", []) as Array).duplicate(true), "seams": (p.get("seams", []) as Array).duplicate(true)}


# Appends a garment to the outfit (outermost) and makes it the active one.
func _add_garment_entry(key: String, p: Dictionary) -> void:
	_store_active()
	var e := _entry_from_plan(key, p)
	_next_uid += 1
	e["uid"] = _next_uid
	outfit.append(e)
	_activate(outfit.size() - 1, false)


# Replaces the active garment with plan p (keeps its uid, visibility and lock). Appends when the outfit is empty.
func _replace_active(key: String, p: Dictionary) -> void:
	if active < 0 or active >= outfit.size():
		_add_garment_entry(key, p)
		return
	var old: Dictionary = outfit[active]
	var e := _entry_from_plan(key, p)
	e["uid"] = old["uid"]
	e["visible"] = old["visible"]
	e["locked"] = old["locked"]
	outfit[active] = e
	_activate(active, false)


# Loads outfit[i] into the working copy. store = false when the working copy is already the entry's (or the entry is
# being replaced). Index -1 leaves the cutting table empty.
func _activate(i: int, store: bool = true) -> void:
	if store:
		_store_active()
	active = i if i >= 0 and i < outfit.size() else -1
	if active < 0:
		pieces = []
		seams = []
		plan = {}
		plan_key = ""
		plan_name = ""
		plan_notes = ""
	else:
		var e: Dictionary = outfit[active]
		pieces = e["pieces"]
		seams = e["seams"]
		plan = e["plan"]
		plan_key = String(e["key"])
		plan_name = String(e["name"])
		plan_notes = String(e["notes"])
		fabric_id = String(e["fabric"])
	sel = String(pieces[0]["id"]) if not pieces.is_empty() else null
	sel_v = -1
	sew_a = null
	undo.clear()
	redo.clear()
	if _active_label != null:
		_active_label.text = "Editing: %s" % (plan_name if active >= 0 else "nothing")
	if plan_opt != null:
		var entries := _plan_entries()
		for k in entries.size():
			if String(entries[k][1]) == plan_key:
				plan_opt.select(k)
	_refresh_garment_tabs()
	_refresh_fabric_tab()


func _fit_table() -> void:
	table.fitted = false
	table.fit()


# Starts the outfit with one garment from a plan key (boot, demo, FL_AUTO). Needs the body (drafted garments).
func _start_outfit(key: String) -> void:
	var p := _plan_for_key(key)
	if p.is_empty():
		key = "fs-teagan"
		p = _plan_for_key(key)
	outfit = []
	active = -1
	_add_garment_entry(key, p)


# Single-garment start from outside the outfit (demo, FL_AUTO): the next regen replaces the outfit with this garment.
func _set_single_garment(key: String) -> void:
	plan_key = key
	outfit = []
	active = -1
	_need_outfit = true


func _entry_set_plan(i: int, p: Dictionary) -> void:
	var e: Dictionary = outfit[i]
	e["plan"] = p
	e["name"] = _display_name(String(e["key"]), p)
	e["edited"] = false  # a fresh draft: no user edits
	e["notes"] = String(p.get("notes", ""))
	e["pieces"] = (p.get("pieces", []) as Array).duplicate(true)
	e["seams"] = (p.get("seams", []) as Array).duplicate(true)
	e["fitted"] = _fs_preset(meas)


# Draft the garment again for the live body (drafted and FreeSewing garments; keeps fabric, colour and visibility).
func _redraft_entry(i: int) -> void:
	if i < 0 or i >= outfit.size() or bool(outfit[i]["locked"]) or String(outfit[i]["redraft"]) == "":
		return
	_confirm_discard(i, "Re-drafting %s" % String(outfit[i]["name"]), func() -> void: _redraft_entry_now(i))


func _redraft_entry_now(i: int) -> void:
	if i < 0 or i >= outfit.size():
		return
	_store_active()
	var key := String(outfit[i]["redraft"])
	if key == "":
		return
	var p := _plan_for_key(key)
	if p.is_empty():
		return
	_entry_set_plan(i, p)
	if i == active:
		_activate(i, false)
	changed("outfit")


func _set_garment_fabric(i: int, id: String) -> void:
	if i < 0 or i >= outfit.size() or id == "":
		return
	outfit[i]["fabric"] = id
	outfit[i]["color"] = String(Table.FABRIC_COLORS.get(id, "#9fb7d6"))
	if i == active:
		fabric_id = id
	_schedule()


func _outfit_set(i: int, field: String, value: Variant) -> void:
	if i < 0 or i >= outfit.size():
		return
	_store_active()
	outfit[i][field] = value
	changed("outfit" if field == "visible" else "ui")


func _outfit_select(i: int) -> void:
	if i == active or i < 0 or i >= outfit.size():
		return
	_activate(i)
	_fit_table()
	table_dirty = true


func _outfit_remove(i: int) -> void:
	if i < 0 or i >= outfit.size():
		return
	_confirm_discard(i, "Removing %s" % String(outfit[i]["name"]), func() -> void: _outfit_remove_now(i))


func _outfit_remove_now(i: int) -> void:
	if i < 0 or i >= outfit.size():
		return
	_store_active()
	var uid := active_uid()
	outfit.remove_at(i)
	var ni := _index_of_uid(uid)
	if ni < 0:
		ni = mini(i, outfit.size() - 1)  # the active garment was removed: take its neighbour
	_activate(ni, false)
	_fit_table()
	changed("outfit")


func _outfit_move(i: int, d: int) -> void:
	var j := i + d
	if i < 0 or j < 0 or j >= outfit.size():
		return
	_store_active()
	var uid := active_uid()
	var t = outfit[i]
	outfit[i] = outfit[j]
	outfit[j] = t
	_activate(_index_of_uid(uid), false)
	changed("outfit")


func _fabric_items() -> Array:
	if _fabric_list.is_empty():
		_fabric_list = PlanLoader.load_json("res://materials/fabrics.json").get("fabrics", [])
	return _fabric_list


# Locked garments: their pattern cannot change. Disables every editing control under vb (the table checks the lock too).
func _lock_editing(vb: Node) -> void:
	if not active_locked():
		return
	for c in vb.find_children("*", "", true, false):
		if c is BaseButton or c is Range or c is ColorPickerButton:
			(c as Control).disabled = true
		elif c is LineEdit:
			(c as LineEdit).editable = false


func _refresh_outfit_tab() -> void:
	var vb := _tab_vbox("Outfit")
	if vb == null:
		return
	for c in vb.get_children():
		vb.remove_child(c)
		c.queue_free()
	var h := Label.new()
	h.text = "Outfit - %d garment%s" % [outfit.size(), "" if outfit.size() == 1 else "s"]
	vb.add_child(h)
	var note := Label.new()
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	note.add_theme_font_size_override("font_size", 12)
	note.text = "Listed inner first. Each garment further out wraps 8 mm wider than the one under it. Shown garments are simulated together and collide with each other. Click a name to edit that garment on the cutting table."
	vb.add_child(note)
	if outfit.is_empty():
		var none := Label.new()
		none.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		none.text = "No garments on the mannequin. Add one from the Library tab."
		vb.add_child(none)
	for i in outfit.size():
		vb.add_child(_outfit_row(i))


func _outfit_row(i: int) -> Control:
	var e: Dictionary = outfit[i]
	var card := PanelContainer.new()
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 4)
	card.add_child(v)
	var top := HBoxContainer.new()
	v.add_child(top)
	var show := CheckBox.new()
	show.text = "Show"
	show.button_pressed = bool(e["visible"])
	show.toggled.connect(func(on: bool) -> void: _outfit_set(i, "visible", on))
	top.add_child(show)
	var nm := Button.new()
	nm.text = String(e["name"])
	nm.flat = i != active
	nm.clip_text = true
	nm.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	nm.tooltip_text = "Edit this garment on the cutting table"
	nm.pressed.connect(func() -> void: _outfit_select(i))
	top.add_child(nm)
	var mid := HBoxContainer.new()
	v.add_child(mid)
	var layer := Label.new()
	layer.text = "Layer %d" % (i + 1)
	layer.add_theme_font_size_override("font_size", 12)
	layer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	mid.add_child(layer)
	var lock := CheckBox.new()
	lock.text = "Locked"
	lock.button_pressed = bool(e["locked"])
	lock.toggled.connect(func(on: bool) -> void: _outfit_set(i, "locked", on))
	mid.add_child(lock)
	var up := Button.new()
	up.text = "Up"
	up.disabled = i == 0
	up.tooltip_text = "Move one layer inward"
	up.pressed.connect(func() -> void: _outfit_move(i, -1))
	mid.add_child(up)
	var down := Button.new()
	down.text = "Down"
	down.disabled = i == outfit.size() - 1
	down.tooltip_text = "Move one layer outward"
	down.pressed.connect(func() -> void: _outfit_move(i, 1))
	mid.add_child(down)
	var rm := Button.new()
	rm.text = "Remove"
	rm.pressed.connect(func() -> void: _outfit_remove(i))
	mid.add_child(rm)
	var bot := HBoxContainer.new()
	v.add_child(bot)
	var fo := OptionButton.new()
	fo.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var fabs := _fabric_items()
	for f in fabs:
		fo.add_item(String(f["name"]))
		fo.set_item_metadata(fo.item_count - 1, String(f["id"]))
		if String(f["id"]) == String(e["fabric"]):
			fo.select(fo.item_count - 1)
	fo.item_selected.connect(func(k: int) -> void: _set_garment_fabric(i, String(fo.get_item_metadata(k))))
	bot.add_child(fo)
	var col := ColorPickerButton.new()
	col.custom_minimum_size = Vector2(34, 0)
	col.color = Color(String(e["color"]))
	col.tooltip_text = "Garment colour"
	col.color_changed.connect(func(c: Color) -> void: outfit[i]["color"] = "#" + c.to_html(false); _schedule())
	bot.add_child(col)
	var fit := HBoxContainer.new()
	v.add_child(fit)
	var fl := Label.new()
	fl.add_theme_font_size_override("font_size", 12)
	fl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	fl.text = "fitted: %s" % String(e["fitted"])
	if String(e["redraft"]) != "" and String(e["fitted"]) != _fs_preset(meas):
		fl.text += " (body is now %s)" % _fs_preset(meas)
	fl.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	fit.add_child(fl)
	if String(e["redraft"]) != "":
		var rd := Button.new()
		rd.text = "Re-draft"
		rd.tooltip_text = "Draft this garment again for the current body"
		rd.disabled = bool(e["locked"])
		rd.pressed.connect(func() -> void: _redraft_entry(i))
		fit.add_child(rd)
	if i == active:
		card.self_modulate = Color(0.62, 0.82, 1.0)
	return card


func _process(_delta: float) -> void:
	_watch_window()
	if table_dirty and table != null:
		table_dirty = false
		table.queue_redraw()
	_demo_tick()


func _on_debounce() -> void:
	regen_and_rebuild()


# Stops the worker before the editor leaves the tree. A step still in flight is finished and its result dropped
# (no UI runs here, so nothing reads the frame).
func _exit_tree() -> void:
	if _in_flight:
		_done_sem.wait()
		_in_flight = false
	if _worker != null:
		_mutex.lock()
		_quit = true
		_mutex.unlock()
		_work_sem.post()
		_worker.wait_to_finish()
		_worker = null


func _build_table_bar() -> void:
	var bar: HBoxContainer = $UI/Root/Main/TablePane/TableBar
	var sel_btn := Button.new()
	sel_btn.text = "Select"
	sel_btn.pressed.connect(func() -> void: table.set_tool("select"))
	bar.add_child(sel_btn)
	var sew_btn := Button.new()
	sew_btn.text = "Sew"
	sew_btn.pressed.connect(func() -> void: table.set_tool("sew"))
	bar.add_child(sew_btn)
	var fit_btn := Button.new()
	fit_btn.text = "Fit"
	fit_btn.pressed.connect(func() -> void: table.fit())
	bar.add_child(fit_btn)
	_active_label = Label.new()
	_active_label.name = "ActiveLabel"
	_active_label.add_theme_font_size_override("font_size", 12)
	bar.add_child(_active_label)
	var snap_box := CheckBox.new()
	snap_box.text = "snap 5 mm"
	snap_box.button_pressed = true
	snap_box.toggled.connect(func(on: bool) -> void: snap = on)
	bar.add_child(snap_box)
	var hint := Label.new()
	hint.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART  # sew lengths and the warning need the full line
	hint.add_theme_font_size_override("font_size", 12)
	($UI/Root/Main/TablePane as VBoxContainer).add_child(hint)
	table.hint_label = hint
	table.set_tool("select")


func _on_preset_button(p: String) -> void:
	meas = (BodyGen.PRESETS[p] as Dictionary).duplicate()
	_rebuild_body_tab()
	_schedule()


# Puts the Plan dropdown back on the plan of the active garment (after a refused or cancelled switch).
func _select_plan_key(key: String) -> void:
	if plan_opt != null:
		plan_opt.select(maxi(0, _plan_entries().map(func(e: Array) -> String: return e[1]).find(key)))


func _on_plan(_i: int) -> void:
	# the Plan dropdown swaps the active garment (with one garment in the outfit: the whole outfit)
	var key: String = _plan_entries()[plan_opt.selected][1]
	if active_locked():
		status_label.text = "This garment is locked: unlock it in the Outfit tab to change its plan"
		_select_plan_key(plan_key)
		return
	var p := _plan_for_key(key)
	if p.is_empty():
		_select_plan_key(plan_key)
		return
	_confirm_discard(active, "Switching to %s" % _plan_title(key),
		func() -> void:
			_replace_active(key, p)
			_fit_table()
			changed("load"),
		func() -> void: _select_plan_key(plan_key))


# Discard guard for actions that replace a garment's pattern (Plan, Re-draft, Open, Remove). When outfit[idx] has
# edits, asks first; action runs on confirm and on_cancel when the user backs out. Without edits action runs at once.
func _confirm_discard(idx: int, what: String, action: Callable, on_cancel := Callable()) -> void:
	if idx < 0 or idx >= outfit.size() or not bool(outfit[idx].get("edited", false)):
		action.call()
		return
	var dlg := ConfirmationDialog.new()
	dlg.title = "Discard your edits?"
	dlg.dialog_text = "%s replaces the pattern you edited for %s on the cutting table.\n\nSave the pattern first to keep those edits. Discard them?" % [what, String(outfit[idx]["name"])]
	dlg.ok_button_text = "Discard edits"
	dlg.cancel_button_text = "Keep editing"
	var answered := [false]
	dlg.confirmed.connect(func() -> void:
		answered[0] = true
		action.call())
	dlg.canceled.connect(func() -> void:
		if not answered[0] and on_cancel.is_valid():
			on_cancel.call())
	dlg.visibility_changed.connect(func() -> void:
		if not dlg.visible:
			dlg.queue_free())
	add_child(dlg)
	dlg.popup_centered()


func _on_quality(_i: int) -> void:
	quality = QUALITIES.keys()[quality_opt.selected]
	_schedule()


func _on_motion(_i: int) -> void:
	motion = "walk" if motion_opt.selected == 1 else "stand"
	regen_and_rebuild()


func _on_redraft() -> void:
	if active < 0 or active_locked():
		return
	_confirm_discard(active, "Re-drafting", func() -> void: _redraft_active())


func _redraft_active() -> void:
	if active < 0 or active_locked():
		return
	var e: Dictionary = outfit[active]
	var key := String(e["redraft"]) if String(e["redraft"]) != "" else String(e["key"])
	var p: Dictionary = e["plan"] if key == "custom" else _plan_for_key(key)
	if p.is_empty():
		return
	_store_active()
	var rk := String(e["redraft"])
	_replace_active(String(e["key"]), p)  # the outline is reset to the draft (edits on the table are dropped)
	outfit[active]["redraft"] = rk  # a saved garment keeps the key it was drafted from
	_fit_table()
	changed("load")


func _on_play() -> void:
	if tl_t >= 3.999:
		tl_t = 0.0
	playing = not playing
	_sync_tl_ui()


func _on_open() -> void:
	var dlg := FileDialog.new()
	dlg.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	dlg.add_filter("*.json", "Pattern JSON")
	dlg.file_selected.connect(_on_open_file)
	add_child(dlg)
	dlg.popup_centered(Vector2(600, 400))


func _on_open_file(path: String) -> void:
	var loaded := PlanLoader.load_json(path)
	if loaded.is_empty() or not loaded.has("pieces"):
		status_label.text = "could not read that pattern"
		return
	if active_locked():
		status_label.text = "This garment is locked: unlock it in the Outfit tab to open a pattern into it"
		return
	_confirm_discard(active, "Opening %s" % path.get_file(), func() -> void: _open_loaded(loaded))


func _open_loaded(loaded: Dictionary) -> void:
	var p := PlanLoader.normalize_plan(loaded)
	if loaded.has("meas") and loaded["meas"] is Dictionary:
		for k in (loaded["meas"] as Dictionary).keys():
			meas[k] = loaded["meas"][k]
		_rebuild_body_tab()
	_replace_active("custom", p)  # the opened pattern becomes the active garment
	_fit_table()
	changed("load")


func _on_save() -> void:
	var dlg := FileDialog.new()
	dlg.file_mode = FileDialog.FILE_MODE_SAVE_FILE
	dlg.add_filter("*.json", "Pattern JSON")
	dlg.current_file = "%s.failoring.json" % (plan_name.to_lower().replace(" ", "-") if plan_name != "" else "pattern")
	dlg.file_selected.connect(_on_save_file)
	add_child(dlg)
	dlg.popup_centered(Vector2(600, 400))


func _on_save_file(path: String) -> void:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		status_label.text = "could not write " + path
		return
	f.store_string(JSON.stringify({"format": "failoring-pattern/1", "name": plan_name, "notes": plan_notes,
		"fabric": fabric_id, "meas": meas, "pieces": pieces, "seams": seams}, " "))
	status_label.text = "saved " + path


# ------------------------------------------------------------------ garment library
# Library panel hooks: what it shows and what adding does (library_panel.gd).
func current_preset() -> String:
	return _fs_preset(meas)


func current_garment_name() -> String:
	return plan_name


func library_plan(id: String) -> Dictionary:
	return _plan_for_key(id)


func _on_library_add(id: String) -> void:
	var p := _plan_for_key(id)
	if p.is_empty():
		library_panel.set_status("That garment could not be loaded.")
		return
	_add_garment_entry(id, p)  # outermost layer, and the active garment
	_fit_table()
	library_panel.set_status("Added %s. Layer %d." % [plan_name, outfit.size()])
	changed("load")


func _on_library_save(category: String, nm: String) -> void:
	var from := ""
	if plan_key.begins_with("user:"):
		from = String(plan.get("from", ""))
	elif plan_key in Library.DRAFTED or plan_key.begins_with("fs-"):
		from = plan_key
	var fitted := _fs_preset(meas) if from != "" else ""
	var file := Library.save(nm if nm != "" else plan_name, category, fitted, from, plan_notes, fabric_id, pieces, seams)
	if file == "":
		library_panel.set_status("Could not write to the library folder.")
		return
	library_panel.set_status("Saved to the library as %s." % (nm if nm != "" else plan_name))
	library_panel.refresh()


# ------------------------------------------------------------------ body + plans
static func _fs_preset(m: Dictionary) -> String:
	var keys := ["height", "bust", "waist", "hip", "shoulder", "muscle"]
	var best := "average"
	var bd := 1e9
	for k in BodyGen.PRESETS.keys():
		var p: Dictionary = BodyGen.PRESETS[k]
		var d := 0.0
		for f in keys:
			var v: float = float(m.get(f, p.get(f, 0.0)))
			var r: float = (v - float(p.get(f, 0.0))) / float(p.get(f, 1.0))
			d += r * r
		if d < bd:
			bd = d
			best = k
	return best


func _helper_wrap():
	return Wrap.new({"id": "h", "outline": [[0.0, 0.0], [1.0, 0.0], [1.0, 1.0]],
		"wrap": {"mode": "arc", "theta": 0.0, "y": 0.0, "clear": 0.012}}, body_stand["sections"], body_stand["arms"])


# Normalized plan for a library/plan key (see library.gd), drafted on the live body. {} if the key is unknown or the
# garment cannot be drafted yet (no body).
func _plan_for_key(key: String) -> Dictionary:
	var p := {}
	if key in Library.DRAFTED:
		if body_stand == null:
			return {}
		if key == "pants":
			p = Draft.pants(body_stand, _helper_wrap(), {})
		elif key == "tank":
			p = Draft.tank(body_stand, _helper_wrap(), {})
		else:
			p = Draft.shirt(body_stand, _helper_wrap(), {})
	elif key.begins_with("fs-"):
		p = PlanLoader.load_json("res://assets/%s/plan_%s.json" % [_fs_preset(meas), key.trim_prefix("fs-")])
	elif Library.is_user(key):
		p = Library.plan_for_user(key)
	elif Builtin.all().has(key):
		p = (Builtin.all()[key] as Dictionary).duplicate(true)
	return PlanLoader.normalize_plan(p) if not p.is_empty() else {}


func regen_and_rebuild() -> void:
	_drain()  # body regen and rebuild replace the sim: no step may be running
	if M.is_empty():
		return
	var meas_key := JSON.stringify(meas)
	var body_changed := meas_key != _meas_key
	_meas_key = meas_key
	body_rest = BodyGen.generate(M, meas, prev_body)
	prev_body = {"x": (body_rest as Dictionary)["x"], "keys": (body_rest as Dictionary)["keys"]}
	body_stand = BodyGen.stand(M, body_rest)
	_store_active()
	if _need_outfit:
		_need_outfit = false
		_start_outfit(plan_key)
	elif body_changed:
		_redraft_drafted()  # drafted garments follow a body change (their outlines are drafted from it)
	_refresh_fabric_tab()
	_refresh_body_note()
	_refresh_garment_tabs()
	library_panel.refresh()  # fitted labels and drafted previews follow the body
	rebuild()


# Drafted garments (pants, tank, shirt) are drafted from the live body: re-draft them on every body change, as the
# single-garment editor always did. FreeSewing garments keep their outline (re-draft on request, see _redraft_entry).
# A garment with user edits keeps its pattern: the body change is shown in the status line, and Re-draft (which asks
# first) fits it to the new body.
func _redraft_drafted() -> void:
	var was := active
	var kept := false
	for i in outfit.size():
		var e: Dictionary = outfit[i]
		if String(e["redraft"]) in Library.DRAFTED and not bool(e["locked"]):
			if bool(e.get("edited", false)):
				kept = kept or i == was
				continue
			var p := _plan_for_key(String(e["redraft"]))
			if not p.is_empty():
				_entry_set_plan(i, p)
	if kept:
		_notice = "Body changed: your edits to this pattern are kept. Press Re-draft to fit it to the new body."
	if was >= 0 and was < outfit.size() and String(outfit[was]["redraft"]) in Library.DRAFTED and not kept:
		# reload the working copy without clearing the undo history
		pieces = outfit[was]["pieces"]
		seams = outfit[was]["seams"]
		plan = outfit[was]["plan"]
		_fix_selection()


func _refresh_body_note() -> void:
	var vb := _tab_vbox("Body")
	var note: Label = vb.get_node("BodyNote") if vb.has_node("BodyNote") else null
	if note == null:
		note = Label.new()
		note.name = "BodyNote"
		note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		vb.add_child(note)
	if body_stand == null:
		note.text = ""
		return
	var off := []
	for k in (body_stand["residual"] as Dictionary).keys():
		if absf(float((body_stand["residual"] as Dictionary)[k])) > 0.003:
			off.append(k)
	var txt := "All measurements matched (within 1 mm)." if off.is_empty() else "Out of range: " + ", ".join(off) + "."
	note.text = "%s Body %.0f ms." % [txt, float(body_stand.get("ms", 0.0))]


# ------------------------------------------------------------------ sim
static func _to_v3a(p: PackedFloat32Array) -> PackedVector3Array:
	var out := PackedVector3Array()
	out.resize(p.size() / 3)
	for i in p.size() / 3:
		out[i] = Vector3(p[3 * i], p[3 * i + 1], p[3 * i + 2])
	return out


func rebuild() -> void:
	_drain()
	sim = null
	frame = 0
	walk_k = 0.0
	_store_active()
	# the shown garments, merged into one plan (outfit.gd): ids are garment-prefixed, each piece keeps its fabric
	var gs := []
	for e in outfit:
		if bool(e["visible"]):
			gs.append({"uid": e["uid"], "fabric": e["fabric"], "pieces": Table.valid_pieces(e["pieces"])["pieces"], "seams": e["seams"]})
	var merged := Outfit.merge(gs)
	_sim_pieces = merged["pieces"]
	_sim_seams = merged["seams"]
	_piece_uid = {}
	for p in _sim_pieces:
		_piece_uid[String(p["id"])] = int(p["garment"])
	if _sim_pieces.is_empty() or body_stand == null:
		meta = {}
		frames.clear()
		_build_mat()
		_render_info()
		status_label.text = ("No garment on the mannequin - add one from the Library tab" if body_stand != null else "starting") + _status_suffix()
		return
	_stand_v3 = _to_v3a(body_stand["pos"])
	var q: Dictionary = QUALITIES[quality]
	# two or more garments need the solver's self collision: it is what keeps them apart (no seams join garments)
	var collide := self_collision or bool(q.get("self", false)) or gs.size() > 1
	var built: Dictionary = PlanLoader.build({"pieces": _sim_pieces, "seams": _sim_seams}, body_stand["sections"], body_stand["arms"],
		_stand_v3, _tris(), fabric_id, q, seam_stiffness, ramp_frames, collide)
	if built.is_empty():
		status_label.text = "sim build failed"
		return
	sim = built["sim"]
	_disp_idx = sim.get_display_indices()  # topology is fixed for this sim: read it once
	_solver_stats = sim.get_solver_stats()
	_vcount = sim.get_vertex_count()
	meta = built
	var garment_names := {}
	for e in outfit:
		garment_names[int(e["uid"])] = String(e["name"])
	health = FailoringGarmentHealth.new(built, _sim_pieces, _sim_seams, garment_names)
	health_text = ""
	health_hot = []
	wraps.clear()
	for pid in built["order"]:
		wraps[pid] = Wrap.new(Table.piece_by_id(_sim_pieces, pid), body_stand["sections"], body_stand["arms"])
	_build_mesh_edges()
	_shade.setup(meta, mesh_edges, _sim_pieces, fabric_id, _stand_v3, _disp_idx)
	_col_key = []  # colours depend on the new meta/mesh_edges
	_build_stitches()
	_build_mat()
	body_node.use_custom(_stand_v3, _tris())
	body_node.show_pose(_stand_v3)
	_body_key = ""
	_last_shown = ""
	frames.clear()
	frame = 0
	walk_k = 0.0
	var qf: int = QUALITIES[quality]["frames"]
	target = qf + (96 if motion == "walk" else qf)
	if OS.has_environment("FL_AUTO"):
		tl_t = 4.0
		playing = true
	if OS.has_environment("FL_T"):
		tl_t = clampf(float(OS.get_environment("FL_T")), 0.0, 4.0)
	_sync_tl_ui()
	_render_info()
	_show_current()
	status_label.text = "%d verts - press play" % int(built["n"]) + _status_suffix()


func _tris() -> PackedInt32Array:
	return M["tris"]


func _physics_process(_delta: float) -> void:
	_update_camera()
	if sim == null:
		return
	_poll_step(false)  # collect a step the worker has finished; never blocks the draw loop (paused or playing)
	if playing:
		_advance()
	_show_current()
	_tick += 1
	if OS.has_environment("FL_UISHOT") and sim != null and (frames.size() == 10 or _tick >= 150):
		var img := get_viewport().get_texture().get_image()
		if img != null and not img.is_empty():
			img.save_png(OS.get_environment("FL_UISHOT"))
			print("UISHOT saved")


var _tick := 0


func _advance() -> void:
	var qf: int = QUALITIES[quality]["frames"]
	if tl_t < 3.0:
		tl_t = minf(4.0, tl_t + tl_speed / 60.0 / 2.4)
		if tl_t >= 4.0:
			playing = false
	else:
		var need := target if tl_t >= 4.0 else mini(target, _frame_idx() + 1)
		# at most one step in flight. Threaded: the next step starts as soon as the last one is collected. Synchronous
		# (headless/tests): one step per drawn frame, as before. Playback time still advances below, so a slow sim makes
		# playback slower instead of freezing the UI in bursts.
		if not _in_flight and frames.size() < need and (_use_worker or _may_step()):
			_submit_step(_make_job())
		tl_t = minf(4.0, tl_t + tl_speed / 60.0 / maxf(1.5, float(target) / 24.0))
		if tl_t >= 4.0 and frames.size() >= target:
			playing = false
	_sync_tl_ui()


var _last_step_drawn := -1


func _may_step() -> bool:
	if DisplayServer.get_name() == "headless":
		return true  # no rendered frames to pace against
	var drawn := Engine.get_frames_drawn()
	if drawn == _last_step_drawn:
		return false
	_last_step_drawn = drawn
	return true


func _frame_idx() -> int:
	if frames.is_empty():
		return -1
	if tl_t >= 4.0:
		return frames.size() - 1
	var n := maxi(2, target if target > 0 else frames.size())
	return mini(int(round(clampf(tl_t - 3.0, 0.0, 1.0) * float(n - 1))), frames.size() - 1)


# Main thread: decides what the next frame needs (the body pose and the gravity ramp are computed here, from main-thread
# state, and handed to the worker as plain data). Must run with no step in flight, so `frame` and `walk_k` stay in step.
func _make_job() -> Dictionary:
	var q: Dictionary = QUALITIES[quality]
	var settle: int = q["frames"]
	frame = frames.size() + 1
	var job := {"body": _stand_v3, "move": false}
	if motion == "walk" and frame > settle:
		walk_k += 1.0
		var stride: float = _smooth(minf(1.0, walk_k / RAMP))
		var phase: float = fposmod(walk_k / CYCLE, 1.0)
		var pose := BodyGen.pose(M, body_rest, phase, {"stride": stride, "arms": stride})
		job["body"] = _to_v3a(pose["pos"])
		job["move"] = true
	var f := float(frame)
	if f <= float(ramp_frames) + 1.0 and ramp_frames > 0:
		job["gravity"] = -9.81 * minf(1.0, pow(f / float(ramp_frames), 2.0))
	return job


# Worker side: the body bake, the gravity, one solver frame and the copies of the display and cloth positions. This is
# the only code that calls `sim` while a step is in flight. It reads the job and writes only its return value.
func _run_step(job: Dictionary) -> Dictionary:
	var t0 := Time.get_ticks_usec()
	var bake := 0.0
	if job["move"]:
		bake = sim.set_body_positions(job["body"])
	if job.has("gravity"):
		sim.set_param("gravity_y", job["gravity"])
	var t1 := Time.get_ticks_usec()
	sim.step(1)
	var t2 := Time.get_ticks_usec()
	var fr := {"pos": sim.get_display_positions(0.0008), "cloth": sim.get_positions(),
		"body": job["body"], "ke": sim.get_kinetic_energy(), "gap": sim.get_max_seam_gap(),
		"clear": sim.get_min_body_clearance(), "ms": (t2 - t1) / 1000.0, "bake": bake}
	return {"frame": fr, "stats": sim.get_solver_stats(), "vcount": sim.get_vertex_count(),
		"total_ms": (t2 - t0) / 1000.0}


# Worker thread body: waits for a posted job, runs it, publishes the result, signals the main thread.
func _worker_main() -> void:
	while true:
		_work_sem.wait()
		_mutex.lock()
		var quit := _quit
		var job := _job
		_job = {}
		_mutex.unlock()
		if quit:
			return
		var res := _run_step(job)
		_mutex.lock()
		_result = res
		_mutex.unlock()
		_done_sem.post()


# Main thread: starts a step. Threaded, it returns at once and the step runs on the worker; synchronous, it runs inline.
func _submit_step(job: Dictionary) -> void:
	if not _use_worker:
		_collect(_run_step(job))
		return
	_mutex.lock()
	_job = job
	_mutex.unlock()
	_in_flight = true
	_work_sem.post()


# Main thread: collects the step in flight if it has finished. block = true waits for it (used by _drain).
func _poll_step(block: bool) -> void:
	if not _in_flight:
		return
	if block:
		_done_sem.wait()
	elif not _done_sem.try_wait():
		return
	_in_flight = false
	_mutex.lock()
	var res := _result
	_result = {}
	_mutex.unlock()
	_collect(res)


# Blocks until no step is in flight. Call before anything that touches `sim` on the main thread (rebuild, body regen,
# plan change, restart, free). Seeks call it too: they stop playback, so the wait happens at most once.
func _drain() -> void:
	_poll_step(true)


# Main thread: appends a finished frame and caches what the UI reads from sim (so the UI never calls sim mid-step).
func _collect(res: Dictionary) -> void:
	frames.append(res["frame"])
	_solver_stats = res["stats"]
	_vcount = res["vcount"]
	var fr: Dictionary = frames[frames.size() - 1]
	if health != null:
		var hr: Dictionary = health.check(sim, fr["cloth"], float(fr["ke"]), frames.size())
		health_text = String(hr["text"])
		if hr["hot"] != health_hot:
			health_hot = hr["hot"]
			table.queue_redraw()
	bake_ms = float(fr["bake"])
	step_ms = float(fr["ms"])
	if frames.size() % 30 == 0:
		_render_stats(float(res["total_ms"]), fr)
		print("EDITOR ", status_label.text.replace("\n", " | "))
	if OS.has_environment("FL_AUTO"):
		var parts := OS.get_environment("FL_AUTO").split(",")
		if parts.size() >= 3 and frames.size() >= int(parts[2]):
			if OS.has_environment("FL_SHOOT"):
				if not shoot_done:
					shoot_done = true
					playing = false
					tl_t = 4.0
					_show_current()
					await take_shots()
					print("EDITOR shoot done, quitting")
					get_tree().quit()
				return
			print("EDITOR auto target %s frames reached, quitting" % parts[2])
			_shot(parts[0] + "_" + parts[1])


var _last_shown := ""


func _show_current() -> void:
	if tl_range != null and (not tl_range.has_focus() or playing) and tl_range.value != tl_t * 1000.0:
		tl_range.set_value_no_signal(tl_t * 1000.0)
	_sync_stage_buttons()
	if DisplayServer.get_name() == "headless":
		var hi := _frame_idx()
		if hi >= 0:
			_render_stats(float(frames[hi]["ms"]), frames[hi])
		return
	# stage 0 (cut -> move) moves with the clock, so it is keyed on the millisecond.
	# stages 1 and 2 are static between frames: they are keyed on the stage and the sim frame index.
	var phase := 0
	if tl_t >= 3.0:
		phase = 2
	elif tl_t >= 2.0:
		phase = 1
	var key := ""
	if phase == 0:
		key = "0|%d|%s" % [int(tl_t * 1000.0), sel]
	else:
		key = "%d|%d|%s" % [phase, _frame_idx(), sel]
	if key == _last_shown:
		return
	_last_shown = key
	if phase == 0:
		_stage_pose()
		_show_stand_body()
		_update_stitches(cur_cloth, tl_t)
	elif phase == 1:
		var p2: PackedVector3Array = meta["p2"]
		_set_cloth_mesh(_with_midpoints(p2))
		cur_cloth = p2.slice(0, meta["n"] * 3) if p2.size() >= meta["n"] * 3 else p2
		_show_stand_body()
		_update_stitches(cur_cloth, tl_t)
	else:
		var i := _frame_idx()
		if i < 0:
			var p2b: PackedVector3Array = meta["p2"]
			_set_cloth_mesh(_with_midpoints(p2b))
			cur_cloth = p2b
			_show_stand_body()
		else:
			var fr: Dictionary = frames[i]
			_set_cloth_mesh(fr["pos"])
			cur_cloth = fr["cloth"]
			body_node.show_pose(fr["body"])
			_body_key = ""
			_update_stitches(cur_cloth, tl_t)
			_render_stats(float(fr["ms"]), fr)


var _body_key := ""  # "stand" while the standing body is on screen (it does not move in stages 0-1)


func _show_stand_body() -> void:
	if _body_key == "stand":
		return
	_body_key = "stand"
	body_node.show_pose(_stand_v3)


var cur_cloth := PackedVector3Array()


func _set_cloth_mesh(disp: PackedVector3Array) -> void:
	if disp.is_empty() or _disp_idx.is_empty():
		return
	# one ArrayMesh for the whole session, rebuilt per update with one surface per fabric (cloth_shade.gd)
	_shade.fill(_cloth_am, cloth_node, disp, _cloth_colors())


var _mid_panel := PackedInt32Array()
var _cloth_am := ArrayMesh.new()
var _disp_idx := PackedInt32Array()   # sim.get_display_indices(), fixed per build (set in rebuild)
var _stand_v3 := PackedVector3Array()  # standing body positions as Vector3, fixed per build (set in rebuild)
var _col_key := []                     # what _col_cache was computed for; cleared on rebuild
var _col_cache := PackedColorArray()


# Per-vertex colors by piece, selected piece brightened (browser V.recolor). Cached: only fabric or selection change it.
func _cloth_colors() -> PackedColorArray:
	var key := [_merged_sel()]
	if key == _col_key:
		return _col_cache
	_col_key = key
	_col_cache = _compute_cloth_colors()
	return _col_cache


# Merged id of the selected piece of the active garment ("" if none).
func _merged_sel() -> String:
	if sel == null or active < 0 or active >= outfit.size():
		return ""
	return Outfit.prefix(active_uid()) + String(sel)


func _compute_cloth_colors() -> PackedColorArray:
	var n: int = meta["n"]
	var ecount: int = mesh_edges.size() / 2
	var col := PackedColorArray()
	col.resize(n + ecount)
	var base_of := {}
	for e in outfit:
		base_of[int(e["uid"])] = Look.cloth_base(String(e["color"]))
	var fallback := Look.cloth_base("#9fb7d6")
	var sel_m := _merged_sel()
	var order: Array = meta["order"]
	for i in order.size():
		var pid := String(order[i])
		var r: Dictionary = meta["ranges"][pid]
		var c: Color = base_of.get(int(_piece_uid.get(pid, -1)), fallback) * (0.92 + 0.06 * float(i % 3))
		if pid == sel_m:
			c = c * 1.18
		for k in int(r["count"]):
			col[int(r["first"]) + k] = c
	for e in ecount:
		col[n + e] = col[_mid_panel[e]]
	return col


# display positions for a cloth mesh: vertices plus plain edge midpoints (browser
# FL.withMidpoints; the solver's body-following lift only exists in sim frames).
func _with_midpoints(cloth: PackedVector3Array) -> PackedVector3Array:
	var n: int = meta["n"]
	var out := PackedVector3Array()
	out.resize(n + mesh_edges.size() / 2)
	for i in n:
		out[i] = cloth[i]
	for e in mesh_edges.size() / 2:
		var a: int = mesh_edges[2 * e]
		var b: int = mesh_edges[2 * e + 1]
		out[n + e] = (cloth[a] + cloth[b]) * 0.5
	return out


func _line_node(col: Color, alpha: float) -> MeshInstance3D:
	var node := MeshInstance3D.new()
	node.extra_cull_margin = 0.5
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	mat.albedo_color = Color(col, alpha)
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	node.material_override = mat
	node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	stage.add_child(node)
	return node


func _build_mesh_edges() -> void:
	var seen := {}
	var edges := PackedInt32Array()
	var tris: PackedInt32Array = meta["tris"]
	for t in tris.size() / 3:
		for k in 3:
			var a: int = tris[3 * t + k]
			var b: int = tris[3 * t + (k + 1) % 3]
			var key := "%d_%d" % [mini(a, b), maxi(a, b)]
			if not seen.has(key):
				seen[key] = true
				edges.append(mini(a, b))
				edges.append(maxi(a, b))
	mesh_edges = edges
	_mid_panel.resize(edges.size() / 2)
	var order: Array = meta["order"]
	for e in edges.size() / 2:
		_mid_panel[e] = -1
		for i in order.size():
			var r: Dictionary = meta["ranges"][String(order[i])]
			if edges[2 * e] >= int(r["first"]) and edges[2 * e] < int(r["first"]) + int(r["count"]):
				_mid_panel[e] = i
				break


func _table_layout() -> Dictionary:
	var x0 := 1e9
	var y0 := 1e9
	var x1 := -1e9
	var y1 := -1e9
	for p in _sim_pieces:
		var b := Table.bbox_of(p["outline"])
		var t: Dictionary = p.get("table", {"x": 0.0, "y": 0.0})
		x0 = minf(x0, b[0] + float(t.get("x", 0.0)))
		x1 = maxf(x1, b[2] + float(t.get("x", 0.0)))
		y0 = minf(y0, b[1] + float(t.get("y", 0.0)))
		y1 = maxf(y1, b[3] + float(t.get("y", 0.0)))
	if x0 > x1:
		x0 = 0.0
		y0 = 0.0
		x1 = 1.0
		y1 = 1.0
	return {"cx": (x0 + x1) * 0.5, "cy": (y0 + y1) * 0.5, "w": x1 - x0, "h": y1 - y0}


# cutting-table mat slab in 3D for the table stages (browser V.buildMat, no grid lines)
func _build_mat() -> void:
	if mat_node == null:
		mat_node = MeshInstance3D.new()
		mat_node.material_override = Look.table_material()
		stage.add_child(mat_node)
	var l := _table_layout()
	var w: float = maxf(1.6, l["w"] + 0.6)
	var h: float = maxf(1.0, l["h"] + 0.6)
	var box := BoxMesh.new()
	box.size = Vector3(w, 0.02, h)
	mat_node.mesh = box
	mat_node.position = Vector3(0, 0.01, 1.75)


# assembly timeline stages 0-2 (browser V.pose T<2): pieces fly from the table mat
# to the body, then ease into the wrapped start pose.
func _stage_pose() -> void:
	var n: int = meta["n"]
	var pos := PackedFloat32Array()
	pos.resize(n * 3)
	var l := _table_layout()
	var t := tl_t
	var order: Array = meta["order"]
	for i in order.size():
		var pid := String(order[i])
		var r: Dictionary = meta["ranges"][pid]
		var w = wraps[pid]
		var uv: PackedVector2Array = meta["uv"][pid]
		var p = Table.piece_by_id(_sim_pieces, pid)
		var pt: Dictionary = p.get("table", {"x": 0.0, "y": 0.0})
		var bb := Table.bbox_of(p["outline"])
		var vm: float = (bb[1] + bb[3]) * 0.5
		var uc: float = w._uc
		var c0 := [uc + float(pt.get("x", 0.0)) - l["cx"], 0.032, 1.75 - (vm + float(pt.get("y", 0.0)) - l["cy"])]
		var c1: Vector3 = w.flat_point(uc, vm, 0.30)
		var ph: Vector3 = w.flat_point(uc, vm, 0.0)
		var tx: Vector3 = w.flat_point(uc + 0.1, vm, 0.0)
		var nx0: Vector3 = w.flat_point(uc, vm, 1.0)
		var ex1: Vector3 = (tx - ph).normalized() if (tx - ph).length() > 1e-9 else Vector3.RIGHT
		var ey1 := Vector3.UP
		var nn: Vector3 = (nx0 - ph).normalized() if (nx0 - ph).length() > 1e-9 else Vector3.BACK
		var q0 := Quaternion(Basis(Vector3.RIGHT, Vector3(0, 0, -1), Vector3.UP))
		var q1 := Quaternion(Basis(ex1, ey1, nn))
		var delay: float = 0.3 * float(i) / float(maxi(1, order.size() - 1)) if order.size() > 1 else 0.0
		var pr: float = clampf((t - 1.0 - delay) / 0.7, 0.0, 1.0)
		var sa: float = _smooth(pr / 0.6)
		var sb: float = _smooth((pr - 0.6) / 0.4)
		var lift: float = 0.35 * sin(PI * clampf(pr / 0.6, 0.0, 1.0))
		var q := q0.slerp(q1, sa)
		var e := Basis(q)
		var cx: float = c0[0] + (c1.x - c0[0]) * sa
		var cy: float = c0[1] + (c1.y - c0[1]) * sa + lift
		var cz: float = c0[2] + (c1.z - c0[2]) * sa
		var p2: PackedVector3Array = meta["p2"]
		for k in int(r["count"]):
			var vi: int = int(r["first"]) + k
			var a: float = uv[k].x - uc
			var b: float = uv[k].y - vm
			var x: float = cx + e.x.x * a + e.y.x * b
			var y: float = cy + e.x.y * a + e.y.y * b
			var z: float = cz + e.x.z * a + e.y.z * b
			if sb > 0.0:
				x += (p2[vi].x - x) * sb
				y += (p2[vi].y - y) * sb
				z += (p2[vi].z - z) * sb
			pos[3 * vi] = x
			pos[3 * vi + 1] = y
			pos[3 * vi + 2] = z
	cur_cloth = PackedVector3Array()
	cur_cloth.resize(n)
	for vi in n:
		cur_cloth[vi] = Vector3(pos[3 * vi], pos[3 * vi + 1], pos[3 * vi + 2])
	_set_cloth_mesh(_with_midpoints(cur_cloth))
	_update_stitches(cur_cloth, t)


var _stitch_mesh: MeshInstance3D
var _seam_mesh: MeshInstance3D


func _build_stitches() -> void:
	stitch_nodes.clear()
	seam_nodes.clear()
	if _stitch_mesh == null:
		_stitch_mesh = _line_node(Color(1, 1, 1), 0.6)
		_seam_mesh = _line_node(Color(1, 1, 1), 0.85)
	var index_of := {}
	var order: Array = meta["order"]
	for i in order.size():
		index_of[String(order[i])] = i
	var si := 0
	for s in _sim_seams:
		var a = Table.piece_by_id(_sim_pieces, String(s["a"][0]))
		var b = Table.piece_by_id(_sim_pieces, String(s["b"][0]))
		si += 1
		if a == null or b == null:
			continue
		var ia: int = index_of.get(String(s["a"][0]), -1)
		var ib: int = index_of.get(String(s["b"][0]), -1)
		if ia < 0 or ib < 0 or ia == ib:
			continue
		var ca: Array = (meta["chains"] as Dictionary)[String(s["a"][0])][int(s["a"][1])]
		var cb: Array = (meta["chains"] as Dictionary)[String(s["b"][0])][int(s["b"][1])]
		if ca.size() < 2 or cb.size() < 2:
			continue
		var col := Color(Table.SEAM_COLORS[(si - 1) % Table.SEAM_COLORS.size()])
		var pairs := []
		var m: int = maxi(ca.size(), cb.size())
		for k in m:
			var tt: float = float(k) / float(maxi(1, m - 1))
			pairs.append([ca[mini(ca.size() - 1, int(round(tt * float(ca.size() - 1))))], cb[mini(cb.size() - 1, int(round((1.0 - tt if bool(s.get("reverse", false)) else tt) * float(cb.size() - 1))))]])
		stitch_nodes.append({"pairs": pairs, "si": si, "col": col})
		var seg := []
		for ch in [ca, cb]:
			for k in ch.size() - 1:
				seg.append(ch[k])
				seg.append(ch[k + 1])
		seam_nodes.append({"seg": seg, "si": si, "col": col})


func _update_stitches(cloth: PackedVector3Array, t: float) -> void:
	var reveal: float = 1.0 if t < 2.0 else clampf(t - 2.0, 0.0, 1.0)
	var show_st: bool = t >= 1.2 - 1e-6
	var sarr := PackedVector3Array()
	var scol := PackedColorArray()
	for s in stitch_nodes:
		var pairs: Array = s["pairs"]
		var sc: Color = s["col"]
		var m: int = pairs.size()
		var count: int = 0 if not show_st else (m if t >= 2.0 else maxi(1, int(ceil(float(m) * reveal))))
		for k in count:
			sarr.append(cloth[int(pairs[k][0])])
			sarr.append(cloth[int(pairs[k][1])])
			scol.append(sc)
			scol.append(sc)
	_set_lines(_stitch_mesh, sarr, scol, show_st and not sarr.is_empty())
	var garr := PackedVector3Array()
	var gcol := PackedColorArray()
	var any_seg := false
	if t >= 0.05:
		for s in seam_nodes:
			var seg: Array = s["seg"]
			var gc: Color = s["col"]
			for k in seg.size():
				garr.append(cloth[int(seg[k])])
				gcol.append(gc)
		any_seg = not garr.is_empty()
	_set_lines(_seam_mesh, garr, gcol, any_seg)


func _set_lines(node: MeshInstance3D, arr: PackedVector3Array, col: PackedColorArray, show: bool) -> void:
	node.visible = show
	if not show:
		node.mesh = null
		return
	var mesh := ArrayMesh.new()
	var a := []
	a.resize(Mesh.ARRAY_MAX)
	a[Mesh.ARRAY_VERTEX] = arr
	a[Mesh.ARRAY_COLOR] = col
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_LINES, a)
	node.mesh = mesh


func _build_timeline_bar() -> void:
	var bar: HBoxContainer = $UI/Root/TimelineBar
	var start := Button.new()
	start.text = "|<"
	start.tooltip_text = "Back to the start"
	start.pressed.connect(func() -> void: _drain(); tl_t = 0.0; playing = false; _sync_tl_ui(); _show_current())
	bar.add_child(start)
	var back := Button.new()
	back.text = "<<"
	back.tooltip_text = "Step back"
	back.pressed.connect(func() -> void: _drain(); tl_t = clampf(tl_t - 0.1, 0.0, 4.0); playing = false; _sync_tl_ui(); _show_current())
	bar.add_child(back)
	tl_play_btn = Button.new()
	tl_play_btn.text = ">"
	tl_play_btn.tooltip_text = "Play / pause (Space)"
	tl_play_btn.custom_minimum_size = Vector2(46, 0)
	tl_play_btn.pressed.connect(_on_play)
	bar.add_child(tl_play_btn)
	var fwd := Button.new()
	fwd.text = ">>"
	fwd.tooltip_text = "Step forward"
	fwd.pressed.connect(func() -> void: _drain(); tl_t = clampf(tl_t + 0.1, 0.0, 4.0); playing = false; _sync_tl_ui(); _show_current())
	bar.add_child(fwd)
	var spd := OptionButton.new()
	spd.add_item("0.5x", 0)
	spd.add_item("1x", 1)
	spd.add_item("2x", 2)
	spd.select(1)
	spd.item_selected.connect(func(i: int) -> void: tl_speed = [0.5, 1.0, 2.0][spd.get_item_id(i)])
	bar.add_child(spd)
	for s in [0, 1, 2, 3]:
		var b := Button.new()
		b.text = "%d" % (s + 1)
		b.tooltip_text = ["Cut pieces", "Move to body", "Sew seams", "Drape"][s]
		b.pressed.connect(func() -> void: _drain(); tl_t = float(s); playing = false; _sync_tl_ui(); _show_current())
		bar.add_child(b)
	tl_range = HSlider.new()
	tl_range.min_value = 0.0
	tl_range.max_value = 4000.0
	tl_range.step = 1.0
	tl_range.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	tl_range.value_changed.connect(func(v: float) -> void: _drain(); tl_t = v / 1000.0; playing = false; _sync_tl_ui(); _show_current())
	bar.add_child(tl_range)
	tl_stage_label = Label.new()
	tl_stage_label.custom_minimum_size = Vector2(150, 0)
	bar.add_child(tl_stage_label)
	tl_sim_label = Label.new()
	bar.add_child(tl_sim_label)
	var more := Button.new()
	more.text = "+60 frames"
	more.tooltip_text = "Simulate 60 more frames"
	more.pressed.connect(func() -> void: target += 60; playing = true; _sync_tl_ui())
	bar.add_child(more)
	var reset_start := Button.new()
	reset_start.text = "Reset to start"
	reset_start.tooltip_text = "Put every garment back at its starting placement and simulate again from frame 0"
	reset_start.pressed.connect(_on_reset_to_start)
	bar.add_child(reset_start)
	var resim := Button.new()
	resim.text = "Restart"
	resim.tooltip_text = "Restart the simulation"
	resim.pressed.connect(func() -> void: regen_and_rebuild())
	bar.add_child(resim)


# Starting placement and a fresh simulation of the current pattern (no re-draft, unlike Restart).
func _on_reset_to_start() -> void:
	_drain()
	playing = false
	tl_t = 0.0
	rebuild()


func _sync_tl_ui() -> void:
	if tl_play_btn != null:
		_set_text_if_changed(tl_play_btn, "||" if playing else ">")
	var names := ["1 Cut", "2 Move", "3 Sew", "4 Drape"]
	if tl_stage_label != null:
		var extra := ""
		if tl_t >= 3.0 and (frames.size() < target or target == 0):
			extra = "  (simulating %d/%d)" % [frames.size(), target]
		_set_text_if_changed(tl_stage_label, names[mini(3, int(tl_t))] + extra)
	if tl_sim_label != null:
		_set_text_if_changed(tl_sim_label, "%d/%d fr" % [frames.size(), target])


# Writing identical text still re-lays out the control; this runs every tick during playback.
static func _set_text_if_changed(c: Object, txt: String) -> void:
	if c.get("text") != txt:
		c.set("text", txt)


func _sync_stage_buttons() -> void:
	pass


static func _smooth(x: float) -> float:
	x = clampf(x, 0.0, 1.0)
	return x * x * (3.0 - 2.0 * x)


func _render_stats(_m: float, fr: Dictionary) -> void:
	var done := " - done" if frames.size() >= target and target > 0 else ""
	var warn := health_text + "\n" if health_text != "" else ""  # the garment check says what went wrong, above "done"
	var body := "%s / %s   frame %d/%d   verts %d   KE %s J   gap %.1f mm   clear %.1f mm\nbake %.0f ms   step %.0f ms (newton %d, ls-fail %d)%s" % [
		plan_name, fabric_id, frames.size(), target, _vcount, str(fr.get("ke", 0.0)),
		float(fr.get("gap", 0.0)) * 1000.0, float(fr.get("clear", 0.0)) * 1000.0,
		float(fr.get("bake", 0.0)), float(fr.get("ms", 0.0)),
		int(_solver_stats.get("newton_iterations", 0)), int(_solver_stats.get("line_search_failures", 0)), done]
	_set_text_if_changed(status_label, warn + body + _status_suffix())
	if frames.size() % 30 == 0 and not frames.is_empty():
		print("EDITOR ", status_label.text.replace("\n", " | "))


func _render_info() -> void:
	var fabric_name := fabric_id
	var vb := _tab_vbox("Fabric")
	if vb.has_node("FabricOpt"):
		var o: OptionButton = vb.get_node("FabricOpt")
		if o.item_count > 0 and o.selected >= 0:
			fabric_name = o.get_item_text(o.selected)
	var text: RichTextLabel = _tab_vbox("Pattern").get_node("InfoText")
	text.text = Info.format(plan.get("info", {}), pieces, fabric_name, meas)


func _shot(tag: String) -> void:
	DirAccess.make_dir_recursive_absolute("res://docs-shots")
	var img := sub_view.get_texture().get_image()
	if img == null or img.is_empty():
		get_tree().quit()  # headless: nothing to save, the run itself is the check
		return
	if img.save_png("res://docs-shots/%s.png" % tag) == OK:
		print("SHOT saved ", tag)
	else:
		push_error("SHOT failed for " + tag)
	get_tree().quit()


# Auto framing: cutting mat early, body later (browser V.focus/V.updateCamera).
# The orbit target is the auto-framed point plus pan_off; yaw/pitch/zoom are the user's orbit.
func _update_camera() -> void:
	var h: float = float(meas.get("height", 1.68))
	var s: float = _smooth(tl_t - 0.9)
	cam_pivot.position = Vector3(0.0, lerpf(0.02, h * 0.52, s), lerpf(1.75, 0.0, s)) + pan_off
	# negative pitch looks down (the camera sits above the pivot); the table view (s = 0) looks
	# further down onto the cutting mat. The sign was flipped, which put the camera under the floor.
	cam_pivot.rotation = Vector3(pitch - 0.45 * (1.0 - s), yaw, 0.0)
	view_dist = lerpf(2.9, 3.3 * h / 1.68, s) * zoom
	cam.position = Vector3(0.0, 0.0, view_dist)
	cam.look_at(cam_pivot.global_position)


# ------------------------------------------------------------------ 3D view camera input
# Browser behaviour (app/src/view3d.js): left-drag orbits, right/middle-drag or shift+left-drag pans the target in the
# camera plane, wheel zooms (dolly around the target, clamped), double-click or F / Home resets the view.
# Trackpad: two-finger scroll pans, pinch zooms. Events come from the viewport container only (gui_input), so a drag
# that starts in the viewport keeps going, and nothing over the side panels reaches the camera.
func _bind_view_input() -> void:
	view_pane = $UI/Root/Main/ViewPane as Control
	view_pane.mouse_filter = Control.MOUSE_FILTER_STOP
	view_pane.gui_input.connect(_on_view_gui_input)


# The 3D SubViewport is drawn by a TextureRect instead of a SubViewportContainer: the container's stretch sizes the
# viewport in design units and cannot be overridden, and with stretch off its minimum size grows without bound.
func _show_view_texture() -> void:
	view_image = TextureRect.new()
	view_image.name = "Image"
	view_image.texture = sub_view.get_texture()
	view_image.stretch_mode = TextureRect.STRETCH_SCALE
	view_image.mouse_filter = Control.MOUSE_FILTER_IGNORE
	view_image.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	view_pane.add_child(view_image)
	view_pane.move_child(view_image, 0)


func _on_view_gui_input(e: InputEvent) -> void:
	if _camera_input(e):
		view_pane.accept_event()


# Tracks where the pointer is from every mouse event (before GUI routing), for the keyboard shortcuts.
func _input(e: InputEvent) -> void:
	if e is InputEventMouse and view_pane != null:
		pointer_over_view = view_pane.get_global_rect().has_point(e.position)


func _unhandled_key_input(e: InputEvent) -> void:
	if e is InputEventKey and e.pressed and not e.echo and (e.ctrl_pressed or e.meta_pressed):
		match e.keycode:
			KEY_EQUAL, KEY_PLUS, KEY_KP_ADD:
				_step_ui_scale(1)
			KEY_MINUS, KEY_KP_SUBTRACT:
				_step_ui_scale(-1)
			KEY_0, KEY_KP_0:
				_set_ui_scale_pref(0.0)
			_:
				return
		get_viewport().set_input_as_handled()
		return
	if not pointer_over_view or not (e is InputEventKey) or not e.pressed or e.echo:
		return
	if e.keycode == KEY_F or e.keycode == KEY_HOME:
		_touch_user_cam()
		_reset_view()
		get_viewport().set_input_as_handled()
	elif e.keycode == KEY_G:
		_cycle_render_quality()
		get_viewport().set_input_as_handled()


func _camera_input(e: InputEvent) -> bool:
	if e is InputEventMouseMotion:
		var mask: int = e.button_mask
		var pan_mode: bool = (mask & (MOUSE_BUTTON_MASK_RIGHT | MOUSE_BUTTON_MASK_MIDDLE)) != 0 \
			or ((mask & MOUSE_BUTTON_MASK_LEFT) != 0 and e.shift_pressed)
		if not pan_mode and (mask & MOUSE_BUTTON_MASK_LEFT) == 0:
			return false
		_touch_user_cam()
		if pan_mode:
			_pan_by(e.relative)
		else:
			yaw -= e.relative.x * 0.005
			pitch = clampf(pitch - e.relative.y * 0.005, CAM_PITCH_MIN, CAM_PITCH_MAX)
		return true
	if e is InputEventMouseButton:
		if not e.pressed:
			return false  # wheel notches arrive as press + release: act once
		if e.button_index == MOUSE_BUTTON_WHEEL_UP:
			_touch_user_cam()
			_zoom_by(0.92)
			return true
		if e.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_touch_user_cam()
			_zoom_by(1.08)
			return true
		if e.button_index == MOUSE_BUTTON_LEFT and e.double_click:
			_touch_user_cam()
			_reset_view()
			return true
		return false
	if e is InputEventPanGesture:
		_touch_user_cam()
		_pan_by(e.delta)
		return true
	if e is InputEventMagnifyGesture:
		_touch_user_cam()
		_zoom_by(1.0 / maxf(0.01, e.factor))
		return true
	return false


func _touch_user_cam() -> void:
	user_cam_until = Time.get_ticks_msec() + CAM_IDLE_MS


# Auto-orbit (demo): one step per frame, paused while the user is working the camera.
func _auto_orbit_step() -> void:
	if Time.get_ticks_msec() < user_cam_until:
		return
	yaw += 0.12 / 60.0


func _zoom_by(factor: float) -> void:
	zoom = clampf(zoom * factor, CAM_ZOOM_MIN, CAM_ZOOM_MAX)


# Moves the orbit target so the scene follows the pointer (same sense as the browser: drag down, target goes up).
func _pan_by(rel: Vector2) -> void:
	var b := cam.global_transform.basis
	var k := 0.0015 * view_dist
	pan_off += (-b.x * rel.x + b.y * rel.y) * k
	pan_off = pan_off.limit_length(CAM_PAN_MAX)


func _reset_view() -> void:
	yaw = CAM_YAW0
	pitch = CAM_PITCH0
	zoom = 1.0
	pan_off = Vector3.ZERO
