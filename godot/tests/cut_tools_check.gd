# Cutting table tools check: the editor's own paths for the step 2 tools (Add shape at the view centre, Add hole with its
# dialog's numbers and its refusal, the Hole tool's drag on the table, Delete hole), each as one undoable action, and the
# piece keeps its holes through undo and redo. Run headless:
#   godot --headless --path godot res://tests/cut_tools_check.tscn
# Prints CUTTOOLS lines and quits 0 when every check passes, 1 otherwise.
extends Node

const Table = preload("res://table.gd")
const Shapes = preload("res://shapes.gd")

const SETTLE_S := 0.45
const TIMEOUT_S := 180.0

var fails := 0
var ed: Node
var t_start := 0.0


func _check(ok: bool, what: String) -> void:
	print("CUTTOOLS %s %s" % ["ok  " if ok else "FAIL", what])
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
		print("CUTTOOLS FAIL the editor did not boot a sim")
		get_tree().quit(1)
		return
	await _run()
	print("CUTTOOLS %s" % ("PASSED" if fails == 0 else "FAILED with %d failure(s)" % fails))
	remove_child(ed)
	ed.free()
	get_tree().quit(0 if fails == 0 else 1)


# the piece with id pid in the editor's pieces
func _piece(pid: String) -> Dictionary:
	return Table.piece_by_id(ed.pieces, pid)


# the table's screen point for a piece-local point of piece p
func _screen_of(p: Dictionary, lx: float, ly: float) -> Vector2:
	var t: Vector2 = ed.table._off(p)
	return ed.table.to_screen(lx + t.x, ly + t.y)


func _run() -> void:
	# a fresh blank rectangle: one piece to add shapes and holes to
	var keys: Array = ed._plan_entries().map(func(e: Array) -> String: return String(e[1]))
	var bi: int = keys.find("blank")
	ed.plan_opt.select(bi)
	ed.plan_opt.item_selected.emit(bi)
	await _settle()
	ed.sel = String(ed.pieces[0]["id"])
	var n0: int = ed.pieces.size()
	var depth0: int = ed.undo_depth()

	# Add shape: an ellipse, sized in cm, lands centred on the view centre and is undoable as one action
	ed._add_shape_now("ellipse", {"w": 40.0, "h": 30.0})
	_check(ed.pieces.size() == n0 + 1, "Add shape adds one piece")
	var shape_p := ed.pieces[ed.pieces.size() - 1] as Dictionary
	var sb := Table.bbox_of(shape_p["outline"])
	var so: Vector2 = ed.table._off(shape_p)
	var centre := Vector2((sb[0] + sb[2]) * 0.5 + so.x, (sb[1] + sb[3]) * 0.5 + so.y)
	var view_c: Vector2 = ed.table.to_world(ed.table.size.x * 0.5, ed.table.size.y * 0.5)
	_check(centre.distance_to(view_c) < 1e-6, "the new shape is centred on the view centre")
	_check(ed.undo_depth() == depth0 + 1 and String(ed.history.undo_name()) == "Add circle or ellipse", "Add shape is one named action")
	_check(Table.bbox_of(shape_p["outline"])[2] - sb[0] > 0.39 and Table.bbox_of(shape_p["outline"])[2] - sb[0] < 0.41, "the ellipse is 40 cm wide")
	ed.do_undo()
	_check(ed.pieces.size() == n0, "undo removes the shape")
	ed.do_redo()
	_check(ed.pieces.size() == n0 + 1, "redo brings the shape back")
	ed.do_undo()  # the ellipse overlaps the blank piece: take it away again so the tests below pick the blank piece
	_check(ed.pieces.size() == n0, "the shape is taken away again for the piece tests")

	# Add hole, typed: a 10 cm circle at the piece's centre, then one that does not fit is refused with a message
	var p := ed.pieces[0] as Dictionary
	var pid := String(p["id"])
	var pb := Table.bbox_of(p["outline"])
	var mid := Vector2((pb[0] + pb[2]) * 0.5, (pb[1] + pb[3]) * 0.5)
	var holes_before: int = (p.get("holes", []) as Array).size()
	var ok1: bool = ed._add_hole_now(p, true, 0.10, 0.10, mid)
	_check(ok1 and (_piece(pid).get("holes", []) as Array).size() == holes_before + 1, "a 10 cm circle hole is added in the piece's middle")
	var depth1: int = ed.undo_depth()
	var ok2: bool = ed._add_hole_now(p, true, 1.2, 1.2, mid)
	_check(not ok2 and ed.undo_depth() == depth1, "a hole larger than the piece is refused, with no undo step")
	_check(String(ed.status_label.text).find("does not fit") >= 0, "the refusal says the hole does not fit")
	var ok3: bool = ed._add_hole_now(p, false, 0.08, 0.06, Vector2(pb[0] + 0.02, pb[1] + 0.02))  # crosses the left and bottom edges
	_check(not ok3, "a rectangle that crosses the edge is refused")
	# the hole cuts the piece's hit test: a click in the hole is not on the piece
	var in_hole := _screen_of(p, mid.x, mid.y)
	var picked = ed.table.hit_piece(in_hole.x, in_hole.y)
	_check(picked == null or String(picked["id"]) != pid, "a click inside the hole does not pick the piece")

	# Delete hole: one undoable action
	ed._delete_hole(_piece(pid), 0)
	_check((_piece(pid).get("holes", []) as Array).is_empty(), "Delete hole removes it")
	ed.do_undo()
	_check((_piece(pid).get("holes", []) as Array).size() == holes_before + 1, "undo restores the deleted hole")
	ed.do_undo()
	_check((_piece(pid).get("holes", []) as Array).size() == holes_before, "undo removes the added hole")
	ed.do_redo()
	ed.do_redo()

	# the Hole tool: press inside the piece, drag to size a circle, release: one hole, one action
	ed.table.set_tool("hole")
	ed.hole_shape = "circle"
	var hp := _piece(pid)
	var hc := _screen_of(hp, pb[0] + 0.30, pb[1] + 0.25)
	var hr := _screen_of(hp, pb[0] + 0.30 + 0.07, pb[1] + 0.25)
	var hcount: int = (hp.get("holes", []) as Array).size()
	ed.table._down(hc, MOUSE_BUTTON_LEFT)
	ed.table._move(hr)
	ed.table._up()
	_check((_piece(pid).get("holes", []) as Array).size() == hcount + 1, "the Hole tool adds a circle where the drag was")
	_check(String(ed.history.undo_name()).begins_with("Add circle hole"), "the Hole tool's hole is a named action")
	# a press outside every piece adds nothing
	var nowhere: Vector2 = ed.table.to_screen(9.0, 9.0)
	ed.table._down(nowhere, MOUSE_BUTTON_LEFT)
	ed.table._up()
	_check((_piece(pid).get("holes", []) as Array).size() == hcount + 1, "a press outside the pieces adds no hole")
	ed.table.set_tool("select")
	_check(Shapes.label_of("poncho") == "Poncho (square with neck hole)", "the Poncho is in the shape menu")
	print("CUTTOOLS pieces %d, holes on the test piece %d" % [ed.pieces.size(), (_piece(pid).get("holes", []) as Array).size()])
