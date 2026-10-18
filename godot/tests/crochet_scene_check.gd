# Checks the crochet scene (docs/CROCHET_PLAN.md section 8.2): it loads, compiles the bundled pouch, reports errors for a bad
# pattern, runs the simulation and fills the view and the labels. Run headless:
#   godot --headless --path godot res://tests/crochet_scene_check.tscn     (prints PASS/FAIL lines, then "RESULT: n failed")
extends Node
var fails := 0
func check(name: String, ok: bool) -> void:
	print(("PASS  " if ok else "FAIL  ") + name)
	if not ok: fails += 1
func _pouch_with_load(c) -> bool:
	c._load_fixture_named("pouch")
	var ok: bool = c.start_run("hang_pin", "mba13")
	c.run_frames(1)
	c._stop_requested = true
	c.run_frames(1)
	return ok and c.sim != null and c.sim.get_load().size() == 0 and c.error_text() == ""

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
	# every control once (plan T7.3): yarn, hook, tension, the bundled-patterns menu, scene and load menus, the camera buttons
	c._load_fixture_named("pouch")
	var yarn_m_before := float(c.pattern.get_summary()["yarn_m"])
	var cotton := -1
	for i in c.yarn_opt.item_count:
		if String(c.yarn_opt.get_item_metadata(i)) == "cotton_worsted":
			cotton = i
	c.yarn_opt.select(cotton)
	c.yarn_opt.item_selected.emit(cotton)
	c.compile_now()
	check("yarn menu: cotton worsted changes the yarn length (%.1f m -> %.1f m)" % [yarn_m_before, float(c.pattern.get_summary()["yarn_m"])], abs(float(c.pattern.get_summary()["yarn_m"]) - yarn_m_before) > 0.5)
	c.yarn_opt.select(0)
	var y2 := float(c.pattern.get_summary()["yarn_m"])
	c.hook_spin.value = 8.0
	c.compile_now()
	check("hook field: an 8 mm hook gives bigger stitches, so more yarn (%.1f m)" % [float(c.pattern.get_summary()["yarn_m"])], float(c.pattern.get_summary()["yarn_m"]) > y2 or abs(float(c.pattern.get_summary()["yarn_m"]) - y2) > 0.5)
	c.hook_spin.value = 0.0
	var before_t := float(c.pattern.get_summary()["yarn_m"])
	c.tension_spin.value = 1.1
	c.compile_now()
	check("tension field changes the model (%.1f m -> %.1f m)" % [before_t, float(c.pattern.get_summary()["yarn_m"])], abs(float(c.pattern.get_summary()["yarn_m"]) - before_t) > 0.1)
	c.tension_spin.value = 1.0
	var circle_idx := -1
	for i in c.fixture_opt.item_count:
		if c.fixture_opt.get_item_text(i) == "circle_plus6":
			circle_idx = i
	c.fixture_opt.select(circle_idx)
	c.fixture_opt.item_selected.emit(circle_idx)
	check("bundled-patterns menu loads circle_plus6 (%s)" % [c.summary_text()], c.summary_text().begins_with("217 stitches"))
	check("scene menu: hang from hook = hang_peg, hang from hand = hang_pin", c.scene_opt.get_item_metadata(0) == "hang_peg" and c.scene_opt.get_item_metadata(1) == "hang_pin")
	check("load menu: 5 entries, none first, MBA13 second", c.load_opt.item_count == 5 and c.load_opt.get_item_metadata(0) == "none" and c.load_opt.get_item_metadata(1) == "mba13")
	check("a circle cannot hang (no strap): the run is refused with a message", not c.start_run("hang_pin", "none") and c.error_text().contains("strap"))
	var yaw_before: float = c._yaw
	for b in c.find_children("*", "Button", true, false):
		if (b as Button).text == "Side":
			(b as Button).pressed.emit()
	check("camera buttons move the camera (yaw %.2f -> %.2f)" % [yaw_before, c._yaw], abs(c._yaw - yaw_before) > 0.1)
	check("Open and Save as exist and open dialogs", c.find_children("*", "Button", true, false).any(func(b): return (b as Button).text == "Open") and c.find_children("*", "Button", true, false).any(func(b): return (b as Button).text == "Save as"))
	var ed_src := FileAccess.get_file_as_string("res://editor.gd")
	check("the editor has the Crochet button that opens res://crochet/crochet.tscn", ed_src.contains("CrochetBtn") and ed_src.contains("res://crochet/crochet.tscn") and load("res://editor.gd") != null)
	check("the load option reaches the scene: pouch + MBA13 builds", _pouch_with_load(c))
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
