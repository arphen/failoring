# Cutting table edit tools check (step 3): the Split tool (a click on an edge), edge curves (make, bulge, straighten), the
# Rotate tool (a drag about the group's centre), the Measure tool, the typed Turn and Scale, Align and Distribute, and the
# scale guard. Each is one undoable action. Drives the editor's own functions and table handlers with screen points.
# Run headless:
#   godot --headless --path godot res://tests/cut_edit_check.tscn
# Prints CUTEDIT lines and quits 0 when every check passes, 1 otherwise.
extends Node

const Table = preload("res://table.gd")
const Shapes = preload("res://shapes.gd")
const Geom = preload("res://cut_geom.gd")

const SETTLE_S := 0.45
const TIMEOUT_S := 180.0

var fails := 0
var ed: Node
var t_start := 0.0


func _check(ok: bool, what: String) -> void:
	print("CUTEDIT %s %s" % ["ok  " if ok else "FAIL", what])
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
		print("CUTEDIT FAIL the editor did not boot a sim")
		get_tree().quit(1)
		return
	await _run()
	print("CUTEDIT %s" % ("PASSED" if fails == 0 else "FAILED with %d failure(s)" % fails))
	remove_child(ed)
	ed.free()
	get_tree().quit(0 if fails == 0 else 1)


func _piece(pid: String) -> Dictionary:
	return Table.piece_by_id(ed.pieces, pid)


# screen point of a world point (metres)
func _sp(x: float, y: float) -> Vector2:
	return ed.table.to_screen(x, y)


func _near(a: float, b: float, tol := 1e-3) -> bool:
	return absf(a - b) <= tol


func _run() -> void:
	var keys: Array = ed._plan_entries().map(func(e: Array) -> String: return String(e[1]))
	ed.plan_opt.select(keys.find("blank"))
	ed.plan_opt.item_selected.emit(keys.find("blank"))
	await _settle()
	var pid := String(ed.pieces[0]["id"])
	ed.sel = pid
	var n_corners: int = (ed.pieces[0]["outline"] as Array).size()

	# Split: a click on the middle of edge 0 (the bottom edge) adds a corner there; undo takes it away
	ed.table.set_tool("split")
	ed.table._split_down(_sp(0.25, 0.0))
	_check((_piece(pid)["outline"] as Array).size() == n_corners + 1, "the Split tool adds a corner on the clicked edge")
	_check(String(ed.history.undo_name()).begins_with("Split edge"), "the split is a named action")
	ed.do_undo()
	_check((_piece(pid)["outline"] as Array).size() == n_corners, "undo takes the split corner away")
	ed.do_redo()
	ed.do_undo()
	ed.table.set_tool("select")

	# Curve: the right edge becomes 8 segments; its bulge can be set and it can be straightened
	var p := _piece(pid)
	ed._curve_edge(p, 1)
	p = _piece(pid)
	_check((p["outline"] as Array).size() == n_corners + 7, "Curve adds 7 corners to the edge (%d now)" % (p["outline"] as Array).size())
	_check((p.get("curves", []) as Array).size() == 1 and Geom.curve_at_edge(p, 1) == 0, "the curve keeps a record on the edge")
	# the right edge runs up at x = 0.5 and bows out to +x; its middle sample (t = 0.5) is outline index 1 + 4
	ed._set_curve_bulge(p, 0, "10")
	p = _piece(pid)
	_check(float(p["outline"][5][0]) > 0.53 and float(p["outline"][5][0]) < 0.57, "a bulge of 10 cm moves the curve's middle out to x = 0.55 (%.3f)" % float(p["outline"][5][0]))
	_check(_near(float(p["curves"][0]["bulge"]), 0.10), "the record keeps the bulge (0.10 m)")
	ed._straighten_curve(p, 0)
	p = _piece(pid)
	_check((p.get("curves", []) as Array).is_empty() and (p["outline"] as Array).size() == n_corners + 7, "Straighten drops the record and keeps the corners")
	ed.do_undo()
	ed.do_undo()
	ed.do_undo()
	p = _piece(pid)
	_check((p["outline"] as Array).size() == n_corners and (p.get("curves", []) as Array).is_empty(), "undo takes the curve back out")

	# Rotate: press right of the centre and drag up: the piece turns 90 degrees about its centre (0.25, 0.25)
	ed.table.set_tool("rotate")
	var centre := Vector2(0.25, 0.25)
	ed.table._down(_sp(0.45, 0.25), MOUSE_BUTTON_LEFT)
	ed.table._move(_sp(0.25, 0.45))
	ed.table._up()
	p = _piece(pid)
	var o0 := Vector2(float(p["outline"][0][0]), float(p["outline"][0][1]))
	_check(o0.distance_to(Vector2(0.5, 0.0)) < 2e-3, "the Rotate drag turns the piece 90 degrees about its centre (corner 0 at %s)" % str(o0))
	_check(String(ed.history.undo_name()) == "Rotate piece", "the rotation is one action named Rotate piece")
	_check(_near(float(p["grain"]), 1.5708 + PI * 0.5, 2e-3), "the grain turns with the piece")
	ed.do_undo()
	p = _piece(pid)
	_check(Vector2(float(p["outline"][0][0]), float(p["outline"][0][1])).distance_to(Vector2(0.0, 0.0)) < 1e-6, "undo restores the rotated piece")
	ed.table.set_tool("select")

	# typed Turn and Scale on the selected piece
	ed.rotate_selection(90.0)
	p = _piece(pid)
	_check(Vector2(float(p["outline"][0][0]), float(p["outline"][0][1])).distance_to(Vector2(0.5, 0.0)) < 2e-3, "Turn by 90 degrees (typed) gives the same turn")
	ed.do_undo()
	ed.scale_selection(50.0)
	p = _piece(pid)
	var bb := Table.bbox_of(p["outline"])
	_check(_near(bb[2] - bb[0], 0.25, 2e-3), "Scale to 50 %% halves the width (%.3f m)" % (bb[2] - bb[0]))
	ed.do_undo()
	ed.scale_selection(0.5)  # 0.5 % would make the edges 2.5 mm: refused with a message
	p = _piece(pid)
	_check(Table.bbox_of(p["outline"])[2] > 0.49, "a scale that makes an edge too short is refused")
	_check(String(ed.status_label.text).find("shorter than") >= 0, "the refusal says why")

	# Measure: two clicks give the distance in cm
	ed.table.set_tool("measure")
	ed.table._down(_sp(0.0, 0.0), MOUSE_BUTTON_LEFT)
	ed.table._up()
	ed.table._down(_sp(0.3, 0.4), MOUSE_BUTTON_LEFT)
	ed.table._up()
	_check(String(ed.status_label.text).find("50.0 cm") >= 0, "Measure gives 50.0 cm between (0, 0) and (0.3, 0.4) (%s)" % ed.status_label.text)
	ed.table.set_tool("select")

	# Align and Distribute on three new rectangles
	var a := Table.add_shape(ed, "Rectangle", Shapes.make("rect", {"w": 20.0, "h": 20.0}), Vector2(1.0, 0.3))
	var b := Table.add_shape(ed, "Rectangle", Shapes.make("rect", {"w": 30.0, "h": 10.0}), Vector2(1.6, 0.6))
	ed.sel = String(a["id"])
	ed.selection.add("piece", String(b["id"]))
	var ref := Geom.world_box(_piece(String(b["id"])))
	ed.align_selection("left")  # the last picked piece (b) is the reference
	_check(_near(float(Geom.world_box(_piece(String(a["id"])))[0].x), float(ref[0].x), 1e-4), "Align left moves the first piece to the last picked piece's left edge")
	ed.do_undo()
	# three pieces of different widths with unequal gaps: distribute across makes the gaps equal
	var d1 := Table.add_shape(ed, "Rectangle", Shapes.make("rect", {"w": 10.0, "h": 10.0}), Vector2(3.0, 0.3))
	var d2 := Table.add_shape(ed, "Rectangle", Shapes.make("rect", {"w": 20.0, "h": 10.0}), Vector2(3.4, 0.3))
	var d3 := Table.add_shape(ed, "Rectangle", Shapes.make("rect", {"w": 10.0, "h": 10.0}), Vector2(4.2, 0.3))
	ed.sel = String(d1["id"])
	ed.selection.add("piece", String(d2["id"]))
	ed.selection.add("piece", String(d3["id"]))
	ed.distribute_selection(0)
	var ba := Geom.world_box(_piece(String(d1["id"])))
	var bb2 := Geom.world_box(_piece(String(d2["id"])))
	var bc := Geom.world_box(_piece(String(d3["id"])))
	var g1 := float(bb2[0].x) - float(ba[1].x)
	var g2 := float(bc[0].x) - float(bb2[1].x)
	_check(_near(g1, g2, 1e-3) and g1 > 0.0, "Distribute across makes the gaps between three pieces equal (%.3f and %.3f m)" % [g1, g2])
	_check(_near(float(ba[0].x), 2.95, 1e-3) and _near(float(bc[1].x), 4.25, 1e-3), "distribute keeps the outermost two pieces")
	ed.do_undo()
	_check(_near(float(Geom.world_box(_piece(String(d2["id"])))[0].x), 3.3, 1e-3), "undo puts the middle piece back")

	print("CUTEDIT pieces %d, corners on the test piece %d" % [ed.pieces.size(), (_piece(pid)["outline"] as Array).size()])
