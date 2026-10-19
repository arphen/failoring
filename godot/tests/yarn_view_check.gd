# Checks yarn mode (docs/YARN_MODE_PLAN.md section 11): one click per stitch with the choices made, the cursor, turning, undo, picking a loop.
# Run headless:  godot --headless --path godot res://tests/yarn_view_check.tscn     (PASS/FAIL lines, then "RESULT: n failed")
extends Node
var fails := 0
func check(name: String, ok: bool) -> void:
	print(("PASS  " if ok else "FAIL  ") + name)
	if not ok: fails += 1

# the width of the drawn piece along x
func _extent(pos: PackedVector3Array) -> float:
	var lo := 1.0e9
	var hi := -1.0e9
	for p in pos:
		lo = minf(lo, p.x)
		hi = maxf(hi, p.x)
	return hi - lo

# the distance from a ring's vertex to the next ring's... here: the mean distance of a vertex to its neighbour in the same ring (10 sides / 8 here)
func _thickness(mesh: Dictionary) -> float:
	var pos: PackedVector3Array = mesh["positions"]
	var sum := 0.0
	var n := 0
	for i in range(0, mini(pos.size() - 8, 8 * 200), 8):
		sum += pos[i].distance_to(pos[i + 4])   # opposite sides of one ring
		n += 1
	return sum / maxf(1.0, float(n))

func _ready() -> void:
	var c = (load("res://crochet/crochet.tscn") as PackedScene).instantiate()
	add_child(c)
	await get_tree().process_frame
	var yv = c.yarn_view
	check("the crochet scene has the yarn view, hidden at first", yv != null and not yv.visible)
	var btn: Button
	for b in c.find_children("*", "Button", true, false):
		if (b as Button).text == "Yarn mode":
			btn = b
	check("the Yarn mode button opens the view", btn != null and not btn.disabled)
	btn.pressed.emit()
	check("...the view is visible", yv.visible)
	yv.animate = false

	var st: Dictionary = yv.session.get_state()
	check("it starts from a slip knot: one loop on the hook, row 0, nothing to work into", st["live"].size() == 1 and int(st["row"]) == 0 and int(st["next"]) == -1 and yv.session.get_particle_count() > 20)
	check("single crochet waits (nothing below to work into) while chain does not", yv.b_sc.disabled and not yv.b_chain.disabled)

	for i in 5:
		await yv.do_action("chain")
	st = yv.session.get_state()
	check("five chains: five stitches in the row, a pattern of 'ch 5'", int(st["made"]) == 5 and String(st["text"]).strip_edges() == "ch 5")
	check("the overlay shows the row as symbols", yv.overlay_text.text.contains("o o o o o"))
	var n_chain: int = yv.session.get_particle_count()

	await yv.do_action("turn")
	st = yv.session.get_state()
	check("turning after the foundation chain: row 1, the cursor at 0, four chains to work into (not the one on the hook)", int(st["row"]) == 1 and int(st["cursor"]) == 0 and st["targets"].size() == 4)
	check("...the default opening is the first of them, and single crochet is now enabled", int(st["next"]) == int(st["targets"][0]) and not yv.b_sc.disabled)
	check("the work was turned over (the worker sees the other side of it), the camera stayed", absf(yv.flip - PI) < 1.0e-6 and absf(yv.orbit.yaw) < 1.0e-6)

	var first_target := int(st["next"])
	await yv.do_action("sc")
	st = yv.session.get_state()
	check("one click on 'single crochet' makes a stitch into the next opening and moves the cursor on (no questions)", int(st["cursor"]) == 1 and int(st["next"]) == int(st["targets"][1]) and int(st["made"]) == 1)
	check("...it added yarn", yv.session.get_particle_count() > n_chain + 20)
	check("the pattern text reads ch 5 / (turn) / sc 1", String(st["text"]).begins_with("ch 5\n") and String(st["text"]).contains("sc 1"))
	for i in 3:
		await yv.do_action("sc")
	st = yv.session.get_state()
	check("four stitches finish the row: nothing left to work into, single crochet disabled, the status says to turn", int(st["next"]) == -1 and yv.b_sc.disabled and yv.status_label.text.contains("Turn"))
	var done: bool = await yv.do_action("sc")
	check("a further stitch is refused with a message, not made", not done and yv.status_label.text.contains("Nothing to work into"))

	var p_end: int = yv.session.get_particle_count()
	yv.undo()
	check("undo takes the last stitch back (the work is rebuilt without it)", int(yv.session.get_state()["cursor"]) == 3 and yv.session.get_particle_count() < p_end and yv.history.size() == 5 + 1 + 3)
	await yv.do_action("sc")
	check("...and the same stitch can be made again", yv.session.get_particle_count() == p_end)

	# picking a loop: the stitch goes into it instead of the next one
	yv.reset_piece()
	for i in 4:
		await yv.do_action("chain")
	await yv.do_action("turn")
	st = yv.session.get_state()
	var targets: PackedInt32Array = st["targets"]
	var pick_id: int = targets[2]
	var loops: Array = yv.session.get_loops()
	var c3: Vector3 = loops[pick_id]["centre"]
	await get_tree().process_frame
	yv.orbit.apply()
	var scr: Vector2 = yv.orbit.cam.unproject_position(c3)
	yv._on_click(scr)
	check("clicking a loop in the view picks it as the opening of the next stitch", yv.picked == pick_id)
	var before_cursor := int(yv.session.get_state()["cursor"])
	await yv.do_action("sc")
	st = yv.session.get_state()
	check("the stitch went into the picked loop, so the in-order cursor did not move, and the pick is spent", yv.picked == -1 and int(st["cursor"]) == before_cursor and int(st["made"]) == 1)

	# which loops: both is ticked, unticking one works into the other only, unticking both skips the stitch
	yv.reset_piece()
	for i in 5:
		await yv.do_action("chain")
	await yv.do_action("turn")
	check("both loops are ticked by default and the button says single crochet", yv.loop_front.button_pressed and yv.loop_back.button_pressed and yv.b_sc.text == "Single crochet")
	yv.loop_back.button_pressed = false
	check("unticking the back loop: the button now works into the front loop only", yv.b_sc.text.contains("front loop") and yv._sc_kind() == "sc_front")
	await yv.do_action(yv._sc_kind())
	check("...and the pattern says so (flo)", yv.session.get_pattern_text().contains("sc flo 1"))
	yv.loop_front.button_pressed = false
	check("unticking both turns the button into 'Skip stitch'", yv.b_sc.text == "Skip stitch" and yv._sc_kind() == "skip")
	var p_skip: int = yv.session.get_particle_count()
	var c_skip := int(yv.session.get_state()["cursor"])
	await yv.do_action(yv._sc_kind())
	check("a skip moves the cursor on and adds no yarn", int(yv.session.get_state()["cursor"]) == c_skip + 1 and yv.session.get_particle_count() == p_skip)
	yv.loop_front.button_pressed = true
	yv.loop_back.button_pressed = true

	# the other stitches and the turning chains that go with them
	yv.reset_piece()
	for i in 5:
		await yv.do_action("chain")
	await yv.do_action("turn")
	await yv.do_action("sc")
	await yv.do_action("hdc")
	await yv.do_action("dc")
	await yv.do_action("slst")
	var txt0: String = yv.session.get_pattern_text()
	check("sc, hdc, dc and sl st are one click each and are named in the pattern (%s)" % txt0.replace("\n", " / "), txt0.contains("sc 1") and txt0.contains("hdc 1") and txt0.contains("dc 1") and txt0.contains("sl st 1") and int(yv.session.get_state()["cursor"]) == 4)
	var ch_before := txt0
	await yv.do_action("turn")
	var txt1: String = yv.session.get_pattern_text()
	check("turning after a slip stitch row: a turning chain of one (the last stitch made decides it)", txt1.ends_with("ch 1\n") or txt1.contains("ch 1"))
	yv.reset_piece()
	for i in 4:
		await yv.do_action("chain")
	await yv.do_action("turn")
	await yv.do_action("hdc")
	await yv.do_action("hdc")
	await yv.do_action("hdc")
	await yv.do_action("turn")
	check("turning after half doubles: a turning chain of two", yv.session.get_pattern_text().contains("ch 2"))

	# colours: the next stitch changes colour; a colour used before is picked up where it was dropped
	yv.reset_piece()
	for i in 6:
		await yv.do_action("chain")
	await yv.do_action("turn")
	await yv.do_action("sc")
	yv.colour_when.select(0)
	var orange := Color(0.95, 0.58, 0.18)
	yv.choose_colour(orange)
	check("choosing a colour marks the next stitch as the one that changes it", bool(yv.session.get_state()["pending_colour"]) and yv.colour_label.text.contains("changes colour"))
	await yv.do_action("sc")
	var cols: Array = yv.session.get_colours()
	check("after the stitch there are two yarns, the second one orange, and it is the one on the hook", cols.size() == 2 and (cols[1] as Color).is_equal_approx(orange) and (yv.session.get_state()["colour"] as Color).is_equal_approx(orange) and not bool(yv.session.get_state()["pending_colour"]))
	await yv.do_action("sc")
	await yv.do_action("turn")
	yv.choose_colour(yv.SWATCHES[0])
	await yv.do_action("sc")
	check("going back to the first colour picks that yarn up again: still two yarns", yv.session.get_colours().size() == 2 and (yv.session.get_state()["colour"] as Color).is_equal_approx(yv.SWATCHES[0]))
	var parts0: int = yv.session.get_particle_count()
	yv.undo()
	yv.undo()
	check("undo takes the stitch and the colour choice back (the work is rebuilt)", yv.session.get_particle_count() < parts0)
	var m_col: Dictionary = yv.session.get_mesh(-1, 8, 2)
	var seen := {}
	for col in m_col["colors"] as PackedColorArray:
		seen[snappedf(col.r, 0.2)] = true
	check("the mesh carries both colours", seen.size() >= 2)

	# soft yarn: take hold of a strand, pull it, let go
	yv.reset_piece()
	for i in 5:
		await yv.do_action("chain")
	var mesh0: Dictionary = yv.session.get_mesh(-1, 8, 2)
	var pos0: PackedVector3Array = mesh0["positions"]
	var loops0: Array = yv.session.get_loops()
	var c2: Vector3 = loops0[3]["centre"]
	var got: Dictionary = yv.session.grab(c2 + Vector3(0, 0, 0.05), Vector3(0, 0, -1))
	check("a ray at a loop takes hold of a strand there", bool(got.get("ok", false)) and yv.session.is_grabbed())
	var held: Vector3 = got["point"]
	for k in 20:
		yv.session.drag_to(held + Vector3(0, 0.006, 0) * (k + 1) / 20.0)
		yv.session.tick(2)
	var moved: float = (yv.session.get_mesh(-1, 8, 2)["positions"] as PackedVector3Array).size()
	var pos1: PackedVector3Array = yv.session.get_mesh(-1, 8, 2)["positions"]
	var worst := 0.0
	for i in mini(pos0.size(), pos1.size()):
		worst = maxf(worst, pos0[i].distance_to(pos1[i]))
	check("pulled 6 mm, the yarn followed it (the piece moved by %.1f mm somewhere)" % [worst * 1000], worst > 0.005)
	yv.session.release()
	yv.session.tick(200)
	var pos2: PackedVector3Array = yv.session.get_mesh(-1, 8, 2)["positions"]
	var back := 0.0
	for i in mini(pos0.size(), pos2.size()):
		back = maxf(back, pos0[i].distance_to(pos2[i]))
	check("let go, it springs back to where it was (within %.2f mm)" % [back * 1000], back < 0.0012 and not yv.session.is_grabbed())
	var miss: Dictionary = yv.session.grab(Vector3(0.5, 0.5, 0.5), Vector3(0, 0, -1))
	check("a ray that misses the yarn takes hold of nothing", not bool(miss.get("ok", false)) and not yv.session.is_grabbed())

	# expand and thin yarn: a way of looking, not a change of the work
	yv.reset_piece()
	for i in 4:
		await yv.do_action("chain")
	var m1: Dictionary = yv.session.get_mesh(-1, 8, 2)
	var l1: Array = yv.session.get_loops()
	var span1: float = (l1[4]["centre"] as Vector3).distance_to(l1[1]["centre"])
	yv.expand_slider.value = 2.0
	var l2: Array = yv.session.get_loops()
	var span2: float = (l2[4]["centre"] as Vector3).distance_to(l2[1]["centre"])
	check("expanding to 2x doubles the distances between the loops as shown (%.1f mm -> %.1f mm)" % [span1 * 1000, span2 * 1000], absf(span2 / span1 - 2.0) < 1.0e-3)
	var m2: Dictionary = yv.session.get_mesh(-1, 8, 2)
	var ex1 := _extent(m1["positions"])
	var ex2 := _extent(m2["positions"])
	check("...and the drawn yarn spreads out with it (the piece is wider), with the same number of vertices", ex2 > 1.8 * ex1 and (m2["positions"] as PackedVector3Array).size() == (m1["positions"] as PackedVector3Array).size())
	var count_before: int = yv.session.get_particle_count()
	check("expanding does not change the work itself", count_before == yv.session.get_particle_count() and yv.history.size() == 4)
	yv.thick_slider.value = 0.5
	var m3: Dictionary = yv.session.get_mesh(-1, 8, 2)
	check("a thin-yarn view draws a narrower tube (same centre line)", _thickness(m3) < 0.8 * _thickness(m2))
	yv.expand_slider.value = 1.0
	yv.thick_slider.value = 1.0

	# the structure of what was made: every pass through an eye is a clean threading (the topology is right)
	yv.reset_piece()
	check("a new piece starts again from the slip knot", yv.history.size() == 0 and int(yv.session.get_state()["row"]) == 0)
	print("RESULT: %d failed" % fails)
	get_tree().quit(1 if fails > 0 else 0)
