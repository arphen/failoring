# The Showcase tab (docs/SCENARIOS.md): a scenario dropdown, Render (a background cloth simulation of the whole scenario,
# with progress and cancel), the renders of this session and on disk, and Play: a replay of the cached frames with no
# solver (scrub, speed, loop). Playback takes over the editor's 3D view (cloth, body and props) while it is on, and the
# editor's orbit camera keeps working. Back to editing hands the view back to the editor.
#
# The editor calls drive(delta) every physics frame (it skips its own display while drive returns true) and shutdown()
# when it leaves the tree. The editor's inputs come from showcase_inputs() (editor.gd).
class_name ShowcasePanel
extends RefCounted

const SPEEDS := [0.25, 0.5, 1.0, 2.0]
const SPEED_LABELS := ["0.25x", "0.5x", "1x", "2x"]
const QUALITY_KEYS := ["draft", "preview", "standard"]
const QUALITY_LABELS := ["Draft (fast)", "Preview", "Standard"]
const LEAD := 0.5

var _ed: Node
var _lib: Dictionary = {}
var _scen_opt: OptionButton
var _qual_opt: OptionButton
var _render_btn: Button
var _cancel_btn: Button
var _bar: ProgressBar
var _status: Label
var _list: ItemList
var _rows: Array = []
var _play_btn: Button
var _slider: HSlider
var _time_label: Label
var _speed_opt: OptionButton
var _loop_chk: CheckBox
var _back_btn: Button

var _pre: ShowcasePrerender = null
var _pre_scenario := ""
var _pre_qual := ""
var _mem := {}                      # key -> render (frames in memory), this session
var _cur: Dictionary = {}           # the render on the view
var _cur_scenario: Dictionary = {}
var _pose: ShowcaseScenarios.Pose = null
var _pose0 := PackedVector3Array()  # the first pose of the render's scenario (the lead-in ends here)
var _view := false                  # the showcase owns the 3D view
var _playing := false
var _t := 0.0                       # playhead, scenario seconds (negative during the lead-in)
var _prop_nodes: Array = []
var _st := {}                       # the last poll of the render in flight


func _init(ed: Node, vb: VBoxContainer) -> void:
	_ed = ed
	_lib = ShowcaseScenarios.load_library()
	_build(vb)


# ---------------------------------------------------------------- UI
func _build(vb: VBoxContainer) -> void:
	var title := Label.new()
	title.text = "Showcase"
	vb.add_child(title)
	var note := Label.new()
	note.text = "Each scenario is simulated once for this garment and body, then replayed from the cache without the solver."
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vb.add_child(note)
	if _lib.is_empty():
		var bad := Label.new()
		bad.text = "The scenario library (assets/scenarios.json) is missing."
		vb.add_child(bad)
		return
	_scen_opt = OptionButton.new()
	for id in _lib["order"]:
		_scen_opt.add_item(String(_lib["by_id"][id]["name"]))
	_scen_opt.item_selected.connect(_on_scenario)
	_add_row(vb, "Scenario", _scen_opt)
	_qual_opt = OptionButton.new()
	for lbl in QUALITY_LABELS:
		_qual_opt.add_item(lbl)
	_qual_opt.select(1)
	_qual_opt.tooltip_text = "The simulation quality of the render (Preview is the editor's default)."
	_add_row(vb, "Quality", _qual_opt)
	var rb := HBoxContainer.new()
	_render_btn = Button.new()
	_render_btn.text = "Render"
	_render_btn.tooltip_text = "Simulate the scenario for this garment (in the background), then cache it."
	_render_btn.pressed.connect(_on_render)
	rb.add_child(_render_btn)
	_cancel_btn = Button.new()
	_cancel_btn.text = "Cancel"
	_cancel_btn.disabled = true
	_cancel_btn.pressed.connect(_on_cancel)
	rb.add_child(_cancel_btn)
	vb.add_child(rb)
	_bar = ProgressBar.new()
	_bar.min_value = 0.0
	_bar.max_value = 100.0
	_bar.show_percentage = true
	vb.add_child(_bar)
	_status = Label.new()
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_status.text = "Pick a scenario and press Render."
	vb.add_child(_status)
	var lh := Label.new()
	lh.text = "Renders (this garment plays; others need a render)"
	vb.add_child(lh)
	_list = ItemList.new()
	_list.custom_minimum_size = Vector2(0, 150)
	_list.select_mode = ItemList.SELECT_SINGLE
	_list.item_activated.connect(func(_i: int) -> void: _on_play())
	vb.add_child(_list)
	var pb := HBoxContainer.new()
	_play_btn = Button.new()
	_play_btn.text = "Play"
	_play_btn.pressed.connect(_on_play)
	pb.add_child(_play_btn)
	var pause := Button.new()
	pause.text = "Pause"
	pause.pressed.connect(func() -> void: _set_playing(false))
	pb.add_child(pause)
	_speed_opt = OptionButton.new()
	for lbl in SPEED_LABELS:
		_speed_opt.add_item(lbl)
	_speed_opt.select(2)
	pb.add_child(_speed_opt)
	_loop_chk = CheckBox.new()
	_loop_chk.text = "Loop"
	_loop_chk.button_pressed = true
	pb.add_child(_loop_chk)
	vb.add_child(pb)
	_slider = HSlider.new()
	_slider.min_value = -LEAD
	_slider.max_value = 1.0
	_slider.step = 0.001
	_slider.value_changed.connect(_on_seek)
	_slider.custom_minimum_size = Vector2(160, 0)
	vb.add_child(_slider)
	_time_label = Label.new()
	_time_label.text = "no render on the view"
	vb.add_child(_time_label)
	_back_btn = Button.new()
	_back_btn.text = "Back to editing"
	_back_btn.pressed.connect(_exit_view)
	vb.add_child(_back_btn)
	_refresh_list()


func _add_row(vb: VBoxContainer, label_text: String, ctl: Control) -> void:
	var row := HBoxContainer.new()
	var l := Label.new()
	l.text = label_text
	l.custom_minimum_size = Vector2(72, 0)
	row.add_child(l)
	ctl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(ctl)
	vb.add_child(row)


func _scenario() -> Dictionary:
	if _lib.is_empty():
		return {}
	return _lib["by_id"][_lib["order"][maxi(_scen_opt.selected, 0)]]


func _quality_key() -> String:
	return QUALITY_KEYS[maxi(_qual_opt.selected, 0)]


func _on_scenario(_i: int) -> void:
	var sc := _scenario()
	_loop_chk.button_pressed = bool(sc["loop"])
	_status.text = "%s: %.1f s, %s." % [sc["name"], float(sc["duration"]), "loops" if bool(sc["loop"]) else "plays once"]


func _on_cancel() -> void:
	if _pre != null:
		_pre.cancel()
		_status.text = "Cancelling..."


# ---------------------------------------------------------------- render
func _on_render() -> void:
	if _pre != null:
		return
	var inputs: Dictionary = _ed.showcase_inputs(_quality_key())
	if inputs.is_empty():
		_status.text = "There is no garment on the mannequin to render. Add one from the Library tab."
		return
	var sc := _scenario()
	var key := ShowcasePrerender.key_for(inputs, sc)
	if _mem.has(key) or FileAccess.file_exists(ShowcasePrerender.file_for(key)):
		var r: Dictionary = _mem[key] if _mem.has(key) else ShowcasePrerender.load(ShowcasePrerender.file_for(key))
		if not r.is_empty():
			_mem[key] = r
			_refresh_list()
			_status.text = "%s is already rendered for this garment: press Play." % sc["name"]
			return
	_pre = ShowcasePrerender.new()
	_pre_scenario = String(sc["id"])
	_pre_qual = _quality_key()
	_pre.start({"inputs": inputs, "scenario": sc})
	_render_btn.disabled = true
	_cancel_btn.disabled = false
	_bar.value = 0.0
	_status.text = "Rendering %s (%s)..." % [sc["name"], QUALITY_LABELS[maxi(_qual_opt.selected, 0)]]


# Main thread, every physics frame: a finished render is kept and listed; progress goes to the bar.
func _poll_render() -> void:
	if _pre == null:
		return
	var st := _pre.poll()
	_st = st
	_bar.value = float(st["progress"]) * 100.0
	if not bool(st["done"]):
		_status.text = "Rendering... %d%% (%s, %.0f s)" % [int(float(st["progress"]) * 100.0), String(st["stage"]), float(st["seconds"])]
		return
	var res := _pre.finish()
	_pre = null
	_render_btn.disabled = false
	_cancel_btn.disabled = true
	var err := String(st["error"])
	if res.is_empty():
		_status.text = "Render stopped: %s." % (err if err != "" else "no result")
		return
	var path := ShowcasePrerender.file_for(String(res["key"]))
	var meta := {"name": String(_lib["by_id"][_pre_scenario]["name"]), "preset": String(_ed.call("current_preset"))}
	ShowcasePrerender.save(path, res, meta)
	_mem[String(res["key"])] = res
	_refresh_list()
	_status.text = "Rendered %s in %.0f s. Press Play." % [meta["name"], float(st["seconds"])]


# ---------------------------------------------------------------- list
func _refresh_list() -> void:
	if _list == null:
		return
	_rows = ShowcasePrerender.list_saved()
	_list.clear()
	for row in _rows:
		var sc: Dictionary = _lib["by_id"].get(String(row["scenario"]), {})
		var name := String(sc.get("name", row["scenario"]))
		var when := Time.get_datetime_string_from_unix_time(int(row["created"]), true) if int(row["created"]) > 0 else ""
		_list.add_item("%s, %s, %d frames, %s" % [name, String(row["quality"]), int(row["frames"]), when])


# ---------------------------------------------------------------- playback
func _on_play() -> void:
	if _pre != null:
		return
	var inputs: Dictionary = _ed.showcase_inputs(_quality_key())
	var sc := _scenario()
	if inputs.is_empty():
		_status.text = "There is no garment on the mannequin to play."
		return
	var key := ShowcasePrerender.key_for(inputs, sc)
	var r: Dictionary = {}
	var sel := _list.get_selected_items()
	if not sel.is_empty() and sel[0] < _rows.size():
		var row: Dictionary = _rows[sel[0]]
		if String(row["key"]) != key:
			_status.text = "That render is for another garment, scenario or quality. Render this one to play it."
			return
	if _mem.has(key):
		r = _mem[key]
	elif FileAccess.file_exists(ShowcasePrerender.file_for(key)):
		r = ShowcasePrerender.load(ShowcasePrerender.file_for(key))
		_mem[key] = r
	if r.is_empty():
		_status.text = "%s is not rendered for this garment yet: press Render." % sc["name"]
		return
	_enter_view(r, sc)
	_set_playing(true)


func _set_playing(on: bool) -> void:
	if not _view:
		return
	_playing = on
	_play_btn.text = "Play" if not on else "Playing"


# Makes the render the one on the view: props and the stand body are made, the playhead goes to the lead-in.
func _enter_view(r: Dictionary, sc: Dictionary) -> void:
	_cur = r
	_cur_scenario = sc
	_pose = ShowcaseScenarios.Pose.new(_ed.M, _ed.body_rest)
	var first := ShowcaseScenarios.sample(sc, 0.0)
	_pose0 = _pose.evaluate(first["rot"], first["root"])["verts"]
	_make_props(sc)
	_view = true
	_t = -LEAD
	_slider.set_value_no_signal(_t)
	_slider.min_value = -LEAD
	_slider.max_value = float(sc["duration"])
	_status.text = "Playing %s (cached, no solver)." % sc["name"]
	_show_frame()


func _make_props(sc: Dictionary) -> void:
	_clear_props()
	var box := ShowcaseScenarios.box_prop(sc)
	if not box.is_empty():
		_prop_nodes.append(_prop_node(box["half"], Color(0.78, 0.6, 0.4)))
	for part in ShowcaseScenarios.chair_parts(sc):
		var n := _prop_node(part["half"], Color(0.45, 0.3, 0.18))
		n.position = part["center"]
		_prop_nodes.append(n)


func _prop_node(half: Vector3, col: Color) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = half * 2.0
	mi.mesh = bm
	var mat := StandardMaterial3D.new()
	mat.albedo_color = col
	mat.roughness = 0.85
	mi.material_override = mat
	_ed.stage.add_child(mi)
	return mi


func _clear_props() -> void:
	for n in _prop_nodes:
		if is_instance_valid(n):
			n.queue_free()
	_prop_nodes = []


func _show_frame() -> void:
	var frames: Array = _cur["frames"]
	var idx := clampi(int(round((_t + LEAD) * 60.0)), 0, frames.size() - 1)
	var fr: Dictionary = frames[idx]
	_ed.call("_set_cloth_mesh", _to_v3(fr["pos"]), _to_v3(fr["nrm"]))
	var verts: PackedVector3Array
	if _t < 0.0:
		var u := ShowcaseScenarios.smooth_step((_t + LEAD) / LEAD)
		var stand: PackedVector3Array = _ed._stand_v3
		verts = PackedVector3Array()
		verts.resize(stand.size())
		for i in stand.size():
			verts[i] = stand[i].lerp(_pose0[i], u)
	else:
		var s := ShowcaseScenarios.sample(_cur_scenario, _t)
		verts = _pose.evaluate(s["rot"], s["root"])["verts"]
	_ed.body_node.show_pose(verts)
	_ed._body_key = ""
	if not _prop_nodes.is_empty() and not ShowcaseScenarios.box_prop(_cur_scenario).is_empty():
		_prop_nodes[0].position = fr["box"]
	_slider.set_value_no_signal(_t)
	_time_label.text = "%.2f / %.2f s%s" % [_t, float(_cur_scenario["duration"]), "  (lead-in)" if _t < 0.0 else ""]


func _on_seek(v: float) -> void:
	if not _view:
		return
	_t = v
	_show_frame()


# The render's frames as Vector3 (the display positions and normals of the sim).
static func _to_v3(flat: PackedFloat32Array) -> PackedVector3Array:
	var out := PackedVector3Array()
	var n := flat.size() / 3
	out.resize(n)
	for i in n:
		out[i] = Vector3(flat[3 * i], flat[3 * i + 1], flat[3 * i + 2])
	return out


# Back to editing: the editor shows its own state again.
func _exit_view() -> void:
	if not _view:
		return
	_view = false
	_playing = false
	_clear_props()
	_cur = {}
	_pose = null
	_pose0 = PackedVector3Array()
	_play_btn.text = "Play"
	_time_label.text = "no render on the view"
	_ed._body_key = ""
	_ed._last_shown = ""
	_ed.call("_show_current")
	_status.text = "Back to editing."


# ---------------------------------------------------------------- the editor's hooks
# Called every physics frame by the editor. Returns true while the showcase owns the 3D view (the editor then skips its own).
func drive(delta: float) -> bool:
	_poll_render()
	if _view and _playing:
		var speed: float = SPEEDS[clampi(_speed_opt.selected, 0, SPEEDS.size() - 1)]
		_t += delta * speed
		var dur := float(_cur_scenario["duration"])
		if _t >= dur:
			if _loop_chk.button_pressed:
				_t = 0.0
			else:
				_t = dur
				_playing = false
				_play_btn.text = "Play"
		_show_frame()
	return _view


func shutdown() -> void:
	if _pre != null:
		_pre.cancel()
		_pre.finish()
		_pre = null
	if _view:
		_clear_props()
		_view = false


# ---------------------------------------------------------------- the API of render mode (render_mode.gd)
func api_scenarios() -> Array:
	var out := []
	if _lib.is_empty():
		return out
	for id in _lib["order"]:
		var sc: Dictionary = _lib["by_id"][id]
		out.append({"id": String(id), "name": String(sc["name"]), "duration": float(sc["duration"]), "loop": bool(sc["loop"])})
	return out


# Starts the cloth simulation of a scenario for the garment on the mannequin, or finds it cached. {state: "cached" | "started" |
# "error", key, message}.
func api_begin(id: String, qkey: String) -> Dictionary:
	if _lib.is_empty():
		return {"state": "error", "message": "The scenario library (assets/scenarios.json) is missing."}
	if _pre != null:
		return {"state": "error", "message": "A render is already running."}
	_scen_opt.select(maxi(_lib["order"].find(id), 0))
	_qual_opt.select(maxi(QUALITY_KEYS.find(qkey), 0))
	var inputs: Dictionary = _ed.showcase_inputs(qkey)
	if inputs.is_empty():
		return {"state": "error", "message": "There is no garment on the mannequin to render. Add one from the Library."}
	var sc := _scenario()
	var key := ShowcasePrerender.key_for(inputs, sc)
	if _mem.has(key) or FileAccess.file_exists(ShowcasePrerender.file_for(key)):
		if not _mem.has(key):
			var r := ShowcasePrerender.load(ShowcasePrerender.file_for(key))
			if not r.is_empty():
				_mem[key] = r
		if _mem.has(key):
			return {"state": "cached", "key": key, "message": ""}
	_on_render()
	return {"state": "started" if _pre != null else "error", "key": key, "message": _status.text}


# The render in flight: {busy, progress 0..1, stage, seconds, status}.
func api_status() -> Dictionary:
	return {"busy": _pre != null, "progress": float(_st.get("progress", 0.0)), "stage": String(_st.get("stage", "")),
		"seconds": float(_st.get("seconds", 0.0)), "status": _status.text if _status != null else ""}


func api_cancel() -> void:
	_on_cancel()


# True when this scenario's simulation is rendered for the garment on the mannequin.
func api_ready(id: String, qkey: String) -> bool:
	var inputs: Dictionary = _ed.showcase_inputs(qkey)
	if inputs.is_empty() or _lib.is_empty():
		return false
	var key := ShowcasePrerender.key_for(inputs, _lib["by_id"][id])
	return _mem.has(key)


# Puts the simulated frames on the 3D view, paused at the lead-in.
func api_enter(id: String, qkey: String) -> bool:
	_scen_opt.select(maxi(_lib["order"].find(id), 0))
	_qual_opt.select(maxi(QUALITY_KEYS.find(qkey), 0))
	_list.deselect_all()
	_on_play()
	_set_playing(false)
	return _view


func api_show(t: float) -> void:
	if _view:
		_t = t
		_show_frame()


func api_duration() -> float:
	return float(_cur_scenario.get("duration", 0.0))


func api_in_view() -> bool:
	return _view


func api_exit() -> void:
	_exit_view()


# The cache key of a scenario's simulation for the garment on the mannequin ("" without a garment).
func api_key(id: String, qkey: String) -> String:
	var inputs: Dictionary = _ed.showcase_inputs(qkey)
	if inputs.is_empty() or _lib.is_empty():
		return ""
	return ShowcasePrerender.key_for(inputs, _lib["by_id"][id])
