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

const BodyGen = preload("res://body_gen.gd")

const NA := 180        # polar samples (shared.js NA)
const MS := 48         # arc samples (shared.js MS)
const ARC_N := 720     # arc-length table samples (shared.js N)

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
static func _polar(hull: Array, fix_x: Variant) -> Dictionary:
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


static func _rad(r: PackedFloat64Array, phi: float) -> float:
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
# on the shared sections Dictionary (like the JS S._curves), so all pieces of one body share it.
static func section_curve_at(sections: Dictionary, k: int, clear: float) -> Dictionary:
	var ny: int = sections["ny"]
	var key := "%d|%.6f" % [k, clear]
	if not sections.has("_curves"):
		sections["_curves"] = {}
	var store: Dictionary = sections["_curves"]
	var c = store.get(key, null)
	if c != null:
		return c
	var hull: Array = []
	for d in ny:
		var found := false
		for kk in [k - d, k + d]:
			if kk >= 0 and kk < ny and kk < sections["hulls"].size():
				var h = sections["hulls"][kk]
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
	cum.resize(ARC_N + 1)
	# like the JS pt(): the radius at angle p is interpolated from the R table
	var rad_at := func(p: float) -> float:
		var f := fposmod(p / TAU, 1.0) * float(NA * 2)
		var ii := mini(NA * 2 - 1, int(floor(f)))
		return r[ii] + (r[ii + 1] - r[ii]) * (f - float(ii))
	var prev := [rad_at.call(0.0) * sin(0.0), cz + rad_at.call(0.0) * cos(0.0)]
	for i in range(1, ARC_N + 1):
		var p := TAU * float(i) / float(ARC_N)
		var rr: float = rad_at.call(p)
		var q := [rr * sin(p), cz + rr * cos(p)]
		cum[i] = cum[i - 1] + sqrt((q[0] - prev[0]) * (q[0] - prev[0]) + (q[1] - prev[1]) * (q[1] - prev[1]))
		prev = q
	c = {"cz": cz, "per": cum[ARC_N], "r": r, "cum": cum}
	store[key] = c
	return c


func _section_curve(k: int, clear: float) -> Dictionary:
	return section_curve_at(_sections, k, clear)


# Section curve at height y, for draft.gd (row from y, same cache).
func section_curve_for(y: float, sections: Dictionary, clear: float) -> Dictionary:
	var dy: float = sections["dy"]
	var ny: int = sections["ny"]
	return section_curve_at(sections, clampi(int(round(y / dy)), 0, ny - 1), clear)


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
	var f := ph / TAU * float(ARC_N)
	var i := mini(ARC_N - 1, int(floor(f)))
	return w * float(c["per"]) + cum[i] + (cum[i + 1] - cum[i]) * (f - float(i))


func _curve_phi_at(c: Dictionary, s: float) -> float:
	var per: float = c["per"]
	var cum: PackedFloat64Array = c["cum"]
	var w: float = floor(s / per)
	var rest: float = s - w * per
	var lo := 0
	var hi := ARC_N
	while hi - lo > 1:
		var mid := (lo + hi) >> 1
		if cum[mid] <= rest:
			lo = mid
		else:
			hi = mid
	return w * TAU + (float(lo) + (rest - cum[lo]) / (cum[lo + 1] - cum[lo])) / float(ARC_N) * TAU


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
	var mode := String(_wrap.get("mode", ""))
	if mode == "":
		var g := _generic()
		var ring: float = float(_wrap.get("ring", 0.0))
		var width: float = _row_t(u, v)[1]
		var f := 1.0
		if ring > 0.0:
			f = maxf(1.0, ring * width / float(g["per"]))
		var p := _gen_phi_of(float(g["sc"]) + (u - _uc) / f)
		var r := _gen_rad(p) * f
		return Vector3(r * sin(p), float(_wrap.get("y", 0.0)) + (v - _bb[1]), float(g["cz"]) + r * cos(p))
	var p := _pos_at(u, v)
	return Vector3(p[0], p[1], p[2])


# Exploded pose (worker.js P1): upright and flat, tangent to the envelope at the piece
# centre, pushed outward by d. Used by the timeline stages, not the simulation.
func flat_point(u: float, v: float, d: float) -> Vector3:
	var mode := String(_wrap.get("mode", ""))
	if mode == "":
		var g := _generic()
		var tx: float = g["tx"]
		var tz: float = g["tz"]
		var c0: Array = g["c0"]
		return Vector3(c0[0] - tz * d + tx * (u - _uc), float(_wrap.get("y", 0.0)) + (v - _bb[1]), c0[1] + tx * d + tz * (u - _uc))
	if mode == "arm":
		var side := String(_wrap.get("side", "L"))
		var adv: Dictionary = _arms[side]
		var sg := 1.0 if side == "L" else -1.0
		var sTop: float = 0.06 if _wrap.get("sTop", null) == null else float(_wrap["sTop"])
		var s: float = clampf(minf(float(adv["total"]), sTop + (_bb[3] - v)), 0.0, float(adv["total"]))
		var k := 0 if s < float(adv["len"][0]) else 1
		var l: float = s - (float(adv["len"][0]) if k == 1 else 0.0)
		var seg: Array = adv["seg"][k]
		var pts: Array = adv["pts"][k]
		var rad: Array = adv["rad"]
		var ra: float = rad[mini(rad.size() - 1, int(round(s / 0.01)))]
		return Vector3(pts[0] + seg[0] * l + sg * (ra + clear_dist() + d), pts[1] + seg[1] * l, pts[2] + seg[2] * l - sg * (u - _uc))
	var fb := _flat_basis()
	return Vector3(fb[0] - fb[3] * d + fb[2] * (u - _uc), float(_wrap.get("y", 0.0)) + (v - _bb[1]), fb[1] + fb[2] * d + fb[3] * (u - _uc))


# Tangent basis at the piece centre for the exploded pose (cached per piece).
func _flat_basis() -> Array:
	if _flat != null:
		return _flat
	var vc: float = (_bb[1] + _bb[3]) * 0.5
	var c0 := _pos_at(_uc, vc)
	var ca := _pos_at(_uc - 0.005, vc)
	var cb := _pos_at(_uc + 0.005, vc)
	var tx: float = cb[0] - ca[0]
	var tz: float = cb[2] - ca[2]
	var tl: float = sqrt(tx * tx + tz * tz)
	if tl == 0.0:
		tl = 1.0
	_flat = [c0[0], c0[2], tx / tl, tz / tl]
	return _flat


var _flat = null
var _env = null


# Generic envelope wrap for pieces without wrap.mode (port of FL.makeWrap): convex hull of
# the body sections the piece spans, arc-length parametrised.
func _generic() -> Dictionary:
	if _env != null:
		return _env
	var w: Dictionary = _wrap
	var dy: float = _sections["dy"]
	var ny: int = _sections["ny"]
	var h0: float = float(w.get("y", 0.0))
	var h1: float = h0 + (_bb[3] - _bb[1])
	var k0 := maxi(0, int(floor(h0 / dy)))
	var k1 := mini(ny - 1, int(ceil(h1 / dy)))
	var pts := []
	for k in range(k0, k1 + 1):
		var h = _sections["hulls"][k]
		if h != null:
			for q in (h as Array):
				pts.append(float(q))
	if pts.size() < 6:
		pts = []
		for a in 16:
			pts.append(0.05 * sin(float(a) * PI / 8.0))
			pts.append(0.05 * cos(float(a) * PI / 8.0))
	var hull := BodyGen.hull2(PackedFloat32Array(pts), pts.size() / 2)
	var n: int = hull.size() / 2
	var a := 0.0
	var cz := 0.0
	for i in n:
		var j := (i + 1) % n
		var cr: float = hull[2 * i] * hull[2 * j + 1] - hull[2 * j] * hull[2 * i + 1]
		a += cr
		cz += (hull[2 * i + 1] + hull[2 * j + 1]) * cr
	cz = cz / (3.0 * a) if absf(a) > 1e-12 else 0.0
	var r := PackedFloat64Array()
	r.resize(361)
	for x in 360:
		var p := TAU * float(x) / 360.0
		var dx := sin(p)
		var dz := cos(p)
		var best := 0.02
		for i in n:
			var j := (i + 1) % n
			var px: float = hull[2 * i]
			var pz: float = hull[2 * i + 1] - cz
			var ex: float = hull[2 * j] - hull[2 * i]
			var ez: float = hull[2 * j + 1] - hull[2 * i + 1]
			var den: float = dx * ez - dz * ex
			if absf(den) < 1e-12:
				continue
			var t: float = (px * ez - pz * ex) / den
			var s: float = (px * dz - pz * dx) / den
			if t > best and s >= -1e-9 and s <= 1.0 + 1e-9:
				best = t
		r[x] = best + clear_dist()
	r[360] = r[0]
	_env = {"cz": cz, "R": r, "hull_n": n}
	_build_gen_tables()
	return _env


func _gen_rad(phi: float) -> float:
	var g: Dictionary = _env
	var r: PackedFloat64Array = g["R"]
	var f := fposmod(phi / TAU, 1.0) * 360.0
	var i := mini(359, int(floor(f)))
	return r[i] + (r[i + 1] - r[i]) * (f - float(i))


func _gen_curve(phi: float) -> Array:
	var r := _gen_rad(phi)
	return [r * sin(phi), float(_env["cz"]) + r * cos(phi)]


func _build_gen_tables() -> void:
	var st := PackedFloat64Array()
	st.resize(721)
	var prev := _gen_curve(0.0)
	for i in range(1, 721):
		var c := _gen_curve(TAU * float(i) / 720.0)
		st[i] = st[i - 1] + sqrt((c[0] - prev[0]) * (c[0] - prev[0]) + (c[1] - prev[1]) * (c[1] - prev[1]))
		prev = c
	var phi0: float = float(_wrap.get("theta", 0.0)) * PI / 180.0
	var f := fposmod(phi0 / TAU, 1.0) * 720.0
	var i := mini(719, int(floor(f)))
	var sc: float = st[i] + (st[i + 1] - st[i]) * (f - float(i))
	var c0 := _gen_curve(phi0)
	var ca := _gen_curve(phi0 - 1e-3)
	var cb := _gen_curve(phi0 + 1e-3)
	var tx: float = cb[0] - ca[0]
	var tz: float = cb[1] - ca[1]
	var tl: float = sqrt(tx * tx + tz * tz)
	if tl == 0.0:
		tl = 1.0
	_env["st"] = st
	_env["per"] = st[720]
	_env["sc"] = sc
	_env["phi0"] = phi0
	_env["c0"] = c0
	_env["tx"] = tx / tl
	_env["tz"] = tz / tl


func _gen_phi_of(s: float) -> float:
	var st: PackedFloat64Array = _env["st"]
	var per: float = _env["per"]
	s = fposmod(s, per)
	var lo := 0
	var hi := 720
	while hi - lo > 1:
		var mid := (lo + hi) >> 1
		if st[mid] <= s:
			lo = mid
		else:
			hi = mid
	return (float(lo) + (s - st[lo]) / (st[lo + 1] - st[lo])) / 720.0 * TAU
