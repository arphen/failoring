# CAD-style cutting table check: the poncho made from scratch with construction geometry, through the editor's own paths.
#   A square, a construction point at its centre of mass (right-click menu), a hole cut centred on it (the menu's dialog
#   path), the point dragged (it becomes free, undo puts it back), the hole dragged (and refused outside the piece), a
#   corner dragged next to a construction point (it snaps there), a corner added in the middle of a sewn edge from the
#   right-click menu (the seam is split, not dropped), Delete on a picked point, and the guide bookkeeping when corners
#   are inserted or removed. Run headless:
#   godot --headless --path godot res://tests/cad_check.tscn
# Prints CAD lines and quits 0 when every check passes, 1 otherwise.
extends Node

const Table = preload("res://table.gd")
const Guides = preload("res://guides.gd")
const Seams = preload("res://seams.gd")

const SETTLE_S := 0.45
const TIMEOUT_S := 180.0

var fails := 0
var ed: Node
var t_start := 0.0


func _check(ok: bool, what: String) -> void:
	print("CAD %s %s" % ["ok  " if ok else "FAIL", what])
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
		print("CAD FAIL the editor did not boot a sim")
		get_tree().quit(1)
		return
	_static_checks()
	await _run()
	print("CAD %s" % ("PASSED" if fails == 0 else "FAILED with %d failure(s)" % fails))
	remove_child(ed)
	ed.free()
	get_tree().quit(0 if fails == 0 else 1)


func _screen_of(p: Dictionary, l: Vector2) -> Vector2:
	var t: Vector2 = ed.table._off(p)
	return ed.table.to_screen(l.x + t.x, l.y + t.y)


# Opens the table's right-click menu at spos and runs the item whose text starts with `text`. False when there is none.
func _menu(spos: Vector2, text: String) -> bool:
	ed.table.context_menu(spos)
	var m: PopupMenu = ed.table._menu
	for i in m.get_item_count():
		if m.get_item_text(i).begins_with(text) and not m.is_item_disabled(i):
			var id := m.get_item_id(i)
			m.hide()
			(ed.table._menu_actions[id] as Callable).call()
			return true
	m.hide()
	return false


func _static_checks() -> void:
	var sq := {"id": "s", "outline": [[0, 0], [1, 0], [1, 1], [0, 1]]}
	_check(Guides.centroid(sq).is_equal_approx(Vector2(0.5, 0.5)), "the centre of mass of a square is its middle")
	sq["holes"] = [Table.rect_hole(0.0, 0.0, 0.5, 0.5)]
	var c := Guides.centroid(sq)   # three quarters left: (3/4 area) centroid of the L shape = (7/12, 7/12)
	_check(c.is_equal_approx(Vector2(7.0 / 12.0, 7.0 / 12.0)), "a hole moves the centre of mass away from it (%s)" % c)
	sq.erase("holes")
	var gm := Guides.add_ref(sq, "mid", 2)
	var gc := Guides.add_ref(sq, "corner", 3)
	_check(Guides.add_ref(sq, "mid", 2) == gm, "a feature gets one construction point (adding it again returns it)")
	var edref := {"pieces": [sq], "seams": []}
	Table.insert_vertex(edref, sq, 1, [1.0, 0.5])
	_check(String(Guides.find(sq, gm)["ref"]) == "mid" and int(Guides.find(sq, gm)["e"]) == 3, "a corner inserted before an edge moves its middle point to the new index")
	_check(int(Guides.find(sq, gc)["i"]) == 4, "and its corner point")
	Table.insert_vertex(edref, sq, 3, [0.5, 1.0])
	var g3: Dictionary = Guides.find(sq, gm)
	_check(String(g3["ref"]) == "free" and Vector2(float(g3["x"]), float(g3["y"])).is_equal_approx(Vector2(0.5, 1.0)), "splitting the edge itself leaves its old middle as a free point where it was")
	Table.remove_vertex(sq, 5, edref["seams"])
	_check(String(Guides.find(sq, gc)["ref"]) == "free", "deleting a corner frees the point on it")
	var gl := Guides.add_line(sq, gm, gc)
	Guides.remove(sq, gm)
	_check(Guides.find(sq, gl) == null, "deleting a point deletes the lines through it")


func _run() -> void:
	var keys: Array = ed._plan_entries().map(func(e: Array) -> String: return String(e[1]))
	var bi: int = keys.find("blank")
	ed.plan_opt.select(bi)
	ed.plan_opt.item_selected.emit(bi)
	await _settle()
	ed.table.size = Vector2(900, 700)
	# 1. a square of cloth, 100 cm
	ed._add_shape_now("square", {"s": 100.0})
	var p: Dictionary = ed.pieces[ed.pieces.size() - 1]
	var pid := String(p["id"])
	ed.table.cam_z = 400.0
	var b := Table.bbox_of(p["outline"])
	var mid := Vector2((b[0] + b[2]) * 0.5, (b[1] + b[3]) * 0.5)
	var off: Vector2 = ed.table._off(p)
	ed.table.ox = 450.0 - (mid.x + off.x) * 400.0
	ed.table.oy = 350.0 + (mid.y + off.y) * 400.0
	ed.table._fit_pending = false
	# 2. right-click inside it: a construction point at the centre of mass
	var inside := _screen_of(p, mid + Vector2(0.2, 0.2))
	_check(_menu(inside, "Construction point at the centre of mass"), "the piece's menu offers a point at the centre of mass")
	var gs: Array = Guides.list(p)
	_check(gs.size() == 1 and String(gs[0]["ref"]) == "centroid", "the piece has a centre-of-mass point")
	var gid := String(gs[0]["id"]) if gs.size() == 1 else ""
	var at: Vector2 = Guides.point_pos(p, gs[0]) if gs.size() == 1 else Vector2.ZERO
	_check(at.is_equal_approx(mid), "it is at the square's middle")
	_check(ed.selection.contains("guide", [pid, gid]), "the new point is picked")
	_check(String(ed.history.undo_name()) == "Add construction point", "adding it is one undoable action")
	# 3. the hole, centred on the point: the menu opens the hole dialog at the point; its OK path is _add_hole_now
	_check(_menu(_screen_of(p, at), "Cut a hole centred here"), "the point's menu offers a hole centred on it")
	for c in ed.get_node("UI").get_children():
		if c is AcceptDialog:
			c.queue_free()
	ed._add_hole_now(p, true, 0.26, 0.0, at)
	var holes: Array = p.get("holes", [])
	_check(holes.size() == 1 and Guides.ring_centre(holes[0]).distance_to(mid) < 1e-6, "the neck hole is centred on the point")
	_check((Guides.point_pos(p, Guides.find(p, gid)) as Vector2).distance_to(mid) < 1e-6, "a centred hole leaves the centre of mass in the middle")
	# 4. drag the point 10 cm right: it becomes a free point there; undo puts it back on the centre of mass
	ed.table.set_tool("select")
	var s0 := _screen_of(p, at)
	ed.table._down(s0, MOUSE_BUTTON_LEFT)
	ed.table._move(s0 + Vector2(40.0, 0.0))   # 400 px/m: 10 cm
	ed.table._up()
	var gd: Dictionary = Guides.find(p, gid)
	_check(String(gd["ref"]) == "free" and Vector2(float(gd["x"]), float(gd["y"])).distance_to(at + Vector2(0.1, 0.0)) < 1e-6, "a dragged centre-of-mass point becomes a free point where it was dropped")
	ed.do_undo()
	p = Table.piece_by_id(ed.pieces, pid)
	_check(String(Guides.find(p, gid)["ref"]) == "centroid", "undo puts it back on the centre of mass")
	# 5. drag the hole 5 cm up; then try to drag it off the piece (refused, it goes back)
	var hc := Guides.ring_centre(p["holes"][0])
	var hs0 := _screen_of(p, hc + Vector2(0.05, 0.0))   # inside the hole, beside the point
	ed.table._down(hs0, MOUSE_BUTTON_LEFT)
	_check(ed.selection.contains("hole", [pid, 0]), "a press inside a hole picks the hole")
	ed.table._move(hs0 + Vector2(0.0, -20.0))
	ed.table._up()
	p = Table.piece_by_id(ed.pieces, pid)
	_check(Guides.ring_centre(p["holes"][0]).distance_to(hc + Vector2(0.0, 0.05)) < 1e-6, "dragging the hole moves it (5 cm up)")
	var hc2 := Guides.ring_centre(p["holes"][0])
	var hs1 := _screen_of(p, hc2 + Vector2(0.05, 0.0))
	ed.table._down(hs1, MOUSE_BUTTON_LEFT)
	ed.table._move(hs1 + Vector2(400.0, 0.0))
	ed.table._up()
	p = Table.piece_by_id(ed.pieces, pid)
	_check(Guides.ring_centre(p["holes"][0]).distance_to(hc2) < 1e-6, "a hole dragged off the piece goes back")
	# 6. a free construction point (menu "here"), and a corner dragged to 5 px from it snaps onto it
	ed.sel = pid
	var n_g: int = Guides.list(p).size()
	_check(_menu(_screen_of(p, mid + Vector2(0.3, 0.3)), "Construction point here"), "the piece's menu offers a point where it was clicked")
	p = Table.piece_by_id(ed.pieces, pid)
	_check(Guides.list(p).size() == n_g + 1, "a free point is added")
	var fg: Dictionary = Guides.list(p)[Guides.list(p).size() - 1]
	var gp: Vector2 = Guides.point_pos(p, fg)
	_check(gp.distance_to(mid + Vector2(0.3, 0.3)) < 0.006, "it is where the menu was opened (on the 5 mm grid)")
	ed.sel = pid
	var c0 := Vector2(float(p["outline"][0][0]), float(p["outline"][0][1]))
	ed.table._down(_screen_of(p, c0), MOUSE_BUTTON_LEFT)
	ed.table._move(_screen_of(p, gp) + Vector2(4.0, 3.0))   # 5 px off the point
	var snapped := Vector2(float(p["outline"][0][0]), float(p["outline"][0][1]))
	_check(snapped.is_equal_approx(gp), "dragging a corner near a construction point snaps onto it (%s)" % [ed.table._snap.get("what", "")])
	ed.table._up()
	ed.do_undo()
	p = Table.piece_by_id(ed.pieces, pid)
	_check(Vector2(float(p["outline"][0][0]), float(p["outline"][0][1])).is_equal_approx(c0), "undo puts the corner back")
	# 7. sew the square's bottom edge to a second square's top, then right-click the bottom edge: a corner in its middle
	ed._add_shape_now("square", {"s": 100.0})
	var q: Dictionary = ed.pieces[ed.pieces.size() - 1]
	ed.push_undo("Sew edges")
	Table.sew_edges(ed, [pid, 0], [String(q["id"]), 2])
	ed.changed("structure")
	p = Table.piece_by_id(ed.pieces, pid)
	var e0a := Vector2(float(p["outline"][0][0]), float(p["outline"][0][1]))
	var e0b := Vector2(float(p["outline"][1][0]), float(p["outline"][1][1]))
	_check(_menu(_screen_of(p, e0a.lerp(e0b, 0.3)), "Add a corner in the middle"), "an edge's menu offers a corner in its middle")
	p = Table.piece_by_id(ed.pieces, pid)
	_check((p["outline"] as Array).size() == 5, "the square has 5 corners")
	_check(ed.seams.size() == 2, "the seam on that edge is split in two, not dropped (%d seams)" % ed.seams.size())
	var tot := 0.0
	for s in ed.seams:
		tot += Seams.range_length(Table.piece_by_id(ed.pieces, String(s["b"][0]))["outline"], s["b"])
	_check(absf(tot - 1.0) < 1e-6, "the two halves still sew the whole 100 cm of the other square (%.4f m)" % tot)
	# 8. Delete on a picked point removes it (one undoable action)
	ed.selection.replace("guide", [pid, gid])
	var n_before: int = Guides.list(p).size()
	_check(ed.table.delete_picked(), "Delete acts on the picked point")
	p = Table.piece_by_id(ed.pieces, pid)
	_check(Guides.list(p).size() == n_before - 1, "the point is gone")
	ed.do_undo()
	p = Table.piece_by_id(ed.pieces, pid)
	_check(Guides.find(p, gid) != null, "undo brings it back")
	# 9. the Line tool: two corners make a construction line (with points on the corners)
	ed.table.set_tool("line")
	ed.table._down(_screen_of(p, Vector2(float(p["outline"][0][0]), float(p["outline"][0][1]))), MOUSE_BUTTON_LEFT)
	ed.table._down(_screen_of(p, Vector2(float(p["outline"][2][0]), float(p["outline"][2][1]))), MOUSE_BUTTON_LEFT)
	var lines := Guides.list(p).filter(func(g) -> bool: return String(g.get("type", "")) == "line")
	_check(lines.size() == 1 and Guides.line_of(p, lines[0]).size() == 2, "two clicks on corners make a construction line")
	ed.table.set_tool("select")
	# 10. the pattern round-trips through the saved JSON with its construction geometry
	var js: Variant = JSON.parse_string(JSON.stringify({"pieces": ed.pieces}))
	var p2 = Table.piece_by_id(js["pieces"], pid)
	_check(p2 != null and (p2.get("guides", []) as Array).size() == Guides.list(p).size(), "construction geometry is saved with the pattern")
