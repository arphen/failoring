# Verifies godot/wrap.gd against JS reference values (tools/export_godot_plans.js writes
# them with the real FL.makeWrap, across all three garments). Run headless:
#   godot --headless --path godot res://tests/wrap_check.tscn            (average)
#   FL_PRESET=plus godot --headless --path godot res://tests/wrap_check.tscn
# Prints WRAPCHECK max_err_mm and quits 0 below 0.01 mm, 1 above. (Fit pieces usually
# match below 0.001 mm; leg/arm regimes accumulate a few microns of libm sin/cos
# difference. Anything structural - wrong regime, bad data - misses by centimetres.)
extends Node

const Wrap = preload("res://wrap.gd")

const GARMENTS := ["teagan", "penelope", "charlie"]
const TOL_MM := 0.01


func _load(path: String) -> Dictionary:
	var v = JSON.parse_string(FileAccess.get_file_as_string(path))
	return v if v is Dictionary else {}


func _ready() -> void:
	var preset := OS.get_environment("FL_PRESET")
	if preset == "":
		preset = "average"
	var data := _load("res://assets/%s/sections.json" % preset)
	var refs := _load("res://assets/%s/wrap_ref.json" % preset)
	if data.is_empty() or refs.is_empty():
		push_error("wrap_check: missing data files for " + preset)
		get_tree().quit(1)
		return
	var plans := {}
	for g in GARMENTS:
		var plan := _load("res://assets/%s/plan_%s.json" % [preset, g])
		if plan.is_empty():
			push_error("wrap_check: missing plan %s for %s" % [g, preset])
			get_tree().quit(1)
			return
		plans[g] = plan
	var worst := 0.0
	var n := 0
	var wraps := {}
	for r in refs["refs"]:
		var gk := "%s|%s" % [String(r["garment"]), String(r["piece"])]
		if not wraps.has(gk):
			var plan: Dictionary = plans[String(r["garment"])]
			var pc = null
			for q in plan["pieces"]:
				if String(q["id"]) == String(r["piece"]):
					pc = q
			if pc == null:
				push_error("wrap_check: piece %s not in plan" % String(r["piece"]))
				get_tree().quit(1)
				return
			wraps[gk] = Wrap.new(pc, data["sections"], data["arms"])
		var wrap = wraps[gk]
		var p: Vector3 = wrap.point(float(r["u"]), float(r["v"]))
		var q: Array = r["p"]
		var qq := Vector3(q[0], q[1], q[2])
		var e: float = qq.distance_to(p)
		if e * 1000.0 > 1.0:
			print("  OFF %s/%s u=%.4f v=%.4f err_mm=%.2f" % [preset, String(r["piece"]), float(r["u"]), float(r["v"]), e * 1000.0])
		worst = maxf(worst, e)
		n += 1
	print("WRAPCHECK %s refs=%d max_err_mm=%.6f" % [preset, n, worst * 1000.0])
	get_tree().quit(0 if worst * 1000.0 < TOL_MM else 1)
