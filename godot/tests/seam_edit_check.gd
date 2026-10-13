# Partial seams in the editor (step 4): the Seams tab's range function (set_seam_range) and its refusals (too short,
# overlapping), the rebuild's split of the outlines at the range ends (Seams.expand), and the range handle drag on the
# table, each one undoable. Runs the editor's own paths. Run headless:
#   godot --headless --path godot res://tests/seam_edit_check.tscn
# Prints SEAMEDIT lines and quits 0 when every check passes, 1 otherwise.
extends Node

const Table = preload("res://table.gd")
const Shapes = preload("res://shapes.gd")
const Seams = preload("res://seams.gd")

const SETTLE_S := 0.45
const TIMEOUT_S := 180.0

var fails := 0
var ed: Node
var t_start := 0.0


func _check(ok: bool, what: String) -> void:
	print("SEAMEDIT %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


func _settle() -> void:
	await get_tree().create_timer(SETTLE_S).timeout


func _ready() -> void:
	t_start = Time.get_ticks_msec() / 1000.0
	ed = load("res://editor.tscn").instantiate()
	add_child(ed)
	await _settle()
	while ed.sim == null and Time.get_ticks_msec() / 1000.0 - t_start < TIMEOUT_S:
		await get_tree().process_frame
	if ed.sim == null:
		print("SEAMEDIT FAIL the editor did not boot a sim")
		get_tree().quit(1)
		return
	await _run()
	print("SEAMEDIT %s" % ("PASSED" if fails == 0 else "FAILED with %d failure(s)" % fails))
	remove_child(ed)
	ed.free()
	get_tree().quit(0 if fails == 0 else 1)


# the world point of a piece's local point
func _world(p: Dictionary, lx: float, ly: float) -> Vector2:
	var t: Vector2 = ed.table._off(p)
	return Vector2(lx + t.x, ly + t.y)


func _run() -> void:
	var keys: Array = ed._plan_entries().map(func(e: Array) -> String: return String(e[1]))
	ed.plan_opt.select(keys.find("blank"))
	ed.plan_opt.item_selected.emit(keys.find("blank"))
	await _settle()
	# two 40 cm squares: A and C (C sits beside A), and the blank piece is left alone
	var a: Dictionary = Table.add_shape(ed, "Square", Shapes.make("square", {"s": 40.0}), Vector2(1.0, 0.2))
	var c: Dictionary = Table.add_shape(ed, "Square", Shapes.make("square", {"s": 40.0}), Vector2(1.6, 0.2))
	var aid := String(a["id"])
	var cid := String(c["id"])
	Table.sew_edges(ed, [aid, 2], [cid, 2])  # the top edges of both squares, joined whole
	var k: int = ed.seams.size() - 1
	_check(ed.seams[k]["a"].size() == 2, "a new seam joins the whole edges (two-element sides)")

	# the Seams tab's range: the first 20 cm of A's top edge only
	_check(ed.set_seam_range(k, "a", 0.0, 20.0), "set_seam_range puts the first 20 cm of A's top in the seam")
	var sa: Array = ed.seams[k]["a"]
	_check(sa.size() == 4 and absf(Seams.range_length(Table.piece_by_id(ed.pieces, aid)["outline"], sa) - 0.2) < 1e-3, "the side is now a range of 20 cm")
	_check(String(ed.history.undo_name()) == "Set seam range", "the range is one named action")
	ed.do_undo()
	_check(ed.seams[k]["a"].size() == 2, "undo makes the seam whole again")
	ed.do_redo()

	# a second seam on A's top from 24 cm to the end: the first seam's range may not reach into it
	ed.seams.append({"a": [aid, 2, 0.6, 1.0], "b": [cid, 2], "reverse": true})
	var k2: int = ed.seams.size() - 1
	var before: Array = ed.seams[k]["a"].duplicate(true)
	_check(not ed.set_seam_range(k, "a", 0.0, 30.0), "a range that overlaps another seam on its edge is refused")
	_check(String(ed.status_label.text).find("overlaps") >= 0, "the refusal says the ranges overlap")
	_check(ed.seams[k]["a"] == before, "a refused range leaves the seam as it was")
	_check(not ed.set_seam_range(k2, "a", 10.0, 10.1), "a range shorter than 0.8 cm is refused")
	ed.seams.remove_at(k2)

	# the rebuild splits A's top at the range ends and gives the simulator whole edges
	ed.set_seam_range(k, "a", 0.0, 20.0)
	ed.rebuild()
	var sim_a: Dictionary = {}
	for p in ed._sim_pieces:
		if String(p["id"]).ends_with(aid):
			sim_a = p
	_check(not sim_a.is_empty() and (sim_a["outline"] as Array).size() == 4 + 1, "the rebuild splits A's top at 20 cm (5 corners)")
	var all_whole := true
	for s in ed._sim_seams:
		if s["a"].size() != 2 or s["b"].size() != 2:
			all_whole = false
	_check(all_whole, "every seam side the simulator sees is a whole edge")

	# the range handle on the table: drag the end of the first 20 cm to 30 cm along A's top
	ed.table._ensure_geo()
	var handle_end: Dictionary = {}
	for h in ed.table._handle_points(k):
		if String(h["side"]) == "a" and int(h["end"]) == 1:
			handle_end = h
	_check(not handle_end.is_empty(), "a partial seam has a handle at the end of its range")
	var ap: Dictionary = Table.piece_by_id(ed.pieces, aid)
	var top_from := Vector2(float(ap["outline"][2][0]), float(ap["outline"][2][1]))
	var top_to := Vector2(float(ap["outline"][3][0]), float(ap["outline"][3][1]))
	var target_t := 0.75  # 30 cm of the 40 cm top, measured from its first corner (three quarters)
	var target_local := top_from.lerp(top_to, target_t)
	var target_screen: Vector2 = ed.table.to_screen(target_local.x + ed.table._off(ap).x, target_local.y + ed.table._off(ap).y)
	ed.table._down(handle_end["screen"], MOUSE_BUTTON_LEFT)
	ed.table._move(target_screen)
	ed.table._up()
	var t1 := float(Seams.side_range(ed.seams[k]["a"]).y)
	_check(absf(t1 - target_t) < 0.02, "dragging the handle moves the range end to 30 cm (t = %.3f)" % t1)
	_check(String(ed.history.undo_name()) == "Move seam end", "the handle drag is one named action")
	ed.do_undo()
	_check(absf(float(Seams.side_range(ed.seams[k]["a"]).y) - 0.5) < 1e-3, "undo puts the handle back at 20 cm")
	print("SEAMEDIT seams %d, pieces %d" % [ed.seams.size(), ed.pieces.size()])
