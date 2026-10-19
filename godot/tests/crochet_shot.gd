# Development aid (needs a real renderer, not --headless): runs the crochet scene for some frames and saves a screenshot of the window.
#   FL_SHOT_PATTERN=pouch FL_SHOT_SCENE=hang_pin FL_SHOT_LOAD=none FL_SHOT_FRAMES=450 FL_SHOT_OUT=/tmp/x.png \
#     godot --path godot res://tests/crochet_shot.tscn
extends Node
func _ready() -> void:
	var c = (load("res://crochet/crochet.tscn") as PackedScene).instantiate()
	add_child(c)
	await get_tree().process_frame
	var pat := OS.get_environment("FL_SHOT_PATTERN") if OS.has_environment("FL_SHOT_PATTERN") else "pouch"
	c._load_fixture_named(pat)
	c.compile_now()
	if OS.has_environment("FL_SHOT_CALIBRATE"):   # the calibration wizard filled with the example numbers of plan section 10
		c.open_calibration()
		var ex := {"stitches_per_10cm": 14, "rows_per_10cm": 16, "mass_g": 9.0, "width_cm": 12, "height_cm": 12, "overhang_wale_mm": 52, "overhang_course_mm": 38,
			"wale_w1_g": 250, "wale_l1_mm": 104, "wale_w2_g": 1000, "wale_l2_mm": 108, "course_w1_g": 250, "course_l1_mm": 110, "course_w2_g": 1000, "course_l2_mm": 150}
		for k in ex.keys():
			c.calibrate_view.set_value(k, ex[k])
		if OS.has_environment("FL_SHOT_HINGE"):   # the hinge section: fit a bendScale of 0.3 from its simulated droop, scrolled into view
			var w = c.calibrate_view
			var droop: float = w._calib.hinge_droop("blo", 0.3, 0.0, "acrylic_worsted", 5.0, w.current_fit()["record"])
			w.set_value("hinge_rest_deg", 0)
			w.set_value("hinge_droop_deg", snappedf(droop, 0.1))
			w.fit_hinge_now()
			for i in 3:
				await get_tree().process_frame
			for sc in w.find_children("*", "ScrollContainer", true, false):
				sc.scroll_vertical = 100000
		for i in 6:
			await get_tree().process_frame
		var wout := OS.get_environment("FL_SHOT_OUT") if OS.has_environment("FL_SHOT_OUT") else "/tmp/crochet_shot.png"
		get_viewport().get_texture().get_image().save_png(wout)
		print("wrote ", wout, " wizard")
		get_tree().quit()
		return
	if OS.has_environment("FL_SHOT_YARN"):   # yarn mode: FL_SHOT_YARN="ch5,turn,sc4" (comma separated actions: chN, scN, turn), no animations
		var yv = c.yarn_view
		yv.visible = true
		yv.animate = false
		for tok in OS.get_environment("FL_SHOT_YARN").split(","):
			var kind := "turn" if tok == "turn" else ("chain" if tok.begins_with("ch") else "sc")
			var count := 1 if tok == "turn" else int(tok.substr(2))
			for i in count:
				await yv.do_action(kind)
		if OS.has_environment("FL_SHOT_YARN_EXPAND"):
			yv.expand_slider.value = float(OS.get_environment("FL_SHOT_YARN_EXPAND"))
		if OS.has_environment("FL_SHOT_YARN_THICK"):
			yv.thick_slider.value = float(OS.get_environment("FL_SHOT_YARN_THICK"))
		if OS.has_environment("FL_SHOT_YARN_FLIP"):   # the other side of the work, as if it were turned over by hand (no row is added)
			var pv: Vector3 = yv._live_centre()
			yv._set_flip(yv.flip + PI, pv, yv.work_root.to_global(pv))
			yv._frame_work(true)
		if OS.has_environment("FL_SHOT_YARN_YAW"):
			yv.orbit.yaw = float(OS.get_environment("FL_SHOT_YARN_YAW"))
		if OS.has_environment("FL_SHOT_YARN_DIST"):
			yv.orbit.dist = float(OS.get_environment("FL_SHOT_YARN_DIST"))
		if OS.has_environment("FL_SHOT_YARN_PITCH"):
			yv.orbit.pitch = float(OS.get_environment("FL_SHOT_YARN_PITCH"))
		yv.orbit.apply()
		for i in 8:
			await get_tree().process_frame
		var yout := OS.get_environment("FL_SHOT_OUT") if OS.has_environment("FL_SHOT_OUT") else "/tmp/crochet_shot.png"
		get_viewport().get_texture().get_image().save_png(yout)
		print("wrote ", yout, " yarn ", yv.session.get_particle_count(), " particles")
		get_tree().quit()
		return
	if OS.has_environment("FL_SHOT_COMPARE"):   # the compare view: FL_SHOT_COMPARE=variants|ab, FL_SHOT_FRAMES frames each
		var cv = c.compare_view
		cv.visible = true
		if OS.get_environment("FL_SHOT_COMPARE") == "variants":
			cv.setup_variants()
		else:
			cv.setup_ab()
		cv.run_sync(int(OS.get_environment("FL_SHOT_FRAMES")) if OS.has_environment("FL_SHOT_FRAMES") else 10)
		for i in 6:
			await get_tree().process_frame
		var cout := OS.get_environment("FL_SHOT_OUT") if OS.has_environment("FL_SHOT_OUT") else "/tmp/crochet_shot.png"
		get_viewport().get_texture().get_image().save_png(cout)
		print("wrote ", cout, " compare ", cv.slots.size(), " slots")
		get_tree().quit()
		return
	var scene := OS.get_environment("FL_SHOT_SCENE") if OS.has_environment("FL_SHOT_SCENE") else "hang_pin"
	var load_id := OS.get_environment("FL_SHOT_LOAD") if OS.has_environment("FL_SHOT_LOAD") else "none"
	if c.start_run(scene, load_id):
		var frames := int(OS.get_environment("FL_SHOT_FRAMES")) if OS.has_environment("FL_SHOT_FRAMES") else 450
		c.run_frames(frames)
		c._show_metrics()
	for i in 4:   # the caret events of loading the text arrive first; the selection comes after them
		await get_tree().process_frame
	if OS.has_environment("FL_SHOT_SELECT_LINE"):   # select the stitches of a pattern line: glow on the cloth, rings in the chart
		c.select_stitches(c._line_stitches.get(int(OS.get_environment("FL_SHOT_SELECT_LINE")), PackedInt32Array()), "line")
		if c.sim != null:
			c._update_mesh()
	# zoom the chart: FL_SHOT_CHART_FOCUS=<stitch id>, or FL_SHOT_CHART_ROW=<row> FL_SHOT_CHART_INDEX=<index in the row>; FL_SHOT_CHART_ZOOM=<factor>
	var focus := -1
	if OS.has_environment("FL_SHOT_CHART_FOCUS"):
		focus = int(OS.get_environment("FL_SHOT_CHART_FOCUS"))
	elif OS.has_environment("FL_SHOT_CHART_ROW"):
		for sy in c.chart_holder.symbols:
			if sy["row"] == int(OS.get_environment("FL_SHOT_CHART_ROW")) and sy["index"] == int(OS.get_environment("FL_SHOT_CHART_INDEX")):
				focus = sy["id"]
	if focus >= 0:
		c.chart_holder.focus(focus, float(OS.get_environment("FL_SHOT_CHART_ZOOM")) if OS.has_environment("FL_SHOT_CHART_ZOOM") else 10.0)
		c.chart_holder.select_stitch(focus)
	if OS.has_environment("FL_SHOT_MENU"):   # the chart's edit menu over the current selection
		c._on_chart_edit_requested(Vector2(60, 300))
	for i in 4:
		await get_tree().process_frame
	var out := OS.get_environment("FL_SHOT_OUT") if OS.has_environment("FL_SHOT_OUT") else "/tmp/crochet_shot.png"
	get_viewport().get_texture().get_image().save_png(out)
	print("wrote ", out, " phase ", c.sim.phase() if c.sim != null else "-")
	get_tree().quit()
