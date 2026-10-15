# Parametric mannequin: port of app/src/body.js (blend shapes, tape measures, fit) and the
# body parts of app/src/gait.js (rotations, posing, standing body, arm tubes). The asset is
# pre-decoded by tools/export_godot_body.js (godot/assets/body_data.json), so no DEFLATE code
# is needed here. Verified against JS reference values by godot/tests/body_check.tscn.
# Conventions mirror the JS: positions are PackedFloat32Array (3 per vertex), small vectors
# are plain Arrays, M is the loaded model Dictionary, bodies are Dictionaries.
class_name FailoringBodyGen

const XMAX := 1.6
const WMAX := 1.5

const CONTROLS := {
	"bust": ["bust-circ"], "waist": ["waist-circ"], "hip": ["hips-circ"], "shoulder": ["shoulder-dist"],
	"neck": ["neck-circ"], "thigh": ["thigh-circ"], "upperarm": ["upperarm-circ"],
	"arm": ["upperarm-length", "lowerarm-length"], "inseam": ["upperleg-height", "lowerleg-height"],
	"back": ["napetowaist-dist"],
}
const PRIMARY := ["height", "bust", "waist", "hip", "shoulder"]
const SECONDARY := ["arm", "inseam", "neck", "thigh", "upperarm", "back"]
const LABELS := {"height": "Height", "bust": "Bust", "waist": "Waist", "hip": "Hip", "shoulder": "Shoulder",
	"arm": "Arm length", "inseam": "Inseam", "neck": "Neck", "thigh": "Thigh", "upperarm": "Upper arm",
	"back": "Back length", "muscle": "Muscle", "cup": "Bust shape", "belly": "Belly"}
const PRESETS := {
	"average": {"height": 1.68, "bust": 0.92, "waist": 0.74, "hip": 0.98, "shoulder": 0.37, "muscle": 0.5, "cup": 0.0},
	"petite": {"height": 1.55, "bust": 0.84, "waist": 0.66, "hip": 0.90, "shoulder": 0.35, "muscle": 0.45, "cup": -0.1},
	"tall": {"height": 1.80, "bust": 0.95, "waist": 0.78, "hip": 1.00, "shoulder": 0.39, "muscle": 0.5, "cup": 0.0},
	"curvy": {"height": 1.66, "bust": 1.04, "waist": 0.80, "hip": 1.12, "shoulder": 0.37, "muscle": 0.45, "cup": 0.5},
	"athletic": {"height": 1.75, "bust": 0.96, "waist": 0.78, "hip": 0.96, "shoulder": 0.40, "muscle": 0.85, "cup": -0.3, "belly": -0.4},
	"plus": {"height": 1.68, "bust": 1.18, "waist": 1.02, "hip": 1.24, "shoulder": 0.39, "muscle": 0.4, "cup": 0.3},
}
const DEFAULT_MEAS := {"height": 1.68, "bust": 0.92, "waist": 0.74, "hip": 0.98, "shoulder": 0.40, "muscle": 0.5, "cup": 0.0}


# ---------------------------------------------------------------- asset
static func load_data(data: Dictionary) -> Dictionary:
	var nv: int = int(data["nv"])
	var nb: int = int(data["nb"])
	var nq: int = int(data["nq"])
	var base := PackedFloat32Array(data["base"])
	var tris := PackedInt32Array(data["tris"])
	var skin_idx := PackedInt32Array(data["skinIdx"])
	var skin_w := PackedFloat32Array(data["skinW"])
	var targets := {}
	for tname in (data["targets"] as Dictionary).keys():
		var t: Dictionary = data["targets"][tname]
		targets[tname] = {"idx": PackedInt32Array(t["idx"]), "d": PackedFloat32Array(t["d"]), "src": t.get("src", "")}
	var m := {
		"nv": nv, "nb": nb, "nq": nq, "base": base, "tris": tris,
		"parts": data["parts"], "part": PackedInt32Array(data["part"]),
		"skinIdx": skin_idx, "skinW": skin_w, "bones": data["bones"],
		"joints": data["joints"], "lm": data["landmarks"], "masks": data["masks"],
		"targets": targets, "bytes": 0,
	}
	m["sets"] = {
		"trunk": _tri_set(m, ["trunk"]),
		"hip": _tri_set(m, ["trunk", "leg.L", "leg.R"]),
		"neck": _tri_set(m, ["neck", "trunk", "head"]),
		"armL": _tri_set(m, ["arm.L"]),
		"section": _tri_set(m, ["trunk", "neck", "head", "leg.L", "leg.R", "foot.L", "foot.R"]),
	}
	_build_smooth(m)
	m["work"] = {"Pm": PackedFloat32Array(), "P": PackedFloat32Array(), "tmp": PackedFloat32Array()}
	m["work"]["Pm"].resize(3 * nv)
	m["work"]["P"].resize(3 * nv)
	m["jcache"] = {}
	calibrate(m)
	return m


static func _tri_set(m: Dictionary, names: Array) -> PackedInt32Array:
	var parts: Array = m["parts"]
	var ok := {}
	for s in names:
		ok[parts.find(s)] = true
	var tris: PackedInt32Array = m["tris"]
	var part: PackedInt32Array = m["part"]
	var out := PackedInt32Array()
	for t in tris.size() / 3:
		if ok.has(part[tris[3 * t]]) and ok.has(part[tris[3 * t + 1]]) and ok.has(part[tris[3 * t + 2]]):
			out.append(t)
	return out


static func _build_smooth(m: Dictionary) -> void:
	var masks: Dictionary = m["masks"]
	var mv := {}
	for k in masks.keys():
		var pair: Array = masks[k]
		var ids: Array = pair[0]
		var ws: Array = pair[1]
		for i in ids.size():
			mv[int(ids[i])] = maxf(float(mv.get(int(ids[i]), 0.0)), float(ws[i]))
	# quad edge neighbourhoods; quads recover from triangle pairs [a,b,c,a,c,d]
	var tris: PackedInt32Array = m["tris"]
	var nbr := {}
	for v in mv.keys():
		nbr[v] = {}
	for q in range(0, tris.size() / 6):
		var quad := [tris[6 * q], tris[6 * q + 1], tris[6 * q + 2], tris[6 * q + 5]]
		for e in 4:
			var a: int = quad[e]
			var b: int = quad[(e + 1) & 3]
			if nbr.has(a):
				nbr[a][b] = true
			if nbr.has(b):
				nbr[b][a] = true
	var ids := PackedInt32Array()
	var ws := PackedFloat32Array()
	var nbl := []
	for v in mv.keys():
		ids.append(v)
		ws.append(mv[v])
		nbl.append(PackedInt32Array(nbr[v].keys()))
	m["smooth"] = {"ids": ids, "w": ws, "nbr": nbl}


# ---------------------------------------------------------------- blending
static func _add_t(m: Dictionary, p: PackedFloat32Array, tname: String, w: float) -> void:
	if w == 0.0:
		return
	var targets: Dictionary = m["targets"]
	if not targets.has(tname):
		return
	var t: Dictionary = targets[tname]
	var idx: PackedInt32Array = t["idx"]
	var d: PackedFloat32Array = t["d"]
	for i in idx.size():
		var o := 3 * idx[i]
		var j := 3 * i
		p[o] += w * d[j]
		p[o + 1] += w * d[j + 1]
		p[o + 2] += w * d[j + 2]


static func _tri3(v: float) -> Dictionary:
	if v < 0.5:
		return {"min": 1.0 - 2.0 * maxf(0.0, v), "average": 2.0 * maxf(0.0, v), "max": 0.0}
	return {"min": 0.0, "average": 2.0 - 2.0 * v, "max": 2.0 * v - 1.0}


static func _macro_body(m: Dictionary, mac: Dictionary, p: PackedFloat32Array) -> void:
	var base: PackedFloat32Array = m["base"]
	for i in base.size():
		p[i] = base[i]
	var mw := _tri3(float(mac["muscle"]))
	var ww := _tri3(float(mac["weight"]))
	for mm in ["min", "average", "max"]:
		for w in ["min", "average", "max"]:
			if mm != "average" or w != "average":
				_add_t(m, p, "universal-%s-%s" % [mm, w], float(mw[mm]) * float(ww[w]))
	var th: float = mac["th"]
	_add_t(m, p, "height-max" if th > 0.0 else "height-min", absf(th))
	var cup: float = mac["cup"]
	_add_t(m, p, "cup-max" if cup > 0.0 else "cup-min", absf(cup))
	var belly: float = mac.get("belly", 0.0)
	_add_t(m, p, "belly-incr" if belly > 0.0 else "belly-decr", absf(belly))
	var seat: float = mac.get("seat", 0.0)
	_add_t(m, p, "buttocks-incr" if seat > 0.0 else "buttocks-decr", absf(seat))
	_add_t(m, p, "nipple-size-decr", 1.0)
	_add_t(m, p, "nipple-point-decr", 1.0)


static func _finish(m: Dictionary, p: PackedFloat32Array) -> void:
	var s: Dictionary = m["smooth"]
	var ids: PackedInt32Array = s["ids"]
	var w: PackedFloat32Array = s["w"]
	var nbr: Array = s["nbr"]
	var n := ids.size()
	var t := PackedFloat32Array()
	t.resize(3 * n)
	for it in 4:
		for i in n:
			var v := 3 * ids[i]
			var nb: PackedInt32Array = nbr[i]
			var x := 0.0
			var y := 0.0
			var z := 0.0
			for k in nb.size():
				var o := 3 * nb[k]
				x += p[o]
				y += p[o + 1]
				z += p[o + 2]
			var l: float = 0.6 * w[i] / float(nb.size())
			t[3 * i] = p[v] + l * (x - float(nb.size()) * p[v])
			t[3 * i + 1] = p[v + 1] + l * (y - float(nb.size()) * p[v + 1])
			t[3 * i + 2] = p[v + 2] + l * (z - float(nb.size()) * p[v + 2])
		for i in n:
			var v := 3 * ids[i]
			p[v] = t[3 * i]
			p[v + 1] = t[3 * i + 1]
			p[v + 2] = t[3 * i + 2]


static func _apply_controls(m: Dictionary, p: PackedFloat32Array, keys: Array, x: Array) -> void:
	for j in keys.size():
		var v: float = x[j]
		if v == 0.0:
			continue
		for t in CONTROLS[keys[j]]:
			_add_t(m, p, t + ("-incr" if v > 0.0 else "-decr"), absf(v))


# ---------------------------------------------------------------- tape measures
static func _vtx(p: PackedFloat32Array, i: Variant) -> Array:
	var vi := int(i)
	return [p[3 * vi], p[3 * vi + 1], p[3 * vi + 2]]


static func _joint(m: Dictionary, p: PackedFloat32Array, jname: String) -> Array:
	var ids: Array = (m["joints"] as Dictionary)[jname]
	var x := 0.0
	var y := 0.0
	var z := 0.0
	for i in ids:
		var vi := int(i)
		x += p[3 * vi]
		y += p[3 * vi + 1]
		z += p[3 * vi + 2]
	return [x / ids.size(), y / ids.size(), z / ids.size()]


static func _dist(a: Array, b: Array) -> float:
	return sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]))


static func hull2(pts: PackedFloat32Array, n: int) -> PackedFloat32Array:
	if n < 3:
		var pre := PackedFloat32Array()
		pre.resize(2 * n)
		for i in 2 * n:
			pre[i] = pts[i]
		return pre
	var idx := []
	for i in n:
		idx.append(i)
	idx.sort_custom(func(a: int, b: int) -> bool:
		if pts[2 * a] != pts[2 * b]:
			return pts[2 * a] < pts[2 * b]
		return pts[2 * a + 1] < pts[2 * b + 1])
	var h := []
	h.resize(2 * n + 2)
	var k := 0
	for i in n:
		while k >= 2:
			var o: int = h[k - 2]
			var aa: int = h[k - 1]
			var bb: int = idx[i]
			var cr: float = (pts[2 * aa] - pts[2 * o]) * (pts[2 * bb + 1] - pts[2 * o + 1]) - (pts[2 * aa + 1] - pts[2 * o + 1]) * (pts[2 * bb] - pts[2 * o])
			if cr > 0.0:
				break
			k -= 1
		h[k] = idx[i]
		k += 1
	var t := k + 1
	for i in range(n - 2, -1, -1):
		while k >= t:
			var o: int = h[k - 2]
			var aa: int = h[k - 1]
			var bb: int = idx[i]
			var cr: float = (pts[2 * aa] - pts[2 * o]) * (pts[2 * bb + 1] - pts[2 * o + 1]) - (pts[2 * aa + 1] - pts[2 * o + 1]) * (pts[2 * bb] - pts[2 * o])
			if cr > 0.0:
				break
			k -= 1
		h[k] = idx[i]
		k += 1
	var out := PackedFloat32Array()
	out.resize(2 * (k - 1))
	for i in k - 1:
		out[2 * i] = pts[2 * h[i]]
		out[2 * i + 1] = pts[2 * h[i] + 1]
	return out


static func perimeter2(h: PackedFloat32Array) -> float:
	var l := 0.0
	var n: int = h.size() / 2
	for i in n:
		var j := (i + 1) % n
		l += sqrt((h[2 * j] - h[2 * i]) * (h[2 * j] - h[2 * i]) + (h[2 * j + 1] - h[2 * i + 1]) * (h[2 * j + 1] - h[2 * i + 1]))
	return l


# Plane-section points in the (e1, e2) basis. Returns PackedFloat32Array flat xy.
static func _slice(p: PackedFloat32Array, tris: PackedInt32Array, list: PackedInt32Array, nrm: Array, d: float, e1: Array, e2: Array, c: Variant, rmax: float) -> PackedFloat32Array:
	var out := PackedFloat32Array()
	var r2: float = rmax * rmax if rmax > 0.0 else 0.0
	for q in list.size():
		var t := 3 * list[q]
		var av := [tris[t], tris[t + 1], tris[t + 2]]
		var s := [p[3 * av[0]] * nrm[0] + p[3 * av[0] + 1] * nrm[1] + p[3 * av[0] + 2] * nrm[2] - d,
			p[3 * av[1]] * nrm[0] + p[3 * av[1] + 1] * nrm[1] + p[3 * av[1] + 2] * nrm[2] - d,
			p[3 * av[2]] * nrm[0] + p[3 * av[2] + 1] * nrm[1] + p[3 * av[2] + 2] * nrm[2] - d]
		if (s[0] > 0.0) == (s[1] > 0.0) and (s[1] > 0.0) == (s[2] > 0.0):
			continue
		var pairs := [[0, 1], [1, 2], [2, 0]]
		for pr in pairs:
			if (s[pr[0]] > 0.0) == (s[pr[1]] > 0.0):
				continue
			var a: int = av[pr[0]]
			var b: int = av[pr[1]]
			var sa: float = s[pr[0]]
			var sb: float = s[pr[1]]
			var tt: float = sa / (sa - sb)
			var x: float = p[3 * a] + (p[3 * b] - p[3 * a]) * tt
			var y: float = p[3 * a + 1] + (p[3 * b + 1] - p[3 * a + 1]) * tt
			var z: float = p[3 * a + 2] + (p[3 * b + 2] - p[3 * a + 2]) * tt
			if r2 > 0.0 and (x - c[0]) * (x - c[0]) + (y - c[1]) * (y - c[1]) + (z - c[2]) * (z - c[2]) > r2:
				continue
			out.append(x * e1[0] + y * e1[1] + z * e1[2])
			out.append(x * e2[0] + y * e2[1] + z * e2[2])
	return out


static func _girth_y(m: Dictionary, p: PackedFloat32Array, list: PackedInt32Array, y: float, keep: Variant) -> float:
	var pts := _slice(p, m["tris"], list, [0.0, 1.0, 0.0], y, [1.0, 0.0, 0.0], [0.0, 0.0, 1.0], null, 0.0)
	if keep != null:
		var f := PackedFloat32Array()
		var kk := 0
		for i in pts.size() / 2:
			if keep.call(pts[2 * i], pts[2 * i + 1]):
				f.append(pts[2 * i])
				f.append(pts[2 * i + 1])
				kk += 1
		if kk < 3:
			return 0.0
		return perimeter2(hull2(f, kk))
	if pts.size() / 2 < 3:
		return 0.0
	return perimeter2(hull2(pts, pts.size() / 2))


static func _girth_plane(m: Dictionary, p: PackedFloat32Array, list: PackedInt32Array, nrm: Array, d: float, e1: Array, e2: Array, c: Array, rmax: float) -> float:
	var pts := _slice(p, m["tris"], list, nrm, d, e1, e2, c, rmax)
	if pts.size() / 2 < 3:
		return 0.0
	return perimeter2(hull2(pts, pts.size() / 2))


static func _nrm(a: Array) -> Array:
	var l: float = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
	if l == 0.0:
		return [0.0, 0.0, 0.0]
	return [a[0] / l, a[1] / l, a[2] / l]


static func _sub(a: Array, b: Array) -> Array:
	return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]


static func _crs(a: Array, b: Array) -> Array:
	return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


static func _dot(a: Array, b: Array) -> float:
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


static func measure(m: Dictionary, p: PackedFloat32Array, hreq: float) -> Dictionary:
	var nb: int = m["nb"]
	var tris: PackedInt32Array = m["tris"]
	var sets: Dictionary = m["sets"]
	var lm: Dictionary = m["lm"]
	var y0 := 1e9
	var y1 := -1e9
	for i in nb:
		y0 = minf(y0, p[3 * i + 1])
		y1 = maxf(y1, p[3 * i + 1])
	var h0: float = y1 - y0
	var s: float = hreq / h0 if hreq > 0.0 else 1.0
	var r := {"s": s, "y0": y0, "height": h0 * s}
	var yb: float = 0.5 * (p[3 * int(lm["bust.L"]) + 1] + p[3 * int(lm["bust.R"]) + 1])
	r["bust"] = _girth_y(m, p, sets["trunk"], yb, null) * s
	var yw: float = p[3 * int(lm["waist"]) + 1]
	var yh: float = p[3 * int(lm["hip"]) + 1]
	r["waist"] = _girth_y(m, p, sets["trunk"], yw, null) * s
	r["hip"] = _girth_y(m, p, sets["hip"], yh, null) * s
	r["shoulder"] = _dist(_vtx(p, int(lm["acromion.L"])), _vtx(p, int(lm["acromion.R"]))) * s
	var c := _vtx(p, int(lm["cervicale"]))
	var jg := _vtx(p, int(lm["jugular"]))
	var nn := _nrm(_crs(_sub(jg, c), [1.0, 0.0, 0.0]))
	if nn[1] < 0.0:
		nn = [-nn[0], -nn[1], -nn[2]]
	var up: float = 0.015 / s
	var mid := [(c[0] + jg[0]) / 2.0 + nn[0] * up, (c[1] + jg[1]) / 2.0 + nn[1] * up, (c[2] + jg[2]) / 2.0 + nn[2] * up]
	r["neck"] = _girth_plane(m, p, sets["neck"], nn, _dot(nn, mid), [1.0, 0.0, 0.0], _crs(nn, [1.0, 0.0, 0.0]), mid, 0.08 / s) * s
	var yc: float = p[3 * int(lm["crotch"]) + 1]
	r["thigh"] = _girth_y(m, p, sets["hip"], yc - 0.025 / s, func(x: float, _z: float) -> bool: return x > 0.004 / s) * s
	var js := _joint(m, p, "l-shoulder")
	var je := _joint(m, p, "l-elbow")
	var jw := _joint(m, p, "l-hand")
	var ax := _nrm(_sub(je, js))
	var q := [(js[0] + je[0]) / 2.0, (js[1] + je[1]) / 2.0, (js[2] + je[2]) / 2.0]
	var e1 := _nrm(_crs(ax, [0.0, 0.0, 1.0]))
	var e2 := _crs(ax, e1)
	r["upperarm"] = _girth_plane(m, p, sets["armL"], ax, _dot(ax, q), e1, e2, q, 0.09 / s) * s
	r["arm"] = (_dist(_vtx(p, int(lm["acromion.L"])), je) + _dist(je, jw)) * s
	r["inseam"] = (yc - y0) * s
	var ch: Array = lm["backMidline"]
	var yc2: float = c[1]
	var bl2 := 0.0
	for i in range(ch.size() - 1):
		var a := _vtx(p, int(ch[i]))
		var b := _vtx(p, int(ch[i + 1]))
		var lo: float = minf(a[1], b[1])
		var hi: float = maxf(a[1], b[1])
		if hi <= yw or lo >= yc2:
			continue
		var f: float = (minf(hi, yc2) - maxf(lo, yw)) / (hi - lo) if hi - lo > 1e-9 else 1.0
		bl2 += f * _dist(a, b)
	r["back"] = bl2 * s
	r["_yw"] = yw
	r["_yh"] = yh
	return r


# ---------------------------------------------------------------- calibration + generation
static func calibrate(m: Dictionary) -> void:
	var work: Dictionary = m["work"]
	var pm: PackedFloat32Array = work["Pm"]
	var mac := {"muscle": 0.5, "weight": 0.5, "th": 0.0, "cup": 0.0}
	var h := []
	for th in [-1.0, -0.5, 0.0, 0.5, 1.0]:
		mac["th"] = th
		_macro_body(m, mac, pm)
		_finish(m, pm)
		h.append([measure(m, pm, 0.0)["height"], th])
	mac["th"] = 0.0
	var g := {}
	for w in [0.0, 0.5, 1.0]:
		mac["weight"] = w
		_macro_body(m, mac, pm)
		_finish(m, pm)
		g[w] = measure(m, pm, 0.0)
	m["cal"] = {"heights": h, "neutral": g[0.5], "min": g[0.0], "max": g[1.0]}


static func _height_target(m: Dictionary, h: float) -> float:
	var t: Array = (m["cal"] as Dictionary)["heights"]
	if h <= float(t[0][0]):
		return -1.0
	if h >= float(t[t.size() - 1][0]):
		return 1.0
	for i in range(t.size() - 1):
		if h <= float(t[i + 1][0]):
			return float(t[i][1]) + (float(t[i + 1][1]) - float(t[i][1])) * (h - float(t[i][0])) / (float(t[i + 1][0]) - float(t[i][0]))
	return 0.0


static func _mean_ratio(a: Dictionary, b: Dictionary, sc: float) -> float:
	var acc := 0.0
	for k in ["bust", "waist", "hip"]:
		acc += float(a[k]) / (float(b[k]) * sc)
	return acc / 3.0


static func _weight_macro(m: Dictionary, req: Dictionary) -> float:
	var c: Dictionary = m["cal"]
	var neutral: Dictionary = c["neutral"]
	var r: float = _mean_ratio(req, neutral, float(req["height"]) / float(neutral["height"]))
	return maxf(0.0, minf(WMAX, 0.5 + 2.5 * (r - 1.0)))


static func _solve(a: Array, b: Array) -> Array:
	var n := b.size()
	var m := []
	for i in n:
		var row: Array = (a[i] as Array).duplicate()
		row.append(b[i])
		m.append(row)
	for cc in n:
		var pv := cc
		for rr in range(cc + 1, n):
			if absf(float(m[rr][cc])) > absf(float(m[pv][cc])):
				pv = rr
		var tmp = m[cc]
		m[cc] = m[pv]
		m[pv] = tmp
		if absf(float(m[cc][cc])) < 1e-12:
			continue
		for rr in n:
			if rr == cc:
				continue
			var f: float = float(m[rr][cc]) / float(m[cc][cc])
			for k in range(cc, n + 1):
				m[rr][k] = float(m[rr][k]) - f * float(m[cc][k])
	var out := []
	for i in n:
		out.append(0.0 if absf(float(m[i][i])) < 1e-12 else float(m[i][n]) / float(m[i][i]))
	return out


static func _key(req: Dictionary) -> String:
	var parts: Array = []
	for k in PRIMARY + SECONDARY + ["muscle", "cup", "belly", "seat", "weight"]:
		parts.append("-" if req.get(k, null) == null else "%.4f" % float(req[k]))
	return "/".join(parts)


static func _fnum(req: Dictionary, k: String, def: float) -> float:
	var v = req.get(k, null)
	return def if v == null else float(v)


static func generate(m: Dictionary, meas: Dictionary, prev: Variant) -> Dictionary:
	var t0 := Time.get_ticks_msec()
	var req := {}
	for k in DEFAULT_MEAS.keys():
		req[k] = DEFAULT_MEAS[k]
	for k in (meas as Dictionary).keys():
		req[k] = meas[k]
	for k in PRIMARY:
		if not (_fnum(req, k, 0.0) > 0.0):
			req[k] = DEFAULT_MEAS[k]
	var mac := {"muscle": clampf(_fnum(req, "muscle", 0.5), 0.0, 1.0), "cup": maxf(-1.0, minf(1.0, _fnum(req, "cup", 0.0))),
		"th": 0.0, "weight": 0.5, "belly": maxf(-1.0, minf(1.0, _fnum(req, "belly", 0.0))), "seat": maxf(-1.0, minf(1.0, _fnum(req, "seat", 0.0)))}
	mac["th"] = _height_target(m, float(req["height"]))
	mac["weight"] = _fnum(req, "weight", -1.0)
	if float(mac["weight"]) < 0.0:
		mac["weight"] = _weight_macro(m, req)
	var keys: Array = []
	for k in CONTROLS.keys():
		if PRIMARY.has(k) or _fnum(req, k, 0.0) > 0.0:
			keys.append(k)
	var work: Dictionary = m["work"]
	var pm: PackedFloat32Array = work["Pm"]
	var p: PackedFloat32Array = work["P"]
	_macro_body(m, mac, pm)
	var x := []
	for k in keys:
		var v := 0.0
		if prev != null and (prev as Dictionary).has("x") and (prev as Dictionary).has("keys"):
			var pi: int = ((prev as Dictionary)["keys"] as Array).find(k)
			if pi >= 0:
				v = float(((prev as Dictionary)["x"] as Array)[pi])
		x.append(v)
	var jkey := ",".join(keys) + "|" + ",".join([mac["muscle"], mac["weight"], mac["th"], mac["cup"], mac["belly"], mac["seat"]].map(func(v: float) -> String: return "%.1f" % (round(v * 10.0) / 10.0)))
	var jcache: Dictionary = m["jcache"]
	var j = jcache.get(jkey, null)
	var evals := 0
	var mm: Dictionary = {}
	if j == null:
		mm = _eval_x(m, pm, p, keys, x, req)
		evals += 1
		j = []
		for _i in keys.size():
			var row := []
			for _j in keys.size():
				row.append(0.0)
			j.append(row)
		for jj in keys.size():
			var hh: float = -0.25 if x[jj] > 0.8 else 0.25
			var xj := x.duplicate()
			xj[jj] += hh
			var mj := _eval_x(m, pm, p, keys, xj, req)
			evals += 1
			for ii in keys.size():
				(j[ii] as Array)[jj] = (float(mj[keys[ii]]) - float(mm[keys[ii]])) / hh
		if jcache.size() > 64:
			jcache.clear()
		jcache[jkey] = j
	mm = _eval_x(m, pm, p, keys, x, req)
	evals += 1
	var it := 0
	var r := []
	while it < 8:
		r = keys.map(func(k: String) -> float: return float(req[k]) - float(mm[k]))
		var mx := 0.0
		for v in r:
			mx = maxf(mx, absf(v))
		if mx < 0.0004:
			break
		var free := []
		for _i in keys.size():
			free.append(true)
		var dx := []
		for _pass in keys.size():
			var fi := []
			for jj in keys.size():
				if free[jj]:
					fi.append(jj)
			var subm := []
			var subr := []
			for i in fi:
				var row := []
				for jj in fi:
					row.append(float((j[i] as Array)[jj]))
				subm.append(row)
				subr.append(r[i])
			var d := _solve(subm, subr)
			dx = []
			for _j in keys.size():
				dx.append(0.0)
			for q in fi.size():
				dx[fi[q]] = d[q]
			var changed := false
			for jj in fi:
				if (x[jj] >= XMAX - 1e-9 and dx[jj] > 0.0) or (x[jj] <= -XMAX + 1e-9 and dx[jj] < 0.0):
					free[jj] = false
					changed = true
			if not changed:
				break
		for jj in keys.size():
			x[jj] = maxf(-XMAX, minf(XMAX, x[jj] + dx[jj]))
		mm = _eval_x(m, pm, p, keys, x, req)
		evals += 1
		it += 1
	r = keys.map(func(k: String) -> float: return float(req[k]) - float(mm[k]))
	var s: float = mm["s"]
	var y0: float = mm["y0"]
	var nb: int = m["nb"]
	var nv: int = m["nv"]
	var hipset: PackedInt32Array = (m["sets"] as Dictionary)["hip"]
	var kk := _slice(p, m["tris"], hipset, [0.0, 1.0, 0.0], float(mm["_yh"]), [1.0, 0.0, 0.0], [0.0, 0.0, 1.0], null, 0.0)
	var zmin := 1e9
	var zmax := -1e9
	for i in kk.size() / 2:
		zmin = minf(zmin, kk[2 * i + 1])
		zmax = maxf(zmax, kk[2 * i + 1])
	var zc := 0.5 * (zmin + zmax)
	var out := PackedFloat32Array()
	out.resize(3 * nv)
	for i in nv:
		out[3 * i] = p[3 * i] * s
		out[3 * i + 1] = (p[3 * i + 1] - y0) * s
		out[3 * i + 2] = (p[3 * i + 2] - zc) * s
	var measm := {}
	for kkk in PRIMARY + SECONDARY:
		measm[kkk] = mm.get(kkk, null)
	var residual := {}
	for i in keys.size():
		residual[keys[i]] = r[i]
	var body := {"pos": out.slice(0, 3 * nb), "allPos": out, "tris": m["tris"], "nb": nb,
		"keys": keys, "x": x, "req": req, "mac": mac, "m": measm, "residual": residual,
		"iters": it, "evals": evals, "key": _key(req), "landmarks": {}, "joints": {}}
	for lname in (m["lm"] as Dictionary).keys():
		var lid = (m["lm"] as Dictionary)[lname]
		if lid is int or lid is float:
			(body["landmarks"] as Dictionary)[lname] = _vtx(out, int(lid))
	for jname in (m["joints"] as Dictionary).keys():
		(body["joints"] as Dictionary)[jname] = _joint(m, out, jname)
	body["sections"] = sections(m, out, 0.01)
	body["ms"] = float(Time.get_ticks_msec() - t0)
	return body


static func _eval_x(m: Dictionary, pm: PackedFloat32Array, p: PackedFloat32Array, keys: Array, x: Array, req: Dictionary) -> Dictionary:
	for i in p.size():
		p[i] = pm[i]
	_apply_controls(m, p, keys, x)
	_finish(m, p)
	return measure(m, p, float(req["height"]))


# ---------------------------------------------------------------- wrap sections
# extra: more triangles to slice with the torso set (stand_at adds the shoulder end of the arms: see there).
static func sections(m: Dictionary, p: PackedFloat32Array, dy: float, extra := PackedInt32Array()) -> Dictionary:
	var tris: PackedInt32Array = m["tris"]
	var set: PackedInt32Array = (m["sets"] as Dictionary)["section"]
	if not extra.is_empty():
		set = set.duplicate()
		set.append_array(extra)
	var ymax := 0.0
	for i in int(m["nb"]):
		ymax = maxf(ymax, p[3 * i + 1])
	var ny := int(floor(ymax / dy)) + 1
	var cnt := PackedInt32Array()
	cnt.resize(ny + 1)
	var k0 := PackedInt32Array()
	k0.resize(set.size())
	var k1 := PackedInt32Array()
	k1.resize(set.size())
	for q in set.size():
		var t := 3 * set[q]
		var a: float = p[3 * tris[t] + 1]
		var b: float = p[3 * tris[t + 1] + 1]
		var c: float = p[3 * tris[t + 2] + 1]
		var lo := maxi(0, int(ceil(minf(a, minf(b, c)) / dy)))
		var hi := mini(ny - 1, int(floor(maxf(a, maxf(b, c)) / dy)))
		k0[q] = lo
		k1[q] = hi
		for k in range(lo, hi + 1):
			cnt[k + 1] += 1
	for k in ny:
		cnt[k + 1] += cnt[k]
	var fill := cnt.duplicate()
	var bins := PackedInt32Array()
	bins.resize(cnt[ny])
	for q in set.size():
		for k in range(k0[q], k1[q] + 1):
			bins[fill[k]] = set[q]
			fill[k] += 1
	var hulls := []
	var girth := PackedFloat32Array()
	girth.resize(ny)
	var yc: float = p[3 * int((m["lm"] as Dictionary)["crotch"]) + 1]
	var legs := {"L": [], "R": []}
	var xax := [1.0, 0.0, 0.0]
	var zax := [0.0, 0.0, 1.0]
	var yax := [0.0, 1.0, 0.0]
	for k in ny:
		var row := _slice(p, tris, bins.slice(cnt[k], cnt[k + 1]), yax, float(k) * dy, xax, zax, null, 0.0)
		var n := row.size() / 2
		for spec in [["L", 1.0], ["R", -1.0]]:
			var side: String = spec[0]
			var sg: float = spec[1]
			var q := PackedFloat32Array()
			if float(k) * dy < yc - 0.004:
				for i in n:
					if row[2 * i] * sg > 0.0:
						q.append(row[2 * i])
						q.append(row[2 * i + 1])
			var h := hull2(q, q.size() / 2) if q.size() / 2 >= 3 else PackedFloat32Array()
			(legs[side] as Array).append(Array(h) if h.size() >= 6 else null)
		var h := hull2(row, n) if n >= 3 else PackedFloat32Array()
		hulls.append(Array(h))
		girth[k] = perimeter2(h) if h.size() > 0 else 0.0
	return {"dy": dy, "ny": ny, "hulls": hulls, "girth": girth, "legs": legs, "yCrotch": yc}


# ---------------------------------------------------------------- posing (gait.js)
static func _rot_m(rx: float, ry: float, rz: float) -> Array:
	var cx := cos(rx)
	var sx := sin(rx)
	var cy := cos(ry)
	var sy := sin(ry)
	var cz := cos(rz)
	var sz := sin(rz)
	return [cy * cz + sy * sx * sz, -cy * sz + sy * sx * cz, sy * cx,
		cx * sz, cx * cz, -sx,
		-sy * cz + cy * sx * sz, sy * sz + cy * sx * cz, cy * cx]


static func _mul3(a: Array, b: Array) -> Array:
	return [a[0] * b[0] + a[1] * b[3] + a[2] * b[6], a[0] * b[1] + a[1] * b[4] + a[2] * b[7], a[0] * b[2] + a[1] * b[5] + a[2] * b[8],
		a[3] * b[0] + a[4] * b[3] + a[5] * b[6], a[3] * b[1] + a[4] * b[4] + a[5] * b[7], a[3] * b[2] + a[4] * b[5] + a[5] * b[8],
		a[6] * b[0] + a[7] * b[3] + a[8] * b[6], a[6] * b[1] + a[7] * b[4] + a[8] * b[7], a[6] * b[2] + a[7] * b[5] + a[8] * b[8]]


static func fk(m: Dictionary, body: Dictionary, rot: Array, root: Variant) -> Array:
	var bones: Array = m["bones"]
	var joints: Dictionary = body["joints"]
	var w := []
	for kk in bones.size():
		var rr: Array = rot[kk] if kk < rot.size() and rot[kk] != null else [0.0, 0.0, 0.0]
		var r := _rot_m(rr[0], rr[1], rr[2])
		var j: Array = joints[(bones[kk] as Dictionary)["joint"]]
		var par: int = int((bones[kk] as Dictionary)["parent"])
		var a := [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0]
		var t := [0.0, 0.0, 0.0]
		if par < 0:
			if root != null:
				t = (root as Array).duplicate()
		else:
			a = (w[par] as Dictionary)["A"]
			t = ((w[par] as Dictionary)["t"] as Array).duplicate()
		var an := _mul3(a, r)
		var d := [j[0] - (r[0] * j[0] + r[1] * j[1] + r[2] * j[2]), j[1] - (r[3] * j[0] + r[4] * j[1] + r[5] * j[2]), j[2] - (r[6] * j[0] + r[7] * j[1] + r[8] * j[2])]
		w.append({"A": an, "t": [a[0] * d[0] + a[1] * d[1] + a[2] * d[2] + t[0], a[3] * d[0] + a[4] * d[1] + a[5] * d[2] + t[1], a[6] * d[0] + a[7] * d[1] + a[8] * d[2] + t[2]]})
	return w


static func pose_explicit(m: Dictionary, body: Dictionary, rot: Array, root: Variant) -> Dictionary:
	var w := fk(m, body, rot, root)
	var nb: int = m["nb"]
	var nv: int = m["nv"]
	var rest: PackedFloat32Array = body["allPos"]
	var out := PackedFloat32Array()
	out.resize(rest.size())
	var si: PackedInt32Array = m["skinIdx"]
	var sw: PackedFloat32Array = m["skinW"]
	for i in nb:
		var x: float = rest[3 * i]
		var y: float = rest[3 * i + 1]
		var z: float = rest[3 * i + 2]
		var ax := 0.0
		var ay := 0.0
		var az := 0.0
		for jj in 4:
			var ww: float = sw[4 * i + jj]
			if ww == 0.0:
				continue
			var b: Dictionary = w[si[4 * i + jj]]
			var aa: Array = b["A"]
			var tt: Array = b["t"]
			ax += ww * (aa[0] * x + aa[1] * y + aa[2] * z + tt[0])
			ay += ww * (aa[3] * x + aa[4] * y + aa[5] * z + tt[1])
			az += ww * (aa[6] * x + aa[7] * y + aa[8] * z + tt[2])
		out[3 * i] = ax
		out[3 * i + 1] = ay
		out[3 * i + 2] = az
	var jb := {}
	var bones: Array = m["bones"]
	for k in bones.size():
		jb[(bones[k] as Dictionary)["joint"]] = k
	var joints := {}
	for jname in (body["joints"] as Dictionary).keys():
		var j: Array = (body["joints"] as Dictionary)[jname]
		var kk: int = int(jb.get(jname, -1))
		if kk < 0:
			kk = int(jb.get("head", 0)) if jname.contains("head") or jname.contains("eye") else 0
		var b: Dictionary = w[kk]
		var aa: Array = b["A"]
		var tt: Array = b["t"]
		joints[jname] = [aa[0] * j[0] + aa[1] * j[1] + aa[2] * j[2] + tt[0], aa[3] * j[0] + aa[4] * j[1] + aa[5] * j[2] + tt[1], aa[6] * j[0] + aa[7] * j[1] + aa[8] * j[2] + tt[2]]
	return {"pos": out.slice(0, 3 * nb), "allPos": out, "joints": joints, "W": w}


static func _crv(keys: Array, f: float) -> float:
	var n := keys.size()
	var ph := []
	var vv := []
	for k in keys:
		ph.append(float(k[0]))
		vv.append(float(k[1]))
	var ff: float = f - floor(f)
	var i := 0
	while i + 1 < n and ph[i + 1] <= ff:
		i += 1
	var p0 := _curve_at(ph, vv, n, i - 1)
	var p1 := _curve_at(ph, vv, n, i)
	var p2 := _curve_at(ph, vv, n, i + 1)
	var p3 := _curve_at(ph, vv, n, i + 2)
	var h: float = p2[0] - p1[0]
	var t: float = (ff - p1[0]) / h
	var m1: float = (p2[1] - p0[1]) / (p2[0] - p0[0]) * h
	var m2: float = (p3[1] - p1[1]) / (p3[0] - p1[0]) * h
	var t2 := t * t
	var t3 := t2 * t
	return (2.0 * t3 - 3.0 * t2 + 1.0) * p1[1] + (t3 - 2.0 * t2 + t) * m1 + (-2.0 * t3 + 3.0 * t2) * p2[1] + (t3 - t2) * m2


static func _HIP(ph: float) -> float:
	return _crv([[0.0, 28.0], [0.12, 25.0], [0.30, 8.0], [0.50, -10.0], [0.60, -4.0], [0.73, 15.0], [0.87, 30.0]], ph)


static func _KNEE(ph: float) -> float:
	return _crv([[0.0, 4.0], [0.14, 17.0], [0.34, 6.0], [0.48, 14.0], [0.60, 42.0], [0.72, 64.0], [0.84, 38.0], [0.94, 9.0]], ph)


static func _ANKLE(ph: float) -> float:
	return _crv([[0.0, 0.0], [0.07, -6.0], [0.20, 2.0], [0.40, 10.0], [0.52, 6.0], [0.60, -17.0], [0.68, -9.0], [0.80, 1.0], [0.92, 3.0]], ph)


static func _TOE(ph: float) -> float:
	return _crv([[0.0, 0.0], [0.40, 0.0], [0.50, 14.0], [0.58, 36.0], [0.66, 22.0], [0.76, 4.0], [0.86, 0.0]], ph)


static func _ELBOW(ph: float) -> float:
	return _crv([[0.0, 12.0], [0.25, 24.0], [0.5, 40.0], [0.75, 24.0]], ph)


static func _curve_at(ph: Array, vv: Array, n: int, i: int) -> Array:
	if i < 0:
		var k: int = ((i % n) + n) % n
		return [ph[k] - ceil(-float(i) / float(n)), vv[k]]
	if i >= n:
		return [ph[i % n] + floor(float(i) / float(n)), vv[i % n]]
	return [ph[i], vv[i]]


static func rotations(m: Dictionary, body: Dictionary, phase: float, opts: Dictionary) -> Array:
	var bones: Array = m["bones"]
	var idx := {}
	for k in bones.size():
		idx[(bones[k] as Dictionary)["joint"]] = k
	var rot := []
	for _b in bones.size():
		rot.append([0.0, 0.0, 0.0])
	var s: float = float(opts.get("stride", 1.0))
	var sa: float = float(opts.get("arms", 1.0))
	var tau := TAU
	var j: Dictionary = body["joints"]
	var dd := PI / 180.0
	for spec in [["l", phase], ["r", phase + 0.5]]:
		var side: String = spec[0]
		var ph: float = spec[1]
		var sg := 1.0 if side == "l" else -1.0
		var hip: Array = j[side + "-upper-leg"]
		var ank: Array = j[side + "-ankle"]
		var a: float = atan2(ank[0] - hip[0], hip[1] - ank[1])
		var stance: float = sg * 2.5 * dd
		rot[idx[side + "-upper-leg"]] = [-_HIP(ph) * s * dd, 0.0, -(a - stance)]
		rot[idx[side + "-knee"]] = [_KNEE(ph) * s * dd, 0.0, 0.0]
		rot[idx[side + "-ankle"]] = [-_ANKLE(ph) * s * dd, 0.0, (a - stance) * 0.0]
		rot[idx[side + "-toe"]] = [-_TOE(ph) * s * dd, 0.0, 0.0]
	var psi: float = -4.5 * dd * s * cos(tau * phase)
	var obl: float = 3.0 * dd * s * sin(tau * phase)
	rot[idx["pelvis"]] = [3.0 * dd * s, psi, obl]
	var rel: float = (5.0 * dd * s * cos(tau * phase) - psi) / 4.0
	for jn in ["spine-4", "spine-3", "spine-2", "spine-1"]:
		rot[idx[jn]] = [0.0, rel, -obl * 0.2]
	rot[idx["neck"]] = [0.0, 0.0, 0.0]
	rot[idx["head"]] = [-3.0 * dd * s, -(psi + rel * 4.0) * 0.5, obl * 0.3]
	for spec in [["l", phase], ["r", phase + 0.5]]:
		var side: String = spec[0]
		var ph: float = spec[1]
		var sg := 1.0 if side == "l" else -1.0
		var sh: Array = j[side + "-shoulder"]
		var hand: Array = j[side + "-hand"]
		var a: float = atan2(hand[0] - sh[0], sh[1] - hand[1])
		var want: float = sg * (0.14 if opts.get("armAbduct", null) == null else float(opts["armAbduct"]))
		var rx: float = 24.0 * dd * sa * cos(tau * ph)
		rot[idx[side + "-shoulder"]] = [rx, 0.0, -(a - want)]
		rot[idx[side + "-elbow"]] = [-(8.0 + _ELBOW(ph) * 0.6) * sa * dd, 0.0, 0.0]
	return rot


static func pose(m: Dictionary, body: Dictionary, phase: float, opts: Dictionary) -> Dictionary:
	var s: float = float(opts.get("stride", 1.0))
	var tau := TAU
	var rot := rotations(m, body, phase, opts)
	var p := pose_explicit(m, body, rot, [0.018 * s * sin(tau * phase), 0.0, 0.0])
	var all: PackedFloat32Array = p["allPos"]
	var part: PackedInt32Array = m["part"]
	var parts: Array = m["parts"]
	var fl: int = parts.find("foot.L")
	var fr: int = parts.find("foot.R")
	var ymin := 1e9
	for i in int(m["nb"]):
		if part[i] == fl or part[i] == fr:
			ymin = minf(ymin, all[3 * i + 1])
	if ymin < 1e8:
		for i in all.size() / 3:
			all[3 * i + 1] -= ymin
		var pjj: Dictionary = p["joints"]
		for jname in pjj.keys():
			(pjj[jname] as Array)[1] -= ymin
		# pos is a copy (JS subarray is a view that sees this shift); refresh it
		p["pos"] = all.slice(0, 3 * int(m["nb"]))
	p["rot"] = rot
	p["root"] = [0.018 * s * sin(tau * phase), -ymin, 0.0]
	return p


static func arms(m: Dictionary, pos: PackedFloat32Array, joints: Dictionary) -> Dictionary:
	var out := {}
	for spec in [["L", "l"], ["R", "r"]]:
		var side: String = spec[0]
		var label: String = spec[1]
		var pts := [joints[label + "-shoulder"], joints[label + "-elbow"], joints[label + "-hand"]]
		var seg := []
		var len := []
		for i in 2:
			var d := [pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1], pts[i + 1][2] - pts[i][2]]
			var l: float = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])
			seg.append([d[0] / l, d[1] / l, d[2] / l])
			len.append(l)
		var total: float = len[0] + len[1]
		var nbin := int(ceil(total / 0.01)) + 1
		var rad := PackedFloat32Array()
		rad.resize(nbin)
		var want: int = (m["parts"] as Array).find("arm." + side)
		var part: PackedInt32Array = m["part"]
		for i in int(m["nb"]):
			if part[i] != want:
				continue
			var x: float = pos[3 * i]
			var y: float = pos[3 * i + 1]
			var z: float = pos[3 * i + 2]
			for k in 2:
				var dd := [x - pts[k][0], y - pts[k][1], z - pts[k][2]]
				var ss: float = dd[0] * seg[k][0] + dd[1] * seg[k][1] + dd[2] * seg[k][2]
				if ss < (0.0 if k == 1 else -0.02) or ss > len[k]:
					continue
				var rr: float = sqrt((dd[0] - ss * seg[k][0]) * (dd[0] - ss * seg[k][0]) + (dd[1] - ss * seg[k][1]) * (dd[1] - ss * seg[k][1]) + (dd[2] - ss * seg[k][2]) * (dd[2] - ss * seg[k][2]))
				var b := int(round((ss + (len[0] if k == 1 else 0.0)) / 0.01))
				if rr > rad[b]:
					rad[b] = rr
		for b in range(1, nbin):
			if rad[b] == 0.0:
				rad[b] = rad[b - 1]
		out[side] = {"pts": pts, "seg": seg, "len": len, "rad": rad, "total": total}
	return out


# Arm clearance (port of gait.js G.stand / G.armGap): the arms hang so that, below the armpit, they stay ARM_GAP clear of
# the body's section hulls (the surface garments are wrapped on). A body with wide hips or a belly (plus, curvy) needs its
# arms held further out, else the cloth is pinched between arm and torso. Never smaller than the default angle.
const ARM_ABDUCT := 0.14
const ARM_GAP := 0.02
const ARM_MAX := 0.5


# Signed distance of (x, z) to a convex polygon [x0, z0, x1, z1, ...]; negative inside.
static func _hull_dist(h: Array, x: float, z: float) -> float:
	var n: int = h.size() >> 1
	if n < 3:
		return 1.0
	var area := 0.0
	for i in n:
		var j := (i + 1) % n
		area += float(h[2 * i]) * float(h[2 * j + 1]) - float(h[2 * j]) * float(h[2 * i + 1])
	var sg := 1.0 if area >= 0.0 else -1.0
	var inside := true
	var best := 1e9
	for i in n:
		var j := (i + 1) % n
		var ax := float(h[2 * i])
		var az := float(h[2 * i + 1])
		var ex := float(h[2 * j]) - ax
		var ez := float(h[2 * j + 1]) - az
		var l := sqrt(ex * ex + ez * ez)
		if l == 0.0:
			l = 1e-9
		if (ex * (z - az) - ez * (x - ax)) * sg < 0.0:
			inside = false
		var t := clampf(((x - ax) * ex + (z - az) * ez) / (l * l), 0.0, 1.0)
		var dx := x - ax - t * ex
		var dz := z - az - t * ez
		best = minf(best, sqrt(dx * dx + dz * dz))
	return -best if inside else best


# Smallest distance between the arm and hand vertices below the waist + 6 cm and the section hulls: {d, y}.
static func arm_gap(m: Dictionary, all_pos: PackedFloat32Array, secs: Dictionary) -> Dictionary:
	var parts: Array = m["parts"]
	var part: PackedInt32Array = m["part"]
	var ids := []
	for nm in ["arm.L", "arm.R", "hand.L", "hand.R"]:
		ids.append(parts.find(nm))
	var y_max: float = all_pos[3 * int(m["lm"]["waist"]) + 1] + 0.06
	var hulls: Array = secs["hulls"]
	var dy: float = float(secs["dy"])
	var d := 1e9
	var y := 0.0
	for i in int(m["nb"]):
		if not ids.has(part[i]):
			continue
		var vy: float = all_pos[3 * i + 1]
		if vy > y_max:
			continue
		var k := int(round(vy / dy))
		if k < 0 or k >= hulls.size():
			continue
		var h = hulls[k]
		if h == null or (h as Array).size() < 6:
			continue
		var q := _hull_dist(h, all_pos[3 * i], all_pos[3 * i + 2])
		if q < d:
			d = q
			y = vy
	return {"d": d, "y": y}


static func stand(m: Dictionary, body: Dictionary) -> Dictionary:
	var abd := ARM_ABDUCT
	var p: Dictionary
	var secs: Dictionary
	for it in 4:
		p = pose(m, body, 0.0, {"stride": 0.0, "arms": 0.0, "armAbduct": abd})
		secs = sections(m, p["allPos"], 0.01)
		var g := arm_gap(m, p["allPos"], secs)
		if float(g["d"]) >= ARM_GAP - 0.002 or abd >= ARM_MAX:
			break
		var shoulder_y: float = float(p["joints"]["l-shoulder"][1])
		abd = minf(ARM_MAX, abd + (ARM_GAP - float(g["d"])) / maxf(0.15, shoulder_y - float(g["y"])))
	return _stand_dict(m, body, p, secs, abd)


# The standing body with the arms raised sideways to `abd` (radians): the dressing pose (an A-pose, as in CLO or
# Marvelous Designer). Same fields as stand(); armAbduct is `abd`.
#
# Its sections enclose the shoulders: the arm triangles above the armpit (SHOULDER_DROP below the shoulder joint) are
# sliced with the torso, so the torso rows from the armpit up reach over the deltoid to the shoulder tip, like a CLO torso
# bounding volume. With the torso-only hull a top's armhole ended about 5 cm short of the shoulder tip and its sleeve cap
# started 7-10 cm from the armhole, through the shoulder (docs/TEE_NOTES.md; sew_check.gd measures it).
const SHOULDER_DROP := 0.07
static func stand_at(m: Dictionary, body: Dictionary, abd: float) -> Dictionary:
	var p := pose(m, body, 0.0, {"stride": 0.0, "arms": 0.0, "armAbduct": abd})
	var all: PackedFloat32Array = p["allPos"]
	var cut: float = float(p["joints"]["l-shoulder"][1]) - SHOULDER_DROP
	var arm_tris := _tri_set(m, ["arm.L", "arm.R"])
	var tris: PackedInt32Array = m["tris"]
	var extra := PackedInt32Array()
	for t in arm_tris:
		if all[3 * tris[3 * t] + 1] > cut and all[3 * tris[3 * t + 1] + 1] > cut and all[3 * tris[3 * t + 2] + 1] > cut:
			extra.append(t)
	return _stand_dict(m, body, p, sections(m, all, 0.01, extra), abd)


static func _stand_dict(m: Dictionary, body: Dictionary, p: Dictionary, secs: Dictionary, abd: float) -> Dictionary:
	var out := {}
	for k in (body as Dictionary).keys():
		out[k] = body[k]
	out["pos"] = p["pos"]
	out["allPos"] = p["allPos"]
	out["joints"] = p["joints"]
	out["rest"] = body
	out["key"] = String(body.get("key", "")) + "/stand"
	out["landmarks"] = {}
	for lname in (m["lm"] as Dictionary).keys():
		var lid = (m["lm"] as Dictionary)[lname]
		if lid is int or lid is float:
			var ap: PackedFloat32Array = p["allPos"]
			out["landmarks"][lname] = [ap[3 * int(lid)], ap[3 * int(lid) + 1], ap[3 * int(lid) + 2]]
	out["sections"] = secs
	out["arms"] = arms(m, p["pos"], p["joints"])
	out["armAbduct"] = abd  # the arm angle used; a walk of this body must pass it to pose()
	return out
