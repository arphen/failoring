# Outfit: several garments on one mannequin, simulated as ONE cloth (one FailoringSim, one body).
#
# merge() concatenates the given garments' pieces and seams into one plan for PlanLoader.build:
#   * piece and seam ids get the garment's prefix ("g<uid>_"), so ids stay unique and the seam indices (edge numbers)
#     are unchanged;
#   * each piece keeps its garment's fabric ("fabric") and colour is looked up by the piece's "garment" uid;
#   * layers: garments[0] is the innermost. Each piece's wrap.clear grows by LAYER_CLEAR per layer, so an outer garment
#     starts outside the inner ones (tee under jacket: the jacket wraps 8 mm further out);
#   * the table layout of the merged pieces puts the garments side by side (only the stage poses use it; the cutting
#     table shows the active garment with its own layout).
# No seam joins two garments. Garments touch through the solver's self collision, which is global over all cloth
# triangles (cloth_core solveSelfCollision), so the editor turns self collision on whenever two or more garments are
# shown. Seamed vertices are excluded only within their own seams, which is what the solver already does.
const Table = preload("res://table.gd")

const LAYER_CLEAR := 0.008     # m added to each piece's wrap.clear per layer above the innermost garment
const DEFAULT_CLEAR := 0.012   # the wrap clearance pieces get when they do not set one (plan_loader / wrap.gd)
const COLUMN_GAP := 0.15       # m between garments in the merged table layout


static func prefix(uid: int) -> String:
	return "g%d_" % uid


# garments: [{uid, fabric, pieces, seams}] in layer order (index 0 = innermost), visible ones only. Inputs are not
# modified. Returns {pieces, seams, layer: {uid: layer index}, count: piece count}.
static func merge(garments: Array) -> Dictionary:
	var out_pieces := []
	var out_seams := []
	var layer_of := {}
	var col_x := 0.0
	for layer in garments.size():
		var g: Dictionary = garments[layer]
		var uid := int(g["uid"])
		var pre := prefix(uid)
		layer_of[uid] = layer
		var pieces: Array = g["pieces"]
		var lo := _table_min_x(pieces)
		for p in pieces:
			var t: Dictionary = p.get("table", {"x": 0.0, "y": 0.0})
			var q: Dictionary = (p as Dictionary).duplicate(true)
			q["id"] = pre + String(p["id"])
			q["garment"] = uid
			q["fabric"] = String(g.get("fabric", "cotton_poplin"))
			q["table"] = {"x": float(t.get("x", 0.0)) - lo + col_x, "y": float(t.get("y", 0.0))}
			var w: Dictionary = (p.get("wrap", {}) as Dictionary).duplicate(true)
			w["clear"] = float(w.get("clear", DEFAULT_CLEAR)) + LAYER_CLEAR * float(layer)
			q["wrap"] = w
			out_pieces.append(q)
		col_x += _table_width(pieces) + COLUMN_GAP
		for s in g.get("seams", []):
			var m: Dictionary = (s as Dictionary).duplicate(true)
			# the sides keep their range (a part of an edge): only the piece id gets the prefix
			var sa: Array = (s["a"] as Array).duplicate()
			sa[0] = pre + String(sa[0])
			var sb: Array = (s["b"] as Array).duplicate()
			sb[0] = pre + String(sb[0])
			m["a"] = sa
			m["b"] = sb
			out_seams.append(m)
	return {"pieces": out_pieces, "seams": out_seams, "layer": layer_of, "count": out_pieces.size()}


# Garment uid of a merged piece id (the prefix), or -1.
static func uid_of(merged_id: String) -> int:
	if not merged_id.begins_with("g") or merged_id.find("_") < 0:
		return -1
	return int(merged_id.substr(1, merged_id.find("_") - 1))


static func _table_min_x(pieces: Array) -> float:
	var x0 := 1e9
	for p in pieces:
		var b := Table.bbox_of(p["outline"])
		var t: Dictionary = p.get("table", {"x": 0.0, "y": 0.0})
		x0 = minf(x0, float(b[0]) + float(t.get("x", 0.0)))
	return x0 if x0 < 1e9 else 0.0


static func _table_width(pieces: Array) -> float:
	var x0 := 1e9
	var x1 := -1e9
	for p in pieces:
		var b := Table.bbox_of(p["outline"])
		var t: Dictionary = p.get("table", {"x": 0.0, "y": 0.0})
		x0 = minf(x0, float(b[0]) + float(t.get("x", 0.0)))
		x1 = maxf(x1, float(b[2]) + float(t.get("x", 0.0)))
	return x1 - x0 if x0 < 1e9 else 0.0
