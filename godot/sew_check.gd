# "Will it sew?": a check of the start pose before the simulation runs, the way a CLO user reads the sewing lines between
# arranged pieces. For every seam it pairs the two edges point by point (as the solver will) on the start positions and
# reports:
#   gap      mean and largest distance between the paired points (the seams have to pull this far)
#   through  the share of pairs whose straight line runs through the body (the seam would drag cloth through or round
#            the body: an arm, the neck, the other side of the torso)
#   twisted  pairing the edges end to end the other way would be much closer (the seam is sewn the wrong way round)
# and a status: 0 fine, 1 far (it will sew but may crumple), 2 problem. Thresholds are this file's choices, not
# measurements: fine below 3 cm mean gap, a problem from 8 cm, or when a quarter of the pairs cross the body, or twisted.
# Pieces that sew several short seams to each other (a curved armhole is dozens of 3 cm seams) are summed up per piece pair.
class_name FailoringSewCheck
extends RefCounted

const GAP_OK := 0.03
const GAP_BAD := 0.08
const THROUGH_BAD := 0.25
const INSIDE := -0.008   # a sample this deep in the body counts as through it

var seams: Array = []    # per entry of the merged seams: {si, a, b, gap, gap_max, through, twisted, status, pairs}
var groups: Array = []   # per piece pair: {a, b, names, status, gap, gap_max, through, twisted, n, text}


# built: FailoringPlanLoader.build result (sim, chains, p2); pieces, seams_in: the merged plan as built.
func run(built: Dictionary, pieces: Array, seams_in: Array, positions: PackedVector3Array = PackedVector3Array()) -> void:
	seams = []
	groups = []
	var sim = built.get("sim", null)
	var pos: PackedVector3Array = positions if not positions.is_empty() else built.get("p2", PackedVector3Array())
	var chains: Dictionary = built.get("chains", {})
	if pos.is_empty() or chains.is_empty():
		return
	var name_of := {}
	for p in pieces:
		name_of[String(p["id"])] = String(p.get("name", p["id"]))
	var probes := PackedVector3Array()   # three samples along every pair's line, body-tested in one call
	var owners := []                     # [seam entry index, pair index]
	for si in seams_in.size():
		var s: Dictionary = seams_in[si]
		var a := String(s["a"][0])
		var b := String(s["b"][0])
		if not chains.has(a) or not chains.has(b):
			continue
		var ea := int(s["a"][1])
		var eb := int(s["b"][1])
		if ea >= (chains[a] as Array).size() or eb >= (chains[b] as Array).size():
			continue
		var ca: Array = chains[a][ea]
		var cb: Array = chains[b][eb]
		if ca.size() < 2 or cb.size() < 2:
			continue
		var rev := bool(s.get("reverse", false))
		var pairs := _pairs(ca, cb, rev)
		var flip := _pairs(ca, cb, not rev)
		var g := _gaps(pos, pairs)
		var gf := _gaps(pos, flip)
		var entry := {"si": si, "a": a, "b": b, "gap": g[0], "gap_max": g[1], "through": 0.0,
			"twisted": g[0] > GAP_OK and gf[0] < 0.6 * g[0], "status": 0, "pairs": pairs}
		var ei := seams.size()
		seams.append(entry)
		for k in pairs.size():
			var p0: Vector3 = pos[int(pairs[k][0])]
			var p1: Vector3 = pos[int(pairs[k][1])]
			for f in [0.25, 0.5, 0.75]:
				probes.append(p0.lerp(p1, f))
				owners.append([ei, k])
	# the body test (a seam line through the body)
	if sim != null and sim.has_body() and not probes.is_empty():
		var d: PackedFloat32Array = sim.body_eval(probes)   # 4 floats per point: distance, normal
		var hit := {}
		for i in owners.size():
			if d[4 * i] < INSIDE:
				hit["%d:%d" % [owners[i][0], owners[i][1]]] = true
		for ei in seams.size():
			var e: Dictionary = seams[ei]
			var n := (e["pairs"] as Array).size()
			var c := 0
			for k in n:
				if hit.has("%d:%d" % [ei, k]):
					c += 1
			e["through"] = float(c) / float(maxi(1, n))
	for e in seams:
		e["status"] = _status(float(e["gap"]), float(e["through"]), bool(e["twisted"]))
	# per piece pair
	var by := {}
	for e in seams:
		var key := "%s|%s" % [e["a"], e["b"]] if String(e["a"]) < String(e["b"]) else "%s|%s" % [e["b"], e["a"]]
		if not by.has(key):
			by[key] = {"a": e["a"], "b": e["b"], "names": [name_of.get(e["a"], e["a"]), name_of.get(e["b"], e["b"])],
				"gap_sum": 0.0, "gap_max": 0.0, "through_sum": 0.0, "twisted": 0, "n": 0}
		var gr: Dictionary = by[key]
		gr["gap_sum"] = float(gr["gap_sum"]) + float(e["gap"])
		gr["gap_max"] = maxf(float(gr["gap_max"]), float(e["gap_max"]))
		gr["through_sum"] = float(gr["through_sum"]) + float(e["through"])
		gr["twisted"] = int(gr["twisted"]) + (1 if bool(e["twisted"]) else 0)
		gr["n"] = int(gr["n"]) + 1
	for key in by.keys():
		var gr: Dictionary = by[key]
		var n := float(gr["n"])
		gr["gap"] = float(gr["gap_sum"]) / n
		gr["through"] = float(gr["through_sum"]) / n
		gr["status"] = _status(float(gr["gap"]), float(gr["through"]), int(gr["twisted"]) * 2 > int(gr["n"]))
		gr["text"] = _text(gr)
		groups.append(gr)
	groups.sort_custom(func(x, y) -> bool: return int(x["status"]) > int(y["status"]) or (int(x["status"]) == int(y["status"]) and float(x["gap"]) > float(y["gap"])))


# Point pairs as the solver sews them: the longer edge's points, each with the point at the same fraction of the other.
static func _pairs(ca: Array, cb: Array, rev: bool) -> Array:
	var out := []
	var m: int = maxi(ca.size(), cb.size())
	for k in m:
		var tt: float = float(k) / float(maxi(1, m - 1))
		var tb: float = 1.0 - tt if rev else tt
		out.append([ca[mini(ca.size() - 1, int(round(tt * float(ca.size() - 1))))], cb[mini(cb.size() - 1, int(round(tb * float(cb.size() - 1))))]])
	return out


static func _gaps(pos: PackedVector3Array, pairs: Array) -> Array:
	var s := 0.0
	var mx := 0.0
	for pr in pairs:
		var d: float = pos[int(pr[0])].distance_to(pos[int(pr[1])])
		s += d
		mx = maxf(mx, d)
	return [s / float(maxi(1, pairs.size())), mx]


static func _status(gap: float, through: float, twisted: bool) -> int:
	if twisted or through >= THROUGH_BAD or gap >= GAP_BAD:
		return 2
	if gap >= GAP_OK:
		return 1
	return 0


static func _text(gr: Dictionary) -> String:
	var names: Array = gr["names"]
	var head := "%s - %s" % [names[0], names[1]]
	var why := []
	if int(gr["twisted"]) * 2 > int(gr["n"]):
		why.append("sewn the wrong way round (flip the seam)")
	if float(gr["through"]) >= THROUGH_BAD:
		why.append("the seam runs through the body (%d%% of it)" % roundi(100.0 * float(gr["through"])))
	if float(gr["gap"]) >= GAP_OK:
		why.append("%.0f cm apart (up to %.0f)" % [100.0 * float(gr["gap"]), 100.0 * float(gr["gap_max"])])
	if why.is_empty():
		return "%s: ready (%.1f cm)" % [head, 100.0 * float(gr["gap"])]
	return "%s: %s" % [head, ", ".join(why)]


# The worst status over all seams (0 when there are none).
func worst() -> int:
	var w := 0
	for g in groups:
		w = maxi(w, int(g["status"]))
	return w


static func status_color(st: int) -> Color:
	return [Color("#4ade80"), Color("#facc15"), Color("#f87171")][clampi(st, 0, 2)]
