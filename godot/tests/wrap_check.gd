# Verifies godot/wrap.gd against JS reference values (tools/export_godot_plans.js writes
# them with the real FL.makeWrap). Run headless:
#   godot --headless --path godot res://tests/wrap_check.tscn
# Prints WRAPCHECK max_err_mm and quits 0 below 0.001 mm, 1 above.
extends Node

const Wrap = preload("res://wrap.gd")

const PRESET := "average"
const TOL_MM := 0.001


func _load(path: String) -> Dictionary:
	var v = JSON.parse_string(FileAccess.get_file_as_string(path))
	return v if v is Dictionary else {}


func _ready() -> void:
	var data := _load("res://assets/%s/sections.json" % PRESET)
	var plan := _load("res://assets/%s/plan_teagan.json" % PRESET)
	var refs := _load("res://tests/wrap_ref_average.json")
	if data.is_empty() or plan.is_empty() or refs.is_empty():
		push_error("wrap_check: missing data files")
		get_tree().quit(1)
		return
	var by_id := {}
	for pc in plan["pieces"]:
		by_id[String(pc["id"])] = pc
	var worst := 0.0
	var n := 0
	for r in refs["refs"]:
		var wrap := Wrap.new(by_id[String(r["piece"])], data["sections"], data["arms"])
		var p: Vector3 = wrap.point(float(r["u"]), float(r["v"]))
		var q: Array = r["p"]
		var qq := Vector3(q[0], q[1], q[2])
		var e: float = qq.distance_to(p)
		if e * 1000.0 > 1.0:
			print("  OFF %s u=%.4f v=%.4f js=(%.4f,%.4f,%.4f) gd=(%.4f,%.4f,%.4f) err_mm=%.2f" % [r["piece"], float(r["u"]), float(r["v"]), qq.x, qq.y, qq.z, p.x, p.y, p.z, e * 1000.0])
		worst = maxf(worst, e)
		n += 1
	print("WRAPCHECK refs=%d max_err_mm=%.6f" % [n, worst * 1000.0])
	get_tree().quit(0 if worst * 1000.0 < TOL_MM else 1)
