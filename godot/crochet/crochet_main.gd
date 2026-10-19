# The crochet scene (docs/CROCHET_PLAN.md section 8.2): a pattern editor with live compile, the 3D view of the simulated piece,
# and the metrics. Built in code; the simulation is the C++ CrochetScene (the same run as tools/crochet_cli), stepped on a worker
# thread so the UI stays alive. Headless runs (tests) step synchronously.
#
# Public for the tests: load_text(), compile_now(), start_run(scene, load), run_frames(n), mesh_vertex_count(), summary_text(), error_text().
extends Control

const Studio = preload("res://studio.gd")
const Look = preload("res://look.gd")
const YARNS_PATH := "res://materials/yarns.json"
const COMPILE_DELAY := 0.4
const Cloth = preload("res://crochet/crochet_cloth.gd")
const Records = preload("res://crochet/crochet_records.gd")
const Orbit = preload("res://crochet/crochet_orbit.gd")
const HANG_Y := 1.35    # the hang point is put this high in the studio (metres)

# ---- state
var pattern: CrochetPattern
var sim: CrochetScene
var _text_changed := false
var _compile_thread: Thread
var _compile_job: CrochetPattern
var _compile_pending := false
var _compile_ok := false
var _run_thread: Thread
var _running := false
var _stop_requested := false
var _chunk := 2          # frames per worker job; adapts to keep a job near 150 ms
var _fixed_chunk := false
var _use_worker := false
var _yarns: Array = []
var _frames_total := 0
var _y_offset := 0.0
var _have_offset := false
var _fixtures: Array = []
var last_vertices := PackedVector3Array()   # what the view shows (tests read it: the headless renderer cannot be queried)

# ---- UI
var editor: CodeEdit
var status_label: Label
var error_label: Label
var phase_label: Label
var scene_opt: OptionButton
var load_opt: OptionButton
var run_btn: Button
var stop_btn: Button
var yarn_opt: OptionButton
var hook_spin: SpinBox
var tension_spin: SpinBox
var fixture_opt: OptionButton
var metrics_text: RichTextLabel
var chart_holder: Control
var view_container: SubViewportContainer
var sub_view: SubViewport
var stage: Node3D
var orbit := Orbit.new()   # the camera: pose, limits, mouse and trackpad handling (crochet_orbit.gd)
var cam_pivot: Node3D
var cam: Camera3D
var cloth_node: MeshInstance3D
var _mat_rs: ShaderMaterial
var _mat_ws: ShaderMaterial
var load_node: MeshInstance3D
var peg_node: MeshInstance3D
var _debounce: Timer
var _frame_lo := Vector3(1, 1, 1)    # the box the camera last framed (the Fit button frames it again); empty until the first one
var _frame_hi := Vector3(-1, -1, -1)
var selection := PackedInt32Array()           # the selected stitch ids (a stitch, or all the stitches of a pattern line)
var _synced_line := -1                        # the editor line we moved the caret to ourselves; its caret event is not a user's click
var _stitch_of_vertex := PackedInt32Array()
var _vertex_of_stitch := PackedInt32Array()
var _indices := PackedInt32Array()
var _line_stitches := {}                      # pattern line (1-based) -> PackedInt32Array of stitch ids
var compare_view: Control                     # crochet_compare.gd
var yarn_view: Control                        # yarn_view.gd
var calibrate_view: Control                   # crochet_calibrate.gd
var records: Array = []                       # the calibration records (crochet_records.gd)
var edit_menu: PopupMenu                      # the right-click menu of the chart (plan 9.2)
var _whole_row := false                       # "whole row" ticked in that menu
const OVERRIDE_MODS := ["sc", "hdc", "dc", "tr", "slst", "blo", "flo", "both"]
const OVERRIDE_LABELS := ["Single crochet (sc)", "Half double (hdc)", "Double (dc)", "Treble (tr)", "Slip stitch (sl st)", "Back loop only (BLO)", "Front loop only (FLO)", "Both loops"]
const WHOLE_ROW_ID := 100

func _ready() -> void:
	_use_worker = DisplayServer.get_name() != "headless" or OS.has_environment("FL_THREAD")
	if OS.has_environment("FL_SYNC"):
		_use_worker = false
	_load_yarns()
	records = Records.load_all(Records.path())
	_build_ui()
	_debounce = Timer.new()
	_debounce.one_shot = true
	_debounce.wait_time = COMPILE_DELAY
	_debounce.timeout.connect(_on_debounce)
	add_child(_debounce)
	if OS.has_environment("FL_CHUNK"):
		_chunk = maxi(1, int(OS.get_environment("FL_CHUNK")))
		_fixed_chunk = true
	if not OS.has_environment("FL_NO_DEFAULT"):
		_load_fixture_named(OS.get_environment("FL_PATTERN") if OS.has_environment("FL_PATTERN") else "pouch")
	if OS.has_environment("FL_AUTORUN"):  # "hang_pin,mba13": start a run right away (screenshots, demos)
		var parts := OS.get_environment("FL_AUTORUN").split(",")
		start_run(parts[0], parts[1] if parts.size() > 1 else "none")

func _exit_tree() -> void:
	_stop_requested = true
	if _run_thread != null and _run_thread.is_started():
		_run_thread.wait_to_finish()
	if _compile_thread != null and _compile_thread.is_started():
		_compile_thread.wait_to_finish()

# ------------------------------------------------------------------------------------------------ data

func _repo_root() -> String:
	return ProjectSettings.globalize_path("res://") + "/.."

func _load_yarns() -> void:
	var f := FileAccess.open(YARNS_PATH, FileAccess.READ)
	if f == null:
		return
	var d = JSON.parse_string(f.get_as_text())
	if d is Dictionary:
		_yarns = d.get("yarns", [])

func _list_fixtures() -> Array:
	var out: Array = []
	var dir := DirAccess.open(_repo_root() + "/patterns/cp")
	if dir == null:
		return out
	for f in dir.get_files():
		if f.ends_with(".txt"):
			out.append(f.get_basename())
	out.sort()
	return out

# ------------------------------------------------------------------------------------------------ UI

func _build_ui() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var root := VBoxContainer.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(root)
	root.add_child(_build_top_bar())
	var split_a := HSplitContainer.new()
	split_a.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root.add_child(split_a)
	split_a.add_child(_build_left())
	var split_b := HSplitContainer.new()
	split_b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	split_a.add_child(split_b)
	split_b.add_child(_build_view())
	split_b.add_child(_build_right())
	compare_view = Control.new()   # the A/B and variants view, hidden until "Compare"
	compare_view.set_script(load("res://crochet/crochet_compare.gd"))
	compare_view.main = self
	compare_view.visible = false
	add_child(compare_view)
	compare_view.closed.connect(func() -> void: set_process(true))
	yarn_view = Control.new()   # yarn mode: crochet one hook action at a time, hidden until "Yarn mode"
	yarn_view.set_script(load("res://crochet/yarn_view.gd"))
	yarn_view.visible = false
	add_child(yarn_view)
	yarn_view.closed.connect(func() -> void: set_process(true))
	calibrate_view = Control.new()   # the swatch calibration wizard
	calibrate_view.set_script(load("res://crochet/crochet_calibrate.gd"))
	calibrate_view.visible = false
	add_child(calibrate_view)
	calibrate_view.records_changed.connect(_reload_records)

func _small(text: String, tip := "") -> Button:
	var b := Button.new()
	b.text = text
	b.tooltip_text = tip
	return b

func _build_top_bar() -> Control:
	var bar := HBoxContainer.new()
	var title := Label.new()
	title.text = "Crochet"
	title.add_theme_font_size_override("font_size", 16)
	bar.add_child(title)
	bar.add_child(_vsep())
	bar.add_child(_label("Scene"))
	scene_opt = OptionButton.new()
	scene_opt.add_item("Hang from hook (peg)")
	scene_opt.set_item_metadata(0, "hang_peg")
	scene_opt.add_item("Hang from hand (pin)")
	scene_opt.set_item_metadata(1, "hang_pin")
	scene_opt.select(1)
	bar.add_child(scene_opt)
	bar.add_child(_label("Load"))
	load_opt = OptionButton.new()
	var loads := [["Empty", "none"], ["MacBook Air 13", "mba13"], ["MacBook Pro 14", "mbp14"], ["MacBook Pro 16", "mbp16"], ["Phone-sized box", "box:70x140x9_mm:0.3"]]
	for i in loads.size():
		load_opt.add_item(loads[i][0])
		load_opt.set_item_metadata(i, loads[i][1])
	bar.add_child(load_opt)
	run_btn = _small("Run", "Relax, hang and (with a load) insert and release, as one flow")
	run_btn.pressed.connect(func() -> void: start_run(String(scene_opt.get_item_metadata(scene_opt.selected)), String(load_opt.get_item_metadata(load_opt.selected))))
	bar.add_child(run_btn)
	stop_btn = _small("Stop")
	stop_btn.disabled = true
	stop_btn.pressed.connect(func() -> void: _stop_requested = true)
	bar.add_child(stop_btn)
	var compare := _small("Compare", "Two patterns or settings side by side, or the five hinge-round variants of the laptop bag")
	compare.pressed.connect(func() -> void: compare_view.visible = true)
	bar.add_child(compare)
	var yarn_mode := _small("Yarn mode", "Crochet a piece one hook action at a time: click a stitch, the hook makes it, see the yarn")
	yarn_mode.pressed.connect(func() -> void: yarn_view.visible = true)
	bar.add_child(yarn_mode)
	var calibrate := _small("Calibrate", "Measure a swatch of this yarn on this hook: the stitch model then uses your numbers instead of the starter values")
	calibrate.pressed.connect(func() -> void: open_calibration())
	bar.add_child(calibrate)
	phase_label = _label("")
	phase_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar.add_child(phase_label)
	for v in [["Front", 0.0, -0.05], ["Side", PI * 0.5, -0.05], ["3/4", 0.5, -0.2], ["Below", 0.4, 0.6]]:
		var b := _small(v[0])
		var yaw: float = v[1]
		var pitch: float = v[2]
		b.pressed.connect(func() -> void: orbit.set_view(yaw, pitch))
		bar.add_child(b)
	var fit := _small("Fit", "Frame the whole piece again. Mouse: left-drag orbits, right-drag or shift+left-drag pans, the wheel zooms towards the pointer")
	fit.pressed.connect(fit_view)
	bar.add_child(fit)
	var back := _small("Back to editor")
	back.pressed.connect(func() -> void: get_tree().change_scene_to_file("res://editor.tscn"))
	bar.add_child(back)
	return bar

func _label(text: String) -> Label:
	var l := Label.new()
	l.text = text
	return l

func _vsep() -> VSeparator:
	return VSeparator.new()

func _build_left() -> Control:
	var box := VBoxContainer.new()
	box.custom_minimum_size = Vector2(420, 0)
	# files
	var files := HBoxContainer.new()
	fixture_opt = OptionButton.new()
	fixture_opt.add_item("Bundled patterns...")
	for n in _list_fixtures():
		fixture_opt.add_item(n)
	fixture_opt.item_selected.connect(func(i: int) -> void:
		if i > 0:
			_load_fixture_named(fixture_opt.get_item_text(i)))
	files.add_child(fixture_opt)
	var open_btn := _small("Open")
	open_btn.pressed.connect(_on_open)
	files.add_child(open_btn)
	var save_btn := _small("Save as")
	save_btn.pressed.connect(_on_save_as)
	files.add_child(save_btn)
	box.add_child(files)
	# editor
	editor = CodeEdit.new()
	editor.size_flags_vertical = Control.SIZE_EXPAND_FILL
	editor.gutters_draw_line_numbers = true
	editor.draw_tabs = true
	editor.syntax_highlighter = _highlighter()
	editor.text_changed.connect(_on_text_changed)
	editor.caret_changed.connect(_on_caret_changed)
	box.add_child(editor)
	status_label = _label("")
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD
	box.add_child(status_label)
	error_label = _label("")
	error_label.autowrap_mode = TextServer.AUTOWRAP_WORD
	error_label.add_theme_color_override("font_color", Color(1, 0.4, 0.4))
	box.add_child(error_label)
	# yarn and hook
	var yh := GridContainer.new()
	yh.columns = 2
	yh.add_child(_label("Yarn"))
	yarn_opt = OptionButton.new()
	for y in _yarns:
		yarn_opt.add_item(String(y.get("name", y.get("id", "?"))))
		yarn_opt.set_item_metadata(yarn_opt.item_count - 1, String(y.get("id", "")))
	for i in yarn_opt.item_count:
		if String(yarn_opt.get_item_metadata(i)) == "acrylic_worsted":
			yarn_opt.select(i)
	yarn_opt.item_selected.connect(func(_i: int) -> void: _settings_changed())
	yh.add_child(yarn_opt)
	yh.add_child(_label("Hook (mm, 0 = the yarn's)"))
	hook_spin = SpinBox.new()
	hook_spin.min_value = 0.0
	hook_spin.max_value = 25.0
	hook_spin.step = 0.25
	hook_spin.value = 0.0
	hook_spin.value_changed.connect(func(_v: float) -> void: _settings_changed())
	yh.add_child(hook_spin)
	yh.add_child(_label("Tension"))
	tension_spin = SpinBox.new()
	tension_spin.min_value = 0.8
	tension_spin.max_value = 1.2
	tension_spin.step = 0.05
	tension_spin.value = 1.0
	tension_spin.value_changed.connect(func(_v: float) -> void: _settings_changed())
	yh.add_child(tension_spin)
	box.add_child(yh)
	return box

func _highlighter() -> CodeHighlighter:
	var h := CodeHighlighter.new()
	h.number_color = Color(0.75, 0.85, 1.0)
	h.symbol_color = Color(0.9, 0.8, 0.5)
	h.function_color = Color(0.9, 0.9, 0.9)
	h.member_variable_color = Color(0.9, 0.9, 0.9)
	for kw in ["sc", "hdc", "dc", "tr", "ch", "ss", "sk", "turn", "ring", "scbl", "scfl", "ssbl", "ssfl", "fpsc", "bpsc"]:
		h.add_keyword_color(kw, Color(0.55, 0.85, 0.6))
	h.add_color_region("#!", "", Color(0.95, 0.65, 0.3), true)  # extension lines
	h.add_color_region("#", "", Color(0.5, 0.55, 0.6), true)    # comments
	return h

func _build_view() -> Control:
	view_container = SubViewportContainer.new()
	view_container.stretch = true
	view_container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	view_container.size_flags_vertical = Control.SIZE_EXPAND_FILL
	view_container.custom_minimum_size = Vector2(300, 300)
	sub_view = SubViewport.new()
	sub_view.own_world_3d = true
	sub_view.handle_input_locally = false
	view_container.add_child(sub_view)
	stage = Node3D.new()
	sub_view.add_child(stage)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	Look.environment(env.environment)
	stage.add_child(env)
	var studio := Studio.build(stage)
	env.environment.sky = studio["sky"]
	for fx in studio["fixtures"]:   # the lamps still light the piece; their stands are not in its way (the editor hides them too)
		(fx as Node3D).visible = false
	orbit.setup(stage, Look)
	cam_pivot = orbit.pivot
	cam = orbit.cam
	cloth_node = MeshInstance3D.new()
	_mat_rs = Cloth.make_material(0.0)
	_mat_ws = Cloth.make_material(1.0)
	stage.add_child(cloth_node)
	load_node = MeshInstance3D.new()
	load_node.visible = false
	stage.add_child(load_node)
	peg_node = MeshInstance3D.new()
	peg_node.visible = false
	stage.add_child(peg_node)
	view_container.gui_input.connect(_on_view_input)
	return view_container

# the yarn's fibre look on both shells
func _apply_yarn_look() -> void:
	Cloth.apply_look([_mat_rs, _mat_ws], pattern.get_yarn_info(pattern.get_yarn()) if pattern != null else {})

func _build_right() -> Control:
	var tabs := TabContainer.new()
	tabs.custom_minimum_size = Vector2(340, 0)
	chart_holder = Control.new()
	chart_holder.set_script(load("res://crochet/crochet_chart.gd"))
	chart_holder.name = "Chart"
	tabs.add_child(chart_holder)
	chart_holder.stitch_selected.connect(_on_chart_selected)
	chart_holder.stitches_selected.connect(_on_chart_band)
	chart_holder.edit_requested.connect(_on_chart_edit_requested)
	edit_menu = PopupMenu.new()
	edit_menu.hide_on_checkable_item_selection = false
	edit_menu.id_pressed.connect(_on_edit_menu)
	add_child(edit_menu)
	var mbox := VBoxContainer.new()
	mbox.name = "Metrics"
	metrics_text = RichTextLabel.new()
	metrics_text.bbcode_enabled = true
	metrics_text.size_flags_vertical = Control.SIZE_EXPAND_FILL
	metrics_text.fit_content = false
	mbox.add_child(metrics_text)
	tabs.add_child(mbox)
	return tabs

# ------------------------------------------------------------------------------------------------ camera

func _on_view_input(event: InputEvent) -> void:
	orbit.handle(event, view_container.size, pick_at)

# ------------------------------------------------------------------------------------------------ pattern text and compile

func load_text(text: String) -> void:
	editor.text = text
	_text_changed = true
	_on_debounce()

func _load_fixture_named(name: String) -> void:
	var f := FileAccess.open(_repo_root() + "/patterns/cp/%s.txt" % name, FileAccess.READ)
	if f == null:
		error_label.text = "cannot read the bundled pattern %s" % name
		return
	load_text(f.get_as_text())

func _on_text_changed() -> void:
	_text_changed = true
	_debounce.start()

func _settings_changed() -> void:
	_text_changed = true
	_debounce.start()

func _on_debounce() -> void:
	_start_compile()

func _make_pattern() -> CrochetPattern:
	var p := CrochetPattern.new()
	if yarn_opt.item_count > 0:
		p.set_yarn(String(yarn_opt.get_item_metadata(yarn_opt.selected)))
	p.set_hook(hook_spin.value)
	p.set_tension(tension_spin.value)
	p.set_calibration(record_for(String(yarn_opt.get_item_metadata(yarn_opt.selected)) if yarn_opt.item_count > 0 else "", hook_spin.value))
	return p

# the hook in mm that a panel value stands for (0 = the yarn's own)
func effective_hook(yarn: String, hook: float) -> float:
	if hook > 0:
		return hook
	for y in _yarns:
		if String(y.get("id", "")) == yarn:
			return float(y.get("hook_mm", 5.0))
	return 5.0

# the calibration record for a yarn on a hook (panel value, 0 = the yarn's), or {} (the starter values)
func record_for(yarn: String, hook: float) -> Dictionary:
	return Records.find(records, yarn, effective_hook(yarn, hook), "sc")

func _reload_records() -> void:
	records = Records.load_all(Records.path())
	_settings_changed()

func _start_compile() -> void:
	if _compile_thread != null and _compile_thread.is_started():
		_compile_pending = true
		return
	_compile_pending = false
	_compile_job = _make_pattern()
	var text := editor.text
	if _use_worker:
		_compile_thread = Thread.new()
		_compile_thread.start(_compile_worker.bind(_compile_job, text))
		set_process(true)
	else:
		_compile_ok = _compile_job.compile(text, YARNS_PATH)
		_finish_compile()

func _compile_worker(p: CrochetPattern, text: String) -> void:
	_compile_ok = p.compile(text, YARNS_PATH)

func _finish_compile() -> void:
	if not _compile_ok:
		pattern = null
		var errs := _compile_job.get_errors()
		var lines: Array[String] = []
		for l in editor.get_line_count():
			editor.set_line_background_color(l, Color(0, 0, 0, 0))
		for e in errs:
			var line := int(e.get("line", 0))
			lines.append("line %d: %s" % [line, String(e.get("msg", ""))])
			if line >= 1 and line <= editor.get_line_count():
				editor.set_line_background_color(line - 1, Color(0.6, 0.1, 0.1, 0.5))
		error_label.text = "\n".join(lines)
		status_label.text = "Does not compile"
		run_btn.disabled = true
		return
	for l in editor.get_line_count():
		editor.set_line_background_color(l, Color(0, 0, 0, 0))
	error_label.text = ""
	pattern = _compile_job
	run_btn.disabled = _running
	var s := pattern.get_summary()
	status_label.text = _summary_line(s)
	chart_holder.set_chart(pattern.get_chart())
	selection = PackedInt32Array()
	_line_stitches.clear()
	for sy in chart_holder.symbols:
		var l := int(sy["line"])
		if not _line_stitches.has(l):
			_line_stitches[l] = PackedInt32Array()
		_line_stitches[l].push_back(int(sy["id"]))
	if not _running:
		_show_metrics_note(s)

func _summary_line(s: Dictionary) -> String:
	if s.has("error"):
		return "%d stitches (model: %s)" % [int(s.get("stitches", 0)), String(s["error"])]
	return "%d stitches, %.0f m of yarn, %.0f g, layout %.1f x %.1f cm%s" % [int(s.get("stitches", 0)), float(s.get("yarn_m", 0.0)), float(s.get("mass_g", 0.0)),
		float(s.get("flat_width_cm", 0.0)), float(s.get("flat_height_cm", 0.0)), " (calibrated)" if bool(s.get("calibrated", false)) else ""]

func compile_now() -> bool:
	_debounce.stop()
	if _compile_thread != null and _compile_thread.is_started():
		_compile_thread.wait_to_finish()
		_finish_compile()
	_compile_job = _make_pattern()
	_compile_ok = _compile_job.compile(editor.text, YARNS_PATH)
	_finish_compile()
	return _compile_ok

func summary_text() -> String:
	return status_label.text

func error_text() -> String:
	return error_label.text

# ------------------------------------------------------------------------------------------------ file dialogs

func _on_open() -> void:
	var dlg := FileDialog.new()
	dlg.access = FileDialog.ACCESS_FILESYSTEM
	dlg.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	dlg.filters = PackedStringArray(["*.txt ; Crochet patterns"])
	dlg.current_dir = _repo_root() + "/patterns/cp"
	dlg.file_selected.connect(func(path: String) -> void:
		var f := FileAccess.open(path, FileAccess.READ)
		if f != null:
			load_text(f.get_as_text())
		dlg.queue_free())
	dlg.canceled.connect(func() -> void: dlg.queue_free())
	add_child(dlg)
	dlg.popup_centered_ratio(0.6)

func _on_save_as() -> void:
	DirAccess.make_dir_recursive_absolute("user://crochet")
	var dlg := FileDialog.new()
	dlg.access = FileDialog.ACCESS_USERDATA
	dlg.file_mode = FileDialog.FILE_MODE_SAVE_FILE
	dlg.filters = PackedStringArray(["*.txt ; Crochet patterns"])
	dlg.current_dir = "user://crochet"
	dlg.file_selected.connect(func(path: String) -> void:
		var f := FileAccess.open(path, FileAccess.WRITE)
		if f != null:
			f.store_string(editor.text)
		dlg.queue_free())
	dlg.canceled.connect(func() -> void: dlg.queue_free())
	add_child(dlg)
	dlg.popup_centered_ratio(0.6)

# ------------------------------------------------------------------------------------------------ selection (plan 9.1)
# One selection, three views of it: the chart rings, a glow on the cloth (vertex colour alpha 0, read by the shader), and the pattern
# line in the editor. It is set by a click in the chart, a click on the cloth (a ray against the triangles), or the caret moving to a line.

func _index_scene() -> void:
	_stitch_of_vertex = sim.get_stitch_of_vertex()
	_indices = sim.get_indices()
	_vertex_of_stitch = PackedInt32Array()
	_vertex_of_stitch.resize(chart_holder.symbol_count())
	_vertex_of_stitch.fill(-1)
	for v in _stitch_of_vertex.size():
		var s := _stitch_of_vertex[v]
		if s >= 0 and s < _vertex_of_stitch.size():
			_vertex_of_stitch[s] = v

func select_stitches(ids: PackedInt32Array, source: String) -> void:
	selection = ids
	chart_holder.set_highlight(ids)
	if ids.size() > 0:
		if source != "chart":
			chart_holder.reveal(ids[0])
		if source != "line":
			_show_line(int(chart_holder.symbols[ids[0]]["line"]))
	if sim != null and not _running:
		_update_mesh()

func _on_chart_selected(id: int) -> void:
	select_stitches(PackedInt32Array([id]) if id >= 0 else PackedInt32Array(), "chart")

func _on_chart_band(ids: PackedInt32Array) -> void:
	select_stitches(ids, "chart")

# ------------------------------------------------------------------------------------------------ lego edits (plan 9.2)
# The edit menu adds `#! override` lines to the end of the pattern text, so the text stays the one source of truth (and Ctrl+Z in the
# editor undoes an edit); the change shows when the pattern recompiles. Only type and insertion change, never the stitch counts.

func _on_chart_edit_requested(at: Vector2) -> void:
	if selection.is_empty():
		return
	_build_edit_menu()
	edit_menu.popup(Rect2i(Vector2i(chart_holder.get_screen_position() + at), Vector2i.ZERO))

func _build_edit_menu() -> void:
	edit_menu.clear()
	var rows := {}
	for id in selection:
		rows[int(chart_holder.symbols[id]["row"])] = true
	edit_menu.add_separator("%d stitches, %s" % [selection.size(), ("row %d" % rows.keys()[0]) if rows.size() == 1 else ("%d rows" % rows.size())])
	for i in OVERRIDE_MODS.size():
		edit_menu.add_item(OVERRIDE_LABELS[i], i)
	edit_menu.add_separator()
	edit_menu.add_check_item("Make the whole row like this", WHOLE_ROW_ID)
	edit_menu.set_item_checked(edit_menu.get_item_index(WHOLE_ROW_ID), _whole_row)

func _on_edit_menu(id: int) -> void:
	if id == WHOLE_ROW_ID:
		_whole_row = not _whole_row
		edit_menu.set_item_checked(edit_menu.get_item_index(WHOLE_ROW_ID), _whole_row)
	elif id >= 0 and id < OVERRIDE_MODS.size():
		apply_override(OVERRIDE_MODS[id], _whole_row)

func _range_line(row: int, a: int, b: int, modifier: String) -> String:
	return "#! override [%d,%d] %s" % [row, a, modifier] if a == b else "#! override [%d,%d-%d] %s" % [row, a, b, modifier]

# the `#! override` lines for the selected stitches: one per row (whole_row) or one per run of neighbouring stitches; chains are left out
func override_lines(modifier: String, whole_row: bool) -> Array[String]:
	var by_row := {}
	for id in selection:
		var sy: Dictionary = chart_holder.symbols[id]
		if String(sy["type"]) == "ch":
			continue
		if not by_row.has(int(sy["row"])):
			by_row[int(sy["row"])] = []
		by_row[int(sy["row"])].append(int(sy["index"]))
	var rows := by_row.keys()
	rows.sort()
	var lines: Array[String] = []
	for r in rows:
		if whole_row:
			lines.append("#! override [%d] %s" % [r, modifier])
			continue
		var idx: Array = by_row[r]
		idx.sort()
		var a: int = idx[0]
		var b: int = idx[0]
		for k in range(1, idx.size()):
			if idx[k] == b + 1:
				b = idx[k]
			else:
				lines.append(_range_line(r, a, b, modifier))
				a = idx[k]
				b = idx[k]
		lines.append(_range_line(r, a, b, modifier))
	return lines

# append the override lines for the selection to the pattern text (one undo step); false when there is nothing to change
func apply_override(modifier: String, whole_row: bool) -> bool:
	var lines := override_lines(modifier, whole_row)
	if lines.is_empty():
		error_label.text = "Select stitches in the chart first (chains cannot be changed)"
		return false
	var last := editor.get_line_count() - 1
	var tail := editor.get_line(last)
	editor.begin_complex_operation()
	editor.insert_text(("\n" if tail != "" else "") + "\n".join(lines) + "\n", last, tail.length())
	editor.end_complex_operation()
	return true

# the pattern line of a stitch: caret there, the whole line selected
func _show_line(line: int) -> void:
	if line < 1 or line > editor.get_line_count():
		return
	_synced_line = line
	editor.set_caret_line(line - 1)
	editor.select(line - 1, 0, line - 1, editor.get_line(line - 1).length())
	editor.center_viewport_to_caret()

func _on_caret_changed() -> void:
	var line := editor.get_caret_line() + 1
	if line == _synced_line:
		return   # our own move, from a stitch that was selected somewhere else
	_synced_line = -1
	var ids: PackedInt32Array = _line_stitches.get(line, PackedInt32Array())
	if ids == selection:
		return
	select_stitches(ids, "line")

func highlight_vertices() -> PackedInt32Array:
	var out := PackedInt32Array()
	for id in selection:
		if id < _vertex_of_stitch.size() and _vertex_of_stitch[id] >= 0:
			out.push_back(_vertex_of_stitch[id])
	return out

# alpha 0 marks the selected vertices (the shader glows there); everything else keeps alpha 1
func highlight_colours(col: PackedColorArray) -> PackedColorArray:
	var out := col.duplicate()
	for v in highlight_vertices():
		if v < out.size():
			var c := out[v]
			c.a = 0.0
			out[v] = c
	return out

# the stitch under a ray (world space): the nearest vertex of the nearest triangle it crosses, -1 when it misses
func pick_ray(origin: Vector3, dir: Vector3) -> int:
	if sim == null or last_vertices.is_empty() or _indices.is_empty():
		return -1
	var best := INF
	var best_v := -1
	for t in range(0, _indices.size(), 3):
		var tri := [_indices[t], _indices[t + 1], _indices[t + 2]]
		var hit = Geometry3D.ray_intersects_triangle(origin, dir, last_vertices[tri[0]], last_vertices[tri[1]], last_vertices[tri[2]])
		if hit == null:
			continue
		var d := origin.distance_to(hit)
		if d >= best:
			continue
		best = d
		var near := 1e9
		for v in tri:
			var dv := last_vertices[v].distance_to(hit)
			if dv < near:
				near = dv
				best_v = v
	return _stitch_of_vertex[best_v] if best_v >= 0 else -1

# a click at a point of the 3D view: select the stitch under it (or clear the selection)
func pick_at(pos: Vector2) -> int:
	var id := pick_ray(cam.project_ray_origin(pos), cam.project_ray_normal(pos))
	select_stitches(PackedInt32Array([id]) if id >= 0 else PackedInt32Array(), "3d")
	return id

# ------------------------------------------------------------------------------------------------ the run

func start_run(scene_id: String, load_id: String) -> bool:
	if pattern == null:
		error_label.text = "Fix the pattern first: it does not compile"
		return false
	if _running:
		return false
	if _run_thread != null and _run_thread.is_started():
		_run_thread.wait_to_finish()
	sim = CrochetScene.new()
	if not sim.build(pattern, scene_id, load_id):
		error_label.text = sim.get_error()
		sim = null
		return false
	error_label.text = ""
	_apply_yarn_look()
	_index_scene()
	_running = true
	_stop_requested = false
	_frames_total = 0
	_have_offset = false
	if not _fixed_chunk:
		_chunk = 2
	run_btn.disabled = true
	stop_btn.disabled = false
	load_node.visible = false
	peg_node.visible = false
	_update_mesh()
	set_process(true)
	if not _use_worker:
		pass  # run_frames() or _process drives it synchronously
	return true

# Synchronous stepping for tests and headless runs: n frames, then the view and phase are refreshed.
func run_frames(n: int) -> void:
	if sim == null:
		return
	var left := n
	while left > 0 and not sim.is_done():
		var k := mini(left, 10)
		sim.step(k)
		_frames_total += k
		left -= k
	_update_mesh()
	_update_labels()
	if sim.is_done() or _stop_requested:
		_finish_run()

func _process(_delta: float) -> void:
	# compile thread
	if _compile_thread != null and _compile_thread.is_started() and not _compile_thread.is_alive():
		_compile_thread.wait_to_finish()
		_finish_compile()
		if _compile_pending:
			_start_compile()
	# run thread
	if _running and sim != null:
		if _run_thread != null and _run_thread.is_started():
			if _run_thread.is_alive():
				return
			_run_thread.wait_to_finish()
			if not _fixed_chunk:  # keep a job near 150 ms: the view refreshes between jobs
				var ms := float(maxi(1, Time.get_ticks_msec() - _started_msec))
				_chunk = clampi(int(round(float(_chunk) * 150.0 / ms)), 1, 40)
			_frames_total += _chunk
			_update_mesh()
			_update_labels()
			if sim.is_done() or _stop_requested:
				_finish_run()
				return
		if not _use_worker:
			sim.step(_chunk)
			_frames_total += _chunk
			_update_mesh()
			_update_labels()
			if sim.is_done() or _stop_requested:
				_finish_run()
			return
		_run_thread = Thread.new()
		_started_msec = Time.get_ticks_msec()
		_run_thread.start(_run_job.bind(_chunk))

var _started_msec := 0

func _run_job(n: int) -> void:
	sim.step(n)

func _finish_run() -> void:
	# the settled piece may be taller than the one that was laid down: frame it again, unless the user has set the camera themselves
	if sim != null and sim.get_positions().size() > 0 and not orbit.user_moved:
		_frame_camera(sim.get_positions())
	_running = false
	run_btn.disabled = pattern == null
	stop_btn.disabled = true
	_update_labels()
	_show_metrics()

func _update_labels() -> void:
	if sim == null:
		return
	phase_label.text = "%s  (%d frames)  %s" % [sim.phase(), sim.get_phase_frames(), ("settled" if sim.is_done() and sim.is_settled() else ("not settled" if sim.is_done() else ""))]

func mesh_vertex_count() -> int:
	return sim.get_positions().size() if sim != null else 0

func _update_mesh() -> void:
	if sim == null:
		return
	var pos := sim.get_positions()
	if pos.size() == 0:
		return
	if not _have_offset:
		var hi := -1e9
		for p in pos:
			hi = maxf(hi, p.y)
		_y_offset = HANG_Y - hi
		_have_offset = true
		_frame_camera(pos)
	var shifted := PackedVector3Array()
	shifted.resize(pos.size())
	for i in pos.size():
		shifted[i] = pos[i] + Vector3(0, _y_offset, 0)
	last_vertices = shifted
	if DisplayServer.get_name() == "headless":
		return  # the dummy renderer has no meshes to fill; tests read last_vertices
	var mesh := Cloth.build_mesh(sim, shifted, highlight_colours(sim.get_vertex_colors()), _mat_rs, _mat_ws)
	cloth_node.mesh = mesh
	_update_props()

func _frame_camera(pos: PackedVector3Array) -> void:
	var lo := Vector3(1e9, 1e9, 1e9)
	var hi := Vector3(-1e9, -1e9, -1e9)
	for p in pos:
		lo = lo.min(p)
		hi = hi.max(p)
	_frame_lo = lo + Vector3(0, _y_offset, 0)
	_frame_hi = hi + Vector3(0, _y_offset, 0)
	fit_view()

# Frames the piece again, whatever the user did with the camera.
func fit_view() -> void:
	if _frame_hi.x < _frame_lo.x:
		return
	orbit.frame(_frame_lo, _frame_hi, float(view_container.size.x) / maxf(1.0, float(view_container.size.y)))

func _update_props() -> void:
	Cloth.place_props(sim, load_node, peg_node, Vector3(0, _y_offset, 0))

func open_calibration() -> void:
	var y := String(yarn_opt.get_item_metadata(yarn_opt.selected)) if yarn_opt.item_count > 0 else "acrylic_worsted"
	calibrate_view.open(y, effective_hook(y, hook_spin.value))
	calibrate_view.visible = true

# "calibrated" with what was measured, or the UNVERIFIED warning (plan section 10)
func calibration_banner(yarn: String, hook: float) -> String:
	var rec := record_for(yarn, hook)
	if rec.is_empty():
		return "[color=#e0a040]UNVERIFIED starter values[/color] (no calibration)"
	var parts: Array[String] = []
	for pair in [["w", "gauge"], ["density", "weight"], ["bendWale", "cantilever"], ["kWale", "stretch"], ["hinge", "hinge"]]:
		if rec.has(pair[0]):
			parts.append(pair[1])
	return "[color=#70c070]Calibrated[/color]: swatch of %s, %s on %.2f mm (%s)" % [String(rec.get("date", "?")), String(rec["yarn"]), float(rec["hook_mm"]), ", ".join(parts)]

# ------------------------------------------------------------------------------------------------ metrics

func _panel_yarn() -> String:
	return String(yarn_opt.get_item_metadata(yarn_opt.selected)) if yarn_opt.item_count > 0 else ""

func _show_metrics_note(s: Dictionary) -> void:
	metrics_text.text = "[i]Run the pattern to see the numbers.[/i]\n\n" + calibration_banner(_panel_yarn(), hook_spin.value) + "\n\n" + \
		"Pattern: %d stitches, %d rows" % [int(s.get("stitches", 0)), int(s.get("rows", 0))]

func _show_metrics() -> void:
	if sim == null:
		return
	var m := sim.get_metrics()
	var t := calibration_banner(_panel_yarn(), hook_spin.value) + "\n\n[table=2]"
	for k in m.keys():
		var v = m[k]
		var vs := "-"
		if v is float:
			vs = String.num(v, 4)
		elif v != null:
			vs = str(v)
		t += "[cell]%s[/cell][cell]%s[/cell]" % [k, vs]
	t += "[/table]"
	metrics_text.text = t
