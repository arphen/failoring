# Body mode (docs/BODY_MODE_PLAN.md): the body set up on the mannequin itself, the way a fitting starts. The garment
# goes, the mannequin stands still, and a card on the right of the 3D view has three pages:
#
#   Measure  every measurement as a dot on the body. Click one (or Guided measuring, which goes through them in a
#            tailor's order): the camera goes there, a tailor's tape unrolls round or along the body where that
#            measurement is taken, and the card says how to take it. Type or step the value; Use this saves it to the
#            wearer's measurements file (MeasurementsForm), the mannequin takes it on, the tape's reading lights up,
#            and guided measuring goes on to the next one.
#   Shape    the presets as figures (generated once, drawn from the front), and muscle, bust shape and belly.
#   Look     face, hair, eyes and skin as cards; pointing at one tries it on (the camera is in close, a portrait lens),
#            clicking keeps it. A face needs the body generated again, so it applies on click only.
#
# The tape is the measurement's own geometry: a girth is the convex hull of the body's section there (a tape bridges
# the hollows, as BodyGen.measure does), a length runs along the surface from landmark to landmark.
extends RefCounted

const BodyGen = preload("res://body_gen.gd")
const MeasurementsForm = preload("res://measurements_form.gd")
const Look = preload("res://look.gd")
const Eyes = preload("res://eyes.gd")
const FaceLikeness = preload("res://face.gd")
const CamViews = preload("res://cam_views.gd")
const TAPE_SHADER := preload("res://shaders/tape.gdshader")

# a tailor's order: down the body, the arm, the leg
const ORDER := ["height", "shoulder", "neck", "bust", "waist", "hip", "back", "arm", "upperarm", "thigh", "inseam"]
# camera per measurement: yaw (0 the front, +pi/2 the mannequin's left), pitch, and the size (m) it frames
const VIEW := {
	"height": [1.2, -0.04, 1.95], "shoulder": [PI - 0.35, -0.28, 0.62], "neck": [0.45, -0.12, 0.42],
	"bust": [0.5, -0.1, 0.62], "waist": [0.55, -0.06, 0.62], "hip": [0.65, -0.04, 0.7],
	"back": [PI - 0.5, -0.08, 0.72], "arm": [1.2, -0.06, 0.9], "upperarm": [1.05, -0.1, 0.45],
	"thigh": [0.55, -0.12, 0.55], "inseam": [-0.22, 0.02, 1.0],
}
# what a tailor adds to the instructions (MeasurementsForm has the how-to)
const TIPS := {
	"height": "Heels, seat and shoulder blades against the wall; a book flat on the head marks the top.",
	"shoulder": "Feel for the bony tip at the end of each shoulder; the tape runs across the top of the back.",
	"neck": "One finger under the tape: a collar sits there and must not choke.",
	"bust": "Arms down, tape level all the way round: check it in a mirror at the back.",
	"waist": "Bend sideways: the crease is the natural waist. Do not pull in.",
	"hip": "Over the fullest part of the seat, seen from the side; feet together.",
	"back": "Chin down a little to find the knob at the base of the neck, down the spine to the waist.",
	"arm": "Hand on the hip so the elbow bends a little: over the elbow to the wrist bone.",
	"upperarm": "Arm relaxed at the side, at the fullest part between shoulder and elbow.",
	"thigh": "Standing, feet apart a little, at the very top of the leg.",
	"inseam": "Best from a pair of trousers that fit: crotch seam to hem, along the inside.",
}
const TAPE_W := 0.016          # m, the tape's width
const LIFT := 0.004            # m the tape lies off the skin
const OVERLAP := 0.04          # m a girth's tape runs past its start (the reading is where the ends meet)
const PANEL_W := 330.0
const DOT_R := 7.0

var ed                         # the editor (editor.gd)
var active := false
var page := "measure"
var focus_key := ""            # the measurement on the card and the tape, "" none
var guided := false
var paths := {}                # key -> {pts, side: PackedVector3Array, loop: bool, length, anchor, normal, centre: Vector3, read: Vector3}
var mine := {}                 # measurements the wearer entered (their file), key -> true
var _hover_dot := ""
var _wrap_t := -1.0            # seconds into the tape's unrolling, -1 when done
var _wrap_dur := 1.0
var _glow_t := -1.0            # seconds into the reading's glow, -1 when none
var _next_at := -1.0           # guided: seconds left before the next measurement comes up, -1 none
var _look_saved := {}          # a look tried on by pointing: what to go back to
var _sil := {}                 # preset -> ImageTexture (the figure), filled by worker tasks
var _sil_tasks := {}           # preset -> {task, out: Array}

var tape: MeshInstance3D
var tape_mat: ShaderMaterial
var reading: Label3D
var panel: PanelContainer
var overlay: Control
var _pages := {}               # name -> Control
var _tab_btns := {}
var _list: VBoxContainer
var _card: VBoxContainer
var _progress: ProgressBar
var _progress_l: Label
var _spin: SpinBox
var _value_note: Label
var _use_btn: Button
var _guided_btn: Button
var _shape_cards := {}


func setup(editor) -> void:
	ed = editor
	tape = MeshInstance3D.new()
	tape.name = "BodyTape"
	tape_mat = ShaderMaterial.new()
	tape_mat.shader = TAPE_SHADER
	tape.material_override = tape_mat
	tape.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	tape.visible = false
	ed.stage.add_child(tape)
	reading = Label3D.new()
	reading.name = "BodyTapeReading"
	reading.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	reading.no_depth_test = true
	reading.fixed_size = true
	reading.pixel_size = 0.0006
	reading.font_size = 26
	reading.outline_size = 9
	reading.outline_modulate = Color(0.05, 0.04, 0.03, 0.85)
	reading.render_priority = 20
	reading.outline_render_priority = 19
	reading.visible = false
	reading.layers = 4        # layer 3: the mirror's camera leaves it out (mirror.gd), it read backwards in the glass
	ed.stage.add_child(reading)


# ------------------------------------------------------------------ UI
func build_ui(pane: Control) -> void:
	overlay = Control.new()
	overlay.name = "BodyOverlay"
	overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
	overlay.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	overlay.draw.connect(_draw_overlay)
	overlay.visible = false
	pane.add_child(overlay)
	panel = PanelContainer.new()
	panel.name = "BodyPanel"
	panel.visible = false
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.07, 0.08, 0.1, 0.9)
	sb.set_corner_radius_all(12)
	sb.border_color = Color(1, 1, 1, 0.08)
	sb.set_border_width_all(1)
	sb.content_margin_left = 14
	sb.content_margin_right = 14
	sb.content_margin_top = 12
	sb.content_margin_bottom = 12
	panel.add_theme_stylebox_override("panel", sb)
	panel.anchor_left = 1.0
	panel.anchor_right = 1.0
	panel.anchor_top = 0.0
	panel.anchor_bottom = 1.0
	panel.offset_left = -PANEL_W - 10.0
	panel.offset_right = -10.0
	panel.offset_top = 48.0
	panel.offset_bottom = -58.0
	pane.add_child(panel)
	var vb := VBoxContainer.new()
	vb.add_theme_constant_override("separation", 10)
	panel.add_child(vb)
	var head := HBoxContainer.new()
	var title := Label.new()
	title.text = "Body"
	title.add_theme_font_size_override("font_size", 20)
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(title)
	var done := Button.new()
	done.text = "Done"
	done.focus_mode = Control.FOCUS_NONE
	done.tooltip_text = "Leave body mode (B or Esc): the garment comes back"
	done.pressed.connect(func() -> void: ed._set_body_mode(false))
	head.add_child(done)
	vb.add_child(head)
	var tabs := HBoxContainer.new()
	tabs.add_theme_constant_override("separation", 4)
	var group := ButtonGroup.new()
	for spec in [["measure", "Measure"], ["shape", "Shape"], ["look", "Look"]]:
		var b := Button.new()
		b.name = "BodyTab_" + String(spec[0])
		b.text = spec[1]
		b.toggle_mode = true
		b.button_group = group
		b.focus_mode = Control.FOCUS_NONE
		b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		b.pressed.connect(func() -> void: set_page(String(spec[0])))
		tabs.add_child(b)
		_tab_btns[spec[0]] = b
	vb.add_child(tabs)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vb.add_child(scroll)
	var stack := VBoxContainer.new()
	stack.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(stack)
	_pages["measure"] = _build_measure_page()
	_pages["shape"] = _build_shape_page()
	_pages["look"] = _build_look_page()
	for k in _pages:
		stack.add_child(_pages[k])


func _small(text: String, alpha := 0.7) -> Label:
	var l := Label.new()
	l.text = text
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.add_theme_font_size_override("font_size", 12)
	l.modulate = Color(1, 1, 1, alpha)
	return l


func _section(text: String) -> Label:
	var l := Label.new()
	l.text = text.to_upper()
	l.add_theme_font_size_override("font_size", 11)
	l.modulate = Color(1, 1, 1, 0.5)
	return l


func _build_measure_page() -> Control:
	var vb := VBoxContainer.new()
	vb.name = "BodyMeasure"
	vb.add_theme_constant_override("separation", 8)
	_progress_l = Label.new()
	_progress_l.add_theme_font_size_override("font_size", 13)
	vb.add_child(_progress_l)
	_progress = ProgressBar.new()
	_progress.show_percentage = false
	_progress.custom_minimum_size.y = 6
	_progress.max_value = ORDER.size()
	vb.add_child(_progress)
	_guided_btn = Button.new()
	_guided_btn.name = "BodyGuided"
	_guided_btn.focus_mode = Control.FOCUS_NONE
	_guided_btn.tooltip_text = "Go through the measurements one by one, in a tailor's order, starting with the first one you have not taken"
	_guided_btn.pressed.connect(func() -> void: start_guided())
	vb.add_child(_guided_btn)
	_card = VBoxContainer.new()
	_card.name = "BodyCard"
	_card.add_theme_constant_override("separation", 6)
	vb.add_child(_card)
	vb.add_child(_section("All measurements"))
	_list = VBoxContainer.new()
	_list.add_theme_constant_override("separation", 2)
	vb.add_child(_list)
	vb.add_child(_small("Measure over light underwear with a soft tape, snug but not tight. Saved to %s." % MeasurementsForm.csv_path(), 0.5))
	return vb


func _spec(key: String) -> Array:
	for s in MeasurementsForm.ESSENTIAL + MeasurementsForm.OPTIONAL:
		if String(s[0]) == key:
			return s
	return [key, key.capitalize(), 0.0, 300.0, ""]


# the value on the mannequin now (cm): the wearer's, the preset's, or the estimate of an optional one
func value_cm(key: String) -> float:
	var v = ed.meas.get(key, null)
	if v == null and ed.body_stand != null:
		v = (ed.body_stand["m"] as Dictionary).get(key, null)
	return float(v) * 100.0 if v != null else 0.0


func _refresh_measure() -> void:
	if _list == null:
		return
	var n := 0
	for k in ORDER:
		if mine.has(k):
			n += 1
	_progress.value = n
	_progress_l.text = "%d of %d measured by you" % [n, ORDER.size()] if n < ORDER.size() else "All %d measurements are yours" % ORDER.size()
	_guided_btn.text = "Stop guided measuring" if guided else ("Guided measuring" if n == 0 else "Guided measuring: go on")
	for c in _list.get_children():
		c.queue_free()
	for k in ORDER:
		var row := Button.new()
		row.name = "BodyRow_" + k
		row.flat = focus_key != k
		row.focus_mode = Control.FOCUS_NONE
		row.alignment = HORIZONTAL_ALIGNMENT_LEFT
		var tag := "●  " if mine.has(k) else "○  "
		var est: bool = ed.meas.get(k, null) == null
		row.text = "%s%s" % [tag, String(_spec(k)[1])]
		row.add_theme_color_override("font_color", Color("#7fd6a0") if mine.has(k) else Color(1, 1, 1, 0.85))
		row.tooltip_text = "Yours" if mine.has(k) else ("Estimated from the others" if est else "From the preset")
		var val := Label.new()
		val.text = "%.1f cm%s" % [value_cm(k), "  est." if est else ""]
		val.add_theme_font_size_override("font_size", 12)
		val.modulate = Color(1, 1, 1, 0.7)
		val.set_anchors_and_offsets_preset(Control.PRESET_CENTER_RIGHT)
		val.offset_left = -110
		val.offset_right = -8
		val.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		val.mouse_filter = Control.MOUSE_FILTER_IGNORE
		row.add_child(val)
		row.pressed.connect(func() -> void: focus(k))
		_list.add_child(row)
	_build_card()


func _build_card() -> void:
	for c in _card.get_children():
		c.queue_free()
	_spin = null
	if focus_key == "":
		_card.add_child(_small("Click a dot on the body, or a measurement below, to see where and how it is taken."))
		return
	var spec := _spec(focus_key)
	var bg := PanelContainer.new()
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(1, 1, 1, 0.05)
	sb.set_corner_radius_all(10)
	sb.content_margin_left = 12
	sb.content_margin_right = 12
	sb.content_margin_top = 10
	sb.content_margin_bottom = 12
	bg.add_theme_stylebox_override("panel", sb)
	_card.add_child(bg)
	var vb := VBoxContainer.new()
	vb.add_theme_constant_override("separation", 7)
	bg.add_child(vb)
	var step := Label.new()
	step.text = ("Step %d of %d" % [ORDER.find(focus_key) + 1, ORDER.size()]) if guided else ("Girth" if bool(paths.get(focus_key, {}).get("loop", false)) else "Length")
	step.add_theme_font_size_override("font_size", 11)
	step.modulate = Color(1.0, 0.75, 0.35, 0.9)
	vb.add_child(step)
	var t := Label.new()
	t.text = String(spec[1])
	t.add_theme_font_size_override("font_size", 22)
	vb.add_child(t)
	var how := _small(String(spec[4]), 0.9)
	how.add_theme_font_size_override("font_size", 13)
	vb.add_child(how)
	if TIPS.has(focus_key):
		vb.add_child(_small("Tailor's tip: " + String(TIPS[focus_key]), 0.6))
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 6)
	var minus := Button.new()
	minus.text = "−"
	minus.custom_minimum_size = Vector2(36, 36)
	minus.focus_mode = Control.FOCUS_NONE
	row.add_child(minus)
	_spin = SpinBox.new()
	_spin.name = "BodyValue"
	_spin.min_value = float(spec[2])
	_spin.max_value = float(spec[3])
	_spin.step = 0.1
	_spin.suffix = "cm"
	_spin.select_all_on_focus = true
	_spin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_spin.get_line_edit().add_theme_font_size_override("font_size", 20)
	_spin.get_line_edit().alignment = HORIZONTAL_ALIGNMENT_CENTER
	_spin.set_value_no_signal(snappedf(value_cm(focus_key), 0.1))
	row.add_child(_spin)
	var plus := Button.new()
	plus.text = "+"
	plus.custom_minimum_size = Vector2(36, 36)
	plus.focus_mode = Control.FOCUS_NONE
	row.add_child(plus)
	minus.pressed.connect(func() -> void: _spin.value -= 0.5)
	plus.pressed.connect(func() -> void: _spin.value += 0.5)
	_spin.get_line_edit().text_submitted.connect(func(_t: String) -> void:
		_spin.apply()
		confirm())
	vb.add_child(row)
	_value_note = _small("", 0.6)
	vb.add_child(_value_note)
	_sync_value_note()
	var btns := HBoxContainer.new()
	btns.add_theme_constant_override("separation", 6)
	_use_btn = Button.new()
	_use_btn.name = "BodyUse"
	_use_btn.text = "Use this  ↵"
	_use_btn.focus_mode = Control.FOCUS_NONE
	_use_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_use_btn.tooltip_text = "Put this value on the mannequin and save it to your measurements (Enter)"
	_use_btn.pressed.connect(confirm)
	btns.add_child(_use_btn)
	if MeasurementsForm.OPTIONAL.any(func(s: Array) -> bool: return String(s[0]) == focus_key):
		var est := Button.new()
		est.text = "Estimate"
		est.focus_mode = Control.FOCUS_NONE
		est.tooltip_text = "Leave it to the other measurements (not measured)"
		est.pressed.connect(_estimate)
		btns.add_child(est)
	if guided:
		var skip := Button.new()
		skip.text = "Skip"
		skip.focus_mode = Control.FOCUS_NONE
		skip.pressed.connect(func() -> void: _advance())
		btns.add_child(skip)
	vb.add_child(btns)
	_spin.value_changed.connect(func(_v: float) -> void: _sync_value_note())


func _sync_value_note() -> void:
	if _value_note == null or _spin == null or focus_key == "":
		return
	var now := value_cm(focus_key)
	var d := _spin.value - now
	if mine.has(focus_key) and absf(d) < 0.05:
		_value_note.text = "Yours, on the mannequin."
	elif absf(d) < 0.05:
		_value_note.text = "The mannequin measures %.1f cm here." % now
	else:
		_value_note.text = "The mannequin measures %.1f cm: %s%.1f cm." % [now, "+" if d > 0.0 else "", d]


func _build_shape_page() -> Control:
	var vb := VBoxContainer.new()
	vb.name = "BodyShape"
	vb.add_theme_constant_override("separation", 8)
	vb.add_child(_section("Start from a figure"))
	var grid := GridContainer.new()
	grid.columns = 3
	grid.add_theme_constant_override("h_separation", 6)
	grid.add_theme_constant_override("v_separation", 6)
	vb.add_child(grid)
	for p in ["petite", "average", "tall", "curvy", "athletic", "plus"]:
		var b := Button.new()
		b.name = "BodyPreset_" + p
		b.custom_minimum_size = Vector2(92, 150)
		b.focus_mode = Control.FOCUS_NONE
		b.tooltip_text = _preset_tip(p)
		b.toggle_mode = true
		b.pressed.connect(func() -> void: _use_preset(p))
		var fig := Control.new()
		fig.mouse_filter = Control.MOUSE_FILTER_IGNORE
		fig.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		fig.draw.connect(func() -> void: _draw_figure(fig, p))
		b.add_child(fig)
		grid.add_child(b)
		_shape_cards[p] = {"btn": b, "fig": fig}
	vb.add_child(_small("A figure replaces the measurements on the mannequin; your saved ones stay in your file (Measure > Use this brings each back).", 0.5))
	vb.add_child(_section("Body type"))
	for spec in MeasurementsForm.SHAPE:
		var key: String = spec[0]
		var row := VBoxContainer.new()
		var top := HBoxContainer.new()
		var l := Label.new()
		l.text = String(spec[1])
		l.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		top.add_child(l)
		var o := Label.new()
		o.add_theme_font_size_override("font_size", 12)
		o.modulate = Color(1, 1, 1, 0.7)
		top.add_child(o)
		row.add_child(top)
		var s := HSlider.new()
		s.name = "BodyShape_" + key
		s.min_value = float(spec[2])
		s.max_value = float(spec[3])
		s.step = 1.0
		s.focus_mode = Control.FOCUS_NONE
		s.tooltip_text = String(spec[4])
		var def := 50.0 if key == "muscle" else 0.0
		s.set_value_no_signal(float(ed.meas.get(key, def / 100.0)) * 100.0)
		o.text = "%d%%" % int(s.value) if key == "muscle" else ("%+d" % int(s.value))
		s.value_changed.connect(func(v: float) -> void:
			ed.meas[key] = v / 100.0
			o.text = "%d%%" % int(v) if key == "muscle" else ("%+d" % int(v))
			ed._schedule())
		row.add_child(s)
		vb.add_child(row)
	return vb


func _preset_tip(p: String) -> String:
	var m: Dictionary = BodyGen.PRESETS[p]
	return "%s: height %.0f, bust %.0f, waist %.0f, hip %.0f cm" % [p.capitalize(), float(m["height"]) * 100.0, float(m["bust"]) * 100.0, float(m["waist"]) * 100.0, float(m["hip"]) * 100.0]


func _use_preset(p: String) -> void:
	ed._on_preset_button(p)
	mine.clear()
	_sync_shape()


func _sync_shape() -> void:
	var cur := ""
	if not ed._meas_mine:
		for p in BodyGen.PRESETS:
			var ok := true
			for k in ["height", "bust", "waist", "hip"]:
				if absf(float(BodyGen.PRESETS[p][k]) - float(ed.meas.get(k, 0.0))) > 1e-4:
					ok = false
			if ok:
				cur = p
	for p in _shape_cards:
		(_shape_cards[p]["btn"] as Button).set_pressed_no_signal(p == cur)


# The preset's figure from the front: its generated body's vertices splatted into a small image, generated on a worker.
func _figure_tex(p: String) -> Texture2D:
	if _sil.has(p):
		return _sil[p]
	if not _sil_tasks.has(p):
		var out := []
		var m: Dictionary = ed.M
		var req: Dictionary = (BodyGen.PRESETS[p] as Dictionary).duplicate()
		var id := WorkerThreadPool.add_task(func() -> void: out.append(_figure_image(m, req)))
		_sil_tasks[p] = {"task": id, "out": out}
	return null


static func _figure_image(m: Dictionary, req: Dictionary) -> Image:
	var b := BodyGen.generate(m, req, null)
	var pos: PackedFloat32Array = b["pos"]
	const W := 64
	const H := 128
	const SCALE := 64.0        # px per metre: every figure at the same scale, so they compare
	var img := Image.create(W, H, false, Image.FORMAT_LA8)
	for i in pos.size() / 3:
		var x := int(W * 0.5 + pos[3 * i] * SCALE)
		var y := int(H - 4 - pos[3 * i + 1] * SCALE)
		for dy in 2:
			for dx in 2:
				var px := x + dx
				var py := y + dy
				if px >= 0 and px < W and py >= 0 and py < H:
					img.set_pixel(px, py, Color(1, 1, 1, 1))
	return img


func _poll_figures() -> void:
	for p in _sil_tasks.keys():
		var t: Dictionary = _sil_tasks[p]
		if WorkerThreadPool.is_task_completed(int(t["task"])):
			WorkerThreadPool.wait_for_task_completion(int(t["task"]))
			if not (t["out"] as Array).is_empty():
				_sil[p] = ImageTexture.create_from_image(t["out"][0])
			_sil_tasks.erase(p)
			if _shape_cards.has(p):
				(_shape_cards[p]["fig"] as Control).queue_redraw()


func _draw_figure(fig: Control, p: String) -> void:
	var tex := _figure_tex(p)
	var r := Rect2(Vector2(4, 4), fig.size - Vector2(8, 26))
	if tex != null:
		var sc := minf(r.size.x / tex.get_width(), r.size.y / tex.get_height())
		var sz := Vector2(tex.get_width(), tex.get_height()) * sc
		fig.draw_texture_rect(tex, Rect2(r.position + (r.size - sz) * 0.5, sz), false, Color(0.92, 0.82, 0.72, 0.9))
	else:
		fig.draw_string(fig.get_theme_default_font(), r.position + Vector2(0, r.size.y * 0.5), "…", HORIZONTAL_ALIGNMENT_CENTER, r.size.x, 14, Color(1, 1, 1, 0.4))
	fig.draw_string(fig.get_theme_default_font(), Vector2(0, fig.size.y - 8), p.capitalize(), HORIZONTAL_ALIGNMENT_CENTER, fig.size.x, 12, Color(1, 1, 1, 0.85))


func _build_look_page() -> Control:
	var vb := VBoxContainer.new()
	vb.name = "BodyLook"
	vb.add_theme_constant_override("separation", 8)
	return vb


# Rebuilt on every visit: the faces folder can change while the editor runs.
func _refresh_look() -> void:
	var vb: VBoxContainer = _pages["look"]
	for c in vb.get_children():
		c.queue_free()
	vb.add_child(_small("Point at a choice to try it on; click to keep it.", 0.6))
	var faces := [["", "Mannequin", null]]
	for id in FaceLikeness.available():
		var inf := FaceLikeness.info(id)
		faces.append([id, String(inf.get("label", "Mine" if id == "me" else String(id).capitalize())), _face_thumb(id)])
	if faces.size() > 1:
		vb.add_child(_section("Face"))
		var g := GridContainer.new()
		g.columns = 3
		g.add_theme_constant_override("h_separation", 6)
		g.add_theme_constant_override("v_separation", 6)
		for f in faces:
			var b := _card_button("BodyFace_" + String(f[0]), String(f[1]), ed.look_face == String(f[0]))
			b.custom_minimum_size = Vector2(92, 104)
			var th = f[2]
			if th != null:
				var tr := TextureRect.new()
				tr.texture = th
				tr.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
				tr.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
				tr.mouse_filter = Control.MOUSE_FILTER_IGNORE
				tr.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
				tr.offset_left = 6
				tr.offset_right = -6
				tr.offset_top = 6
				tr.offset_bottom = -26
				b.add_child(tr)
			var id: String = f[0]
			b.pressed.connect(func() -> void: _keep_look("face", id))
			g.add_child(b)
		vb.add_child(g)
		vb.add_child(_small("A face changes the head's shape: it is put on when clicked.", 0.45))
	vb.add_child(_section("Hair"))
	var hairs := Look.HAIR_COLORS.duplicate()
	if ed.look_face != "":
		hairs["mine"] = {"label": "Long (mine)", "col": _face_hair_col()}
	vb.add_child(_swatches("hair", hairs, ed.look_hair))
	vb.add_child(_section("Eyes"))
	var eyes := {}
	for k in Eyes.IRIS.keys():
		eyes[k] = {"label": String(k).capitalize(), "col": (Eyes.IRIS[k] as Array)[1]}
	if ed.look_face != "":
		var iris = FaceLikeness.info(ed.look_face).get("iris", null)
		eyes["mine"] = {"label": "Mine", "col": Color(iris[0], iris[1], iris[2]).lightened(0.25) if iris is Array else Color.GRAY}
	vb.add_child(_swatches("eyes", eyes, ed.look_eyes))
	vb.add_child(_section("Skin"))
	var skins := {}
	var choices: Dictionary = Look.skin_choices()
	for k in choices:
		var c: Dictionary = choices[k]
		skins[k] = {"label": String(c.get("label", k)), "col": c.get("tone", Color(0.8, 0.6, 0.5))}
	vb.add_child(_swatches("skin", skins, ed.look_skin))


func _face_hair_col() -> Color:
	var hc = FaceLikeness.info(ed.look_face).get("hair", null)
	return Color(hc[0], hc[1], hc[2]) if hc is Array else Color(0.31, 0.24, 0.18)


# A face's picture: the front photo its likeness was fitted to (work/best_front.png of the face, or of the face it was made
# from), cropped round the face.
func _face_thumb(id: String) -> Texture2D:
	var src := id
	var inf := FaceLikeness.info(id)
	for _i in 3:
		var p := FaceLikeness.path_of(src, "work/best_front.png")
		if FileAccess.file_exists(p):
			var img := Image.load_from_file(p)
			if img != null and not img.is_empty():
				var s := mini(img.get_width(), img.get_height())
				img = img.get_region(Rect2i((img.get_width() - s) / 2, (img.get_height() - s) / 2, s, s))
				img.resize(128, 128, Image.INTERPOLATE_LANCZOS)
				return ImageTexture.create_from_image(img)
		if not inf.has("from"):
			break
		src = String(inf["from"])
		inf = FaceLikeness.info(src)
	return null


func _card_button(nm: String, text: String, on: bool) -> Button:
	var b := Button.new()
	b.name = nm
	b.toggle_mode = true
	b.button_pressed = on
	b.focus_mode = Control.FOCUS_NONE
	b.tooltip_text = text
	b.custom_minimum_size = Vector2(92, 40)
	# the name at the bottom, under the picture
	var l := Label.new()
	l.text = text
	l.clip_text = true
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	l.add_theme_font_size_override("font_size", 11)
	l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	l.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_WIDE)
	l.offset_top = -22
	l.offset_bottom = -4
	l.offset_left = 4
	l.offset_right = -4
	b.add_child(l)
	return b


# Round colour chips with the name under them; pointing tries the look on, clicking keeps it.
func _swatches(key: String, items: Dictionary, current: String) -> Control:
	var flow := HFlowContainer.new()
	flow.add_theme_constant_override("h_separation", 6)
	flow.add_theme_constant_override("v_separation", 6)
	for k in items:
		var it: Dictionary = items[k]
		var b := Button.new()
		b.name = "BodyLook_%s_%s" % [key, k]
		b.toggle_mode = true
		b.button_pressed = String(k) == current
		b.focus_mode = Control.FOCUS_NONE
		b.custom_minimum_size = Vector2(64, 62)
		b.tooltip_text = String(it.get("label", k))
		var col: Color = it.get("col", Color.GRAY)
		var lbl := String(it.get("label", k)).replace(" crop", "")
		var chip := Control.new()
		chip.mouse_filter = Control.MOUSE_FILTER_IGNORE
		chip.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		chip.draw.connect(func() -> void:
			var c := Vector2(chip.size.x * 0.5, 22)
			chip.draw_circle(c, 15, col)
			chip.draw_arc(c, 15, 0, TAU, 32, Color(1, 1, 1, 0.9 if b.button_pressed else 0.15), 2.0 if b.button_pressed else 1.0, true)
			chip.draw_string(chip.get_theme_default_font(), Vector2(0, chip.size.y - 7), lbl, HORIZONTAL_ALIGNMENT_CENTER, chip.size.x, 11, Color(1, 1, 1, 0.8)))
		b.add_child(chip)
		b.toggled.connect(func(_on: bool) -> void: chip.queue_redraw())
		var kk := String(k)
		b.mouse_entered.connect(func() -> void: _try_look(key, kk))
		b.mouse_exited.connect(func() -> void: _untry_look())
		b.pressed.connect(func() -> void: _keep_look(key, kk))
		flow.add_child(b)
	return flow


func _look_var(key: String) -> String:
	return "look_" + key


func _try_look(key: String, value: String) -> void:
	if _look_saved.is_empty():
		_look_saved = {"key": key, "value": String(ed.get(_look_var(key)))}
	ed.set(_look_var(key), value)
	ed._apply_look()


func _untry_look() -> void:
	if _look_saved.is_empty():
		return
	ed.set(_look_var(String(_look_saved["key"])), String(_look_saved["value"]))
	_look_saved = {}
	ed._apply_look()


func _keep_look(key: String, value: String) -> void:
	_look_saved = {}
	ed._set_look(key, value)
	_refresh_look.call_deferred()


func set_page(p: String) -> void:
	page = p
	for k in _pages:
		(_pages[k] as Control).visible = k == p
	for k in _tab_btns:
		(_tab_btns[k] as Button).set_pressed_no_signal(k == p)
	if p == "look":
		_refresh_look()
		_portrait()
		_show_tape(false)
	elif p == "shape":
		_sync_shape()
		for q in _shape_cards:
			(_shape_cards[q]["fig"] as Control).queue_redraw()
		_overview()
		_show_tape(false)
	else:
		_refresh_measure()
		if focus_key != "":
			focus(focus_key)
		else:
			_overview()
	overlay.queue_redraw()


# ------------------------------------------------------------------ mode
func enter() -> void:
	active = true
	ed.layout.maximize()      # the whole window for the mannequin, as render mode
	_load_mine()
	_compute_paths()
	panel.visible = true
	overlay.visible = true
	guided = false
	focus_key = ""
	set_page("measure")


func leave() -> void:
	_untry_look()
	active = false
	guided = false
	focus_key = ""
	panel.visible = false
	overlay.visible = false
	_show_tape(false)
	ed.cam.h_offset = 0.0
	ed.layout.restore()
	ed._cam_goto("fit")


func _load_mine() -> void:
	mine.clear()
	if ed._meas_mine:
		for k in MeasurementsForm.load_csv():
			mine[k] = true


# The body changed (a measurement, a preset, a face): the tapes follow it.
func on_body_changed() -> void:
	if not active:
		return
	_compute_paths()
	if focus_key != "" and page == "measure":
		_build_tape(focus_key, false)   # an unrolling in progress goes on over the new shape
	_refresh_measure()
	_sync_shape()


func start_guided() -> void:
	if guided:
		guided = false
		_refresh_measure()
		return
	guided = true
	var k := ""
	for q in ORDER:
		if not mine.has(q):
			k = q
			break
	focus(k if k != "" else ORDER[0])


func _advance() -> void:
	var i := ORDER.find(focus_key)
	for j in range(1, ORDER.size() + 1):
		var q: String = ORDER[(i + j) % ORDER.size()]
		if not mine.has(q):
			focus(q)
			return
	guided = false
	focus("")
	ed.notify("All measurements are yours. Garments are drafted on this body.")


func focus(key: String) -> void:
	_next_at = -1.0
	focus_key = key
	if page != "measure":
		set_page("measure")
		return
	_refresh_measure()
	if key == "":
		_show_tape(false)
		_overview()
		return
	_build_tape(key, true)
	_aim(key)
	if _spin != null:
		_spin.get_line_edit().grab_focus.call_deferred()


func confirm() -> void:
	if focus_key == "" or _spin == null:
		return
	var key := focus_key
	var v := _spin.value / 100.0
	var saved := MeasurementsForm.load_csv()
	saved[key] = v
	var e := MeasurementsForm.save_csv(saved)
	if e != OK:
		ed.notify("Could not save to %s (error %d); the value is used anyway." % [MeasurementsForm.csv_path(), e])
	ed.meas[key] = v
	ed._meas_mine = true
	mine[key] = true
	ed._schedule()
	_glow_t = 0.0
	_refresh_measure()
	if guided:
		_next_at = 1.3


func _estimate() -> void:
	if focus_key == "":
		return
	var saved := MeasurementsForm.load_csv()
	saved.erase(focus_key)
	MeasurementsForm.save_csv(saved)
	ed.meas[focus_key] = null
	mine.erase(focus_key)
	ed._schedule()
	_refresh_measure()


# ------------------------------------------------------------------ camera
func _cam(pivot: Vector3, size: float, yaw: float, pitch: float, fov: float) -> void:
	var asp: float = (overlay.size.x - PANEL_W - 20.0) / maxf(1.0, overlay.size.y)
	ed.cam_custom = {"p": pivot, "d": CamViews.fit(size, size * 0.8, maxf(0.4, asp), fov)}
	ed.cam_focus = "custom"
	ed._fov_goal = fov
	ed._cam_goal = {"yaw": ed.yaw + wrapf(yaw - ed.yaw, -PI, PI), "pitch": pitch, "zoom": 1.0}
	ed.user_cam_until = Time.get_ticks_msec() + 60000


func _overview() -> void:
	var h := float(ed.meas.get("height", 1.68))
	_cam(Vector3(0.0, h * 0.5, 0.0), h * 1.06, 0.3, -0.06, Look.CAM_FOV)


func _portrait() -> void:
	var h := float(ed.meas.get("height", 1.68))
	# a portrait lens from 1.6 m or so: the face as people see it across a table, not a selfie's
	_cam(Vector3(0.0, h * 0.915, 0.02), 0.4, 0.3, -0.03, 15.0)


func _aim(key: String) -> void:
	var v: Array = VIEW[key]
	var c: Vector3 = paths[key]["centre"] if paths.has(key) else Vector3(0.0, 1.0, 0.0)
	_cam(c, float(v[2]), float(v[0]), float(v[1]), Look.CAM_FOV)


# ------------------------------------------------------------------ tape geometry
func _compute_paths() -> void:
	paths.clear()
	if ed.body_stand == null or ed.M.is_empty():
		return
	var st: Dictionary = ed.body_stand
	var m: Dictionary = ed.M
	var ap: PackedFloat32Array = st["allPos"]
	var lm: Dictionary = m["lm"]
	var sets: Dictionary = m["sets"]
	var nb: int = m["nb"]
	var y0 := 1e9
	var y1 := -1e9
	var zmin := 1e9
	for i in nb:
		y0 = minf(y0, ap[3 * i + 1])
		y1 = maxf(y1, ap[3 * i + 1])
		zmin = minf(zmin, ap[3 * i + 2])
	var up := Vector3.UP
	var ex := Vector3.RIGHT
	var ez := Vector3.BACK
	var yb := 0.5 * (_v(ap, int(lm["bust.L"])).y + _v(ap, int(lm["bust.R"])).y)
	paths["bust"] = _loop_path(_hull_loop(m, ap, sets["trunk"], up, yb, ex, ez, null, 0.0, Callable()), up)
	paths["waist"] = _loop_path(_hull_loop(m, ap, sets["trunk"], up, _v(ap, int(lm["waist"])).y, ex, ez, null, 0.0, Callable()), up)
	paths["hip"] = _loop_path(_hull_loop(m, ap, sets["hip"], up, _v(ap, int(lm["hip"])).y, ex, ez, null, 0.0, Callable()), up)
	var yc := _v(ap, int(lm["crotch"])).y
	paths["thigh"] = _loop_path(_hull_loop(m, ap, sets["hip"], up, yc - 0.025, ex, ez, null, 0.0, func(x: float, _z: float) -> bool: return x > 0.004), up)
	# neck: tilted with the neck, from the base at the back to the hollow at the front (BodyGen.measure)
	var c := _v(ap, int(lm["cervicale"]))
	var jg := _v(ap, int(lm["jugular"]))
	var nn := (jg - c).cross(ex).normalized()
	if nn.y < 0.0:
		nn = -nn
	var mid := (c + jg) * 0.5 + nn * 0.015
	paths["neck"] = _loop_path(_hull_loop(m, ap, sets["neck"], nn, nn.dot(mid), ex, nn.cross(ex), mid, 0.08, Callable()), nn)
	# upper arm: across the arm half way from the shoulder to the elbow
	var jt: Dictionary = st["joints"]       # the posed joints (the rig's joint vertices are not posed in allPos)
	var js := _arr3(jt["l-shoulder"])
	var je := _arr3(jt["l-elbow"])
	var jw := _arr3(jt["l-hand"])
	var ax := (je - js).normalized()
	var q := (js + je) * 0.5
	var e1 := ax.cross(ez).normalized()
	paths["upperarm"] = _loop_path(_hull_loop(m, ap, sets["armL"], ax, ax.dot(q), e1, ax.cross(e1), q, 0.09, Callable()), ax)
	# height: against the wall behind the heels, floor to the top of the head, read at the top
	var zw := zmin - 0.05
	paths["height"] = _line_path(PackedVector3Array([Vector3(0.0, y0, zw), Vector3(0.0, y1, zw)]), Vector3.BACK, Vector3.RIGHT)
	# its dot on the crown, where the book lies
	var crown := Vector3.ZERO
	var nc := 0
	for i in nb:
		if ap[3 * i + 1] > y1 - 0.01:
			crown += _v(ap, i)
			nc += 1
	paths["height"]["anchor"] = crown / float(maxi(nc, 1)) + Vector3(0.0, 0.01, 0.0)
	paths["height"]["normal"] = Vector3(0.3, 1.0, 0.3).normalized()
	# shoulder: tip to tip across the top of the back
	var aR := _v(ap, int(lm["acromion.R"]))
	var aL := _v(ap, int(lm["acromion.L"]))
	var sh := PackedVector3Array()
	for i in 25:
		var t := float(i) / 24.0
		var s := aR.lerp(aL, t)
		var z := _surface_z(ap, nb, s, -1.0)
		sh.append(Vector3(s.x, s.y + 0.004, z - LIFT))
	paths["shoulder"] = _line_path(sh, Vector3.UP, Vector3(0.0, 0.5, -1.0).normalized())
	# back length: down the spine from the base of the neck to the waist
	var chain: Array = lm["backMidline"]
	var yw := _v(ap, int(lm["waist"])).y
	var bk := []
	for vi in chain:
		var p := _v(ap, int(vi))
		if p.y <= c.y + 1e-4 and p.y >= yw - 1e-4:
			bk.append(p + Vector3(0.0, 0.0, -LIFT))
	bk.sort_custom(func(a: Vector3, b: Vector3) -> bool: return a.y > b.y)
	paths["back"] = _line_path(PackedVector3Array(bk), Vector3.RIGHT, Vector3(0.0, 0.0, -1.0))
	# arm: shoulder tip over the elbow to the wrist, on the outside of the arm
	var arm := PackedVector3Array()
	var armset: PackedInt32Array = sets["armL"]
	var tris: PackedInt32Array = m["tris"]
	var arm_v := {}
	for t in armset:
		for k in 3:
			arm_v[tris[3 * t + k]] = true
	var joints := [aL, je, jw]
	for seg in 2:
		var a: Vector3 = joints[seg] if seg > 0 else js
		var b: Vector3 = joints[seg + 1]
		var axis := (b - a).normalized()
		var o := -(axis.cross(ez)).normalized()
		for i in 12:
			var s := a.lerp(b, float(i) / 12.0)
			arm.append(_surface_out(ap, arm_v, s, axis, o))
	arm.append(_surface_out(ap, arm_v, jw, (jw - je).normalized(), -((jw - je).normalized().cross(ez)).normalized()))
	arm[0] = aL + Vector3(LIFT, LIFT, 0.0)
	paths["arm"] = _line_path(arm, Vector3.BACK, Vector3(1.0, 0.0, 0.2).normalized())
	# inseam: down the inside of the left leg from the crotch to the floor
	var secs: Dictionary = st["sections"]
	var legs: Array = (secs["legs"] as Dictionary)["L"]
	var dy: float = secs["dy"]
	var ins := PackedVector3Array()
	var kc := int(floor(yc / dy))
	var k := kc
	while k >= 0:
		var h = legs[k] if k < legs.size() else null
		if h is Array and (h as Array).size() >= 6:
			# the inside of the leg: its smallest x, at the middle of the leg front to back (the hull's corner hopped about)
			var hx := 1e9
			var z0 := 1e9
			var z1 := -1e9
			for i in (h as Array).size() / 2:
				hx = minf(hx, float(h[2 * i]))
				z0 = minf(z0, float(h[2 * i + 1]))
				z1 = maxf(z1, float(h[2 * i + 1]))
			ins.append(Vector3(hx - LIFT, float(k) * dy, 0.5 * (z0 + z1)))
		k -= 3
	for _pass in 2:
		for i in range(1, ins.size() - 1):
			ins[i] = Vector3(0.5 * ins[i].x + 0.25 * (ins[i - 1].x + ins[i + 1].x), ins[i].y, 0.5 * ins[i].z + 0.25 * (ins[i - 1].z + ins[i + 1].z))
	if ins.size() >= 2:
		ins[ins.size() - 1].y = y0
		paths["inseam"] = _line_path(ins, Vector3.BACK, Vector3(-1.0, 0.0, 0.25).normalized())


static func _v(ap: PackedFloat32Array, i: int) -> Vector3:
	return Vector3(ap[3 * i], ap[3 * i + 1], ap[3 * i + 2])


static func _arr3(a: Array) -> Vector3:
	return Vector3(float(a[0]), float(a[1]), float(a[2]))


# The convex hull of a plane section (as BodyGen.measure takes a girth), in world points.
static func _hull_loop(m: Dictionary, ap: PackedFloat32Array, list: PackedInt32Array, n: Vector3, d: float, e1: Vector3, e2: Vector3, c: Variant, rmax: float, keep: Callable) -> PackedVector3Array:
	var ca = [c.x, c.y, c.z] if c is Vector3 else null
	var pts := BodyGen._slice(ap, m["tris"], list, [n.x, n.y, n.z], d, [e1.x, e1.y, e1.z], [e2.x, e2.y, e2.z], ca, rmax)
	if keep.is_valid():
		var f := PackedFloat32Array()
		for i in pts.size() / 2:
			if keep.call(pts[2 * i], pts[2 * i + 1]):
				f.append(pts[2 * i])
				f.append(pts[2 * i + 1])
		pts = f
	var out := PackedVector3Array()
	if pts.size() / 2 < 3:
		return out
	var h := BodyGen.hull2(pts, pts.size() / 2)
	for i in h.size() / 2:
		out.append(n * d + e1 * h[2 * i] + e2 * h[2 * i + 1])
	return out


# A girth's tape: the hull lifted off the skin, starting at its front, round once and a little past the start.
func _loop_path(h: PackedVector3Array, side: Vector3) -> Dictionary:
	if h.size() < 3:
		return {}
	var cen := Vector3.ZERO
	for p in h:
		cen += p
	cen /= float(h.size())
	var s0 := 0
	for i in h.size():
		if h[i].z > h[s0].z:
			s0 = i
	var pts := PackedVector3Array()
	var n := h.size()
	var per := 0.0
	for i in n + 1:
		var p: Vector3 = h[(s0 + i) % n]
		var out := (p - cen) - side * side.dot(p - cen)
		pts.append(p + out.normalized() * LIFT)
		if i > 0:
			per += pts[i].distance_to(pts[i - 1])
	# past the start: the end overlaps the beginning, a little further out
	var run := 0.0
	var i := 1
	while run < OVERLAP and i <= n:
		var p: Vector3 = h[(s0 + i) % n]
		var out := ((p - cen) - side * side.dot(p - cen)).normalized()
		var q := p + out * (LIFT + 0.0015)
		var seg := q.distance_to(pts[pts.size() - 1])
		if run + seg > OVERLAP:
			q = pts[pts.size() - 1].lerp(q, (OVERLAP - run) / maxf(seg, 1e-6))
			seg = OVERLAP - run
		pts.append(q)
		run += seg
		i += 1
	var sides := PackedVector3Array()
	sides.resize(pts.size())
	sides.fill(side.normalized())
	var a := pts[0]
	return {"pts": pts, "side": sides, "loop": true, "length": per, "read_s": per, "read": a,
		"anchor": a, "normal": ((a - cen) - side * side.dot(a - cen)).normalized(), "centre": cen}


# A length's tape along a polyline; `face` is the way the tape faces (out of the body), read at its end.
func _line_path(pts: PackedVector3Array, side_hint: Vector3, face: Vector3) -> Dictionary:
	if pts.size() < 2:
		return {}
	var sides := PackedVector3Array()
	var per := 0.0
	var cen := Vector3.ZERO
	for i in pts.size():
		var t := (pts[mini(i + 1, pts.size() - 1)] - pts[maxi(i - 1, 0)]).normalized()
		var s := t.cross(face).normalized()
		if s.length() < 0.5:
			s = side_hint
		sides.append(s)
		cen += pts[i]
		if i > 0:
			per += pts[i].distance_to(pts[i - 1])
	cen /= float(pts.size())
	var anchor := pts[pts.size() / 2]
	return {"pts": pts, "side": sides, "loop": false, "length": per, "read_s": per,
		"read": pts[pts.size() - 1], "anchor": anchor, "normal": face, "centre": cen}


# The back of the body at s (dir -1) or the front (+1): the furthest vertex near s in x and y.
static func _surface_z(ap: PackedFloat32Array, nb: int, s: Vector3, dir: float) -> float:
	var best := s.z
	var found := false
	for i in nb:
		var x := ap[3 * i]
		var y := ap[3 * i + 1]
		if absf(x - s.x) < 0.012 and absf(y - s.y) < 0.02:
			var z := ap[3 * i + 2]
			if not found or z * dir > best * dir:
				best = z
				found = true
	return best


# The outside of the arm at s (on its axis): the furthest arm vertex along o, within a band across the axis.
static func _surface_out(ap: PackedFloat32Array, verts: Dictionary, s: Vector3, axis: Vector3, o: Vector3) -> Vector3:
	var r := 0.0
	for vi in verts:
		var p := _v(ap, int(vi))
		var d := p - s
		if absf(d.dot(axis)) < 0.012:
			var side := d - axis * d.dot(axis)
			if side.normalized().dot(o) > 0.8:
				r = maxf(r, d.dot(o))
	return s + o * (r + LIFT)


# ------------------------------------------------------------------ the tape
func _build_tape(key: String, animate: bool) -> void:
	var p: Dictionary = paths.get(key, {})
	if p.is_empty():
		_show_tape(false)
		return
	var pts: PackedVector3Array = p["pts"]
	var sides: PackedVector3Array = p["side"]
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var s := 0.0
	var verts := PackedVector3Array()
	var uvs := PackedVector2Array()
	for i in pts.size():
		if i > 0:
			s += pts[i].distance_to(pts[i - 1])
		var w: Vector3 = sides[i] * (TAPE_W * 0.5)
		verts.append(pts[i] - w)
		uvs.append(Vector2(s, 0.0))
		verts.append(pts[i] + w)
		uvs.append(Vector2(s, 1.0))
	for i in pts.size() - 1:
		var a := 2 * i
		for idx in [a, a + 1, a + 2, a + 1, a + 3, a + 2]:
			st.set_uv(uvs[idx])
			st.add_vertex(verts[idx])
	st.generate_normals()
	tape.mesh = st.commit()
	tape_mat.set_shader_parameter("read_at", float(p["read_s"]))
	var total := s
	_wrap_dur = clampf(0.5 + total * 0.9, 0.6, 2.2)
	if animate:
		_wrap_t = 0.0
		tape_mat.set_shader_parameter("reveal", 0.0)
		reading.visible = false
	elif _wrap_t >= 0.0:
		pass
	else:
		_wrap_t = -1.0
		tape_mat.set_shader_parameter("reveal", total + 1.0)
		reading.visible = true
	tape.set_meta("total", total)
	tape.visible = true
	_place_reading()


func _place_reading() -> void:
	var p: Dictionary = paths.get(focus_key, {})
	if p.is_empty():
		return
	var r: Vector3 = p["read"]
	var n: Vector3 = p["normal"]
	reading.position = r + n * 0.03 + Vector3(0.0, 0.025, 0.0)
	reading.text = "%.1f cm" % value_cm(focus_key)


func _show_tape(on: bool) -> void:
	if tape == null:
		return
	tape.visible = on
	reading.visible = on
	if not on:
		_wrap_t = -1.0
		_glow_t = -1.0


func tick(delta: float) -> void:
	if not active:
		return
	_frame_left_of_panel()
	ed.cloth_node.visible = false
	for n in [ed._stitch_mesh, ed._seam_mesh, ed._trail_mesh, ed._hl_mesh]:
		if n != null:
			n.visible = false
	_poll_figures()
	if _wrap_t >= 0.0:
		_wrap_t += delta
		var f := clampf(_wrap_t / _wrap_dur, 0.0, 1.0)
		var e := 1.0 - pow(1.0 - f, 3.0)
		var total: float = tape.get_meta("total", 1.0)
		tape_mat.set_shader_parameter("reveal", e * total + 0.002)
		if f >= 1.0:
			_wrap_t = -1.0
			reading.visible = true
			_glow_t = 0.0 if mine.has(focus_key) else -1.0
	if _glow_t >= 0.0:
		_glow_t += delta
		var g := exp(-1.6 * _glow_t) * (0.75 + 0.25 * cos(_glow_t * 9.0))
		tape_mat.set_shader_parameter("glow", g)
		var hdr := 1.0 + 2.2 * g
		reading.modulate = Color(hdr, hdr * 0.86, hdr * 0.55)
		if _glow_t > 3.0:
			_glow_t = -1.0
	else:
		tape_mat.set_shader_parameter("glow", 0.0)
		reading.modulate = Color(1, 1, 1)
	if reading.visible:
		_place_reading()
	if _next_at >= 0.0:
		_next_at -= delta
		if _next_at < 0.0:
			_next_at = -1.0
			_advance()
	overlay.queue_redraw()


# The camera aims at the subject; shifted sideways (h_offset) it sits in the middle of the view left of the card.
func _frame_left_of_panel() -> void:
	var cam: Camera3D = ed.cam
	var w := maxf(1.0, overlay.size.x)
	var f := -(PANEL_W + 20.0) / (2.0 * w)
	var asp := w / maxf(1.0, overlay.size.y)
	var half_w := tan(deg_to_rad(cam.fov) * 0.5) * asp
	cam.h_offset = -f * 2.0 * float(ed.view_dist) * half_w


# ------------------------------------------------------------------ hotspots
func _dots() -> Array:
	var out := []
	if page != "measure":
		return out
	var cam: Camera3D = ed.cam
	var to_cam := cam.global_transform.basis.z
	var sx: float = overlay.size.x / maxf(1.0, float(ed.sub_view.size.x))
	var sy: float = overlay.size.y / maxf(1.0, float(ed.sub_view.size.y))
	for k in ORDER:
		var p: Dictionary = paths.get(k, {})
		if p.is_empty():
			continue
		var a: Vector3 = p["anchor"]
		if cam.is_position_behind(a):
			continue
		var facing: float = (p["normal"] as Vector3).dot(to_cam)
		var sp := cam.unproject_position(a)
		out.append({"key": k, "pos": Vector2(sp.x * sx, sp.y * sy), "facing": facing})
	return out


func _draw_overlay() -> void:
	if not active:
		return
	var font := overlay.get_theme_default_font()
	for d in _dots():
		var k: String = d["key"]
		var front: bool = float(d["facing"]) > -0.15
		var hot: bool = k == _hover_dot or k == focus_key
		var col := Color("#7fd6a0") if mine.has(k) else Color("#ffb45a")
		var a := 1.0 if front else 0.35
		var r := DOT_R * (1.35 if hot else 1.0)
		var p: Vector2 = d["pos"]
		overlay.draw_circle(p, r + 5.0, Color(col.r, col.g, col.b, 0.18 * a))
		overlay.draw_circle(p, r, Color(col.r, col.g, col.b, 0.95 * a))
		overlay.draw_arc(p, r, 0, TAU, 24, Color(1, 1, 1, 0.8 * a), 1.5, true)
		if hot or (front and focus_key == ""):
			var txt := String(_spec(k)[1])
			var tw := font.get_string_size(txt, HORIZONTAL_ALIGNMENT_LEFT, -1, 12).x
			var at := p + Vector2(r + 8.0, 4.0)
			overlay.draw_rect(Rect2(at - Vector2(5, 14), Vector2(tw + 10, 19)), Color(0.05, 0.06, 0.08, 0.75 * a))
			overlay.draw_string(font, at, txt, HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color(1, 1, 1, a))


func _dot_at(pos: Vector2) -> String:
	var best := ""
	var bd := 14.0
	for d in _dots():
		var dd: float = (d["pos"] as Vector2).distance_to(pos)
		if dd < bd:
			bd = dd
			best = String(d["key"])
	return best


# Pointer events over the 3D view. True when used (a dot); else the camera gets them.
func input(e: InputEvent) -> bool:
	if not active:
		return false
	if e is InputEventMouseMotion:
		var h := _dot_at((e as InputEventMouseMotion).position)
		if h != _hover_dot:
			_hover_dot = h
			overlay.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND if h != "" else Control.CURSOR_ARROW
			ed.view_pane.mouse_default_cursor_shape = overlay.mouse_default_cursor_shape
		return false
	if e is InputEventMouseButton:
		var mb := e as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_LEFT and mb.pressed:
			var k := _dot_at(mb.position)
			if k != "":
				focus(k)
				return true
	return false


# Keys while the pointer is over the view (Esc and B are the editor's).
func key(e: InputEventKey) -> bool:
	if not active:
		return false
	match e.keycode:
		KEY_ENTER, KEY_KP_ENTER:
			confirm()
			return true
		KEY_TAB:
			if page == "measure":
				var i := ORDER.find(focus_key)
				focus(ORDER[(i + (ORDER.size() - 1 if e.shift_pressed else 1)) % ORDER.size()])
				return true
	return false
