extends Control
# The calibration wizard (docs/CROCHET_PLAN.md section 10): four kitchen-table measurements of a swatch of the yarn on the hook turn into
# a record in user://crochet/swatches.json. The stitch model uses the record instead of the starter values for the parts that were
# measured, and the crochet scene then says "calibrated" instead of "UNVERIFIED". Every number can be left at 0 (not measured); the result
# panel shows at once what the record would be and what it does to one single crochet stitch.

const Records = preload("res://crochet/crochet_records.gd")
const YARNS_PATH := "res://materials/yarns.json"

signal closed
signal records_changed

# the Peirce cantilever of step 3: a strip pushed over a table edge until its tip touches the 41.5 degree line of a card
class Diagram extends Control:
	func _draw() -> void:
		var w := size.x
		var h := size.y
		draw_rect(Rect2(Vector2.ZERO, size), Color(0.16, 0.17, 0.19))
		var edge := Vector2(w * 0.38, h * 0.38)
		draw_rect(Rect2(Vector2(0, edge.y), Vector2(edge.x, h * 0.12)), Color(0.45, 0.33, 0.22))                     # the table
		var tip := edge + Vector2(w * 0.5, 0)
		draw_line(Vector2(0, edge.y), tip, Color(0.88, 0.88, 0.9), 4.0)                                              # the strip on the table and over the edge
		draw_line(edge + Vector2(0, 8), tip + Vector2(0, 8), Color(0.88, 0.5, 0.4), 2.0)
		var ang := deg_to_rad(41.5)
		var card_end := edge + Vector2(cos(ang), sin(ang)) * w * 0.62                                                 # the 41.5 degree card, from the edge
		draw_dashed_line(edge, card_end, Color(0.95, 0.85, 0.3), 2.0, 6.0)
		draw_arc(edge, w * 0.14, 0, ang, 16, Color(0.95, 0.85, 0.3), 2.0)
		var font := ThemeDB.fallback_font
		draw_string(font, edge + Vector2(w * 0.17, h * 0.2), "41.5°", HORIZONTAL_ALIGNMENT_LEFT, -1, 14, Color(0.95, 0.85, 0.3))
		draw_line(edge + Vector2(0, -14), tip + Vector2(0, -14), Color(0.6, 0.85, 1.0), 1.5)
		draw_string(font, edge + Vector2(w * 0.2, -22), "overhang L", HORIZONTAL_ALIGNMENT_LEFT, -1, 14, Color(0.6, 0.85, 1.0))
		draw_string(font, Vector2(6, h - 8), "Slide the strip until its tip touches the card line; the bending length is c = L / 2.", HORIZONTAL_ALIGNMENT_LEFT, w - 12, 12, Color(0.8, 0.8, 0.82))

var yarn_id := "acrylic_worsted"
var hook_mm := 5.0
var stitch := "sc"
var records_path := ""
var _calib: CrochetCalibration
var _fields := {}
var _result: RichTextLabel
var _header: Label
var _notes: LineEdit
var _save_btn: Button
var _del_btn: Button
var _last := {}

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_calib = CrochetCalibration.new()
	_calib.load_yarns(YARNS_PATH)
	_build_ui()

# --- UI

func _num(parent: Control, key: String, label: String, max_value: float, step: float, tip: String) -> void:
	var row := HBoxContainer.new()
	var l := Label.new()
	l.text = label
	l.custom_minimum_size = Vector2(250, 0)
	l.tooltip_text = tip
	row.add_child(l)
	var sb := SpinBox.new()
	sb.min_value = 0
	sb.max_value = max_value
	sb.step = step
	sb.allow_greater = true
	sb.custom_minimum_size = Vector2(110, 0)
	sb.tooltip_text = tip
	sb.value_changed.connect(func(_v: float) -> void: _refresh())
	row.add_child(sb)
	parent.add_child(row)
	_fields[key] = sb

func _section(parent: Control, title: String, help: String) -> VBoxContainer:
	var box := VBoxContainer.new()
	var t := Label.new()
	t.text = title
	t.add_theme_font_size_override("font_size", 15)
	box.add_child(t)
	var h := Label.new()
	h.text = help
	h.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	h.add_theme_color_override("font_color", Color(0.72, 0.74, 0.78))
	box.add_child(h)
	parent.add_child(box)
	parent.add_child(HSeparator.new())
	return box

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
	title.text = "Calibrate"
	title.add_theme_font_size_override("font_size", 16)
	bar.add_child(title)
	_header = Label.new()
	_header.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar.add_child(_header)
	_save_btn = Button.new()
	_save_btn.text = "Save record"
	_save_btn.pressed.connect(func() -> void: save())
	bar.add_child(_save_btn)
	_del_btn = Button.new()
	_del_btn.text = "Delete record"
	_del_btn.pressed.connect(func() -> void: delete())
	bar.add_child(_del_btn)
	var close := Button.new()
	close.text = "Back"
	close.pressed.connect(func() -> void:
		visible = false
		closed.emit())
	bar.add_child(close)
	var split := HSplitContainer.new()
	split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root.add_child(split)
	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size = Vector2(560, 0)
	scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	split.add_child(scroll)
	var col := VBoxContainer.new()
	col.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(col)
	var intro := Label.new()
	intro.text = "Work a swatch in this yarn on this hook, then measure it. Leave a number at 0 when you did not measure it: that part of the stitch stays at the starter value."
	intro.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	col.add_child(intro)
	col.add_child(HSeparator.new())
	var s1 := _section(col, "1. Gauge", "A swatch of about 12 x 12 cm. Count the stitches and the rows in 10 cm. That gives the width and height of one stitch.")
	_num(s1, "stitches_per_10cm", "Stitches in 10 cm", 100, 0.5, "single crochet stitches along a row")
	_num(s1, "rows_per_10cm", "Rows in 10 cm", 100, 0.5, "rows up the swatch")
	var s2 := _section(col, "2. Weight", "Weigh the swatch, and give its size in cm, or the number of stitches and rows it has (the gauge from step 1 then gives its area). That gives the density of the fabric and the yarn in one stitch.")
	_num(s2, "mass_g", "Mass (g)", 500, 0.1, "weigh it on kitchen scales")
	_num(s2, "width_cm", "Width (cm)", 100, 0.1, "")
	_num(s2, "height_cm", "Height (cm)", 100, 0.1, "")
	_num(s2, "stitches", "or stitches per row", 500, 1, "used when the size above is 0")
	_num(s2, "rows", "or rows", 500, 1, "used when the size above is 0")
	var s3 := _section(col, "3. Cantilever (Peirce)", "A strip 3-4 cm wide and at least 20 cm long, worked once as a tall narrow strip (long axis along the wale) and once as a short wide one (long axis along the course). Slide it over a table edge until the tip touches the 41.5° line of a card. Measure the overhang L. Needs the weight of step 2.")
	_num(s3, "overhang_wale_mm", "Overhang, long axis = wale (mm)", 500, 1, "")
	_num(s3, "overhang_course_mm", "Overhang, long axis = course (mm)", 500, 1, "")
	var diagram := Diagram.new()
	diagram.custom_minimum_size = Vector2(0, 190)
	s3.add_child(diagram)
	var s4 := _section(col, "4. Stretch", "Hang a strip about 10 cm wide from a clip bar across its full width, mark 10 cm (L0), and hang a weight W1 (say 250 g), then W2 (say 1 kg). Measure the marked length L1, L2 under each. Do it for the wale and for the course. If the second slope is not clearly steeper no lock was reached: use a heavier W2.")
	for dir in ["wale", "course"]:
		var l := Label.new()
		l.text = "Strip along the %s" % dir
		s4.add_child(l)
		_num(s4, dir + "_width_cm", "  strip width (cm)", 100, 0.1, "")
		_num(s4, dir + "_l0_mm", "  marked length L0 (mm)", 500, 1, "")
		_num(s4, dir + "_w1_g", "  weight W1 (g)", 5000, 10, "")
		_num(s4, dir + "_l1_mm", "  length L1 under W1 (mm)", 1000, 0.5, "")
		_num(s4, dir + "_w2_g", "  weight W2 (g)", 5000, 10, "")
		_num(s4, dir + "_l2_mm", "  length L2 under W2 (mm)", 1000, 0.5, "")
	for dir in ["wale", "course"]:
		_fields[dir + "_width_cm"].value = 10
		_fields[dir + "_l0_mm"].value = 100
	var right := VBoxContainer.new()
	right.custom_minimum_size = Vector2(520, 0)
	split.add_child(right)
	_notes = LineEdit.new()
	_notes.placeholder_text = "notes (fibre batch, how the swatch was blocked ...)"
	right.add_child(_notes)
	_result = RichTextLabel.new()
	_result.bbcode_enabled = true
	_result.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_child(_result)
	_refresh()

# --- the numbers

# the context: which yarn on which hook (mm, the real size) the record is for; the inputs are those of its record when there is one
func open(yarn: String, hook: float, path := "") -> void:
	yarn_id = yarn
	hook_mm = hook
	records_path = path if path != "" else Records.path()
	var rec := Records.find(Records.load_all(records_path), yarn_id, hook_mm, stitch)
	for k in _fields.keys():
		_fields[k].set_value_no_signal(0)
	for dir in ["wale", "course"]:
		_fields[dir + "_width_cm"].set_value_no_signal(10)
		_fields[dir + "_l0_mm"].set_value_no_signal(100)
	_notes.text = ""
	if not rec.is_empty():
		_notes.text = String(rec.get("notes", ""))
		var m: Dictionary = rec.get("measured", {})
		for k in m.keys():
			if _fields.has(k):
				_fields[k].set_value_no_signal(float(m[k]))
	_refresh()

func set_value(key: String, v: float) -> void:
	_fields[key].value = v

func value(key: String) -> float:
	return float(_fields[key].value)

func measured() -> Dictionary:
	var m := {}
	for k in _fields.keys():
		m[k] = float(_fields[k].value)
	return m

# what CrochetCalibration.fit() takes
func measurements() -> Dictionary:
	var d := {
		"stitches_per_10cm": value("stitches_per_10cm"), "rows_per_10cm": value("rows_per_10cm"),
		"mass_g": value("mass_g"), "width_cm": value("width_cm"), "height_cm": value("height_cm"),
		"stitches": value("stitches"), "rows": value("rows"),
		"overhang_wale_mm": value("overhang_wale_mm"), "overhang_course_mm": value("overhang_course_mm"),
	}
	for dir in ["wale", "course"]:
		if value(dir + "_w1_g") > 0 or value(dir + "_l1_mm") > 0:
			d[dir] = {"width_cm": value(dir + "_width_cm"), "l0_mm": value(dir + "_l0_mm"), "w1_g": value(dir + "_w1_g"), "w2_g": value(dir + "_w2_g"),
				"l1_mm": value(dir + "_l1_mm"), "l2_mm": value(dir + "_l2_mm")}
	return d

func current_fit() -> Dictionary:
	return _calib.fit(measurements())

# the record to save: the fitted parts, the context, the date, the notes, the inputs (so that the wizard can show them again), and the
# hinge entries of the record already there (they come from the hinge fit and are not measured here)
func record() -> Dictionary:
	var fit := current_fit()
	var rec: Dictionary = fit["record"].duplicate(true)
	rec["yarn"] = yarn_id
	rec["hook_mm"] = hook_mm
	rec["stitch"] = stitch
	rec["date"] = Time.get_date_string_from_system()
	rec["notes"] = _notes.text
	rec["measured"] = measured()
	return rec

func _refresh() -> void:
	if _result == null:
		return
	_header.text = "   %s on a %.2f mm hook, stitch %s" % [yarn_id, hook_mm, stitch]
	var fit := current_fit()
	_last = fit
	var existing := Records.find(Records.load_all(records_path if records_path != "" else Records.path()), yarn_id, hook_mm, stitch)
	var t := ""
	if not existing.is_empty():
		t += "[color=#80d080]A record of %s is saved for this yarn and hook.[/color]\n\n" % String(existing.get("date", "?"))
	if fit["empty"] and fit["errors"].is_empty():
		t += "[i]Nothing measured yet.[/i]\n\n"
	for e in fit["errors"]:
		t += "[color=#e07060]%s[/color]\n" % e
	for n in fit["notes"]:
		t += "%s\n" % n
	if not fit["empty"]:
		var cmp: Dictionary = _calib.compare(yarn_id, hook_mm, fit["record"])
		if not cmp.is_empty():
			var a: Dictionary = cmp["starter"]
			var b: Dictionary = cmp["calibrated"]
			t += "\n[b]One single crochet stitch[/b]\n[table=3][cell][b][/b][/cell][cell][b]starter[/b][/cell][cell][b]with this record[/b][/cell]"
			for k in ["w_mm", "h_mm", "yarn_mm", "density", "kWale", "kCourse", "lockWale", "lockCourse", "lockRatio", "bendWale", "bendCourse"]:
				var same := absf(float(a[k]) - float(b[k])) <= 1e-9 * maxf(1e-12, absf(float(a[k])))
				t += "[cell]%s[/cell][cell]%s[/cell][cell]%s[/cell]" % [k, _num_text(float(a[k])), ("[b]%s[/b]" % _num_text(float(b[k]))) if not same else _num_text(float(b[k]))]
			t += "[/table]"
	_result.text = t
	_save_btn.disabled = fit["empty"]
	_del_btn.disabled = existing.is_empty()

# 4 significant digits, in scientific form for the small bending numbers
func _num_text(v: float) -> String:
	return String.num_scientific(v) if v != 0.0 and absf(v) < 0.01 else String.num(v, 4)

func save() -> bool:
	if current_fit()["empty"]:
		return false
	var p := records_path if records_path != "" else Records.path()
	var ok := Records.save_all(p, Records.upsert(Records.load_all(p), record()))
	_refresh()
	if ok:
		records_changed.emit()
	return ok

func delete() -> bool:
	var p := records_path if records_path != "" else Records.path()
	var all := Records.load_all(p)
	var left := Records.remove(all, yarn_id, hook_mm, stitch)
	if left.size() == all.size():
		return false
	var ok := Records.save_all(p, left)
	_refresh()
	if ok:
		records_changed.emit()
	return ok

func result_text() -> String:
	return _result.text
