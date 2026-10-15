# Arrangement points and the sewing check: the editor boots the tee, the sewing check lists every pair of sewn pieces
# with a status, the arrangement points cover the torso, arms and legs, a piece put on a torso dot gets that placement
# (one undoable action), a sleeve put on the shoulder dot gets a cap and its seams start no farther from the armholes
# than at the arm's default sTop. Run headless:
#   godot --headless --path godot res://tests/arrange_check.tscn
extends Node

const Arrange = preload("res://arrange.gd")
const Wrap = preload("res://wrap.gd")

var fails := 0
var ed: Node


func _check(ok: bool, what: String) -> void:
	print("ARRANGE %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


func _settle() -> void:
	await get_tree().create_timer(0.6).timeout
	var t0 := Time.get_ticks_msec()
	while ed.debounce.time_left > 0.0 and Time.get_ticks_msec() - t0 < 20000:
		await get_tree().process_frame
	await get_tree().process_frame


func _ready() -> void:
	ed = load("res://editor.tscn").instantiate()
	add_child(ed)
	var t0 := Time.get_ticks_msec()
	while ed.sim == null and Time.get_ticks_msec() - t0 < 180000:
		await get_tree().process_frame
	if ed.sim == null:
		print("ARRANGE FAIL no sim")
		get_tree().quit(1)
		return
	await _settle()
	await _run()
	print("ARRANGE %s" % ("PASSED" if fails == 0 else "FAILED with %d failure(s)" % fails))
	remove_child(ed)
	ed.free()
	get_tree().quit(0 if fails == 0 else 1)


func _run() -> void:
	# the default garment is the FreeSewing tee: front, back, two sleeves
	var sc = ed.sew_check
	_check(not sc.groups.is_empty(), "the sewing check lists the sewn piece pairs (%d)" % sc.groups.size())
	var fb := false
	for g in sc.groups:
		if (g["names"] as Array).has("Front") and (g["names"] as Array).has("Back"):
			fb = int(g["status"]) == 0
	_check(fb, "front and back start sewn together (side and shoulder seams green)")
	var pv = ed.place_view
	pv._ensure_points()
	var kinds := {}
	for pt in pv.arr.points:
		kinds[pt["kind"]] = int(kinds.get(pt["kind"], 0)) + 1
	_check(int(kinds.get("torso", 0)) >= 24 and int(kinds.get("arm", 0)) == 6 and int(kinds.get("leg", 0)) == 6, "dots on the torso (%d), arms (%d) and legs (%d)" % [kinds.get("torso", 0), kinds.get("arm", 0), kinds.get("leg", 0)])
	# a torso dot: the back on the back at waist height
	var back = null
	var sleeve = null
	for p in ed.pieces:
		if String(p.get("name", "")) == "Back":
			back = p
		if String((p.get("wrap", {}) as Dictionary).get("mode", "")) == "arm" and String(p["wrap"].get("side", "")) == "L":
			sleeve = p
	_check(back != null and sleeve != null, "the tee has a back and a left sleeve")
	if back == null or sleeve == null:
		return
	var di := -1
	for i in pv.arr.points.size():
		if String(pv.arr.points[i]["id"]) == "torso_waist_180":
			di = i
	ed.sel = String(back["id"])
	_check(di >= 0 and pv.put_on(di), "the back can be put on the back-at-waist dot")
	var wb: Dictionary = back["wrap"]
	_check(is_equal_approx(float(wb.get("theta", -1.0)), 180.0), "its placement is round the back (theta 180, %s)" % wb.get("theta"))
	_check(String(ed.history.undo_name()).begins_with("Arrange Back"), "one undoable action (%s)" % ed.history.undo_name())
	ed.do_undo()
	await _settle()
	# the sleeve on the shoulder dot: a cap, and its seams start at least as close to the armholes as the plain wrap
	var plain: Dictionary = (sleeve["wrap"] as Dictionary).duplicate(true)
	plain.erase("cap")
	var fitted := Arrange.fit_sleeve(sleeve, plain, ed.pieces, ed.seams, ed._start_body["sections"], ed._start_body["arms"])
	_check(fitted.has("cap"), "a set-in sleeve gets a cap")
	_check(_gap(sleeve, fitted) <= _gap(sleeve, plain) + 1e-6, "its seams start closer to the armholes (%.1f cm, plain %.1f cm)" % [_gap(sleeve, fitted) * 100.0, _gap(sleeve, plain) * 100.0])


# mean start distance of the sleeve's seam sides from their partners, with wrap w on the sleeve
func _gap(sl: Dictionary, w: Dictionary) -> float:
	var q: Dictionary = sl.duplicate(true)
	q["wrap"] = w
	var secs: Dictionary = ed._start_body["sections"]
	var arms: Dictionary = ed._start_body["arms"]
	var ws = Wrap.new(q, secs, arms)
	var sum := 0.0
	var n := 0
	for s in ed.seams:
		var a := String(s["a"][0])
		var b := String(s["b"][0])
		if (a == String(sl["id"])) == (b == String(sl["id"])):
			continue
		var mine: Array = s["a"] if a == String(sl["id"]) else s["b"]
		var oth: Array = s["b"] if a == String(sl["id"]) else s["a"]
		var other = null
		for p in ed.pieces:
			if String(p["id"]) == String(oth[0]):
				other = p
		var wo = Wrap.new(other, secs, arms)
		var pm := Arrange._edge_point(sl["outline"], int(mine[1]), 0.5)
		var po := Arrange._edge_point(other["outline"], int(oth[1]), 0.5)
		sum += ws.point(pm.x, pm.y).distance_to(wo.point(po.x, po.y))
		n += 1
	return sum / float(maxi(1, n))
