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
var sel = null
var sel_v := -1
var sew_a = null
var pieces: Array = []
var seams: Array = []
var tool := "select"
var snap := true
var undo: Array = []
var redo: Array = []
var table_dirty := true
var table: Control
var stage: Node3D
var sub_view: SubViewport

var frame := 0
var walk_k := 0.0
var playing := false
var bake_ms := 0.0
var step_ms := 0.0

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

var plan_opt: OptionButton
var quality_opt: OptionButton
var motion_opt: OptionButton
var status_label: Label
@onready var tabs: TabContainer = $UI/Root/Main/Right/Tabs


func _ready() -> void:
	table = $UI/Root/Main/TablePane/Table
	table.ed = self
	stage = $UI/Root/Main/ViewPane/View/Stage
	sub_view = $UI/Root/Main/ViewPane/View
	_build_scene()
	_apply_auto_env()
	_build_ui()
	_build_table_bar()
	M = BodyGen.load_data(PlanLoader.load_json("res://assets/body_data.json"))
	meas = (BodyGen.PRESETS["average"] as Dictionary).duplicate()
	if OS.has_environment("FL_STAND"):
		motion = "stand"
	debounce = Timer.new()
	debounce.one_shot = true
	debounce.wait_time = 0.25
	debounce.timeout.connect(_on_debounce)
	add_child(debounce)
	regen_and_rebuild()


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


# ------------------------------------------------------------------ scene
func _build_scene() -> void:
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.12, 0.13, 0.15)
	env.environment.ambient_light_color = Color(0.6, 0.6, 0.65)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	stage.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	stage.add_child(sun)
	cam_pivot = Node3D.new()
	cam_pivot.position = Vector3(0, 0.9, 0)
	stage.add_child(cam_pivot)
	cam = Camera3D.new()
	cam.position = Vector3(0, 0, dist)
	cam_pivot.add_child(cam)
	body_node = BodyView.new()
	var skin := StandardMaterial3D.new()
	skin.albedo_color = Color(0.85, 0.68, 0.58)
	body_node.material_override = skin
	stage.add_child(body_node)
	cloth_node = MeshInstance3D.new()
	cloth_node.extra_cull_margin = 0.5
	var cmat := StandardMaterial3D.new()
	cmat.albedo_color = Color(0.25, 0.45, 0.8)
	cmat.cull_mode = BaseMaterial3D.CULL_DISABLED
	cloth_node.material_override = cmat
	stage.add_child(cloth_node)


# ------------------------------------------------------------------ UI
func _build_ui() -> void:
	var box: VBoxContainer = $UI/Root/Main/Left/VBox
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
	_build_tabs()


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


func _build_tabs() -> void:
	for tab in ["Piece", "Seams", "Body", "Fabric", "Pattern", "Shoot"]:
		var sc := ScrollContainer.new()
		sc.name = tab
		var vb := VBoxContainer.new()
		vb.name = "VBox"
		vb.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		sc.add_child(vb)
		tabs.add_child(sc)
	_build_body_tab()
	_build_fabric_tab()
	_build_pattern_tab()
	_placeholder_tab("Piece", "Piece editing arrives with the cutting table.")
	_placeholder_tab("Seams", "Seam editing arrives with the cutting table.")
	_placeholder_tab("Shoot", "Photo shoot arrives in a later commit.")


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


func _placeholder_tab(tab: String, text: String) -> void:
	var l := Label.new()
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.text = text
	_tab_vbox(tab).add_child(l)


func _slider_row(parent: VBoxContainer, label_text: String, minv: float, maxv: float, step: float, get: Callable, set: Callable, fmt: Callable) -> HSlider:
	var h := HBoxContainer.new()
	var l := Label.new()
	l.custom_minimum_size = Vector2(92, 0)
	l.text = label_text
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
	s.value_changed.connect(func(v: float) -> void: o.text = fmt.call(v); set.call(v))
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
	o.item_selected.connect(func(i: int) -> void: fabric_id = String(o.get_item_metadata(i)); _schedule())
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


# ------------------------------------------------------------------ state changes
func _schedule() -> void:
	debounce.start()


func snapshot() -> String:
	return JSON.stringify({"pieces": pieces, "seams": seams})


func push_undo() -> void:
	undo.append(snapshot())
	if undo.size() > 60:
		undo.pop_front()
	redo.clear()


func do_undo() -> void:
	if undo.is_empty():
		return
	redo.append(snapshot())
	var o = JSON.parse_string(undo.pop_back())
	pieces = o["pieces"]
	seams = o["seams"]
	_fix_selection()
	changed("structure")


func do_redo() -> void:
	if redo.is_empty():
		return
	undo.append(snapshot())
	var o = JSON.parse_string(redo.pop_back())
	pieces = o["pieces"]
	seams = o["seams"]
	_fix_selection()
	changed("structure")


func _fix_selection() -> void:
	if Table.piece_by_id(pieces, sel) == null:
		sel = null
	sel_v = -1
	sew_a = null


# kind: load (new plan), structure (rebuild sim), structure-drag (rebuild debounced),
# table/table-done (table moved), ui (redraw), body/fabric (regen + rebuild)
func changed(kind: String) -> void:
	table_dirty = true
	if kind == "load":
		sim = null
		undo.clear()
		redo.clear()
		table.fitted = false
		table.fit()
		regen_and_rebuild()
	elif kind == "structure" or kind == "structure-drag" or kind == "body" or kind == "fabric":
		_schedule()
	elif kind == "table" or kind == "table-done" or kind == "ui":
		pass


func _process(_delta: float) -> void:
	if table_dirty and table != null:
		table_dirty = false
		table.queue_redraw()


func _on_debounce() -> void:
	regen_and_rebuild()


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
	var snap_box := CheckBox.new()
	snap_box.text = "snap 5 mm"
	snap_box.button_pressed = true
	snap_box.toggled.connect(func(on: bool) -> void: snap = on)
	bar.add_child(snap_box)
	var hint := Label.new()
	hint.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hint.clip_text = true
	hint.add_theme_font_size_override("font_size", 12)
	bar.add_child(hint)
	table.hint_label = hint
	table.set_tool("select")


func _on_preset_button(p: String) -> void:
	meas = (BodyGen.PRESETS[p] as Dictionary).duplicate()
	_rebuild_body_tab()
	_schedule()


func _on_plan(_i: int) -> void:
	plan_key = _plan_entries()[plan_opt.selected][1]
	_resolve_plan()
	_reset_working()
	changed("load")


func _on_quality(_i: int) -> void:
	quality = QUALITIES.keys()[quality_opt.selected]
	_schedule()


func _on_motion(_i: int) -> void:
	motion = "walk" if motion_opt.selected == 1 else "stand"
	regen_and_rebuild()


func _on_redraft() -> void:
	_resolve_plan()
	_reset_working()
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
	plan = PlanLoader.normalize_plan(loaded)
	plan_key = "custom"
	plan_name = String(plan.get("name", "Untitled"))
	plan_notes = String(plan.get("notes", ""))
	if String(plan.get("fabric", "")) != "":
		fabric_id = String(plan["fabric"])
	_reset_working()
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


func _resolve_plan() -> void:
	var key := plan_key
	if key == "pants":
		plan = Draft.pants(body_stand, _helper_wrap(), {})
	elif key == "tank":
		plan = Draft.tank(body_stand, _helper_wrap(), {})
	elif key == "shirt":
		plan = Draft.shirt(body_stand, _helper_wrap(), {})
	elif key.begins_with("fs-"):
		var g := key.trim_prefix("fs-")
		plan = PlanLoader.load_json("res://assets/%s/plan_%s.json" % [_fs_preset(meas), g])
	elif Builtin.all().has(key):
		plan = (Builtin.all()[key] as Dictionary).duplicate(true)
	elif key != "custom":
		plan_key = "fs-teagan"
		_resolve_plan()
		return
	plan = PlanLoader.normalize_plan(plan)
	plan_name = String(plan.get("name", key))
	if String(plan.get("fabric", "")) != "" and key != "custom":
		fabric_id = String(plan["fabric"])
	plan_notes = String(plan.get("notes", ""))


func _reset_working() -> void:
	pieces = (plan.get("pieces", []) as Array).duplicate(true)
	seams = (plan.get("seams", []) as Array).duplicate(true)
	undo.clear()
	redo.clear()
	sel = String(pieces[0]["id"]) if not pieces.is_empty() else null
	sel_v = -1
	sew_a = null


func regen_and_rebuild() -> void:
	if M.is_empty():
		return
	body_rest = BodyGen.generate(M, meas, prev_body)
	prev_body = {"x": (body_rest as Dictionary)["x"], "keys": (body_rest as Dictionary)["keys"]}
	body_stand = BodyGen.stand(M, body_rest)
	if plan_key in ["pants", "tank", "shirt"] or pieces.is_empty():
		_resolve_plan()
		_reset_working()
	_refresh_fabric_tab()
	_refresh_body_note()
	rebuild()


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
	sim = null
	frame = 0
	walk_k = 0.0
	if plan.is_empty() or body_stand == null:
		return
	var q: Dictionary = QUALITIES[quality]
	var valid := Table.valid_pieces(pieces)
	var spec := {"pieces": valid["pieces"], "seams": seams}
	var built: Dictionary = PlanLoader.build(spec, body_stand["sections"], body_stand["arms"],
		_to_v3a(body_stand["pos"]), _tris(), fabric_id, q, seam_stiffness, ramp_frames,
		self_collision or bool(q.get("self", false)))
	if built.is_empty():
		status_label.text = "sim build failed"
		return
	sim = built["sim"]
	meta = built
	wraps.clear()
	for pid in built["order"]:
		wraps[pid] = Wrap.new(Table.piece_by_id(pieces, pid), body_stand["sections"], body_stand["arms"])
	_build_mesh_edges()
	_build_stitches()
	_build_mat()
	body_node.use_custom(_to_v3a(body_stand["pos"]), _tris())
	body_node.show_pose(_to_v3a(body_stand["pos"]))
	frames.clear()
	frame = 0
	walk_k = 0.0
	var qf: int = QUALITIES[quality]["frames"]
	target = qf + (96 if motion == "walk" else qf)
	playing = false
	if OS.has_environment("FL_AUTO"):
		tl_t = 4.0
		playing = true
	if OS.has_environment("FL_T"):
		tl_t = clampf(float(OS.get_environment("FL_T")), 0.0, 4.0)
	_sync_tl_ui()
	_render_info()
	_show_current()
	status_label.text = "%d verts - press play" % int(built["n"])


func _tris() -> PackedInt32Array:
	return M["tris"]


func _physics_process(_delta: float) -> void:
	_update_camera()
	if sim == null:
		return
	if playing:
		_advance()
	_show_current()
	_tick += 1
	if _tick % 20 == 0:
		print("HEARTBEAT playing=%s frames=%d tl=%.2f" % [playing, frames.size(), tl_t])
	if OS.has_environment("FL_UISHOT") and (frames.size() == 10 or _tick >= 150):
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
		var guard := 0
		while frames.size() < need and guard < 4:
			_step_frame()
			guard += 1
		tl_t = minf(4.0, tl_t + tl_speed / 60.0 / maxf(1.5, float(target) / 24.0))
		if tl_t >= 4.0 and frames.size() >= target:
			playing = false
	_sync_tl_ui()


func _frame_idx() -> int:
	if frames.is_empty():
		return -1
	if tl_t >= 4.0:
		return frames.size() - 1
	var n := maxi(2, target if target > 0 else frames.size())
	return mini(int(round(clampf(tl_t - 3.0, 0.0, 1.0) * float(n - 1))), frames.size() - 1)


func _step_frame() -> void:
	var q: Dictionary = QUALITIES[quality]
	var settle: int = q["frames"]
	var t0 := Time.get_ticks_usec()
	bake_ms = 0.0
	var pose_pos := _to_v3a(body_stand["pos"])
	frame = frames.size() + 1
	if motion == "walk" and frame > settle:
		walk_k += 1.0
		var stride: float = _smooth(minf(1.0, walk_k / RAMP))
		var phase: float = fposmod(walk_k / CYCLE, 1.0)
		var pose := BodyGen.pose(M, body_rest, phase, {"stride": stride, "arms": stride})
		pose_pos = _to_v3a(pose["pos"])
		bake_ms = sim.set_body_positions(pose_pos)
	var f := float(frame)
	if f <= float(ramp_frames) + 1.0 and ramp_frames > 0:
		sim.set_param("gravity_y", -9.81 * minf(1.0, pow(f / float(ramp_frames), 2.0)))
	var t1 := Time.get_ticks_usec()
	sim.step(1)
	var t2 := Time.get_ticks_usec()
	step_ms = (t2 - t1) / 1000.0
	frames.append({"pos": sim.get_display_positions(0.0008), "cloth": sim.get_positions(),
		"body": pose_pos, "ke": sim.get_kinetic_energy(), "gap": sim.get_max_seam_gap(),
		"clear": sim.get_min_body_clearance(), "ms": (t2 - t1) / 1000.0, "bake": bake_ms})
	if frames.size() % 30 == 0:
		_render_stats((t2 - t0) / 1000.0, frames[frames.size() - 1])
		print("EDITOR ", status_label.text.replace("\n", " | "))
	if OS.has_environment("FL_AUTO"):
		var parts := OS.get_environment("FL_AUTO").split(",")
		if parts.size() >= 3 and frames.size() >= int(parts[2]):
			print("EDITOR auto target %s frames reached, quitting" % parts[2])
			_shot(parts[0] + "_" + parts[1])


var _last_shown := ""


func _show_current() -> void:
	if tl_range != null and (not tl_range.has_focus() or playing):
		tl_range.set_value_no_signal(tl_t * 1000.0)
	_sync_stage_buttons()
	if DisplayServer.get_name() == "headless":
		var hi := _frame_idx()
		if hi >= 0:
			_render_stats(float(frames[hi]["ms"]), frames[hi])
		return
	var key := "%d|%d" % [int(tl_t * 1000.0), _frame_idx()]
	if key == _last_shown:
		return
	_last_shown = key
	if tl_t < 2.0:
		_stage_pose()
		body_node.show_pose(_to_v3a(body_stand["pos"]))
		_update_stitches(cur_cloth, tl_t)
	elif tl_t < 3.0:
		var p2: PackedVector3Array = meta["p2"]
		_set_cloth_mesh(_with_midpoints(p2))
		cur_cloth = p2.slice(0, meta["n"] * 3) if p2.size() >= meta["n"] * 3 else p2
		body_node.show_pose(_to_v3a(body_stand["pos"]))
		_update_stitches(cur_cloth, tl_t)
	else:
		var i := _frame_idx()
		if i < 0:
			var p2b: PackedVector3Array = meta["p2"]
			_set_cloth_mesh(_with_midpoints(p2b))
			cur_cloth = p2b
			body_node.show_pose(_to_v3a(body_stand["pos"]))
		else:
			var fr: Dictionary = frames[i]
			_set_cloth_mesh(fr["pos"])
			cur_cloth = fr["cloth"]
			body_node.show_pose(fr["body"])
			_update_stitches(cur_cloth, tl_t)
			_render_stats(float(fr["ms"]), fr)


var cur_cloth := PackedVector3Array()


func _set_cloth_mesh(disp: PackedVector3Array) -> void:
	var idx: PackedInt32Array = sim.get_display_indices()
	if disp.is_empty() or idx.is_empty():
		return
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = disp
	arr[Mesh.ARRAY_INDEX] = idx
	var m := ArrayMesh.new()
	m.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	cloth_node.mesh = m


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


func _table_layout() -> Dictionary:
	var x0 := 1e9
	var y0 := 1e9
	var x1 := -1e9
	var y1 := -1e9
	for p in pieces:
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
		var mm := StandardMaterial3D.new()
		mm.albedo_color = Color(0.16, 0.27, 0.25)
		mat_node.material_override = mm
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
		var p = Table.piece_by_id(pieces, pid)
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
	for s in seams:
		var a = Table.piece_by_id(pieces, String(s["a"][0]))
		var b = Table.piece_by_id(pieces, String(s["b"][0]))
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
	start.pressed.connect(func() -> void: tl_t = 0.0; playing = false; _sync_tl_ui(); _show_current())
	bar.add_child(start)
	var back := Button.new()
	back.text = "<<"
	back.tooltip_text = "Step back"
	back.pressed.connect(func() -> void: tl_t = clampf(tl_t - 0.1, 0.0, 4.0); playing = false; _sync_tl_ui(); _show_current())
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
	fwd.pressed.connect(func() -> void: tl_t = clampf(tl_t + 0.1, 0.0, 4.0); playing = false; _sync_tl_ui(); _show_current())
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
		b.pressed.connect(func() -> void: tl_t = float(s); playing = false; _sync_tl_ui(); _show_current())
		bar.add_child(b)
	tl_range = HSlider.new()
	tl_range.min_value = 0.0
	tl_range.max_value = 4000.0
	tl_range.step = 1.0
	tl_range.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	tl_range.value_changed.connect(func(v: float) -> void: tl_t = v / 1000.0; playing = false; _sync_tl_ui(); _show_current())
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
	var resim := Button.new()
	resim.text = "Restart"
	resim.tooltip_text = "Restart the simulation"
	resim.pressed.connect(func() -> void: regen_and_rebuild())
	bar.add_child(resim)


func _sync_tl_ui() -> void:
	if tl_play_btn != null:
		tl_play_btn.text = "||" if playing else ">"
	var names := ["1 Cut", "2 Move", "3 Sew", "4 Drape"]
	if tl_stage_label != null:
		var extra := ""
		if tl_t >= 3.0 and (frames.size() < target or target == 0):
			extra = "  (simulating %d/%d)" % [frames.size(), target]
		tl_stage_label.text = names[mini(3, int(tl_t))] + extra
	if tl_sim_label != null:
		tl_sim_label.text = "%d/%d fr" % [frames.size(), target]


func _sync_stage_buttons() -> void:
	pass


static func _smooth(x: float) -> float:
	x = clampf(x, 0.0, 1.0)
	return x * x * (3.0 - 2.0 * x)


func _render_stats(_m: float, fr: Dictionary) -> void:
	var st: Dictionary = sim.get_solver_stats()
	var done := " - done" if frames.size() >= target and target > 0 else ""
	status_label.text = "%s / %s   frame %d/%d   verts %d   KE %s J   gap %.1f mm   clear %.1f mm\nbake %.0f ms   step %.0f ms (newton %d, ls-fail %d)%s" % [
		plan_name, fabric_id, frames.size(), target, sim.get_vertex_count(), str(fr.get("ke", 0.0)),
		float(fr.get("gap", 0.0)) * 1000.0, float(fr.get("clear", 0.0)) * 1000.0,
		float(fr.get("bake", 0.0)), float(fr.get("ms", 0.0)),
		int(st.get("newton_iterations", 0)), int(st.get("line_search_failures", 0)), done]
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
func _update_camera() -> void:
	var h: float = float(meas.get("height", 1.68))
	var s: float = _smooth(tl_t - 0.9)
	cam_pivot.position = Vector3(0.0, lerpf(0.02, h * 0.52, s), lerpf(1.75, 0.0, s))
	cam_pivot.rotation = Vector3(pitch + 0.45 * (1.0 - s), yaw, 0.0)
	cam.position = Vector3(0.0, 0.0, lerpf(2.9, 3.3 * h / 1.68, s) * zoom)
	cam.look_at(cam_pivot.global_position)


func _unhandled_input(e: InputEvent) -> void:
	var vp_rect: Rect2 = ($UI/Root/Main/ViewPane as SubViewportContainer).get_global_rect()
	if not vp_rect.has_point(get_viewport().get_mouse_position()):
		return
	if e is InputEventMouseMotion and (e.button_mask & MOUSE_BUTTON_MASK_LEFT):
		yaw -= e.relative.x * 0.005
		pitch = clampf(pitch - e.relative.y * 0.005, -1.4, 1.4)
	elif e is InputEventMouseButton and e.pressed:
		if e.button_index == MOUSE_BUTTON_WHEEL_UP:
			zoom = clampf(zoom * 0.92, 0.25, 3.0)
		elif e.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			zoom = clampf(zoom * 1.08, 0.25, 3.0)
