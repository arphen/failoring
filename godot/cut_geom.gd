# Geometry for the cutting table's transform and edit tools (step 3): rotate, scale, align and distribute pieces, and
# edge curves. Pure functions on piece dictionaries ({id, outline, holes, table, grain, ...}) with outlines in the
# piece's own metres and the table offset ("table") in table metres. A world point is local + table offset.
# Everything here is static; the table and the editor call it inside their undoable actions.
extends RefCounted

const Guides = preload("res://guides.gd")
const CURVE_SEGMENTS := 8   # an edge turned into a curve has this many segments (7 new corners)


static func offset_of(p: Dictionary) -> Vector2:
	var t: Dictionary = p.get("table", {"x": 0.0, "y": 0.0})
	return Vector2(float(t.get("x", 0.0)), float(t.get("y", 0.0)))


# The world box of a piece (outline plus table offset) as [min Vector2, max Vector2].
static func world_box(p: Dictionary) -> Array:
	var o := offset_of(p)
	var mn := Vector2(1e9, 1e9)
	var mx := Vector2(-1e9, -1e9)
	for q in p["outline"]:
		var w := Vector2(float(q[0]), float(q[1])) + o
		mn = mn.min(w)
		mx = mx.max(w)
	return [mn, mx]


# The centre of the union of the given pieces' world boxes (the pivot for a group transform).
static func group_centre(pieces: Array) -> Vector2:
	if pieces.is_empty():
		return Vector2.ZERO
	var mn := Vector2(1e9, 1e9)
	var mx := Vector2(-1e9, -1e9)
	for p in pieces:
		var b := world_box(p)
		mn = mn.min(b[0])
		mx = mx.max(b[1])
	return (mn + mx) * 0.5


# Applies a map of world points to a piece: its outline and holes are mapped and stored back in local coordinates.
# The table offset stays where it is, so a piece moves on the table only through the points it is given.
static func _map_piece(p: Dictionary, f: Callable) -> void:
	var o := offset_of(p)
	var out := []
	for q in p["outline"]:
		var w: Vector2 = f.call(Vector2(float(q[0]), float(q[1])) + o)
		out.append([w.x - o.x, w.y - o.y])
	p["outline"] = out
	if p.has("holes"):
		var hs := []
		for h in p["holes"]:
			var hh := []
			for q in h:
				var w: Vector2 = f.call(Vector2(float(q[0]), float(q[1])) + o)
				hh.append([w.x - o.x, w.y - o.y])
			hs.append(hh)
		p["holes"] = hs
	if p.has("curves"):
		for c in p["curves"]:
			c["a"] = _map_pt(c["a"], o, f)
			c["b"] = _map_pt(c["b"], o, f)
	Guides.map_points(p, o, f)  # free construction points move with the cloth


static func _map_pt(q: Array, o: Vector2, f: Callable) -> Array:
	var w: Vector2 = f.call(Vector2(float(q[0]), float(q[1])) + o)
	return [w.x - o.x, w.y - o.y]


# Rotates each piece by `angle` radians about the world point `centre`. The grain turns with the piece.
static func rotate_pieces(pieces: Array, angle: float, centre: Vector2) -> void:
	var c := cos(angle)
	var s := sin(angle)
	for p in pieces:
		_map_piece(p, func(w: Vector2) -> Vector2:
			var d := w - centre
			return centre + Vector2(d.x * c - d.y * s, d.x * s + d.y * c))
		p["grain"] = float(p.get("grain", 0.0)) + angle
		Guides.rotate_lines(p, angle)


# Scales each piece by `factor` about the world point `centre`. Grain and placement on the body are unchanged.
static func scale_pieces(pieces: Array, factor: float, centre: Vector2) -> void:
	for p in pieces:
		_map_piece(p, func(w: Vector2) -> Vector2: return centre + (w - centre) * factor)


# Moves pieces so their boxes line up: mode "left", "right", "top", "bottom", "centre_x" or "centre_y", against the
# reference box (world [min, max]). Only table offsets change.
static func align_pieces(pieces: Array, mode: String, ref: Array) -> void:
	for p in pieces:
		var b := world_box(p)
		var d := Vector2.ZERO
		match mode:
			"left":
				d.x = float(ref[0].x) - float(b[0].x)
			"right":
				d.x = float(ref[1].x) - float(b[1].x)
			"top":
				d.y = float(ref[1].y) - float(b[1].y)
			"bottom":
				d.y = float(ref[0].y) - float(b[0].y)
			"centre_x":
				d.x = (float(ref[0].x) + float(ref[1].x)) * 0.5 - (float(b[0].x) + float(b[1].x)) * 0.5
			"centre_y":
				d.y = (float(ref[0].y) + float(ref[1].y)) * 0.5 - (float(b[0].y) + float(b[1].y)) * 0.5
		_shift(p, d)


# Spaces the pieces evenly along x (or y) between the first and the last box: the gaps are equal. Pieces are taken in
# the order of their box's low edge; the two outermost stay where they are.
static func distribute_pieces(pieces: Array, axis: int) -> void:
	if pieces.size() < 3:
		return
	var sorted := pieces.duplicate()
	sorted.sort_custom(func(a, b) -> bool: return float(world_box(a)[0][axis]) < float(world_box(b)[0][axis]))
	var first: Array = world_box(sorted[0])
	var last: Array = world_box(sorted[sorted.size() - 1])
	var span := float(last[1][axis]) - float(first[0][axis])
	var total := 0.0
	for p in sorted:
		var b := world_box(p)
		total += float(b[1][axis]) - float(b[0][axis])
	var gap := (span - total) / float(sorted.size() - 1)
	var cursor := float(first[0][axis])
	for i in sorted.size():
		var p: Dictionary = sorted[i]
		var b := world_box(p)
		var size := float(b[1][axis]) - float(b[0][axis])
		if i > 0 and i < sorted.size() - 1:
			var d := Vector2.ZERO
			d[axis] = cursor - float(b[0][axis])
			_shift(p, d)
		cursor += size + gap


static func _shift(p: Dictionary, d: Vector2) -> void:
	var t: Dictionary = p.get("table", {"x": 0.0, "y": 0.0})
	t["x"] = float(t.get("x", 0.0)) + d.x
	t["y"] = float(t.get("y", 0.0)) + d.y
	p["table"] = t


# The interior points of a quadratic Bezier from a to b. The control point is the midpoint pushed `bulge` metres to the
# right of a->b, which is outward for a counter-clockwise outline (negative: inward). n segments give n-1 interior points.
static func bezier_points(a: Vector2, b: Vector2, bulge: float, n: int) -> Array:
	var d := b - a
	var len := d.length()
	var nrm := Vector2(d.y, -d.x) / len if len > 0.0 else Vector2.ZERO
	var c := (a + b) * 0.5 + nrm * bulge
	var out := []
	for k in range(1, n):
		var t := float(k) / float(n)
		var p := (1.0 - t) * (1.0 - t) * a + 2.0 * (1.0 - t) * t * c + t * t * b
		out.append([p.x, p.y])
	return out


# The outline with edge e (corner e to corner e+1) replaced by a curve: the interior points are inserted after corner e.
# Returns {outline, curve} where curve is the record {a, b, bulge, n} kept on the piece.
static func curve_edge(outline: Array, e: int, bulge: float, n: int = CURVE_SEGMENTS) -> Dictionary:
	var m := outline.size()
	var a: Array = outline[e]
	var b: Array = outline[(e + 1) % m]
	var pts := bezier_points(Vector2(float(a[0]), float(a[1])), Vector2(float(b[0]), float(b[1])), bulge, n)
	var out := []
	for i in m:
		out.append(outline[i])
		if i == e:
			for q in pts:
				out.append(q)
	return {"outline": out, "curve": {"a": [float(a[0]), float(a[1])], "b": [float(b[0]), float(b[1])], "bulge": bulge, "n": n}}


# Re-bends a curve record of piece p to a new bulge (metres). The curve is found by its end corners; when they have moved
# (or the curve is gone) nothing changes and false is returned.
static func rebend(p: Dictionary, curve_index: int, bulge: float) -> bool:
	var curves: Array = p.get("curves", [])
	if curve_index < 0 or curve_index >= curves.size():
		return false
	var c: Dictionary = curves[curve_index]
	var o: Array = p["outline"]
	var n := int(c["n"])
	var m := o.size()
	var start := -1
	for i in m:
		if _same_pt(o[i], c["a"]) and _same_pt(o[(i + n) % m], c["b"]):
			start = i
			break
	if start < 0:
		return false
	var pts := bezier_points(Vector2(float(c["a"][0]), float(c["a"][1])), Vector2(float(c["b"][0]), float(c["b"][1])), bulge, n)
	for k in pts.size():
		o[(start + 1 + k) % m] = pts[k]
	c["bulge"] = bulge
	return true


static func _same_pt(q: Array, r: Array) -> bool:
	return absf(float(q[0]) - float(r[0])) < 1e-9 and absf(float(q[1]) - float(r[1])) < 1e-9


# The curve record index whose end corners are at edge e (its start corner) of the outline, or -1.
static func curve_at_edge(p: Dictionary, e: int) -> int:
	var curves: Array = p.get("curves", [])
	var o: Array = p["outline"]
	var m := o.size()
	for i in curves.size():
		var c: Dictionary = curves[i]
		if _same_pt(o[e], c["a"]) and _same_pt(o[(e + int(c["n"])) % m], c["b"]):
			return i
	return -1
