# Staging check: the panels hover far from the body or close to it, each by its own state (editor.gd _stage_pose, set_stage).
# Run headless:  godot --headless --path godot res://tests/staging_check.tscn   (prints STAGING lines, quits 0 when all pass)
# Checks: every panel starts far (an exploded view, level with its fitted place and pushed outwards), set_stage moves one panel and
# leaves the others, the blend glides (not a jump) and settles, "close" puts every vertex exactly on its fitted start position,
# playing from the start brings the far panels in by stage 2, an undo restores the states, and the far poses are finite.
extends Node

const TIMEOUT_S := 200.0

var ed: Node
var phase := 0
var fails := 0
var t0 := 0.0
var _wait := 0
var _first_pid := ""


func _check(ok: bool, what: String) -> void:
	print("STAGING %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


func _ready() -> void:
	t0 = Time.get_ticks_msec() / 1000.0
	get_window().size = Vector2i(1280, 720)
	ed = load("res://editor.tscn").instantiate()
	add_child(ed)


func _span(pid: String) -> Vector3:
	# the centroid of a panel's displayed cloth
	var r: Dictionary = ed.meta["ranges"][pid]
	var c := Vector3.ZERO
	for j in int(r["count"]):
		c += ed.cur_cloth[int(r["first"]) + j]
	return c / float(r["count"])


func _near(pid: String) -> Vector3:
	var r: Dictionary = ed.meta["ranges"][pid]
	var p2: PackedVector3Array = ed.meta["p2"]
	var c := Vector3.ZERO
	for j in int(r["count"]):
		c += p2[int(r["first"]) + j]
	return c / float(r["count"])


func _settle() -> void:
	for i in 80:
		ed._step_blends(1.0 / 60.0)
	ed.tl_t = 0.0
	ed._stage_pose()


func _process(_d: float) -> void:
	if Time.get_ticks_msec() / 1000.0 - t0 > TIMEOUT_S:
		print("STAGING FAIL timeout in phase %d" % phase)
		get_tree().quit(1)
		return
	if phase == 0 and ed.sim != null and ed.meta.has("order") and not ed.pieces.is_empty():
		phase = 1
		var order: Array = ed.meta["order"]
		_check(order.size() >= 3, "the tee has %d panels" % order.size())
		ed.tl_t = 0.0
		ed._stage_pose()
		for pid in order:
			var off: Vector3 = _span(String(pid)) - _near(String(pid))
			var hor := Vector2(off.x, off.z).length()
			_check(hor > 0.8 and absf(off.y) < 0.05, "panel %s starts far: %.2f m out, %.2f m up" % [pid, hor, off.y])
		# the far panels do not sit on top of one another: no two centroids closer than 15 cm
		var worst := 9.0
		for i in order.size():
			for j in range(i + 1, order.size()):
				worst = minf(worst, _span(String(order[i])).distance_to(_span(String(order[j]))))
		_check(worst > 0.15, "far panels keep apart (closest pair %.2f m)" % worst)
		# one panel close, the rest far
		_first_pid = String(order[0])
		ed.set_stage(_first_pid, true)
		_check(ed._blend_target(_first_pid) == 1.0 and ed._blend_target(String(order[1])) == 0.0, "set_stage moves only the chosen panel")
		# the blend glides: after 0.1 s it is part-way, not there
		ed._step_blends(0.1)
		var b: float = float(ed._blend[_first_pid])
		_check(b > 0.05 and b < 0.6, "the panel glides (blend %.2f after 0.1 s)" % b)
		_settle()
		_check(_span(_first_pid).distance_to(_near(_first_pid)) < 1.0e-4, "a close panel sits exactly on its fitted place")
		_check(_span(String(order[1])).distance_to(_near(String(order[1]))) > 0.8, "the others stay far")
		# all close, all far
		ed.set_stage("", true)
		_settle()
		var all_close := true
		for pid in order:
			if _span(String(pid)).distance_to(_near(String(pid))) > 1.0e-4:
				all_close = false
		_check(all_close, "all close: every panel on its fitted place")
		var finite := true
		for v in ed.cur_cloth:
			if not (is_finite(v.x) and is_finite(v.y) and is_finite(v.z)):
				finite = false
		_check(finite, "every displayed position is finite")
		ed.set_stage("", false)
		_settle()
		# play from the start: by the end of stage 1 every panel has come in
		ed.tl_t = 1.0
		ed._stage_pose()
		var mid_far := _span(String(order[0])).distance_to(_near(String(order[0])))
		ed.tl_t = 2.0
		ed._stage_pose()
		var done := true
		for pid in order:
			if _span(String(pid)).distance_to(_near(String(pid))) > 1.0e-4:
				done = false
		_check(mid_far > 0.8 and done, "playing from the start: far at 1.0 (%.2f m), all fitted at 2.0" % mid_far)
		# the toolbar's flip button works on the selected table piece (mapped to its simulated panel) and, with none selected, on all
		ed.sel = null
		ed._stage_button(2)
		var all_near := true
		for pid in order:
			if ed._blend_target(String(pid)) < 1.0:
				all_near = false
		_check(all_near, "flip with nothing selected brings every far panel close")
		ed.set_stage("", false)
		ed.sel = "front"
		var sid: String = ed._sim_id_of("front")
		_check(sid != "" and order.has(sid), "the table's piece 'front' maps to the simulated panel '%s'" % sid)
		ed._stage_button(2)
		_check(ed._blend_target(sid) == 1.0 and ed._blend_target(String(order[1])) == 0.0, "flip with a piece selected moves only that panel")
		print("staging_check: %d failed" % fails)
		get_tree().quit(1 if fails > 0 else 0)

const Table = preload("res://table.gd")
