# Construction geometry on a pattern piece (the CAD idea of construction points and lines): helpers that are drawn on
# the cutting table, picked, dragged and snapped to, but never cut and never sent to the simulator.
#
# A piece keeps them in p["guides"], an Array of dictionaries, each with a unique "id" ("g1", "g2", ...) within the
# piece. Coordinates are the piece's own metres (like the outline), so guides move with the piece on the table.
#
#   point, free:      {"id", "type": "point", "ref": "free", "x", "y"}
#   point, derived:   {"id", "type": "point", "ref": "centroid"}            the area centroid (centre of mass of the cloth,
#                                                                            holes included) - it follows outline edits
#                     {"id", "type": "point", "ref": "mid", "e"}             the middle of edge e
#                     {"id", "type": "point", "ref": "corner", "i"}          corner i
#                     {"id", "type": "point", "ref": "hole", "h"}            the centre of hole h
#   line:             {"id", "type": "line", "a": point id, "b": point id}   the line through two guide points
#                     {"id", "type": "line", "a": point id, "angle": rad}    the line through one point at an angle
#                                                                            (0 = across the table, PI/2 = up)
# Lines are construction lines: drawn across the whole view, dashed.
#
# Derived points follow the piece's geometry. Edits that renumber corners or edges keep them on the same feature
# (on_insert, on_remove); a derived point whose feature disappears becomes a free point where it was (freeze).
extends RefCounted

const EPS := 1e-12


static func list(p: Dictionary) -> Array:
	var g = p.get("guides", null)
	if g is Array:
		return g
	return []


static func find(p: Dictionary, gid: String) -> Variant:
	for g in list(p):
		if String(g.get("id", "")) == gid:
			return g
	return null


static func new_id(p: Dictionary) -> String:
	var i := 1
	while find(p, "g%d" % i) != null:
		i += 1
	return "g%d" % i


static func _push(p: Dictionary, g: Dictionary) -> String:
	if not (p.get("guides", null) is Array):
		p["guides"] = []
	g["id"] = new_id(p)
	(p["guides"] as Array).append(g)
	return String(g["id"])


static func add_free(p: Dictionary, at: Vector2) -> String:
	return _push(p, {"type": "point", "ref": "free", "x": at.x, "y": at.y})


static func add_ref(p: Dictionary, ref: String, index: int = -1) -> String:
	var g := {"type": "point", "ref": ref}
	match ref:
		"mid":
			g["e"] = index
		"corner":
			g["i"] = index
		"hole":
			g["h"] = index
	# one derived point per feature: adding it again returns the existing one
	for o in list(p):
		if String(o.get("type", "")) == "point" and String(o.get("ref", "")) == ref and int(o.get("e", o.get("i", o.get("h", -1)))) == index:
			return String(o["id"])
	return _push(p, g)


static func add_line(p: Dictionary, a: String, b: String) -> String:
	return _push(p, {"type": "line", "a": a, "b": b})


static func add_axis(p: Dictionary, a: String, angle: float) -> String:
	return _push(p, {"type": "line", "a": a, "angle": angle})


# Removes guide gid, and the lines that depend on it when it is a point. Returns how many guides were removed.
static func remove(p: Dictionary, gid: String) -> int:
	var gs := list(p)
	var before := gs.size()
	p["guides"] = gs.filter(func(g) -> bool:
		return String(g.get("id", "")) != gid and String(g.get("a", "")) != gid and String(g.get("b", "")) != gid)
	return before - (p["guides"] as Array).size()


# ------------------------------------------------------------------ geometry
# Area centroid of the cloth: the outline minus its holes (falls back to the corner average for a degenerate outline).
static func centroid(p: Dictionary) -> Vector2:
	var a := 0.0
	var acc := Vector2.ZERO
	var rings := [p["outline"]]
	for h in p.get("holes", []):
		rings.append(h)
	for ri in rings.size():
		var ring: Array = rings[ri]
		var n := ring.size()
		var ra := 0.0
		var racc := Vector2.ZERO
		for k in n:
			var q0 := Vector2(float(ring[k][0]), float(ring[k][1]))
			var q1 := Vector2(float(ring[(k + 1) % n][0]), float(ring[(k + 1) % n][1]))
			var cr := q0.x * q1.y - q1.x * q0.y
			ra += cr
			racc += (q0 + q1) * cr
		if ri > 0 and signf(ra) == signf(a):   # a hole is subtracted whatever its winding
			ra = -ra
			racc = -racc
		a += ra
		acc += racc
	if absf(a) > EPS:
		return acc / (3.0 * a)
	var avg := Vector2.ZERO
	for q in p["outline"]:
		avg += Vector2(float(q[0]), float(q[1]))
	return avg / maxf(1.0, float((p["outline"] as Array).size()))


static func ring_centre(ring: Array) -> Vector2:
	var c := Vector2.ZERO
	for q in ring:
		c += Vector2(float(q[0]), float(q[1]))
	return c / maxf(1.0, float(ring.size()))


# The position of point guide g in piece-local metres, or null when its feature is gone.
static func point_pos(p: Dictionary, g: Dictionary) -> Variant:
	var o: Array = p["outline"]
	var n := o.size()
	match String(g.get("ref", "free")):
		"free":
			return Vector2(float(g.get("x", 0.0)), float(g.get("y", 0.0)))
		"centroid":
			return centroid(p)
		"mid":
			var e := int(g.get("e", -1))
			if e < 0 or e >= n:
				return null
			return (Vector2(float(o[e][0]), float(o[e][1])) + Vector2(float(o[(e + 1) % n][0]), float(o[(e + 1) % n][1]))) * 0.5
		"corner":
			var i := int(g.get("i", -1))
			if i < 0 or i >= n:
				return null
			return Vector2(float(o[i][0]), float(o[i][1]))
		"hole":
			var hs: Array = p.get("holes", [])
			var h := int(g.get("h", -1))
			if h < 0 or h >= hs.size():
				return null
			return ring_centre(hs[h])
	return null


# A line guide as [point on it, unit direction] in piece-local metres, or [] when a point it needs is gone.
static func line_of(p: Dictionary, g: Dictionary) -> Array:
	var ga = find(p, String(g.get("a", "")))
	if ga == null:
		return []
	var pa = point_pos(p, ga)
	if pa == null:
		return []
	if g.has("b"):
		var gb = find(p, String(g.get("b", "")))
		if gb == null:
			return []
		var pb = point_pos(p, gb)
		if pb == null or (pb - pa).length() < 1e-9:
			return []
		return [pa, (pb - pa).normalized()]
	var ang := float(g.get("angle", 0.0))
	return [pa, Vector2(cos(ang), sin(ang))]


static func label(p: Dictionary, g: Dictionary) -> String:
	if String(g.get("type", "")) == "line":
		var ga = find(p, String(g.get("a", "")))
		var an := label(p, ga) if ga != null else "?"
		if g.has("b"):
			var gb = find(p, String(g.get("b", "")))
			return "Line %s - %s" % [an, label(p, gb) if gb != null else "?"]
		var deg := fposmod(rad_to_deg(float(g.get("angle", 0.0))), 180.0)
		var dir := "across" if absf(deg) < 0.01 or absf(deg - 180.0) < 0.01 else ("up" if absf(deg - 90.0) < 0.01 else "%.0f°" % deg)
		return "Line %s through %s" % [dir, an]
	match String(g.get("ref", "free")):
		"centroid":
			return "Centre of mass"
		"mid":
			return "Middle of edge %d" % (int(g.get("e", 0)) + 1)
		"corner":
			return "Corner %d" % (int(g.get("i", 0)) + 1)
		"hole":
			return "Centre of hole %d" % (int(g.get("h", 0)) + 1)
	return "Point %s" % String(g.get("id", ""))


# ------------------------------------------------------------------ keeping derived points on their features
# Turns derived point g into a free point where it is now (before the edit that would move its feature away).
static func freeze(p: Dictionary, g: Dictionary) -> void:
	var at = point_pos(p, g)
	if at == null:
		at = Vector2.ZERO
	for k in ["e", "i", "h"]:
		g.erase(k)
	g["ref"] = "free"
	g["x"] = (at as Vector2).x
	g["y"] = (at as Vector2).y


static func freeze_all(p: Dictionary) -> void:
	for g in list(p):
		if String(g.get("type", "")) == "point" and String(g.get("ref", "free")) not in ["free", "centroid"]:
			freeze(p, g)


# k corners are about to be inserted after corner e (call before the outline changes): the middle of edge e is no
# longer an edge's middle and is frozen; later edges and corners move on by k.
static func on_insert(p: Dictionary, e: int, k: int) -> void:
	for g in list(p):
		match String(g.get("ref", "")):
			"mid":
				if int(g["e"]) == e:
					freeze(p, g)
				elif int(g["e"]) > e:
					g["e"] = int(g["e"]) + k
			"corner":
				if int(g["i"]) > e:
					g["i"] = int(g["i"]) + k


# Corner i is about to be removed (call before the outline changes): its point and the middles of its two edges are
# frozen; later edges and corners move back by one.
static func on_remove(p: Dictionary, i: int) -> void:
	var n := (p["outline"] as Array).size()
	var prev := (i - 1 + n) % n
	for g in list(p):
		match String(g.get("ref", "")):
			"mid":
				if int(g["e"]) == i or int(g["e"]) == prev:
					freeze(p, g)
				elif int(g["e"]) > i:
					g["e"] = int(g["e"]) - 1
			"corner":
				if int(g["i"]) == i:
					freeze(p, g)
				elif int(g["i"]) > i:
					g["i"] = int(g["i"]) - 1


# Hole h is about to be removed: its centre point is frozen, later holes move back by one.
static func on_remove_hole(p: Dictionary, h: int) -> void:
	for g in list(p):
		if String(g.get("ref", "")) == "hole":
			if int(g["h"]) == h:
				freeze(p, g)
			elif int(g["h"]) > h:
				g["h"] = int(g["h"]) - 1


# The piece is mirrored left-right about x = cx with its corner order reversed: free points are mirrored, derived
# points on corners and edges are frozen first (their indices change), line angles mirror.
static func on_mirror(p: Dictionary, map_x: Callable) -> void:
	freeze_all(p)
	for g in list(p):
		if String(g.get("type", "")) == "point" and String(g.get("ref", "")) == "free":
			g["x"] = map_x.call(float(g["x"]))
		elif String(g.get("type", "")) == "line" and g.has("angle"):
			g["angle"] = PI - float(g["angle"])


# Free points through a map of world points (rotate, scale; see cut_geom.gd _map_piece). o is the table offset.
static func map_points(p: Dictionary, o: Vector2, f: Callable) -> void:
	for g in list(p):
		if String(g.get("type", "")) == "point" and String(g.get("ref", "")) == "free":
			var w: Vector2 = f.call(Vector2(float(g["x"]), float(g["y"])) + o)
			g["x"] = w.x - o.x
			g["y"] = w.y - o.y


static func rotate_lines(p: Dictionary, angle: float) -> void:
	for g in list(p):
		if String(g.get("type", "")) == "line" and g.has("angle"):
			g["angle"] = float(g["angle"]) + angle
