# Checks the crochet scene (docs/CROCHET_PLAN.md section 8.2): it loads, compiles the bundled pouch, reports errors for a bad
# pattern, runs the simulation and fills the view and the labels. Run headless:
#   godot --headless --path godot res://tests/crochet_scene_check.tscn     (prints PASS/FAIL lines, then "RESULT: n failed")
extends Node
var fails := 0
func check(name: String, ok: bool) -> void:
	print(("PASS  " if ok else "FAIL  ") + name)
	if not ok: fails += 1
func _ready() -> void:
	var packed := load("res://crochet/crochet.tscn") as PackedScene
	check("crochet.tscn loads", packed != null)
	var c = packed.instantiate()
	add_child(c)
	await get_tree().process_frame
	check("the pouch compiled on start: %s" % [c.summary_text()], c.summary_text().begins_with("1380 stitches") and c.error_text() == "")
	check("summary has yarn and size", c.summary_text().contains("m of yarn") and c.summary_text().contains("layout"))
	check("the editor shows the pattern text", c.editor.text.contains("16ch,turn") and c.editor.get_line_count() > 100)
	check("the run button is enabled", not c.run_btn.disabled)
	# a bad pattern: the error comes with its line, the run is blocked
	c.load_text("ch,sc,xyz@[3,4]\n")
	check("a bad pattern reports an error (%s)" % [c.error_text()], c.error_text().length() > 0 and c.summary_text() == "Does not compile")
	check("...and the run button is disabled", c.run_btn.disabled)
	check("start_run refuses without a compiled pattern", not c.start_run("hang_pin", "none"))
	# back to the pouch and run
	c._load_fixture_named("pouch")
	check("the pouch compiles again", c.summary_text().begins_with("1380 stitches") and c.error_text() == "")
	check("start_run hang_pin", c.start_run("hang_pin", "none"))
	c.run_frames(200)
	check("200 frames: 1250 vertices in the view", c.mesh_vertex_count() == 1250)
	var finite := true
	var top := -1e9
	for p in c.last_vertices:
		if is_nan(p.x) or is_nan(p.y) or is_nan(p.z) or abs(p.y) > 100.0:
			finite = false
			break
		top = maxf(top, p.y)
	check("the mesh vertices are finite and in the studio's range", finite and c.last_vertices.size() == 1250)
	check("the phase label shows the phase (%s)" % [c.phase_label.text], c.phase_label.text.begins_with("hang"))
	check("the pouch is shown at the hang height: its highest point is at %.2f m (want %.2f)" % [top, c.HANG_Y], abs(top - c.HANG_Y) < 0.05)
	check("the run is still going (hang not settled yet)", c._running)
	c._stop_requested = true
	c.run_frames(1)
	print("RESULT: %d failed" % fails)
	get_tree().quit(1 if fails > 0 else 0)
