# Small 2D drawing of a garment's pattern pieces: outlines at their cutting-table positions, scaled to fit the control.
# Used by the library cards. Plain Control, no state beyond the pieces and the tint.
extends Control

var pieces: Array = []
var tint := Color("#9fb7d6")


func set_pieces(p: Array) -> void:
	pieces = p
	queue_redraw()


func _draw() -> void:
	if pieces.is_empty():
		return
	var polys: Array = []
	var x0 := 1e9
	var y0 := 1e9
	var x1 := -1e9
	var y1 := -1e9
	for p in pieces:
		var t: Dictionary = p.get("table", {"x": 0.0, "y": 0.0})
		var pts := PackedVector2Array()
		for q in p["outline"]:
			var x := float(q[0]) + float(t.get("x", 0.0))
			var y := float(q[1]) + float(t.get("y", 0.0))
			pts.append(Vector2(x, y))
			x0 = minf(x0, x)
			x1 = maxf(x1, x)
			y0 = minf(y0, y)
			y1 = maxf(y1, y)
		polys.append(pts)
	if x0 > x1:
		return
	var bw := maxf(x1 - x0, 1e-6)
	var bh := maxf(y1 - y0, 1e-6)
	var pad := 6.0
	var s := minf((size.x - 2.0 * pad) / bw, (size.y - 2.0 * pad) / bh)
	var ox := (size.x - bw * s) * 0.5
	var oy := (size.y - bh * s) * 0.5
	var fill := Color(tint.r, tint.g, tint.b, 0.35)
	var edge := tint.darkened(0.35)
	for pts in polys:
		if pts.size() < 3:
			continue
		var sp := PackedVector2Array()
		for v in pts:
			sp.append(Vector2(ox + (v.x - x0) * s, oy + (y1 - v.y) * s))  # table y points up, screen y down
		draw_colored_polygon(sp, fill)
		var closed := sp.duplicate()
		closed.append(sp[0])
		draw_polyline(closed, edge, 1.5, true)
