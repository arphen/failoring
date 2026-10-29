# Garment drafting: port of app/src/draft.js. Patterns computed from the mannequin's own
# measurements: pants (trousers), tank, shirt. Needs the live body (sections, joints,
# landmarks); section curves come from FailoringWrap's _section_curve via a helper instance.
class_name FailoringDraft

const BodyGen = preload("res://body_gen.gd")


# Measures read off the body's section table (port of FL.draft.measures).
static func measures(body: Dictionary, wrap_helper: FailoringWrap) -> Dictionary:
	var s: Dictionary = body["sections"]
	var _dy: float = s["dy"]
	var _ny: int = s["ny"]
	var at := func(list: Array, y: float) -> Array:
		var h = list[clampi(int(round(y / _dy)), 0, _ny - 1)]
		return h if h != null else []
	var per := func(h) -> float:
		if h == null or (h as Array).size() < 6:
			return 0.0
		return BodyGen.perimeter2(PackedFloat32Array(h))
	var j: Dictionary = body["joints"]
	var lm: Dictionary = body["landmarks"]
	var yc: float = s["yCrotch"]
	return {
		"yCrotch": yc, "yWaist": lm["waist"][1], "yHip": lm["hip"][1],
		"yKnee": j["l-knee"][1], "yAnkle": j["l-ankle"][1],
		"yShoulder": lm["acromion.L"][1], "yNeck": lm["cervicale"][1], "yBust": lm["bust.L"][1],
		"hip": per.call(at.call(s["hulls"], lm["hip"][1])), "waist": per.call(at.call(s["hulls"], lm["waist"][1])),
		"bust": per.call(at.call(s["hulls"], lm["bust.L"][1])),
		"thigh": per.call(at.call(s["legs"]["L"], yc - 0.035)),
		"knee": per.call(at.call(s["legs"]["L"], j["l-knee"][1])),
		"ankle": per.call(at.call(s["legs"]["L"], j["l-ankle"][1] + 0.04)),
	}


static func _mirror_piece(pts: Array, names: Array) -> Dictionary:
	var n := pts.size()
	var mx := -1e9
	for p in pts:
		mx = maxf(mx, float(p[0]))
	var out := []
	var on := []
	for jj in n:
		out.append([mx - float(pts[n - 1 - jj][0]), pts[n - 1 - jj][1]])
	for jj in n:
		on.append(names[((n - 2 - jj) % n + n) % n])
	return {"pts": out, "names": on}


static func pants(body: Dictionary, wrap_helper: FailoringWrap, opt: Dictionary) -> Dictionary:
	opt = opt if opt != null else {}
	var m := measures(body, wrap_helper)
	var hem_y: float = opt.get("hemY", 0.04)
	var ease: float = opt.get("ease", 0.035)
	var li: float = m["yCrotch"] - hem_y
	var vk: float = m["yKnee"] - hem_y
	var vh: float = m["yHip"] - hem_y
	var vt: float = (m["yWaist"] - 0.01 if opt.get("topY", null) == null else float(opt["topY"])) - hem_y
	var hg: float = m["hip"]
	var wg: float = m["waist"]
	var s: Dictionary = body["sections"]
	var dy: float = s["dy"]
	var ny: int = s["ny"]
	var rk := func(y: float) -> int: return clampi(int(round(y / dy)), 0, ny - 1)
	var curve := func(y: float) -> Dictionary: return wrap_helper.section_curve_for(y, s, 0.012)
	var arc_f := func(y: float) -> float:
		var c: Dictionary = curve.call(y)
		return _cum_at(c, PI * 0.5) - _cum_at(c, 0.0)
	var arc_b := func(y: float) -> float:
		var c: Dictionary = curve.call(y)
		return _cum_at(c, PI) - _cum_at(c, PI * 0.5)
	var y_top_w: float = m["yWaist"] - 0.01 if opt.get("topY", null) == null else float(opt["topY"])
	var y_top_b: float = y_top_w + 0.03
	var yb: float = (y_top_w + m["yHip"]) / 2.0
	var gb := 0.0
	var y: float = m["yHip"] + 0.03
	while y <= y_top_w - 0.03:
		var g: float = (s["girth"] as PackedFloat32Array)[rk.call(y)]
		if g > gb:
			gb = g
			yb = y
		y += dy
	var wf: float = arc_f.call(m["yHip"]) + ease / 4.0
	var wb: float = arc_b.call(m["yHip"]) + ease / 4.0
	var ext_total: float = maxf(0.07, (m["thigh"] + 0.08) - (wf + wb))
	var ef := 0.30 * ext_total
	var eb := 0.70 * ext_total
	var knee_g: float = maxf(m["knee"] + 0.06, 0.34)
	var hem_g: float = float(opt.get("hemGirth", maxf(m["ankle"] + 0.07, 0.40)))
	var fronts := {"w": wf, "e": ef, "top": vt, "cfTop": wf, "share": 0.46, "arc": arc_f, "yTop": y_top_w}
	var backs := {"w": wb, "e": eb, "top": y_top_b - hem_y, "cfTop": wb + 0.025, "share": 0.54, "arc": arc_b, "yTop": y_top_b}
	var make := func(f: Dictionary) -> Dictionary:
		var xc: float = (f["w"] + f["e"]) / 2.0
		var kw: float = f["share"] * knee_g
		var hw: float = f["share"] * hem_g
		var pts := []
		var names := []
		var add := func(x: float, yy: float, nm: String) -> void:
			pts.append([x, yy])
			names.append(nm)
		add.call(xc - hw / 2.0, 0.0, "hem")
		add.call(xc + hw / 2.0, 0.0, "inseam0")
		add.call(xc + kw / 2.0, vk, "inseam1")
		add.call(f["w"] + f["e"], li, "crotch0")
		for i in range(1, 4):
			var s2: float = 1.0 - float(i) / 4.0
			add.call(f["w"] + f["e"] * (1.0 - cos(PI * 0.5 * s2)), vh + (li - vh) * s2, "crotch%d" % i)
		add.call(f["w"], vh, "rise")
		add.call(f["cfTop"], f["top"], "waist")
		add.call(f["cfTop"] - (f["arc"] as Callable).call(f["yTop"]) - ease / 4.0, f["top"], "out2a")
		add.call(f["w"] - (f["arc"] as Callable).call(yb) - ease / 4.0, yb - hem_y, "out2b")
		add.call(0.0, vh, "out1")
		add.call(xc - kw / 2.0, vk, "out0")
		return {"pts": pts, "names": names}
	var f: Dictionary = make.call(fronts)
	var bk: Dictionary = make.call(backs)
	var fm := _mirror_piece(f["pts"], f["names"])
	var bm := _mirror_piece(bk["pts"], bk["names"])
	var mk := func(pid: String, pname: String, pp: Dictionary, side: String, theta: float, x: float) -> Dictionary:
		return {"id": pid, "name": pname, "outline": pp["pts"], "names": pp["names"], "grain": PI * 0.5,
			"table": {"x": x, "y": 0.0}, "wrap": {"mode": "leg", "side": side, "theta": theta, "y": hem_y, "clear": 0.012}}
	var pieces := [mk.call("frontR", "Front, right leg", f, "R", 0.0, 0.0), mk.call("frontL", "Front, left leg", fm, "L", 0.0, 0.55),
		mk.call("backL", "Back, left leg", bk, "L", 180.0, 1.1), mk.call("backR", "Back, right leg", bm, "R", 180.0, 1.65)]
	var by_id := {}
	for pp in pieces:
		by_id[pp["id"]] = pp
	var idx := func(pp: Dictionary, nm: String) -> int: return (pp["names"] as Array).find(nm)
	var pair := func(a: String, b: String, names: Array) -> Array:
		var out := []
		for nm in names:
			out.append({"a": [a, idx.call(by_id[a], nm)], "b": [b, idx.call(by_id[b], nm)], "reverse": true})
		return out
	var seams := []
	seams.append_array(pair.call("frontL", "backL", ["out0", "out1", "out2a", "out2b", "inseam0", "inseam1"]))
	seams.append_array(pair.call("frontR", "backR", ["out0", "out1", "out2a", "out2b", "inseam0", "inseam1"]))
	seams.append_array(pair.call("frontL", "frontR", ["crotch0", "crotch1", "crotch2", "crotch3", "rise"]))
	seams.append_array(pair.call("backL", "backR", ["crotch0", "crotch1", "crotch2", "crotch3", "rise"]))
	for pp in pieces:
		(pp as Dictionary).erase("names")
	return {
		"name": "Straight trousers - 4 panels (drafted to this body)", "fabric": String(opt.get("fabric", "denim_12oz")),
		"notes": "Front and back panel for each leg, drafted from this mannequin: hip %.0f cm, waist %.0f cm, thigh %.0f cm, knee %.0f cm. Seams: outseams, inseams, front and back rise. No waistband, pockets or darts." % [hg * 100.0, wg * 100.0, m["thigh"] * 100.0, m["knee"] * 100.0],
		"pieces": pieces, "seams": seams,
	}


static func shirt(body: Dictionary, wrap_helper: FailoringWrap, opt: Dictionary) -> Dictionary:
	var o := dict_merge({"sleeves": true, "armholeScoop": 12.0, "armholeDepth": 0.19, "sleeveEase": 1.0, "fabric": "cotton_poplin"}, opt)
	return tank(body, wrap_helper, o)


# A crop top: the tank cut off above the waist, its hem 38 % of the way from the waist up to the bust (about 7 to 8 cm
# above the waist on the presets), so a strip of midriff shows above a skirt or trousers at the waist. Below the bust its
# sides run straight down (straightBelow), a boxy crop: cut to the ribcage, the hem rode up under the bust.
static func crop(body: Dictionary, wrap_helper: FailoringWrap, opt: Dictionary) -> Dictionary:
	var m := measures(body, wrap_helper)
	var hem := float(m["yWaist"]) + 0.38 * (float(m["yBust"]) - float(m["yWaist"]))
	return tank(body, wrap_helper, dict_merge({"hemY": hem, "straightBelow": float(m["yBust"]), "name": "Crop top - 2 panels (drafted to this body)",
		"fabric": "cotton_jersey"}, opt))


static func tank(body: Dictionary, wrap_helper: FailoringWrap, opt: Dictionary) -> Dictionary:
	opt = opt if opt != null else {}
	var s: Dictionary = body["sections"]
	var m := measures(body, wrap_helper)
	var clear: float = opt.get("clear", 0.02)
	var dd := PI / 180.0
	var y_tip: float = m["yShoulder"]
	var y_top: float = y_tip + 0.05
	var y_u: float = y_tip - float(opt.get("armholeDepth", 0.17))
	var y_hem: float = opt.get("hemY", m["yHip"])
	var scoop: float = opt.get("scoop", 0.11)
	var strap_w := 0.032
	var row_k := func(y: float) -> int: return clampi(int(round(y / float(s["dy"]))), 0, int(s["ny"]) - 1)
	var arc_at := func(y: float, theta: float, dphi: float) -> float:
		var c: Dictionary = wrap_helper.section_curve_for(y, s, clear)
		return _cum_at(c, theta + dphi) - _cum_at(c, theta)
	var ys := [y_hem]
	var y := y_hem + 0.05
	while y < y_u - 0.02:
		ys.append(y)
		y += 0.05
	ys.append(y_u)
	var na := 4
	for i in range(1, na + 1):
		ys.append(y_u + (y_tip - y_u) * float(i) / float(na))
	var nsh := 2
	for i in range(1, nsh + 1):
		ys.append(y_tip + (y_top - y_tip) * float(i) / float(nsh))
	var i_u: int = ys.find(y_u)
	var i_tip: int = i_u + na
	var make := func(theta: float, depth: float) -> Dictionary:
		var half := func(yy: float) -> float:
			if yy <= y_u or yy >= y_tip:
				return 90.0
			var ss: float = (yy - y_u) / (y_tip - y_u)
			return 90.0 - float(opt.get("armholeScoop", 24.0)) * sin(PI * ss)
		var r := []
		for yy in ys:
			r.append(arc_at.call(yy, theta, half.call(yy) * dd))
		# opt.straightBelow: below this height the panel is no narrower than there (a crop top hangs straight down from the
		# bust; following the ribcage in, its hem was a ring smaller than the bust and rode up under it)
		if opt.has("straightBelow"):
			var yb := float(opt["straightBelow"])
			var rb: float = arc_at.call(yb, theta, half.call(yb) * dd)
			for i in ys.size():
				if float(ys[i]) < yb:
					r[i] = maxf(float(r[i]), rb)
		var x_top: float = r[r.size() - 1] - strap_w
		var y_n0: float = y_top - depth
		var neck := []
		var nn := 6
		for i in range(nn + 1):
			var s_n: float = float(i) / float(nn)
			var yy: float = y_top - (y_top - y_n0) * s_n
			var qq: float = (yy - y_n0) / (y_top - y_n0)
			neck.append([x_top * sqrt(maxf(0.0, 2.0 * qq - qq * qq)), yy])
		var pts := []
		var names := []
		var v := func(yy: float) -> float: return yy - y_hem
		pts.append([-r[0], 0.0])
		names.append("hem")
		pts.append([r[0], 0.0])
		for i in range(ys.size() - 1):
			names.append(("side%d" % i) if i < i_u else (("arm%d" % i) if i < i_tip else ("sh%d" % i)))
			pts.append([r[i + 1], v.call(ys[i + 1])])
		names.append("strapR")
		pts.append([neck[0][0], v.call(neck[0][1])])
		for i in range(neck.size() - 1):
			names.append("neck%d" % i)
			pts.append([neck[i + 1][0], v.call(neck[i + 1][1])])
		for i in range(neck.size() - 2, -1, -1):
			names.append("neck%d" % (neck.size() - 1 + (neck.size() - 2 - i)))
			pts.append([-neck[i][0], v.call(neck[i][1])])
		names.append("strapL")
		pts.append([-r[r.size() - 1], v.call(ys[ys.size() - 1])])
		for i in range(ys.size() - 2, -1, -1):
			names.append(("%sL%d" % ["side", i]) if i < i_u else (("%sL%d" % ["arm", i]) if i < i_tip else ("%sL%d" % ["sh", i])))
			pts.append([-r[i], v.call(ys[i])])
		pts.pop_back()
		return {"pts": pts, "names": names}
	var fr: Dictionary = make.call(0.0, scoop)
	var bk: Dictionary = make.call(PI, 0.03)
	var mk := func(pid: String, pname: String, pp: Dictionary, theta: float, x: float) -> Dictionary:
		return {"id": pid, "name": pname, "outline": pp["pts"], "names": pp["names"], "grain": PI * 0.5,
			"table": {"x": x, "y": 0.0}, "wrap": {"mode": "arc", "theta": theta, "y": y_hem, "clear": clear}}
	var pieces := [mk.call("front", "Front", fr, 0.0, 0.0), mk.call("back", "Back", bk, 180.0, 0.75)]
	var idx := func(pp: Dictionary, nm: String) -> int: return (pp["names"] as Array).find(nm)
	var seams := []
	for i in range(ys.size() - 1):
		var kind := ""
		var kind_l := ""
		if i < i_u:
			kind = "side"
			kind_l = "sideL"
		elif i >= i_tip:
			kind = "sh"
			kind_l = "shL"
		else:
			continue
		seams.append({"a": ["front", idx.call(pieces[0], kind + str(i))], "b": ["back", idx.call(pieces[1], kind_l + str(i))], "reverse": true})
		seams.append({"a": ["front", idx.call(pieces[0], kind_l + str(i))], "b": ["back", idx.call(pieces[1], kind + str(i))], "reverse": true})
	if opt.get("sleeves", false):
		var sl: float = opt.get("sleeveLen", 0.20)
		var elen := func(pp: Dictionary, nm: String) -> float:
			var ii: int = idx.call(pp, nm)
			var o: Array = pp["outline"]
			var qq: Array = o[ii]
			var rr: Array = o[(ii + 1) % o.size()]
			return sqrt((rr[0] - qq[0]) * (rr[0] - qq[0]) + (rr[1] - qq[1]) * (rr[1] - qq[1]))
		pieces[0]["names"] = fr["names"]
		pieces[1]["names"] = bk["names"]
		var tx := 1.5
		for side in ["L", "R"]:
			var a: Dictionary = pieces[0] if side == "L" else pieces[1]
			var b: Dictionary = pieces[1] if side == "L" else pieces[0]
			var la := []
			var lb := []
			for k in na:
				la.append(elen.call(a, "arm%d" % (i_u + k)))
			for jj in na:
				lb.append(elen.call(b, "armL%d" % (i_tip - 1 - jj)))
			var se: float = opt.get("sleeveEase", 0.9)
			var xs := [0.0]
			for l in la + lb:
				xs.append(xs[xs.size() - 1] + float(l) * se)
			var w: float = xs[xs.size() - 1]
			var wh: float = w * float(opt.get("sleeveHem", 0.9))
			var pts := [[0.0, 0.0], [wh, 0.0], [w, sl]]
			var names := ["hem", "seamR"]
			for i in range(xs.size() - 1, 0, -1):
				names.append(("topB%d" % (i - 1 - na)) if i - 1 >= na else ("topA%d" % (i - 1)))
				pts.append([xs[i - 1], sl])
			names.append("seamL")
			var pid: String = "sleeve" + side
			pieces.append({"id": pid, "name": ("Left" if side == "L" else "Right") + " sleeve", "outline": pts, "names": names,
				"grain": PI * 0.5, "table": {"x": tx, "y": 0.0},
				"wrap": {"mode": "arm", "side": side, "sTop": 0.06, "clear": 0.02, "phiSeam": -90.0 if side == "L" else 90.0}})
			tx += 0.0
			var s2: Dictionary = pieces[pieces.size() - 1]
			for k in na:
				seams.append({"a": [pid, idx.call(s2, "topA%d" % k)], "b": [a["id"], idx.call(a, "arm%d" % (i_u + k))], "reverse": true})
			for jj in na:
				seams.append({"a": [pid, idx.call(s2, "topB%d" % jj)], "b": [b["id"], idx.call(b, "armL%d" % (i_tip - 1 - jj))], "reverse": true})
			seams.append({"a": [pid, idx.call(s2, "seamR")], "b": [pid, idx.call(s2, "seamL")], "reverse": true})
		pieces[2]["table"] = {"x": 1.3, "y": 0.0}
		pieces[3]["table"] = {"x": 1.3, "y": 0.4}
	for pp in pieces:
		(pp as Dictionary).erase("names")
	return {
		"name": String(opt["name"]) if opt.has("name") else ("T-shirt - body + 2 sleeves (drafted to this body)" if opt.get("sleeves", false) else "Tank top - 2 panels (drafted to this body)"),
		"fabric": String(opt.get("fabric", "cotton_poplin")),
		"notes": "Front and back, unrolled from this mannequin's trunk sections (arc length of the body at every height plus %d mm ease). Side seams and shoulder seams; scoop neckline, no darts or hem allowance." % int(round(clear * 1000.0)),
		"pieces": pieces, "seams": seams,
	}


static func dict_merge(base: Dictionary, over: Variant) -> Dictionary:
	var out := {}
	for k in base.keys():
		out[k] = base[k]
	if over is Dictionary:
		for k in (over as Dictionary).keys():
			out[k] = over[k]
	return out


# cum(s) helper for a section-curve dict (port of the c.cum closure use in draft.js).
static func _cum_at(c: Dictionary, phi: float) -> float:
	var per: float = c["per"]
	var cum: PackedFloat64Array = c["cum"]
	var n := 720
	var w: float = floor(phi / TAU)
	var ph: float = phi - w * TAU
	var f := ph / TAU * float(n)
	var i := mini(n - 1, int(floor(f)))
	return w * per + cum[i] + (cum[i + 1] - cum[i]) * (f - float(i))
