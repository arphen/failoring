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
const Pattern = preload("res://crochet/yarn_pattern.gd")

signal closed

const HOOK_LENGTH := 0.07
const HOOK_RADIUS := 0.0022
const PICK_RADIUS_PX := 28.0
const YARN_COLOUR := Color(0.80, 0.30, 0.22)
const SWATCHES := [Color(0.80, 0.30, 0.22), Color(0.95, 0.58, 0.18), Color(0.93, 0.88, 0.76), Color(0.25, 0.45, 0.78), Color(0.30, 0.62, 0.38), Color(0.14, 0.14, 0.16)]
const BALL_RADIUS := 0.026
const OPEN_TOOL_SCALE := 0.55      # on the open lattice the yarn is thin (0.8 mm): a thinner hook, and hands to match
const BALL_SHADER := """shader_type spatial;
uniform vec4 albedo : source_color = vec4(0.8, 0.3, 0.2, 1.0);
varying vec3 obj;
void vertex() { obj = VERTEX; }
void fragment() {
	vec3 p = normalize(obj);
	float a = sin(46.0 * p.x + 17.0 * sin(7.0 * p.y + 3.0 * p.z));
	float b = sin(41.0 * p.y + 14.0 * sin(5.0 * p.z + 2.0 * p.x));
	float c = sin(49.0 * p.z + 15.0 * sin(6.0 * p.x + 4.0 * p.y));
	float t = 0.74 + 0.26 * (0.5 + 0.5 * (a * b + b * c + c * a) / 3.0);
	ALBEDO = albedo.rgb * t;
	ROUGHNESS = 0.95;
}
"""
const REST_HANDLE := Vector3(0.86, 0.38, 0.30)   # how the hook is held at rest: from the tip toward the right hand (world coordinates)
const TURNING_CHAINS := {"sc": 1, "hdc": 2, "dc": 3, "slst": 1}
const WORK_HEIGHT := 0.95          # the work is held at about the height of a table in the studio
const WORK_TILT := 1.0             # rad: the work leans back like a piece held in the left hand, its face toward the worker above the table

var session: YarnSession
var animate := true               # tests switch the animations off
var busy := false
var history: Array = []           # [kind, opening] of every action, for undo (replayed)
var picked := -1                  # a loop the user clicked: the opening of the next stitch (-1: the next in order)
var flip := 0.0                   # 0: the right side of the work faces the worker; -PI after a turn (the right edge comes toward you, every time); the foundation chain is not turned
var soft_steps := 0               # rod steps still to run so that new yarn settles

var sub_view: SubViewport
var stage: Node3D
var orbit := Orbit.new()
var view_container: SubViewportContainer
var work_root: Node3D             # the piece, its hook and its markers, at table height
var yarn_node: MeshInstance3D
var yarn_mat: StandardMaterial3D
var hook_node: Node3D
var tool_scale := OPEN_TOOL_SCALE              # the hook and the hands are scaled to the yarn (the open lattice's yarn is thin)
var lattice_option: OptionButton
var left_hand: Node3D              # the left hand holds the work by the loop on the hook, its index finger lifting the yarn
var right_hand: Node3D             # the right hand holds the hook (a child of the hook)
var hands_check: CheckBox
var _hand_basis := Basis(Vector3.RIGHT, -WORK_TILT)
const FINGER_LOCAL := Vector3(-0.016, 0.026, 0.012)   # the left index finger's tip, in the hand's frame (x right, y up the work, z out of its face) from the loop on the hook
var hook_sway := Vector3.ZERO      # a hand is never still: a small offset and tilt that come and go while the hook travels
var hook_tilt := Vector3.ZERO
var _run_ov := {}                  # while the hook makes a stitch: the run to the ball starts where the yarn is now (strand, end, dir, spin, rem, blend)
var _pre_ends: Array = []          # the yarns' ends before the stitch was made
var hook_roll := 0.0               # about the hook's own axis: 0 the throat faces up (going in), PI down (the yarn is over it, being drawn through)
var _hook_axis := Vector3.ZERO     # where the shaft lies (the piece's coordinates) when it is through an eye, and how much of the way from the usual position it is
var _hook_blend := 0.0
var marker: MeshInstance3D
var extra_markers: Array = []       # yellow rings on the loops picked up in the next last pull-through
var _click_shift := false
var ball_nodes: Array = []          # a ball of yarn for every yarn, and the run of the yarn to it
var run_nodes: Array = []
var hover_nodes: Array = []         # the stitch under the pointer, drawn over the yarn
var _hover_top := -1
var _ball_shader: Shader
var status_label: Label
var target_label: Label
var overlay_panel: PanelContainer
var pattern_panel: PanelContainer
var pattern_edit: TextEdit
var pattern_status: Label
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
var b_finish: Button
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
	call_deferred("_auto_sim")

# The piece starts as a slip knot made by the simulator (one strand of yarn, the hook through a ring), played as it is made. The stitch
# buttons still work on the placed lattice: the first one takes the view back to it.
func _auto_sim() -> void:
	if animate:
		start_sim("slip_knot")

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
	b_finish = _button("Finish off", "cut the yarn and draw the end through the loop on the hook: the piece is finished (Undo takes it back)", func() -> void: do_action("finish"))
	bar.add_child(b_finish)
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
	bar.add_child(_button("Pattern...", "Type or paste a pattern and have it made, or edit the pattern of what you have made", func() -> void: _toggle_pattern()))
	follow_check = CheckBox.new()
	follow_check.text = "Follow the hook"
	follow_check.button_pressed = true
	bar.add_child(follow_check)
	lattice_option = OptionButton.new()
	lattice_option.add_item("Open lattice: thin yarn, no crossings")
	lattice_option.add_item("Dense: thick yarn, overlaps")
	lattice_option.tooltip_text = "Open: one strand that does not pass through itself (a thin yarn on wide stitches). Dense: looks like crochet, but the strands overlap each other."
	lattice_option.item_selected.connect(func(i: int) -> void: set_lattice(i == 0))
	bar.add_child(lattice_option)
	hands_check = CheckBox.new()
	hands_check.text = "Hands"
	hands_check.tooltip_text = "Show the hands: the right one holds the hook, the left one the work, its index finger carrying the yarn"
	hands_check.button_pressed = true
	hands_check.toggled.connect(func(on: bool) -> void:
		left_hand.visible = on
		right_hand.visible = on
		_update_balls())
	bar.add_child(hands_check)
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
	bar.add_child(_button("Slip knot, simulated", "Watch a slip knot made from one real strand of yarn: the hook goes through a ring, catches the yarn and draws it through, and the knot closes. Nothing is placed: the yarn is simulated (it cannot pass through itself or the hook).",
			func() -> void: start_sim("slip_knot")))
	var close := _button("Back", "", func() -> void:
		visible = false
		closed.emit())
	bar.add_child(close)
	status_label = Label.new()
	status_label.text = ""
	root.add_child(status_label)
	_build_sim_bar(root)
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
	session.set_view(expand_slider.value, thick_slider.value)
	_build_mat()
	yarn_node = MeshInstance3D.new()
	yarn_mat = StandardMaterial3D.new()
	yarn_mat.vertex_color_use_as_albedo = true
	yarn_mat.roughness = 0.9
	yarn_mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	yarn_node.material_override = yarn_mat
	work_root.add_child(yarn_node)
	_build_hook()
	_build_hands()
	left_hand.basis = _hand_basis.scaled(Vector3.ONE * tool_scale)
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
	_build_pattern_panel(holder)
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

func _build_pattern_panel(holder: Control) -> void:
	pattern_panel = PanelContainer.new()
	pattern_panel.visible = false
	pattern_panel.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
	pattern_panel.offset_left = -400
	pattern_panel.offset_right = -12
	pattern_panel.offset_top = 12
	var box := VBoxContainer.new()
	pattern_panel.add_child(box)
	var t := Label.new()
	t.text = "Pattern: one row per line"
	box.add_child(t)
	pattern_edit = TextEdit.new()
	pattern_edit.custom_minimum_size = Vector2(380, 230)
	pattern_edit.placeholder_text = "ch 12\nhdc 11\nch 2, hdc 5, colour orange, hdc 6"
	box.add_child(pattern_edit)
	var row := HFlowContainer.new()
	box.add_child(row)
	row.add_child(_button("Build now", "Start again and make it all at once", func() -> void: build_pattern(false)))
	row.add_child(_button("Play", "Start again and watch each stitch being made", func() -> void: build_pattern(true)))
	row.add_child(_button("Show mine", "The pattern of what you have made", func() -> void: pattern_edit.text = Pattern.text_from_rows(session.get_pattern_rows())))
	pattern_status = Label.new()
	pattern_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	box.add_child(pattern_status)
	holder.add_child(pattern_panel)

func _toggle_pattern() -> void:
	pattern_panel.visible = not pattern_panel.visible
	if pattern_panel.visible and pattern_edit.text == "":
		pattern_edit.text = Pattern.text_from_rows(session.get_pattern_rows())

# Makes the pattern in the text box from a slip knot: at once, or stitch by stitch with the animations.
func build_pattern(play: bool) -> bool:
	if busy:
		return false
	var r: Dictionary = Pattern.parse(pattern_edit.text)
	if not bool(r["ok"]):
		pattern_status.text = "Line %d: %s" % [int(r["line"]), String(r["error"])]
		return false
	pattern_status.text = ""
	if not play:
		_replay(r["actions"])
		return true
	_replay([])
	for a in r["actions"]:
		if String(a[0]) == "colour":
			choose_colour(a[1])
		elif String(a[0]) == "turn":
			await do_action("turn", -2, int(a[1]))
		else:
			await do_action(String(a[0]))
	return true

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

# A smooth solid along z through elliptical sections [z, half width x, half width y, offset y], from the tip back; the first section closes the tip.
func _loft(sections: Array, segs := 24) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var rings: Array = []
	var norms: Array = []
	for sec in sections:
		var ring := PackedVector3Array()
		var nr := PackedVector3Array()
		for k in segs:
			var ang := TAU * float(k) / float(segs)
			ring.append(Vector3(cos(ang) * float(sec[1]), sin(ang) * float(sec[2]) + float(sec[3]), float(sec[0])))
			nr.append(Vector3(cos(ang) * maxf(float(sec[2]), 1.0e-6), sin(ang) * maxf(float(sec[1]), 1.0e-6), 0.0).normalized())
		rings.append(ring)
		norms.append(nr)
	for i in range(rings.size() - 1):
		for k in segs:
			var k2 := (k + 1) % segs
			var a: Vector3 = rings[i][k]
			var b: Vector3 = rings[i][k2]
			var c: Vector3 = rings[i + 1][k]
			var d: Vector3 = rings[i + 1][k2]
			var na: Vector3 = norms[i][k]
			var nb: Vector3 = norms[i][k2]
			var nc: Vector3 = norms[i + 1][k]
			var nd: Vector3 = norms[i + 1][k2]
			if i == 0:   # the tip: the first ring is a point, its normal points forward
				na = Vector3(0, 0, -1)
				nb = na
			for t in [[a, na], [c, nc], [b, nb], [b, nb], [c, nc], [d, nd]]:
				st.set_normal(t[1])
				st.add_vertex(t[0])
	# the back end, closed
	var last: PackedVector3Array = rings[rings.size() - 1]
	var zc: float = float(sections[sections.size() - 1][0])
	for k in segs:
		var k2 := (k + 1) % segs
		for v in [last[k], Vector3(0, float(sections[sections.size() - 1][3]), zc), last[k2]]:
			st.set_normal(Vector3(0, 0, 1))
			st.add_vertex(v)
	return st.commit()

# A finger, or a thumb: capsules between the joints, a ball at each joint.
func _limb(parent: Node3D, pts: Array, radius: float, mat: Material) -> void:
	for i in range(pts.size()):
		var j := MeshInstance3D.new()
		var sp := SphereMesh.new()
		sp.radius = radius
		sp.height = radius * 2.0
		sp.radial_segments = 16
		sp.rings = 8
		j.mesh = sp
		j.material_override = mat
		j.position = pts[i]
		parent.add_child(j)
		if i == 0:
			continue
		var a: Vector3 = pts[i - 1]
		var b: Vector3 = pts[i]
		var seg := MeshInstance3D.new()
		var cm := CapsuleMesh.new()
		cm.radius = radius
		cm.height = a.distance_to(b)
		cm.radial_segments = 16
		cm.rings = 4
		seg.mesh = cm
		seg.material_override = mat
		seg.position = (a + b) * 0.5
		var dir := (b - a).normalized()
		var ax := Vector3.UP.cross(dir)
		seg.basis = Basis(ax.normalized(), Vector3.UP.angle_to(dir)) if ax.length() > 1.0e-6 else (Basis() if dir.y > 0.0 else Basis(Vector3.RIGHT, PI))
		parent.add_child(seg)

func _blob(parent: Node3D, at: Vector3, size: Vector3, mat: Material) -> void:
	var m := MeshInstance3D.new()
	var sp := SphereMesh.new()
	sp.radius = 0.5
	sp.height = 1.0
	sp.radial_segments = 20
	sp.rings = 10
	m.mesh = sp
	m.material_override = mat
	m.position = at
	m.scale = size
	parent.add_child(m)

# The hands, as in the photographs. The right one holds the hook by its thumb rest: the thumb on one flat and the index finger on the other, the other fingers curled under
# the handle, the palm behind. The left one holds the work next to the loop on the hook: the thumb presses on its face, the index finger is raised and the yarn runs over
# its tip, the middle finger is behind the work.
func _build_hands() -> void:
	var skin := StandardMaterial3D.new()
	skin.albedo_color = Color(0.91, 0.72, 0.60)
	skin.roughness = 0.65
	var nail := StandardMaterial3D.new()
	nail.albedo_color = Color(0.95, 0.80, 0.76)
	nail.roughness = 0.35
	# the right hand, in the hook's frame (the tip at the origin, the shaft along +z, the throat toward +y)
	right_hand = Node3D.new()
	hook_node.add_child(right_hand)
	var tr := HOOK_LENGTH * 0.53   # the thumb rest
	# seen from the worker's side the thumb is on top (toward the viewer, local -x), the index finger behind the hook
	_limb(right_hand, [Vector3(-0.0042, 0.001, tr), Vector3(-0.013, -0.001, tr + 0.018), Vector3(-0.024, -0.005, tr + 0.038)], 0.0062, skin)
	_limb(right_hand, [Vector3(0.0042, 0.002, tr), Vector3(0.012, 0.010, tr + 0.016), Vector3(0.019, 0.017, tr + 0.034), Vector3(0.024, 0.019, tr + 0.054)], 0.0056, skin)
	_limb(right_hand, [Vector3(0.010, -0.006, tr + 0.040), Vector3(0.016, -0.012, tr + 0.054), Vector3(0.008, -0.016, tr + 0.066)], 0.0056, skin)
	_limb(right_hand, [Vector3(0.002, -0.012, tr + 0.044), Vector3(0.006, -0.019, tr + 0.058), Vector3(-0.002, -0.021, tr + 0.070)], 0.0052, skin)
	_blob(right_hand, Vector3(-0.002, -0.004, tr + 0.080), Vector3(0.046, 0.040, 0.060), skin)
	_blob(right_hand, Vector3(0.0, -0.002, tr + 0.140), Vector3(0.040, 0.036, 0.120), skin)
	# the left hand, in the frame of the work's lean (x right, y up the work, z out of its face), its origin at the loop on the hook
	left_hand = Node3D.new()
	stage.add_child(left_hand)
	left_hand.basis = _hand_basis
	_limb(left_hand, [Vector3(-0.115, 0.032, 0.012), Vector3(-0.088, 0.054, 0.016), Vector3(-0.056, 0.054, 0.014), FINGER_LOCAL], 0.0066, skin)   # the raised index finger
	_limb(left_hand, [Vector3(-0.120, -0.020, 0.052), Vector3(-0.092, -0.016, 0.040), Vector3(-0.066, -0.010, 0.030), Vector3(-0.046, -0.006, 0.022)], 0.0076, skin)   # the thumb on the face
	_limb(left_hand, [Vector3(-0.110, -0.010, -0.040), Vector3(-0.080, -0.012, -0.028), Vector3(-0.048, -0.008, -0.016)], 0.0066, skin)   # the middle finger behind
	_blob(left_hand, Vector3(-0.146, 0.006, 0.016), Vector3(0.060, 0.075, 0.050), skin)

# The left hand goes to the loop on the hook (it holds the work there); the tip of its index finger is where the yarn is carried.
func _hands_to(w: Vector3) -> void:
	left_hand.global_position = w

func _finger_tip_world() -> Vector3:
	return left_hand.to_global(FINGER_LOCAL)

func _build_hook() -> void:
	hook_node = Node3D.new()
	var steel := StandardMaterial3D.new()
	steel.albedo_color = Color(0.74, 0.76, 0.80)
	steel.metallic = 0.6
	steel.roughness = 0.35
	steel.cull_mode = BaseMaterial3D.CULL_DISABLED
	# An aluminium hook as in the photographs: one smooth piece. The tip at the origin, the shaft along +z, the throat facing +y (`hook_roll` turns it). A rounded head
	# whose tip curls toward the throat, its back edge a lip over the groove, a thinner neck lying a little to -y (that is the groove the yarn sits in), the shaft
	# swelling back to its full round, and the flat paddle of the thumb rest.
	var rr := HOOK_RADIUS
	var ll := HOOK_LENGTH
	var body := MeshInstance3D.new()
	var secs: Array = []
	for i in 8:   # the head: a dome whose tip is raised toward the throat side, a beak
		var ang := float(i) / 7.0 * PI * 0.5
		var zz := rr * 1.05 * (1.0 - cos(ang))
		var rad := rr * 1.05 * sin(ang)
		secs.append([zz, rad, rad, 0.22 * rr + 0.55 * rr * maxf(0.0, 1.0 - zz / (0.95 * rr))])
	secs.append([2.0 * rr, 1.05 * rr, 1.05 * rr, 0.22 * rr])
	secs.append([2.3 * rr, 0.98 * rr, 0.94 * rr, 0.14 * rr])   # the back edge of the head: the lip over the groove
	secs.append([2.45 * rr, 0.72 * rr, 0.70 * rr, -0.18 * rr])
	secs.append([2.65 * rr, 0.55 * rr, 0.55 * rr, -0.40 * rr])  # the neck
	secs.append([3.8 * rr, 0.58 * rr, 0.58 * rr, -0.37 * rr])
	secs.append([4.6 * rr, 0.85 * rr, 0.85 * rr, -0.12 * rr])
	secs.append([5.5 * rr, rr, rr, 0.0])
	secs.append_array([
		[0.40 * ll, 1.0 * rr, 1.0 * rr, 0.0],
		[0.46 * ll, 0.55 * rr, 1.9 * rr, 0.0],
		[0.60 * ll, 0.55 * rr, 1.9 * rr, 0.0],
		[0.67 * ll, 1.0 * rr, 1.0 * rr, 0.0],
		[0.76 * ll, 1.0 * rr, 1.0 * rr, 0.0]])
	body.mesh = _loft(secs)
	body.material_override = steel
	hook_node.add_child(body)
	var handle := MeshInstance3D.new()
	var hc := CylinderMesh.new()
	hc.top_radius = HOOK_RADIUS * 2.0
	hc.bottom_radius = HOOK_RADIUS * 1.7
	hc.height = HOOK_LENGTH * 0.40
	handle.mesh = hc
	var wood := StandardMaterial3D.new()
	wood.albedo_color = Color(0.20, 0.45, 0.62)
	wood.roughness = 0.5
	handle.material_override = wood
	handle.rotation = Vector3(PI * 0.5, 0, 0)
	handle.position = Vector3(0, 0, HOOK_LENGTH * 0.86)
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
func do_action(kind: String, opening: int = -2, turn_chains: int = -1) -> bool:
	if sim_root != null and sim_root.visible:
		close_sim()
	if opening == -2:
		opening = picked
	if busy:   # clicks while the hook is working are kept and made one after the other (a few at most)
		if _queue.size() < MAX_QUEUE:
			_queue.append([kind, opening, turn_chains])
			status_label.text = "%s (%d waiting)" % [status_label.text.get_slice("  (", 0), _queue.size()]
			return true
		return false
	busy = true
	var st: Dictionary = session.get_state()
	if int(st["row"]) == 0 and (_is_stitch(kind) or kind == "skip") and int(st["made"]) >= 2:
		# the foundation chain is done: the first stitch goes into the chain, nothing to press, nothing to rotate (the chain already lies to the left of the hook)
		var r0: Dictionary = session.action("turn", 0)
		if bool(r0.get("ok", false)):
			history.append(["turn", 0])
			_rebuild(-1)
			st = session.get_state()
	var rotate := kind == "turn" and int(st["row"]) > 0   # only a finished row is turned over
	if (_is_stitch(kind) or kind == "skip") and opening < 0 and int(st.get("next", -1)) < 0:
		status_label.text = "Nothing to work into: turn the work over first."
		busy = false
		return false
	var open_id := opening if opening >= 0 else int(st.get("next", -1))
	if animate and kind == "skip":   # (the other stitches come at their eye themselves)
		var at := _opening_centre(open_id) if ((_is_stitch(kind) or kind == "skip") and open_id >= 0) else _live_centre()
		await _move_hook(at, 0.30)
	var before := session.get_particle_count()
	var arg := opening
	if kind == "turn":
		arg = turn_chains if turn_chains >= 0 else (0 if int(st["row"]) == 0 else int(TURNING_CHAINS.get(last_kind, 1)))
	_pre_ends = session.get_state()["ends"]
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
		var turn_made: Array = r.get("loops", [])
		if animate and turn_made.size() > 0:
			await _pull_animation(before, turn_made)   # the turning chain first ...
		else:
			_rebuild(-1)
		if rotate:
			await _turn_animation()                      # ... then the work is turned over
	elif kind != "skip":
		var made: Array = r.get("loops", [])
		if animate and made.size() > 0:
			await _pull_animation(before, made)
		else:
			await _grow(before, int(r["particles"]))
	soft_steps = 16 if animate else 0
	_refresh()
	if animate:
		var hw0 := left_hand.global_position
		var hw1 := work_root.to_global(_loop_centre())
		var hand_tw := create_tween()
		hand_tw.tween_method(func(t: float) -> void:
			left_hand.global_position = hw0.lerp(hw1, _minjerk(t))
			_update_balls(), 0.0, 1.0, 0.45)
		await _move_hook(_live_centre(), 0.25)
		await hand_tw.finished
		if kind == "finish":   # the hook is taken away from the finished piece
			await _move_hook(work_root.to_local(work_root.to_global(_live_centre()) + Vector3(0.09, 0.05, 0.02)), 0.6)
		_frame_work(false)
	else:
		_rest_hook()
		_frame_work(true)
	busy = false
	if _queue.size() > 0:
		var nx: Array = _queue.pop_front()
		await do_action(String(nx[0]), int(nx[1]), int(nx[2]))
	else:
		idle.emit()
	return true

func undo() -> void:
	_queue.clear()
	if busy or history.size() == 0:
		return
	var keep: Array = history.duplicate()
	keep.pop_back()
	_replay(keep)

# The open lattice (the yarn does not pass through itself) or the dense one; the piece so far is made again on it.
func set_lattice(open: bool) -> void:
	if busy:
		return
	session.set_open_lattice(open)
	tool_scale = OPEN_TOOL_SCALE if open else 1.0
	left_hand.basis = _hand_basis.scaled(Vector3.ONE * tool_scale)
	lattice_option.select(0 if open else 1)
	var keep: Array = history.duplicate()
	_replay(keep)

func reset_piece() -> void:
	_queue.clear()
	if busy:
		return
	_replay([])
	_auto_sim()

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
		var row_before := int(session.get_state()["row"])
		var r: Dictionary = session.action(String(a[0]), int(a[1]))
		if bool(r.get("ok", false)):
			history.append(a)
			if _is_stitch(String(a[0])):
				last_kind = _base_kind(String(a[0]))
			if String(a[0]) == "turn" and row_before > 0:
				flip -= PI
	flip = fmod(flip, TAU)
	_set_flip(flip, _loop_centre(), work_root.to_global(_loop_centre()))
	_rebuild(-1)
	_refresh()
	_frame_work(true)
	_rest_hook()

# The hook makes the stitch, and the yarn is the yarn: it hangs from the work to its ball, and the ball turns as it is drawn off. For each loop the stitch made, the hook
# comes at the eye it works into from the front, throat up, and goes in through it to behind (insert); it picks the yarn up off its hanging curve, lays it over the shaft
# and turns its throat down (yarn over); and draws it back through the eye, the loop opening as it comes (pull). A loop with no eye (a double's first yarn over) is the yarn
# picked up and wound round the hook. `before` is how many particles the yarn had.
var _last_frame := {}
signal idle   # the hook has done everything it was asked (nothing is waiting)
const MAX_QUEUE := 12
var _queue: Array = []             # clicks made while the hook was busy: [kind, opening, turn_chains]

func _minjerk(t: float) -> float:   # how a hand moves: no jerk at either end
	return t * t * t * (10.0 - 15.0 * t + 6.0 * t * t)

func _tween01(secs: float, fn: Callable) -> void:
	if not animate:
		fn.call(1.0)
		return
	var tw := create_tween()
	tw.tween_method(fn, 0.0, 1.0, secs)
	await tw.finished

# One moment of the yarn while the hook works: the mesh, and the run to the ball from where the yarn is now.
func _frame_step(id: int, s: float, wrap: float, hang: PackedVector3Array, phi: float, rem: PackedVector3Array, spin: float, has_run: bool, strand: int) -> Dictionary:
	var d: Dictionary = session.anim_frame(id, s, wrap, hang, phi, 10, 3)
	if d.is_empty():
		return d
	_show_mesh(d)
	if has_run:
		_run_ov = {"strand": strand, "end": d["run_end"], "dir": d["run_dir"], "spin": spin, "rem": rem, "blend": phi}
		_update_balls()
	return d

func _pull_animation(before: int, made: Array) -> void:
	_rebuild(before)
	var en0 := {}
	for en in _pre_ends:
		if bool(en.get("active", false)):
			en0 = en
	var has_run := not en0.is_empty()
	var cur_end: Vector3 = en0.get("end", Vector3.ZERO)
	var cur_dir: Vector3 = en0.get("dir", Vector3.RIGHT)
	var strand := int(en0.get("strand", 0))
	var spin := float(en0.get("length", 0.0)) / BALL_RADIUS
	if has_run:
		_run_ov = {"strand": strand, "end": cur_end, "dir": cur_dir, "spin": spin}
		_update_balls()
	var prev := {}
	for id in made:
		var hang := PackedVector3Array()
		if has_run:
			hang = _hang_points(cur_end, cur_dir, strand)
		var f0: Dictionary = session.anim_frame(int(id), 0.0, 1.0, hang, 0.0, 10, 3)   # the yarn laid along the curve it hangs on
		if f0.is_empty():
			continue
		var nat := float(f0["nat_length"])                # the yarn this loop takes off the ball
		var dspin := nat / BALL_RADIUS
		var rem := _remainder(hang, nat) if has_run else PackedVector3Array()
		var spin0 := spin
		var iid := int(id)
		if not prev.is_empty():
			_show_mesh(prev)                              # the loop before is finished and stays
		_last_frame = {}
		if not bool(f0["through"]):
			await _hook_travel(f0["tip"], f0["axis"], PI, true)
			await _tween01(0.7, func(t: float) -> void:
				var u := _minjerk(t)
				var d := _frame_step(iid, maxf(0.0, 2.0 * u - 1.0), 0.0, hang, minf(1.0, 2.0 * u), rem, spin0 + dspin * u, has_run, strand)
				if not d.is_empty():
					_last_frame = d
				_place_hook(f0["tip"], f0["axis"], 1.0))
		else:
			var eye: Vector3 = f0["eye"]
			var ax: Vector3 = f0["axis"]
			var ride: Callable = func() -> void: _show_mesh(session.get_mesh(before, 10, 3))
			if not prev.is_empty():
				ride = Callable()
			await _hook_travel(eye + ax * 0.016, ax, 0.0, true, ride)   # in front of the eye, throat up
			await _hook_travel(f0["tip"], ax, 0.0, false, ride)         # insert: through the eye to behind it
			await _tween01(0.6, func(t: float) -> void:               # pick the yarn up and lay it over the shaft; the throat turns down
				var u := _minjerk(t)
				var d := _frame_step(iid, 0.0, 1.0, hang, u, rem, spin0 + dspin * 0.3 * u, has_run, strand)
				if not d.is_empty():
					_last_frame = d
				hook_roll = PI * u
				_place_hook(f0["tip"], ax, 1.0))
			await _tween01(0.85, func(t: float) -> void:              # draw it back through the eye with the yarn in the throat
				var u := _minjerk(t)
				var d := _frame_step(iid, u, maxf(0.0, 1.0 - 2.0 * u), hang, 1.0, rem, spin0 + dspin * (0.3 + 0.7 * u), has_run, strand)
				if not d.is_empty():
					_last_frame = d
					_place_hook(d["tip"], d["axis"], 1.0))
		if _last_frame.is_empty():
			continue
		prev = _last_frame
		cur_end = _last_frame["run_end"]
		cur_dir = _last_frame["run_dir"]
		spin += dspin
	_run_ov = {}
	_rebuild(-1)

func _grow(from_particles: int, to_particles: int) -> void:
	if not animate or to_particles - from_particles < 4:
		_rebuild(-1)
		return
	var tw := create_tween()
	tw.tween_method(func(n: float) -> void: _rebuild(int(n)), float(from_particles), float(to_particles), 0.45).set_trans(Tween.TRANS_SINE)
	await tw.finished
	_rebuild(-1)

# Turning the work over, as the left hand does it: the hook and the loop on it stay where they are and the work swings round them, half a turn about its
# own vertical axis through the loop, the right edge coming toward the worker (the same way every row). The run to the ball keeps its ball.
func _turn_animation() -> void:
	var f0 := flip
	flip = fmod(flip - PI, TAU)
	var pivot_local := _loop_centre()
	var pivot_world := work_root.to_global(pivot_local)
	if not animate:
		_set_flip(f0 - PI, pivot_local, pivot_world)
		return
	var tw := create_tween()
	tw.tween_method(func(t: float) -> void:
		_set_flip(f0 - PI * t, pivot_local, pivot_world)
		_place_hook(_live_centre())
		_show_mesh(session.get_mesh(-1, 10, 3))
		_update_balls(), 0.0, 1.0, 1.1).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN_OUT)
	await tw.finished
	_set_flip(f0 - PI, pivot_local, pivot_world)
	_place_hook(_live_centre())
	_show_mesh(session.get_mesh(-1, 10, 3))
	_update_balls()
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
		_rest_hook()
		_frame_work(false)

func _rebuild(up_to: int) -> void:
	_show_mesh(session.get_mesh(up_to, 10, 3))
	_shown_particles = up_to if up_to >= 0 else session.get_particle_count()
	_update_balls()

func _show_mesh(d: Dictionary) -> void:
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

func _refresh() -> void:
	var st: Dictionary = session.get_state()
	var next_id := int(st.get("next", -1))
	var targets: PackedInt32Array = st["targets"]
	var cursor := int(st["cursor"])
	b_sc.disabled = next_id < 0 and picked < 0 and not (int(st["row"]) == 0 and int(st["made"]) >= 2)   # after the foundation chain it starts the row
	var sk := _sc_kind() == "skip"
	b_sc.text = "Skip stitch" if sk else ("Single crochet" if _sc_kind() == "sc" else ("sc, front loop only" if _sc_kind() == "sc_front" else "sc, back loop only"))
	b_hdc.disabled = b_sc.disabled or sk
	b_dc.disabled = b_sc.disabled or sk
	b_slst.disabled = b_sc.disabled or sk
	colour_label.text = ("  next stitch changes colour" if bool(st.get("pending_colour", false)) else "")
	b_undo.disabled = history.size() == 0
	var done_work := bool(st.get("finished", false))
	b_finish.disabled = done_work or int(st["made"]) + int(st["row"]) < 1
	b_chain.disabled = done_work
	b_turn.disabled = done_work
	if done_work:
		for b in [b_sc, b_hdc, b_dc, b_slst]:
			(b as Button).disabled = true
	var row := int(st["row"])
	var made := int(st["made"])
	if done_work:
		status_label.text = "Fastened off: the yarn is cut and its end is through the last loop. The piece is finished (Undo takes the last step back, New piece starts again)."
	elif row == 0:
		status_label.text = "Foundation chain: %d chains so far. Chain as many as you like (the loop on the hook is not counted, the first chain from it is skipped), then work single crochet back along them: no turning needed." % made
	elif next_id < 0:
		status_label.text = "Row %d is done (%d stitches). Turn the work over for the next row." % [row, cursor]
	else:
		status_label.text = "Row %d: stitch %d of %d. Single crochet goes into the highlighted stitch; click another loop to work into that instead." % [row, cursor + 1, targets.size()]
	_refresh_overlay(st)
	_update_marker()

# The pattern so far, as symbols in the yarn's colour, semi transparent over the view. o chain, + single crochet, T half double, a dagger double,
# a dot slip stitch, a dash a skipped stitch; an underline marks a stitch worked into one loop only.
const SYMBOLS := {"ch": "o", "sc": "+", "hdc": "T", "dc": "†", "sl st": "•", "sk": "–", "fo": "■"}

func _refresh_overlay(st: Dictionary) -> void:
	var out := "[b]Pattern[/b]\n"
	var rows: Array = session.get_pattern_rows()
	var n := 0
	for row in rows:
		var syms := ""
		for e in row:
			var kind := String(e["kind"])
			var one := kind.ends_with(" flo") or kind.ends_with(" blo")
			var base := kind.substr(0, kind.length() - 4) if one else kind
			var sym := String(SYMBOLS.get(base, "?"))
			var col: Color = e["colour"]
			var piece := ""
			for i in int(e["count"]):
				piece += ("[u]%s[/u] " % sym) if one else (sym + " ")
			syms += "[color=#%s]%s[/color]" % [col.to_html(false), piece]
		out += "[color=#bbbbbb]row %d[/color]  %s\n" % [n, syms]
		n += 1
	if rows.size() == 0 or (rows.size() == 1 and (rows[0] as Array).size() == 0):
		out += "[color=#bbbbbb](a slip knot)[/color]\n"
	overlay_text.text = out
	var next_id := int(st.get("next", -1))
	var target := picked if picked >= 0 else next_id
	var extra: PackedInt32Array = st.get("extra", PackedInt32Array())
	target_label.text = ("Next stitch: into loop %d%s" % [target, " (picked)" if picked >= 0 else ""]) if target >= 0 else ""
	if extra.size() > 0:
		target_label.text += "   last pull-through also takes %d loop%s (Shift+click toggles)" % [extra.size(), "" if extra.size() == 1 else "s"]

func _update_marker() -> void:
	_update_extra_markers()
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

# Every yarn runs to its ball: a curve from the end of the yarn to a ball of yarn standing on the table beside the work. A yarn does not end in the air.
func _ball_position(i: int) -> Vector3:
	return Vector3(0.13 + 0.065 * i, WORK_HEIGHT - 0.016 + 0.003 + BALL_RADIUS, -0.11)

# The run of a yarn from `e0` (leaving along `d0`, world coordinates) to its ball at `bp`, `n` points: from the work taut over the tip of the left index finger (when the
# hands are shown), and from there to the ball hanging a little.
func _run_curve(e0: Vector3, d0: Vector3, bp: Vector3, n := 25) -> PackedVector3Array:
	if hands_check == null or left_hand == null or not hands_check.button_pressed:
		return _run_curve_plain(e0, d0, bp, n)
	var up := (_hand_basis * Vector3(0.3, 1.0, 0.2)).normalized()
	var f := _finger_tip_world() + up * 0.0085   # over the finger
	var n1 := maxi(6, n / 3)
	var pts := PackedVector3Array()
	for k in n1:
		var t := float(k) / float(n1 - 1)
		pts.append(e0.lerp(f, t) + Vector3.DOWN * 0.004 * sin(PI * t))   # held taut, but not like a rod
	var rest := _run_curve_plain(f, up, bp, n - n1 + 1)
	for k in range(1, rest.size()):
		pts.append(rest[k])
	return pts

func _run_curve_plain(e0: Vector3, d0: Vector3, bp: Vector3, n := 25) -> PackedVector3Array:
	var surf := bp + (e0 - bp).normalized() * BALL_RADIUS * 0.96
	var p1 := e0 + d0 * 0.03
	var p2 := surf + ((e0 - surf).normalized() * 0.035 + Vector3.UP * 0.012)
	var table_y := WORK_HEIGHT - 0.016 + 0.003 + 0.0016
	var pts := PackedVector3Array()
	for k in n:
		var t := float(k) / float(n - 1)
		var u := 1.0 - t
		var pt := u * u * u * e0 + 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t * surf
		pt.y -= 0.010 * sin(PI * t)   # it hangs a little
		pt.y = maxf(pt.y, table_y)
		pts.append(pt)
	return pts

# The yarn as it hangs from the work to its ball, in the piece's coordinates (what the hook picks up).
func _hang_points(e_loc: Vector3, d_loc: Vector3, strand: int) -> PackedVector3Array:
	var w := _run_curve(work_root.to_global(e_loc), (work_root.global_transform.basis * d_loc).normalized(), _ball_position(strand), 80)
	var out := PackedVector3Array()
	for q in w:
		out.append(work_root.to_local(q))
	return out

# The part of a hanging yarn beyond `arc` from its start (world coordinates), `n` points evenly along it: the run left when the hook has taken the first part.
func _remainder(hang_local: PackedVector3Array, arc: float, n := 25) -> PackedVector3Array:
	var w := PackedVector3Array()
	for q in hang_local:
		w.append(work_root.to_global(q))
	var acc := 0.0
	var start := w.size() - 2
	var from := w[start]
	for i in range(1, w.size()):
		var seg := w[i].distance_to(w[i - 1])
		if acc + seg >= arc:
			start = i - 1
			from = w[i - 1].lerp(w[i], (arc - acc) / maxf(seg, 1.0e-9))
			break
		acc += seg
	var chain := PackedVector3Array([from])
	for i in range(start + 1, w.size()):
		chain.append(w[i])
	var total := 0.0
	for i in range(1, chain.size()):
		total += chain[i].distance_to(chain[i - 1])
	var out := PackedVector3Array()
	var k := 1
	var run := 0.0
	out.append(chain[0])
	for j in range(1, n):
		var want := total * float(j) / float(n - 1)
		while k < chain.size() - 1 and run + chain[k].distance_to(chain[k - 1]) < want:
			run += chain[k].distance_to(chain[k - 1])
			k += 1
		var seg2 := chain[k].distance_to(chain[k - 1])
		out.append(chain[k - 1].lerp(chain[k], clampf((want - run) / maxf(seg2, 1.0e-9), 0.0, 1.0)))
	return out

func _update_balls() -> void:
	if session == null or work_root == null:
		return
	var ends: Array = session.get_state().get("ends", [])
	if _ball_shader == null:
		_ball_shader = Shader.new()
		_ball_shader.code = BALL_SHADER
	while ball_nodes.size() < ends.size():
		var b := MeshInstance3D.new()
		var sp := SphereMesh.new()
		sp.radius = BALL_RADIUS
		sp.height = BALL_RADIUS * 2.0
		sp.radial_segments = 32
		sp.rings = 16
		b.mesh = sp
		b.material_override = ShaderMaterial.new()
		(b.material_override as ShaderMaterial).shader = _ball_shader
		stage.add_child(b)
		ball_nodes.append(b)
		var r := MeshInstance3D.new()
		r.material_override = yarn_mat
		stage.add_child(r)
		run_nodes.append(r)
	for i in ball_nodes.size():
		var visible_i := i < ends.size()
		(ball_nodes[i] as Node3D).visible = visible_i
		(run_nodes[i] as Node3D).visible = visible_i
		if not visible_i:
			continue
		var en: Dictionary = ends[i]
		var col: Color = en["colour"]
		var strand := int(en["strand"])
		var bp := _ball_position(strand)
		var b: MeshInstance3D = ball_nodes[i]
		b.position = bp
		(b.material_override as ShaderMaterial).set_shader_parameter("albedo", col)
		var e_loc: Vector3 = en["end"]
		var d_loc: Vector3 = en["dir"]
		var spin := float(en.get("length", 0.0)) / BALL_RADIUS   # the ball turns as its yarn is drawn off
		var ov: Dictionary = _run_ov if (not _run_ov.is_empty() and int(_run_ov["strand"]) == strand) else {}
		if not ov.is_empty():
			e_loc = ov["end"]
			d_loc = ov["dir"]
			spin = float(ov["spin"])
		var e0 := work_root.to_global(e_loc)
		var d0: Vector3 = (work_root.global_transform.basis * d_loc).normalized()
		var pts := _run_curve(e0, d0, bp)
		if not ov.is_empty() and ov.has("rem") and float(ov["blend"]) < 1.0:   # still the yarn that hung there, becoming the run as the hook takes its part
			var rem: PackedVector3Array = ov["rem"]
			var wb := smoothstep(0.0, 1.0, float(ov["blend"]))
			for k in pts.size():
				pts[k] = rem[k].lerp(pts[k], wb)
		var spin_axis := Vector3.UP.cross(bp - e0)
		if spin_axis.length() > 1.0e-6:
			b.basis = Basis(spin_axis.normalized(), spin)
		var d: Dictionary = session.tube(pts, col, session.get_radius() * thick_slider.value, 8, 2)
		var arrays := []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = d["positions"]
		arrays[Mesh.ARRAY_NORMAL] = d["normals"]
		arrays[Mesh.ARRAY_COLOR] = d["colors"]
		arrays[Mesh.ARRAY_INDEX] = d["indices"]
		var m := ArrayMesh.new()
		m.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		(run_nodes[i] as MeshInstance3D).mesh = m

# The stitch under the pointer, drawn over the yarn so that it is clear what a click would work into.
func _update_hover(pos: Vector2) -> void:
	if busy or session.is_grabbed():
		_clear_hover()
		return
	var ray := _local_ray(pos)
	var r: Dictionary = session.pick(ray[0], ray[1])
	var top := int(r["top"]) if bool(r.get("ok", false)) else -1
	if top == _hover_top:
		return
	_hover_top = top
	_clear_hover()
	if top < 0:
		return
	var loops: Array = session.get_loops()
	for l in loops:
		if int(l["top"]) != top:
			continue
		var d: Dictionary = session.get_loop_mesh(int(l["id"]))
		if d.is_empty():
			continue
		var arrays := []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = d["positions"]
		arrays[Mesh.ARRAY_NORMAL] = d["normals"]
		arrays[Mesh.ARRAY_INDEX] = d["indices"]
		var m := ArrayMesh.new()
		m.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		var mi := MeshInstance3D.new()
		mi.mesh = m
		var mt := StandardMaterial3D.new()
		mt.albedo_color = Color(1.0, 0.9, 0.3, 0.45)
		mt.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		mt.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		mt.no_depth_test = false
		mi.material_override = mt
		work_root.add_child(mi)
		hover_nodes.append(mi)

func _clear_hover() -> void:
	for n in hover_nodes:
		(n as Node).queue_free()
	hover_nodes.clear()

func _update_extra_markers() -> void:
	var extra: PackedInt32Array = session.get_state().get("extra", PackedInt32Array())
	var loops: Array = session.get_loops()
	while extra_markers.size() < extra.size():
		var m := MeshInstance3D.new()
		m.mesh = marker.mesh
		var mt := StandardMaterial3D.new()
		mt.albedo_color = Color(1.0, 0.85, 0.2, 0.8)
		mt.emission_enabled = true
		mt.emission = Color(0.9, 0.7, 0.1)
		mt.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		mt.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		m.material_override = mt
		work_root.add_child(m)
		extra_markers.append(m)
	for i in extra_markers.size():
		var m: MeshInstance3D = extra_markers[i]
		if i < extra.size() and int(extra[i]) < loops.size():
			var l: Dictionary = loops[int(extra[i])]
			m.visible = true
			m.position = l["centre"]
			var n: Vector3 = l["normal"]
			var axis := Vector3.UP.cross(n)
			m.basis = Basis(axis.normalized(), Vector3.UP.angle_to(n)) if axis.length() > 1.0e-6 else Basis()
		else:
			m.visible = false

# Where the tip of the resting hook is: through the loop on it, which stands round it.
func _live_centre() -> Vector3:
	var st: Dictionary = session.get_state()
	if st.has("hook_tip"):
		return session.get_stand_tip(_rest_dir_piece())
	return Vector3.ZERO

# The middle of the ring of the loop on the hook as it was made (the piece's coordinates): what the work is turned about.
func _loop_centre() -> Vector3:
	var st: Dictionary = session.get_state()
	return st["loop_centre"] if st.has("loop_centre") else Vector3.ZERO

# The way from the handle to the tip of the resting hook, in the piece's coordinates.
func _rest_dir_piece() -> Vector3:
	return (work_root.global_basis.inverse() * (-REST_HANDLE.normalized())).normalized()

# The hook at rest, the loop standing on it.
func _rest_hook() -> void:
	_hands_to(work_root.to_global(_loop_centre()))
	_place_hook(_live_centre())
	_show_mesh(session.get_mesh(-1, 10, 3))

func _opening_centre(id: int) -> Vector3:
	var loops: Array = session.get_loops()
	if id >= 0 and id < loops.size():
		return loops[id]["centre"]
	return _live_centre()

# The hook's tip goes to `p` (in the piece's coordinates). Its shaft comes from the worker's right hand, up and a little toward the worker.
func _place_hook(p: Vector3, axis := Vector3.ZERO, blend := 0.0) -> void:
	if blend > 0.0 and axis.length() > 0.0:   # `axis` (the piece's coordinates) is the way from the tip to the handle: the shaft goes through the eye
		_hook_axis = axis
	_hook_blend = blend
	var handle_dir := REST_HANDLE.normalized()
	if blend > 0.0 and _hook_axis.length() > 0.0:
		handle_dir = handle_dir.lerp((work_root.global_basis * _hook_axis).normalized(), blend).normalized()
	handle_dir = (handle_dir + hook_tilt).normalized()
	hook_node.global_position = work_root.to_global(p) + hook_sway
	hook_node.global_basis = Basis.looking_at(-handle_dir, Vector3.UP) * Basis(Vector3(0, 0, 1), hook_roll)   # the node's -z points from the handle to the tip
	hook_node.scale = Vector3.ONE * tool_scale
	# the loops on the hook stand round it and ride it
	session.set_stand((work_root.global_basis.inverse() * (-hook_node.global_basis.z)).normalized(), work_root.to_local(hook_node.global_position), 1.0)

var _sway_phase := 0.0

# The hook travels to `p` (the piece's coordinates) the way a hand takes it: along an arc that lifts it over the work (`arc`), with no jerk at either end and a little
# drift and tilt on the way; its shaft turns to lie along `axis` (zero: let go of the eye, back to the usual way it is held) and its throat to `roll`.
func _hook_travel(p: Vector3, axis: Vector3, roll: float, arc: bool, follow := Callable()) -> void:
	if not animate:
		hook_roll = roll
		_place_hook(p, axis, 1.0 if axis.length() > 0.0 else 0.0)
		if follow.is_valid():
			follow.call()
		return
	var p0w := hook_node.global_position - hook_sway
	var p1w := work_root.to_global(p)
	var dist := p0w.distance_to(p1w)
	var secs := clampf(0.28 + dist * 7.0, 0.3, 0.8)
	var ctrl := (p0w + p1w) * 0.5
	if arc:
		ctrl += (Vector3.UP + Vector3(0, 0, 0.7)).normalized() * dist * 0.3
	var has_axis := axis.length() > 0.0
	var a0 := _hook_axis
	var a1 := axis if has_axis else (a0 if a0.length() > 0.0 else Vector3.RIGHT)
	var b0 := _hook_blend
	var b1 := 1.0 if has_axis else 0.0
	var r0 := hook_roll
	_sway_phase += 1.7
	var ph := _sway_phase
	var tw := create_tween()
	tw.tween_method(func(t: float) -> void:
		var u := _minjerk(t)
		var pw := p0w.lerp(ctrl, u).lerp(ctrl.lerp(p1w, u), u)
		var env := sin(PI * t)
		hook_sway = Vector3(sin(t * 9.0 + ph), sin(t * 7.0 + ph * 1.3), sin(t * 8.0 + ph * 0.7)) * 0.0007 * env
		hook_tilt = Vector3(sin(t * 6.0 + ph), sin(t * 4.0 + ph * 1.1) * 0.5, sin(t * 5.0 + ph * 2.0)) * 0.06 * env
		hook_roll = lerpf(r0, roll, u)
		var ax := a0.lerp(a1, u).normalized() if a0.length() > 0.0 else a1
		_place_hook(work_root.to_local(pw), ax, lerpf(b0, b1, u))
		if follow.is_valid():
			follow.call(), 0.0, 1.0, secs)
	await tw.finished
	hook_sway = Vector3.ZERO
	hook_tilt = Vector3.ZERO

func _move_hook(p: Vector3, _secs: float) -> void:
	await _hook_travel(p, Vector3.ZERO, 0.0, true, func() -> void: _show_mesh(session.get_mesh(-1, 10, 3)))

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
	if event is InputEventMouseButton and (event as InputEventMouseButton).button_index == MOUSE_BUTTON_LEFT:
		_click_shift = (event as InputEventMouseButton).shift_pressed
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
	if event is InputEventMouseMotion and (event as InputEventMouseMotion).button_mask == 0:
		_update_hover((event as InputEventMouseMotion).position)
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

# A click that did not drag picks the stitch whose yarn is under the pointer (or whose hole the pointer is in) as the opening of the next stitch.
func _on_click(pos: Vector2) -> void:
	if busy:
		return
	var ray := _local_ray(pos)
	var r: Dictionary = session.pick(ray[0], ray[1])
	if not bool(r.get("ok", false)):
		return
	var top := int(r["top"])
	if _click_shift:   # Shift + click: this stitch's loop is picked up in the last pull-through of the next stitch as well (a seam variation)
		session.toggle_extra(top)
	else:
		picked = -1 if top == picked else top
	_refresh()

func _process(delta: float) -> void:
	_sim_process(delta)
	_marker_t += delta
	if marker != null and marker.visible:
		var pulse := 1.0 + 0.10 * sin(_marker_t * 5.0)
		marker.scale = Vector3(pulse, pulse, pulse)
	if soft_steps > 0 and not busy:
		session.tick(2)
		soft_steps -= 2
		if session.is_grabbed() or soft_steps % 6 == 0:
			_rebuild(-1)

# ------------------------------------------------------------------------------------------------ the simulator (docs/YARN_MODE_PLAN.md section 14)

# A procedure simulated from one real strand of yarn, played back: the hook (drawn from the capsules the yarn collides with) moves, the yarn does
# what the physics makes it do. The design piece and its hook are hidden meanwhile.
var sim_session: YarnSimSession
var sim_root: Node3D
var sim_yarn: MeshInstance3D
var sim_hook: MeshInstance3D
var sim_bar: HBoxContainer
var sim_slider: HSlider
var sim_play: Button
var sim_speed: OptionButton
var sim_caption: Label
var sim_t := 0.0
var sim_playing := false
var sim_frames := 0
var _sim_shown := -1
var _sim_hidden: Array = []

func _build_sim_bar(root: Control) -> void:
	sim_bar = HBoxContainer.new()
	sim_bar.visible = false
	root.add_child(sim_bar)
	sim_play = _button("Pause", "play or pause", func() -> void:
		if not sim_playing and sim_t * 60.0 >= sim_frames - 1:
			sim_t = 0.0
		sim_playing = not sim_playing
		sim_play.text = "Pause" if sim_playing else "Play")
	sim_bar.add_child(sim_play)
	sim_bar.add_child(_button("Again", "from the start", func() -> void:
		sim_t = 0.0
		sim_playing = true
		sim_play.text = "Pause"))
	sim_speed = OptionButton.new()
	for sp in ["1/4 speed", "1/2 speed", "real speed"]:
		sim_speed.add_item(sp)
	sim_speed.select(1)
	sim_speed.tooltip_text = "how fast it plays (real speed is how fast the hook moved in the simulation)"
	sim_bar.add_child(sim_speed)
	sim_slider = HSlider.new()
	sim_slider.custom_minimum_size = Vector2(260, 0)
	sim_slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	sim_slider.tooltip_text = "drag to any moment"
	sim_slider.value_changed.connect(func(v: float) -> void:
		if not sim_playing:
			sim_t = v / 60.0
			_show_sim_frame(int(v)))
	sim_bar.add_child(sim_slider)
	sim_bar.add_child(_button("Done", "back to the piece", func() -> void: close_sim()))
	sim_caption = Label.new()
	sim_caption.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	sim_caption.custom_minimum_size = Vector2(420, 0)
	sim_caption.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	sim_bar.add_child(sim_caption)

func start_sim(procedure: String) -> void:
	if busy:
		return
	status_label.text = "Simulating the yarn..."
	await get_tree().process_frame
	if sim_session == null:
		sim_session = YarnSimSession.new()
	sim_frames = sim_session.record(procedure)
	if sim_frames == 0:
		status_label.text = "Nothing to simulate for " + procedure
		return
	if sim_root == null:
		sim_root = Node3D.new()
		work_root.add_child(sim_root)
		sim_yarn = MeshInstance3D.new()
		sim_yarn.material_override = yarn_mat
		sim_root.add_child(sim_yarn)
		sim_hook = MeshInstance3D.new()
		var steel := StandardMaterial3D.new()
		steel.albedo_color = Color(0.74, 0.76, 0.80)
		steel.metallic = 0.6
		steel.roughness = 0.35
		sim_hook.material_override = steel
		sim_root.add_child(sim_hook)
	_sim_hidden.clear()
	for n in [yarn_node, hook_node, left_hand, marker] + ball_nodes + run_nodes + hover_nodes + extra_markers:
		if n != null and n is Node3D and (n as Node3D).visible:
			(n as Node3D).visible = false
			_sim_hidden.append(n)
	sim_root.visible = true
	sim_slider.max_value = sim_frames - 1
	sim_bar.visible = true
	sim_t = 0.0
	_sim_shown = -1
	sim_playing = true
	sim_play.text = "Pause"
	_show_sim_frame(0)
	orbit.target = sim_root.global_position + Vector3(0, -0.004, 0)
	orbit.dist = 0.11
	orbit.apply()
	status_label.text = "Simulated, %d frames: one strand of yarn, a rigid hook, the left hand holding the yarn. Nothing is placed." % sim_frames

func close_sim() -> void:
	sim_playing = false
	if sim_root != null:
		sim_root.visible = false
	sim_bar.visible = false
	for n in _sim_hidden:
		if is_instance_valid(n):
			(n as Node3D).visible = true
	_sim_hidden.clear()
	_refresh()
	_frame_work(true)

func _sim_process(delta: float) -> void:
	if not sim_playing or sim_frames == 0:
		return
	var speeds := [0.25, 0.5, 1.0]
	sim_t += delta * float(speeds[sim_speed.selected])
	var f := int(sim_t * 60.0)
	if f >= sim_frames - 1:
		f = sim_frames - 1
		sim_playing = false
		sim_play.text = "Play"
	_show_sim_frame(f)
	sim_slider.set_value_no_signal(f)

func _sim_mesh(d: Dictionary) -> ArrayMesh:
	var pos: PackedVector3Array = d.get("positions", PackedVector3Array())
	if pos.size() == 0:
		return null
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = pos
	arrays[Mesh.ARRAY_NORMAL] = d["normals"]
	arrays[Mesh.ARRAY_COLOR] = d["colors"]
	arrays[Mesh.ARRAY_INDEX] = d["indices"]
	var m := ArrayMesh.new()
	m.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return m

func _show_sim_frame(f: int) -> void:
	f = clampi(f, 0, sim_frames - 1)
	if f == _sim_shown:
		return
	_sim_shown = f
	sim_yarn.mesh = _sim_mesh(sim_session.get_yarn_mesh(f, 12, 3))
	sim_hook.mesh = _sim_mesh(sim_session.get_hook_mesh(f))
	var info: Dictionary = sim_session.get_frame_info(f)
	var on_hook := absi(int(info.get("loops_on_hook", 0)))
	sim_caption.text = "%s.   %.1f s%s" % [info.get("phase", ""), f / 60.0, ("   (a loop on the hook)" if on_hook == 1 else "")]
