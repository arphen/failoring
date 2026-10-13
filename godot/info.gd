# Pattern info panel: mirrors app/src/info.js. Formats plan.info (from FL.fsimport.plan)
# as BBCode for the editor's RichTextLabel: pattern title, cutting list, seams grouped by
# piece pair with lengths and ease, suggested sewing order, measurements.
class_name FailoringInfo

const Wrap = preload("res://wrap.gd")


static func _words(s: String) -> String:
	var out := s
	var r := ""
	var i := 0
	while i < out.length():
		var c := out[i]
		if i > 0 and c >= "A" and c <= "Z" and out[i - 1] >= "a" and out[i - 1] <= "z":
			r += " "
		r += c
		i += 1
	return r.replacen("_", " ").replacen("-", " ").replacen(".", " ").to_lower().strip_edges()


static func _f1(x: float) -> String:
	return "%.1f" % (round(x * 10.0) / 10.0)


static func _name_of(id: String, pieces: Array, info_pieces: Array) -> String:
	for p in pieces:
		if String(p.get("id", "")) == id:
			return String(p.get("name", id))
	for q in info_pieces:
		if String(q.get("id", "")) == id:
			return String(q.get("role", id))
	return id


# Seams of one piece pair (both directions, several runs) as one group.
static func _seam_groups(seams: Array, by_type: bool) -> Array:
	var order: Array = []
	var map := {}
	for s in seams:
		var a := String(s["a"])
		var b := String(s["b"])
		var key := ("self|%s" % a) if a == b else "%s|%s" % [a, b] if a < b else "%s|%s" % [b, a]
		if by_type:
			var labs: Array = s.get("labels", [])
			var first := ""
			for l in labs:
				if String(l) != "":
					first = String(l)
					break
			key += "|" + _words(first)
		if not map.has(key):
			map[key] = {"a": a, "b": b, "runs": [], "la": 0.0, "lb": 0.0}
			order.append(key)
		var g: Dictionary = map[key]
		(g["runs"] as Array).append(s)
		g["la"] = float(g["la"]) + float(s.get("la", 0.0))
		g["lb"] = float(g["lb"]) + float(s.get("lb", 0.0))
	var out: Array = []
	for k in order:
		var g: Dictionary = map[k]
		g["diff"] = absf(float(g["lb"]) - float(g["la"]))
		var m: float = minf(float(g["la"]), float(g["lb"]))
		g["ease"] = m > 0.0 and float(g["diff"]) > 0.03 * m
		var seen := {}
		var labs: Array = []
		for r in g["runs"]:
			for l in (r as Dictionary).get("labels", []):
				var w := _words(String(l))
				if w != "" and not seen.has(w):
					seen[w] = true
					labs.append(w)
		g["labels"] = labs
		out.append(g)
	return out


static func _len_text(g: Dictionary) -> String:
	if float(g["diff"]) < 0.05:
		return _f1(float(g["la"])) + " cm"
	return _f1(float(g["la"])) + " / " + _f1(float(g["lb"])) + " cm"


static func _sewing_steps(groups: Array, piece_ids: Array, pieces: Array, info_pieces: Array) -> Array:
	var left := groups.duplicate()
	left.sort_custom(func(x: Dictionary, y: Dictionary) -> bool: return maxf(float(y["la"]), float(y["lb"])) < maxf(float(x["la"]), float(x["lb"])))
	var done := {}
	var steps: Array = []
	while not left.is_empty():
		var i := -1
		for k in left.size():
			if done.has(left[k]["a"]) or done.has(left[k]["b"]):
				i = k
				break
		if i < 0:
			i = 0
		var g: Dictionary = left[i]
		left.remove_at(i)
		var an := _name_of(String(g["a"]), pieces, info_pieces)
		var bn := _name_of(String(g["b"]), pieces, info_pieces)
		var where: String = ", ".join(g["labels"]) if not (g["labels"] as Array).is_empty() else "seam"
		var len := _len_text(g) + (" in %d seams" % (g["runs"] as Array).size() if (g["runs"] as Array).size() > 1 else "")
		var t := ""
		if String(g["a"]) == String(g["b"]):
			t = "Sew %s along its %s (%s)" % [an, where, len]
		elif done.has(g["a"]) and done.has(g["b"]):
			t = "Also sew %s to %s at the %s (%s)" % [an, bn, where, len]
		else:
			t = "Join %s and %s at the %s (%s)" % [an, bn, where, len]
		if bool(g["ease"]):
			t += ". Ease: %s cm gathered into the shorter side" % _f1(float(g["diff"]))
		steps.append(t)
		done[g["a"]] = true
		done[g["b"]] = true
	var names: Array = []
	for id in piece_ids:
		names.append(_name_of(String(id), pieces, info_pieces))
	steps.append("Finish the edges that were not joined (hems, necklines, cuffs, openings) on: " + ", ".join(names))
	return steps


static func _cutting_rows(info_pieces: Array) -> Array:
	var order: Array = []
	var map := {}
	for q in info_pieces:
		var k := String(q.get("role", q.get("id", "?")))
		if not map.has(k):
			map[k] = {"role": k, "ids": [], "count": 0, "fold": false, "mirrored": false, "grain": q.get("grain", null), "material": q.get("material", null)}
			order.append(k)
		var r: Dictionary = map[k]
		(r["ids"] as Array).append(String(q.get("id", "?")))
		var note := String(q.get("cutNote", ""))
		if note.contains("mirrored pair"):
			r["mirrored"] = true
			var m := -1
			var toks := note.split(" ")
			for ti in toks.size():
				if toks[ti] == "cut" and ti + 1 < toks.size():
					m = toks[ti + 1].to_int()
			r["count"] = maxi(int(r["count"]), m if m > 0 else 2)
		else:
			r["count"] = int(r["count"]) + int(q.get("cut", 1))
		if note.contains("on the fold"):
			r["fold"] = true
	return order.map(func(k: String) -> Dictionary: return map[k])


static func _fabric_name(material: Variant, fallback: String) -> String:
	if material == null or String(material) == "" or String(material) == "fabric":
		return fallback
	return String(material)


# Full panel text. pieces = plan pieces (for names), fabric_name = current fabric
# display name, meas = body measurements (for plans without info).
static func format(info: Dictionary, pieces: Array, fabric_name: String, meas: Dictionary) -> String:
	var lines: Array = []
	var info_pieces: Array = info.get("pieces", [])
	var title := String(info.get("title", ""))
	if title != "":
		lines.append("[b][font_size=16]%s[/font_size][/b]" % title)
		var url := String(info.get("url", ""))
		var credit := "Pattern by FreeSewing (MIT licence)"
		if url.begins_with("https://"):
			credit += " - " + url
		lines.append("[color=#8f98a6]%s[/color]" % credit)
		lines.append("")
		lines.append("[b]Cutting list[/b]")
		for r in _cutting_rows(info_pieces):
			var det: Array = []
			det.append("on fold: %s" % ("yes" if bool(r["fold"]) else "no"))
			if bool(r["mirrored"]):
				det.append("mirrored pair")
			if r["grain"] != null:
				det.append("grain %s°" % str(r["grain"]))
			det.append(_fabric_name(r["material"], fabric_name))
			lines.append("• [b]%s[/b] ×%d (%s)" % [String(r["role"]), int(r["count"]), " · ".join(det)])
		lines.append("[color=#8f98a6]Grain is the grainline angle from the FreeSewing draft.[/color]")
		lines.append("")
		lines.append("[b]Seams[/b]")
		var groups := _seam_groups(info.get("seams", []), false)
		if groups.is_empty():
			lines.append("No seams.")
		for g in groups:
			var head := "[b]%s" % _name_of(String(g["a"]), pieces, info_pieces)
			if String(g["a"]) != String(g["b"]):
				head += " ↔ %s" % _name_of(String(g["b"]), pieces, info_pieces)
			else:
				head += " (own edges)"
			head += "[/b]  %s  (%d seam%s)" % [_len_text(g), (g["runs"] as Array).size(), "" if (g["runs"] as Array).size() == 1 else "s"]
			lines.append("• " + head)
			for run in g["runs"]:
				var lab := "seam"
				for l in (run as Dictionary).get("labels", []):
					if String(l) != "":
						lab = _words(String(l))
						break
				var lens := _f1(float((run as Dictionary).get("la", 0.0)))
				if absf(float((run as Dictionary).get("la", 0.0)) - float((run as Dictionary).get("lb", 0.0))) >= 0.05:
					lens += " / " + _f1(float((run as Dictionary).get("lb", 0.0)))
				lines.append("    %s (%d edge%s): %s" % [lab, int((run as Dictionary).get("n", 1)), "" if int((run as Dictionary).get("n", 1)) == 1 else "s", lens])
			if bool(g["ease"]):
				lines.append("    [color=#ff9d4d]ease: %s cm gathered (the longer side goes into the shorter)[/color]" % _f1(float(g["diff"])))
		lines.append("[color=#8f98a6]Lengths in cm, from the design for these measurements.[/color]")
		lines.append("")
		lines.append("[b]Suggested sewing order[/b]")
		var ids: Array = []
		for q in info_pieces:
			ids.append(String(q.get("id", "?")))
		var steps := _sewing_steps(_seam_groups(info.get("seams", []), true), ids, pieces, info_pieces)
		var n := 1
		for t in steps:
			lines.append("%d. %s" % [n, t])
			n += 1
		lines.append("")
		lines.append("[b]Measurements[/b]")
		var mmeas: Dictionary = info.get("measurements", {})
		for k in mmeas.keys():
			if mmeas[k] is float or mmeas[k] is int:
				lines.append("• %s: %s cm" % [_words(String(k)), _f1(float(mmeas[k]) / 10.0)])
	else:
		lines.append("[b]Hand-drafted pattern[/b]")
		lines.append("[color=#8f98a6]Drafted in this app, not from a published pattern.[/color]")
		lines.append("")
		lines.append("[b]Pieces[/b]")
		for p in pieces:
			var bb := Wrap.bbox_of(_outlines(p))
			lines.append("• [b]%s[/b] %s x %s cm" % [String(p.get("name", p.get("id", "?"))), _f1((bb[2] - bb[0]) * 100.0), _f1((bb[3] - bb[1]) * 100.0)])
		lines.append("")
		lines.append("[b]Body measurements in use[/b]")
		for k in ["height", "bust", "waist", "hip", "shoulder", "arm", "inseam", "neck", "thigh", "upperarm", "back"]:
			if meas.has(k):
				var v = meas[k]
				lines.append("• %s: %s" % [_words(k), "auto" if v == null else "%s cm" % _f1(float(v) * 100.0)])
	return "\n".join(lines)


static func _outlines(p: Dictionary) -> Array:
	var out := []
	for q in p.get("outline", []):
		out.append(Vector2(float(q[0]), float(q[1])))
	return out
