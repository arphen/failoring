# Checks the CrochetPattern / CrochetScene API (docs/CROCHET_PLAN.md section 8.1). Run headless:
#   godot --headless --path godot res://tests/crochet_api_check.tscn     (prints PASS/FAIL lines, then "RESULT: n failed")
# Needs node and a CrochetPARADE checkout (third_party/crochetparade): the compile step runs the pattern exporter.
extends Node3D
var fails := 0
func check(name: String, ok: bool) -> void:
	print(("PASS  " if ok else "FAIL  ") + name)
	if not ok: fails += 1
func _read(path: String) -> String:
	var f := FileAccess.open(path, FileAccess.READ)
	return f.get_as_text() if f != null else ""
func _ready() -> void:
	var root := ProjectSettings.globalize_path("res://") + "/.."
	var pouch := _read(root + "/patterns/cp/pouch.txt")
	check("pouch pattern text read", pouch.length() > 1000)
	var pat := CrochetPattern.new()
	check("compile the pouch", pat.compile(pouch, "res://materials/yarns.json"))
	check("no errors", pat.get_errors().size() == 0)
	var sum := pat.get_summary()
	check("summary: 1380 stitches, 133 rows, 55 links (%s)" % [sum.get("stitches", -1)], sum.get("stitches", -1) == 1380 and sum.get("rows", -1) == 133 and sum.get("links", -1) == 55)
	check("summary: 39.8 m of yarn, 22.1 g", abs(sum.get("yarn_m", 0.0) - 39.77) < 0.1 and abs(sum.get("mass_g", 0.0) - 22.09) < 0.1)
	check("summary: has a strap, folds after row 25", sum.get("has_strap", false) == true and sum.get("fold_after_row", -1) == 25)
	var chart := pat.get_chart()
	check("chart: one entry per stitch", chart.get("stitches", []).size() == 1380 and chart.get("rows", []).size() == 133)
	var s0: Dictionary = chart["stitches"][100]
	check("chart: a stitch entry has id, row, type, insertion, targets, x, y", s0.has_all(["id", "row", "index", "type", "insertion", "facing", "targets", "x", "y", "line"]) and s0["id"] == 100)
	check("yarns listed", pat.get_yarn_ids().size() >= 3 and pat.get_yarn_info("acrylic_worsted").get("diameter_mm", 0.0) > 2.0)
	# errors: a bad pattern is reported with a line number
	var bad := CrochetPattern.new()
	check("a malformed pattern does not compile", not bad.compile("ch,sc,xyz@[3,4]\n", "res://materials/yarns.json"))
	var errs := bad.get_errors()
	check("...and reports an error with a message", errs.size() >= 1 and str(errs[0].get("msg", "")).length() > 0)
	# the scene
	var sc := CrochetScene.new()
	check("build hang_pin without a load", sc.build(pat, "hang_pin", "none"))
	check("starts in the relax phase", sc.phase() == "relax")
	var frames := 0
	while frames < 200:
		sc.step(10)
		frames += 10
	var pos := sc.get_positions()
	var finite := true
	for p in pos:
		if is_nan(p.x) or is_nan(p.y) or is_nan(p.z) or is_inf(p.x) or is_inf(p.y) or is_inf(p.z):
			finite = false
			break
	check("200 frames run: %d vertices, all finite" % [pos.size()], pos.size() == 1250 and finite)
	check("phase moved on from relax (%s)" % [sc.phase()], sc.phase() == "hang")
	check("indices: 2204 triangles", sc.get_indices().size() == 2204 * 3)
	check("normals and stitch ids per vertex", sc.get_normals().size() == 1250 and sc.get_stitch_of_vertex().size() == 1250)
	check("vertex uv, colours and thickness for the shader", sc.get_vertex_uv().size() == 1250 and sc.get_vertex_colors().size() == 1250 and abs(sc.get_thickness() - 0.00475) < 0.0005)
	var colr := sc.get_vertex_colors()
	check("vertex colours are in 0..1 and carry the RS facing (row 1 is RS)", colr[0].r > 0.0 and colr[0].r <= 1.0 and colr[0].b == 1.0)
	var shader := load("res://crochet/crochet_surface.gdshader") as Shader
	check("the surface shader loads and declares its inputs", shader != null and shader.code.contains("shader_type spatial") and shader.code.contains("uniform vec4 albedo") and shader.code.contains("COLOR.r"))
	var m := sc.get_metrics()
	check("metrics: all 31 keys", m.size() == 31 and m.has("yarn_m") and m.has("drop_cm") and m.has("strain_wale_max") and m.has("final_kinetic_energy"))
	check("metrics: numbers that do not apply are null (no load)", m["fits"] == null and m["corner_angle_deg"] == null and m["drop_cm"] != null)
	check("no error on the scene", sc.get_error() == "")
	check("a scene without a load has no load transform data", sc.get_load().size() == 0 and sc.get_peg().size() == 0)
	# rejected requests
	var bad_scene := CrochetScene.new()
	check("hang_pin on an uncompiled pattern is rejected", not bad_scene.build(CrochetPattern.new(), "hang_pin", "none"))
	check("an unknown load is rejected with a message", not bad_scene.build(pat, "hang_pin", "laptop") and bad_scene.get_error().length() > 0)
	print("RESULT: %d failed" % fails)
	get_tree().quit(1 if fails > 0 else 0)
