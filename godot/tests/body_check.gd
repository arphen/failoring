# Verifies godot/body_gen.gd against JS reference values (tools/export_godot_body.js).
# Run headless (takes a while: full generate per preset in GDScript):
#   godot --headless --path godot res://tests/body_check.tscn
#   FL_PRESET=average godot --headless --path godot res://tests/body_check.tscn
# Prints BODYCHECK lines and quits 0 when every measurement matches within 0.5 mm
# (and kept joints/landmarks within 1 mm), 1 otherwise.
extends Node

const BodyGen = preload("res://body_gen.gd")
const BodyView = preload("res://scripts/body_loader.gd")


func _load(path: String):
	var v = JSON.parse_string(FileAccess.get_file_as_string(path))
	return v


func _ready() -> void:
	var only := OS.get_environment("FL_PRESET")
	var t0 := Time.get_ticks_msec()
	var data = _load("res://assets/body_data.json")
	if not (data is Dictionary) or not (data as Dictionary).has("base"):
		push_error("body_check: missing body_data.json")
		get_tree().quit(1)
		return
	var m := BodyGen.load_data(data)
	print("BODYCHECK asset loaded in %d ms" % (Time.get_ticks_msec() - t0))
	var ref = _load("res://assets/body_ref.json")
	var fails := 0
	var count := 0
	for preset in (ref["presets"] as Dictionary).keys():
		if only != "" and preset != only:
			continue
		var t1 := Time.get_ticks_msec()
		var body := BodyGen.generate(m, BodyGen.PRESETS[preset], null)
		var stand := BodyGen.stand(m, body)
		var want: Dictionary = ref["presets"][preset]
		var worst := 0.0
		for k in (want["m"] as Dictionary).keys():
			var got: float = float((stand["m"] as Dictionary).get(k, 0.0)) * 1000.0
			worst = maxf(worst, absf(got - float((want["m"] as Dictionary)[k])))
		var worst_res := 0.0
		for k in (want["residual"] as Dictionary).keys():
			var got: float = float((stand["residual"] as Dictionary).get(k, 0.0)) * 1000.0
			worst_res = maxf(worst_res, absf(got - float((want["residual"] as Dictionary)[k])))
		var worst_keep := 0.0
		for k in (want["keep"] as Dictionary).keys():
			var a: Array = (want["keep"] as Dictionary)[k]
			var b: Array = (stand["joints"] as Dictionary).get(k.substr(6), (stand["landmarks"] as Dictionary).get(k.substr(9), [0.0, 0.0, 0.0]))
			worst_keep = maxf(worst_keep, Vector3(a[0], a[1], a[2]).distance_to(Vector3(b[0], b[1], b[2])) * 1000.0)
		var ok: bool = worst < 0.5 and worst_res < 0.5 and worst_keep < 1.0
		if not ok:
			fails += 1
		count += 1
		print("BODYCHECK %s meas %.3fmm resid %.3fmm keep %.3fmm iters %d %dms %s" % [preset, worst, worst_res, worst_keep, int(stand.get("iters", body.get("iters", 0))), Time.get_ticks_msec() - t1, "OK" if ok else "FAIL"])
		# walk poses vs the baked sheet (exact frame phases need no interpolation)
		var obj = BodyView.parse_obj("res://assets/%s/body.obj" % preset)
		var wk = BodyView.parse_walk("res://assets/%s/walk.bin" % preset, obj["vertices"])
		var wworst := 0.0
		for ph in [0.0, 0.25, 0.5]:
			var live = BodyGen.pose(m, body, ph, {"stride": 1.0, "arms": 1.0})
			var fr: PackedVector3Array = wk["frames"][int(ph * 24.0)]
			var lp: PackedFloat32Array = live["pos"]
			for i in int(m["nb"]):
				wworst = maxf(wworst, Vector3(lp[3 * i], lp[3 * i + 1], lp[3 * i + 2]).distance_to(fr[i]))
		print("BODYCHECK %s walk %.3fmm %s" % [preset, wworst * 1000.0, "OK" if wworst * 1000.0 < 0.5 else "FAIL"])
		if wworst * 1000.0 >= 0.5:
			fails += 1
	print("BODYCHECK %d presets, %d failed" % [count, fails])
	get_tree().quit(0 if fails == 0 else 1)
