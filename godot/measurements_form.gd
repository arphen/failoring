# "My measurements": a form for the wearer's own body measurements, and the CSV file that keeps them.
#
# The file lives in the user's Documents folder (Documents/Failoring/my_measurements.csv), not in the app's data folder,
# so clearing the app's data keeps it, and it can be edited or copied by hand. One measurement per row:
#   measurement,value,unit
#   height,168,cm
#   muscle,50,%
# Lengths in cm, body type in %. A row with an empty value (or a missing row) for an optional measurement means
# "estimate it from the others" (BodyGen's auto value). Lines starting with # are comments. FL_MEAS_CSV=<path> uses
# another file (tests).
#
# The editor loads the file at start (when it exists and the editor has a window) and opens the form from the Body tab;
# "Use and save" applies the values to the mannequin and writes the file.
class_name MeasurementsForm
extends RefCounted

const BodyGen = preload("res://body_gen.gd")

# key, label, min, max (cm), how to measure
const ESSENTIAL := [
	["height", "Height", 140.0, 200.0, "Barefoot, back to a wall: floor to the top of the head."],
	["bust", "Bust", 70.0, 140.0, "Round the fullest part of the chest, tape level, breathing normally."],
	["waist", "Waist", 55.0, 130.0, "Round the narrowest part of the torso, usually a little above the navel."],
	["hip", "Hip", 75.0, 145.0, "Round the fullest part of the seat, feet together."],
	["shoulder", "Shoulder width", 30.0, 48.0, "Across the back, from one shoulder point (the bony tip) to the other."],
]
const OPTIONAL := [
	["arm", "Arm length", 44.0, 68.0, "Shoulder point to the wrist bone, arm hanging, elbow slightly bent."],
	["inseam", "Inseam", 62.0, 96.0, "Crotch to the floor on the inside of the leg, barefoot."],
	["neck", "Neck", 28.0, 48.0, "Round the base of the neck, where a collar sits."],
	["thigh", "Thigh", 40.0, 85.0, "Round the fullest part of one thigh, just below the crotch."],
	["upperarm", "Upper arm", 20.0, 45.0, "Round the fullest part of the upper arm, arm relaxed."],
	["back", "Back length", 30.0, 48.0, "From the prominent bone at the base of the neck down to the waist."],
]
# key, label, min, max (%), what it does
const SHAPE := [
	["muscle", "Muscle", 0.0, 100.0, "Muscle tone: 0 soft, 100 very toned."],
	["cup", "Bust shape", -100.0, 100.0, "Smaller (-) or fuller (+) bust for the same bust measurement."],
	["belly", "Belly", -100.0, 100.0, "Flatter (-) or rounder (+) belly for the same waist measurement."],
]


static func csv_path() -> String:
	if OS.has_environment("FL_MEAS_CSV"):
		return OS.get_environment("FL_MEAS_CSV")
	return OS.get_system_dir(OS.SYSTEM_DIR_DOCUMENTS).path_join("Failoring").path_join("my_measurements.csv")


static func has_file() -> bool:
	return FileAccess.file_exists(csv_path())


# The measurements in the file, in BodyGen's units (metres; body type -1..1 or 0..1), or {} when there is no file or
# nothing readable in it. Optional measurements left empty are absent (BodyGen estimates them).
static func load_csv() -> Dictionary:
	if not has_file():
		return {}
	var f := FileAccess.open(csv_path(), FileAccess.READ)
	if f == null:
		return {}
	var known := {}
	for spec in ESSENTIAL + OPTIONAL + SHAPE:
		known[spec[0]] = spec
	var out := {}
	while not f.eof_reached():
		var line := f.get_line().strip_edges()
		if line == "" or line.begins_with("#"):
			continue
		var cells := line.split(",")
		var key := cells[0].strip_edges().to_lower()
		if not known.has(key) or cells.size() < 2:
			continue  # the header and anything unknown
		var txt := cells[1].strip_edges()
		if txt == "" or not txt.is_valid_float():
			continue
		var spec: Array = known[key]
		var v := clampf(float(txt), float(spec[2]), float(spec[3]))
		out[key] = v / 100.0
	return out


static func save_csv(meas: Dictionary) -> Error:
	var path := csv_path()
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return FileAccess.get_open_error()
	f.store_line("# Failoring: my body measurements. Lengths in cm, body type in %. Leave an optional value empty to")
	f.store_line("# have it estimated from the others. Edit by hand if you like; the editor reads this file at start.")
	f.store_line("measurement,value,unit")
	for spec in ESSENTIAL + OPTIONAL:
		var v = meas.get(spec[0], null)
		f.store_line("%s,%s,cm" % [spec[0], "" if v == null else "%.1f" % (float(v) * 100.0)])
	for spec in SHAPE:
		var v = meas.get(spec[0], null)
		f.store_line("%s,%s,%%" % [spec[0], "" if v == null else "%d" % roundi(float(v) * 100.0)])
	f.close()
	return OK


# ---------------------------------------------------------------- the form
var _win: Window
var _edits := {}       # key -> LineEdit (lengths)
var _sliders := {}     # key -> HSlider (body type)
var _err: Label
var _on_apply: Callable


# Opens the form over `parent`, filled with `meas`; `auto` gives the estimated value (metres) of an optional measurement
# for the placeholder. on_apply(meas) gets the new measurements when the user presses "Use and save".
func open(parent: Node, meas: Dictionary, auto: Callable, on_apply: Callable) -> void:
	_on_apply = on_apply
	_win = Window.new()
	_win.title = "My measurements"
	_win.transient = true
	_win.exclusive = true
	# a fixed size with a scroll box: word-wrapped labels measure themselves one character wide in a growing window, which
	# then grew thousands of pixels tall
	# (the title bar is outside the size: leave room for it)
	_win.size = Vector2i(720, mini(760, int(parent.get_viewport().get_visible_rect().size.y) - 90))
	_win.close_requested.connect(close)
	var bg := PanelContainer.new()
	bg.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_win.add_child(bg)
	var outer := VBoxContainer.new()
	bg.add_child(outer)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	outer.add_child(scroll)
	var pad := MarginContainer.new()
	pad.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	for side in ["left", "right", "top", "bottom"]:
		pad.add_theme_constant_override("margin_" + side, 18)
	scroll.add_child(pad)
	var vb := VBoxContainer.new()
	vb.add_theme_constant_override("separation", 10)
	pad.add_child(vb)
	var intro := Label.new()
	intro.text = "Measure over light underwear with a soft tape, snug but not tight."
	vb.add_child(intro)
	vb.add_child(_section("Essential"))
	vb.add_child(_length_grid(ESSENTIAL, meas, Callable()))
	vb.add_child(_section("Optional  (leave empty to estimate)"))
	vb.add_child(_length_grid(OPTIONAL, meas, auto))
	vb.add_child(_section("Body type"))
	vb.add_child(_shape_grid(meas))
	_err = Label.new()
	_err.add_theme_color_override("font_color", Color(1.0, 0.55, 0.35))
	_err.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_err.custom_minimum_size = Vector2(640, 0)
	_err.visible = false
	vb.add_child(_err)
	var where := Label.new()
	where.text = "Saved to %s" % csv_path()
	where.add_theme_font_size_override("font_size", 11)
	where.modulate = Color(1, 1, 1, 0.6)
	where.autowrap_mode = TextServer.AUTOWRAP_ARBITRARY
	where.custom_minimum_size = Vector2(640, 0)
	vb.add_child(where)
	var row := HBoxContainer.new()
	row.alignment = BoxContainer.ALIGNMENT_END
	row.add_theme_constant_override("separation", 8)
	var show := Button.new()
	show.text = "Show file"
	show.tooltip_text = "Open the folder with the measurements file"
	show.disabled = not has_file()
	show.pressed.connect(func() -> void: OS.shell_show_in_file_manager(csv_path()))
	row.add_child(show)
	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(spacer)
	var cancel := Button.new()
	cancel.text = "Cancel"
	cancel.pressed.connect(close)
	row.add_child(cancel)
	var ok := Button.new()
	ok.name = "UseAndSave"
	ok.text = "Use and save"
	ok.pressed.connect(_apply)
	row.add_child(ok)
	# the buttons stay in view below the scrolling part
	var rpad := MarginContainer.new()
	for side in ["left", "right", "bottom"]:
		rpad.add_theme_constant_override("margin_" + side, 18)
	rpad.add_theme_constant_override("margin_top", 8)
	rpad.add_child(row)
	outer.add_child(rpad)
	parent.add_child(_win)
	_win.popup_centered()


func close() -> void:
	if _win != null and is_instance_valid(_win):
		_win.queue_free()
	_win = null


func _section(text: String) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", 15)
	return l


func _hint(text: String) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", 11)
	l.modulate = Color(1, 1, 1, 0.6)
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.custom_minimum_size = Vector2(260, 0)
	l.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	return l


func _length_grid(specs: Array, meas: Dictionary, auto: Callable) -> GridContainer:
	var g := GridContainer.new()
	g.columns = 4
	g.add_theme_constant_override("h_separation", 10)
	for spec in specs:
		var l := Label.new()
		l.text = spec[1]
		l.custom_minimum_size = Vector2(110, 0)
		g.add_child(l)
		var e := LineEdit.new()
		e.name = "Edit_" + spec[0]
		e.custom_minimum_size = Vector2(72, 0)
		e.alignment = HORIZONTAL_ALIGNMENT_RIGHT
		var v = meas.get(spec[0], null)
		e.text = "" if v == null else "%.1f" % (float(v) * 100.0)
		if auto.is_valid():
			var a = auto.call(spec[0])
			e.placeholder_text = "auto" if a == null else "%.1f" % (float(a) * 100.0)
		e.tooltip_text = "%s, %d to %d cm" % [spec[1], int(spec[2]), int(spec[3])]
		e.text_submitted.connect(func(_t: String) -> void: _apply())
		_edits[spec[0]] = e
		g.add_child(e)
		var u := Label.new()
		u.text = "cm"
		g.add_child(u)
		g.add_child(_hint(spec[4]))
	return g


func _shape_grid(meas: Dictionary) -> GridContainer:
	var g := GridContainer.new()
	g.columns = 4
	g.add_theme_constant_override("h_separation", 10)
	for spec in SHAPE:
		var l := Label.new()
		l.text = spec[1]
		l.custom_minimum_size = Vector2(110, 0)
		g.add_child(l)
		var s := HSlider.new()
		s.min_value = spec[2]
		s.max_value = spec[3]
		s.step = 1.0
		s.custom_minimum_size = Vector2(120, 0)
		s.value = float(meas.get(spec[0], 0.5 if spec[0] == "muscle" else 0.0)) * 100.0
		_sliders[spec[0]] = s
		g.add_child(s)
		var o := Label.new()
		o.custom_minimum_size = Vector2(44, 0)
		var show := func(v: float) -> void: o.text = ("%d%%" % int(v)) if spec[0] == "muscle" else (("+" if v > 0.0 else "") + str(int(v)))
		show.call(s.value)
		s.value_changed.connect(show)
		g.add_child(o)
		g.add_child(_hint(spec[4]))
	return g


# Reads the fields; essential ones must be numbers in range, optional ones may be empty. Then applies and saves.
func _apply() -> void:
	var out := {}
	var bad := []
	for spec in ESSENTIAL + OPTIONAL:
		var txt := (_edits[spec[0]] as LineEdit).text.strip_edges().replace(",", ".")
		var optional := OPTIONAL.has(spec)
		if txt == "" and optional:
			out[spec[0]] = null
			continue
		if not txt.is_valid_float() or float(txt) < float(spec[2]) or float(txt) > float(spec[3]):
			bad.append("%s (%d to %d cm)" % [spec[1], int(spec[2]), int(spec[3])])
			continue
		out[spec[0]] = float(txt) / 100.0
	if not bad.is_empty():
		_err.text = "Please check: " + ", ".join(bad) + "."
		_err.visible = true
		return
	for spec in SHAPE:
		out[spec[0]] = (_sliders[spec[0]] as HSlider).value / 100.0
	var e := save_csv(out)
	if e != OK:
		_err.text = "The measurements could not be saved to %s (error %d). They are used anyway." % [csv_path(), e]
		_err.visible = true
	_on_apply.call(out)
	if e == OK:
		close()
