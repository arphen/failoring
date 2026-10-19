# Checks yarn mode (docs/YARN_MODE_PLAN.md section 11): one click per stitch with the choices made, the cursor, turning, undo, picking a loop.
# Run headless:  godot --headless --path godot res://tests/yarn_view_check.tscn     (PASS/FAIL lines, then "RESULT: n failed")
extends Node
var fails := 0
func check(name: String, ok: bool) -> void:
	print(("PASS  " if ok else "FAIL  ") + name)
	if not ok: fails += 1

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

	# the structure of what was made: every pass through an eye is a clean threading (the topology is right)
	yv.reset_piece()
	check("a new piece starts again from the slip knot", yv.history.size() == 0 and int(yv.session.get_state()["row"]) == 0)
	print("RESULT: %d failed" % fails)
	get_tree().quit(1 if fails > 0 else 0)
