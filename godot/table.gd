# Cutting table: port of app/src/editor2d.js plus the piece-geometry edits of app/src/state.js.
# Pan/zoom canvas, select/move pieces, drag corners, add/delete corners, sew edges, snap,
# undo/redo. State (pieces, seams, selection, undo) lives in the editor; this Control draws
# it and translates mouse and keys into edits, then calls ed.changed(kind).
extends Control

const Geom = preload("res://cut_geom.gd")
const Seams = preload("res://seams.gd")
const Guides = preload("res://guides.gd")
const SEAM_COLORS := ["#3fd3c0", "#ff9d4d", "#c792ea", "#82aaff", "#f07178", "#c3e88d", "#ffcb6b", "#89ddff"]
const FABRIC_COLORS := {"cotton_poplin": "#6f93c4", "cotton_crepe": "#b9776a", "denim_12oz": "#3d5c8c", "linen_medium": "#c2b184",
	"wool_suiting": "#566175", "silk_charmeuse": "#a8405c", "polyester_chiffon": "#dc94ac",
	"canvas_heavy": "#a89a76", "cotton_jersey": "#6f9a88", "lambskin_leather": "#7a4630"}

var ed = null  # set by the editor: the Editor node holding pieces/seams/selection/undo
var cam_z := 260.0
var ox := 0.0
var oy := 0.0
var drag = null
var fitted := false  # kept for the editor, which resets it on load; fit() is what matters now
var hint_label: Label
var _fit_pending := false   # fit() is deferred to the next draw, once the control has a real size
var _sew_note := ""         # what the last sew said (lengths, warning), shown in the hint line until the next click
var measure_pts: Array = []  # Measure tool: the first point (and the second once clicked), world metres
var _pointer := Vector2.ZERO  # the last pointer position in world metres (the measure line follows it)
# View options (the table bar's View menu). Seam links are the thin lines joining the two sewn edges of a seam: a pattern
# with many short sewn edges (a FreeSewing curve is dozens of 3 cm edges, each its own seam) draws dozens of crossing
# lines, so by default only the links of the piece under the pointer and the picked seams are drawn; the sewn part of
# every edge is always marked in its seam's colour. Edge lengths are drawn only where the label fits beside its edge.
var seam_links := "hover"     # "hover", "all" or "off" (off: picked seams only)
var show_lengths := true
var line_a := {}               # Line tool: the first point picked, {pid, gid} (empty until then)
var _snap := {}                # the snap target under the last snapped point {at: world Vector2, what: String}, for drawing
const SNAP_PX := 10.0          # object snap radius on screen
var _link_pid := ""            # the last piece the pointer was over: its links stay drawn while the pointer leaves it


# The hovered edge as {"p": piece id, "e": edge index} while the sew tool is on, or null. Hover and picks live in the
# editor's selection model (selection.gd), shared with the tabs and the 3D view.
func _hover_edge():
	if ed == null or ed.selection.hover_kind() != "edge":
		return null
	var id = ed.selection.hover_id()
	return {"p": String(id[0]), "e": int(id[1])}


func _on_selection_hover() -> void:
	if ed != null and _hover_pid() != "":
		_link_pid = _hover_pid()
	if ed != null and ed.tool == "sew":
		update_hint()
	queue_redraw()


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	resized.connect(_on_resized)


func _on_resized() -> void:
	_fit_pending = true
	queue_redraw()


# ------------------------------------------------------------------ geometry helpers
static func signed_area(o: Array) -> float:
	var a := 0.0
	for i in o.size():
		var q: Array = o[(i + 1) % o.size()]
		a += float(o[i][0]) * float(q[1]) - float(q[0]) * float(o[i][1])
	return a * 0.5


static func edge_len(o: Array, i: int) -> float:
	var a: Array = o[i]
	var b: Array = o[(i + 1) % o.size()]
	return sqrt((float(b[0]) - float(a[0])) * (float(b[0]) - float(a[0])) + (float(b[1]) - float(a[1])) * (float(b[1] - a[1])))


static func self_intersects(o: Array) -> bool:
	var n := o.size()
	var ccw := func(a: Array, b: Array, c: Array) -> float:
		return (float(c[1]) - float(a[1])) * (float(b[0]) - float(a[0])) - (float(b[1]) - float(a[1])) * (float(c[0]) - float(a[0]))
	for i in n:
		for j in range(i + 2, n):
			if i == 0 and j == n - 1:
				continue
			var a: Array = o[i]
			var b: Array = o[(i + 1) % n]
			var c: Array = o[j]
			var d: Array = o[(j + 1) % n]
			if ccw.call(a, c, d) * ccw.call(b, c, d) < 0.0 and ccw.call(a, b, c) * ccw.call(a, b, d) < 0.0:
				return true
	return false


# Even-odd point in polygon (poly as [x, y] pairs). Static, so the static checks below can use it.
static func poly_contains(poly: Array, x: float, y: float) -> bool:
	var c := false
	var j: int = poly.size() - 1
	for i in poly.size():
		var a: Array = poly[i]
		var b: Array = poly[j]
		if (float(a[1]) > y) != (float(b[1]) > y) and x < (float(b[0]) - float(a[0])) * (y - float(a[1])) / (float(b[1]) - float(a[1])) + float(a[0]):
			c = not c
		j = i
	return c


# Holes of a piece sit inside its outline and do not overlap each other: every hole corner is inside the outline and
# outside the other holes, and no outline corner is inside a hole. A hole that crosses an edge is caught by the corner
# tests for the usual shapes; the rule is the table's (the mesher then drops the cloth inside the hole).
static func holes_ok(o: Array, holes: Array) -> bool:
	for hi in holes.size():
		var h: Array = holes[hi]
		if h.size() < 3:
			return false
		for q in h:
			if not poly_contains(o, float(q[0]), float(q[1])):
				return false
		for hj in holes.size():
			if hj == hi:
				continue
			for q in h:
				if poly_contains(holes[hj], float(q[0]), float(q[1])):
					return false
		for q in o:
			if poly_contains(h, float(q[0]), float(q[1])):
				return false
	return true


static func bbox_of(o: Array) -> Array:
	var x0 := 1e9
	var y0 := 1e9
	var x1 := -1e9
	var y1 := -1e9
	for q in o:
		x0 = minf(x0, float(q[0]))
		y0 = minf(y0, float(q[1]))
		x1 = maxf(x1, float(q[0]))
		y1 = maxf(y1, float(q[1]))
	return [x0, y0, x1, y1]


# Shortest edge the simulator accepts. The editor keeps every edit at MIN_EDGE_M (8 mm, editor.gd) or more, but a loaded
# FreeSewing pattern is not an edit: its dart tips are 6 mm on some sizes (penelope, average). With the 8 mm rule here the
# whole back half was dropped from the simulation and the skirt had no back (godot/tests/skirt_cover_check.gd). Only
# edges too short to mesh are refused now. Edges shorter than 6 mm were not tested.
const MIN_SIM_EDGE := 0.002


# validity before sending to the simulator (App.validPieces in the browser keeps the 8 mm rule)
static func valid_pieces(pieces: Array) -> Dictionary:
	var out := []
	var bad := {}
	for p in pieces:
		var o: Array = p["outline"]
		var why = null
		if o.size() < 3:
			why = "needs 3+ corners"
		elif signed_area(o) < 6e-4:
			why = "too small or inverted"
		elif self_intersects(o):
			why = "outline crosses itself"
		else:
			for i in o.size():
				if edge_len(o, i) < MIN_SIM_EDGE:
					why = "edge shorter than 2 mm"
					break
		if why == null and not holes_ok(o, p.get("holes", [])):
			why = "a hole is not inside the piece"
		if why != null:
			bad[String(p["id"])] = why
		else:
			out.append(p)
	return {"pieces": out, "bad": bad}


# Derived geometry for drawing, in piece-local coordinates (the table offset is added at draw time, so a drag
# does not rebuild it). Rebuilt only when ids, names, grain, outlines or seams change. The signature is compared
# exactly, not hashed. valid_pieces() is O(n^2) per piece, so validity lives here too.
var _geo_sig := []
var _geo_pieces := []   # per piece: id, name, nw, bad, bad_w, pts, lc, bb, gc, edges
var _geo_seams := []    # per seam (same index as ed.seams): ai, am, bi, bm, col, key


func _ensure_geo() -> void:
	if ed == null or ed.pieces == null:
		return
	var sig := []
	for p in ed.pieces:
		sig.append(String(p["id"]))
		sig.append(String(p.get("name", "")))
		sig.append(float(p.get("grain", 1.5708)))
		for q in p["outline"]:
			sig.append(float(q[0]))
			sig.append(float(q[1]))
		for h in p.get("holes", []):
			sig.append("#hole")
			for q in h:
				sig.append(float(q[0]))
				sig.append(float(q[1]))
	sig.append("#seams")
	for s in ed.seams:
		sig.append(String(s["a"][0]))
		sig.append(int(s["a"][1]))
		sig.append(String(s["b"][0]))
		sig.append(int(s["b"][1]))
		for side in [s["a"], s["b"]]:  # the ranges of parts of edges
			var r := Seams.side_range(side)
			sig.append(r.x)
			sig.append(r.y)
	if sig == _geo_sig:
		return
	_geo_sig = sig
	var fnt := ThemeDB.fallback_font
	var bad: Dictionary = valid_pieces(ed.pieces)["bad"]
	_geo_pieces = []
	var index := {}
	for i in ed.pieces.size():
		var p: Dictionary = ed.pieces[i]
		var o: Array = p["outline"]
		var n := o.size()
		var pts := PackedVector2Array()
		var gc := Vector2.ZERO
		var bx0 := 1e9
		var by0 := 1e9
		var bx1 := -1e9
		var by1 := -1e9
		for q in o:
			var v := Vector2(float(q[0]), float(q[1]))
			pts.append(v)
			gc += v
			bx0 = minf(bx0, v.x)
			by0 = minf(by0, v.y)
			bx1 = maxf(bx1, v.x)
			by1 = maxf(by1, v.y)
		if n > 0:
			gc /= float(n)
		# area centroid for the label anchor, falling back to the vertex average for odd shapes
		var area := 0.0
		var acc := Vector2.ZERO
		for k in n:
			var a: Vector2 = pts[k]
			var b: Vector2 = pts[(k + 1) % n]
			var cr := a.x * b.y - b.x * a.y
			area += cr
			acc += (a + b) * cr
		var lc := gc
		if absf(area) > 1e-9:
			var cand := acc / (3.0 * area)
			if point_in(o, cand.x, cand.y):
				lc = cand
		var edges := []
		for k in n:
			var a2: Vector2 = pts[k]
			var b2: Vector2 = pts[(k + 1) % n]
			var el := a2.distance_to(b2)
			var d := (b2 - a2) / el if el > 0.0 else Vector2.ZERO
			var txt := "%d: %.1f cm" % [k + 1, el * 100.0]
			edges.append({"mid": (a2 + b2) * 0.5, "n": Vector2(d.y, d.x), "txt": txt, "len": el,
				"tw": fnt.get_string_size(txt, HORIZONTAL_ALIGNMENT_LEFT, -1, LABEL_FS).x})
		var pid := String(p["id"])
		var pname := String(p.get("name", p["id"]))
		var why = bad.get(pid, null)
		var bad_w := 0.0
		if why != null:
			bad_w = fnt.get_string_size(String(why), HORIZONTAL_ALIGNMENT_LEFT, -1, LABEL_FS).x
		var hls := []  # holes in piece-local coordinates, for drawing and hit tests
		for h in p.get("holes", []):
			var hp := PackedVector2Array()
			for q in h:
				hp.append(Vector2(float(q[0]), float(q[1])))
			hls.append(hp)
		index[pid] = i
		_geo_pieces.append({"id": pid, "name": pname, "nw": fnt.get_string_size(pname, HORIZONTAL_ALIGNMENT_LEFT, -1, LABEL_FS).x,
			"bad": why, "bad_w": bad_w, "pts": pts, "lc": lc, "gc": gc, "holes": hls,
			"bb": Rect2(Vector2(bx0, by0), Vector2(bx1 - bx0, by1 - by0)), "edges": edges,
			"grain": float(p.get("grain", 1.5708))})
	_geo_seams = []
	for si in ed.seams.size():
		var s: Dictionary = ed.seams[si]
		var ea := _seam_side(s["a"], index)
		var eb := _seam_side(s["b"], index)
		var col := Color(SEAM_COLORS[si % SEAM_COLORS.size()])
		var ra := Seams.side_range(s["a"])
		var rb := Seams.side_range(s["b"])
		var key := "%s:%d:%.6f:%.6f-%s:%d:%.6f:%.6f" % [String(s["a"][0]), int(s["a"][1]), ra.x, ra.y, String(s["b"][0]), int(s["b"][1]), rb.x, rb.y]
		_geo_seams.append({"ai": ea[0], "am": ea[1], "bi": eb[0], "bm": eb[1], "col": col, "key": key})


# [piece index, edge midpoint in local coords], or [-1, zero] when the piece is missing
func _seam_side(side: Array, index: Dictionary) -> Array:
	var pid := String(side[0])
	if not index.has(pid):
		return [-1, Vector2.ZERO]
	var i: int = index[pid]
	var pts: PackedVector2Array = _geo_pieces[i]["pts"]
	var n := pts.size()
	if n == 0:
		return [-1, Vector2.ZERO]
	var e: int = ((int(side[1]) % n) + n) % n
	var r := Seams.side_range(side)  # the seam's end sits in the middle of the side's range
	return [i, pts[e].lerp(pts[(e + 1) % n], (r.x + r.y) * 0.5)]


func _off(p: Dictionary) -> Vector2:
	var t: Dictionary = p.get("table", {"x": 0.0, "y": 0.0})
	return Vector2(float(t.get("x", 0.0)), float(t.get("y", 0.0)))


# screen endpoints of seam k, or [] when one of its pieces is gone
func _seam_ends(k: int) -> Array:
	var s: Dictionary = _geo_seams[k]
	if int(s["ai"]) < 0 or int(s["bi"]) < 0:
		return []
	var pa: Vector2 = s["am"] + _off(ed.pieces[int(s["ai"])])
	var pb: Vector2 = s["bm"] + _off(ed.pieces[int(s["bi"])])
	return [to_screen(pa.x, pa.y), to_screen(pb.x, pb.y)]


# Sewing check for two edges (lengths in metres). The drape eases a difference larger than 5 % of the shorter
# edge or 1 cm, so the sew is flagged then. Returned numbers are in cm (pct in %).
static func sew_check(la: float, lb: float) -> Dictionary:
	var diff_cm := absf(lb - la) * 100.0
	var pct := diff_cm / maxf(minf(la, lb) * 100.0, 1e-9) * 100.0
	return {"la": la * 100.0, "lb": lb * 100.0, "diff": diff_cm, "pct": pct, "warn": diff_cm > 1.0 or pct > 5.0}


static func sew_text(a_name: String, ea: int, la: float, b_name: String, eb: int, lb: float) -> String:
	var c := sew_check(la, lb)
	var s := "%s edge %d %.1f cm ↔ %s edge %d %.1f cm" % [a_name, ea, c["la"], b_name, eb, c["lb"]]
	if c["warn"]:
		s += "  |  Warning: they differ by %.1f cm (%.1f%%), the drape will ease the difference" % [c["diff"], c["pct"]]
	else:
		s += "  |  within 5 % and 1 cm"
	return s


# Symmetric edits. A twin is the mirror image of its piece about the axis fixed when the pair was made
# (sym.axis, in the piece's own metres), shifted by sym.dx so the twin sits beside it. The same map works in
# both directions (reflect, then shift), so editing either piece sets the other. The twin takes the driver's
# outline (reversed corner order to keep it counter-clockwise), grain, table offset and placement (Around mirrored).
# Only while sym.on is set, and only when both pieces have the same corner count.
static func sync_twin(edref, p: Dictionary) -> void:
	var sym = p.get("sym", null)
	if not (sym is Dictionary) or not bool(sym.get("on", false)):
		return
	var tw = piece_by_id(edref.pieces, String(sym.get("twin", "")))
	if tw == null or String(tw["id"]) == String(p["id"]):
		return
	var o: Array = p["outline"]
	var n := o.size()
	if (tw["outline"] as Array).size() != n:
		return
	var ax := float(sym.get("axis", 0.0))
	var dx := float(sym.get("dx", 0.0))
	var out := []
	for k in n:
		var q: Array = o[n - 1 - k]
		out.append([2.0 * ax - float(q[0]) + dx, float(q[1])])
	tw["outline"] = out
	tw["holes"] = mirror_holes(p.get("holes", []), func(x: float) -> float: return 2.0 * ax - x + dx)
	tw["grain"] = PI - float(p.get("grain", 0.0))
	tw["table"] = (p.get("table", {"x": 0.0, "y": 0.0}) as Dictionary).duplicate()
	var w: Dictionary = (p.get("wrap", {}) as Dictionary).duplicate()
	w["theta"] = fposmod(360.0 - float(w.get("theta", 0.0)), 360.0)
	tw["wrap"] = w


func _sew_preview() -> String:
	var hv = _hover_edge()
	if ed.sew_a == null or hv == null:
		return ""
	if String(hv["p"]) == String(ed.sew_a[0]) and int(hv["e"]) == int(ed.sew_a[1]):
		return ""
	var a := _edge_info(String(ed.sew_a[0]), int(ed.sew_a[1]))
	var b := _edge_info(String(hv["p"]), int(hv["e"]))
	if a.is_empty() or b.is_empty():
		return ""
	return sew_text(a["name"], int(ed.sew_a[1]), a["len"], b["name"], int(hv["e"]), b["len"])


# name and length (metres) of one edge, or {} when the piece or edge is missing
func _edge_info(pid: String, e: int) -> Dictionary:
	var p = piece_by_id(ed.pieces, pid)
	if p == null or e < 0 or e >= (p["outline"] as Array).size():
		return {}
	return {"name": String(p.get("name", pid)), "len": edge_len(p["outline"], e)}


static func piece_by_id(pieces: Array, pid: String):
	for p in pieces:
		if String(p["id"]) == pid:
			return p
	return null


static func new_id(pieces: Array, base: String) -> String:
	var i := 1
	while piece_by_id(pieces, base + str(i)) != null:
		i += 1
	return base + str(i)


# ------------------------------------------------------------------ view transform
func to_screen(x: float, y: float) -> Vector2:
	return Vector2(ox + x * cam_z, oy - y * cam_z)


func to_world(sx: float, sy: float) -> Vector2:
	return Vector2((sx - ox) / cam_z, (oy - sy) / cam_z)


# Fit all pieces (with a margin) into the view. Deferred to the next draw: on plan load the pieces may not be
# placed yet, and the control may not have its final size until layout has run.
func fit() -> void:
	_fit_pending = true
	queue_redraw()


func _apply_fit() -> void:
	if size.x < 50.0 or size.y < 50.0:
		return  # keep it pending until the control has its real size
	_fit_pending = false
	if ed == null or ed.pieces.is_empty():
		return
	var x0 := 1e9
	var y0 := 1e9
	var x1 := -1e9
	var y1 := -1e9
	for p in ed.pieces:
		var b := bbox_of(p["outline"])
		var off := _off(p)
		x0 = minf(x0, b[0] + off.x)
		x1 = maxf(x1, b[2] + off.x)
		y0 = minf(y0, b[1] + off.y)
		y1 = maxf(y1, b[3] + off.y)
	var w: float = maxf(0.3, x1 - x0)
	var h: float = maxf(0.3, y1 - y0)
	var avail := Vector2(maxf(40.0, size.x - 150.0), maxf(40.0, size.y - 150.0))
	cam_z = clampf(minf(avail.x / w, avail.y / h), 40.0, 3000.0)
	ox = (size.x - w * cam_z) * 0.5 - x0 * cam_z
	oy = (size.y + h * cam_z) * 0.5 + y0 * cam_z


func snapv(v: float) -> float:
	if ed.snap:
		return round(v / 0.005) * 0.005
	return v


# ------------------------------------------------------------------ hit tests
func point_in(poly: Array, x: float, y: float) -> bool:
	var c := false
	var j: int = poly.size() - 1
	for i in poly.size():
		var a: Array = poly[i]
		var b: Array = poly[j]
		if (float(a[1]) > y) != (float(b[1]) > y) and x < (float(b[0]) - float(a[0])) * (y - float(a[1])) / (float(b[1]) - float(a[1])) + float(a[0]):
			c = not c
		j = i
	return c


func hit_vertex(p: Dictionary, sx: float, sy: float) -> int:
	var t: Dictionary = p.get("table", {"x": 0.0, "y": 0.0})
	for i in (p["outline"] as Array).size():
		var q: Array = p["outline"][i]
		if to_screen(float(q[0]) + float(t.get("x", 0.0)), float(q[1]) + float(t.get("y", 0.0))).distance_to(Vector2(sx, sy)) < 9.0:
			return i
	return -1


func hit_edge(p: Dictionary, sx: float, sy: float, tol: float) -> Dictionary:
	var best := -1
	var bd: float = tol
	var bt := 0.0
	var o: Array = p["outline"]
	var t: Dictionary = p.get("table", {"x": 0.0, "y": 0.0})
	for i in o.size():
		var a: Array = o[i]
		var b: Array = o[(i + 1) % o.size()]
		var pa := to_screen(float(a[0]) + float(t.get("x", 0.0)), float(a[1]) + float(t.get("y", 0.0)))
		var pb := to_screen(float(b[0]) + float(t.get("x", 0.0)), float(b[1]) + float(t.get("y", 0.0)))
		var d := pb - pa
		var l2: float = d.length_squared()
		if l2 < 1e-12:
			l2 = 1.0
		var tt: float = clampf((Vector2(sx, sy) - pa).dot(d) / l2, 0.0, 1.0)
		var dd: float = (pa + d * tt).distance_to(Vector2(sx, sy))
		if dd < bd:
			bd = dd
			best = i
			bt = tt
	if best < 0:
		return {}
	return {"edge": best, "t": bt}


func hit_piece(sx: float, sy: float):
	var w := to_world(sx, sy)
	var ps: Array = ed.pieces
	for i in range(ps.size() - 1, -1, -1):
		var t: Dictionary = (ps[i] as Dictionary).get("table", {"x": 0.0, "y": 0.0})
		var lx: float = w.x - float(t.get("x", 0.0))
		var ly: float = w.y - float(t.get("y", 0.0))
		if point_in(ps[i]["outline"], lx, ly) and not _in_any_hole(ps[i], lx, ly):
			return ps[i]
	return null


# True when the piece-local point (x, y) is inside one of the piece's holes (a click there is not the piece).
static func _in_any_hole(p: Dictionary, x: float, y: float) -> bool:
	for h in p.get("holes", []):
		if poly_contains(h, x, y):
			return true
	return false


# The id of the piece under the pointer (or of the hovered edge's piece in sew mode), or "".
func _hover_pid() -> String:
	var hk: String = ed.selection.hover_kind()
	var hid = ed.selection.hover_id()
	if hk == "piece" and hid != null:
		return String(hid)
	if hk == "edge" and hid is Array and (hid as Array).size() > 0:
		return String(hid[0])
	return ""


# Whether seam k's link line is drawn (and can be clicked): see seam_links.
func link_shown(k: int) -> bool:
	if ed.selection.contains("seam", k) or seam_links == "all":
		return true
	if seam_links == "off":
		return false
	var hp := _hover_pid()
	if hp == "":
		hp = _link_pid  # sticky, so a link can be followed off its piece and clicked
	if hp == "":
		return false
	var sg: Dictionary = _geo_seams[k]
	for key in ["ai", "bi"]:
		var pi := int(sg[key])
		if pi >= 0 and pi < ed.pieces.size() and String(ed.pieces[pi]["id"]) == hp:
			return true
	return false


# The sewn part of one side of seam k, as a screen polyline along its edge ([] when the piece is missing).
func _seam_mark(k: int, side_name: String) -> PackedVector2Array:
	var s: Dictionary = ed.seams[k]
	var sg: Dictionary = _geo_seams[k]
	var pi := int(sg["ai"]) if side_name == "a" else int(sg["bi"])
	if pi < 0:
		return PackedVector2Array()
	var pts: PackedVector2Array = _geo_pieces[pi]["pts"]
	var n := pts.size()
	if n == 0:
		return PackedVector2Array()
	var side: Array = s[side_name]
	var e := ((int(side[1]) % n) + n) % n
	var r := Seams.side_range(side)
	var off := _off(ed.pieces[pi])
	var a := pts[e].lerp(pts[(e + 1) % n], r.x)
	var b := pts[e].lerp(pts[(e + 1) % n], r.y)
	return PackedVector2Array([to_screen(a.x + off.x, a.y + off.y), to_screen(b.x + off.x, b.y + off.y)])


# index into ed.seams of the seam drawn nearest the point (within 7 px), or -1
func hit_seam(sx: float, sy: float) -> int:
	_ensure_geo()
	var pt := Vector2(sx, sy)
	var best := -1
	var bd := 7.0
	for k in _geo_seams.size():
		if not link_shown(k):
			continue
		var seg: Array = _seam_ends(k)
		if seg.is_empty():
			continue
		var a: Vector2 = seg[0]
		var d: Vector2 = seg[1] - a
		var l2: float = maxf(d.length_squared(), 1e-9)
		var t: float = clampf((pt - a).dot(d) / l2, 0.0, 1.0)
		var dd: float = (a + d * t).distance_to(pt)
		if dd < bd:
			bd = dd
			best = k
	return best


# ------------------------------------------------------------------ construction geometry and holes (guides.gd)
func _piece_or_empty(pid: String) -> Dictionary:
	var p = piece_by_id(ed.pieces, pid)
	return p if p is Dictionary else {}


# The construction point (points_only) or line under the screen point: {pid, gid}, or {} when none. Points within 8 px,
# lines within 5 px; the nearest wins.
func hit_guide(sx: float, sy: float, points_only: bool) -> Dictionary:
	var pt := Vector2(sx, sy)
	var best := {}
	var bd := 8.0 if points_only else 5.0
	for p in ed.pieces:
		var off := _off(p)
		for g in Guides.list(p):
			var is_pt := String(g.get("type", "")) == "point"
			if is_pt != points_only:
				continue
			var dd := 1e9
			if is_pt:
				var at = Guides.point_pos(p, g)
				if at == null:
					continue
				dd = to_screen((at as Vector2).x + off.x, (at as Vector2).y + off.y).distance_to(pt)
			else:
				var ln := Guides.line_of(p, g)
				if ln.is_empty():
					continue
				var a: Vector2 = to_screen((ln[0] as Vector2).x + off.x, (ln[0] as Vector2).y + off.y)
				var dir := Vector2((ln[1] as Vector2).x, -(ln[1] as Vector2).y)  # screen y points down
				var rel := pt - a
				dd = absf(rel.x * dir.y - rel.y * dir.x)
			if dd < bd:
				bd = dd
				best = {"pid": String(p["id"]), "gid": String(g["id"])}
	return best


# The hole whose inside contains the screen point: {p, h}, or {} when none.
func hit_hole(sx: float, sy: float) -> Dictionary:
	var w := to_world(sx, sy)
	for i in range(ed.pieces.size() - 1, -1, -1):
		var p: Dictionary = ed.pieces[i]
		var off := _off(p)
		var hs: Array = p.get("holes", [])
		for h in hs.size():
			if poly_contains(hs[h], w.x - off.x, w.y - off.y):
				return {"p": p, "h": h}
	return {}


func pick_guide(pid: String, gid: String) -> void:
	ed.sel = pid
	ed.sel_v = -1
	ed.selection.replace("guide", [pid, gid])
	ed.table_dirty = true
	ed.changed("ui")


func pick_hole(pid: String, h: int) -> void:
	ed.sel = pid
	ed.sel_v = -1
	ed.selection.replace("hole", [pid, h])
	ed.table_dirty = true
	ed.changed("ui")


# Object snap for a world point: the nearest of the corners, edge middles, hole centres, centres of mass and construction
# points within SNAP_PX on screen; else the nearest construction line within SNAP_PX (the point slides along it); else
# the 5 mm grid (when snap is on), measured from `origin` (a piece's table offset, so its own coordinates land on the
# grid). `skip` names what is being dragged, so it does not snap to itself: {pid, corner} or {pid, gid}.
# Sets _snap for the marker drawn at the target.
func snap_point(w: Vector2, skip: Dictionary = {}, origin: Vector2 = Vector2.ZERO, guides_only := false) -> Vector2:
	_snap = {}
	if not ed.snap:
		return w
	var ws := Vector2(to_screen(w.x, w.y))
	var best_d := SNAP_PX
	var best = null
	var best_what := ""
	for t in _snap_targets(skip, guides_only):
		var at: Vector2 = t[0]
		var d := to_screen(at.x, at.y).distance_to(ws)
		if d < best_d:
			best_d = d
			best = at
			best_what = String(t[1])
	if best != null:
		_snap = {"at": best, "what": best_what}
		return best
	# construction lines
	var best_l = null
	best_d = SNAP_PX
	for p in ed.pieces:
		var off := _off(p)
		for g in Guides.list(p):
			if String(g.get("type", "")) != "line":
				continue
			if skip.has("gid") and String(skip["pid"]) == String(p["id"]) and (String(g.get("a", "")) == String(skip["gid"]) or String(g.get("b", "")) == String(skip["gid"])):
				continue
			var ln := Guides.line_of(p, g)
			if ln.is_empty():
				continue
			var a: Vector2 = (ln[0] as Vector2) + off
			var dir: Vector2 = ln[1]
			var proj := a + dir * (w - a).dot(dir)
			var d2 := to_screen(proj.x, proj.y).distance_to(ws)
			if d2 < best_d:
				best_d = d2
				best_l = proj
	if best_l != null:
		_snap = {"at": best_l, "what": "on a construction line"}
		return best_l
	return Vector2(snapv(w.x - origin.x) + origin.x, snapv(w.y - origin.y) + origin.y)


# Snap for a move by a world offset (a hole dragged): the 5 mm grid only.
func snap_delta(dw: Vector2) -> Vector2:
	return Vector2(snapv(dw.x), snapv(dw.y))


# [world point, description] for every object snap target, without what `skip` drags (and what moves with it).
func _snap_targets(skip: Dictionary, guides_only := false) -> Array:
	var out := []
	for p in ed.pieces:
		var pid := String(p["id"])
		var off := _off(p)
		var o: Array = p["outline"]
		var n := o.size()
		var own := skip.has("corner") and String(skip["pid"]) == pid   # a corner of this piece is being dragged
		for i in n:
			if guides_only or (own and i == int(skip["corner"])):
				continue
			out.append([Vector2(float(o[i][0]), float(o[i][1])) + off, "corner %d" % (i + 1)])
			if not own:
				out.append([(Vector2(float(o[i][0]), float(o[i][1])) + Vector2(float(o[(i + 1) % n][0]), float(o[(i + 1) % n][1]))) * 0.5 + off, "middle of edge %d" % (i + 1)])
		if not own and not guides_only:
			out.append([Guides.centroid(p) + off, "centre of mass"])
			var hs: Array = p.get("holes", [])
			for h in hs.size():
				out.append([Guides.ring_centre(hs[h]) + off, "centre of hole %d" % (h + 1)])
		for g in Guides.list(p):
			if String(g.get("type", "")) != "point":
				continue
			if skip.has("gid") and String(skip["pid"]) == pid and String(g["id"]) == String(skip["gid"]):
				continue
			if own and String(g.get("ref", "free")) != "free":
				continue
			var at = Guides.point_pos(p, g)
			if at != null:
				out.append([(at as Vector2) + off, Guides.label(p, g)])
	return out


# Point tool: a click on a piece adds a free construction point there (snapped), or, on a snap target that is a feature
# (a corner, an edge's middle, the centre of mass, a hole's centre), a point that follows that feature.
func _point_down(spos: Vector2) -> void:
	var w := to_world(spos.x, spos.y)
	var tgt := _feature_at(spos)
	var p = tgt.get("p", null)
	if p == null:
		p = hit_piece(spos.x, spos.y)
	if p == null:
		ed.notify("Point: click on a piece (construction points belong to a piece and move with it).")
		return
	ed.push_undo("Add construction point")
	var gid := _guide_for_feature(p, tgt, snap_point(w, {}, _off(p)) - _off(p))
	pick_guide(String(p["id"]), gid)
	ed.changed("table-done")


# Line tool: two clicks on points of one piece (construction points, or corners, edge middles, centre of mass, which get
# a construction point) make a construction line through them.
func _line_down(spos: Vector2) -> void:
	var hg := hit_guide(spos.x, spos.y, true)
	var tgt := {} if not hg.is_empty() else _feature_at(spos)
	var p = null
	if not hg.is_empty():
		p = piece_by_id(ed.pieces, String(hg["pid"]))
	elif tgt.has("p"):
		p = tgt["p"]
	if p == null:
		ed.notify("Line: click a corner, an edge's middle, the centre of mass or a construction point.")
		return
	if not line_a.is_empty() and String(line_a["pid"]) != String(p["id"]):
		ed.notify("Line: both points must be on the same piece.")
		return
	ed.push_undo("Add construction line")
	var gid: String = String(hg["gid"]) if not hg.is_empty() else _guide_for_feature(p, tgt, Vector2.ZERO)
	if line_a.is_empty():
		line_a = {"pid": String(p["id"]), "gid": gid}
		ed.changed("table-done")
		update_hint()
		return
	if gid != String(line_a["gid"]):
		Guides.add_line(p, String(line_a["gid"]), gid)
	line_a = {}
	update_hint()
	ed.changed("table-done")


# The feature of a piece under the screen point (within SNAP_PX): {p, ref, index} for a corner, an edge's middle, a hole's
# centre or the centre of mass; {} when none.
func _feature_at(spos: Vector2) -> Dictionary:
	var best := {}
	var bd := SNAP_PX
	for p in ed.pieces:
		var off := _off(p)
		var o: Array = p["outline"]
		var n := o.size()
		var cands := [["centroid", -1, Guides.centroid(p)]]
		for i in n:
			var a := Vector2(float(o[i][0]), float(o[i][1]))
			var b := Vector2(float(o[(i + 1) % n][0]), float(o[(i + 1) % n][1]))
			cands.append(["corner", i, a])
			cands.append(["mid", i, (a + b) * 0.5])
		var hs: Array = p.get("holes", [])
		for h in hs.size():
			cands.append(["hole", h, Guides.ring_centre(hs[h])])
		for c in cands:
			var at: Vector2 = (c[2] as Vector2) + off
			var d := to_screen(at.x, at.y).distance_to(spos)
			if d < bd:
				bd = d
				best = {"p": p, "ref": String(c[0]), "index": int(c[1])}
	return best


# The construction point for a feature (an existing one when the piece has it), or a new free point at `local`.
func _guide_for_feature(p: Dictionary, tgt: Dictionary, local: Vector2) -> String:
	if tgt.has("ref"):
		return Guides.add_ref(p, String(tgt["ref"]), int(tgt["index"]))
	return Guides.add_free(p, local)


# Deletes the picked construction geometry, holes and seams. Returns true when something was picked to delete.
func delete_picked() -> bool:
	var gs: Array = ed.selection.items("guide")
	var hs: Array = ed.selection.items("hole")
	var ss: Array = ed.selection.items("seam")
	if gs.is_empty() and hs.is_empty() and ss.is_empty():
		return false
	ed.push_undo("Delete")
	var n := 0
	for id in gs:
		var pg: Dictionary = _piece_or_empty(String(id[0]))
		if not pg.is_empty():
			n += Guides.remove(pg, String(id[1]))
	# holes from the last index down, so earlier indices stay valid
	hs.sort_custom(func(a, b) -> bool: return int(a[1]) > int(b[1]))
	for id in hs:
		var ph: Dictionary = _piece_or_empty(String(id[0]))
		if not ph.is_empty() and int(id[1]) < (ph.get("holes", []) as Array).size():
			Guides.on_remove_hole(ph, int(id[1]))
			(ph["holes"] as Array).remove_at(int(id[1]))
			ed._sym_sync(ph)
			n += 1
	var sidx: Array = ss.map(func(x) -> int: return int(x))
	sidx.sort()
	sidx.reverse()
	for k in sidx:
		if k < ed.seams.size():
			ed.seams.remove_at(k)
			n += 1
	ed.selection.clear_kind("guide")
	ed.selection.clear_kind("hole")
	ed.selection.clear_kind("seam")
	ed.notify("Deleted %d item(s). Ctrl+Z restores them." % n)
	ed.changed("structure" if not (hs.is_empty() and ss.is_empty()) else "table-done")
	return true


# ------------------------------------------------------------------ context menu (right click)
var _menu: PopupMenu
var _menu_actions: Array = []


func _menu_item(text: String, action: Callable, disabled := false) -> void:
	_menu.add_item(text, _menu_actions.size())
	_menu.set_item_disabled(_menu.get_item_count() - 1, disabled)
	_menu_actions.append(action)


func _menu_title(text: String) -> void:
	_menu.add_separator(text)


# What can be done with the thing under the pointer: a construction point or line, a corner, an edge, a hole, a piece,
# a seam link, or the empty table.
func context_menu(spos: Vector2) -> void:
	if _menu == null:
		_menu = PopupMenu.new()
		_menu.name = "TableMenu"
		_menu.id_pressed.connect(func(i: int) -> void:
			if i >= 0 and i < _menu_actions.size():
				(_menu_actions[i] as Callable).call())
		add_child(_menu)
	_menu.clear()
	_menu_actions = []
	_build_menu(spos)
	if _menu.get_item_count() == 0:
		return
	_menu.reset_size()
	_menu.position = Vector2i(get_screen_transform() * spos)
	_menu.popup()


func _build_menu(spos: Vector2) -> void:
	var sx := spos.x
	var sy := spos.y
	var w := to_world(sx, sy)
	var locked: bool = ed.active_locked()
	var hg := hit_guide(sx, sy, true)
	if hg.is_empty():
		hg = hit_guide(sx, sy, false)
	if not hg.is_empty():
		var p: Dictionary = _piece_or_empty(String(hg["pid"]))
		var g: Dictionary = Guides.find(p, String(hg["gid"]))
		pick_guide(String(hg["pid"]), String(hg["gid"]))
		_menu_title(Guides.label(p, g))
		if String(g.get("type", "")) == "point":
			var at: Vector2 = Guides.point_pos(p, g)
			_menu_item("Cut a hole centred here...", func() -> void: ed._open_hole_dialog(p, at), locked)
			_menu_item("Line across through here", func() -> void: _guide_edit(p, "Add construction line", func() -> void: Guides.add_axis(p, String(g["id"]), 0.0)), locked)
			_menu_item("Line up through here", func() -> void: _guide_edit(p, "Add construction line", func() -> void: Guides.add_axis(p, String(g["id"]), PI * 0.5)), locked)
			_menu_item("Line from here to... (click the second point)", func() -> void:
				set_tool("line")
				line_a = {"pid": String(p["id"]), "gid": String(g["id"])}
				update_hint(), locked)
			if String(g.get("ref", "free")) != "free":
				_menu_item("Make it a free point", func() -> void: _guide_edit(p, "Free construction point", func() -> void: Guides.freeze(p, g)), locked)
		_menu_item("Delete", func() -> void: delete_picked(), locked)
		return
	# a corner of any piece (corners are drawn as handles on the selected piece only, but can be used anywhere)
	for p in ed.pieces:
		var vi := hit_vertex(p, sx, sy)
		if vi < 0:
			continue
		var pid := String(p["id"])
		ed.sel = pid
		ed.sel_v = vi
		_menu_title("%s, corner %d" % [String(p.get("name", pid)), vi + 1])
		_menu_item("Construction point on this corner", func() -> void: _guide_edit(p, "Add construction point", func() -> void: pick_guide_later(pid, Guides.add_ref(p, "corner", vi))), locked)
		_menu_item("Delete corner", func() -> void:
			if (p["outline"] as Array).size() <= 3:
				ed.notify("A piece needs at least three corners.")
				return
			ed.push_undo("Delete corner")
			var dropped := remove_vertex(p, vi, ed.seams)
			ed.sel_v = -1
			if dropped > 0:
				ed.notify("Deleted the corner and %d seam(s) on it. Ctrl+Z restores them." % dropped)
			ed.changed("structure"), locked)
		return
	# an edge
	for p in ed.pieces:
		var he := hit_edge(p, sx, sy, 8.0)
		if he.is_empty():
			continue
		var pid := String(p["id"])
		var e := int(he["edge"])
		var t := float(he["t"])
		ed.sel = pid
		ed.selection.replace("edge", [pid, e])
		var o: Array = p["outline"]
		var len_cm := edge_len(o, e) * 100.0
		_menu_title("%s, edge %d (%.1f cm)" % [String(p.get("name", pid)), e + 1, len_cm])
		_menu_item("Add a corner here", func() -> void: split_edge_at(p, e, t), locked)
		_menu_item("Add a corner in the middle", func() -> void:
			ed.push_undo("Split edge %d in the middle" % (e + 1))
			var a: Array = o[e]
			var b: Array = o[(e + 1) % o.size()]
			insert_vertex(ed, p, e, [(float(a[0]) + float(b[0])) * 0.5, (float(a[1]) + float(b[1])) * 0.5])
			ed.sel = pid
			ed.sel_v = e + 1
			ed.changed("structure"), locked)
		_menu_item("Construction point in the middle", func() -> void: _guide_edit(p, "Add construction point", func() -> void: pick_guide_later(pid, Guides.add_ref(p, "mid", e))), locked)
		_menu_item("Sew this edge to... (click the other edge)", func() -> void:
			set_tool("sew")
			ed.sew_a = [pid, e]
			update_hint()
			ed.table_dirty = true, locked)
		var sewn := []
		for k in ed.seams.size():
			for side in ["a", "b"]:
				if String(ed.seams[k][side][0]) == pid and int(ed.seams[k][side][1]) == e:
					sewn.append(k)
		if not sewn.is_empty():
			_menu_item("Unsew this edge (%d seam%s)" % [sewn.size(), "" if sewn.size() == 1 else "s"], func() -> void:
				ed.push_undo("Unsew edge %d" % (e + 1))
				sewn.sort()
				sewn.reverse()
				for k in sewn:
					ed.seams.remove_at(int(k))
				ed.changed("structure"), locked)
		return
	# a hole
	var hh := hit_hole(sx, sy)
	if not hh.is_empty():
		var p: Dictionary = hh["p"]
		var h := int(hh["h"])
		var pid := String(p["id"])
		pick_hole(pid, h)
		var hb := bbox_of(p["holes"][h])
		_menu_title("Hole %d (%.1f x %.1f cm)" % [h + 1, (hb[2] - hb[0]) * 100.0, (hb[3] - hb[1]) * 100.0])
		_menu_item("Construction point at its centre", func() -> void: _guide_edit(p, "Add construction point", func() -> void: pick_guide_later(pid, Guides.add_ref(p, "hole", h))), locked)
		_menu_item("Delete hole", func() -> void: delete_picked(), locked)
		return
	# a piece
	var hp: Variant = hit_piece(sx, sy)
	if hp != null:
		var p: Dictionary = hp
		var pid := String(p["id"])
		if not ed.selection.contains("piece", pid):
			ed.sel = pid
		var local := w - _off(p)
		_menu_title(String(p.get("name", pid)))
		_menu_item("Construction point at the centre of mass", func() -> void: _guide_edit(p, "Add construction point", func() -> void: pick_guide_later(pid, Guides.add_ref(p, "centroid"))), locked)
		_menu_item("Construction point here", func() -> void: _guide_edit(p, "Add construction point", func() -> void: pick_guide_later(pid, Guides.add_free(p, snap_point(w, {}, _off(p)) - _off(p)))), locked)
		_menu_item("Cut a hole here...", func() -> void: ed._open_hole_dialog(p, local), locked)
		_menu_item("Duplicate", func() -> void:
			ed.push_undo("Duplicate piece")
			duplicate_piece(ed, p)
			ed.changed("structure"), locked)
		_menu_item("Mirror left-right", func() -> void:
			ed.push_undo("Mirror piece")
			mirror_piece(p, ed.seams)
			ed.changed("structure"), locked)
		_menu_item("Delete piece", func() -> void:
			ed.push_undo("Delete piece")
			var gone := remove_piece(ed, pid)
			ed.notify("Deleted %s%s. Ctrl+Z restores it." % [String(p.get("name", pid)), (" and its %d seam(s)" % gone) if gone > 0 else ""])
			ed.changed("structure"), locked)
		return
	# a seam link
	var ks := hit_seam(sx, sy)
	if ks >= 0:
		ed.selection.replace("seam", ks)
		_menu_title("Seam %d" % (ks + 1))
		_menu_item("Flip its direction", func() -> void:
			ed.push_undo("Flip seam direction")
			ed.seams[ks]["reverse"] = not bool(ed.seams[ks].get("reverse", false))
			ed.changed("structure"), locked)
		_menu_item("Delete seam", func() -> void: delete_picked(), locked)
		return
	_menu_title("Table")
	_menu_item("Fit all pieces in view", func() -> void: fit())
	_menu_item("Construction point tool", func() -> void: set_tool("point"))
	_menu_item("Construction line tool", func() -> void: set_tool("line"))


# Runs a construction-geometry edit as one undoable action (no simulator rebuild: guides are not simulated).
func _guide_edit(p: Dictionary, name: String, f: Callable) -> void:
	ed.push_undo(name)
	f.call()
	ed.table_dirty = true
	ed.changed("table-done")


func pick_guide_later(pid: String, gid: String) -> void:
	ed.selection.replace("guide", [pid, gid])


# ------------------------------------------------------------------ seam range handles
# The ends of each side's range are handles on the edge: a small square in the seam's colour. Dragging one moves that end
# along its edge (seam_end drag, see _drag_seam_end). Picked seams draw bigger handles.
func _handle_points(k: int) -> Array:
	var out := []
	var s: Dictionary = ed.seams[k]
	var sg: Dictionary = _geo_seams[k]
	for side_name in ["a", "b"]:
		var side: Array = s[side_name]
		var pi := int(sg["ai"]) if side_name == "a" else int(sg["bi"])
		if pi < 0 or Seams.is_full(side):  # a whole edge has no handles: its ends are corners
			continue
		var pts: PackedVector2Array = _geo_pieces[pi]["pts"]
		var n := pts.size()
		var e := ((int(side[1]) % n) + n) % n
		var r := Seams.side_range(side)
		var off := _off(ed.pieces[pi])
		for end in [0, 1]:
			var t := r.x if end == 0 else r.y
			var lp := pts[e].lerp(pts[(e + 1) % n], t)
			out.append({"k": k, "side": side_name, "end": end, "screen": to_screen(lp.x + off.x, lp.y + off.y), "pi": pi})
	return out


func _draw_range_handles(k: int, c: Color, picked: bool) -> void:
	var sz := 4.0 if picked else 3.0
	for h in _handle_points(k):
		var p: Vector2 = h["screen"]
		draw_rect(Rect2(p - Vector2(sz, sz), Vector2(sz * 2.0, sz * 2.0)), c, true)


# The seam range handle under the pointer (within 8 px), or {} when none.
func _handle_at(spos: Vector2) -> Dictionary:
	_ensure_geo()
	var best := {}
	var bd := 8.0
	for k in _geo_seams.size():
		for h in _handle_points(k):
			var d: float = (h["screen"] as Vector2).distance_to(spos)
			if d < bd:
				bd = d
				best = h
	return best


# Moves one end of a side's range to the pointer's place on the edge (snapped to 5 mm when snap is on). The range keeps
# at least 8 mm and does not overlap another seam on its edge; a move that would do either is not applied.
func _drag_seam_end(d: Dictionary, spos: Vector2) -> void:
	var k: int = int(d["k"])
	if k < 0 or k >= ed.seams.size():
		return
	var s: Dictionary = ed.seams[k]
	var side: Array = s[String(d["side"])]
	var pid := String(side[0])
	var e := int(side[1])
	var pi := int(_geo_seams[k]["ai"]) if String(d["side"]) == "a" else int(_geo_seams[k]["bi"])
	if pi < 0:
		return
	var pts: PackedVector2Array = _geo_pieces[pi]["pts"]
	var n := pts.size()
	var a := pts[e]
	var b := pts[(e + 1) % n]
	var ab := b - a
	var len := ab.length()
	if len <= 0.0:
		return
	var w := to_world(spos.x, spos.y) - _off(ed.pieces[pi])
	var t := clampf((w - a).dot(ab) / (len * len), 0.0, 1.0)
	if ed.snap:
		t = clampf(snappedf(t * len, 0.005) / len, 0.0, 1.0)
	var r := Seams.side_range(side)
	var t0 := r.x
	var t1 := r.y
	var min_t := 0.008 / len
	if int(d["end"]) == 0:
		t0 = clampf(t, 0.0, t1 - min_t)
	else:
		t1 = clampf(t, t0 + min_t, 1.0)
	if is_equal_approx(t0, r.x) and is_equal_approx(t1, r.y):
		return
	if Seams.overlaps(ed.seams, pid, e, t0, t1, k):
		return
	s[String(d["side"])] = Seams.make_side(pid, e, t0, t1)
	ed.table_dirty = true


# ------------------------------------------------------------------ tools
func set_tool(t: String) -> void:
	ed.tool = t
	ed.sew_a = null
	_sew_note = ""
	measure_pts = []  # a measurement belongs to the Measure tool that made it
	line_a = {}
	update_hint()
	ed.table_dirty = true


# Bottom hint line. In sew mode it says what the next click does; once the first edge is picked, the hovered
# candidate's two lengths (and a warning when they differ by more than 5 % or 1 cm) show before the click.
func update_hint() -> void:
	if hint_label == null:
		return
	if ed.tool == "sew":
		var text := "Click the first edge to sew (Esc cancels)"
		if ed.sew_a != null:
			text = "Now click the edge to sew it to (Esc cancels)"
			var pv := _sew_preview()
			if pv != "":
				text += "  |  " + pv
				ed.status_label.text = pv  # both lengths and the warning, in the status line as well
		elif _sew_note != "":
			text = _sew_note + "  |  " + text
		hint_label.text = text
	elif ed.tool == "hole":
		hint_label.text = "Hole: press inside a piece and drag. " + ("A circle, centred on the press point." if ed.hole_shape == "circle" else "A rectangle, corner to corner.") + " Esc cancels."
	elif ed.tool == "split":
		hint_label.text = "Split: press on an edge to add a corner there (a seam on that edge is split with it). Right-click an edge for more."
	elif ed.tool == "rotate":
		hint_label.text = "Rotate: press on a piece and drag around the centre point. Shift snaps to 15 degrees. The Piece tab turns by a typed angle."
	elif ed.tool == "measure":
		hint_label.text = "Measure: click two points. The distance shows in cm. Esc clears."
	elif ed.tool == "point":
		hint_label.text = "Point: click on a piece to add a construction point. It snaps to corners, edge middles, the centre of mass and other construction geometry."
	elif ed.tool == "line":
		hint_label.text = ("Line: now click the second point (Esc cancels)." if not line_a.is_empty() else "Line: click two points of a piece (corners, edge middles, construction points) for a construction line through them.")
	else:
		hint_label.text = "Drag pieces, corners, construction points and holes. Right-click anything for what you can do with it."


# ------------------------------------------------------------------ mouse
func _gui_input(e: InputEvent) -> void:
	if ed == null or ed.pieces == null:
		return
	if e is InputEventMouseButton:
		var b := e as InputEventMouseButton
		if b.pressed and b.button_index == MOUSE_BUTTON_WHEEL_UP:
			_zoom_at(b.position, 1.0 / 0.92)
			accept_event()
		elif b.pressed and b.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_zoom_at(b.position, 1.0 / 1.08)
			accept_event()
		elif b.pressed and b.double_click and b.button_index == MOUSE_BUTTON_LEFT:
			_dbl(b.position)
			accept_event()
		elif b.pressed:
			_down(b.position, b.button_index)
			accept_event()
		elif not b.pressed and b.button_index == MOUSE_BUTTON_LEFT:
			_up()
			accept_event()
		elif not b.pressed and b.button_index == MOUSE_BUTTON_RIGHT:
			# a right press that did not pan is a click: the context menu of what is under the pointer
			var d = drag
			drag = null
			if d != null and String(d.get("kind", "")) == "pan" and Vector2(float(d["sx"]), float(d["sy"])).distance_to(b.position) < 4.0:
				context_menu(b.position)
			accept_event()
	elif e is InputEventMouseMotion:
		var m := e as InputEventMouseMotion
		_move(m.position)
		accept_event()


func _zoom_at(spos: Vector2, f: float) -> void:
	var w := to_world(spos.x, spos.y)
	cam_z = clampf(cam_z * f, 40.0, 3000.0)
	ox = spos.x - w.x * cam_z
	oy = spos.y + w.y * cam_z
	ed.table_dirty = true


func _down(spos: Vector2, button: int) -> void:
	var sx := spos.x
	var sy := spos.y
	# pan is on the mouse buttons only (Space is play/pause, see _unhandled_key_input)
	if button == MOUSE_BUTTON_MIDDLE or button == MOUSE_BUTTON_RIGHT:
		drag = {"kind": "pan", "sx": sx, "sy": sy, "ox": ox, "oy": oy}
		return
	if ed.active_locked():
		# a locked garment can be looked at, not edited: a click selects its piece, nothing else
		var hl: Variant = hit_piece(sx, sy)
		if hl != null:
			ed.sel = (hl as Dictionary)["id"]
			ed.sel_v = -1
			ed.changed("ui")
		else:
			drag = {"kind": "pan", "sx": sx, "sy": sy, "ox": ox, "oy": oy}
		return
	if ed.tool == "hole":
		_hole_down(spos)
		return
	if ed.tool == "split":
		_split_down(spos)
		return
	if ed.tool == "rotate":
		_rotate_down(spos)
		return
	if ed.tool == "measure":
		_measure_down(spos)
		return
	if ed.tool == "point":
		_point_down(spos)
		return
	if ed.tool == "line":
		_line_down(spos)
		return
	if ed.tool == "sew":
		var best: Variant = null
		for p in ed.pieces:
			var h := hit_edge(p, sx, sy, 12.0)
			if not h.is_empty():
				best = {"p": p, "e": h["edge"]}
		if best == null:
			ed.sew_a = null
			update_hint()
			ed.table_dirty = true
			return
		if ed.sew_a == null:
			ed.sew_a = [(best["p"] as Dictionary)["id"], best["e"]]
			ed.sel = (best["p"] as Dictionary)["id"]
			_sew_note = ""
			update_hint()
			ed.changed("ui")
			return
		if ed.sew_a[0] == (best["p"] as Dictionary)["id"] and ed.sew_a[1] == best["e"]:
			ed.sew_a = null
			update_hint()
			ed.changed("ui")
			return
		ed.push_undo("Sew edges")
		var bside := [(best["p"] as Dictionary)["id"], best["e"]]
		# warning text is computed before the sew so it shows alongside the removed-seam note
		var ea := _edge_info(String(ed.sew_a[0]), int(ed.sew_a[1]))
		var eb := _edge_info(String(bside[0]), int(bside[1]))
		_sew_note = sew_text(ea["name"], int(ed.sew_a[1]), ea["len"], eb["name"], int(bside[1]), eb["len"])
		var replaced := sew_edges(ed, ed.sew_a, bside)
		ed.sew_a = null
		update_hint()
		var msg := _sew_note  # lengths and warning stay visible next to the removed-seam note
		if replaced > 0:
			msg += "  |  Sewing replaced %d seam(s) on those edges. Ctrl+Z restores them." % replaced
		ed.notify(msg)
		ed.changed("structure")
		return
	var sel: Variant = null
	if ed.sel != null:
		sel = piece_by_id(ed.pieces, ed.sel)
	if sel != null:
		var vi := hit_vertex(sel, sx, sy)
		if vi >= 0:
			ed.push_undo("Move corner")
			ed.sel_v = vi
			drag = {"kind": "vertex", "p": sel, "vi": vi}
			ed.changed("ui")
			return
	# a construction point (any piece): pick it, and drag it when it moves
	var hgp := hit_guide(sx, sy, true)
	if not hgp.is_empty():
		pick_guide(String(hgp["pid"]), String(hgp["gid"]))
		drag = {"kind": "guide", "pid": String(hgp["pid"]), "gid": String(hgp["gid"]), "moved": false}
		return
	# a handle at the end of a part of an edge (after the corners, so a corner still wins)
	var hh := _handle_at(spos)
	if not hh.is_empty():
		ed.push_undo("Move seam end")
		ed.selection.replace("seam", int(hh["k"]))
		drag = {"kind": "seam_end", "k": int(hh["k"]), "side": String(hh["side"]), "end": int(hh["end"])}
		ed.table_dirty = true
		return
	# a construction line, then a hole (a press inside a hole is not on the piece)
	var hgl := hit_guide(sx, sy, false)
	if not hgl.is_empty():
		pick_guide(String(hgl["pid"]), String(hgl["gid"]))
		return
	var hho := hit_hole(sx, sy)
	if not hho.is_empty():
		var hpc: Dictionary = hho["p"]
		pick_hole(String(hpc["id"]), int(hho["h"]))
		ed.push_undo("Move hole %d" % (int(hho["h"]) + 1))
		drag = {"kind": "hole_move", "p": hpc, "h": int(hho["h"]), "w0": to_world(sx, sy),
			"orig": ((hpc["holes"] as Array)[int(hho["h"])] as Array).duplicate(true), "moved": false}
		return
	var hp: Variant = hit_piece(sx, sy)
	if hp != null:
		hp = hp as Dictionary
		ed.push_undo("Move piece")
		ed.sel = hp["id"]
		ed.sel_v = -1
		ed.selection.clear_kind("seam")
		var w := to_world(sx, sy)
		var t: Dictionary = hp.get("table", {"x": 0.0, "y": 0.0})
		drag = {"kind": "piece", "p": hp, "dx": float(t.get("x", 0.0)) - w.x, "dy": float(t.get("y", 0.0)) - w.y, "moved": false}
		ed.changed("ui")
	else:
		var ks := hit_seam(sx, sy)
		if ks >= 0:
			ed.selection.replace("seam", ks)
			ed.table_dirty = true
			return
		ed.selection.clear()
		drag = {"kind": "pan", "sx": sx, "sy": sy, "ox": ox, "oy": oy}
		ed.changed("ui")


static func _same_side(a: Array, b: Array) -> bool:
	return String(a[0]) == String(b[0]) and int(a[1]) == int(b[1])


func _move(spos: Vector2) -> void:
	var sx := spos.x
	var sy := spos.y
	var d = drag
	_pointer = to_world(sx, sy)
	if ed.tool == "line" and not line_a.is_empty():
		ed.table_dirty = true  # the rubber band follows the pointer
	if measure_pts.size() == 1:
		ed.table_dirty = true  # the measure line follows the pointer until the second click
	if d == null:
		var hk := ""
		var hid = null
		if ed.tool == "sew":
			for p in ed.pieces:
				var he := hit_edge(p, sx, sy, 12.0)
				if not he.is_empty():
					hk = "edge"
					hid = [String(p["id"]), int(he["edge"])]
		else:
			var hp: Variant = hit_piece(sx, sy)
			if hp != null:
				hk = "piece"
				hid = String((hp as Dictionary)["id"])
		ed.selection.set_hover(hk, hid)  # the selection emits hover_changed only when the item changed
		return
	if String(d["kind"]) == "pan":
		ox = float(d["ox"]) + sx - float(d["sx"])
		oy = float(d["oy"]) + sy - float(d["sy"])
		ed.table_dirty = true
	elif String(d["kind"]) == "piece":
		var w := to_world(sx, sy)
		var p: Dictionary = d["p"]
		if not (p.get("table") is Dictionary):
			p["table"] = {"x": 0.0, "y": 0.0}
		(p["table"] as Dictionary)["x"] = snapv(w.x + float(d["dx"]))
		(p["table"] as Dictionary)["y"] = snapv(w.y + float(d["dy"]))
		sync_twin(ed, p)
		d["moved"] = true
		ed.table_dirty = true
		ed.changed("table")
	elif String(d["kind"]) == "vertex":
		var p2: Dictionary = d["p"]
		var t2 := _off(p2)
		var w2 := snap_point(to_world(sx, sy), {"pid": String(p2["id"]), "corner": int(d["vi"])}, t2) - t2
		(p2["outline"] as Array)[int(d["vi"])] = [w2.x, w2.y]
		sync_twin(ed, p2)
		ed.table_dirty = true
		ed.changed("structure-drag")
	elif String(d["kind"]) == "guide":
		var pg = piece_by_id(ed.pieces, String(d["pid"]))
		var g = Guides.find(pg, String(d["gid"])) if pg != null else null
		if g == null:
			return
		if not bool(d["moved"]):
			ed.push_undo("Move construction point")
			if String(g.get("ref", "free")) != "free":
				Guides.freeze(pg, g)  # dragged off its feature: it is a free point from now on
				ed.notify("The point no longer follows the piece's shape: it is a free point now. Ctrl+Z undoes.")
			d["moved"] = true
		var wg := snap_point(to_world(sx, sy), {"pid": String(d["pid"]), "gid": String(d["gid"])}, _off(pg)) - _off(pg)
		g["x"] = wg.x
		g["y"] = wg.y
		ed.table_dirty = true
		queue_redraw()
	elif String(d["kind"]) == "hole_move":
		var pm: Dictionary = d["p"]
		var dw := snap_delta(to_world(sx, sy) - (d["w0"] as Vector2))
		var moved := []
		for q in d["orig"]:
			moved.append([float(q[0]) + dw.x, float(q[1]) + dw.y])
		(pm["holes"] as Array)[int(d["h"])] = moved
		d["moved"] = dw.length() > 0.0
		ed.table_dirty = true
		queue_redraw()
	elif String(d["kind"]) == "hole":
		var th: Vector2 = _off(d["p"])
		var wh := snap_point(to_world(sx, sy), {}, th, true)
		d["b"] = Vector2(wh.x - th.x, wh.y - th.y)
		ed.table_dirty = true
	elif String(d["kind"]) == "seam_end":
		_drag_seam_end(d, spos)
	elif String(d["kind"]) == "rotate":
		var wr := to_world(sx, sy)
		var ang: float = (wr - d["centre"]).angle() - float(d["a0"])
		if Input.is_key_pressed(KEY_SHIFT):
			ang = snappedf(ang, deg_to_rad(15.0))
		d["angle"] = ang
		_apply_rotate(d)
		ed.table_dirty = true


# Split tool: a press on an edge adds a corner there (snapped when snap is on). Seams on that edge follow (insert_points),
# as when double-clicking an edge.
func _split_down(spos: Vector2) -> void:
	var best: Variant = null
	for p in ed.pieces:
		var h := hit_edge(p, spos.x, spos.y, 12.0)
		if not h.is_empty():
			best = {"p": p, "e": h["edge"], "t": h["t"]}
	if best == null:
		ed.notify("Split: press on an edge to add a corner there.")
		return
	split_edge_at(best["p"], int(best["e"]), float(best["t"]))


# Adds a corner on edge e of p at fraction t along it. Returns the number of seams dropped (0: they follow the split).
func split_edge_at(p: Dictionary, e: int, t: float) -> int:
	var o: Array = p["outline"]
	var a: Array = o[e]
	var b: Array = o[(e + 1) % o.size()]
	var pt := [snapv(float(a[0]) + (float(b[0]) - float(a[0])) * t), snapv(float(a[1]) + (float(b[1]) - float(a[1])) * t)]
	ed.push_undo("Split edge %d" % (e + 1))
	var dropped := insert_vertex(ed, p, e, pt)
	ed.sel = String(p["id"])
	ed.sel_v = e + 1
	ed.changed("structure")
	return dropped


# Rotate tool: a press on a piece turns it (and the other selected pieces) about the centre of the group. Shift snaps the
# turn to 15 degrees. The rotation handle is the centre point; the line from it shows the angle.
func _rotate_down(spos: Vector2) -> void:
	var hit: Variant = hit_piece(spos.x, spos.y)
	if hit == null:
		ed.notify("Rotate: press on a piece and drag around it (hold Shift to snap to 15 degrees).")
		return
	var hp: Dictionary = hit
	if not ed.selection.contains("piece", String(hp["id"])):
		ed.sel = String(hp["id"])
	var group := _group_pieces()
	var centre := Geom.group_centre(group)
	var w := to_world(spos.x, spos.y)
	var orig := {}
	for p in group:
		orig[String(p["id"])] = (p as Dictionary).duplicate(true)
	ed.push_undo("Rotate %s" % ("pieces" if group.size() > 1 else "piece"))
	drag = {"kind": "rotate", "centre": centre, "a0": (w - centre).angle(), "orig": orig, "angle": 0.0}
	ed.table_dirty = true


# The pieces of the selection (or the primary piece), as the editor's dictionaries.
func _group_pieces() -> Array:
	var out := []
	for id in ed.selection.items("piece"):
		var q = piece_by_id(ed.pieces, String(id))
		if q != null:
			out.append(q)
	return out


# Puts every rotating piece back to its state at the press, then turns it by the drag's angle about the centre.
func _apply_rotate(d: Dictionary) -> void:
	var group := []
	for id in d["orig"]:
		var q = piece_by_id(ed.pieces, String(id))
		if q == null:
			continue
		var src: Dictionary = (d["orig"][id] as Dictionary).duplicate(true)
		q.merge(src, true)
		group.append(q)
	Geom.rotate_pieces(group, float(d["angle"]), d["centre"])
	for q in group:
		sync_twin(ed, q)


# Measure tool: two clicks give the distance between them in cm, across, and up. A third click starts again.
func _measure_down(spos: Vector2) -> void:
	var w := to_world(spos.x, spos.y)
	var pt := Vector2(snapv(w.x), snapv(w.y))
	if measure_pts.size() != 1:
		measure_pts = [pt]
		ed.notify("Measure: click the second point.")
	else:
		measure_pts.append(pt)
		var d: Vector2 = measure_pts[1] - measure_pts[0]
		ed.notify("Measured %.1f cm: %.1f cm across, %.1f cm up (Esc clears)" % [d.length() * 100.0, absf(d.x) * 100.0, absf(d.y) * 100.0])
	ed.table_dirty = true


# Hole tool: a press inside a piece starts a hole there (its centre for a circle, a corner for a rectangle).
func _hole_down(spos: Vector2) -> void:
	var hit: Variant = hit_piece(spos.x, spos.y)
	if hit == null:
		ed.notify("Hole: press inside a piece, then drag to size the hole.")
		return
	var hp: Dictionary = hit
	var t: Vector2 = _off(hp)
	var w := snap_point(to_world(spos.x, spos.y), {}, t, true)  # a hole can start on a construction point (a poncho's centre)
	var start := Vector2(w.x - t.x, w.y - t.y)
	drag = {"kind": "hole", "p": hp, "a": start, "b": start}
	ed.sel = hp["id"]
	ed.sel_v = -1
	ed.table_dirty = true


# The hole a Hole-tool drag describes, in the piece's own metres: a circle centred on the press point with its edge
# at the pointer, or a rectangle with the press point and the pointer as opposite corners.
func _drag_hole(d: Dictionary) -> Array:
	var a: Vector2 = d["a"]
	var b: Vector2 = d["b"]
	if ed.hole_shape == "circle":
		return circle_hole(a.x, a.y, 2.0 * a.distance_to(b))
	return rect_hole(minf(a.x, b.x), minf(a.y, b.y), absf(b.x - a.x), absf(b.y - a.y))


# Adds the drawn hole when it is big enough (2 cm) and fits in the piece; otherwise says why.
func _finish_hole(d: Dictionary) -> void:
	var a: Vector2 = d["a"]
	var b: Vector2 = d["b"]
	var big := 2.0 * a.distance_to(b) >= 0.02 if ed.hole_shape == "circle" else (absf(b.x - a.x) >= 0.02 and absf(b.y - a.y) >= 0.02)
	if not big:
		ed.notify("A hole needs at least 2 cm in each direction.")
		return
	var p: Dictionary = d["p"]
	var hole := _drag_hole(d)
	if not hole_fits(p, hole):
		ed.notify("That hole does not fit: it must lie inside the piece and clear of its other holes.")
		return
	ed.push_undo("Add %s hole" % ("circle" if ed.hole_shape == "circle" else "rectangle"))
	var holes: Array = p.get("holes", [])
	holes.append(hole)
	p["holes"] = holes
	ed._sym_sync(p)
	ed.changed("structure")


func _up() -> void:
	var d = drag
	drag = null
	if d != null and String(d.get("kind", "")) == "vertex":
		ed.changed("structure")  # a click that moved nothing records nothing (the pattern is unchanged)
	elif d != null and String(d.get("kind", "")) == "piece" and bool(d.get("moved", false)):
		ed.changed("table-done")
	elif d != null and String(d.get("kind", "")) == "piece":
		ed.discard_pending()  # a click on a piece without a move is not an action
	elif d != null and String(d.get("kind", "")) == "hole":
		_finish_hole(d)
	elif d != null and String(d.get("kind", "")) == "guide":
		if bool(d["moved"]):
			ed.changed("table-done")  # construction geometry is not simulated: record, no rebuild
		_snap = {}
	elif d != null and String(d.get("kind", "")) == "hole_move":
		var pm: Dictionary = d["p"]
		if not bool(d["moved"]):
			ed.discard_pending()
		else:
			var hs: Array = pm["holes"]
			var mine: Array = hs[int(d["h"])]
			hs[int(d["h"])] = d["orig"]
			var others := hs.duplicate()
			others.remove_at(int(d["h"]))
			if holes_ok(pm["outline"], others + [mine]):
				hs[int(d["h"])] = mine
				ed._sym_sync(pm)
				ed.changed("structure")
			else:
				ed.discard_pending()
				ed.notify("The hole must stay inside the piece and clear of its other holes: it went back.")
				ed.table_dirty = true
		_snap = {}
	elif d != null and String(d.get("kind", "")) == "seam_end":
		ed.changed("structure")
	elif d != null and String(d.get("kind", "")) == "rotate":
		ed.changed("structure")  # a turn that moved nothing records nothing (the pattern is unchanged)


func _dbl(spos: Vector2) -> void:
	if ed.tool != "select" or ed.active_locked():
		return
	var p: Variant = null
	if ed.sel != null:
		p = piece_by_id(ed.pieces, ed.sel)
	if p == null:
		return
	var h := hit_edge(p, spos.x, spos.y, 10.0)
	if h.is_empty():
		return
	var o: Array = p["outline"]
	var a: Array = o[int(h["edge"])]
	var b: Array = o[(int(h["edge"]) + 1) % o.size()]
	ed.push_undo("Add corner")
	var dropped := insert_vertex(ed, p, int(h["edge"]), [snapv(float(a[0]) + (float(b[0]) - float(a[0])) * float(h["t"])), snapv(float(a[1]) + (float(b[1]) - float(a[1])) * float(h["t"]))])
	ed.sel_v = int(h["edge"]) + 1
	ed.changed("structure")


# ------------------------------------------------------------------ keys
func _unhandled_key_input(e: InputEvent) -> void:
	var k := e as InputEventKey
	if k == null or not k.pressed or k.echo:
		return
	var fo := get_viewport().gui_get_focus_owner()
	if fo != null and (fo is LineEdit or fo is TextEdit):
		return
	if k.keycode == KEY_SPACE:
		ed._on_play()  # Space is play/pause only
		return
	if (k.ctrl_pressed or k.meta_pressed) and k.keycode == KEY_Z:
		if k.shift_pressed:
			ed.do_redo()
		else:
			ed.do_undo()
		accept_event()
		return
	if (k.ctrl_pressed or k.meta_pressed) and k.keycode == KEY_Y:
		ed.do_redo()
		accept_event()
		return
	if (k.ctrl_pressed or k.meta_pressed) and k.keycode == KEY_C:
		ed.copy_piece()
		accept_event()
		return
	if (k.ctrl_pressed or k.meta_pressed) and k.keycode == KEY_V:
		ed.paste_piece()
		accept_event()
		return
	if k.keycode == KEY_ESCAPE:
		if drag != null and String(drag.get("kind", "")) == "hole":
			drag = null  # Esc cancels a hole being drawn
		measure_pts = []
		line_a = {}
		ed.sew_a = null
		ed.selection.clear_kind("seam")
		ed.selection.clear_kind("guide")
		ed.selection.clear_kind("hole")
		update_hint()
		ed.changed("ui")
	elif k.keycode == KEY_V:
		set_tool("select")
	elif k.keycode == KEY_S and not k.ctrl_pressed and not k.meta_pressed:
		set_tool("sew")
	elif k.keycode == KEY_F:
		fit()
	elif (k.keycode == KEY_DELETE or k.keycode == KEY_BACKSPACE) and not ed.active_locked():
		if delete_picked():  # picked construction geometry, holes or seams go first
			return
		var p = null
		if ed.sel != null:
			p = piece_by_id(ed.pieces, ed.sel)
		if p == null:
			return
		var del_corner: bool = ed.sel_v >= 0 and (p["outline"] as Array).size() > 3
		ed.push_undo("Delete corner" if del_corner else "Delete piece")
		var pname := String(p.get("name", p["id"]))
		if del_corner:
			var dropped := remove_vertex(p, ed.sel_v, ed.seams)
			ed.sel_v = -1
			if dropped > 0:
				ed.notify("Deleted the corner and %d seam(s) on it. Ctrl+Z restores them." % dropped)
		else:
			var gone := remove_piece(ed, p["id"])
			ed.notify("Deleted %s%s. Ctrl+Z restores it." % [pname, (" and its %d seam(s)" % gone) if gone > 0 else ""])
		ed.changed("structure")


# ------------------------------------------------------------------ piece edits (state.js)
# Splits edge e of p at pt (the new corner is e + 1). Seams on the split edge cannot follow either half, so they are
# dropped. Returns how many were dropped (the caller warns and the edit is undoable).
static func insert_vertex(edref, p: Dictionary, e: int, pt: Array) -> int:
	return insert_points(edref, p, e, [pt])


# Inserts the points pts after corner e of p (they become corners e+1 .. e+k). Seams on the later edges move on by k.
# A seam side on edge e follows the new corners: each point is placed at its fraction along the old edge (its projection
# onto it), and a side's range is re-expressed on the sub-edges it covers. A side that spans a new corner is cut there
# into one seam per sub-edge, and its partner's range is cut at the same fraction of the seam (from the partner's other
# end when the seam is reversed), so every stitch stays where it was. Returns how many seams were dropped: 0 (kept for
# callers that report it).
static func insert_points(edref, p: Dictionary, e: int, pts: Array) -> int:
	var k := pts.size()
	var ol: Array = p["outline"]
	var n := ol.size()
	var pa := Vector2(float(ol[e][0]), float(ol[e][1]))
	var pb := Vector2(float(ol[(e + 1) % n][0]), float(ol[(e + 1) % n][1]))
	var cuts := [0.0]   # the sub-edge boundaries along the old edge, as fractions
	var dl := pb - pa
	for q in pts:
		var t := 0.5
		if dl.length_squared() > 1e-18:
			t = clampf((Vector2(float(q[0]), float(q[1])) - pa).dot(dl) / dl.length_squared(), 1e-6, 1.0 - 1e-6)
		cuts.append(maxf(t, float(cuts[cuts.size() - 1]) + 1e-6))
	cuts.append(1.0)
	Guides.on_insert(p, e, k)
	for i in k:
		ol.insert(e + 1 + i, pts[i])
	var pid := String(p["id"])
	# 1. later edges move on by k (sides on edge e keep index e for step 2)
	for s in edref.seams:
		for side in ["a", "b"]:
			if String(s[side][0]) == pid and int(s[side][1]) > e:
				s[side][1] = int(s[side][1]) + k
	# 2. sides on edge e: one seam per sub-edge the side covers (side a first, then side b of each result)
	var out := []
	for s in edref.seams:
		for s1 in _split_side(s, "a", pid, e, cuts):
			out.append_array(_split_side(s1, "b", pid, e, cuts))
	edref.seams = out
	return 0


# Seam s with side `which` on edge e of piece pid re-expressed on the sub-edges between the fractions `cuts` (sub-edge j is
# edge e + j). Returns [s] unchanged when that side is on another edge.
static func _split_side(s: Dictionary, which: String, pid: String, e: int, cuts: Array) -> Array:
	var side: Array = s[which]
	if String(side[0]) != pid or int(side[1]) != e:
		return [s]
	var other := "b" if which == "a" else "a"
	var r := Seams.side_range(side)
	var span := maxf(r.y - r.x, 1e-12)
	var orng := Seams.side_range(s[other])
	var rev := bool(s.get("reverse", false))
	var res := []
	for j in cuts.size() - 1:
		var c0: float = cuts[j]
		var c1: float = cuts[j + 1]
		var lo := maxf(r.x, c0)
		var hi := minf(r.y, c1)
		if hi - lo <= 1e-6:
			continue
		var u0 := (lo - r.x) / span   # this part's place along the seam, 0..1 from side `which`'s range start
		var u1 := (hi - r.x) / span
		var v0 := 1.0 - u1 if rev else u0
		var v1 := 1.0 - u0 if rev else u1
		var ns: Dictionary = s.duplicate(true)
		ns[which] = Seams.make_side(pid, e + j, (lo - c0) / (c1 - c0), (hi - c0) / (c1 - c0))
		var op: Array = s[other]
		ns[other] = Seams.make_side(String(op[0]), int(op[1]), orng.x + v0 * (orng.y - orng.x), orng.x + v1 * (orng.y - orng.x))
		res.append(ns)
	return res


# Removes corner i of p with the seams on its two edges. Returns the number of seams dropped, or -1 when p has
# three corners or fewer (nothing is removed).
static func remove_vertex(p: Dictionary, i: int, seams: Array) -> int:
	var n: int = (p["outline"] as Array).size()
	if n <= 3:
		return -1
	var prev: int = (i - 1 + n) % n
	var dropped := 0
	for idx in range(seams.size() - 1, -1, -1):
		var s: Dictionary = seams[idx]
		var drop := false
		for side in ["a", "b"]:
			if String(s[side][0]) == String(p["id"]):
				var ee := int(s[side][1])
				if ee == prev or ee == i:
					drop = true
		if drop:
			seams.remove_at(idx)
			dropped += 1
	for s in seams:
		for side in ["a", "b"]:
			if String(s[side][0]) == String(p["id"]) and int(s[side][1]) > i:
				s[side][1] = int(s[side][1]) - 1
	Guides.on_remove(p, i)
	(p["outline"] as Array).remove_at(i)
	return dropped


# Removes piece pid and every seam on it. Returns the number of seams removed.
static func remove_piece(edref, pid: String) -> int:
	var before: int = (edref.seams as Array).size()
	edref.pieces = (edref.pieces as Array).filter(func(p) -> bool: return String(p["id"]) != pid)
	edref.seams = (edref.seams as Array).filter(func(s) -> bool: return String(s["a"][0]) != pid and String(s["b"][0]) != pid)
	for q in edref.pieces:  # a twin left without its piece is no longer linked
		var sy = q.get("sym", null)
		if sy is Dictionary and String(sy.get("twin", "")) == pid:
			q.erase("sym")
	if edref.sel == pid:
		edref.sel = null
		edref.sel_v = -1
	return before - (edref.seams as Array).size()


# Joins edge a to edge b (each [piece id, edge index]). Seams already on either edge are replaced. Returns how many.
static func sew_edges(edref, a: Array, b: Array) -> int:
	var before: int = (edref.seams as Array).size()
	edref.seams = (edref.seams as Array).filter(func(s) -> bool:
		return not (_same_side(s["a"], a) or _same_side(s["b"], a) or _same_side(s["a"], b) or _same_side(s["b"], b)))
	var replaced := before - (edref.seams as Array).size()
	edref.seams.append({"a": a, "b": b, "reverse": true})
	return replaced


static func add_piece(edref, kind: String) -> Dictionary:
	var pid := new_id(edref.pieces, "p")
	var outlines := {"rect": [[0.0, 0.0], [0.4, 0.0], [0.4, 0.5], [0.0, 0.5]],
		"trap": [[-0.2, 0.0], [0.2, 0.0], [0.1, 0.5], [-0.1, 0.5]], "tri": [[0.0, 0.0], [0.4, 0.0], [0.2, 0.45]]}
	var maxx := -1e9
	for p in edref.pieces:
		var b := bbox_of(p["outline"])
		maxx = maxf(maxx, b[2] + float((p.get("table", {}) as Dictionary).get("x", 0.0)))
	var ol: Array = outlines.get(kind, outlines["rect"])
	var p := {"id": pid, "name": "Piece %d" % (edref.pieces.size() + 1),
		"outline": ol.map(func(q: Array) -> Array: return [float(q[0]), float(q[1])]), "grain": 1.5708,
		"table": {"x": ((maxx + 0.12) if edref.pieces.size() > 0 else 0.0) + (0.2 if kind == "trap" else 0.0), "y": 0.0},
		"wrap": {"theta": 0.0, "y": 0.5, "clear": 0.012, "ring": 0.0}}
	normalize_in_place(p)
	(edref.pieces as Array).append(p)
	edref.sel = pid
	return p


# Adds a shape from shapes.gd (its outline and holes in metres) with its bounding box centred on `at` (table metres).
# The shape may carry a "wrap" dictionary for its placement on the body; otherwise it starts at the front, mid height.
static func add_shape(edref, label: String, shape: Dictionary, at: Vector2) -> Dictionary:
	var pid := new_id(edref.pieces, "p")
	var ol: Array = (shape["outline"] as Array).duplicate(true)
	var b := bbox_of(ol)
	var wrap: Dictionary = shape.get("wrap", {"theta": 0.0, "y": 0.6, "clear": 0.012, "ring": 0.0})
	var p := {"id": pid, "name": "%s %d" % [label, edref.pieces.size() + 1], "outline": ol, "grain": 1.5708,
		"table": {"x": at.x - (b[0] + b[2]) * 0.5, "y": at.y - (b[1] + b[3]) * 0.5},
		"wrap": (wrap as Dictionary).duplicate(true), "holes": (shape.get("holes", []) as Array).duplicate(true)}
	normalize_in_place(p)
	(edref.pieces as Array).append(p)
	edref.sel = pid
	return p


# A circle (n points) or a rectangle as a hole, in the piece's own metres.
static func circle_hole(cx: float, cy: float, d: float, n: int = 24) -> Array:
	var out := []
	for i in n:
		var t := TAU * float(i) / float(n)
		out.append([cx + d * 0.5 * cos(t), cy + d * 0.5 * sin(t)])
	return out


static func rect_hole(x0: float, y0: float, w: float, h: float) -> Array:
	return [[x0, y0], [x0 + w, y0], [x0 + w, y0 + h], [x0, y0 + h]]


# Whether `hole` can be added to p: inside the outline, clear of the other holes (see holes_ok).
static func hole_fits(p: Dictionary, hole: Array) -> bool:
	var hs: Array = (p.get("holes", []) as Array).duplicate()
	hs.append(hole)
	return holes_ok(p["outline"], hs)


static func normalize_in_place(p: Dictionary) -> void:
	if signed_area(p["outline"]) < 0.0:
		Guides.freeze_all(p)  # derived guides name corners and edges by index, which the reversal renumbers
		(p["outline"] as Array).reverse()
	p["grain"] = float(p.get("grain", 1.5708))
	if not (p.get("table") is Dictionary):
		p["table"] = {"x": 0.0, "y": 0.0}
	if not (p.get("wrap") is Dictionary):
		p["wrap"] = {"theta": 0.0, "y": 0.5, "clear": 0.012}
	p["name"] = String(p.get("name", p.get("id", "?")))


static func mirror_piece(p: Dictionary, seams: Array) -> void:
	var n: int = (p["outline"] as Array).size()
	var b := bbox_of(p["outline"])
	var cx: float = (b[0] + b[2]) * 0.5
	Guides.on_mirror(p, func(x: float) -> float: return 2.0 * cx - x)
	var o: Array = []
	for q in p["outline"]:
		o.append([2.0 * cx - float(q[0]), float(q[1])])
	o.reverse()
	p["outline"] = o
	if p.has("holes"):
		p["holes"] = mirror_holes(p["holes"], func(x: float) -> float: return 2.0 * cx - x)
	for s in seams:
		for side in ["a", "b"]:
			if String(s[side][0]) == String(p["id"]):
				s[side] = Seams.reversed_side(s[side], n)
				s["reverse"] = not bool(s.get("reverse", false))
	p["grain"] = PI - float(p.get("grain", 0.0))
	p["wrap"]["theta"] = fposmod(360.0 - float((p.get("wrap", {}) as Dictionary).get("theta", 0.0)), 360.0)


# Holes mirrored with the same x map as the outline. A hole's own corner order does not matter for the mesher.
static func mirror_holes(holes: Array, map_x: Callable) -> Array:
	var out := []
	for h in holes:
		var hh := []
		for q in h:
			hh.append([map_x.call(float(q[0])), float(q[1])])
		out.append(hh)
	return out


static func duplicate_piece(edref, p: Dictionary) -> Dictionary:
	var q: Dictionary = JSON.parse_string(JSON.stringify(p))
	q["id"] = new_id(edref.pieces, "p")
	q["name"] = String(p.get("name", "?")) + " copy"
	q.erase("sym")  # a copy is not the twin of its original
	var b := bbox_of(p["outline"])
	(q["table"] as Dictionary)["x"] = float((q["table"] as Dictionary).get("x", 0.0)) + (b[2] - b[0]) + 0.1
	(edref.pieces as Array).append(q)
	edref.sel = q["id"]
	return q


# ------------------------------------------------------------------ drawing
const LABEL_FS := 10


func _draw() -> void:
	if ed == null or ed.pieces == null:
		return
	if _fit_pending:
		_apply_fit()
	_ensure_geo()
	var font := ThemeDB.fallback_font
	# grid: 10 cm lines, 50 cm heavier
	var w0 := to_world(0.0, 0.0)
	var w1 := to_world(size.x, size.y)
	var step: float = 0.1 if cam_z > 120.0 else 0.5
	var x: float = floor(w0.x / step) * step
	while x <= w1.x:
		var heavy: bool = absf(fmod(x, 0.5)) < 1e-6 or absf(fmod(x, 0.5) - 0.5) < 1e-6
		draw_line(Vector2(to_screen(x, 0.0).x, 0.0), Vector2(to_screen(x, 0.0).x, size.y), Color("#232a35") if heavy else Color("#1a1f27"))
		x += step
	var y: float = floor(w1.y / step) * step
	while y <= w0.y:
		var heavy2: bool = absf(fmod(y, 0.5)) < 1e-6 or absf(fmod(y, 0.5) - 0.5) < 1e-6
		var sy := to_screen(0.0, y).y
		draw_line(Vector2(0.0, sy), Vector2(size.x, sy), Color("#232a35") if heavy2 else Color("#1a1f27"))
		y += step
	var fcol := Color(FABRIC_COLORS.get(ed.fabric_id, "#9fb7d6"))
	# pieces: fill, outline, fabric tint, grain arrow, corner handles
	for i in ed.pieces.size():
		var p: Dictionary = ed.pieces[i]
		var g: Dictionary = _geo_pieces[i]
		var off := _off(p)
		var is_sel: bool = ed.sel == String(p["id"])
		var bad = g["bad"]
		var lpts: PackedVector2Array = g["pts"]
		var pts := PackedVector2Array()
		for q in lpts:
			pts.append(to_screen(q.x + off.x, q.y + off.y))
		var fill := Color("#5a2328cc") if bad != null else (Color("#ece6d6") if is_sel else Color("#d9d1bd"))
		fill.a *= 0.93
		draw_colored_polygon(pts, fill)
		var poly := PackedVector2Array(pts)
		if pts.size() > 0:
			poly.append(pts[0])
		var hot: bool = ed.health_hot.has(String(p["id"]))  # a garment check flagged this piece (editor.gd)
		draw_polyline(poly, Color("#ff3b3b") if hot else (Color("#ff6b6b") if bad != null else (Color("#ff9d4d") if is_sel else Color("#6e674f"))), 4.0 if hot else (2.5 if is_sel else 1.6))
		draw_colored_polygon(pts, Color(fcol, 0.22))
		# holes: drawn over the fill in the table's dark colour, outlined, so the cloth reads as open there
		for hp in g["holes"]:
			if hp.size() < 3:
				continue
			var hpts := PackedVector2Array()
			for q in hp:
				hpts.append(to_screen(q.x + off.x, q.y + off.y))
			draw_colored_polygon(hpts, Color("#11151b"))
			var hpoly := PackedVector2Array(hpts)
			hpoly.append(hpts[0])
			draw_polyline(hpoly, Color("#ff9d4d") if is_sel else Color("#6e674f"), 1.6)
		# grain arrow at the vertex centroid
		var gc: Vector2 = g["gc"]
		var gang: float = float(g["grain"])
		var gx := cos(gang)
		var gy := sin(gang)
		var a0 := to_screen(gc.x + off.x - gx * 0.07, gc.y + off.y - gy * 0.07)
		var a1 := to_screen(gc.x + off.x + gx * 0.07, gc.y + off.y + gy * 0.07)
		draw_line(a0, a1, Color("#2b2b2b"), 1.5)
		var ang := (a1 - a0).angle()
		draw_colored_polygon(PackedVector2Array([a1, a1 - Vector2(cos(ang - 0.4), sin(ang - 0.4)) * 8.0, a1 - Vector2(cos(ang + 0.4), sin(ang + 0.4)) * 8.0]), Color("#2b2b2b"))
		if is_sel:
			for vi in pts.size():
				var hp := pts[vi]
				draw_rect(Rect2(hp - Vector2(5, 5), Vector2(10, 10)), Color("#ff9d4d") if ed.sel_v == vi else Color("#14171c"), true)
				draw_rect(Rect2(hp - Vector2(5, 5), Vector2(10, 10)), Color("#ff9d4d"), false, 2.0)
	# seams: the sewn part of each edge in the seam's colour; then, for the seams whose link is shown (see seam_links),
	# one thin semi-transparent line between the two edge midpoints, a dot at each end. The picked seam is heavier.
	for k in _geo_seams.size():
		var sg0: Dictionary = _geo_seams[k]
		var pk: bool = ed.selection.contains("seam", k)
		for side_name in ["a", "b"]:
			var mk := _seam_mark(k, side_name)
			if mk.size() == 2:
				draw_line(mk[0], mk[1], Color(sg0["col"], 0.95 if pk else 0.8), 4.0 if pk else 3.0)
	for k in _geo_seams.size():
		if not link_shown(k):
			continue
		var seg := _seam_ends(k)
		if seg.is_empty():
			continue
		var sg: Dictionary = _geo_seams[k]
		var picked: bool = ed.selection.contains("seam", k)
		var c := Color(sg["col"], 0.95 if picked else 0.5)
		draw_line(seg[0], seg[1], c, 2.5 if picked else 1.0)
		var r := 3.0 if picked else 2.0
		draw_circle(seg[0], r, c)
		draw_circle(seg[1], r, c)
		_draw_range_handles(k, c, picked)
	_draw_guides()
	# sew mode: the first edge picked (orange) and the edge under the pointer (teal), so the pair is visible
	# before the second click
	if ed.tool == "sew":
		if ed.sew_a != null:
			_draw_edge_hl(String(ed.sew_a[0]), int(ed.sew_a[1]), Color("#ff9d4d"))
		var hv = _hover_edge()
		if hv != null:
			var same: bool = ed.sew_a != null and String(ed.sew_a[0]) == String(hv["p"]) and int(ed.sew_a[1]) == int(hv["e"])
			if not same:
				_draw_edge_hl(String(hv["p"]), int(hv["e"]), Color("#3fd3c0"))
	# labels. Each one claims its screen rectangle; a label that would overlap an earlier one is skipped, and
	# piece names come first so they win. Names sit at the piece's area centroid and are hidden when they do
	# not fit inside the piece's on-screen bounding box.
	var fs := LABEL_FS
	var asc := font.get_ascent(fs)
	var lh := font.get_height(fs)
	var view := Rect2(Vector2.ZERO, size)
	var taken := []
	var order := []
	for i in ed.pieces.size():
		if ed.sel == String(ed.pieces[i]["id"]):
			order.push_front(i)
		else:
			order.append(i)
	for i in order:
		var p: Dictionary = ed.pieces[i]
		var g: Dictionary = _geo_pieces[i]
		var off := _off(p)
		var lc: Vector2 = g["lc"]
		var sc := to_screen(lc.x + off.x, lc.y + off.y)
		var bb: Rect2 = g["bb"]
		var top_left := to_screen(bb.position.x + off.x, bb.end.y + off.y)
		var bot_right := to_screen(bb.end.x + off.x, bb.position.y + off.y)
		var nw: float = g["nw"]
		var screen_w := bot_right.x - top_left.x
		var screen_h := bot_right.y - top_left.y
		if nw + 8.0 <= screen_w and lh + 4.0 <= screen_h:
			var nrect := Rect2(sc - Vector2(nw, lh) * 0.5, Vector2(nw, lh))
			if view.intersects(nrect) and _claim(taken, nrect):
				draw_string(font, Vector2(nrect.position.x, nrect.position.y + asc), String(g["name"]), HORIZONTAL_ALIGNMENT_LEFT, -1, fs, Color("#3a3426"))
		var why = g["bad"]
		if why != null:
			var bw: float = g["bad_w"]
			var brect := Rect2(Vector2(sc.x - bw * 0.5, sc.y + lh * 0.5 + 2.0), Vector2(bw, lh))
			if view.intersects(brect) and _claim(taken, brect):
				draw_string(font, Vector2(brect.position.x, brect.position.y + asc), String(why), HORIZONTAL_ALIGNMENT_LEFT, -1, fs, Color("#ffd0d0"))
	# edge length labels: selected piece, sew mode, or zoomed in; each only where its edge is at least about as long on
	# screen as its label (short edges of a curve would bury the pieces in numbers). The hovered edge always gets one.
	var hov_edge = ed.selection.hover_id() if ed.selection.hover_kind() == "edge" else null
	for i in ed.pieces.size():
		if not show_lengths:
			break
		var p: Dictionary = ed.pieces[i]
		var is_sel: bool = ed.sel == String(p["id"])
		if not (is_sel or ed.tool == "sew" or cam_z > 200.0):
			continue
		var g: Dictionary = _geo_pieces[i]
		var off := _off(p)
		for ei in (g["edges"] as Array).size():
			var e: Dictionary = g["edges"][ei]
			var hovered: bool = hov_edge is Array and String(hov_edge[0]) == String(p["id"]) and int(hov_edge[1]) == ei
			if not hovered and float(e["len"]) * cam_z < float(e["tw"]) * 0.8:
				continue
			var m: Vector2 = e["mid"]
			var n: Vector2 = e["n"]
			var pp := to_screen(m.x + off.x, m.y + off.y) + n * 15.0
			var tw: float = e["tw"]
			var erect := Rect2(Vector2(pp.x, pp.y - asc), Vector2(tw, lh))
			if view.intersects(erect) and _claim(taken, erect):
				draw_string(font, pp, String(e["txt"]), HORIZONTAL_ALIGNMENT_LEFT, -1, fs, Color("#ffb877") if is_sel else Color("#8f98a6"))
	# the rotation pivot while a Rotate drag runs, and the measure line (from the first point to the second or the pointer)
	if drag != null and String(drag.get("kind", "")) == "rotate":
		var pc := to_screen(float(drag["centre"].x), float(drag["centre"].y))
		draw_circle(pc, 5.0, Color("#ff9d4d"))
		draw_arc(pc, 28.0, 0.0, TAU, 40, Color("#ff9d4d"), 1.5)
	if ed.tool == "measure" and measure_pts.size() >= 1:
		var ma := to_screen(float(measure_pts[0].x), float(measure_pts[0].y))
		var mb_w: Vector2 = measure_pts[1] if measure_pts.size() == 2 else _pointer
		var mb := to_screen(mb_w.x, mb_w.y)
		draw_line(ma, mb, Color("#89ddff"), 2.0)
		draw_circle(ma, 4.0, Color("#89ddff"))
		draw_circle(mb, 4.0, Color("#89ddff"))
		var md: Vector2 = mb_w - measure_pts[0]
		draw_string(font, (ma + mb) * 0.5 + Vector2(6, -6), "%.1f cm" % (md.length() * 100.0), HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color("#89ddff"))
	# the hole a Hole-tool drag is drawing, in its piece's place
	if drag != null and String(drag.get("kind", "")) == "hole":
		var hpre: Array = _drag_hole(drag)
		var ht: Vector2 = _off(drag["p"])
		var hpts := PackedVector2Array()
		for q in hpre:
			hpts.append(to_screen(float(q[0]) + ht.x, float(q[1]) + ht.y))
		hpts.append(hpts[0])
		draw_polyline(hpts, Color("#ff9d4d"), 2.0)
	# scale bar
	var bx := 14.0
	var by := size.y - 18.0
	var blen := 0.1 * cam_z
	draw_line(Vector2(bx, by), Vector2(bx + blen, by), Color("#8f98a6"), 2.0)
	draw_string(font, Vector2(bx + blen + 6.0, by), "10 cm", HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("#8f98a6"))


const GUIDE_COL := Color("#89ddff")
const GUIDE_SEL := Color("#ff9d4d")


# Construction lines (dashed, across the view), construction points (a ring with a cross; the centre of mass a filled
# ring), the picked hole's outline, and the object-snap marker while a drag snaps.
func _draw_guides() -> void:
	var view := Rect2(Vector2.ZERO, size)
	var reach := size.length()
	for p in ed.pieces:
		var off := _off(p)
		for g in Guides.list(p):
			var picked: bool = ed.selection.contains("guide", [String(p["id"]), String(g["id"])])
			var col := GUIDE_SEL if picked else Color(GUIDE_COL, 0.75)
			if String(g.get("type", "")) == "line":
				var ln := Guides.line_of(p, g)
				if ln.is_empty():
					continue
				var a := to_screen((ln[0] as Vector2).x + off.x, (ln[0] as Vector2).y + off.y)
				var dir := Vector2((ln[1] as Vector2).x, -(ln[1] as Vector2).y)
				draw_dashed_line(a - dir * reach * 2.0, a + dir * reach * 2.0, col, 2.0 if picked else 1.0, 8.0)
		for g in Guides.list(p):
			if String(g.get("type", "")) != "point":
				continue
			var at = Guides.point_pos(p, g)
			if at == null:
				continue
			var sp := to_screen((at as Vector2).x + off.x, (at as Vector2).y + off.y)
			if not view.grow(10.0).has_point(sp):
				continue
			var picked2: bool = ed.selection.contains("guide", [String(p["id"]), String(g["id"])])
			var c2 := GUIDE_SEL if picked2 else GUIDE_COL
			var r := 6.0 if picked2 else 5.0
			draw_arc(sp, r, 0.0, TAU, 20, c2, 1.5)
			draw_line(sp - Vector2(r + 3.0, 0), sp + Vector2(r + 3.0, 0), c2, 1.0)
			draw_line(sp - Vector2(0, r + 3.0), sp + Vector2(0, r + 3.0), c2, 1.0)
			if String(g.get("ref", "free")) == "centroid":
				draw_circle(sp, 2.5, c2)
	for id in ed.selection.items("hole"):
		var p: Dictionary = _piece_or_empty(String(id[0]))
		if p.is_empty() or int(id[1]) >= (p.get("holes", []) as Array).size():
			continue
		var off := _off(p)
		var pts := PackedVector2Array()
		for q in (p["holes"] as Array)[int(id[1])]:
			pts.append(to_screen(float(q[0]) + off.x, float(q[1]) + off.y))
		if pts.size() > 1:
			pts.append(pts[0])
			draw_polyline(pts, GUIDE_SEL, 2.5)
	if not _snap.is_empty() and drag != null:
		var at2: Vector2 = _snap["at"]
		var s2 := to_screen(at2.x, at2.y)
		draw_rect(Rect2(s2 - Vector2(6, 6), Vector2(12, 12)), Color("#ffe36b"), false, 1.5)
		draw_string(ThemeDB.fallback_font, s2 + Vector2(9, -8), String(_snap["what"]), HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("#ffe36b"))
	if ed.tool == "line" and not line_a.is_empty():
		var pa = piece_by_id(ed.pieces, String(line_a["pid"]))
		var ga = Guides.find(pa, String(line_a["gid"])) if pa != null else null
		var at3 = Guides.point_pos(pa, ga) if ga != null else null
		if at3 != null:
			var s3 := to_screen((at3 as Vector2).x + _off(pa).x, (at3 as Vector2).y + _off(pa).y)
			draw_dashed_line(s3, to_screen(_pointer.x, _pointer.y), GUIDE_SEL, 1.5, 6.0)


func _draw_edge_hl(pid: String, e: int, col: Color) -> void:
	for i in ed.pieces.size():
		var p: Dictionary = ed.pieces[i]
		if String(p["id"]) != pid:
			continue
		var pts: PackedVector2Array = _geo_pieces[i]["pts"]
		if e < 0 or e >= pts.size():
			return
		var off := _off(p)
		var a := to_screen(pts[e].x + off.x, pts[e].y + off.y)
		var q: Vector2 = pts[(e + 1) % pts.size()]
		var b := to_screen(q.x + off.x, q.y + off.y)
		draw_line(a, b, col, 5.0)
		draw_circle(a, 4.0, col)
		draw_circle(b, 4.0, col)
		return


static func _claim(taken: Array, r: Rect2) -> bool:
	for t in taken:
		if (t as Rect2).intersects(r):
			return false
	taken.append(r)
	return true
