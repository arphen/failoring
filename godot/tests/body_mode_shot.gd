# Body mode (body_mode.gd), scripted with a window (screenshots into FL_SHOTDIR): entered, a few measurements focused (the
# tape unrolls), one taken (the reading glows, the file is written), guided measuring started, the Shape and Look pages.
# Writes the measurements to FL_MEAS_CSV (set it to a scratch file: the default is the user's own file).
#   FL_MEAS_CSV=/tmp/m.csv FL_SHOTDIR=/tmp/x godot --path godot res://tests/body_mode_shot.tscn
extends Node

var ed: Node
var dir := ""


func _shot(name: String) -> void:
	RenderingServer.force_draw(false)
	RenderingServer.force_draw(false)
	var img := get_viewport().get_texture().get_image()
	img.save_png("%s/%s.png" % [dir, name])
	print("BODY shot %s | %s" % [name, ed.status_label.text.replace("\n", " / ")])


func _wait(sec: float) -> void:
	await get_tree().create_timer(sec).timeout


func _ready() -> void:
	dir = OS.get_environment("FL_SHOTDIR")
	if not OS.has_environment("FL_MEAS_CSV"):
		print("BODY set FL_MEAS_CSV to a scratch file")
		get_tree().quit(1)
		return
	get_window().always_on_top = true
	ed = load("res://editor.tscn").instantiate()
	add_child(ed)
	while ed.sim == null:
		await get_tree().process_frame
	await _wait(1.5)
	ed._set_body_mode(true)
	await _wait(2.0)
	print("BODY paths: %s" % str(ed.body_mode.paths.keys()))
	for k in ed.body_mode.paths:
		var p: Dictionary = ed.body_mode.paths[k]
		if p.is_empty():
			print("BODY %s: no tape" % k)
			continue
		print("BODY %s: tape %.1f cm, value %.1f cm, %d points" % [k, float(p["length"]) * 100.0, ed.body_mode.value_cm(k), (p["pts"] as PackedVector3Array).size()])
	await _shot("0_enter")
	var keys := OS.get_environment("FL_BODY_KEYS").split(",") if OS.has_environment("FL_BODY_KEYS") else PackedStringArray(["bust", "waist", "shoulder", "arm", "inseam", "height"])
	for k in keys:
		ed.body_mode.focus(k)
		await _wait(0.6)
		await _shot("1_%s_unrolling" % k)
		await _wait(2.4)
		await _shot("1_%s" % k)
	# take one: the reading glows, the file has it
	ed.body_mode.focus("waist")
	await _wait(2.5)
	ed.body_mode._spin.value = ed.body_mode.value_cm("waist") + 4.0
	ed.body_mode.confirm()
	await _wait(0.4)
	await _shot("2_waist_taken_glow")
	await _wait(2.0)
	await _shot("2_waist_after")
	print("BODY file: %s" % str(ed.body_mode.mine.keys()))
	ed.body_mode.start_guided()
	await _wait(3.0)
	await _shot("3_guided")
	ed.body_mode.set_page("shape")
	await _wait(4.0)
	await _shot("4_shape")
	ed.body_mode.set_page("look")
	await _wait(2.5)
	await _shot("5_look")
	ed.body_mode._try_look("hair", "blond")
	await _wait(1.0)
	await _shot("5_look_try_blond")
	ed.body_mode._untry_look()
	ed._set_body_mode(false)
	await _wait(1.5)
	await _shot("6_left")
	get_tree().quit()
