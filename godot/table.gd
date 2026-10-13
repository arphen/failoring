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
var space_down := false
var fitted := false
var hint_label: Label


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	resized.connect(_on_resized)


func _on_resized() -> void:
	if not fitted and size.x > 50.0:
		fitted = true
		fit()


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


# validity before sending to the simulator (mirrors App.validPieces)
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
				if edge_len(o, i) < 0.008:
					why = "edge shorter than 8 mm"
					break
		if why != null:
			bad[String(p["id"])] = why
		else:
			out.append(p)
	return {"pieces": out, "bad": bad}


var _valid_sig := []      # ids + outlines that _valid_res was computed from
var _valid_res := {"pieces": [], "bad": {}}


# valid_pieces() is O(n^2) per piece and _draw runs on every redraw (drags included). Validity only depends on
# ids and outlines, so recompute only when those change. The signature is compared exactly, not hashed.
func _valid_cached() -> Dictionary:
	var sig := []
	for p in ed.pieces:
		sig.append(String(p["id"]))
		for q in p["outline"]:
			sig.append(float(q[0]))
			sig.append(float(q[1]))
	if sig != _valid_sig:
		_valid_sig = sig
		_valid_res = valid_pieces(ed.pieces)
	return _valid_res


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


func fit() -> void:
	if ed.pieces.is_empty() or size.x < 50.0:
		return
	var x0 := 1e9
	var y0 := 1e9
	var x1 := -1e9
	var y1 := -1e9
	for p in ed.pieces:
		var b := bbox_of(p["outline"])
		x0 = minf(x0, b[0] + float((p.get("table", {}) as Dictionary).get("x", 0.0)))
		x1 = maxf(x1, b[2] + float((p.get("table", {}) as Dictionary).get("x", 0.0)))
		y0 = minf(y0, b[1] + float((p.get("table", {}) as Dictionary).get("y", 0.0)))
		y1 = maxf(y1, b[3] + float((p.get("table", {}) as Dictionary).get("y", 0.0)))
	var w: float = maxf(0.3, x1 - x0)
	var h: float = maxf(0.3, y1 - y0)
	var sz := size
	cam_z = minf((sz.x - 150.0) / w, (sz.y - 150.0) / h)
	ox = (sz.x - w * cam_z) * 0.5 - x0 * cam_z
	oy = (sz.y + h * cam_z) * 0.5 + y0 * cam_z
	ed.table_dirty = true


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


# ------------------------------------------------------------------ tools
func set_tool(t: String) -> void:
	ed.tool = t
	ed.sew_a = null
	update_hint()
	ed.table_dirty = true


func update_hint() -> void:
	if hint_label == null:
		return
	if ed.tool == "sew":
		hint_label.text = "Now click the edge to sew it to (Esc cancels)" if ed.sew_a != null else "Sew: click two edges"
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
	if button == MOUSE_BUTTON_MIDDLE or button == MOUSE_BUTTON_RIGHT or space_down:
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
		ed.seams = ed.seams.filter(func(s) -> bool:
			return not (_same_side(s["a"], ed.sew_a) or _same_side(s["b"], ed.sew_a) or _same_side(s["a"], bside) or _same_side(s["b"], bside)))
		ed.seams.append({"a": ed.sew_a, "b": bside, "reverse": true})
		ed.sew_a = null
		update_hint()
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
		var w := to_world(sx, sy)
		var t: Dictionary = hp.get("table", {"x": 0.0, "y": 0.0})
		drag = {"kind": "piece", "p": hp, "dx": float(t.get("x", 0.0)) - w.x, "dy": float(t.get("y", 0.0)) - w.y, "moved": false}
		ed.changed("ui")
	else:
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
		d["moved"] = true
		ed.table_dirty = true
		ed.changed("table")
	elif String(d["kind"]) == "vertex":
		var w2 := to_world(sx, sy)
		var p2: Dictionary = d["p"]
		var t2: Dictionary = p2.get("table", {"x": 0.0, "y": 0.0})
		(p2["outline"] as Array)[int(d["vi"])] = [snapv(w2.x - float(t2.get("x", 0.0))), snapv(w2.y - float(t2.get("y", 0.0)))]
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
	if ed.tool != "select":
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
	insert_vertex(ed, p, int(h["edge"]), [snapv(float(a[0]) + (float(b[0]) - float(a[0])) * float(h["t"])), snapv(float(a[1]) + (float(b[1]) - float(a[1])) * float(h["t"]))])
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
		space_down = true
		ed._on_play()
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
	if k.keycode == KEY_ESCAPE:
		ed.sew_a = null
		update_hint()
		ed.changed("ui")
	elif k.keycode == KEY_V:
		set_tool("select")
	elif k.keycode == KEY_S and not k.ctrl_pressed and not k.meta_pressed:
		set_tool("sew")
	elif k.keycode == KEY_F:
		fit()
	elif k.keycode == KEY_DELETE or k.keycode == KEY_BACKSPACE:
		var p = null
		if ed.sel != null:
			p = piece_by_id(ed.pieces, ed.sel)
		if p == null:
			return
		ed.push_undo()
		if ed.sel_v >= 0 and (p["outline"] as Array).size() > 3:
			remove_vertex(p, ed.sel_v, ed.seams)
			ed.sel_v = -1
		else:
			remove_piece(ed, p["id"])
		ed.changed("structure")


func _unhandled_input(e: InputEvent) -> void:
	if e is InputEventKey:
		var k := e as InputEventKey
		if not k.pressed and k.keycode == KEY_SPACE:
			space_down = false


# ------------------------------------------------------------------ piece edits (state.js)
static func insert_vertex(edref, p: Dictionary, e: int, pt: Array) -> void:
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


static func remove_vertex(p: Dictionary, i: int, seams: Array) -> bool:
	var n: int = (p["outline"] as Array).size()
	if n <= 3:
		return false
	var prev: int = (i - 1 + n) % n
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
	for s in seams:
		for side in ["a", "b"]:
			if String(s[side][0]) == String(p["id"]) and int(s[side][1]) > i:
				s[side][1] = int(s[side][1]) - 1
	(p["outline"] as Array).remove_at(i)
	return true


static func remove_piece(edref, pid: String) -> void:
	edref.pieces = (edref.pieces as Array).filter(func(p) -> bool: return String(p["id"]) != pid)
	edref.seams = (edref.seams as Array).filter(func(s) -> bool: return String(s["a"][0]) != pid and String(s["b"][0]) != pid)
	if edref.sel == pid:
		edref.sel = null
		edref.sel_v = -1


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
	var b := bbox_of(p["outline"])
	(q["table"] as Dictionary)["x"] = float((q["table"] as Dictionary).get("x", 0.0)) + (b[2] - b[0]) + 0.1
	(edref.pieces as Array).append(q)
	edref.sel = q["id"]
	return q


# ------------------------------------------------------------------ drawing
func _draw() -> void:
	if ed == null or ed.pieces == null:
		return
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
	var val := _valid_cached()
	var fcol := Color(FABRIC_COLORS.get(ed.fabric_id, "#9fb7d6"))
	# seams: edge pairs + match ticks
	var si := 0
	for s in ed.seams:
		var a = piece_by_id(ed.pieces, String(s["a"][0]))
		var b = piece_by_id(ed.pieces, String(s["b"][0]))
		si += 1
		if a == null or b == null:
			continue
		var col := Color(SEAM_COLORS[(si - 1) % SEAM_COLORS.size()])
		var ea := _edge_ends(a, int(s["a"][1]))
		var eb := _edge_ends(b, int(s["b"][1]))
		for e in [ea, eb]:
			draw_line(to_screen(e[0][0], e[0][1]), to_screen(e[1][0], e[1][1]), col, 3.5)
		var kk := 5
		for k in kk + 1:
			var t: float = float(k) / float(kk)
			var pa := [ea[0][0] + (ea[1][0] - ea[0][0]) * t, ea[0][1] + (ea[1][1] - ea[0][1]) * t]
			var tb: float = (1.0 - t) if bool(s.get("reverse", false)) else t
			var pb := [eb[0][0] + (eb[1][0] - eb[0][0]) * tb, eb[0][1] + (eb[1][1] - eb[0][1]) * tb]
			draw_dashed_line(to_screen(pa[0], pa[1]), to_screen(pb[0], pb[1]), Color(col, 0.4), 1.5, 5.0)
	# pieces
	for p in ed.pieces:
		var o: Array = p["outline"]
		var t: Dictionary = p.get("table", {"x": 0.0, "y": 0.0})
		var tx: float = float(t.get("x", 0.0))
		var ty: float = float(t.get("y", 0.0))
		var is_sel: bool = ed.sel == String(p["id"])
		var bad = (val["bad"] as Dictionary).get(String(p["id"]), null)
		var pts := PackedVector2Array()
		for q in o:
			pts.append(to_screen(float(q[0]) + tx, float(q[1]) + ty))
		var fill := Color("#5a2328cc") if bad != null else (Color("#ece6d6") if is_sel else Color("#d9d1bd"))
		fill.a *= 0.93
		draw_colored_polygon(pts, fill)
		var poly := PackedVector2Array(pts)
		poly.append(pts[0])
		draw_polyline(poly, Color("#ff6b6b") if bad != null else (Color("#ff9d4d") if is_sel else Color("#6e674f")), 2.5 if is_sel else 1.6)
		# fabric tint
		draw_colored_polygon(pts, Color(fcol, 0.22))
		# grain arrow
		var bb := bbox_of(o)
		var g: float = float(p.get("grain", 1.5708))
		var gx := cos(g)
		var gy := sin(g)
		var gc := [0.0, 0.0]
		for q in o:
			gc[0] += float(q[0]) / o.size()
			gc[1] += float(q[1]) / o.size()
		var a0 := to_screen(gc[0] + tx - gx * 0.07, gc[1] + ty - gy * 0.07)
		var a1 := to_screen(gc[0] + tx + gx * 0.07, gc[1] + ty + gy * 0.07)
		draw_line(a0, a1, Color("#2b2b2b"), 1.5)
		var ang := (a1 - a0).angle()
		draw_colored_polygon(PackedVector2Array([a1, a1 - Vector2(cos(ang - 0.4), sin(ang - 0.4)) * 8.0, a1 - Vector2(cos(ang + 0.4), sin(ang + 0.4)) * 8.0]), Color("#2b2b2b"))
		draw_string(font, to_screen(gc[0] + tx, gc[1] + ty - 0.04) + Vector2(-20, 0), String(p.get("name", p["id"])), HORIZONTAL_ALIGNMENT_LEFT, 120, 11, Color("#3a3426"))
		if bad != null:
			draw_string(font, to_screen(gc[0] + tx, gc[1] + ty + 0.06), String(bad), HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("#ffd0d0"))
		# edge labels
		for i in o.size():
			var ea2: Array = o[i]
			var eb2: Array = o[(i + 1) % o.size()]
			var mx: float = (float(ea2[0]) + float(eb2[0])) * 0.5 + tx
			var my: float = (float(ea2[1]) + float(eb2[1])) * 0.5 + ty
			var el: float = edge_len(o, i)
			var pp := to_screen(mx, my)
			var ex: float = (float(eb2[0]) - float(ea2[0])) / el if el > 0.0 else 0.0
			var ey: float = (float(eb2[1]) - float(ea2[1])) / el if el > 0.0 else 0.0
			if is_sel or ed.tool == "sew" or cam_z > 200.0:
				draw_string(font, pp + Vector2(ey, ex) * 15.0, "%d: %.1f" % [i, el * 100.0], HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("#ffb877") if is_sel else Color("#8f98a6"))
		if is_sel:
			for i in o.size():
				var hp := to_screen(float(o[i][0]) + tx, float(o[i][1]) + ty)
				draw_rect(Rect2(hp - Vector2(5, 5), Vector2(10, 10)), Color("#ff9d4d") if ed.sel_v == i else Color("#14171c"), true)
				draw_rect(Rect2(hp - Vector2(5, 5), Vector2(10, 10)), Color("#ff9d4d"), false, 2.0)
	# scale bar
	var bx := 14.0
	var by := size.y - 18.0
	var blen := 0.1 * cam_z
	draw_line(Vector2(bx, by), Vector2(bx + blen, by), Color("#8f98a6"), 2.0)
	draw_string(font, Vector2(bx + blen + 6.0, by), "10 cm", HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("#8f98a6"))


func _edge_ends(p: Dictionary, e: int) -> Array:
	var o: Array = p["outline"]
	var t: Dictionary = p.get("table", {"x": 0.0, "y": 0.0})
	var a: Array = o[e % o.size()]
	var b: Array = o[(e + 1) % o.size()]
	return [[float(a[0]) + float(t.get("x", 0.0)), float(a[1]) + float(t.get("y", 0.0))], [float(b[0]) + float(t.get("x", 0.0)), float(b[1]) + float(t.get("y", 0.0))]]
