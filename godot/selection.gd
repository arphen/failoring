# Shared selection and hover model for the cutting table, the panels and (later) the 3D view.
#
# Four kinds of thing can be picked, each by a small id:
#   "piece"  id = piece id (String)              e.g. "p1"
#   "edge"   id = [piece id, edge index]         edge i runs from corner i to corner i+1
#   "corner" id = [piece id, corner index]
#   "seam"   id = seam index (into the garment's seams array)
#   "hole"   id = [piece id, hole index]
#   "guide"  id = [piece id, guide id]          a construction point or line (guides.gd)
# Selection is a set per kind, kept in pick order. The last piece picked is the primary piece: the Piece tab shows it,
# and a corner selection belongs to it. Hover is one item at a time, and is separate from the selection.
#
# Callers never touch the arrays: they use the methods below. `changed` fires when the selection changes, and
# `hover_changed` when the hovered item changes, so a panel or a 3D view can connect to either and redraw.
# `prune` drops picks that no longer exist after an edit (pieces removed, seams deleted, edges split).
extends RefCounted

signal changed
signal hover_changed

const KINDS := ["piece", "edge", "corner", "seam", "hole", "guide"]

var _items := {"piece": [], "edge": [], "corner": [], "seam": [], "hole": [], "guide": []}   # kind -> Array of ids, in pick order
var _hover := {}          # {"kind": String, "id": Variant} or {} for none


# ------------------------------------------------------------------ keys
static func _key(kind: String, id: Variant) -> String:
	match kind:
		"piece":
			return String(id)
		"edge", "corner", "hole":
			return "%s:%d" % [String(id[0]), int(id[1])]
		"guide":
			return "%s:%s" % [String(id[0]), String(id[1])]
		_:
			return str(int(id))


static func _same(kind: String, a: Variant, b: Variant) -> bool:
	return _key(kind, a) == _key(kind, b)


# ------------------------------------------------------------------ reading
func items(kind: String) -> Array:
	return (_items[kind] as Array).duplicate()


func count(kind: String) -> int:
	return (_items[kind] as Array).size()


func contains(kind: String, id: Variant) -> bool:
	for x in _items[kind]:
		if _same(kind, x, id):
			return true
	return false


func is_empty() -> bool:
	for k in KINDS:
		if not (_items[k] as Array).is_empty():
			return false
	return true


# The primary piece id, or null when no piece is picked.
func primary_piece() -> Variant:
	var p: Array = _items["piece"]
	return String(p[p.size() - 1]) if not p.is_empty() else null


# The corner index picked on the primary piece, or -1.
func primary_corner() -> int:
	var pp = primary_piece()
	if pp == null:
		return -1
	var c: Array = _items["corner"]
	for i in range(c.size() - 1, -1, -1):
		if String(c[i][0]) == String(pp):
			return int(c[i][1])
	return -1


func hover_kind() -> String:
	return String(_hover.get("kind", ""))


func hover_id() -> Variant:
	return _hover.get("id", null)


func has_hover() -> bool:
	return not _hover.is_empty()


func hovers(kind: String, id: Variant) -> bool:
	return _hover.has("kind") and String(_hover["kind"]) == kind and _same(kind, _hover["id"], id)


# ------------------------------------------------------------------ selection edits
# Replaces the selection with one item (or clears it when id is null).
func select(kind: String, id: Variant) -> void:
	var before := _snapshot()
	_clear_kinds()
	if id != null:
		(_items[kind] as Array).append(id)
	_emit_if_changed(before)


# Replaces the picks of one kind only (a seam pick keeps the piece selection, for example).
func replace(kind: String, id: Variant) -> void:
	var before := _snapshot()
	(_items[kind] as Array).clear()
	if id != null:
		(_items[kind] as Array).append(id)
	_emit_if_changed(before)


func clear_kind(kind: String) -> void:
	var before := _snapshot()
	(_items[kind] as Array).clear()
	_emit_if_changed(before)


# Adds to the selection, or removes the item when it is already picked (Shift or Ctrl click).
func toggle(kind: String, id: Variant) -> void:
	var before := _snapshot()
	var arr: Array = _items[kind]
	for i in arr.size():
		if _same(kind, arr[i], id):
			arr.remove_at(i)
			_emit_if_changed(before)
			return
	arr.append(id)
	_emit_if_changed(before)


# Adds an item without removing the others (a piece, a corner or a seam joins the selection).
func add(kind: String, id: Variant) -> void:
	if contains(kind, id):
		return
	var before := _snapshot()
	(_items[kind] as Array).append(id)
	_emit_if_changed(before)


func clear() -> void:
	var before := _snapshot()
	_clear_kinds()
	_emit_if_changed(before)


# Sets the primary piece (the table's and tabs' "sel"): a single piece pick, which drops corners on other pieces.
func set_primary_piece(pid: Variant) -> void:
	var before := _snapshot()
	_clear_kinds()
	if pid != null:
		(_items["piece"] as Array).append(String(pid))
	_emit_if_changed(before)


# Sets the corner of the primary piece (-1 clears it), keeping the piece.
func set_primary_corner(i: int) -> void:
	var before := _snapshot()
	var pp = primary_piece()
	(_items["corner"] as Array).clear()
	if pp != null and i >= 0:
		(_items["corner"] as Array).append([String(pp), i])
	_emit_if_changed(before)


# ------------------------------------------------------------------ hover
func set_hover(kind: String, id: Variant) -> void:
	var nh := {} if id == null else {"kind": kind, "id": id}
	if _hover_equal(nh):
		return
	_hover = nh
	hover_changed.emit()


func clear_hover() -> void:
	set_hover("", null)


func _hover_equal(nh: Dictionary) -> bool:
	if _hover.is_empty() or nh.is_empty():
		return _hover.is_empty() and nh.is_empty()
	return String(_hover["kind"]) == String(nh["kind"]) and _same(String(nh["kind"]), _hover["id"], nh["id"])


# ------------------------------------------------------------------ maintenance
# Drops picks and hover that refer to pieces or seams which no longer exist, and edges or corners beyond a piece's
# outline. `pieces` is the garment's piece array (dictionaries with "id" and "outline"); `seam_count` its seam count.
func prune(pieces: Array, seam_count: int) -> void:
	var before := _snapshot()
	var outlines := {}
	var holes := {}
	var guides := {}
	for p in pieces:
		outlines[String(p["id"])] = (p["outline"] as Array).size()
		holes[String(p["id"])] = (p.get("holes", []) as Array).size()
		for g in p.get("guides", []):
			guides["%s:%s" % [String(p["id"]), String(g.get("id", ""))]] = true
	(_items["piece"] as Array).assign((_items["piece"] as Array).filter(func(x) -> bool: return outlines.has(String(x))))
	for kind in ["edge", "corner"]:
		(_items[kind] as Array).assign((_items[kind] as Array).filter(func(x) -> bool:
			return outlines.has(String(x[0])) and int(x[1]) >= 0 and int(x[1]) < int(outlines[String(x[0])])))
	(_items["seam"] as Array).assign((_items["seam"] as Array).filter(func(x) -> bool: return int(x) >= 0 and int(x) < seam_count))
	(_items["hole"] as Array).assign((_items["hole"] as Array).filter(func(x) -> bool:
		return holes.has(String(x[0])) and int(x[1]) >= 0 and int(x[1]) < int(holes[String(x[0])])))
	(_items["guide"] as Array).assign((_items["guide"] as Array).filter(func(x) -> bool: return guides.has(_key("guide", x))))
	if not _hover.is_empty():
		var hk := String(_hover["kind"])
		var hid = _hover["id"]
		var gone := false
		match hk:
			"piece":
				gone = not outlines.has(String(hid))
			"edge", "corner":
				gone = not outlines.has(String(hid[0])) or int(hid[1]) >= int(outlines.get(String(hid[0]), 0))
			"seam":
				gone = int(hid) >= seam_count
			"hole":
				gone = int(hid[1]) >= int(holes.get(String(hid[0]), 0))
			"guide":
				gone = not guides.has(_key("guide", hid))
		if gone:
			_hover = {}
			hover_changed.emit()
	_emit_if_changed(before)


# ------------------------------------------------------------------ internals
func _clear_kinds() -> void:
	for k in KINDS:
		(_items[k] as Array).clear()


func _snapshot() -> String:
	return var_to_str(_items)


func _emit_if_changed(before: String) -> void:
	if _snapshot() != before:
		changed.emit()
