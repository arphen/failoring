# Port of FL.makeWrapRows + FL.sectionCurve (app/src/shared.js) for the Godot editor.
# Built for one plan piece: Wrap.new(piece, sections, arms).point(u, v) places the solver mesh
# vertex with pattern coordinates (u, v) on the body, exactly like the browser's start positions
# (worker.js evaluates makeWrap at each mesh vertex). Verified against JS reference values by
# godot/tests/wrap_check.tscn. Only wrap.mode pieces are supported (the fsimport plans always
# carry one: fit, leg, arm, torso, arc); anything else is a load error, not a silent fallback.
#
# Precision: every intermediate value is a float (GDScript float is 64-bit, like JS numbers).
# Pattern outlines stay plain [x, y] pairs and tables are PackedFloat64Array on purpose: Godot's
# Vector2/Vector3 are 32-bit, and single precision flips the scanline crossing tests for vertices
# lying exactly on the scanline. Vector3 is built only for the returned position.
class_name FailoringWrap
extends RefCounted

const NA := 180        # polar samples (shared.js NA)
const MS := 48         # arc samples (shared.js MS)

var _piece: Dictionary
var _wrap: Dictionary
var _outline: Array    # [x, y] pattern pairs (floats, as in the plan JSON)
var _bb: Array         # [x0, y0, x1, y1]
var _uc := 0.0
var _sections: Dictionary
var _dy := 0.01
var _ny := 0
var _yc := 0.0
var _arms: Dictionary
var _arc_cache := {}   # key -> {cx, cz, R, cum, pa, pb}


static func bbox_of(outline: Array) -> Array:
	var x0 := 1e9
	var y0 := 1e9
	var x1 := -1e9
	var y1 := -1e9
	for q in outline:
		x0 = minf(x0, float(q[0]))
		y0 = minf(y0, float(q[1]))
		x1 = maxf(x1, float(q[0]))
		y1 = maxf(y1, float(q[1]))
	return [x0, y0, x1, y1]


static func _cross3(a: Array, b: Array) -> Array:
	return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


static func _norm3(a: Array) -> Array:
	var l: float = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
	return [a[0] / l, a[1] / l, a[2] / l]


func _init(piece: Dictionary, sections: Dictionary, arms: Dictionary) -> void:
	_piece = piece
	_wrap = piece.get("wrap", {})
	if String(_wrap.get("mode", "")) == "":
		push_error("FailoringWrap: piece '%s' has no wrap.mode" % piece.get("id", "?"))
	_outline = piece["outline"]
	_bb = bbox_of(_outline)
	_uc = (_bb[0] + _bb[2]) * 0.5
	_sections = sections
	_dy = float(sections.get("dy", 0.01))
	_ny = int(sections.get("ny", 0))
	_yc = float(sections.get("yCrotch", 0.0))
	_arms = arms


func clear_dist() -> float:
	var c = _wrap.get("clear", null)
	return 0.012 if c == null else float(c)


# Fractional position of u within the piece row at height v (scanline width).
# Returns [t, width]; a zero-width row (apex point, not a real row) keeps JS semantics:
# t is then garbage in both implementations (see wrap_ref notes), so reference checks skip it.
func _row_t(u: float, v: float) -> Array:
	var o: Array = _outline
	var lo := 1e9
	var hi := -1e9
	for i in o.size():
		var a: Array = o[i]
		var b: Array = o[(i + 1) % o.size()]
		if (float(a[1]) - v) * (float(b[1]) - v) <= 0.0 and float(a[1]) != float(b[1]):
			var x: float = float(a[0]) + (float(b[0]) - float(a[0])) * (v - float(a[1])) / (float(b[1]) - float(a[1]))
			lo = minf(lo, x)
			hi = maxf(hi, x)
	if hi <= lo:
		lo = _uc - 1e-4
		hi = _uc + 1e-4
	return [(u - lo) / (hi - lo), hi - lo]


# Nearest non-empty hull (flat [x0,z0,...]) at row k, or [] when none exists.
func _nearest(list: Array, k: int) -> Array:
	for d in _ny:
		if k - d >= 0 and k - d < list.size():
			var h = list[k - d]
			if h != null and (h as Array).size() >= 6:
				return h
		if k + d < _ny and k + d < list.size():
			var h2 = list[k + d]
			if h2 != null and (h2 as Array).size() >= 6:
				return h2
	return []


# Polar description of a convex polygon around its area centroid (x fixed when given).
func _polar(hull: Array, fix_x: Variant) -> Dictionary:
	var n: int = hull.size() / 2
	var a := 0.0
	var cx := 0.0
	var cz := 0.0
	for i in n:
		var j := (i + 1) % n
		var cr: float = float(hull[2 * i]) * float(hull[2 * j + 1]) - float(hull[2 * j]) * float(hull[2 * i + 1])
		a += cr
		cx += (float(hull[2 * i]) + float(hull[2 * j])) * cr
		cz += (float(hull[2 * i + 1]) + float(hull[2 * j + 1])) * cr
	if absf(a) > 1e-12:
		cx /= 3.0 * a
		cz /= 3.0 * a
	else:
		cx = 0.0
		cz = 0.0
	if fix_x != null:
		cx = float(fix_x)
	var r := PackedFloat64Array()
	r.resize(NA + 1)
	for x in NA:
		var p := TAU * float(x) / float(NA)
		var dx := sin(p)
		var dz := cos(p)
		var best := 0.02
		for i in n:
			var j := (i + 1) % n
			var px: float = float(hull[2 * i]) - cx
			var pz: float = float(hull[2 * i + 1]) - cz
			var ex: float = float(hull[2 * j]) - float(hull[2 * i])
			var ez: float = float(hull[2 * j + 1]) - float(hull[2 * i + 1])
			var den: float = dx * ez - dz * ex
			if absf(den) < 1e-12:
				continue
			var t: float = (px * ez - pz * ex) / den
			var s: float = (px * dz - pz * dx) / den
			if t > best and s >= -1e-9 and s <= 1.0 + 1e-9:
				best = t
		r[x] = best
	r[NA] = r[0]
	return {"cx": cx, "cz": cz, "R": r}


func _rad(r: PackedFloat64Array, phi: float) -> float:
	var f := fposmod(phi / TAU, 1.0) * float(NA)
	var i := mini(NA - 1, int(floor(f)))
	return r[i] + (r[i + 1] - r[i]) * (f - float(i))


# Point at fraction t of the arc through [pa, pb] of a polar section (cached per key).
func _arc_point(key: String, hull: Array, fix_x: Variant, pa: float, pb: float, t: float) -> Array:
	var e = _arc_cache.get(key, null)
	if e == null:
		var pl := _polar(hull, fix_x)
		var cum := PackedFloat64Array()
		cum.resize(MS + 1)
		var prev := [0.0, 0.0]
		for i in MS + 1:
			var p: float = pa + (pb - pa) * float(i) / float(MS)
			var rr: float = _rad(pl["R"], p) + clear_dist()
			var c := [float(pl["cx"]) + rr * sin(p), float(pl["cz"]) + rr * cos(p)]
			if i > 0:
				cum[i] = cum[i - 1] + sqrt((c[0] - prev[0]) * (c[0] - prev[0]) + (c[1] - prev[1]) * (c[1] - prev[1]))
			prev = c
		e = {"pl": pl, "cum": cum, "pa": pa, "pb": pb}
		_arc_cache[key] = e
	var tot: float = e["cum"][MS]
	var s: float = clampf(t, 0.0, 1.0) * tot
	var lo := 0
	var hi := MS
	while hi - lo > 1:
		var mid := (lo + hi) >> 1
		if e["cum"][mid] <= s:
			lo = mid
		else:
			hi = mid
	var span: float = e["cum"][lo + 1] - e["cum"][lo]
	var p2: float = e["pa"] + (e["pb"] - e["pa"]) * (float(lo) + (s - e["cum"][lo]) / (span if span != 0.0 else 1.0)) / float(MS)
	var r2: float = _rad(e["pl"]["R"], p2) + clear_dist()
	return [float(e["pl"]["cx"]) + r2 * sin(p2), float(e["pl"]["cz"]) + r2 * cos(p2)]


func _theta() -> float:
	return float(_wrap.get("theta", 0.0)) * PI / 180.0


func _regime_torso(k: int, t: float) -> Array:
	var span: float = (180.0 if _wrap.get("span", null) == null else float(_wrap["span"])) * PI / 180.0
	var th := _theta()
	return _arc_point("t%d" % k, _nearest(_sections["hulls"], k), 0.0, th - span * 0.5, th + span * 0.5, t)


func _regime_leg(k: int, t: float) -> Array:
	var side := String(_wrap.get("side", "L"))
	var kv := mini(k, int(floor((_yc - 0.006) / _dy)))
	var th := _theta()
	return _arc_point("l%s%d" % [side, kv], _nearest(_sections["legs"][side], kv), null, th - PI * 0.5, th + PI * 0.5, t)


func _regime_half(k: int, t: float) -> Array:
	var side := String(_wrap.get("side", "L"))
	var up: bool = (side == "L") == (float(_wrap.get("theta", 0.0)) == 0.0)
	var th := _theta()
	var pa: float = th if up else th - PI * 0.5
	var tag := "u" if up else "d"
	return _arc_point("h%s%s%d" % [side, tag, k], _nearest(_sections["hulls"], k), 0.0, pa, pa + PI * 0.5, t)


static func _ysmooth(x: float) -> float:
	x = clampf(x, 0.0, 1.0)
	return x * x * (3.0 - 2.0 * x)


# Arc-length parametrised trunk section at row k (port of FL.sectionCurve). The cache lives
# on the shared sections Dictionary (like the JS S._curves), so all pieces of one preset share it.
func _section_curve(k: int, clear: float) -> Dictionary:
	var key := "%d|%.6f" % [k, clear]
	if not _sections.has("_curves"):
		_sections["_curves"] = {}
	var store: Dictionary = _sections["_curves"]
	var c = store.get(key, null)
	if c != null:
		return c
	var hull: Array = []
	for d in _ny:
		var found := false
		for kk in [k - d, k + d]:
			if kk >= 0 and kk < _ny and kk < _sections["hulls"].size():
				var h = _sections["hulls"][kk]
				if h != null and (h as Array).size() >= 6:
					hull = h
					found = true
					break
		if found:
			break
	var n: int = hull.size() / 2
	var a := 0.0
	var cz := 0.0
	for i in n:
		var j := (i + 1) % n
		var cr: float = float(hull[2 * i]) * float(hull[2 * j + 1]) - float(hull[2 * j]) * float(hull[2 * i + 1])
		a += cr
		cz += (float(hull[2 * i + 1]) + float(hull[2 * j + 1])) * cr
	cz = cz / (3.0 * a) if absf(a) > 1e-12 else 0.0
	var r := PackedFloat64Array()
	r.resize(NA * 2 + 1)
	for x in NA * 2:
		var p := TAU * float(x) / float(NA * 2)
		var dx := sin(p)
		var dz := cos(p)
		var best := 0.02
		for i in n:
			var j := (i + 1) % n
			var px: float = float(hull[2 * i])
			var pz: float = float(hull[2 * i + 1]) - cz
			var ex: float = float(hull[2 * j]) - float(hull[2 * i])
			var ez: float = float(hull[2 * j + 1]) - float(hull[2 * i + 1])
			var den: float = dx * ez - dz * ex
			if absf(den) < 1e-12:
				continue
			var t: float = (px * ez - pz * ex) / den
			var s: float = (px * dz - pz * dx) / den
			if t > best and s >= -1e-9 and s <= 1.0 + 1e-9:
				best = t
		r[x] = best + clear
	r[NA * 2] = r[0]
	var cum := PackedFloat64Array()
	cum.resize(NA * 2 + 1)
	var prev := [r[0] * sin(0.0), cz + r[0] * cos(0.0)]
	for i in range(1, NA * 2 + 1):
		var p := TAU * float(i) / float(NA * 2)
		var q := [r[i] * sin(p), cz + r[i] * cos(p)]
		cum[i] = cum[i - 1] + sqrt((q[0] - prev[0]) * (q[0] - prev[0]) + (q[1] - prev[1]) * (q[1] - prev[1]))
		prev = q
	c = {"cz": cz, "per": cum[NA * 2], "r": r, "cum": cum}
	store[key] = c
	return c


func _curve_rad(c: Dictionary, phi: float) -> float:
	var r: PackedFloat64Array = c["r"]
	var f := fposmod(phi / TAU, 1.0) * float(NA * 2)
	var i := mini(NA * 2 - 1, int(floor(f)))
	return r[i] + (r[i + 1] - r[i]) * (f - float(i))


func _curve_pt(c: Dictionary, phi: float) -> Array:
	var rr := _curve_rad(c, phi)
	return [rr * sin(phi), float(c["cz"]) + rr * cos(phi)]


func _curve_cum(c: Dictionary, phi: float) -> float:
	var w: float = floor(phi / TAU)
	var ph: float = phi - w * TAU
	var cum: PackedFloat64Array = c["cum"]
	var f := ph / TAU * float(NA * 2)
	var i := mini(NA * 2 - 1, int(floor(f)))
	return w * float(c["per"]) + cum[i] + (cum[i + 1] - cum[i]) * (f - float(i))


func _curve_phi_at(c: Dictionary, s: float) -> float:
	var per: float = c["per"]
	var cum: PackedFloat64Array = c["cum"]
	var w: float = floor(s / per)
	var rest: float = s - w * per
	var lo := 0
	var hi := NA * 2
	while hi - lo > 1:
		var mid := (lo + hi) >> 1
		if cum[mid] <= rest:
			lo = mid
		else:
			hi = mid
	return w * TAU + (float(lo) + (rest - cum[lo]) / (cum[lo + 1] - cum[lo])) / float(NA * 2) * TAU


# Sleeve: tube around the arm axis (port of armPos). Returns [x, y, z].
func _arm_pos(u: float, v: float) -> Array:
	var side := String(_wrap.get("side", "L"))
	var adv: Dictionary = _arms[side]
	var rt := _row_t(u, v)
	var t: float = rt[0]
	var sTop: float = 0.06 if _wrap.get("sTop", null) == null else float(_wrap["sTop"])
	var s: float = clampf(minf(float(adv["total"]), sTop + (_bb[3] - v)), 0.0, float(adv["total"]))
	var k := 0 if s < float(adv["len"][0]) else 1
	var l: float = s - (float(adv["len"][0]) if k == 1 else 0.0)
	var seg: Array = adv["seg"][k]
	var pts: Array = adv["pts"][k]
	var c := [float(pts[0]) + float(seg[0]) * l, float(pts[1]) + float(seg[1]) * l, float(pts[2]) + float(seg[2]) * l]
	var dz: float = float(seg[2])
	var e0 := _norm3([-dz * float(seg[0]), -dz * float(seg[1]), 1.0 - dz * float(seg[2])])
	var up := [-float(seg[0]), -float(seg[1]), -float(seg[2])]
	var e1 := _cross3(up, e0)
	var rad: Array = adv["rad"]
	var ra: float = rad[mini(rad.size() - 1, int(round(s / 0.01)))]
	var width: float = rt[1]
	var loose: float = 0.0 if _wrap.get("loose", null) == null else float(_wrap["loose"])
	var r: float = maxf(float(ra) + 0.004, minf(float(ra) + clear_dist() + loose, width / TAU))
	var phiSeam: float = (0.0 if _wrap.get("phiSeam", null) == null else float(_wrap["phiSeam"])) * PI / 180.0
	var ph: float = phiSeam + TAU * t
	var cs := cos(ph)
	var sn := sin(ph)
	return [c[0] + r * (cs * e0[0] + sn * e1[0]), c[1] + r * (cs * e0[1] + sn * e1[1]), c[2] + r * (cs * e0[2] + sn * e1[2])]


func _pos_at(u: float, v: float) -> Array:
	var mode := String(_wrap.get("mode", ""))
	if mode == "arm":
		return _arm_pos(u, v)
	var y: float = float(_wrap.get("y", 0.0)) + (v - _bb[1])
	var k := clampi(int(round(y / _dy)), 0, _ny - 1)
	var rt := _row_t(u, v)
	var t: float = rt[0]
	var p: Array
	if mode == "leg":
		var zb := 0.05
		var wt := _ysmooth((y - (_yc - zb)) / (2.0 * zb))
		if wt <= 0.0:
			p = _regime_leg(k, t)
		elif wt >= 1.0:
			p = _regime_half(k, t)
		else:
			var la := _regime_leg(k, t)
			var hb := _regime_half(k, t)
			p = [la[0] + (hb[0] - la[0]) * wt, la[1] + (hb[1] - la[1]) * wt]
	elif mode == "fit":
		var pw: Array = _wrap["P"]
		var py0 := float(_wrap["Py0"])
		var pdy := float(_wrap["Pdy"])
		var pii: float = (y - py0) / pdy
		var i0 := clampi(int(floor(pii)), 0, pw.size() - 1)
		var i1 := mini(pw.size() - 1, i0 + 1)
		var pww: float = float(pw[i0]) + (float(pw[i1]) - float(pw[i0])) * clampf(pii - float(i0), 0.0, 1.0)
		var c0 := _section_curve(k, clear_dist())
		var d := 0.0
		if pww > float(c0["per"]):
			d = minf(0.06, round((pww - float(c0["per"])) / TAU * 1000.0) / 1000.0)
		var c: Dictionary = _section_curve(k, clear_dist() + d) if d > 0.0 else c0
		var kk: float = float(c["per"]) / pww
		var th := _theta()
		var base: float = th + TAU if th < 0.0 else th
		var cc: Dictionary = c
		if _wrap.get("over", null) != null and u * float(_wrap["over"]) < 0.0:
			cc = _section_curve(k, clear_dist() + d + 0.014)
		var s: float = _curve_cum(cc, base) + u * kk * (float(cc["per"]) / float(c["per"]))
		var q := _curve_pt(cc, _curve_phi_at(cc, s))
		p = [q[0], q[1]]
	else:  # "torso" and "arc" and anything else with sections
		if mode == "arc":
			var c := _section_curve(k, clear_dist())
			var th := _theta()
			var base: float = th + TAU if th < 0.0 else th
			var qq := _curve_pt(c, _curve_phi_at(c, _curve_cum(c, base) + u))
			p = [qq[0], qq[1]]
		else:
			p = _regime_torso(k, t)
	return [p[0], y, p[1]]


# Start position of the solver mesh vertex with pattern coordinates (u, v).
func point(u: float, v: float) -> Vector3:
	var p := _pos_at(u, v)
	return Vector3(p[0], p[1], p[2])
