extends Control
# Yarn mode (docs/YARN_MODE_PLAN.md, section 11): crochet a piece one hook action at a time and watch the yarn. It should feel like a lego
# simulator: click a stitch, the hook performs it, the stitch appears, go again. A stitch button asks nothing: it works into the next stitch of
# the row below, both loops. Clicking a loop in the view picks it as the opening for the next stitch instead. "Turn" turns the work over: the
# camera swings round it, because the worker sees the other side. The pattern so far is a semi-transparent overlay.
#
# The yarn is YarnSession's (crochet_core YarnWork): one continuous yarn, its loops placed on a lattice, soft under a shape memory spring.
# The hook is cosmetic: it moves to the opening, the yarn grows, it moves to the new loop on the hook.

const Studio = preload("res://studio.gd")
const Look = preload("res://look.gd")
const Orbit = preload("res://crochet/crochet_orbit.gd")

signal closed

const HOOK_LENGTH := 0.07
const HOOK_RADIUS := 0.0022
const PICK_RADIUS_PX := 28.0
const YARN_COLOUR := Color(0.80, 0.30, 0.22)
const SWATCHES := [Color(0.80, 0.30, 0.22), Color(0.95, 0.58, 0.18), Color(0.93, 0.88, 0.76), Color(0.25, 0.45, 0.78), Color(0.30, 0.62, 0.38), Color(0.14, 0.14, 0.16)]
const TURNING_CHAINS := {"sc": 1, "hdc": 2, "dc": 3, "slst": 1}
const WORK_HEIGHT := 0.95          # the work is held at about the height of a table in the studio
const WORK_TILT := 1.0             # rad: the work leans back like a piece held in the left hand, its face toward the worker above the table

var session: YarnSession
var animate := true               # tests switch the animations off
var busy := false
var history: Array = []           # [kind, opening] of every action, for undo (replayed)
var picked := -1                  # a loop the user clicked: the opening of the next stitch (-1: the next in order)
var flip := 0.0                   # 0: the right side of the work faces the worker; PI after a turn
var soft_steps := 0               # rod steps still to run so that new yarn settles

var sub_view: SubViewport
var stage: Node3D
var orbit := Orbit.new()
var view_container: SubViewportContainer
var work_root: Node3D             # the piece, its hook and its markers, at table height
var yarn_node: MeshInstance3D
var yarn_mat: StandardMaterial3D
var hook_node: Node3D
var marker: MeshInstance3D
var status_label: Label
var target_label: Label
var overlay_panel: PanelContainer
var overlay_text: RichTextLabel
var follow_check: CheckBox
var expand_slider: HSlider
var thick_slider: HSlider
var view_label: Label
var overlay_check: CheckBox
var b_chain: Button
var b_sc: Button
var b_hdc: Button
var b_dc: Button
var b_slst: Button
var colour_picker: ColorPickerButton
var colour_when: OptionButton
var colour_label: Label
var last_kind := "sc"            # the stitch last made: the turning chain after a row follows it (sc 1, hdc 2, dc 3)
var loop_front: CheckBox
var loop_back: CheckBox
var b_turn: Button
var b_undo: Button
var _shown_particles := 0         # how much of the yarn is drawn (it grows as it is made)
var _marker_t := 0.0
var _grab_normal := Vector3.ZERO     # the plane (in the piece's coordinates) a held strand is dragged in
var _grab_point := Vector3.ZERO

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	session = YarnSession.new()
	_build_ui()
	_rebuild(-1)
	_refresh()
	_frame_work(true)

# ------------------------------------------------------------------------------------------------ UI

func _build_ui() -> void:
	var back := ColorRect.new()
	back.color = Color(0.11, 0.12, 0.14)
	back.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(back)
	var root := VBoxContainer.new()
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(root)
	var bar := HFlowContainer.new()   # wraps when the window is narrow
	root.add_child(bar)
	var title := Label.new()
	title.text = "Yarn mode  "
	title.add_theme_font_size_override("font_size", 16)
	bar.add_child(title)
	b_chain = _button("Chain", "yarn over, pull through the loop on the hook", func() -> void: do_action("chain"))
	bar.add_child(b_chain)
	b_sc = _button("Single crochet", "into the next stitch of the row below, under both loops; click a loop first to work into that one instead",
			func() -> void: do_action(_stitch_kind("sc")))
	bar.add_child(b_sc)
	b_hdc = _button("Half double", "yarn over first, then as a single crochet, and pull through all three loops",
			func() -> void: do_action(_stitch_kind("hdc")))
	bar.add_child(b_hdc)
	b_dc = _button("Double", "yarn over, pull up a loop, then pull through two loops twice",
			func() -> void: do_action(_stitch_kind("dc")))
	bar.add_child(b_dc)
	b_slst = _button("Slip stitch", "through the stitch below and the loop on the hook in one go: a flat stitch",
			func() -> void: do_action(_stitch_kind("slst")))
	bar.add_child(b_slst)
	loop_front = CheckBox.new()
	loop_front.text = "front loop"
	loop_front.button_pressed = true
	loop_front.tooltip_text = "Which loops of the stitch below the hook goes under. Both (the usual) is already ticked. Untick one to work into the other only; untick both to skip the stitch."
	loop_front.toggled.connect(func(_on: bool) -> void: _refresh())
	bar.add_child(loop_front)
	loop_back = CheckBox.new()
	loop_back.text = "back loop"
	loop_back.button_pressed = true
	loop_back.tooltip_text = loop_front.tooltip_text
	loop_back.toggled.connect(func(_on: bool) -> void: _refresh())
	bar.add_child(loop_back)
	b_turn = _button("Turn", "turn the work over at the end of a row", func() -> void: do_action("turn"))
	bar.add_child(b_turn)
	b_undo = _button("Undo", "take the last stitch back", func() -> void: undo())
	bar.add_child(b_undo)
	bar.add_child(_button("New piece", "start again from a slip knot", func() -> void: reset_piece()))
	bar.add_child(VSeparator.new())
	var cl := Label.new()
	cl.text = "Colour"
	cl.tooltip_text = "Pick a colour: the next stitch changes to it. A colour used before is picked up where it was dropped."
	bar.add_child(cl)
	for c in SWATCHES:
		var sw := Button.new()
		sw.custom_minimum_size = Vector2(22, 22)
		var st := StyleBoxFlat.new()
		st.bg_color = c
		st.set_corner_radius_all(4)
		sw.add_theme_stylebox_override("normal", st)
		sw.add_theme_stylebox_override("hover", st)
		sw.add_theme_stylebox_override("pressed", st)
		sw.tooltip_text = cl.tooltip_text
		sw.pressed.connect(func() -> void: choose_colour(c))
		bar.add_child(sw)
	colour_picker = ColorPickerButton.new()
	colour_picker.custom_minimum_size = Vector2(34, 22)
	colour_picker.color = SWATCHES[1]
	colour_picker.edit_alpha = false
	colour_picker.tooltip_text = "Any other colour"
	colour_picker.color_changed.connect(func(c: Color) -> void: choose_colour(c))
	bar.add_child(colour_picker)
	colour_when = OptionButton.new()
	colour_when.add_item("at the last pull-through")
	colour_when.add_item("from the start of the stitch")
	colour_when.tooltip_text = "How the stitch that changes colour is made: finished in the new colour (the usual way: its top is the new colour), or all of it in the new colour"
	bar.add_child(colour_when)
	colour_label = Label.new()
	bar.add_child(colour_label)
	follow_check = CheckBox.new()
	follow_check.text = "Follow the hook"
	follow_check.button_pressed = true
	bar.add_child(follow_check)
	overlay_check = CheckBox.new()
	overlay_check.text = "Pattern overlay"
	overlay_check.button_pressed = true
	overlay_check.toggled.connect(func(on: bool) -> void: overlay_panel.visible = on)
	bar.add_child(overlay_check)
	bar.add_child(VSeparator.new())
	var el := Label.new()
	el.text = "Expand"
	el.tooltip_text = "Loosen the work: the loops open up and the yarn keeps its thickness, so you can see how they hook into each other"
	bar.add_child(el)
	expand_slider = HSlider.new()
	expand_slider.min_value = 1.0
	expand_slider.max_value = 3.0
	expand_slider.step = 0.05
	expand_slider.value = 1.0
	expand_slider.custom_minimum_size = Vector2(110, 0)
	expand_slider.tooltip_text = el.tooltip_text
	expand_slider.value_changed.connect(func(_v: float) -> void: _apply_view())
	bar.add_child(expand_slider)
	var tl := Label.new()
	tl.text = "Yarn"
	tl.tooltip_text = "How thick the yarn is drawn (thin: see through the interlocking)"
	bar.add_child(tl)
	thick_slider = HSlider.new()
	thick_slider.min_value = 0.3
	thick_slider.max_value = 1.0
	thick_slider.step = 0.05
	thick_slider.value = 1.0
	thick_slider.custom_minimum_size = Vector2(90, 0)
	thick_slider.tooltip_text = tl.tooltip_text
	thick_slider.value_changed.connect(func(_v: float) -> void: _apply_view())
	bar.add_child(thick_slider)
	var close := _button("Back", "", func() -> void:
		visible = false
		closed.emit())
	bar.add_child(close)
	status_label = Label.new()
	status_label.text = ""
	root.add_child(status_label)
	var holder := Control.new()   # the 3D view with the pattern overlay and the target note on top of it
	holder.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	holder.size_flags_vertical = Control.SIZE_EXPAND_FILL
	holder.custom_minimum_size = Vector2(500, 360)
	root.add_child(holder)
	var vc := SubViewportContainer.new()
	vc.stretch = true
	vc.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	sub_view = SubViewport.new()
	sub_view.own_world_3d = true
	vc.add_child(sub_view)
	vc.gui_input.connect(_on_view_input)
	view_container = vc
	holder.add_child(vc)
	stage = Node3D.new()
	sub_view.add_child(stage)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	Look.environment(env.environment)
	stage.add_child(env)
	var studio := Studio.build(stage)
	env.environment.sky = studio["sky"]
	for fx in studio["fixtures"]:
		(fx as Node3D).visible = false
	orbit.setup(stage, Look)
	orbit.yaw = 0.0
	orbit.pitch = -0.85
	work_root = Node3D.new()
	work_root.position = Vector3(0, WORK_HEIGHT, 0)
	work_root.rotation = Vector3(-WORK_TILT, 0, 0)
	stage.add_child(work_root)
	_build_mat()
	yarn_node = MeshInstance3D.new()
	yarn_mat = StandardMaterial3D.new()
	yarn_mat.vertex_color_use_as_albedo = true
	yarn_mat.roughness = 0.9
	yarn_mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	yarn_node.material_override = yarn_mat
	work_root.add_child(yarn_node)
	_build_hook()
	_build_marker()
	# the pattern as a semi-transparent overlay on the view
	overlay_panel = PanelContainer.new()
	overlay_panel.modulate = Color(1, 1, 1, 0.62)
	overlay_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	overlay_panel.position = Vector2(12, 12)
	overlay_text = RichTextLabel.new()
	overlay_text.bbcode_enabled = true
	overlay_text.fit_content = true
	overlay_text.scroll_active = false
	overlay_text.custom_minimum_size = Vector2(300, 0)
	overlay_text.mouse_filter = Control.MOUSE_FILTER_IGNORE
	overlay_panel.add_child(overlay_text)
	holder.add_child(overlay_panel)
	target_label = Label.new()
	target_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	target_label.add_theme_color_override("font_color", Color(1, 1, 1, 0.8))
	holder.add_child(target_label)
	target_label.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_LEFT)
	target_label.offset_left = 12
	target_label.offset_top = -34
	target_label.offset_bottom = -8

# What a stitch button does, from the loop ticks: both -> under both loops, one -> that loop only, none -> skip the stitch.
func _stitch_kind(base: String) -> String:
	if loop_front.button_pressed and loop_back.button_pressed:
		return base
	if loop_front.button_pressed:
		return base + "_front"
	if loop_back.button_pressed:
		return base + "_back"
	return "skip"

func _sc_kind() -> String:
	return _stitch_kind("sc")

func _is_stitch(kind: String) -> bool:
	for b in ["sc", "hdc", "dc", "slst"]:
		if kind == b or kind.begins_with(b + "_"):
			return true
	return false

func _base_kind(kind: String) -> String:
	for b in ["sc", "hdc", "dc", "slst"]:
		if kind == b or kind.begins_with(b + "_"):
			return b
	return kind

# The next stitch changes to this colour (history keeps it so that undo replays it).
func choose_colour(c: Color) -> void:
	if busy:
		return
	var when := "stitch" if colour_when.selected == 1 else "pull"
	session.set_colour(c, when)
	history.append(["colour", c, when])
	_refresh()

func _button(text: String, tip: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.tooltip_text = tip
	b.pressed.connect(cb)
	return b

# a small cloth mat the work lies on
func _build_mat() -> void:
	var m := MeshInstance3D.new()
	var d := CylinderMesh.new()
	d.top_radius = 0.30
	d.bottom_radius = 0.30
	d.height = 0.006
	d.radial_segments = 48
	m.mesh = d
	var mt := StandardMaterial3D.new()
	mt.albedo_color = Color(0.80, 0.78, 0.72)
	mt.roughness = 0.95
	m.material_override = mt
	m.position = Vector3(0, WORK_HEIGHT - 0.016, 0)
	stage.add_child(m)

func _build_hook() -> void:
	hook_node = Node3D.new()
	var steel := StandardMaterial3D.new()
	steel.albedo_color = Color(0.78, 0.80, 0.84)
	steel.metallic = 0.85
	steel.roughness = 0.28
	var shaft := MeshInstance3D.new()
	var cyl := CylinderMesh.new()
	cyl.top_radius = HOOK_RADIUS
	cyl.bottom_radius = HOOK_RADIUS
	cyl.height = HOOK_LENGTH
	cyl.radial_segments = 14
	shaft.mesh = cyl
	shaft.material_override = steel
	# the node's -z axis points from the handle to the tip: the cylinder lies along z with its tip at the origin
	shaft.rotation = Vector3(PI * 0.5, 0, 0)
	shaft.position = Vector3(0, 0, HOOK_LENGTH * 0.5)
	hook_node.add_child(shaft)
	var head := MeshInstance3D.new()
	var sph := SphereMesh.new()
	sph.radius = HOOK_RADIUS * 1.15
	sph.height = HOOK_RADIUS * 2.3
	head.mesh = sph
	head.material_override = steel
	hook_node.add_child(head)
	var handle := MeshInstance3D.new()
	var hc := CylinderMesh.new()
	hc.top_radius = HOOK_RADIUS * 2.0
	hc.bottom_radius = HOOK_RADIUS * 1.7
	hc.height = HOOK_LENGTH * 0.55
	handle.mesh = hc
	var wood := StandardMaterial3D.new()
	wood.albedo_color = Color(0.20, 0.45, 0.62)
	wood.roughness = 0.5
	handle.material_override = wood
	handle.rotation = Vector3(PI * 0.5, 0, 0)
	handle.position = Vector3(0, 0, HOOK_LENGTH * 0.78)
	hook_node.add_child(handle)
	stage.add_child(hook_node)

func _build_marker() -> void:
	marker = MeshInstance3D.new()
	var t := TorusMesh.new()
	t.inner_radius = 0.0046
	t.outer_radius = 0.0060
	t.rings = 24
	t.ring_segments = 8
	marker.mesh = t
	var m := StandardMaterial3D.new()
	m.albedo_color = Color(0.2, 0.9, 0.5, 0.75)
	m.emission_enabled = true
	m.emission = Color(0.1, 0.8, 0.4)
	m.emission_energy_multiplier = 1.2
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	marker.material_override = m
	marker.visible = false
	work_root.add_child(marker)

# ------------------------------------------------------------------------------------------------ actions

# One hook action. Everything the stitch needs is chosen: it goes into the next stitch of the row below (both loops), or into the loop the
# user clicked. Returns false when it could not be made (the status line says why).
func do_action(kind: String, opening: int = -2) -> bool:
	if busy:
		return false
	if opening == -2:
		opening = picked
	busy = true
	var st: Dictionary = session.get_state()
	if (_is_stitch(kind) or kind == "skip") and opening < 0 and int(st.get("next", -1)) < 0:
		status_label.text = "Nothing to work into: turn the work over first."
		busy = false
		return false
	var open_id := opening if opening >= 0 else int(st.get("next", -1))
	if animate and kind != "turn" and kind != "skip":
		var at := _opening_centre(open_id) if ((_is_stitch(kind) or kind == "skip") and open_id >= 0) else _live_centre()
		await _move_hook(at, 0.30)
	var before := session.get_particle_count()
	var arg := opening
	if kind == "turn":
		arg = -1 if int(st["row"]) == 0 else int(TURNING_CHAINS.get(last_kind, 1))
	var r: Dictionary = session.action(kind, arg)
	if not bool(r.get("ok", false)):
		status_label.text = String(r.get("msg", "that did not work"))
		busy = false
		return false
	history.append([kind, arg])
	if _is_stitch(kind):
		last_kind = _base_kind(kind)
	picked = -1
	if kind == "turn":
		_rebuild(-1)
		await _turn_animation()
	elif kind != "skip":
		await _grow(before, int(r["particles"]))
	soft_steps = 16 if animate else 0
	_refresh()
	if animate:
		await _move_hook(_live_centre(), 0.25)
		_frame_work(false)
	else:
		_place_hook(_live_centre())
		_frame_work(true)
	busy = false
	return true

func undo() -> void:
	if busy or history.size() == 0:
		return
	var keep: Array = history.duplicate()
	keep.pop_back()
	_replay(keep)

func reset_piece() -> void:
	if busy:
		return
	_replay([])

# Rebuilds the piece from a list of [kind, opening] without the animations.
func _replay(actions: Array) -> void:
	session.reset()
	history.clear()
	picked = -1
	flip = 0.0
	last_kind = "sc"
	for a in actions:
		if String(a[0]) == "colour":
			session.set_colour(a[1], String(a[2]))
			history.append(a)
			continue
		var r: Dictionary = session.action(String(a[0]), int(a[1]))
		if bool(r.get("ok", false)):
			history.append(a)
			if _is_stitch(String(a[0])):
				last_kind = _base_kind(String(a[0]))
			if String(a[0]) == "turn":
				flip += PI
	flip = fmod(flip, TAU)
	_set_flip(flip, _live_centre(), work_root.to_global(_live_centre()))
	_rebuild(-1)
	_refresh()
	_frame_work(true)
	_place_hook(_live_centre())

func _grow(from_particles: int, to_particles: int) -> void:
	if not animate or to_particles - from_particles < 4:
		_rebuild(-1)
		return
	var tw := create_tween()
	tw.tween_method(func(n: float) -> void: _rebuild(int(n)), float(from_particles), float(to_particles), 0.45).set_trans(Tween.TRANS_SINE)
	await tw.finished
	_rebuild(-1)

# Turning the work over: the piece is flipped about its own vertical axis, lifted a little on the way, as a left hand would turn it; the
# worker sees the other side. The camera and the hook stay where they are.
func _turn_animation() -> void:
	var f0 := flip
	flip = fmod(flip + PI, TAU)
	var pivot_local := _live_centre()
	var pivot_world := work_root.to_global(pivot_local)
	if not animate:
		_set_flip(f0 + PI, pivot_local, pivot_world)
		return
	var tw := create_tween()
	tw.tween_method(func(t: float) -> void:
		var lift := sin(t * PI) * 0.035
		_set_flip(f0 + PI * t, pivot_local, pivot_world + Vector3(0, lift, 0))
		_place_hook(_live_centre()), 0.0, 1.0, 1.2).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_IN_OUT)
	await tw.finished
	_set_flip(f0 + PI, pivot_local, pivot_world)
	_frame_work(false)

# The piece leans back (the face toward the worker) and is turned by `f` about its own vertical axis; `pivot_local` stays at `pivot_world`.
func _set_flip(f: float, pivot_local: Vector3, pivot_world: Vector3) -> void:
	work_root.basis = Basis(Vector3.RIGHT, -WORK_TILT) * Basis(Vector3.UP, f)
	work_root.position = pivot_world - work_root.basis * pivot_local

# ------------------------------------------------------------------------------------------------ drawing

# The expand and thickness sliders: how the piece is shown (the work itself is not changed).
func _apply_view() -> void:
	session.set_view(expand_slider.value, thick_slider.value)
	_rebuild(-1)
	_update_marker()
	if not busy:
		_place_hook(_live_centre())
		_frame_work(false)

func _rebuild(up_to: int) -> void:
	var d: Dictionary = session.get_mesh(up_to, 10, 3)
	var pos: PackedVector3Array = d["positions"]
	if pos.size() == 0:
		yarn_node.mesh = null
		return
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = pos
	arrays[Mesh.ARRAY_NORMAL] = d["normals"]
	arrays[Mesh.ARRAY_COLOR] = d["colors"]
	arrays[Mesh.ARRAY_INDEX] = d["indices"]
	var m := ArrayMesh.new()
	m.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	yarn_node.mesh = m
	_shown_particles = up_to if up_to >= 0 else session.get_particle_count()

func _refresh() -> void:
	var st: Dictionary = session.get_state()
	var next_id := int(st.get("next", -1))
	var targets: PackedInt32Array = st["targets"]
	var cursor := int(st["cursor"])
	b_sc.disabled = next_id < 0 and picked < 0
	var sk := _sc_kind() == "skip"
	b_sc.text = "Skip stitch" if sk else ("Single crochet" if _sc_kind() == "sc" else ("sc, front loop only" if _sc_kind() == "sc_front" else "sc, back loop only"))
	b_hdc.disabled = b_sc.disabled or sk
	b_dc.disabled = b_sc.disabled or sk
	b_slst.disabled = b_sc.disabled or sk
	colour_label.text = ("  next stitch changes colour" if bool(st.get("pending_colour", false)) else "")
	b_undo.disabled = history.size() == 0
	var row := int(st["row"])
	var made := int(st["made"])
	if row == 0:
		status_label.text = "Foundation chain: %d chains so far. Chain as many as you like, then Turn and work single crochet back along them." % made
	elif next_id < 0:
		status_label.text = "Row %d is done (%d stitches). Turn the work over for the next row." % [row, cursor]
	else:
		status_label.text = "Row %d: stitch %d of %d. Single crochet goes into the highlighted stitch; click another loop to work into that instead." % [row, cursor + 1, targets.size()]
	_refresh_overlay(st)
	_update_marker()

# The pattern so far, as symbols, semi transparent over the view. o = chain, x = single crochet; the stitch being worked into is marked.
func _refresh_overlay(st: Dictionary) -> void:
	var lines: PackedStringArray = String(st["text"]).split("\n", false)
	var out := "[b]Pattern[/b]\n"
	var row := 0
	for line in lines:
		var syms := ""
		for part in line.split(", ", false):
			var bits := part.split(" ")
			var n := int(bits[1]) if bits.size() > 1 else 1
			for i in n:
				syms += "o " if bits[0] == "ch" else "x "
		out += "[color=#bbbbbb]row %d[/color]  %s\n" % [row, syms]
		row += 1
	if lines.size() == 0:
		out += "[color=#bbbbbb](a slip knot)[/color]\n"
	overlay_text.text = out
	var next_id := int(st.get("next", -1))
	var target := picked if picked >= 0 else next_id
	target_label.text = ("Next stitch: into loop %d%s" % [target, " (picked)" if picked >= 0 else ""]) if target >= 0 else ""

func _update_marker() -> void:
	var st: Dictionary = session.get_state()
	var id := picked if picked >= 0 else int(st.get("next", -1))
	if id < 0:
		marker.visible = false
		return
	var loops: Array = session.get_loops()
	if id >= loops.size():
		marker.visible = false
		return
	var l: Dictionary = loops[id]
	marker.visible = true
	marker.position = l["centre"]
	var n: Vector3 = l["normal"]
	# the torus lies in the xz plane (its axis is y); turn its axis onto the loop's normal
	var basis := Basis()
	var up_ref := Vector3(0, 1, 0)
	var axis := up_ref.cross(n)
	if axis.length() > 1.0e-6:
		basis = Basis(axis.normalized(), up_ref.angle_to(n))
	elif n.y < 0:
		basis = Basis(Vector3(1, 0, 0), PI)
	marker.basis = basis

func _live_centre() -> Vector3:
	var st: Dictionary = session.get_state()
	if st.has("hook_tip"):
		return st["hook_tip"]
	return Vector3.ZERO

func _opening_centre(id: int) -> Vector3:
	var loops: Array = session.get_loops()
	if id >= 0 and id < loops.size():
		return loops[id]["centre"]
	return _live_centre()

# The hook's tip goes to `p` (in the piece's coordinates). Its shaft comes from the worker's right hand, up and a little toward the worker.
func _place_hook(p: Vector3) -> void:
	var handle_dir := Vector3(0.86, 0.38, 0.30).normalized()
	hook_node.global_position = work_root.to_global(p)
	hook_node.global_basis = Basis.looking_at(-handle_dir, Vector3.UP)   # the node's -z points from the handle to the tip

func _move_hook(p: Vector3, secs: float) -> void:
	if not animate:
		_place_hook(p)
		return
	var p0 := work_root.to_local(hook_node.global_position)
	var tw := create_tween()
	tw.tween_method(func(t: float) -> void: _place_hook(p0.lerp(p, t)), 0.0, 1.0, secs).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_IN_OUT)
	await tw.finished

# The camera looks at the live end of the work, close enough to see the stitches.
func _frame_work(snap: bool) -> void:
	var at := _live_centre()
	var tgt := work_root.to_global(at + Vector3(0, 0.004, 0))
	if snap or not follow_check.button_pressed:
		if snap:
			orbit.target = tgt
			orbit.radius = 0.05
			orbit.dist = 0.085
			orbit.apply()
		return
	if animate:
		var t0 := orbit.target
		var tw := create_tween()
		tw.tween_method(func(t: float) -> void:
			orbit.target = t0.lerp(tgt, t)
			orbit.apply(), 0.0, 1.0, 0.5).set_trans(Tween.TRANS_SINE)
	else:
		orbit.target = tgt
		orbit.apply()

# ------------------------------------------------------------------------------------------------ input and frames

func _on_view_input(event: InputEvent) -> void:
	# Ctrl (or Cmd) + left drag takes hold of the yarn under the pointer and pulls it; letting go, the work springs back
	if event is InputEventMouseButton and (event as InputEventMouseButton).button_index == MOUSE_BUTTON_LEFT:
		var mb := event as InputEventMouseButton
		if mb.pressed and (mb.ctrl_pressed or mb.meta_pressed) and not busy:
			_grab_at(mb.position)
			return
		if not mb.pressed and session.is_grabbed():
			_let_go()
			return
	if event is InputEventMouseMotion and session.is_grabbed():
		_drag_to((event as InputEventMouseMotion).position)
		return
	orbit.handle(event, view_container.size, _on_click)

# A ray from the pointer, in the piece's coordinates.
func _local_ray(pos: Vector2) -> Array:
	var o := orbit.cam.project_ray_origin(pos)
	var d := orbit.cam.project_ray_normal(pos)
	var inv := work_root.global_transform.basis.inverse()
	return [work_root.to_local(o), (inv * d).normalized()]

func _grab_at(pos: Vector2) -> void:
	var ray := _local_ray(pos)
	var r: Dictionary = session.grab(ray[0], ray[1])
	if not bool(r.get("ok", false)):
		return
	_grab_point = r["point"]
	_grab_normal = ((work_root.global_transform.basis.inverse() * (-orbit.cam.global_transform.basis.z)).normalized())
	status_label.text = "Holding the yarn: drag it about, let go and it springs back."
	soft_steps = 100000

func _drag_to(pos: Vector2) -> void:
	var ray := _local_ray(pos)
	var o: Vector3 = ray[0]
	var d: Vector3 = ray[1]
	var denom := d.dot(_grab_normal)
	if absf(denom) < 1.0e-6:
		return
	var t := (_grab_point - o).dot(_grab_normal) / denom
	session.drag_to(o + d * t)

func _let_go() -> void:
	session.release()
	soft_steps = 80
	_refresh()

# A click that did not drag picks the loop nearest to it (within a few pixels) as the opening of the next stitch.
func _on_click(pos: Vector2) -> void:
	if busy:
		return
	var best := -1
	var best_d := PICK_RADIUS_PX
	var loops: Array = session.get_loops()
	for l in loops:
		var c: Vector3 = l["centre"]
		if orbit.cam.is_position_behind(c):
			continue
		var s := orbit.cam.unproject_position(c)
		var d := s.distance_to(pos)
		if d < best_d:
			best_d = d
			best = int(l["id"])
	if best >= 0:
		picked = -1 if best == picked else best
		_refresh()

func _process(delta: float) -> void:
	_marker_t += delta
	if marker != null and marker.visible:
		var pulse := 1.0 + 0.10 * sin(_marker_t * 5.0)
		marker.scale = Vector3(pulse, pulse, pulse)
	if soft_steps > 0 and not busy:
		session.tick(2)
		soft_steps -= 2
		if session.is_grabbed() or soft_steps % 6 == 0:
			_rebuild(-1)
