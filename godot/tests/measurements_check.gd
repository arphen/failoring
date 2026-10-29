# My-measurements check (measurements_form.gd): the CSV round trip (lengths, empty optional values, body type), tolerant
# reading (comments, spaces, unknown rows, out-of-range values clamped), and the form: a bad value is refused, "Use and
# save" hands the values over and writes the file. Uses a scratch file (FL_MEAS_CSV), never the user's own.
#   godot --headless --path godot res://tests/measurements_check.tscn      (PASS/FAIL lines, then "RESULT: n failed")
extends Node

const Form = preload("res://measurements_form.gd")

var fails := 0


func check(name: String, ok: bool) -> void:
	print(("PASS  " if ok else "FAIL  ") + name)
	if not ok:
		fails += 1


func _ready() -> void:
	var path := OS.get_user_data_dir().path_join("measurements_check/my_measurements.csv")
	OS.set_environment("FL_MEAS_CSV", path)
	DirAccess.remove_absolute(path)
	check("no file: nothing loaded", not Form.has_file() and Form.load_csv().is_empty())

	var m := {"height": 1.715, "bust": 0.93, "waist": 0.72, "hip": 1.01, "shoulder": 0.385, "arm": 0.6, "inseam": null,
		"muscle": 0.62, "cup": -0.2, "belly": 0.1}
	check("save", Form.save_csv(m) == OK and Form.has_file())
	var back := Form.load_csv()
	var same := true
	for k in m:
		if m[k] == null:
			same = same and not back.has(k)
		else:
			same = same and back.has(k) and absf(float(back[k]) - float(m[k])) < 0.0006
	check("round trip (lengths to 1 mm, body type to 1 %%, empty optional values stay empty): %s" % str(back), same)

	# a hand-edited file
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string("# my numbers\nmeasurement,value,unit\n Height , 165 ,cm\nwaist,300,cm\nbust,,cm\nshoe,38,eu\nmuscle,40,%\n")
	f.close()
	back = Form.load_csv()
	check("hand-edited: spaces, a clamped value, an empty row, an unknown row (%s)" % str(back),
		absf(float(back.get("height", 0)) - 1.65) < 1e-6 and absf(float(back.get("waist", 0)) - 1.30) < 1e-6
		and not back.has("bust") and not back.has("shoe") and absf(float(back.get("muscle", 0)) - 0.40) < 1e-6)

	# the form
	var got := {}
	var form := Form.new()
	form.open(self, m, func(_k: String) -> Variant: return 0.8, func(r: Dictionary) -> void: got.merge(r, true))
	await get_tree().process_frame
	var edits: Dictionary = form._edits
	check("form: a field per length, filled from the measurements", edits.size() == 11 and (edits["height"] as LineEdit).text == "171.5"
		and (edits["inseam"] as LineEdit).text == "" and (edits["inseam"] as LineEdit).placeholder_text == "80.0")
	(edits["waist"] as LineEdit).text = "20"
	form._apply()
	check("form: an out-of-range essential value is refused", got.is_empty() and form._err.visible)
	(edits["waist"] as LineEdit).text = "70,5"
	(edits["neck"] as LineEdit).text = ""
	form._apply()
	back = Form.load_csv()
	check("form: Use and save applies (comma decimal accepted, empty optional = auto) and writes the file (%s)" % str(got),
		absf(float(got.get("waist", 0)) - 0.705) < 1e-6 and got.has("neck") and got["neck"] == null
		and absf(float(back.get("waist", 0)) - 0.705) < 1e-6 and not back.has("neck"))
	DirAccess.remove_absolute(path)
	print("RESULT: %d failed" % fails)
	get_tree().quit(1 if fails > 0 else 0)
