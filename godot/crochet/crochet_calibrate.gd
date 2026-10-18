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
var _hinge := {}              # construction -> {bendScale, restAngleDeg}: fitted here, or already in the saved record
var _hinge_opt: OptionButton
var _hinge_label: Label
var _hinge_btn: Button
var _hinge_thread: Thread
var _hinge_job := {}          # what the running thread was asked: {construction, rest}
var _hinge_result := {}

func _exit_tree() -> void:
	if _hinge_thread != null and _hinge_thread.is_started():
		_hinge_thread.wait_to_finish()

func _ready() -> void:
	set_process(false)
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_calib = CrochetCalibration.new()
	_calib.load_yarns(YARNS_PATH)
	_build_ui()

# --- UI

func _num(parent: Control, key: String, label: String, max_value: float, step: float, tip: String, min_value := 0.0) -> void:
	var row := HBoxContainer.new()
	var l := Label.new()
	l.text = label
	l.custom_minimum_size = Vector2(250, 0)
	l.tooltip_text = tip
	row.add_child(l)
	var sb := SpinBox.new()
	sb.min_value = min_value
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
	var s5 := _section(col, "5. Hinge (optional, per construction)", "A swatch of 10 rows, the hinge row, 10 rows. (a) Lay it free on a table, right side up, and measure the angle the upper part settles at: the rest angle (positive when it folds toward the wrong side). (b) Clamp the lower half flat with the hinge at the table edge and measure the droop angle of the upper half (below the horizontal, along the line from the hinge to the far edge). The wizard runs the same setup in the simulator and finds the bendScale that gives your droop (about a dozen runs). Use the fabric numbers above for the same yarn and hook.")
	var hrow := HBoxContainer.new()
	var hl := Label.new()
	hl.text = "The hinge row is worked"
	hl.custom_minimum_size = Vector2(250, 0)
	hrow.add_child(hl)
	_hinge_opt = OptionButton.new()
	for it in [["BLO (back loop only)", "blo"], ["FLO (front loop only)", "flo"], ["a row of sl sts", "slst"]]:
		_hinge_opt.add_item(it[0])
		_hinge_opt.set_item_metadata(_hinge_opt.item_count - 1, it[1])
	_hinge_opt.item_selected.connect(func(_i: int) -> void: _refresh())
	hrow.add_child(_hinge_opt)
	s5.add_child(hrow)
	_num(s5, "hinge_rest_deg", "Rest angle (degrees, toward the WS)", 180, 0.1, "the free swatch's upper part, measured against the lower", -180)
	_num(s5, "hinge_droop_deg", "Droop angle (degrees below horizontal)", 180, 0.1, "the clamped swatch's upper half")
	_hinge_btn = Button.new()
	_hinge_btn.text = "Fit bendScale"
	_hinge_btn.tooltip_text = "Runs the simulator about a dozen times on a worker thread"
	_hinge_btn.pressed.connect(func() -> void: start_hinge_fit())
	s5.add_child(_hinge_btn)
	_hinge_label = Label.new()
	_hinge_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	s5.add_child(_hinge_label)
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
	_hinge = {}
	_hinge_label.text = ""
	if not rec.is_empty():
		_notes.text = String(rec.get("notes", ""))
		_hinge = rec.get("hinge", {}).duplicate(true)
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
	if not _hinge.is_empty():
		rec["hinge"] = _hinge.duplicate(true)
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
	if fit["empty"] and fit["errors"].is_empty() and _hinge.is_empty():
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
	for k in _hinge.keys():
		t += "\nhinge %s: bendScale %.3f, rest angle %.1f deg" % [k, float(_hinge[k]["bendScale"]), float(_hinge[k]["restAngleDeg"])]
	_result.text = t
	_save_btn.disabled = fit["empty"] and _hinge.is_empty()
	_del_btn.disabled = existing.is_empty()

# 4 significant digits, in scientific form for the small bending numbers
func _num_text(v: float) -> String:
	return String.num_scientific(v) if v != 0.0 and absf(v) < 0.01 else String.num(v, 4)

func save() -> bool:
	if current_fit()["empty"] and _hinge.is_empty():
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

# --- the hinge fit (section 10.5)

func hinge_construction() -> String:
	return String(_hinge_opt.get_item_metadata(_hinge_opt.selected))

func _hinge_fit_args() -> Array:
	return [hinge_construction(), value("hinge_rest_deg"), value("hinge_droop_deg"), yarn_id, hook_mm, current_fit()["record"]]

# the fit on this thread (tests); start_hinge_fit() runs it on a worker so that the window stays alive
func fit_hinge_now() -> Dictionary:
	var a := _hinge_fit_args()
	var r := _calib.fit_hinge(a[0], a[1], a[2], a[3], a[4], a[5])
	_apply_hinge_fit(r, a[0], a[1])
	return r

func start_hinge_fit() -> void:
	if _hinge_thread != null and _hinge_thread.is_started():
		return
	if value("hinge_droop_deg") <= 0:
		_hinge_label.text = "Enter the droop angle of the clamped swatch first."
		return
	var a := _hinge_fit_args()
	_hinge_job = {"construction": a[0], "rest": a[1]}
	_hinge_btn.disabled = true
	_hinge_label.text = "Fitting: the simulator runs about a dozen times ..."
	_hinge_thread = Thread.new()
	_hinge_thread.start(_hinge_worker.bind(a))
	set_process(true)

func _hinge_worker(a: Array) -> void:
	_hinge_result = _calib.fit_hinge(a[0], a[1], a[2], a[3], a[4], a[5])

func _process(_delta: float) -> void:
	if _hinge_thread != null and _hinge_thread.is_started() and not _hinge_thread.is_alive():
		_hinge_thread.wait_to_finish()
		_hinge_btn.disabled = false
		_apply_hinge_fit(_hinge_result, String(_hinge_job["construction"]), float(_hinge_job["rest"]))
		set_process(false)

func _apply_hinge_fit(r: Dictionary, construction: String, rest: float) -> void:
	if r.is_empty():
		_hinge_label.text = "The fit could not run (unknown yarn?)."
		return
	var warnings: Array[String] = []   # the notes except the summary of the last run, which the line below says itself
	for n in r["notes"]:
		if not String(n).contains("gives a droop of"):
			warnings.append(String(n))
	var notes := " ".join(warnings)
	if bool(r["bracketed"]):
		_hinge[construction] = {"bendScale": float(r["bend_scale"]), "restAngleDeg": rest}
		_hinge_label.text = "bendScale %.3f for %s: the simulation droops %.2f deg (measured %.2f deg), %d runs. %sSave the record to keep it." % [float(r["bend_scale"]), construction, float(r["droop_deg"]), value("hinge_droop_deg"), int(r["runs"]), (notes + " ") if notes != "" else ""]
	else:
		_hinge_label.text = "No fit: " + notes
	_refresh()

func hinge_entries() -> Dictionary:
	return _hinge

func hinge_text() -> String:
	return _hinge_label.text

func result_text() -> String:
	return _result.text
