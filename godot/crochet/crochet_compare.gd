extends Control
# Compare view (docs/CROCHET_PLAN.md 9.3). Two patterns (or the same pattern with other yarn / hook settings), or the five hinge-round
# variants of the laptop bag, are simulated in parallel on worker threads with the same scene and load, drawn side by side 0.5 m apart
# in the studio with a label each, and listed in a metrics table: A, B and the difference for two pieces, one column per piece for
# more. The rows that matter (corner angle, base sag, drop, strap elongation, mass) are in bold when the pieces differ in them.

const Studio = preload("res://studio.gd")
const Look = preload("res://look.gd")
const Cloth = preload("res://crochet/crochet_cloth.gd")
const YARNS_PATH := "res://materials/yarns.json"
const SPACING := 0.5
const HANG_Y := 1.35
const KEY_METRICS := ["corner_angle_deg", "base_sag_mm", "drop_cm", "strap_elongation_pct", "mass_g"]
const VARIANTS := [["plain", "bag_round_plain"], ["BLO round", "bag_round_blo"], ["FLO round", "bag_round_flo"], ["sl st round", "bag_round_slst"], ["surface sl st", "bag_round_surface"]]

signal closed

class Slot:
	var label := ""
	var text := ""
	var yarn := ""            # yarn id
	var hook := 0.0           # mm, 0 = the yarn's
	var tension := 1.0
	var pattern: CrochetPattern
	var sim: CrochetScene
	var state := "queued"     # queued, preparing, running, done, failed
	var error := ""
	var thread: Thread
	var frames := 0
	var chunk := 2
	var started_msec := 0
	var y_offset := 0.0
	var have_offset := false
	var metrics := {}
	var vertices := PackedVector3Array()
	var node: Node3D
	var cloth: MeshInstance3D
	var load_node: MeshInstance3D
	var peg_node: MeshInstance3D
	var tag: Label3D
	var mat_rs: ShaderMaterial
	var mat_ws: ShaderMaterial

var main: Node                 # the crochet scene: its yarn, hook, tension, scene and load menus are the settings of every piece here
var slots: Array = []
var scene_id := "hang_pin"
var load_id := "none"
var max_parallel := 3
var running := false
var _use_worker := true
var _stop := false
var sub_view: SubViewport
var stage: Node3D
var cam_pivot: Node3D
var cam: Camera3D
var table_text: RichTextLabel
var note_label: Label
var status_label: Label
var b_pattern: OptionButton
var b_yarn: OptionButton
var b_hook: SpinBox
var _yaw := 0.25
var _pitch := -0.12
var _dist := 2.2
var _target := Vector3(0, 1.0, 0)
var _dragging := false
var _fixtures: Array = []
var _yarn_ids: Array = []

func _ready() -> void:
	_use_worker = DisplayServer.get_name() != "headless" or OS.has_environment("FL_THREAD")
	max_parallel = clampi(OS.get_processor_count() - 3, 1, 5)
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_build_ui()
	set_process(false)

func _exit_tree() -> void:
	_stop = true
	for s in slots:
		if s.thread != null and s.thread.is_started():
			s.thread.wait_to_finish()

# ------------------------------------------------------------------------------------------------ UI

func _build_ui() -> void:
	var back := ColorRect.new()
	back.color = Color(0.11, 0.12, 0.14)
	back.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(back)
	var root := VBoxContainer.new()
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(root)
	var bar := HBoxContainer.new()
	root.add_child(bar)
	var title := Label.new()
	title.text = "Compare"
	title.add_theme_font_size_override("font_size", 16)
	bar.add_child(title)
	var a_label := Label.new()
	a_label.text = "  A = the pattern in the editor.   B:"
	bar.add_child(a_label)
	b_pattern = OptionButton.new()
	b_pattern.add_item("the same pattern")
	_fixtures = _list_fixtures()
	for f in _fixtures:
		b_pattern.add_item(f)
	bar.add_child(b_pattern)
	var yl := Label.new()
	yl.text = "yarn"
	bar.add_child(yl)
	b_yarn = OptionButton.new()
	b_yarn.add_item("same as A")
	_yarn_ids = []
	if main != null and main.get("yarn_opt") != null:
		for i in main.yarn_opt.item_count:
			_yarn_ids.append(String(main.yarn_opt.get_item_metadata(i)))
			b_yarn.add_item(main.yarn_opt.get_item_text(i))
	bar.add_child(b_yarn)
	var hl := Label.new()
	hl.text = "hook mm"
	bar.add_child(hl)
	b_hook = SpinBox.new()
	b_hook.min_value = 0.0
	b_hook.max_value = 15.0
	b_hook.step = 0.25
	b_hook.tooltip_text = "0 = the same as A"
	bar.add_child(b_hook)
	var run_ab := Button.new()
	run_ab.text = "Run A and B"
	run_ab.pressed.connect(func() -> void: start_ab())
	bar.add_child(run_ab)
	var run_v := Button.new()
	run_v.text = "Hinge-round variants (5)"
	run_v.tooltip_text = "The laptop bag with a plain, BLO, FLO, sl st round or a surface sl st row at the bottom edge, one run each"
	run_v.pressed.connect(func() -> void: start_variants())
	bar.add_child(run_v)
	var stop := Button.new()
	stop.text = "Stop"
	stop.pressed.connect(func() -> void: stop())
	bar.add_child(stop)
	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar.add_child(spacer)
	var close := Button.new()
	close.text = "Back"
	close.pressed.connect(func() -> void:
		visible = false
		closed.emit())
	bar.add_child(close)
	status_label = Label.new()
	status_label.text = "Pick B and run, or run the five hinge-round variants."
	root.add_child(status_label)
	var split := HSplitContainer.new()
	split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root.add_child(split)
	var vc := SubViewportContainer.new()
	vc.stretch = true
	vc.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vc.custom_minimum_size = Vector2(400, 300)
	sub_view = SubViewport.new()
	sub_view.own_world_3d = true
	vc.add_child(sub_view)
	vc.gui_input.connect(_on_view_input)
	split.add_child(vc)
	stage = Node3D.new()
	sub_view.add_child(stage)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	Look.environment(env.environment)
	stage.add_child(env)
	var studio := Studio.build(stage)
	env.environment.sky = studio["sky"]
	cam_pivot = Node3D.new()
	stage.add_child(cam_pivot)
	cam = Camera3D.new()
	Look.camera(cam)
	cam_pivot.add_child(cam)
	_apply_camera()
	var right := VBoxContainer.new()
	right.custom_minimum_size = Vector2(660, 0)
	split.add_child(right)
	table_text = RichTextLabel.new()
	table_text.bbcode_enabled = true
	table_text.add_theme_font_size_override("normal_font_size", 13)
	table_text.add_theme_font_size_override("bold_font_size", 13)
	table_text.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_child(table_text)
	note_label = Label.new()
	note_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	right.add_child(note_label)

func _list_fixtures() -> Array:
	var out: Array = []
	var dir := DirAccess.open(_repo_root() + "/patterns/cp")
	if dir != null:
		for f in dir.get_files():
			if f.ends_with(".txt"):
				out.append(f.get_basename())
	out.sort()
	return out

func _repo_root() -> String:
	return ProjectSettings.globalize_path("res://").path_join("..").simplify_path()

func _apply_camera() -> void:
	cam_pivot.position = _target
	cam_pivot.rotation = Vector3(_pitch, _yaw, 0)
	cam.position = Vector3(0, 0, _dist)

func _on_view_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_LEFT or mb.button_index == MOUSE_BUTTON_RIGHT:
			_dragging = mb.pressed
		elif mb.button_index == MOUSE_BUTTON_WHEEL_UP and mb.pressed:
			_dist = maxf(0.4, _dist * 0.9)
			_apply_camera()
		elif mb.button_index == MOUSE_BUTTON_WHEEL_DOWN and mb.pressed:
			_dist = minf(12.0, _dist / 0.9)
			_apply_camera()
	elif event is InputEventMouseMotion and _dragging:
		var mm := event as InputEventMouseMotion
		_yaw -= mm.relative.x * 0.006
		_pitch = clampf(_pitch - mm.relative.y * 0.006, -1.4, 1.4)
		_apply_camera()

# ------------------------------------------------------------------------------------------------ slots

func clear_slots() -> void:
	stop()
	for s in slots:
		if s.thread != null and s.thread.is_started():
			s.thread.wait_to_finish()
		if s.node != null:
			s.node.queue_free()
	slots.clear()
	_refresh_table()

func add_slot(label: String, text: String, yarn: String, hook: float, tension: float) -> Slot:
	var s := Slot.new()
	s.label = label
	s.text = text
	s.yarn = yarn
	s.hook = hook
	s.tension = tension
	s.mat_rs = Cloth.make_material(0.0)
	s.mat_ws = Cloth.make_material(1.0)
	s.node = Node3D.new()
	s.cloth = MeshInstance3D.new()
	s.load_node = MeshInstance3D.new()
	s.load_node.visible = false
	s.peg_node = MeshInstance3D.new()
	s.peg_node.visible = false
	s.tag = Label3D.new()
	s.tag.text = label
	s.tag.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	s.tag.pixel_size = 0.001
	s.tag.font_size = 48
	s.tag.outline_size = 10
	s.node.add_child(s.cloth)
	s.node.add_child(s.load_node)
	s.node.add_child(s.peg_node)
	s.node.add_child(s.tag)
	stage.add_child(s.node)
	slots.append(s)
	_place_slots()
	return s

# the pieces stand in a row, 0.5 m between neighbours, centred on the middle of the stage
func _place_slots() -> void:
	var n := slots.size()
	for i in n:
		slots[i].node.position = Vector3((i - 0.5 * (n - 1)) * SPACING, 0, 0)
		slots[i].tag.position = Vector3(0, HANG_Y + 0.12, 0)
	_dist = maxf(2.4, 1.2 + n * 0.75)
	_target = Vector3(0, 1.0, 0)
	_apply_camera()

func slot_offset(i: int) -> Vector3:
	return slots[i].node.position

# the settings that every piece shares (scene, load) and the defaults of A (yarn, hook, tension) from the editor's panel
func _settings() -> Dictionary:
	var d := {"scene": "hang_pin", "load": "none", "yarn": "acrylic_worsted", "hook": 0.0, "tension": 1.0, "text": ""}
	if main != null:
		d["scene"] = String(main.scene_opt.get_item_metadata(main.scene_opt.selected))
		d["load"] = String(main.load_opt.get_item_metadata(main.load_opt.selected))
		if main.yarn_opt.item_count > 0:
			d["yarn"] = String(main.yarn_opt.get_item_metadata(main.yarn_opt.selected))
		d["hook"] = main.hook_spin.value
		d["tension"] = main.tension_spin.value
		d["text"] = main.editor.text
	return d

func start_ab() -> void:
	setup_ab()
	start()

# A is the editor's pattern with the panel's settings; B the chosen pattern with the chosen yarn and hook (the rest as A)
func setup_ab() -> void:
	var d := _settings()
	var b_text: String = d["text"]
	var b_name := "B"
	if b_pattern.selected > 0:
		b_name = String(_fixtures[b_pattern.selected - 1])
		b_text = FileAccess.get_file_as_string(_repo_root() + "/patterns/cp/%s.txt" % b_name)
	var b_y: String = d["yarn"]
	if b_yarn.selected > 0:
		b_y = String(_yarn_ids[b_yarn.selected - 1])
	var b_h: float = d["hook"] if b_hook.value == 0.0 else b_hook.value
	clear_slots()
	scene_id = d["scene"]
	load_id = d["load"]
	add_slot("A", d["text"], d["yarn"], d["hook"], d["tension"])
	add_slot("B: %s" % b_name if b_pattern.selected > 0 else "B", b_text, b_y, b_h, d["tension"])

# the laptop bag with each hinge round, with the yarn and hook of the panel (the plan's report used cotton worsted on 4.5 mm)
func start_variants() -> void:
	setup_variants()
	start()

func setup_variants() -> void:
	var d := _settings()
	clear_slots()
	scene_id = d["scene"]
	load_id = d["load"]
	for v in VARIANTS:
		add_slot(String(v[0]), FileAccess.get_file_as_string(_repo_root() + "/patterns/cp/%s.txt" % v[1]), d["yarn"], d["hook"], d["tension"])

func start() -> void:
	if slots.is_empty():
		return
	_stop = false
	running = true
	for s in slots:
		s.state = "queued"
		s.error = ""
		s.frames = 0
	if scene_id == "hang_peg":
		_note("hang_peg needs the hook scene: every pattern here needs a strap for hang_pin and a hook for hang_peg.")
	set_process(true)
	_refresh_table()

func stop() -> void:
	_stop = true

# ------------------------------------------------------------------------------------------------ running

# the worker's first job: compile the text and build the scene (nothing here touches the scene tree)
func _prepare(s: Slot) -> void:
	var p := CrochetPattern.new()
	p.set_yarn(s.yarn)
	p.set_hook(s.hook)
	p.set_tension(s.tension)
	if not p.compile(s.text, YARNS_PATH):
		var msgs: Array[String] = []
		for e in p.get_errors():
			msgs.append("line %d: %s" % [int(e.get("line", 0)), String(e.get("msg", ""))])
		s.error = "\n".join(msgs) if not msgs.is_empty() else "does not compile"
		return
	s.pattern = p
	var sc := CrochetScene.new()
	if not sc.build(p, scene_id, load_id):
		s.error = sc.get_error()
		return
	s.sim = sc

func _step_job(s: Slot, n: int) -> void:
	s.sim.step(n)

func _count(state: String) -> int:
	var n := 0
	for s in slots:
		n += 1 if s.state == state else 0
	return n

func _process(_delta: float) -> void:
	if not running:
		return
	var busy := _count("preparing") + _count("running")
	for s in slots:
		if s.state == "queued" and busy < max_parallel and not _stop:
			s.state = "preparing"
			busy += 1
			if _use_worker:
				s.thread = Thread.new()
				s.thread.start(_prepare.bind(s))
			else:
				_prepare(s)
				_after_prepare(s)
		elif s.state == "preparing" and _use_worker and s.thread != null and not s.thread.is_alive():
			s.thread.wait_to_finish()
			_after_prepare(s)
		elif s.state == "running":
			_drive(s)
	if _stop:
		for s in slots:
			if s.state == "queued":
				s.state = "failed"
				s.error = "stopped"
	if _count("queued") + _count("preparing") + _count("running") == 0:
		running = false
		set_process(false)
		_refresh_table()
	_status()

func _after_prepare(s: Slot) -> void:
	if s.sim == null:
		s.state = "failed"
		_refresh_table()
		return
	s.state = "running"
	_apply_look(s)
	_show(s)

func _drive(s: Slot) -> void:
	if s.thread != null and s.thread.is_started():
		if s.thread.is_alive():
			return
		s.thread.wait_to_finish()
		var ms := float(maxi(1, Time.get_ticks_msec() - s.started_msec))
		s.chunk = clampi(int(round(float(s.chunk) * 200.0 / ms)), 1, 40)
		s.frames += s.chunk
		_show(s)
	if s.sim.is_done() or _stop:
		_finish(s)
		return
	if not _use_worker:
		s.sim.step(s.chunk)
		s.frames += s.chunk
		_show(s)
		if s.sim.is_done():
			_finish(s)
		return
	s.thread = Thread.new()
	s.started_msec = Time.get_ticks_msec()
	s.thread.start(_step_job.bind(s, s.chunk))

func _finish(s: Slot) -> void:
	s.metrics = s.sim.get_metrics()
	s.state = "done"
	_show(s)
	_refresh_table()

func _apply_look(s: Slot) -> void:
	Cloth.apply_look([s.mat_rs, s.mat_ws], s.pattern.get_yarn_info(s.pattern.get_yarn()))

# the piece as it is now, hung at the studio's hang height
func _show(s: Slot) -> void:
	var pos := s.sim.get_positions()
	if pos.size() == 0:
		return
	if not s.have_offset:
		var hi := -1e9
		for p in pos:
			hi = maxf(hi, p.y)
		s.y_offset = HANG_Y - hi
		s.have_offset = true
	var shifted := PackedVector3Array()
	shifted.resize(pos.size())
	for i in pos.size():
		shifted[i] = pos[i] + Vector3(0, s.y_offset, 0)
	s.vertices = shifted
	if DisplayServer.get_name() == "headless":
		return
	s.cloth.mesh = Cloth.build_mesh(s.sim, shifted, s.sim.get_vertex_colors(), s.mat_rs, s.mat_ws)
	Cloth.place_props(s.sim, s.load_node, s.peg_node, Vector3(0, s.y_offset, 0))

# Synchronous run for tests and headless: every piece is compiled and built, then stepped `frames` frames (or to the end when 0).
func run_sync(frames: int) -> void:
	_use_worker = false
	_stop = false
	for s in slots:
		s.state = "preparing"
		_prepare(s)
		_after_prepare(s)
		if s.state != "running":
			continue
		var left := frames
		while not s.sim.is_done() and (frames == 0 or left > 0):
			var k := 10 if frames == 0 else mini(left, 10)
			s.sim.step(k)
			s.frames += k
			left -= k
		_finish(s)
	running = false
	_refresh_table()

func _status() -> void:
	var parts: Array[String] = []
	for s in slots:
		var st := String(s.state)
		if s.state == "running":
			st = "%s %d frames" % [s.sim.phase(), s.frames]
		parts.append("%s: %s" % [s.label, st])
	status_label.text = "   ".join(parts)

func _note(t: String) -> void:
	note_label.text = t

# ------------------------------------------------------------------------------------------------ the table

# One entry per metric: {key, values (one per slot, null where the piece has no such number), bold}. Bold = a key metric in which
# the pieces differ (more than 2 % of the larger value, or 0.05 for tiny numbers).
func table_rows() -> Array:
	var keys: Array = []
	for s in slots:
		for k in s.metrics.keys():
			if not keys.has(k):
				keys.append(k)
	var rows: Array = []
	for k in keys:
		var vals: Array = []
		var nums: Array = []
		for s in slots:
			var v = s.metrics.get(k, null)
			vals.append(v)
			if v is float or v is int:
				nums.append(float(v))
		var bold := false
		if KEY_METRICS.has(k) and nums.size() >= 2:
			var lo: float = nums.min()
			var hi: float = nums.max()
			bold = hi - lo > maxf(0.02 * maxf(absf(hi), absf(lo)), 0.05)
		rows.append({"key": k, "values": vals, "bold": bold})
	return rows

# the numbers of one metric for every piece, in slot order (null = not applicable)
func values_of(key: String) -> Array:
	var out: Array = []
	for s in slots:
		out.append(s.metrics.get(key, null))
	return out

func _fmt(v) -> String:
	if v == null:
		return "-"
	if v is float:
		return String.num(v, 3)
	if v is Dictionary:   # a worst-strain position
		return "r%d s%d" % [int(v.get("row", 0)), int(v.get("stitch", 0))]
	return str(v)

# a table with the metric names in a wide first column; for two pieces a third column with B - A; the rows in which the pieces differ in a
# key number are bold
func table_bbcode() -> String:
	if slots.is_empty():
		return "[i]Nothing to compare yet.[/i]"
	var two := slots.size() == 2
	var cols := slots.size() + 1 + (1 if two else 0)
	var t := "[color=#e0a040]UNVERIFIED starter values[/color] (no calibration); bold = the pieces differ in a key number\n\n[font_size=13][table=%d]" % cols
	t += "[cell expand=34]metric[/cell]"
	for s in slots:
		t += "[cell expand=12][b]%s[/b][/cell]" % s.label
	if two:
		t += "[cell expand=12][b]B - A[/b][/cell]"
	for row in table_rows():
		var key := String(row["key"])
		var vals: Array = row["values"]
		var b := bool(row["bold"])
		t += "[cell expand=34]%s[/cell]" % (("[b]%s[/b]" % key) if b else key)
		for v in vals:
			t += "[cell expand=12]%s[/cell]" % (("[b]%s[/b]" % _fmt(v)) if b else _fmt(v))
		if two:
			var d := "-"
			if (vals[0] is float or vals[0] is int) and (vals[1] is float or vals[1] is int):
				d = String.num(float(vals[1]) - float(vals[0]), 3)
			t += "[cell expand=12]%s[/cell]" % (("[b]%s[/b]" % d) if b else d)
	t += "[/table][/font_size]"
	return t

func _refresh_table() -> void:
	if table_text != null:
		table_text.text = table_bbcode()
	var errs: Array[String] = []
	for s in slots:
		if s.state == "failed" and s.error != "":
			errs.append("%s: %s" % [s.label, s.error])
	if not errs.is_empty():
		_note("\n".join(errs))
