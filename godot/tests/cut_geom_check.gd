# Cutting table geometry check (step 3): rotate and scale about a point (holes and grain follow), align and distribute
# piece boxes, and edge curves (a Bezier edge stored as corners, re-bent by its record). Pure data, no editor. Run headless:
#   godot --headless --path godot res://tests/cut_geom_check.tscn
# Prints GEOMCHECK lines and quits 0 when every check passes, 1 otherwise.
extends Node

const Geom = preload("res://cut_geom.gd")

var fails := 0


func _check(ok: bool, what: String) -> void:
	print("GEOMCHECK %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


func _near(a: float, b: float, tol := 1e-6) -> bool:
	return absf(a - b) <= tol


func _sq(x: float, y: float, s: float, table_x := 0.0, table_y := 0.0) -> Dictionary:
	return {"id": "p%d" % int(x * 100 + y * 1000), "outline": [[0.0, 0.0], [s, 0.0], [s, s], [0.0, s]], "grain": 0.0,
		"table": {"x": x + table_x, "y": y + table_y}, "wrap": {"theta": 0.0, "y": 0.5, "clear": 0.012, "ring": 0.0}}


func _ready() -> void:
	# rotate a 10 cm square 90 degrees about the origin (its table offset is 0): corners move, grain turns
	var p := _sq(0.0, 0.0, 0.10)
	Geom.rotate_pieces([p], PI * 0.5, Vector2.ZERO)
	var o: Array = p["outline"]
	_check(_near(float(o[1][0]), 0.0, 1e-6) and _near(float(o[1][1]), 0.10, 1e-6), "a 90 degree turn moves the corner (10, 0) to (0, 10) cm")
	_check(_near(float(p["grain"]), PI * 0.5), "the grain turns with the piece")
	# twelve 30 degree turns bring a piece back
	var q := _sq(0.2, 0.1, 0.10)
	var before: Array = q["outline"].duplicate(true)
	for i in 12:
		Geom.rotate_pieces([q], PI / 6.0, Geom.group_centre([q]))
	var same := true
	for i in before.size():
		if not (_near(float(q["outline"][i][0]), float(before[i][0]), 1e-7) and _near(float(q["outline"][i][1]), float(before[i][1]), 1e-7)):
			same = false
	_check(same, "twelve 30 degree turns about the centre return the outline")
	# a hole and a curve record follow the rotation
	var h := _sq(0.0, 0.0, 0.2)
	h["holes"] = [[[0.05, 0.05], [0.15, 0.05], [0.15, 0.15], [0.05, 0.15]]]
	Geom.rotate_pieces([h], PI * 0.5, Vector2(0.1, 0.1))
	var hb := _box_of(h["holes"][0])
	_check(_near((hb[0] + hb[2]) * 0.5, 0.1, 1e-6) and _near((hb[1] + hb[3]) * 0.5, 0.1, 1e-6) and _near(hb[2] - hb[0], 0.1, 1e-6),
		"the hole turns about the pivot and keeps its size")
	# scale 2x about the centre: the area is four times larger
	var s := _sq(0.0, 0.0, 0.10)
	var c := Geom.group_centre([s])
	Geom.scale_pieces([s], 2.0, c)
	var sb := Geom.world_box(s)
	_check(_near((float(sb[1].x) - float(sb[0].x)) * (float(sb[1].y) - float(sb[0].y)), 0.04, 1e-6), "a 2x scale makes a 10 cm square 20 cm (area 0.04 m2)")
	_check(_near(float(sb[0].x) + float(sb[1].x), 2.0 * c.x, 1e-6), "the scale keeps the centre fixed")

	# align: three boxes of different sizes, aligned to the first's left, right, top, bottom, and centres
	var a := _sq(0.0, 0.0, 0.10)
	var b := _sq(0.5, 0.3, 0.20)
	var ref := Geom.world_box(a)
	Geom.align_pieces([b], "left", ref)
	_check(_near(float(Geom.world_box(b)[0].x), float(ref[0].x), 1e-6), "align left puts the box's left edge on the reference")
	Geom.align_pieces([b], "top", ref)
	_check(_near(float(Geom.world_box(b)[1].y), float(ref[1].y), 1e-6), "align top puts the box's top edge on the reference")
	Geom.align_pieces([b], "centre_y", ref)
	_check(_near((float(Geom.world_box(b)[0].y) + float(Geom.world_box(b)[1].y)) * 0.5, (float(ref[0].y) + float(ref[1].y)) * 0.5, 1e-6), "align centres vertically")
	Geom.align_pieces([b], "right", ref)
	_check(_near(float(Geom.world_box(b)[1].x), float(ref[1].x), 1e-6), "align right puts the box's right edge on the reference")

	# distribute: three pieces with gaps that are unequal before, equal after
	var d1 := _sq(0.0, 0.0, 0.10)
	var d2 := _sq(0.30, 0.0, 0.25)
	var d3 := _sq(0.95, 0.0, 0.05)
	Geom.distribute_pieces([d1, d2, d3], 0)
	var b1 := Geom.world_box(d1)
	var b2 := Geom.world_box(d2)
	var b3 := Geom.world_box(d3)
	var g1 := float(b2[0].x) - float(b1[1].x)
	var g2 := float(b3[0].x) - float(b2[1].x)
	_check(_near(g1, g2, 1e-6) and g1 > 0.0, "distribute makes the gaps equal (%.4f m and %.4f m)" % [g1, g2])
	_check(_near(float(b3[1].x), 1.0, 1e-6), "distribute keeps the outermost right edge (1.00 m)")

	# edge curve: a 20 cm edge with a 5 cm bulge becomes 8 segments; the middle sample is bulge/2 off the chord
	var sqr: Array = [[0.0, 0.0], [0.2, 0.0], [0.2, 0.2], [0.0, 0.2]]
	var cv := Geom.curve_edge(sqr, 0, 0.05)
	var o2: Array = cv["outline"]
	_check(o2.size() == 4 + 7, "an edge curve adds 7 corners (%d corners now)" % o2.size())
	var mid: Array = o2[4]  # corner 0, then 7 new corners (1..7): the middle one, t = 0.5, is index 4
	_check(_near(float(mid[0]), 0.1, 1e-6) and _near(float(mid[1]), -0.025, 1e-6), "the curve's middle is at (10, -2.5) cm: half the bulge outward, to the right of a->b")
	var pc := {"outline": o2, "curves": [cv["curve"]]}
	_check(Geom.curve_at_edge(pc, 0) == 0, "the curve record is found at its first edge")
	_check(Geom.rebend(pc, 0, 0.10), "a curve re-bends by its record")
	_check(_near(float(pc["outline"][4][1]), -0.05, 1e-6), "re-bending to 10 cm moves the middle to 5 cm outward")
	_check(pc["outline"].size() == 4 + 7, "re-bending keeps the corner count")
	pc["outline"][8] = [0.2, 0.25]  # the far end corner moves: the curve is no longer the record's
	_check(not Geom.rebend(pc, 0, 0.02), "a curve whose end corner has moved does not re-bend")
	print("GEOMCHECK %s" % ("PASSED" if fails == 0 else "FAILED with %d failure(s)" % fails))
	get_tree().quit(0 if fails == 0 else 1)


static func _box_of(o: Array) -> Array:
	var mn := Vector2(1e9, 1e9)
	var mx := Vector2(-1e9, -1e9)
	for q in o:
		mn = mn.min(Vector2(float(q[0]), float(q[1])))
		mx = mx.max(Vector2(float(q[0]), float(q[1])))
	return [mn.x, mn.y, mx.x, mx.y]
