# Arrangement points (the CLO / Marvelous Designer way to put pattern pieces on an avatar): dots round the body, on the
# torso at the shoulder, bust, waist and hip heights (front, back, sides and between), on each arm (shoulder, upper arm,
# forearm) and on each leg (thigh, knee, shin). Picking a piece and clicking a dot puts the piece's centre there, wrapped
# round that body part: the torso dots set an around-the-body placement (wrap theta and height), the arm and leg dots
# wrap the piece round that limb (wrap modes "arm" and "leg", wrap.gd). The mirror twin follows (editor _sym_sync).
class_name FailoringArrange
extends RefCounted

const Wrap = preload("res://wrap.gd")
const BodyGen = preload("res://body_gen.gd")

const TORSO_ANGLES := [0.0, 45.0, 90.0, 135.0, 180.0, 225.0, 270.0, 315.0]
const ANGLE_NAMES := {0.0: "front", 45.0: "front left", 90.0: "left side", 135.0: "back left", 180.0: "back",
	225.0: "back right", 270.0: "right side", 315.0: "front right"}
const OUT := 0.03   # the dots float this far off the skin (m)

var points: Array = []   # {id, label, kind: torso|arm|leg, pos: Vector3, angle, y, side, s}


# body: the stand dictionary the garment starts on (sections, arms, landmarks).
func build(body: Dictionary) -> void:
	points = []
	var secs: Dictionary = body["sections"]
	var lm: Dictionary = body.get("landmarks", {})
	var bust := _lm_y(lm, "bust.L", 1.2)
	var waist := _lm_y(lm, "waist", 1.05)
	var hip := _lm_y(lm, "hip", 0.92)
	var acro := _lm_y(lm, "acromion.L", 1.38)
	for h in [["shoulder", acro - 0.04], ["bust", bust], ["waist", waist], ["hip", hip]]:
		for a in TORSO_ANGLES:
			if String(h[0]) == "shoulder" and fmod(a, 90.0) != 0.0:
				continue  # the shoulder line: front, back and the two shoulder tips
			var pos = _torso_point(secs, float(a), float(h[1]))
			if pos == null:
				continue
			points.append({"id": "torso_%s_%d" % [h[0], int(a)], "label": "%s, %s height" % [String(ANGLE_NAMES[a]).capitalize(), h[0]],
				"kind": "torso", "pos": pos, "angle": float(a), "y": float(h[1])})
	var arms: Dictionary = body["arms"]
	for side in ["L", "R"]:
		if not arms.has(side):
			continue
		var total := float(arms[side]["total"])
		for spec in [["shoulder", 0.03], ["upper arm", 0.15], ["forearm", minf(0.40, total * 0.75)]]:
			points.append({"id": "arm_%s_%s" % [side, spec[0]], "label": "%s %s" % ["Left" if side == "L" else "Right", spec[0]],
				"kind": "arm", "pos": _arm_point(arms[side], float(spec[1]), side), "side": side, "s": float(spec[1])})
	var yc := float(secs.get("yCrotch", 0.8))
	for side in ["L", "R"]:
		for spec in [["thigh", yc - 0.12], ["knee", yc * 0.62], ["shin", yc * 0.35]]:
			var pos2 = _leg_point(secs, side, float(spec[1]))
			if pos2 == null:
				continue
			points.append({"id": "leg_%s_%s" % [side, spec[0]], "label": "%s %s" % ["Left" if side == "L" else "Right", spec[0]],
				"kind": "leg", "pos": pos2, "side": side, "y": float(spec[1])})


static func _lm_y(lm: Dictionary, name: String, fallback: float) -> float:
	var v = lm.get(name, null)
	return float(v[1]) if v is Array and (v as Array).size() >= 2 else fallback


# A point on the torso section at height y, in the direction `angle` (degrees; 0 front, 90 the body's left), OUT off it.
static func _torso_point(secs: Dictionary, angle: float, y: float) -> Variant:
	var dummy := {"outline": [[-0.01, 0.0], [0.01, 0.0], [0.01, 0.02], [-0.01, 0.02]],
		"wrap": {"mode": "torso", "theta": angle, "span": 1.0, "y": y - 0.01, "clear": OUT}}
	var w = Wrap.new(dummy, secs, {})
	var p: Vector3 = w.point(0.0, 0.01)
	return p if is_finite(p.x) and is_finite(p.z) else null


static func _arm_point(adv: Dictionary, s: float, side: String) -> Vector3:
	s = clampf(s, 0.0, float(adv["total"]))
	var k := 0 if s < float(adv["len"][0]) else 1
	var l: float = s - (float(adv["len"][0]) if k == 1 else 0.0)
	var seg: Array = adv["seg"][k]
	var pts: Array = adv["pts"][k]
	var rad: Array = adv["rad"]
	var r: float = float(rad[mini(rad.size() - 1, int(round(s / 0.01)))]) + OUT
	var c := Vector3(float(pts[0]) + float(seg[0]) * l, float(pts[1]) + float(seg[1]) * l, float(pts[2]) + float(seg[2]) * l)
	var sg := 1.0 if side == "L" else -1.0
	return c + Vector3(sg * r, 0.0, 0.0)


static func _leg_point(secs: Dictionary, side: String, y: float) -> Variant:
	var dummy := {"outline": [[-0.01, 0.0], [0.01, 0.0], [0.01, 0.02], [-0.01, 0.02]],
		"wrap": {"mode": "leg", "side": side, "theta": 0.0, "y": y - 0.01, "clear": OUT}}
	var w = Wrap.new(dummy, secs, {})
	var p: Vector3 = w.point(0.0, 0.01)
	return p if is_finite(p.x) and is_finite(p.z) else null


# The piece's wrap after arranging it on point pt (a new dictionary; the piece is not changed). secs: the sections, for the
# girth that centres an "arc" piece on the dot.
static func wrap_for(p: Dictionary, pt: Dictionary, secs: Dictionary) -> Dictionary:
	var w: Dictionary = (p.get("wrap", {}) as Dictionary).duplicate(true)
	var bb := Wrap.bbox_of(p["outline"])
	var h: float = float(bb[3]) - float(bb[1])
	var width: float = float(bb[2]) - float(bb[0])
	match String(pt["kind"]):
		"torso":
			var mode := String(w.get("mode", ""))
			if mode == "arm" or mode == "leg" or mode == "":
				w = {"mode": "torso", "span": clampf(360.0 * width / maxf(0.3, _girth(secs, float(pt["y"]))), 20.0, 340.0),
					"clear": float(w.get("clear", 0.012))}
				mode = "torso"
			w["y"] = float(pt["y"]) - h * 0.5
			var th := float(pt["angle"])
			if mode == "arc":
				# an arc piece starts at theta and runs along the section by its width: centre it on the dot
				th -= 360.0 * (width * 0.5) / maxf(0.3, _girth(secs, float(pt["y"])))
			w["theta"] = fposmod(th, 360.0)
		"arm":
			w = {"mode": "arm", "side": String(pt["side"]), "sTop": maxf(-0.05, float(pt["s"]) - h * 0.5),
				"clear": float(w.get("clear", 0.02)), "phiSeam": -90.0 if String(pt["side"]) == "L" else 90.0}
			var cap := sleeve_cap(p["outline"])
			if not cap.is_empty():
				w["cap"] = cap   # a set-in sleeve: the cap climbs the arm to the shoulder (wrap.gd wrap.cap)
		"leg":
			w = {"mode": "leg", "side": String(pt["side"]), "theta": 0.0, "y": float(pt["y"]) - h * 0.5,
				"clear": float(w.get("clear", 0.012))}
	return w


# Slides a sleeve (an arm wrap) along the arm (wrap sTop, -0.16 to 0.10 m) to where its seams start closest to the
# pieces they join, in pattern space: each seam side is sampled at 9 points and placed with its piece's wrap. Also gives
# a set-in sleeve its cap (sleeve_cap). pieces, seams: the garment's (or the merged plan's: ids must match); secs, arms:
# the body the garment starts on. Returns the new wrap; the piece is not changed.
static func fit_sleeve(p: Dictionary, w: Dictionary, pieces: Array, seams: Array, secs: Dictionary, arms: Dictionary) -> Dictionary:
	w = w.duplicate(true)
	if not w.has("cap"):
		var cap := sleeve_cap(p["outline"])
		if not cap.is_empty():
			w["cap"] = cap
	var pid := String(p["id"])
	var mine := []    # pattern points on the sleeve
	var theirs := []  # their positions on the body (fixed)
	var wraps := {}
	for s in seams:
		var a := String(s["a"][0])
		var b := String(s["b"][0])
		if (a == pid) == (b == pid):
			continue
		var my_side: Array = s["a"] if a == pid else s["b"]
		var ot_side: Array = s["b"] if a == pid else s["a"]
		var other = null
		for q in pieces:
			if String(q["id"]) == String(ot_side[0]):
				other = q
		if other == null:
			continue
		if not wraps.has(String(other["id"])):
			wraps[String(other["id"])] = Wrap.new(other, secs, arms)
		var wo = wraps[String(other["id"])]
		var rev := bool(s.get("reverse", false))
		var rm := _side_range(my_side)
		var ro := _side_range(ot_side)
		for k in 9:
			var f := float(k) / 8.0
			var fo := 1.0 - f if rev else f
			mine.append(_edge_point(p["outline"], int(my_side[1]), lerpf(rm.x, rm.y, f)))
			var q2 := _edge_point(other["outline"], int(ot_side[1]), lerpf(ro.x, ro.y, fo))
			theirs.append(wo.point(q2.x, q2.y))
	if mine.is_empty():
		return w
	var best := w
	var best_gap := 1e9
	var st := -0.16
	while st <= 0.1001:
		var w2: Dictionary = w.duplicate(true)
		w2["sTop"] = st
		var q3: Dictionary = p.duplicate(true)
		q3["wrap"] = w2
		var wr = Wrap.new(q3, secs, arms)
		var sum := 0.0
		for k in mine.size():
			sum += wr.point(mine[k].x, mine[k].y).distance_to(theirs[k])
		var gap := sum / float(mine.size())
		if gap < best_gap:
			best_gap = gap
			best = w2
		st += 0.02
	return best


static func _side_range(side: Array) -> Vector2:
	return Vector2(float(side[2]), float(side[3])) if side.size() >= 4 else Vector2(0.0, 1.0)


static func _edge_point(o: Array, e: int, t: float) -> Vector2:
	var a: Array = o[e % o.size()]
	var b: Array = o[(e + 1) % o.size()]
	return Vector2(float(a[0]), float(a[1])).lerp(Vector2(float(b[0]), float(b[1])), t)


# A set-in sleeve's cap: the widest row of the piece (the underarm line, the cap's two corners) when the piece narrows
# above it by at least 3 cm of height, as {v, x0, x1}; {} for a piece without one (a tube, a cuff).
static func sleeve_cap(outline: Array) -> Dictionary:
	var bb := Wrap.bbox_of(outline)
	var best_w := -1.0
	var best := {}
	var v := float(bb[1]) + 0.002
	while v < float(bb[3]):
		var ext := row_extents(outline, v)
		if not ext.is_empty() and float(ext[1]) - float(ext[0]) > best_w + 1e-4:
			best_w = float(ext[1]) - float(ext[0])
			best = {"v": v, "x0": float(ext[0]), "x1": float(ext[1])}
		v += 0.002
	if best.is_empty() or float(bb[3]) - float(best["v"]) < 0.03:
		return {}
	return best


static func row_extents(outline: Array, v: float) -> Array:
	var lo := 1e9
	var hi := -1e9
	var n := outline.size()
	for i in n:
		var a: Array = outline[i]
		var b: Array = outline[(i + 1) % n]
		if (float(a[1]) - v) * (float(b[1]) - v) <= 0.0 and float(a[1]) != float(b[1]):
			var x: float = float(a[0]) + (float(b[0]) - float(a[0])) * (v - float(a[1])) / (float(b[1]) - float(a[1]))
			lo = minf(lo, x)
			hi = maxf(hi, x)
	return [lo, hi] if hi > lo else []


static func _girth(secs: Dictionary, y: float) -> float:
	var g: PackedFloat32Array = secs.get("girth", PackedFloat32Array())
	if g.is_empty():
		return 0.9
	return float(g[clampi(int(round(y / float(secs.get("dy", 0.01)))), 0, g.size() - 1)])
