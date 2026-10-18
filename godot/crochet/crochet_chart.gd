extends Control
# Stitch chart (docs/CROCHET_PLAN.md section 9.1). One US-style symbol per stitch, placed from CrochetPattern.get_chart():
# row 1 at the bottom for flat pieces, rounds as rings for pieces worked in the round. Hover reads out the stitch, a click selects it.
# The symbols are built once in set_chart(); _draw only walks the visible ones, so a 5800-stitch bag still pans and zooms.

signal stitch_hovered(id: int)
signal stitch_selected(id: int)

const PAD := 18.0
const COL_BG := Color(0.13, 0.14, 0.16)
const COL_RS := Color(0.86, 0.87, 0.90)
const COL_WS := Color(0.55, 0.72, 0.95)
const COL_JOIN := Color(1.0, 0.62, 0.20)
const COL_BOTH := Color(0.95, 0.40, 0.85)
const COL_SEL := Color(1.0, 0.92, 0.25)
const COL_HOVER := Color(1.0, 1.0, 1.0)
const COL_ROW := Color(1, 1, 1, 0.08)

# one entry per stitch: {id, pos (chart units, y up), type, insertion, written, facing, row, index, line, targets, link}
var symbols: Array = []
var rows: Array = []
var selected := -1            # the clicked stitch (-1 when the highlight is a whole line or nothing)
var highlight := PackedInt32Array()  # every highlighted stitch (the selection, from here, the 3D view or a pattern line)
var hover := -1

var _base_count := {}         # stitch id -> how many stitches are worked into it (2 or more = an increase)
var _rows_pts: Array = []     # per row: PackedInt32Array of stitch ids in made order
var _bounds := Rect2()
var _zoom := 1.0              # multiplies the fit scale
var _pan := Vector2.ZERO      # pixels
var _panning := false
var _ellipse := PackedVector2Array()
var _info: Label

func _ready() -> void:
	clip_contents = true
	mouse_filter = Control.MOUSE_FILTER_STOP
	for i in 13:
		var a := TAU * i / 12.0
		_ellipse.push_back(Vector2(cos(a) * 0.62, sin(a) * 0.36))
	_info = Label.new()
	_info.position = Vector2(6, 4)
	_info.add_theme_font_size_override("font_size", 12)
	_info.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_info)
	resized.connect(queue_redraw)

# ------------------------------------------------------------------------------------------------ data

func set_chart(chart: Dictionary) -> void:
	symbols.clear()
	rows = chart.get("rows", [])
	selected = -1
	highlight = PackedInt32Array()
	hover = -1
	var link_kind := {}
	for l in chart.get("links", []):
		link_kind[int(l["a"])] = String(l["kind"])
		link_kind[int(l["b"])] = String(l["kind"])
	_rows_pts.clear()
	_base_count.clear()
	for r in rows:
		_rows_pts.push_back(PackedInt32Array())
	var lo := Vector2(INF, INF)
	var hi := Vector2(-INF, -INF)
	for s in chart.get("stitches", []):
		var p := Vector2(float(s["x"]), float(s["y"]))
		var id := int(s["id"])
		symbols.push_back({"id": id, "pos": p, "type": String(s["type"]), "insertion": String(s["insertion"]), "written": String(s["written"]),
			"facing": String(s["facing"]), "row": int(s["row"]), "index": int(s["index"]), "line": int(s["line"]),
			"targets": s["targets"], "link": link_kind.get(id, ""), "piece": String(s["piece"])})
		for t in s["targets"]:
			_base_count[int(t)] = int(_base_count.get(int(t), 0)) + 1
		var r := int(s["row"])
		if r >= 0 and r < _rows_pts.size():
			_rows_pts[r].push_back(id)
		lo = lo.min(p)
		hi = hi.max(p)
	_bounds = Rect2(lo, hi - lo) if symbols.size() > 0 else Rect2()
	fit()
	_set_info(HINT)

const HINT := "wheel: zoom   right-drag: pan   double-click: fit"

func symbol_count() -> int:
	return symbols.size()

# zoom to `zoom` times the fitted view with stitch `id` in the middle
func focus(id: int, zoom: float) -> void:
	if id < 0 or id >= symbols.size():
		return
	_zoom = zoom
	_pan = Vector2.ZERO
	_pan = size * 0.5 - to_screen(symbols[id]["pos"])
	queue_redraw()

func fit() -> void:
	_zoom = 1.0
	_pan = Vector2.ZERO
	queue_redraw()

# ------------------------------------------------------------------------------------------------ geometry

func _scale() -> float:
	var avail := size - Vector2(2 * PAD, 2 * PAD)
	var w := maxf(_bounds.size.x, 1.0)
	var h := maxf(_bounds.size.y, 1.0)
	return maxf(minf(avail.x / w, avail.y / h), 1.0) * _zoom

# chart units (y up) to control pixels
func to_screen(p: Vector2) -> Vector2:
	var sc := _scale()
	var c := _bounds.get_center()
	return size * 0.5 + _pan + Vector2((p.x - c.x) * sc, -(p.y - c.y) * sc)

func stitch_screen_pos(id: int) -> Vector2:
	return to_screen(symbols[id]["pos"]) if id >= 0 and id < symbols.size() else Vector2(-1, -1)

# the stitch whose symbol is under a point of this control, -1 if none within a symbol's reach
func stitch_at(pt: Vector2) -> int:
	var reach := maxf(_scale() * 0.5, 6.0)
	var best := -1
	var best_d := reach * reach
	for s in symbols:
		var d := to_screen(s["pos"]).distance_squared_to(pt)
		if d < best_d:
			best_d = d
			best = int(s["id"])
	return best

func describe(id: int) -> String:
	if id < 0 or id >= symbols.size():
		return ""
	var s: Dictionary = symbols[id]
	var ins := String(s["insertion"]).to_upper()
	var written := String(s["written"])
	var t := "stitch %d: row %d #%d, %s" % [id, int(s["row"]), int(s["index"]), s["type"]]
	if ins != "BOTH":
		t += " %s" % ins
		if written != String(s["insertion"]):
			t += " (written %s)" % written.to_upper()
	t += ", %s, line %d" % [s["facing"], int(s["line"])]
	if String(s["link"]) != "":
		t += ", %s seam" % String(s["link"]).replace("_", " ")
	return t

func select_stitch(id: int) -> void:
	set_highlight(PackedInt32Array([id]) if id >= 0 and id < symbols.size() else PackedInt32Array())

# highlight these stitches (one makes it the selected stitch) and say what they are
func set_highlight(ids: PackedInt32Array) -> void:
	highlight = ids
	selected = ids[0] if ids.size() == 1 else -1
	if ids.size() == 0:
		_set_info(HINT)
	elif ids.size() == 1:
		_set_info(describe(ids[0]))
	else:
		_set_info("%d stitches, line %d" % [ids.size(), int(symbols[ids[0]]["line"])])
	queue_redraw()

# scroll (not zoom) so that stitch `id` is inside the view
func reveal(id: int) -> void:
	if id < 0 or id >= symbols.size():
		return
	var at := to_screen(symbols[id]["pos"])
	if not Rect2(Vector2.ZERO, size).grow(-PAD).has_point(at):
		_pan += size * 0.5 - at
		queue_redraw()

# ------------------------------------------------------------------------------------------------ input

func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		if _panning:
			_pan += event.relative
			queue_redraw()
			return
		var h := stitch_at(event.position)
		if h != hover:
			hover = h
			_set_info(describe(h) if h >= 0 else HINT)
			stitch_hovered.emit(h)
			queue_redraw()
	elif event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_LEFT and event.pressed:
			if event.double_click:
				fit()
				return
			var id := stitch_at(event.position)
			select_stitch(id)
			stitch_selected.emit(id)
		elif event.button_index == MOUSE_BUTTON_MIDDLE or event.button_index == MOUSE_BUTTON_RIGHT:
			_panning = event.pressed
		elif event.pressed and (event.button_index == MOUSE_BUTTON_WHEEL_UP or event.button_index == MOUSE_BUTTON_WHEEL_DOWN):
			var f := 1.15 if event.button_index == MOUSE_BUTTON_WHEEL_UP else 1.0 / 1.15
			var before: Vector2 = event.position - size * 0.5 - _pan
			_zoom = clampf(_zoom * f, 0.5, 40.0)
			_pan = event.position - size * 0.5 - before * f
			queue_redraw()

func _set_info(text: String) -> void:
	if _info != null:
		_info.text = text

# ------------------------------------------------------------------------------------------------ drawing

func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), COL_BG)
	if symbols.is_empty():
		return
	var sc := _scale()
	var r := sc * 0.30           # half size of a symbol, px
	var view := Rect2(Vector2.ZERO, size).grow(r * 2.0)
	# the rows as lines through their stitches in made order (horizontal for flat work, rings for rounds)
	for ri in _rows_pts.size():
		var ids: PackedInt32Array = _rows_pts[ri]
		if ids.size() < 2:
			continue
		var pts := PackedVector2Array()
		for id in ids:
			pts.push_back(to_screen(symbols[id]["pos"]))
		if String(rows[ri].get("kind", "row")) == "round":
			pts.push_back(pts[0])
		draw_polyline(pts, COL_ROW, 1.0)
	if r < 2.5:   # too small for symbols: one dot per stitch, coloured by type of facing
		for s in symbols:
			var c := to_screen(s["pos"])
			if view.has_point(c):
				draw_rect(Rect2(c - Vector2(1, 1), Vector2(2, 2)), _colour(s))
		_draw_marks()
		return
	for s in symbols:
		var c := to_screen(s["pos"])
		if not view.has_point(c):
			continue
		_draw_base_lines(s, c)
	for s in symbols:
		var c := to_screen(s["pos"])
		if view.has_point(c):
			_draw_symbol(s, c, r)
	_draw_marks()

func _colour(s: Dictionary) -> Color:
	var k := String(s["link"])
	if k == "join":
		return COL_JOIN
	if k == "both_layers":
		return COL_BOTH
	return COL_RS if String(s["facing"]) == "RS" else COL_WS

# an inc is two stitches worked into one base, a dec one stitch worked into two: a thin leg from the symbol to each base
func _draw_base_lines(s: Dictionary, c: Vector2) -> void:
	var t: PackedInt32Array = s["targets"]
	if t.is_empty():
		return
	if t.size() == 1 and _base_count.get(t[0], 0) < 2:
		return
	for b in t:
		draw_line(c, to_screen(symbols[b]["pos"]), Color(_colour(s), 0.35), 1.0)

func _draw_symbol(s: Dictionary, c: Vector2, r: float) -> void:
	var col := _colour(s)
	var w := maxf(1.0, r * 0.18)
	match String(s["type"]):
		"ch":
			var pts := PackedVector2Array()
			for p in _ellipse:
				pts.push_back(c + p * r * 1.6)
			draw_polyline(pts, col, w)
		"slst":
			draw_circle(c, r * 0.45, col)
		"sc":
			draw_line(c + Vector2(-r, -r) * 0.8, c + Vector2(r, r) * 0.8, col, w)
			draw_line(c + Vector2(-r, r) * 0.8, c + Vector2(r, -r) * 0.8, col, w)
		"hdc":
			_draw_t(c, r, col, w, 0)
		"dc":
			_draw_t(c, r, col, w, 1)
		"tr":
			_draw_t(c, r, col, w, 2)
		_:
			draw_circle(c, r * 0.4, col)
	var ins := String(s["insertion"])
	if ins == "blo":
		draw_arc(c + Vector2(0, r * 0.95), r * 0.8, deg_to_rad(20), deg_to_rad(160), 8, col, w)    # ‿ under the symbol
	elif ins == "flo":
		draw_arc(c + Vector2(0, -r * 0.95), r * 0.8, deg_to_rad(200), deg_to_rad(340), 8, col, w)  # ⁀ over it
	elif ins == "space":
		draw_rect(Rect2(c + Vector2(-r, -r) * 1.05, Vector2(r, r) * 2.1), Color(col, 0.4), false, 1.0)

func _draw_t(c: Vector2, r: float, col: Color, w: float, slashes: int) -> void:
	draw_line(c + Vector2(-r * 0.8, -r), c + Vector2(r * 0.8, -r), col, w)
	draw_line(c + Vector2(0, -r), c + Vector2(0, r), col, w)
	for i in slashes:
		var y := r * (0.15 - 0.45 * i) + r * 0.15
		draw_line(c + Vector2(-r * 0.5, y + r * 0.3), c + Vector2(r * 0.5, y - r * 0.3), col, w)

func _draw_marks() -> void:
	var r := maxf(_scale() * 0.42, 5.0)
	if hover >= 0 and not highlight.has(hover):
		draw_arc(to_screen(symbols[hover]["pos"]), r, 0, TAU, 20, COL_HOVER, 1.5)
	var view := Rect2(Vector2.ZERO, size).grow(r)
	for id in highlight:
		var c := to_screen(symbols[id]["pos"])
		if view.has_point(c):
			draw_arc(c, r, 0, TAU, 20, COL_SEL, 2.5)
