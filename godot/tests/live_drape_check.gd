# Live drape (live_drape.gd, docs/LIVE_DRAPE_PLAN.md) on the default tee, driven through its calls (no pointer):
# entered before the seams close, every panel hangs from its top; a panel slid out swings past its distance and settles;
# a grab carries the cloth to the pointer; a pin holds; a tack pulls two points together and is saved in the pattern;
# Sew closes the seams; the Length tool lengthens a panel in the simulator and the pattern by the same amount without a
# rebuild; linked panels change together; leaving rebuilds with the tack sewn. Run headless:
#   godot --headless --path godot res://tests/live_drape_check.tscn
extends Node

var fails := 0
var ed: Node
var live


func _check(ok: bool, what: String) -> void:
	print("LIVE %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


func _ready() -> void:
	ed = load("res://editor.tscn").instantiate()
	add_child(ed)
	var t0 := Time.get_ticks_msec()
	while ed.sim == null and Time.get_ticks_msec() - t0 < 180000:
		await get_tree().process_frame
	if ed.sim == null:
		print("LIVE FAIL no sim")
		get_tree().quit(1)
		return
	await get_tree().create_timer(0.6).timeout
	while ed.debounce.time_left > 0.0:
		await get_tree().process_frame
	await _run()
	print("LIVE %s" % ("PASSED" if fails == 0 else "FAILED with %d failure(s)" % fails))
	remove_child(ed)
	ed.free()
	get_tree().quit(0 if fails == 0 else 1)


func _frames(n: int) -> void:
	for i in n:
		await get_tree().physics_frame


func _merged(name: String) -> String:
	for p in ed.pieces:
		if String(p.get("name", "")) == name:
			return ed._sim_id_of(String(p["id"]))
	return ""


func _centre(m: String) -> Vector3:
	var r: Dictionary = ed.meta["ranges"][m]
	var pos: PackedVector3Array = live.positions()
	var c := Vector3.ZERO
	for j in int(r["count"]):
		c += pos[int(r["first"]) + j]
	return c / float(r["count"])


# The vertex of panel m nearest to a point of its start pose at fraction f of its height (0 top, 1 bottom), centred.
func _vertex_at(m: String, f: float) -> int:
	var r: Dictionary = ed.meta["ranges"][m]
	var p2: PackedVector3Array = ed.meta["p2"]
	var lo := 1e9
	var hi := -1e9
	var cx := 0.0
	for j in int(r["count"]):
		var p: Vector3 = p2[int(r["first"]) + j]
		lo = minf(lo, p.y)
		hi = maxf(hi, p.y)
		cx += p.x
	cx /= float(r["count"])
	var want := Vector3(cx, lerpf(hi, lo, f), 0.0)
	var best := -1
	var bd := 1e9
	for j in int(r["count"]):
		var v := int(r["first"]) + j
		var d := Vector2(p2[v].x - want.x, p2[v].y - want.y).length()
		if d < bd:
			bd = d
			best = v
	return best


func _extent(p: Dictionary, a: Vector2) -> float:
	var lo := 1e9
	var hi := -1e9
	for q in p["outline"]:
		var s := Vector2(float(q[0]), float(q[1])).dot(a)
		lo = minf(lo, s)
		hi = maxf(hi, s)
	return hi - lo


func _run() -> void:
	live = ed.live
	var front := _merged("Front")
	var back := _merged("Back")
	_check(front != "" and back != "", "the tee has a front and a back (%s, %s)" % [front, back])
	# ---- enter before the seams close: panels hang from their tops
	ed.tl_t = 0.0
	ed._last_shown = ""
	ed._show_current()
	ed._set_live_mode(true)
	_check(live.active and not live.sewn, "live drape entered unsewn")
	_check(live.holds.size() == (ed.meta["order"] as Array).size(), "every panel is held (%d holds)" % live.holds.size())
	await _frames(40)
	_check(live.frames_run >= 30, "the simulator runs live (%d frames)" % live.frames_run)
	var pos: PackedVector3Array = live.positions()
	var hv: PackedInt32Array = live.holds[front]["verts"]
	var hb: PackedVector3Array = live.holds[front]["base"]
	_check(pos[hv[0]].distance_to(hb[0]) < 0.002, "a held vertex stays at its hold (%.1f mm)" % (pos[hv[0]].distance_to(hb[0]) * 1000.0))
	# ---- distance: slide the front out 30 cm; the hold overshoots and settles
	live._activate(front)
	_check(live.pid == front, "the front is the active panel")
	var c0 := _centre(front)
	var goal: float = float(live.holds[front]["d0"]) + 0.3
	live.set_distance(front, goal)
	var dmax := 0.0
	for i in 150:
		await get_tree().physics_frame
		dmax = maxf(dmax, float(live.holds[front]["d"]))
	var dnow := float(live.holds[front]["d"])
	_check(dmax > goal + 0.01, "the hold overshoots its distance (%.1f cm past it): the cloth swings" % ((dmax - goal) * 100.0))
	_check(absf(dnow - goal) < 0.01, "the hold settles at its distance (%.1f cm off)" % (absf(dnow - goal) * 100.0))
	var moved := (_centre(front) - c0).dot(live.holds[front]["dir"])
	_check(moved > 0.2, "the front moved out with it (%.0f cm)" % (moved * 100.0))
	live.set_distance(front, float(live.holds[front]["d0"]))
	await _frames(120)
	# ---- grab: the bottom of the front follows the pointer
	var vb := _vertex_at(front, 0.95)
	pos = live.positions()
	live._grab_start({"pid": front, "v": vb, "p": pos[vb]}, Vector2.ZERO)
	var tgt: Vector3 = pos[vb] + Vector3(0.0, 0.15, 0.15)
	live.grab["target"] = tgt
	await _frames(20)
	pos = live.positions()
	var gd: float = (pos[vb] - tgt).length()
	_check(gd < 0.05, "the grabbed cloth is at the pointer (%.1f cm)" % (gd * 100.0))
	live._release(Vector2.ZERO)
	await _frames(60)
	_check(live.positions()[vb].distance_to(tgt) > 0.03, "released, the cloth falls back")
	# ---- pin: a pinned point stays put
	var vp := _vertex_at(back, 0.8)
	pos = live.positions()
	var pp: Vector3 = pos[vp]
	live._activate(back)
	live._pin_at({"pid": back, "v": vp, "p": pp})
	await _frames(40)
	_check(live.positions()[vp].distance_to(pp) < 0.002, "the pinned point stays (%.1f mm)" % (live.positions()[vp].distance_to(pp) * 1000.0))
	# ---- tack two points of the front together
	live._activate(front)
	var ta := _vertex_at(front, 0.55)
	var tb := _vertex_at(front, 0.9)
	pos = live.positions()
	var d0: float = pos[ta].distance_to(pos[tb])
	var undo0: int = ed.undo_depth()
	live._tack_click({"pid": front, "v": ta, "p": pos[ta]})
	live._tack_click({"pid": front, "v": tb, "p": pos[tb]})
	await _frames(90)
	var d1: float = live.positions()[ta].distance_to(live.positions()[tb])
	_check(d1 < 0.02 and d1 < d0 * 0.3, "the tack pulls its points together (%.1f -> %.1f cm)" % [d0 * 100.0, d1 * 100.0])
	var fp: Dictionary = live._table_piece(front)
	_check((fp.get("tacks", []) as Array).size() == 1, "the tack is saved in the pattern")
	_check(ed.undo_depth() == undo0 + 1, "the tack is one undoable action")
	# ---- sew
	var sim0 = ed.sim
	var gap0 := float(live.latest["gap"])
	live.set_sewn(true)
	await _frames(90)
	_check(live.holds.is_empty(), "Sew lets the holds go")
	var gap1 := float(live.latest["gap"])
	_check(gap1 < gap0 * 0.5 and gap1 < 0.03, "the seams close (gap %.1f -> %.1f cm)" % [gap0 * 100.0, gap1 * 100.0])
	# ---- length: lengthen the front below its middle; the pattern gets the same change, nothing is rebuilt
	var a: Vector2 = live.fit(front)["a"]
	var len0 := _extent(fp, a)
	var len_back0 := _extent(live._table_piece(back), live.fit(back)["a"])
	var vm := _vertex_at(front, 0.5)
	live._len_press(front, vm, live.positions()[vm], Vector2(200, 200), "view")
	live.len_drag["mouse"] = Vector2(200, 200) + (live.len_drag["sdir"] as Vector2) * 300.0
	await _frames(40)
	var dl := float(live.len_drag["d"])
	live._release(Vector2.ZERO)
	var len1 := _extent(fp, a)
	_check(dl > 0.05, "the bent rod lengthens the front (%.1f cm in 40 frames)" % (dl * 100.0))
	_check(absf((len1 - len0) - dl) < 0.003, "the pattern is longer by the same (%.1f cm)" % ((len1 - len0) * 100.0))
	_check(absf(_extent(live._table_piece(back), live.fit(back)["a"]) - len_back0) < 1e-4, "the back, not linked, keeps its length")
	_check(is_same(ed.sim, sim0), "the live simulator goes on (no rebuild)")
	await _frames(30)
	# ---- link front and back, then shorten both
	live._toggle_link(front, back)
	_check(live.group_of(front).size() == 2, "front and back are linked")
	var lf := _extent(fp, a)
	var lb := _extent(live._table_piece(back), live.fit(back)["a"])
	vm = _vertex_at(front, 0.4)
	live._len_press(front, vm, live.positions()[vm], Vector2(200, 200), "view")
	live.len_drag["mouse"] = Vector2(200, 200) - (live.len_drag["sdir"] as Vector2) * 300.0
	await _frames(30)
	var ds := float(live.len_drag["d"])
	live._release(Vector2.ZERO)
	var dlf := _extent(fp, a) - lf
	var dlb := _extent(live._table_piece(back), live.fit(back)["a"]) - lb
	_check(ds < -0.03, "pulled the other way the rod shortens (%.1f cm)" % (ds * 100.0))
	_check(absf(dlf - ds) < 0.003 and absf(dlb - ds) < 0.003, "front and back shorten together (%.1f, %.1f cm)" % [dlf * 100.0, dlb * 100.0])
	await _frames(20)
	var finite := true
	for p in live.positions():
		if not p.is_finite():
			finite = false
	_check(finite, "the cloth stays finite through all of it")
	# ---- leave: rebuilt from the pattern, the tack sewn by the loader
	ed._set_live_mode(false)
	_check(not live.active and not is_same(ed.sim, sim0), "leaving rebuilds the garment")
	_check((ed.meta.get("tacks", []) as Array).size() == 1, "the rebuilt garment has the tack")
