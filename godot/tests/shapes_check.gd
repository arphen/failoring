# Shapes and holes check (step 2 of the cutting table work): the Add shape generators (areas, corner counts, bounding box),
# the holes rule (inside the outline, clear of other holes), the placement at a view centre, mirroring with holes, and the
# mesher's hole support through the simulator's add_polygon_panel_with_holes (the mesh area is the outline minus the holes).
# Pure data and one mesh, no editor. Run headless:
#   godot --headless --path godot res://tests/shapes_check.tscn
# Prints SHAPECHECK lines and quits 0 when every check passes, 1 otherwise.
extends Node

const Shapes = preload("res://shapes.gd")
const Table = preload("res://table.gd")
const PlanLoader = preload("res://plan_loader.gd")

var fails := 0


func _check(ok: bool, what: String) -> void:
	print("SHAPECHECK %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


func _near(a: float, b: float, tol: float) -> bool:
	return absf(a - b) <= tol


static func _area(o: Array) -> float:
	return absf(Table.signed_area(o))


func _ready() -> void:
	# every kind builds a closed outline of at least 3 points, counter-clockwise, with its bounding box at the origin
	var kinds: Array = Shapes.kinds()
	_check(kinds.size() == 6, "the menu offers six shapes (%d)" % kinds.size())
	for k in kinds:
		var s: Dictionary = Shapes.make(String(k["id"]), {})
		var o: Array = s["outline"]
		_check(o.size() >= 3 and Table.signed_area(o) > 0.0, "%s is a counter-clockwise outline of %d points" % [k["label"], o.size()])
		var b := Table.bbox_of(o)
		_check(_near(b[0], 0.0, 1e-9) and _near(b[1], 0.0, 1e-9), "%s starts at the origin" % k["label"])
	# areas from the default sizes
	_check(_near(_area(Shapes.rect(0.5, 0.4)), 0.2, 1e-9), "a 50 x 40 cm rectangle is 0.2 m2")
	_check(_near(_area((Shapes.make("square", {"s": 50.0}) as Dictionary)["outline"]), 0.25, 1e-9), "a 50 cm square is 0.25 m2")
	var ell := _area(Shapes.ellipse(0.4, 0.4, Shapes.ELLIPSE_POINTS))
	_check(_near(ell, PI * 0.04, 0.02 * PI * 0.04), "a 40 cm circle is within 2%% of pi r2 (%.5f)" % ell)
	_check((Shapes.ellipse(0.4, 0.4, Shapes.ELLIPSE_POINTS) as Array).size() == 24, "a circle has 24 points")
	_check(_near(_area(Shapes.triangle(0.5, 0.5)), 0.125, 1e-9), "a 50 cm base, 50 cm high triangle is 0.125 m2")
	_check(_near(_area(Shapes.trapezoid(0.3, 0.5, 0.5)), 0.2, 1e-9), "a 30/50 cm trapezoid 50 cm high is 0.2 m2")
	var tz: Array = Shapes.trapezoid(0.3, 0.5, 0.5)
	_check(_near(float(tz[2][0]) - float(tz[3][0]), 0.3, 1e-9), "the trapezoid's top is 30 cm wide and its bottom 50 cm")
	# the poncho: a 140 cm square with a 24 cm neck hole at its centre, the hole inside the outline
	var pon: Dictionary = Shapes.make("poncho", {})
	var po: Array = pon["outline"]
	_check(_near(_area(po), 1.96, 1e-9), "the poncho square is 140 x 140 cm")
	_check((pon["holes"] as Array).size() == 1, "the poncho has one hole")
	var hole: Array = pon["holes"][0]
	var hb := Table.bbox_of(hole)
	_check(_near(hb[2] - hb[0], 0.24, 0.001) and _near(hb[3] - hb[1], 0.24, 0.001), "the neck hole is 24 cm across")
	_check(_near((hb[0] + hb[2]) * 0.5, 0.7, 1e-6) and _near((hb[1] + hb[3]) * 0.5, 0.7, 1e-6), "the neck hole is centred on the square")
	_check(Table.holes_ok(po, pon["holes"]), "the poncho hole is inside its square")
	var big: Dictionary = Shapes.make("poncho", {"neck": 30.0, "s": 100.0})
	_check(Table.holes_ok(big["outline"], big["holes"]), "a neck hole of another size is still inside")

	# the holes rule
	var sq := Shapes.rect(0.5, 0.5)
	var inside := Table.circle_hole(0.25, 0.25, 0.1)
	var outside := Table.circle_hole(0.7, 0.25, 0.1)
	var second := Table.circle_hole(0.25, 0.25, 0.05)
	var edge := Table.rect_hole(0.45, 0.45, 0.2, 0.2)
	_check(Table.holes_ok(sq, [inside]), "a hole inside a square is accepted")
	_check(not Table.holes_ok(sq, [outside]), "a hole outside the square is refused")
	_check(not Table.holes_ok(sq, [edge]), "a hole crossing the edge is refused")
	_check(not Table.holes_ok(sq, [inside, second]), "two overlapping holes are refused")
	_check(Table.holes_ok(sq, [inside, Table.circle_hole(0.4, 0.4, 0.05)]), "two separate holes are accepted")
	_check(not Table.holes_ok(sq, [[[0.1, 0.1], [0.2, 0.1]]]), "a hole with fewer than 3 corners is refused")
	var valid := Table.valid_pieces([{"id": "a", "outline": sq, "holes": [outside]}, {"id": "b", "outline": sq, "holes": [inside]}])
	_check(valid["bad"].has("a") and not valid["bad"].has("b"), "a piece whose hole is outside is dropped from the simulation, the other kept")

	# placement: the bounding box centre lands on the requested point
	var edref := {"pieces": [], "sel": null}
	var at := Vector2(0.8, 0.3)
	var placed := Table.add_shape(edref, "Rectangle", Shapes.make("rect", {"w": 40.0, "h": 20.0}), at)
	var pb := Table.bbox_of(placed["outline"])
	var off: Dictionary = placed["table"]
	_check(_near(pb[0] + float(off["x"]) + 0.2, at.x, 1e-9) and _near(pb[1] + float(off["y"]) + 0.1, at.y, 1e-9),
		"a shape is centred on the view centre point")
	_check(edref["pieces"].size() == 1 and String(edref["sel"]) == String(placed["id"]), "the new shape is added and selected")

	# mirroring keeps holes inside the outline
	var mp: Dictionary = Shapes.make("poncho", {})
	mp["id"] = "m"
	mp["name"] = "M"
	mp["wrap"] = {"theta": 0.0, "y": 0.7, "clear": 0.012, "ring": 0.0}
	mp["table"] = {"x": 0.0, "y": 0.0}
	mp["grain"] = 1.5708
	Table.mirror_piece(mp, [])
	_check(Table.holes_ok(mp["outline"], mp["holes"]), "a mirrored poncho keeps its hole inside")
	var mhb := Table.bbox_of(mp["holes"][0])
	_check(_near((mhb[0] + mhb[2]) * 0.5, 0.7, 1e-6), "the mirror keeps the neck hole centred")

	# the mesher through the simulator: a 50 cm square with a 10 cm hole (the simulator's own mesh)
	var fs := FailoringSim.new()
	fs.load_materials("res://materials/fabrics.json")
	fs.begin()
	var sq_v := PackedVector2Array()
	for q in sq:
		sq_v.append(Vector2(float(q[0]), float(q[1])))
	var hs := [PackedVector2Array(circle_pts(0.25, 0.25, 0.05))]
	var idx := fs.add_polygon_panel_with_holes(sq_v, hs, 0.02, "cotton_poplin", Vector3.ZERO, Vector3.RIGHT, Vector3.UP, 0.0, "t")
	_check(idx == 0, "the simulator takes a panel with a hole")
	_check(fs.get_warnings() == 0, "a clean hole raises no mesher warning")
	# no cloth vertex lies inside the hole, and the panel has cloth on the square's corners (outside the hole)
	var uv := fs.get_panel_vertices_2d(0)
	var n_in := 0
	var corner_near := false
	for v in uv:
		if v.distance_to(Vector2(0.25, 0.25)) < 0.05 * 0.98:
			n_in += 1
		if v.distance_to(Vector2(0.5, 0.5)) < 0.01:
			corner_near = true
	_check(n_in == 0, "no cloth vertex inside the hole (%d inside)" % n_in)
	_check(corner_near and uv.size() > 100, "the panel has its corners and interior (%d vertices)" % uv.size())
	print("SHAPECHECK %s" % ("PASSED" if fails == 0 else "FAILED with %d failure(s)" % fails))
	get_tree().quit(0 if fails == 0 else 1)


static func circle_pts(cx: float, cy: float, r: float, n: int = 24) -> PackedVector2Array:
	var out := PackedVector2Array()
	for i in n:
		var t := TAU * float(i) / float(n)
		out.append(Vector2(cx + r * cos(t), cy + r * sin(t)))
	return out
