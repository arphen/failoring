# Garment recipes: the format an agent (or a person in a text editor) writes a garment in, expanded to a plan the solver
# builds (plan_loader.gd) on a given body. docs/GARMENT_RECIPES.md is the guide; ./garment is the command line around it.
#
# A recipe is a plan with conveniences:
#   - numbers can be expressions over the body ("waist * 0.55", "y_knee - 0.05"): girths and heights in metres
#   - "include": drafted pieces fitted to the body (draft.gd: tank, crop, shirt, pants), with their options
#   - "shape": a rectangle, trapezoid or triangle from its sizes instead of an outline
#   - "around": N copies of a piece round the body (N panels of a tube), side seams sewn automatically
#   - edges by name ("top", "bottom", "left", "right", "hem", ...) as well as by index
# expand() resolves all of these, so the result is an ordinary plan; check() reports problems with the fix in the text.
class_name FailoringGarmentRecipe
extends RefCounted

const Draft = preload("res://draft.gd")
const Shapes = preload("res://shapes.gd")
const Table = preload("res://table.gd")
const Seams = preload("res://seams.gd")

const FORMAT := "failoring-garment/1"
const FABRICS := ["cotton_poplin", "cotton_crepe", "denim_12oz", "linen_medium", "wool_suiting", "silk_charmeuse",
	"polyester_chiffon", "canvas_heavy", "cotton_jersey", "lambskin_leather"]
const DRAFTS := ["tank", "crop", "shirt", "pants"]
const CATEGORIES := ["tops", "bottoms", "skirts", "dresses", "custom"]
# edge names of the shapes, by edge index (edge i runs from outline point i to point i + 1, counter-clockwise)
const SHAPE_EDGES := {
	"rect": ["bottom", "right", "top", "left"],
	"trapezoid": ["bottom", "right", "top", "left"],
	"triangle": ["bottom", "right", "left"],
}
const GATHER_MAX := 3.5   # a seam whose one side is longer than this many times the other cannot be eased in
const WRAP_KEYS := ["mode", "theta", "y", "clear", "ring", "loose", "side", "sTop", "phiSeam", "lift", "vcap", "P", "Py0", "Pdy",
	"tilt", "over"]


# Body variables the expressions can use (metres): girths and heights above the floor.
static func body_vars(body: Dictionary, wrap_helper: FailoringWrap) -> Dictionary:
	var m := Draft.measures(body, wrap_helper)
	var lm: Dictionary = body["landmarks"]
	var top := 0.0
	for k in lm:
		top = maxf(top, float(lm[k][1]))
	var pos: PackedFloat32Array = body["pos"]
	var h := 0.0
	for i in range(1, pos.size(), 3):
		h = maxf(h, pos[i])
	return {
		"height": h,
		"bust": m["bust"], "waist": m["waist"], "hip": m["hip"], "thigh": m["thigh"], "knee": m["knee"], "ankle": m["ankle"],
		"y_neck": m["yNeck"], "y_shoulder": m["yShoulder"], "y_bust": m["yBust"], "y_waist": m["yWaist"], "y_hip": m["yHip"],
		"y_crotch": m["yCrotch"], "y_knee": m["yKnee"], "y_ankle": m["yAnkle"],
	}


# The recipe on this body -> {plan, errors: [text], warnings: [text], edges: {pid: [names]}}.
# The plan has numeric outlines and seams (no expressions, shapes, includes or names) and is not normalized.
static func expand(recipe: Dictionary, body: Dictionary, wrap_helper: FailoringWrap) -> Dictionary:
	var ctx := {"vars": body_vars(body, wrap_helper), "errors": [], "warnings": []}
	var pieces := []
	var names := {}       # pid -> [edge name per edge index]
	var groups := {}      # around id -> [copy ids]
	var seams := []
	var fabric := String(recipe.get("fabric", "cotton_poplin"))
	if not fabric in FABRICS:
		ctx["errors"].append("fabric '%s' is not one of %s" % [fabric, ", ".join(FABRICS)])
		fabric = "cotton_poplin"
	# drafted parts first: they come with their own pieces and seams
	for inc in recipe.get("include", []):
		if not (inc is Dictionary) or not DRAFTS.has(String(inc.get("draft", ""))):
			ctx["errors"].append("include %s: needs \"draft\": one of %s" % [JSON.stringify(inc), ", ".join(DRAFTS)])
			continue
		var opt: Dictionary = _eval_all(inc.get("opt", {}), ctx, "include %s opt" % inc["draft"])
		opt["names"] = true
		var d: Dictionary = _draft(String(inc["draft"]), body, wrap_helper, opt)
		var prefix := String(inc.get("prefix", ""))
		for p in d["pieces"]:
			var q: Dictionary = (p as Dictionary).duplicate(true)
			q["id"] = prefix + String(q["id"])
			names[q["id"]] = q.get("names", [])
			q.erase("names")
			pieces.append(q)
		for s in d["seams"]:
			var t: Dictionary = (s as Dictionary).duplicate(true)
			t["a"] = [prefix + String(t["a"][0]), t["a"][1]]
			t["b"] = [prefix + String(t["b"][0]), t["b"][1]]
			seams.append(t)
	var tx := 0.0
	for p in pieces:
		tx = maxf(tx, float(p["table"]["x"]) + 0.8)
	for raw in recipe.get("pieces", []):
		if not (raw is Dictionary) or String(raw.get("id", "")) == "":
			ctx["errors"].append("piece %s: needs an \"id\"" % JSON.stringify(raw).left(80))
			continue
		var pid := String(raw["id"])
		var where := "piece '%s'" % pid
		var outline := []
		var en := []
		var holes := []
		if raw.has("shape"):
			var sh: Dictionary = _eval_all(raw["shape"], ctx, where + " shape")
			var kind := String(sh.get("kind", ""))
			if not SHAPE_EDGES.has(kind) and kind != "ellipse":
				ctx["errors"].append("%s: shape kind '%s' unknown (rect, trapezoid, triangle, ellipse)" % [where, kind])
				continue
			var cm := {}
			for k in sh:
				if k != "kind":
					cm[k] = float(sh[k]) * 100.0
			var made: Dictionary = Shapes.make(kind, cm)
			outline = made["outline"]
			en = SHAPE_EDGES.get(kind, [])
		elif raw.has("outline"):
			for q in _eval_all(raw["outline"], ctx, where + " outline"):
				outline.append([float(q[0]), float(q[1])])
			en = raw.get("edges", [])
			holes = raw.get("holes", [])
		else:
			ctx["errors"].append("%s: needs \"shape\" or \"outline\"" % where)
			continue
		var wrap: Dictionary = _eval_all(raw.get("wrap", {}), ctx, where + " wrap")
		for k in wrap:
			if not k in WRAP_KEYS:
				ctx["warnings"].append("%s: wrap key '%s' is not used (keys: theta, y, ring, clear)" % [where, k])
		if not wrap.has("y"):
			ctx["errors"].append("%s: wrap needs \"y\", the height of the piece's lowest point above the floor (m)" % where)
		var base := {"name": String(raw.get("name", pid)), "outline": outline, "grain": float(_eval(raw.get("grain", PI * 0.5), ctx, where)),
			"wrap": wrap}
		if not holes.is_empty():
			base["holes"] = holes
		var n := int(raw.get("around", 0))
		if n >= 2:
			var ids := []
			var th0 := float(wrap.get("theta", 0.0))
			for i in n:
				var c: Dictionary = base.duplicate(true)
				c["id"] = "%s%d" % [pid, i + 1]
				c["name"] = "%s %d" % [base["name"], i + 1]
				c["wrap"]["theta"] = fposmod(th0 + 360.0 * float(i) / float(n), 360.0)
				if not c["wrap"].has("ring"):
					c["wrap"]["ring"] = float(n)
				c["table"] = {"x": tx, "y": 0.0}
				tx += _width(outline) + 0.1
				ids.append(c["id"])
				names[c["id"]] = en
				pieces.append(c)
			groups[pid] = ids
			# the tube: copy i's right edge to copy i + 1's left edge (round the body, the last one back to the first)
			var ri := en.find("right")
			var li := en.find("left")
			if ri < 0 or li < 0:
				ctx["errors"].append("%s: \"around\" needs edges named \"right\" and \"left\" (use a rect or trapezoid shape, or name them in \"edges\")" % where)
			else:
				for i in n:
					seams.append({"a": [ids[i], ri], "b": [ids[(i + 1) % n], li], "reverse": true})
		else:
			base["id"] = pid
			base["table"] = {"x": tx, "y": 0.0}
			tx += _width(outline) + 0.1
			names[pid] = en
			pieces.append(base)
	var by_id := {}
	for p in pieces:
		if by_id.has(p["id"]):
			ctx["errors"].append("piece id '%s' is used twice" % p["id"])
		by_id[p["id"]] = p
	# seams: a group id stands for all its copies (sewn copy to copy, so both sides need the same count)
	for raw in recipe.get("seams", []):
		if not (raw is Dictionary) or not raw.has("a") or not raw.has("b"):
			ctx["errors"].append("seam %s: needs \"a\" and \"b\", each [piece id, edge name or index]" % JSON.stringify(raw))
			continue
		var la: Array = groups.get(String(raw["a"][0]), [String(raw["a"][0])])
		var lb: Array = groups.get(String(raw["b"][0]), [String(raw["b"][0])])
		if la.size() != lb.size():
			ctx["errors"].append("seam %s: '%s' has %d pieces and '%s' has %d; sew groups with the same \"around\" count, or name single copies (%s)" % [
				JSON.stringify(raw), raw["a"][0], la.size(), raw["b"][0], lb.size(), ", ".join(PackedStringArray(la + lb))])
			continue
		for i in la.size():
			var sa := _sides(by_id, names, String(la[i]), raw["a"][1], ctx)
			var sb := _sides(by_id, names, String(lb[i]), raw["b"][1], ctx)
			if sa.is_empty() or sb.is_empty():
				continue
			var rev := bool(raw.get("reverse", true))
			if sa.size() > 1 and sb.size() > 1:
				ctx["errors"].append("seam %s: both sides are several edges (%d and %d); one side must be a single edge (it is shared out along the other)" % [
					JSON.stringify(raw), sa.size(), sb.size()])
				continue
			if sa.size() == 1 and sb.size() == 1:
				seams.append({"a": sa[0], "b": sb[0], "reverse": rev})
				continue
			# one edge along several (a frill along a neckline): the single edge is shared out in proportion to their lengths
			var many := sa if sa.size() > 1 else sb
			var one: Array = (sb if sa.size() > 1 else sa)[0]
			var lens := []
			var total := 0.0
			for side in many:
				var l := Table.edge_len(by_id[side[0]]["outline"], int(side[1]))
				lens.append(l)
				total += l
			var r0 := Seams.side_range(one)
			var acc := 0.0
			for k in many.size():
				var f0: float = acc / total
				acc += float(lens[k])
				var f1: float = acc / total
				# reversed: the run along the many edges meets the single edge from its far end
				var t0: float = r0.x + (r0.y - r0.x) * ((1.0 - f1) if rev else f0)
				var t1: float = r0.x + (r0.y - r0.x) * ((1.0 - f0) if rev else f1)
				var part := Seams.make_side(String(one[0]), int(one[1]), t0, t1)
				seams.append({"a": many[k], "b": part, "reverse": rev} if sa.size() > 1 else {"a": part, "b": many[k], "reverse": rev})
	var cat := String(recipe.get("category", "custom"))
	if not cat in CATEGORIES:
		ctx["warnings"].append("category '%s' unknown, using custom (%s)" % [cat, ", ".join(CATEGORIES)])
	var plan := {"name": String(recipe.get("name", "Garment")), "fabric": fabric, "notes": String(recipe.get("notes", "")),
		"pieces": pieces, "seams": seams}
	_check(plan, names, ctx)
	return {"plan": plan, "errors": ctx["errors"], "warnings": ctx["warnings"], "edges": names, "vars": ctx["vars"]}


static func _draft(kind: String, body: Dictionary, wrap_helper: FailoringWrap, opt: Dictionary) -> Dictionary:
	match kind:
		"pants":
			return Draft.pants(body, wrap_helper, opt)
		"crop":
			return Draft.crop(body, wrap_helper, opt)
		"shirt":
			return Draft.shirt(body, wrap_helper, opt)
	return Draft.tank(body, wrap_helper, opt)


static func _width(outline: Array) -> float:
	var bb := Table.bbox_of(outline)
	return float(bb[2]) - float(bb[0])


# An edge spec on piece pid -> sides ([pid, edge] or [pid, edge, t0, t1]), in the order of the outline. The spec is an
# index, a name, a name family "neck*" (every edge whose name starts with "neck", in outline order), or
# {"edge": name or index, "from": 0..1, "to": 0..1} for part of an edge.
static func _sides(by_id: Dictionary, names: Dictionary, pid: String, e, ctx: Dictionary) -> Array:
	if not by_id.has(pid):
		ctx["errors"].append("seam: no piece '%s' (pieces: %s)" % [pid, ", ".join(PackedStringArray(by_id.keys()))])
		return []
	var n: int = (by_id[pid]["outline"] as Array).size()
	var en: Array = names.get(pid, [])
	if e is Dictionary:
		var inner := _sides(by_id, names, pid, e.get("edge", ""), ctx)
		if inner.size() != 1:
			if inner.size() > 1:
				ctx["errors"].append("seam: piece '%s' {\"edge\": %s} must name one edge" % [pid, JSON.stringify(e.get("edge"))])
			return []
		var t0 := clampf(float(e.get("from", 0.0)), 0.0, 1.0)
		var t1 := clampf(float(e.get("to", 1.0)), 0.0, 1.0)
		if t1 - t0 < 0.02:
			ctx["errors"].append("seam: piece '%s' edge range from %.2f to %.2f is empty" % [pid, t0, t1])
			return []
		return [Seams.make_side(pid, int(inner[0][1]), t0, t1)]
	if e is String and String(e).ends_with("*"):
		var pre := String(e).trim_suffix("*")
		var out := []
		for i in en.size():
			if String(en[i]).begins_with(pre) and String(en[i]) != "":
				out.append([pid, i])
		if out.is_empty():
			ctx["errors"].append("seam: piece '%s' has no edges named %s (named edges: %s)" % [pid, e, ", ".join(PackedStringArray(_named(en)))])
		return out
	if e is String:
		var i: int = en.find(e)
		if i < 0:
			ctx["errors"].append("seam: piece '%s' has no edge '%s' (named edges: %s; or use an index 0..%d)" % [
				pid, e, ", ".join(PackedStringArray(_named(en))), n - 1])
			return []
		return [[pid, i]]
	var k := int(e)
	if k < 0 or k >= n:
		ctx["errors"].append("seam: piece '%s' edge %d out of range 0..%d" % [pid, k, n - 1])
		return []
	return [[pid, k]]


static func _named(en: Array) -> Array:
	var out := []
	for x in en:
		var f := _family(String(x))
		var nm := String(x) if f == "" else f + "*"
		if x != "" and not nm in out:
			out.append(nm)
	return out


# "neck11" -> "neck" (a family of numbered edges), "hem" -> ""
static func _family(nm: String) -> String:
	var i := nm.length()
	while i > 0 and nm[i - 1] >= "0" and nm[i - 1] <= "9":
		i -= 1
	return nm.left(i) if i < nm.length() and i > 0 else ""


# Every string in v (deep) that is not a plain word is evaluated as an expression over the body variables.
static func _eval_all(v, ctx: Dictionary, where: String):
	if v is Dictionary:
		var o := {}
		for k in v:
			o[k] = v[k] if k in ["kind", "mode", "side", "name"] else _eval_all(v[k], ctx, where)
		return o
	if v is Array:
		return (v as Array).map(func(x): return _eval_all(x, ctx, where))
	return _eval(v, ctx, where)


static func _eval(v, ctx: Dictionary, where: String):
	if not (v is String):
		return v
	var vars: Dictionary = ctx["vars"]
	var e := Expression.new()
	if e.parse(v, PackedStringArray(vars.keys())) != OK:
		ctx["errors"].append("%s: cannot read \"%s\" (%s). Variables: %s" % [where, v, e.get_error_text(), ", ".join(PackedStringArray(vars.keys()))])
		return 0.0
	var r = e.execute(vars.values(), null, false)
	if e.has_execute_failed() or not (r is float or r is int):
		ctx["errors"].append("%s: \"%s\" is not a number (%s)" % [where, v, e.get_error_text()])
		return 0.0
	return float(r)


# Problems an agent can fix from the text alone.
static func _check(plan: Dictionary, names: Dictionary, ctx: Dictionary) -> void:
	var by_id := {}
	for p in plan["pieces"]:
		by_id[p["id"]] = p
		var o: Array = p["outline"]
		if o.size() < 3:
			ctx["errors"].append("piece '%s': outline has %d points, needs 3 or more" % [p["id"], o.size()])
			continue
		if Table.self_intersects(o):
			ctx["errors"].append("piece '%s': outline crosses itself" % p["id"])
		var bb := Table.bbox_of(o)
		var w: float = float(bb[2]) - float(bb[0])
		var h: float = float(bb[3]) - float(bb[1])
		if w > 2.2 or h > 1.6:
			ctx["errors"].append("piece '%s' is %.0f x %.0f cm: sizes are in metres (0.5 = 50 cm)" % [p["id"], w * 100.0, h * 100.0])
		if w < 0.02 or h < 0.02:
			ctx["errors"].append("piece '%s' is %.1f x %.1f cm, too small to simulate (sizes are in metres)" % [p["id"], w * 100.0, h * 100.0])
		var y := float(p["wrap"].get("y", 0.0))
		if y < 0.0 or y + h > 2.1:
			ctx["errors"].append("piece '%s': wrap.y %.2f puts it from %.2f to %.2f m above the floor" % [p["id"], y, y, y + h])
	var used := {}   # "pid:edge" -> [ranges]
	for s in plan["seams"]:
		for side in [s["a"], s["b"]]:
			var key := "%s:%d" % [side[0], side[1]]
			var r := Seams.side_range(side)
			for q in used.get(key, []):
				if r.x < q.y - 1e-6 and q.x < r.y - 1e-6:
					ctx["errors"].append("edge %s of '%s' is sewn twice: sew each edge (or each part of it) to one other edge" % [
						_ename(names, side[0], side[1]), side[0]])
			if not used.has(key):
				used[key] = []
			used[key].append(r)
		if not by_id.has(s["a"][0]) or not by_id.has(s["b"][0]):
			continue
		var la := Seams.range_length(by_id[s["a"][0]]["outline"], s["a"])
		var lb := Seams.range_length(by_id[s["b"][0]]["outline"], s["b"])
		var r: float = maxf(la, lb) / maxf(1e-6, minf(la, lb))
		if r > GATHER_MAX:
			ctx["errors"].append("seam %s (%.0f cm) to %s (%.0f cm): %.1fx, more gathering than cloth can take (at most %.1fx)" % [
				_sname(names, s["a"]), la * 100.0, _sname(names, s["b"]), lb * 100.0, r, GATHER_MAX])


static func _sname(names: Dictionary, side: Array) -> String:
	var t := "%s.%s" % [side[0], _ename(names, side[0], int(side[1]))]
	if side.size() >= 4:
		t += "[%.2f-%.2f]" % [float(side[2]), float(side[3])]
	return t


static func _ename(names: Dictionary, pid: String, e: int) -> String:
	var en: Array = names.get(pid, [])
	return String(en[e]) if e < en.size() and String(en[e]) != "" else str(e)


# Human-readable table of the expanded plan: pieces with edges (index, name, length) and seams with lengths and
# gather ratio. What an agent reads to pick edges and check that seams pair the right sides.
static func describe(res: Dictionary) -> String:
	var plan: Dictionary = res["plan"]
	var names: Dictionary = res["edges"]
	var out := PackedStringArray()
	var by_id := {}
	var sewn := {}
	for s in plan["seams"]:
		sewn["%s:%d" % [s["a"][0], s["a"][1]]] = true
		sewn["%s:%d" % [s["b"][0], s["b"][1]]] = true
	out.append("GARMENT %s  fabric=%s  pieces=%d seams=%d" % [plan["name"], plan["fabric"], plan["pieces"].size(), plan["seams"].size()])
	for p in plan["pieces"]:
		by_id[p["id"]] = p
		var o: Array = p["outline"]
		var bb := Table.bbox_of(o)
		var w: Dictionary = p["wrap"]
		out.append("PIECE %s  %.0fx%.0f cm  wrap: theta=%s y=%.3f m (top at %.3f)%s" % [p["id"], (float(bb[2]) - float(bb[0])) * 100.0,
			(float(bb[3]) - float(bb[1])) * 100.0, str(w.get("theta", w.get("mode", "?"))), float(w.get("y", 0.0)),
			float(w.get("y", 0.0)) + float(bb[3]) - float(bb[1]), (" ring=%s" % str(w["ring"])) if w.has("ring") else ""])
		# numbered families (neck0..neck11 of a drafted piece) on one line each
		var edges := PackedStringArray()
		var e := 0
		while e < o.size():
			var nm := _ename(names, p["id"], e)
			var fam := _family(nm)
			var e1 := e
			if fam != "":
				while e1 + 1 < o.size() and _family(_ename(names, p["id"], e1 + 1)) == fam:
					e1 += 1
			if e1 > e:
				var tot := 0.0
				var open := 0
				for k in range(e, e1 + 1):
					tot += Table.edge_len(o, k)
					open += 0 if sewn.has("%s:%d" % [p["id"], k]) else 1
				edges.append("%d-%d=%s* %.1fcm%s" % [e, e1, fam, tot * 100.0, " (open)" if open == e1 - e + 1 else (" (%d open)" % open if open > 0 else "")])
			else:
				edges.append("%d%s %.1fcm%s" % [e, ("=" + nm) if nm != str(e) else "", Table.edge_len(o, e) * 100.0,
					"" if sewn.has("%s:%d" % [p["id"], e]) else " (open)"])
			e = e1 + 1
		out.append("  edges: " + ", ".join(edges))
	for s in plan["seams"]:
		if not by_id.has(s["a"][0]) or not by_id.has(s["b"][0]):
			continue
		var la := Seams.range_length(by_id[s["a"][0]]["outline"], s["a"])
		var lb := Seams.range_length(by_id[s["b"][0]]["outline"], s["b"])
		var r: float = maxf(la, lb) / maxf(1e-6, minf(la, lb))
		out.append("SEAM %s %.1fcm <-> %s %.1fcm%s%s" % [_sname(names, s["a"]), la * 100.0, _sname(names, s["b"]), lb * 100.0,
			("  gathered %.2fx" % r) if r > 1.08 else "", "" if bool(s.get("reverse", false)) else "  reverse=false"])
	for e in res["errors"]:
		out.append("ERROR " + e)
	for w in res["warnings"]:
		out.append("WARNING " + w)
	return "\n".join(out)
