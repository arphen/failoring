# 3D placement check: a panel moved and turned in the 3D view's Move panels mode (place_view.gd) through the view's own
# input path, stored in the piece (place3d.gd), applied to the simulator's start positions on the rebuild, undoable, and
# saved with the pattern. Run headless:
#   godot --headless --path godot res://tests/place_check.tscn
# Prints PLACE lines and quits 0 when every check passes, 1 otherwise.
extends Node

const Table = preload("res://table.gd")
const Place3D = preload("res://place3d.gd")

const SETTLE_S := 0.6
const TIMEOUT_S := 180.0

var fails := 0
var ed: Node
var t_start := 0.0


func _check(ok: bool, what: String) -> void:
	print("PLACE %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


func _settle() -> void:
	await get_tree().create_timer(SETTLE_S).timeout
	var t0 := Time.get_ticks_msec()
	while ed.debounce.time_left > 0.0 and Time.get_ticks_msec() - t0 < 20000:
		await get_tree().process_frame
	await get_tree().process_frame


func _ready() -> void:
	t_start = Time.get_ticks_msec() / 1000.0
	ed = load("res://editor.tscn").instantiate()
	add_child(ed)
	await _settle()
	while ed.sim == null and Time.get_ticks_msec() / 1000.0 - t_start < TIMEOUT_S:
		await get_tree().process_frame
	if ed.sim == null:
		print("PLACE FAIL the editor did not boot a sim")
		get_tree().quit(1)
		return
	_static_checks()
	await _run()
	print("PLACE %s" % ("PASSED" if fails == 0 else "FAILED with %d failure(s)" % fails))
	remove_child(ed)
	ed.free()
	get_tree().quit(0 if fails == 0 else 1)


func _static_checks() -> void:
	var p := {"id": "a"}
	var pos := PackedVector3Array([Vector3(1, 0, 0), Vector3(-1, 0, 0), Vector3(0, 0, 1), Vector3(0, 0, -1)])
	Place3D.set_place(p, Vector3.ZERO, Quaternion.IDENTITY)
	_check(not Place3D.has(p), "no move and no turn stores nothing")
	Place3D.set_place(p, Vector3(0, 0.5, 0), Quaternion(Vector3.UP, PI * 0.5))
	var q := pos.duplicate()
	Place3D.apply(q, 0, 4, p)
	_check(Place3D.centre(q, 0, 4).is_equal_approx(Vector3(0, 0.5, 0)), "the turn is about the piece's centre, then the move is added")
	_check(q[0].is_equal_approx(Vector3(0, 0.5, -1)), "a quarter turn about up takes +x to -z")
	Place3D.compose(p, Quaternion(Vector3.UP, -PI * 0.5), Vector3(0, -0.5, 0))
	_check(not Place3D.has(p), "composing the inverse move and turn gives no placement")
	var e := Vector3(10, 20, 30)
	Place3D.set_place(p, Vector3.ZERO, Place3D.q_from_euler_deg(e))
	_check(Place3D.euler_deg(p).is_equal_approx(e), "the Piece tab's degrees round-trip")


# the start position centre of the merged piece m (meta p2)
func _p2_centre(m: String) -> Vector3:
	var r: Dictionary = ed.meta["ranges"][m]
	return Place3D.centre(ed.meta["p2"], int(r["first"]), int(r["count"]))


func _run() -> void:
	var keys: Array = ed._plan_entries().map(func(x: Array) -> String: return String(x[1]))
	var bi: int = keys.find("blank")
	ed.plan_opt.select(bi)
	ed.plan_opt.item_selected.emit(bi)
	await _settle()
	var pid := String(ed.pieces[0]["id"])
	ed.sel = pid
	var m: String = ed._merged_sel()
	var c0 := _p2_centre(m)
	# a placement typed in (the Piece tab's path): the rebuild applies it to the start positions
	ed.push_undo("Move")
	Place3D.set_place(ed.pieces[0], Vector3(0, 0, 0.1), Quaternion.IDENTITY)
	ed.changed("structure")
	await _settle()
	_check(_p2_centre(m).distance_to(c0 + Vector3(0, 0, 0.1)) < 1e-4, "the rebuild moves the panel's start positions 10 cm forward")
	ed.do_undo()
	await _settle()
	_check(_p2_centre(m).distance_to(c0) < 1e-4 and not Place3D.has(ed.pieces[0]), "undo takes it back")
	# the 3D view: Move panels mode shows the start positions (stage 3) and the gizmo on the selected panel
	ed.view_pane.size = Vector2(600, 600)
	ed.sub_view.size = Vector2i(600, 600)
	ed._set_place_mode(true)
	ed.sel = pid
	await get_tree().process_frame
	var pv = ed.place_view
	_check(is_equal_approx(ed.tl_t, 2.0) and not ed.playing, "Move panels mode shows the start positions, paused")
	pv.refresh()
	_check(pv.pid == m and pv.gizmo.visible, "the selected panel gets the gizmo")
	var c = pv.centre()
	_check(c != null, "the panel has a centre on screen")
	if c == null:
		return
	# drag the panel itself: a press on it, a move, a release
	var s0: Vector2 = pv._to_pane(c)
	var hit: Array = pv.pick(s0)
	_check(not hit.is_empty() and String(hit[0]) == m, "a ray through the panel's centre hits the panel")
	_press(s0)
	_motion(s0 + Vector2(40, 0))
	_release(s0 + Vector2(40, 0))
	var t1 := Place3D.t_of(ed.pieces[0])
	_check(Place3D.has(ed.pieces[0]) and t1.length() > 0.01, "dragging the panel moves it (%s m)" % t1)
	var right: Vector3 = ed.cam.global_transform.basis.x
	_check(t1.normalized().dot(right) > 0.95, "a drag to the right moves it to the camera's right")
	_check(String(ed.history.undo_name()).begins_with("Move ") and String(ed.history.undo_name()).ends_with(" in 3D"), "the drag is one undoable action (%s)" % ed.history.undo_name())
	await _settle()
	_check(_p2_centre(m).distance_to(c0 + t1) < 1e-4, "the rebuilt start positions keep the move")
	# the up arrow: a drag up the screen moves it up only
	pv.refresh()
	c = pv.centre()
	var L: float = pv._size(c)
	var sa: Vector2 = pv._to_pane(c + Vector3.UP * L * 0.7)
	_check(pv.handle_at(sa) == "axis1", "the up arrow is under its own screen point")
	_press(sa)
	_motion(sa + Vector2(0, -30))
	_release(sa + Vector2(0, -30))
	var t2 := Place3D.t_of(ed.pieces[0])
	var d2 := t2 - t1
	_check(d2.y > 0.01 and absf(d2.x) < 1e-6 and absf(d2.z) < 1e-6, "dragging the up arrow moves it up only (%s)" % d2)
	await _settle()
	# a ring: a drag around the centre turns it about that axis, about its own centre
	pv.refresh()
	c = pv.centre()
	L = pv._size(c)
	var ring_pt: Vector3 = (pv._ring_pts(c, 1, L * 0.75) as Array)[6]
	var sr: Vector2 = pv._to_pane(ring_pt)
	_check(pv.handle_at(sr) == "ring1", "the ring about up is under its own screen point (%s)" % pv.handle_at(sr))
	_press(sr)
	var cs: Vector2 = pv._to_pane(c)
	var sr2: Vector2 = cs + (sr - cs).rotated(0.4)
	_motion(sr2)
	_release(sr2)
	var q3 := Place3D.q_of(ed.pieces[0])
	_check(not q3.is_equal_approx(Quaternion.IDENTITY) and absf(absf(q3.get_axis().normalized().dot(Vector3.UP)) - 1.0) < 1e-3, "dragging the ring turns the panel about up")
	_check(Place3D.t_of(ed.pieces[0]).is_equal_approx(t2), "turning keeps the move")
	await _settle()
	_check(_p2_centre(m).distance_to(c0 + t2) < 1e-3, "the turn is about the panel's centre (it stays put)")
	# saved with the pattern, and the placement survives a reload of the saved JSON
	var js = JSON.parse_string(JSON.stringify({"pieces": ed.pieces}))
	var p2 = Table.piece_by_id(js["pieces"], pid)
	_check(p2 != null and Place3D.has(p2) and Place3D.t_of(p2).is_equal_approx(t2), "the placement is saved with the pattern")
	# Reset panel drops it
	ed._reset_place()
	_check(not Place3D.has(ed.pieces[0]), "Reset panel drops the move and turn")
	await _settle()
	_check(_p2_centre(m).distance_to(c0) < 1e-4, "and the panel is back where the body placement puts it")
	ed._set_place_mode(false)
	_check(not pv.gizmo.visible, "leaving the mode hides the gizmo")


func _press(at: Vector2) -> void:
	var e := InputEventMouseButton.new()
	e.button_index = MOUSE_BUTTON_LEFT
	e.pressed = true
	e.position = at
	ed._on_view_gui_input(e)


func _motion(at: Vector2) -> void:
	var e := InputEventMouseMotion.new()
	e.button_mask = MOUSE_BUTTON_MASK_LEFT
	e.position = at
	ed._on_view_gui_input(e)


func _release(at: Vector2) -> void:
	var e := InputEventMouseButton.new()
	e.button_index = MOUSE_BUTTON_LEFT
	e.pressed = false
	e.position = at
	ed._on_view_gui_input(e)
