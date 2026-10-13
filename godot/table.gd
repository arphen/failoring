# Cutting table: port of app/src/editor2d.js plus the piece-geometry edits of app/src/state.js.
# Pan/zoom canvas, select/move pieces, drag corners, add/delete corners, sew edges, snap,
# undo/redo. State (pieces, seams, selection, undo) lives in the editor; this Control draws
# it and translates mouse and keys into edits, then calls ed.changed(kind).
extends Control

const SEAM_COLORS := ["#3fd3c0", "#ff9d4d", "#c792ea", "#82aaff", "#f07178", "#c3e88d", "#ffcb6b", "#89ddff"]
const FABRIC_COLORS := {"cotton_poplin": "#6f93c4", "denim_12oz": "#3d5c8c", "linen_medium": "#c2b184",
	"wool_suiting": "#566175", "silk_charmeuse": "#a8405c", "polyester_chiffon": "#dc94ac",
	"canvas_heavy": "#a89a76", "cotton_jersey": "#6f9a88", "lambskin_leather": "#7a4630"}

var ed = null  # set by the editor: the Editor node holding pieces/seams/selection/undo
var cam_z := 260.0
var ox := 0.0
var oy := 0.0
var drag = null
var hover = null
var fitted := false  # kept for the editor, which resets it on load; fit() is what matters now
var hint_label: Label
var _fit_pending := false   # fit() is deferred to the next draw, once the control has a real size
var _sel_seam_key := ""     # "a:e-b:e" of the seam picked on the table, "" when none
var _sew_note := ""         # what the last sew said (lengths, warning), shown in the hint line until the next click


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
	sig.append("#seams")
	for s in ed.seams:
		sig.append(String(s["a"][0]))
		sig.append(int(s["a"][1]))
		sig.append(String(s["b"][0]))
		sig.append(int(s["b"][1]))
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
			edges.append({"mid": (a2 + b2) * 0.5, "n": Vector2(d.y, d.x), "txt": txt,
				"tw": fnt.get_string_size(txt, HORIZONTAL_ALIGNMENT_LEFT, -1, LABEL_FS).x})
		var pid := String(p["id"])
		var pname := String(p.get("name", p["id"]))
		var why = bad.get(pid, null)
		var bad_w := 0.0
		if why != null:
			bad_w = fnt.get_string_size(String(why), HORIZONTAL_ALIGNMENT_LEFT, -1, LABEL_FS).x
		index[pid] = i
		_geo_pieces.append({"id": pid, "name": pname, "nw": fnt.get_string_size(pname, HORIZONTAL_ALIGNMENT_LEFT, -1, LABEL_FS).x,
			"bad": why, "bad_w": bad_w, "pts": pts, "lc": lc, "gc": gc,
			"bb": Rect2(Vector2(bx0, by0), Vector2(bx1 - bx0, by1 - by0)), "edges": edges,
			"grain": float(p.get("grain", 1.5708))})
	_geo_seams = []
	for si in ed.seams.size():
		var s: Dictionary = ed.seams[si]
		var ea := _seam_side(s["a"], index)
		var eb := _seam_side(s["b"], index)
		var col := Color(SEAM_COLORS[si % SEAM_COLORS.size()])
		var key := "%s:%d-%s:%d" % [String(s["a"][0]), int(s["a"][1]), String(s["b"][0]), int(s["b"][1])]
		_geo_seams.append({"ai": ea[0], "am": ea[1], "bi": eb[0], "bm": eb[1], "col": col, "key": key})


# [piece index, edge midpoint in local coords], or [-1, zero] when the piece is missing
func _seam_side(side: Array, index: Dictionary) -> Array:
	var pid := String(side[0])
	if not index.has(pid):
		return [-1, Vector2.ZERO]
	var i: int = index[pid]
	var edges: Array = _geo_pieces[i]["edges"]
	if edges.is_empty():
		return [-1, Vector2.ZERO]
	var e: int = ((int(side[1]) % edges.size()) + edges.size()) % edges.size()
	return [i, edges[e]["mid"]]


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
	tw["grain"] = PI - float(p.get("grain", 0.0))
	tw["table"] = (p.get("table", {"x": 0.0, "y": 0.0}) as Dictionary).duplicate()
	var w: Dictionary = (p.get("wrap", {}) as Dictionary).duplicate()
	w["theta"] = fposmod(360.0 - float(w.get("theta", 0.0)), 360.0)
	tw["wrap"] = w


func _sew_preview() -> String:
	if ed.sew_a == null or hover == null or not hover.has("e"):
		return ""
	if String(hover["p"]) == String(ed.sew_a[0]) and int(hover["e"]) == int(ed.sew_a[1]):
		return ""
	var a := _edge_info(String(ed.sew_a[0]), int(ed.sew_a[1]))
	var b := _edge_info(String(hover["p"]), int(hover["e"]))
	if a.is_empty() or b.is_empty():
		return ""
	return sew_text(a["name"], int(ed.sew_a[1]), a["len"], b["name"], int(hover["e"]), b["len"])


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
		if point_in(ps[i]["outline"], w.x - float(t.get("x", 0.0)), w.y - float(t.get("y", 0.0))):
			return ps[i]
	return null


# index into ed.seams of the seam drawn nearest the point (within 7 px), or -1
func hit_seam(sx: float, sy: float) -> int:
	_ensure_geo()
	var pt := Vector2(sx, sy)
	var best := -1
	var bd := 7.0
	for k in _geo_seams.size():
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


# ------------------------------------------------------------------ tools
func set_tool(t: String) -> void:
	ed.tool = t
	ed.sew_a = null
	_sew_note = ""
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
	else:
		hint_label.text = "Drag pieces and corners to reshape them"


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
		ed.push_undo()
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
			ed.push_undo()
			ed.sel_v = vi
			drag = {"kind": "vertex", "p": sel, "vi": vi}
			ed.changed("ui")
			return
	var hp: Variant = hit_piece(sx, sy)
	if hp != null:
		hp = hp as Dictionary
		ed.push_undo()
		ed.sel = hp["id"]
		ed.sel_v = -1
		_sel_seam_key = ""
		var w := to_world(sx, sy)
		var t: Dictionary = hp.get("table", {"x": 0.0, "y": 0.0})
		drag = {"kind": "piece", "p": hp, "dx": float(t.get("x", 0.0)) - w.x, "dy": float(t.get("y", 0.0)) - w.y, "moved": false}
		ed.changed("ui")
	else:
		var ks := hit_seam(sx, sy)
		if ks >= 0:
			_sel_seam_key = String(_geo_seams[ks]["key"])
			ed.table_dirty = true
			return
		_sel_seam_key = ""
		ed.sel = null
		ed.sel_v = -1
		drag = {"kind": "pan", "sx": sx, "sy": sy, "ox": ox, "oy": oy}
		ed.changed("ui")


static func _same_side(a: Array, b: Array) -> bool:
	return String(a[0]) == String(b[0]) and int(a[1]) == int(b[1])


func _move(spos: Vector2) -> void:
	var sx := spos.x
	var sy := spos.y
	var d = drag
	if d == null:
		var nh = null
		if ed.tool == "sew":
			for p in ed.pieces:
				var he := hit_edge(p, sx, sy, 12.0)
				if not he.is_empty():
					nh = {"p": p["id"], "e": he["edge"]}
		else:
			var p: Variant = hit_piece(sx, sy)
			if p != null:
				nh = {"p": (p as Dictionary)["id"]}
			else:
				nh = null
		if (nh == null) != (hover == null) or (nh != null and (nh["p"] != hover.get("p", null) or nh.get("e", -1) != hover.get("e", -1))):
			hover = nh
			ed.table_dirty = true
			if ed.tool == "sew":
				update_hint()
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
		var w2 := to_world(sx, sy)
		var p2: Dictionary = d["p"]
		var t2: Dictionary = p2.get("table", {"x": 0.0, "y": 0.0})
		(p2["outline"] as Array)[int(d["vi"])] = [snapv(w2.x - float(t2.get("x", 0.0))), snapv(w2.y - float(t2.get("y", 0.0)))]
		sync_twin(ed, p2)
		ed.table_dirty = true
		ed.changed("structure-drag")


func _up() -> void:
	var d = drag
	drag = null
	if d != null and String(d.get("kind", "")) == "vertex":
		ed.changed("structure")
	elif d != null and String(d.get("kind", "")) == "piece" and bool(d.get("moved", false)):
		ed.changed("table-done")


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
	ed.push_undo()
	var dropped := insert_vertex(ed, p, int(h["edge"]), [snapv(float(a[0]) + (float(b[0]) - float(a[0])) * float(h["t"])), snapv(float(a[1]) + (float(b[1]) - float(a[1])) * float(h["t"]))])
	ed.sel_v = int(h["edge"]) + 1
	if dropped > 0:
		ed.notify("Splitting that edge removed %d seam(s). Ctrl+Z restores them." % dropped)
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
		ed.sew_a = null
		_sel_seam_key = ""
		update_hint()
		ed.changed("ui")
	elif k.keycode == KEY_V:
		set_tool("select")
	elif k.keycode == KEY_S and not k.ctrl_pressed and not k.meta_pressed:
		set_tool("sew")
	elif k.keycode == KEY_F:
		fit()
	elif (k.keycode == KEY_DELETE or k.keycode == KEY_BACKSPACE) and not ed.active_locked():
		var p = null
		if ed.sel != null:
			p = piece_by_id(ed.pieces, ed.sel)
		if p == null:
			return
		ed.push_undo()
		var pname := String(p.get("name", p["id"]))
		if ed.sel_v >= 0 and (p["outline"] as Array).size() > 3:
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
	(p["outline"] as Array).insert(e + 1, pt)
	var dropped := 0
	var kept := []
	for s in edref.seams:
		var kill := false
		for side in ["a", "b"]:
			if String(s[side][0]) == String(p["id"]):
				if int(s[side][1]) == e:
					kill = true
				elif int(s[side][1]) > e:
					s[side][1] = int(s[side][1]) + 1
		if kill:
			dropped += 1
		else:
			kept.append(s)
	edref.seams = kept
	return dropped


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


static func normalize_in_place(p: Dictionary) -> void:
	if signed_area(p["outline"]) < 0.0:
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
	var o: Array = []
	for q in p["outline"]:
		o.append([2.0 * cx - float(q[0]), float(q[1])])
	o.reverse()
	p["outline"] = o
	for s in seams:
		for side in ["a", "b"]:
			if String(s[side][0]) == String(p["id"]):
				s[side][1] = ((n - 2 - int(s[side][1])) % n + n) % n
				s["reverse"] = not bool(s.get("reverse", false))
	p["grain"] = PI - float(p.get("grain", 0.0))
	p["wrap"]["theta"] = fposmod(360.0 - float((p.get("wrap", {}) as Dictionary).get("theta", 0.0)), 360.0)


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
	# seams: one thin semi-transparent line between the two edge midpoints, a dot at each end.
	# Only the picked seam is drawn heavier.
	for k in _geo_seams.size():
		var seg := _seam_ends(k)
		if seg.is_empty():
			continue
		var sg: Dictionary = _geo_seams[k]
		var picked: bool = String(sg["key"]) == _sel_seam_key
		var c := Color(sg["col"], 0.95 if picked else 0.5)
		draw_line(seg[0], seg[1], c, 2.5 if picked else 1.0)
		var r := 3.0 if picked else 2.0
		draw_circle(seg[0], r, c)
		draw_circle(seg[1], r, c)
	# sew mode: the first edge picked (orange) and the edge under the pointer (teal), so the pair is visible
	# before the second click
	if ed.tool == "sew":
		if ed.sew_a != null:
			_draw_edge_hl(String(ed.sew_a[0]), int(ed.sew_a[1]), Color("#ff9d4d"))
		if hover != null and hover.has("e"):
			var same: bool = ed.sew_a != null and String(ed.sew_a[0]) == String(hover["p"]) and int(ed.sew_a[1]) == int(hover["e"])
			if not same:
				_draw_edge_hl(String(hover["p"]), int(hover["e"]), Color("#3fd3c0"))
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
	# edge length labels: selected piece, sew mode, or zoomed in
	for i in ed.pieces.size():
		var p: Dictionary = ed.pieces[i]
		var is_sel: bool = ed.sel == String(p["id"])
		if not (is_sel or ed.tool == "sew" or cam_z > 200.0):
			continue
		var g: Dictionary = _geo_pieces[i]
		var off := _off(p)
		for e in g["edges"]:
			var m: Vector2 = e["mid"]
			var n: Vector2 = e["n"]
			var pp := to_screen(m.x + off.x, m.y + off.y) + n * 15.0
			var tw: float = e["tw"]
			var erect := Rect2(Vector2(pp.x, pp.y - asc), Vector2(tw, lh))
			if view.intersects(erect) and _claim(taken, erect):
				draw_string(font, pp, String(e["txt"]), HORIZONTAL_ALIGNMENT_LEFT, -1, fs, Color("#ffb877") if is_sel else Color("#8f98a6"))
	# scale bar
	var bx := 14.0
	var by := size.y - 18.0
	var blen := 0.1 * cam_z
	draw_line(Vector2(bx, by), Vector2(bx + blen, by), Color("#8f98a6"), 2.0)
	draw_string(font, Vector2(bx + blen + 6.0, by), "10 cm", HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("#8f98a6"))


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
