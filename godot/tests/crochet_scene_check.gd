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

func _lines_with(ch, line: int) -> PackedInt32Array:
	var out := PackedInt32Array()
	for sy in ch.symbols:
		if int(sy["line"]) == line:
			out.push_back(int(sy["id"]))
	return out

# selection (plan T8.2) without a run: the chart and the pattern line follow each other
func _line_checks(c) -> void:
	var ch = c.chart_holder
	ch.size = Vector2(400, 600)
	var id := 100
	while ch.symbols[id]["type"] != "sc":
		id += 1
	var line := int(ch.symbols[id]["line"])
	var mb := InputEventMouseButton.new()
	mb.button_index = MOUSE_BUTTON_LEFT
	mb.pressed = true
	mb.position = ch.stitch_screen_pos(id)
	ch._gui_input(mb)
	check("select: a click in the chart selects the stitch and puts the caret on its line (%d)" % [c.editor.get_caret_line() + 1], c.selection == PackedInt32Array([id]) and c.editor.get_caret_line() + 1 == line and c.editor.get_selected_text().contains("sc.R6"))
	await get_tree().process_frame
	await get_tree().process_frame
	check("select: the caret event of that move does not widen the selection to the whole line", c.selection == PackedInt32Array([id]) and ch.highlight == PackedInt32Array([id]))
	c.editor.set_caret_line(9)
	await get_tree().process_frame
	await get_tree().process_frame
	check("select: a caret on line 10 selects line 10's stitches (%d)" % c.selection.size(), c.selection.size() > 10 and c.selection == _lines_with(ch, 10))
	c.editor.set_caret_line(12)
	await get_tree().process_frame
	await get_tree().process_frame
	var l13 := _lines_with(ch, 13)
	check("select: a caret on line 13 selects its %d stitches in the chart too (%s)" % [l13.size(), ch._info.text], c.selection == l13 and ch.highlight == l13 and ch._info.text.contains("line 13"))
	c.editor.set_caret_line(0)
	await get_tree().process_frame
	await get_tree().process_frame
	check("select: a comment line holds no stitches, so the selection is empty", c.selection.size() == 0 and ch.highlight.size() == 0)
	mb.position = ch.stitch_screen_pos(id)
	ch._gui_input(mb)
	mb.position = Vector2(-300, -300)
	ch._gui_input(mb)
	check("select: a click on empty chart clears the selection", c.selection.size() == 0 and ch.highlight.size() == 0 and ch.selected == -1)

# selection with a run: the stitch glows on the cloth, a ray finds the stitch (plan T8.2)
func _scene_selection_checks(c) -> void:
	var ch = c.chart_holder
	var id := 100
	while ch.symbols[id]["type"] != "sc":
		id += 1
	c.select_stitches(PackedInt32Array([id]), "chart")
	var v: int = c._vertex_of_stitch[id]
	check("glow: the selected stitch maps to one cloth vertex (%d) that belongs to it" % v, v >= 0 and c.highlight_vertices() == PackedInt32Array([v]) and c.sim.get_stitch_of_vertex()[v] == id)
	var plain: PackedColorArray = c.sim.get_vertex_colors()
	var lit: PackedColorArray = c.highlight_colours(plain)
	var dark := 0
	var same := true
	for i in lit.size():
		if lit[i].a == 0.0:
			dark += 1
		same = same and lit[i].r == plain[i].r and lit[i].g == plain[i].g and lit[i].b == plain[i].b
	check("glow: alpha 0 on that vertex only (%d), colours otherwise unchanged" % dark, dark == 1 and lit[v].a == 0.0 and same and plain[v].a == 1.0)
	var l13 := _lines_with(ch, 13)
	c.select_stitches(l13, "line")
	var mesh_ids := 0
	for sid in l13:
		if c._vertex_of_stitch[sid] >= 0:
			mesh_ids += 1
	check("glow: a pattern line lights every stitch of it that is in the mesh (%d of %d)" % [mesh_ids, l13.size()], mesh_ids > 10 and c.highlight_vertices().size() == mesh_ids)
	var row3 := -1
	for sy in ch.symbols:
		if sy["row"] == 3 and sy["index"] == 5:
			row3 = sy["id"]
	var p3: Vector3 = c.last_vertices[c._vertex_of_stitch[row3]]
	check("pick: a ray from the front through a row 3 vertex finds its stitch (%d)" % row3, c.pick_ray(p3 + Vector3(0, 0, 1), Vector3(0, 0, -1)) == row3)
	check("pick: a ray that misses the cloth finds nothing", c.pick_ray(Vector3(0, 0, 5), Vector3(0, 0, 1)) == -1)
	c._yaw = 0.0   # an earlier check left the camera on the side view, edge-on to the flat pouch
	c._pitch = 0.0
	c._apply_camera()
	var at: Vector2 = c.cam.unproject_position(p3)
	var got: int = c.pick_at(at)
	check("pick: a click on the point where the camera sees that vertex selects it (%d, selection %s)" % [got, c.selection], got == row3 and c.selection == PackedInt32Array([row3]) and c.editor.get_caret_line() + 1 == int(ch.symbols[row3]["line"]))
	c.pick_at(Vector2(-2000, -2000))
	check("pick: a click beside the cloth clears the selection", c.selection.size() == 0 and c.highlight_vertices().size() == 0)

# chart view (plan T8.1): one symbol per stitch, placed with row 1 at the bottom, hover/click/zoom
func _chart_checks(c) -> void:
	var ch = c.chart_holder
	check("chart: one symbol per stitch for the pouch (%d)" % ch.symbol_count(), ch.symbol_count() == 1380 and ch.symbol_count() == int(c.pattern.get_summary()["stitches"]))
	var drawable := true
	var finite := true
	var y_row := {}
	var n_row := {}
	for sy in ch.symbols:
		drawable = drawable and ["ch", "slst", "sc", "hdc", "dc", "tr"].has(sy["type"])
		finite = finite and is_finite(sy["pos"].x) and is_finite(sy["pos"].y)
		y_row[sy["row"]] = float(y_row.get(sy["row"], 0.0)) + sy["pos"].y
		n_row[sy["row"]] = int(n_row.get(sy["row"], 0)) + 1
	check("chart: every symbol is a stitch type the chart can draw, at a finite position", drawable and finite)
	check("chart: row 1 is below row 5 (y up, row 1 at the bottom)", y_row[1] / n_row[1] < y_row[5] / n_row[5])
	var y1: float = y_row[1] / n_row[1]
	var y50: float = y_row[50] / n_row[50]
	var y51: float = y_row[51] / n_row[51]
	check("chart: the folded body is opened out: 50 rows are 49 units tall (%.1f), the border is the next row up (%.1f)" % [y50 - y1, y51 - y50], abs((y50 - y1) - 49.0) < 3.0 and abs((y51 - y50) - 1.0) < 1.5)
	ch.size = Vector2(400, 600)
	var id := 100
	while ch.symbols[id]["type"] != "sc":
		id += 1
	var at: Vector2 = ch.stitch_screen_pos(id)
	check("chart: the point of stitch %d hits stitch %d" % [id, id], ch.stitch_at(at) == id and ch.stitch_at(Vector2(-500, -500)) == -1)
	var hovered := []
	ch.stitch_hovered.connect(func(i): hovered.append(i))
	var picked := []
	ch.stitch_selected.connect(func(i): picked.append(i))
	var mm := InputEventMouseMotion.new()
	mm.position = at
	ch._gui_input(mm)
	var line := int(ch.symbols[id]["line"])
	check("chart: hover reads out the stitch and its pattern line (%s)" % ch._info.text, ch.hover == id and hovered == [id] and ch._info.text.contains("stitch %d" % id) and ch._info.text.contains("line %d" % line))
	var row := int(ch.symbols[id]["row"])
	var text: String = c.editor.get_line(line - 1)
	check("chart: that line of the pattern text is the stitch's own row (%s)" % [text], text.contains("sc.R%d," % row) or text.contains("sc.L%d," % row))
	check("chart: a WS stitch is drawn in the WS colour, an RS stitch in the RS colour (%s %s)" % [ch.symbols[id]["facing"], ch._colour(ch.symbols[id])], ch.symbols[id]["facing"] == "WS" and ch._colour(ch.symbols[id]) == ch.COL_WS and ch._colour(ch.symbols[16 + 5]) == ch.COL_RS)
	var mb := InputEventMouseButton.new()
	mb.button_index = MOUSE_BUTTON_LEFT
	mb.pressed = true
	mb.position = at
	ch._gui_input(mb)
	check("chart: a click selects the stitch and tells the owner", ch.selected == id and picked == [id])
	var wheel := InputEventMouseButton.new()
	wheel.button_index = MOUSE_BUTTON_WHEEL_UP
	wheel.pressed = true
	wheel.position = at
	ch._gui_input(wheel)
	var zoomed: float = ch._zoom
	check("chart: the wheel zooms in about the pointer (the stitch stays under it)", zoomed > 1.0 and ch.stitch_screen_pos(id).distance_to(at) < 1.0)
	ch.fit()
	check("chart: fit resets the zoom", ch._zoom == 1.0)

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
	_chart_checks(c)
	await _line_checks(c)
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
	check("chart: the new pattern replaces the chart (%d symbols) and clears the selection" % c.chart_holder.symbol_count(), c.chart_holder.symbol_count() == 217 and c.chart_holder.selected == -1)
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
	_scene_selection_checks(c)
	c._stop_requested = true
	c.run_frames(1)
	print("RESULT: %d failed" % fails)
	get_tree().quit(1 if fails > 0 else 0)
