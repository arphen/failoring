# Per-garment health checks for the Godot editor (see docs/VANISH_NOTES.md).
# One instance per simulation build. check() runs after every solver frame on the main thread, when no step is in
# flight. It reads the cloth positions and the body collider (sim.body_eval) and never steps the solver.
# It reports: a garment that came off the body, one on the floor or below hip height, NaN values, a whole-scene
# kinetic-energy spike, and a seam that does not close on the body. The editor shows the text and tints the pieces.
class_name FailoringGarmentHealth
extends RefCounted

const FALL_MEAN := 0.15   # m: mean distance from the body, beyond the start, that means the garment came off
const FLOOR_Y := 0.03     # m: a vertex this low is on the floor (the feet stand on y = 0)
const HIP_Y := 0.5        # m: a garment whose top drops below this has fallen below hip height
const SEAM_OPEN := 0.10   # m: a seam still this far apart on the body is not closed
const KE_SPIKE := 0.25    # J: whole-scene kinetic energy above this after the first frames is a spike
const KE_FROM := 4        # frames before the energy check (the gravity ramp starts the cloth moving)
const FALL_TEXT := "Garment is falling off: check seams and placement"

var _pieces := {}     # piece id -> {uid, name, first, count}
var _garments := {}   # uid -> {name, pieces: Array of piece ids}
var _seams := []      # {a, ea, b, eb, reverse, uid}
var _chains := {}     # piece id -> edge index -> vertex indices (from sim.get_edge_chain)
var _base := {}       # uid -> {mean, min_y, max_y} at the start positions
var _base_piece := {} # piece id -> {mean, min_y, max_y}
var _ok := false
var _notes := {}      # problem key -> {error, text, hot}, in the order first seen


# built: the dictionary from FailoringPlanLoader.build (sim, first, uv, chains, p2); pieces and seams: the merged
# plan that was built; names: uid -> garment name.
func _init(built: Dictionary, pieces: Array, seams: Array, names: Dictionary) -> void:
	var sim = built.get("sim", null)
	_chains = built.get("chains", {})
	for p in pieces:
		var pid := String(p["id"])
		var uid := int(p.get("garment", 0))
		var uv: PackedVector2Array = built["uv"][pid]
		_pieces[pid] = {"uid": uid, "name": String(p.get("name", pid)), "first": int(built["first"][pid]), "count": uv.size()}
		if not _garments.has(uid):
			_garments[uid] = {"name": String(names.get(uid, "Garment")), "pieces": []}
		(_garments[uid]["pieces"] as Array).append(pid)
	for s in seams:
		var a := String(s["a"][0])
		var b := String(s["b"][0])
		if not _pieces.has(a) or not _pieces.has(b):
			continue
		_seams.append({"a": a, "ea": int(s["a"][1]), "b": b, "eb": int(s["b"][1]),
			"reverse": bool(s.get("reverse", false)), "uid": int(_pieces[a]["uid"])})
	# baseline: the start positions, measured once with the same body collider the run uses
	if sim == null or not sim.has_body() or not built.has("p2"):
		return
	var p2: PackedVector3Array = built["p2"]
	var stats := _measure(sim, p2)
	_base = stats["garments"]
	_base_piece = stats["pieces"]
	_ok = true


# Per-piece and per-garment distance from the body (outside only) and height extent.
func _measure(sim, cloth: PackedVector3Array) -> Dictionary:
	var dist: PackedFloat32Array = sim.body_eval(cloth)
	var pieces := {}
	var garments := {}
	for pid in _pieces.keys():
		var pr: Dictionary = _pieces[pid]
		var uid: int = pr["uid"]
		var st := _extent(cloth, dist, int(pr["first"]), int(pr["count"]))
		pieces[pid] = st
		if not garments.has(uid):
			garments[uid] = {"sum": 0.0, "n": 0, "min_y": 1e9, "max_y": -1e9, "max": 0.0, "nan": false}
		var g: Dictionary = garments[uid]
		g["sum"] = float(g["sum"]) + float(st["sum"])
		g["n"] = int(g["n"]) + int(st["n"])
		g["min_y"] = minf(float(g["min_y"]), float(st["min_y"]))
		g["max_y"] = maxf(float(g["max_y"]), float(st["max_y"]))
		g["max"] = maxf(float(g["max"]), float(st["max"]))
		g["nan"] = bool(g["nan"]) or bool(st["nan"])
	for uid in garments.keys():
		var g: Dictionary = garments[uid]
		g["mean"] = float(g["sum"]) / maxf(1.0, float(g["n"]))
	return {"pieces": pieces, "garments": garments}


static func _extent(cloth: PackedVector3Array, dist: PackedFloat32Array, first: int, count: int) -> Dictionary:
	var sum := 0.0
	var n := 0
	var mx := 0.0
	var miny := 1e9
	var maxy := -1e9
	var nan := false
	for i in range(first, first + count):
		var v := cloth[i]
		if not (is_finite(v.x) and is_finite(v.y) and is_finite(v.z)):
			nan = true
			continue
		var d := maxf(0.0, float(dist[4 * i]))
		sum += d
		mx = maxf(mx, d)
		miny = minf(miny, v.y)
		maxy = maxf(maxy, v.y)
		n += 1
	return {"sum": sum, "n": n, "mean": sum / maxf(1.0, float(n)), "max": mx, "min_y": miny, "max_y": maxy, "nan": nan}


# One frame. cloth: all cloth vertices of the frame; ke: whole-scene kinetic energy; frame: frames so far (1-based).
# Returns {ok, text, lines, hot}: lines are the messages (errors first, at most three shown in text), hot the piece ids
# to tint. A problem stays listed until the build is replaced (Reset to start): a garment that flew off and came back
# is still reported. ok is false while an error has been seen.
func check(sim, cloth: PackedVector3Array, ke: float, frame: int) -> Dictionary:
	if not _ok or sim == null or not sim.has_body():
		return {"ok": true, "text": "", "lines": [], "hot": []}
	var st := _measure(sim, cloth)
	for uid in _garments.keys():
		var g: Dictionary = _garments[uid]
		var cur: Dictionary = st["garments"][uid]
		var base: Dictionary = _base[uid]
		var why := ""
		if bool(cur["nan"]):
			why = "invalid numbers (NaN)"
		elif float(cur["min_y"]) < FLOOR_Y and float(base["min_y"]) > 0.1:
			why = "on the floor"
		elif float(cur["max_y"]) < HIP_Y and float(base["max_y"]) > 0.8:
			why = "below hip height"
		elif float(cur["mean"]) - float(base["mean"]) > FALL_MEAN:
			why = "%.2f m from the body on average (max %.2f m)" % [float(cur["mean"]), float(cur["max"])]
		if why != "":
			var line := (FALL_TEXT + " (%s: %s)" % [g["name"], why]) if why != "invalid numbers (NaN)" else "Garment has invalid numbers (NaN): %s - press Reset to start" % g["name"]
			_note("fall:%d" % uid, true, line, _flag_pieces(st, uid, g["pieces"]))
	# a seam that does not close on the body: reported once per garment, with its widest gap
	if frame >= 2:
		for s in _seams:
			var uid: int = s["uid"]
			var key := "seam:%d" % uid
			if _notes.has(key) or not _garments.has(uid):
				continue
			var gap := _seam_gap(cloth, s)
			if gap > SEAM_OPEN:
				var txt := "%s: a seam does not close (%d mm apart) - the edge lengths do not fit the body here" % [_garments[uid]["name"], int(round(gap * 1000.0))]
				_note(key, false, txt, [String(s["a"]), String(s["b"])])
	if frame >= KE_FROM and ke > KE_SPIKE:
		_note("ke", true, "Energy spike (%.2f J): check seams and placement" % ke, [])
	var lines := []
	var hot := []
	var ok := true
	var keys: Array = _notes.keys()
	keys.sort_custom(func(a: String, b: String) -> bool: return _rank(a) < _rank(b))  # falls first, then energy, then seams
	for key in keys:
		var n: Dictionary = _notes[key]
		lines.append(String(n["text"]))
		for pid in n["hot"]:
			if not hot.has(pid):
				hot.append(pid)
		ok = ok and not bool(n["error"])
	var txt := ""
	for i in mini(3, lines.size()):
		txt += ("\n" if i > 0 else "") + String(lines[i])
	return {"ok": ok, "text": txt, "lines": lines, "hot": hot}


static func _rank(key: String) -> int:
	if key.begins_with("fall"):
		return 0
	return 1 if key == "ke" else 2


# Remember a problem for this build. The text is the latest one (its numbers change); the place in the list stays.
func _note(key: String, is_error: bool, text: String, pieces: Array) -> void:
	_notes[key] = {"error": is_error, "text": text, "hot": pieces}


# Pieces of a falling garment: the ones that moved away from their start (or touch the floor). All of them when
# none is singled out.
func _flag_pieces(st: Dictionary, uid: int, pids: Array) -> Array:
	var out := []
	for pid in pids:
		var cur: Dictionary = st["pieces"][pid]
		var base: Dictionary = _base_piece[pid]
		if float(cur["mean"]) - float(base["mean"]) > FALL_MEAN or (float(cur["min_y"]) < FLOOR_Y and float(base["min_y"]) > 0.1) or bool(cur["nan"]):
			out.append(pid)
	return out if not out.is_empty() else pids.duplicate()


func _edge(pid: String, edge: int) -> Array:
	var lists = _chains.get(pid, [])
	if edge < 0 or edge >= lists.size():
		return []
	return lists[edge]


# Widest distance between the two edges of a seam, paired the way the editor draws its stitches (editor.gd).
func _seam_gap(cloth: PackedVector3Array, s: Dictionary) -> float:
	var ca = _edge(s["a"], int(s["ea"]))
	var cb = _edge(s["b"], int(s["eb"]))
	if ca.size() < 2 or cb.size() < 2:
		return 0.0
	var m: int = maxi(ca.size(), cb.size())
	var worst := 0.0
	for k in m:
		var tt := float(k) / float(maxi(1, m - 1))
		var ia: int = ca[mini(ca.size() - 1, int(round(tt * float(ca.size() - 1))))]
		var tb := (1.0 - tt) if bool(s["reverse"]) else tt
		var ib: int = cb[mini(cb.size() - 1, int(round(tb * float(cb.size() - 1))))]
		if ia < 0 or ib < 0 or ia >= cloth.size() or ib >= cloth.size():
			continue
		worst = maxf(worst, cloth[ia].distance_to(cloth[ib]))
	return worst
