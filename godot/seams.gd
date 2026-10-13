# Seams with ranges (step 4: partial seams). A seam side is either the whole edge, as before:
#   [piece id, edge index]
# or a part of the edge, from fraction t0 to t1 along it (0 = the edge's first corner, 1 = its last):
#   [piece id, edge index, t0, t1]
# A seam {a, b, reverse} joins side a's range to side b's range; the rest of each edge stays open (an armhole, a vent,
# a slit). Two ranges on one edge must not overlap. The seam's reverse flag works as for whole edges.
#
# The cutting table and the editor keep ranges as they are. Only the simulator needs whole edges, so expand() splits the
# outlines at the range ends and rewrites every side as a whole (sub-)edge of the split outline. It is the step the
# editor runs before the plan goes to PlanLoader.build (see editor.gd rebuild).
extends RefCounted

const EPS := 1e-9


# The range of a side as Vector2(t0, t1). A whole-edge side (two elements) is (0, 1).
static func side_range(side: Array) -> Vector2:
	if side.size() >= 4:
		return Vector2(float(side[2]), float(side[3]))
	return Vector2(0.0, 1.0)


static func is_full(side: Array) -> bool:
	var r := side_range(side)
	return r.x <= EPS and r.y >= 1.0 - EPS


# A side for piece pid, edge e, range t0..t1: the two-element form when the range is the whole edge.
static func make_side(pid: String, e: int, t0: float, t1: float) -> Array:
	if t0 <= EPS and t1 >= 1.0 - EPS:
		return [pid, e]
	return [pid, e, t0, t1]


# The point at fraction t along edge e of outline o (outline points are [x, y] pairs). Returns a Vector2.
static func point_on(o: Array, e: int, t: float) -> Vector2:
	var a: Array = o[e]
	var b: Array = o[(e + 1) % o.size()]
	return Vector2(float(a[0]), float(a[1])).lerp(Vector2(float(b[0]), float(b[1])), t)


# Length in metres of the side's range on outline o.
static func range_length(o: Array, side: Array) -> float:
	var e := int(side[1])
	var r := side_range(side)
	var a: Array = o[e]
	var b: Array = o[(e + 1) % o.size()]
	return (Vector2(float(b[0]), float(b[1])) - Vector2(float(a[0]), float(a[1]))).length() * (r.y - r.x)


# The middle of the side's range on outline o (where the table draws the seam's end).
static func range_mid(o: Array, side: Array) -> Vector2:
	var r := side_range(side)
	return point_on(o, int(side[1]), (r.x + r.y) * 0.5)


# True when a range [t0, t1] on edge e of piece pid overlaps a range of another seam (seam index skip is ignored).
static func overlaps(seams: Array, pid: String, e: int, t0: float, t1: float, skip: int) -> bool:
	for i in seams.size():
		if i == skip:
			continue
		var s: Dictionary = seams[i]
		for side in [s["a"], s["b"]]:
			if String(side[0]) == pid and int(side[1]) == e:
				var r := side_range(side)
				if r.x < t1 - 1e-6 and r.y > t0 + 1e-6:
					return true
	return false


# A side after its piece's outline is reversed (clockwise to counter-clockwise, or a mirror), for an outline of n corners:
# edge e becomes edge n-2-e, and a range t0..t1 becomes 1-t1..1-t0 along the reversed edge. Whole sides stay whole.
static func reversed_side(side: Array, n: int) -> Array:
	var e2 := ((n - 2 - int(side[1])) % n + n) % n
	if side.size() < 4:
		return [String(side[0]), e2]
	return [String(side[0]), e2, 1.0 - float(side[3]), 1.0 - float(side[2])]


# Whether any seam side has a range that is not the whole edge.
static func has_partial(seams: Array) -> bool:
	for s in seams:
		if not is_full(s["a"]) or not is_full(s["b"]):
			return true
	return false


# Plan level, for the simulator: returns {pieces, seams} with each piece's outline split at the range ends (the new
# corners are on the straight edge, or on the curve for a curved edge: they are points of the old edge at the same
# fractions) and every seam side as a whole edge of the split outline. Without partial sides the input is copied unchanged.
static func expand(pieces: Array, seams: Array) -> Dictionary:
	if not has_partial(seams):
		return {"pieces": pieces.duplicate(true), "seams": seams.duplicate(true)}
	# 1. the split fractions of each edge: the start and end of every partial range on it
	var splits := {}   # "pid|e" -> Array of fractions strictly inside (0, 1)
	for s in seams:
		for side in [s["a"], s["b"]]:
			if is_full(side):
				continue
			var r := side_range(side)
			var key := "%s|%d" % [String(side[0]), int(side[1])]
			if not splits.has(key):
				splits[key] = []
			if r.x > EPS:
				(splits[key] as Array).append(r.x)
			if r.y < 1.0 - EPS:
				(splits[key] as Array).append(r.y)
	# 2. the split outlines: each old corner, then the split points of the edge after it, in order
	var out_pieces := []
	var start_of := {}   # "pid|e|t" -> the index of the corner (edge index) where the piece's sub-edge at t starts
	for p in pieces:
		var pid := String(p["id"])
		var o: Array = p["outline"]
		var n := o.size()
		var no := []
		for e in n:
			var key := "%s|%d" % [pid, e]
			var ts := [0.0]
			if splits.has(key):
				var fr: Array = (splits[key] as Array).duplicate()
				fr.sort()
				for t in fr:
					if float(t) > float(ts[ts.size() - 1]) + EPS and float(t) < 1.0 - EPS:
						ts.append(float(t))
			for t in ts:
				start_of["%s|%d|%.6f" % [pid, e, float(t)]] = no.size()
				var q := point_on(o, e, float(t))
				no.append([q.x, q.y])
		var q2: Dictionary = (p as Dictionary).duplicate(true)
		q2["outline"] = no
		out_pieces.append(q2)
	# 3. the seams, each side as the whole sub-edge that starts at its range start
	var out_seams := []
	for s in seams:
		var ns: Dictionary = (s as Dictionary).duplicate(true)
		for side_name in ["a", "b"]:
			var side: Array = s[side_name]
			var r := side_range(side)
			var pid := String(side[0])
			var key := "%s|%d|%.6f" % [pid, int(side[1]), r.x if r.x > EPS else 0.0]
			ns[side_name] = [pid, int(start_of.get(key, 0))]
		out_seams.append(ns)
	return {"pieces": out_pieces, "seams": out_seams}
