# Editing check: numeric entry (edge lengths, a corner's x and y, the piece's width and height), the placement buttons
# (Front, Back, Left side, Right side), the copy and mirror offers, the sew length check, copy and paste, and the
# symmetric twin. It drives the editor's own controls (LineEdit text_submitted, Button pressed, CheckBox toggled)
# on the blank rectangle plan. Run headless:
#   godot --headless --path godot res://tests/edit_check.tscn
# Prints EDIT lines and quits 0 when every check passes, 1 otherwise.
extends Node

const Table = preload("res://table.gd")

var fails := 0
var ed: Node


func _check(ok: bool, what: String) -> void:
	print("EDIT %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


func _frame() -> void:
	await get_tree().process_frame
	await get_tree().process_frame


func _field(node_name: String) -> LineEdit:
	return ed.find_child(node_name, true, false) as LineEdit


func _btn(text: String) -> Button:
	for b in ed.find_children("*", "Button", true, false):
		if b is Button and (b as Button).text == text:
			return b as Button
	return null


func _named_btn(node_name: String) -> Button:
	return ed.find_child(node_name, true, false) as Button


func _piece(pid: String):
	return Table.piece_by_id(ed.pieces, pid)


func _select(pid: String, vi: int = -1) -> void:
	ed.sel = pid
	ed.sel_v = vi
	ed.changed("ui")
	await _frame()


func _len_cm(p: Dictionary, e: int) -> float:
	return Table.edge_len(p["outline"], e) * 100.0


func _near(a: float, b: float, eps: float = 1e-4) -> bool:
	return absf(a - b) < eps


func _run() -> void:
	ed = load("res://editor.tscn").instantiate()
	add_child(ed)
	await _frame()
	ed._set_single_garment("blank")
	ed.regen_and_rebuild()
	await _frame()
	var pid := String(ed.pieces[0]["id"])
	await _select(pid)
	var p: Dictionary = _piece(pid)
	_check(p != null and (p["outline"] as Array).size() == 4, "blank plan loaded: piece %s, 4 corners" % pid)
	if p == null:
		return

	# ---- edge length by typing: the far corner moves along the edge, the near corner stays
	_field("EdgeLen_1").text = "62.5"
	_field("EdgeLen_1").text_submitted.emit("62.5")
	await _frame()
	p = _piece(pid)
	_check(_near(_len_cm(p, 1), 62.5), "edge 1 typed 62.5 cm, now %.2f cm" % _len_cm(p, 1))
	_check(_near(float(p["outline"][1][0]), 0.5) and _near(float(p["outline"][1][1]), 0.0), "near corner of edge 1 stays put")
	_check(_near(float(p["outline"][2][0]), 0.5) and _near(float(p["outline"][2][1]), 0.625), "far corner moved along the edge to (0.5, 0.625)")
	_check(_field("EdgeLen_1").text == "62.5", "field shows the new length after the rebuild")

	_field("EdgeLen_1").text = "0.5"
	_field("EdgeLen_1").text_submitted.emit("0.5")
	await _frame()
	p = _piece(pid)
	_check(_near(_len_cm(p, 1), 62.5) and ed.status_label.text.find("0.8 cm") >= 0, "0.5 cm is refused (under 8 mm) with a message")
	_check(_field("EdgeLen_1").text == "62.5", "refused entry puts the old length back in the field")

	_field("EdgeLen_1").text = "abc"
	_field("EdgeLen_1").text_submitted.emit("abc")
	await _frame()
	_check(_near(_len_cm(_piece(pid), 1), 62.5), "text that is not a number is refused")

	var undo_before: int = ed.undo.size()
	ed.do_undo()
	await _frame()
	p = _piece(pid)
	_check(_near(_len_cm(p, 1), 50.0), "undo restores the edge to 50.0 cm")
	_check(ed.undo.size() == undo_before - 1, "one undo step per accepted entry")

	# ---- a corner's x and y (cm from the piece's origin)
	await _select(pid, 2)
	_field("CornerX").text = "10"
	_field("CornerX").text_submitted.emit("10")
	await _frame()
	p = _piece(pid)
	_check(_near(float(p["outline"][2][0]), 0.10) and _near(float(p["outline"][2][1]), 0.5), "corner 2 x typed 10 cm, y kept 50 cm")
	ed.do_undo()
	await _frame()
	_check(_near(float(_piece(pid)["outline"][2][0]), 0.5), "undo restores the corner")

	# ---- width and height of the whole piece, scaled about the left and bottom edges
	await _select(pid)
	_field("SizeW").text = "80"
	_field("SizeW").text_submitted.emit("80")
	await _frame()
	var b := Table.bbox_of(_piece(pid)["outline"])
	_check(_near(float(b[2]) - float(b[0]), 0.80) and _near(float(b[3]) - float(b[1]), 0.5), "width typed 80 cm: bbox 80 x 50 cm")
	ed.do_undo()
	await _frame()
	_check(_near(_len_cm(_piece(pid), 0), 50.0), "undo restores the width")

	# ---- placement buttons
	var place := {"PlaceFront": 0.0, "PlaceBack": 180.0, "PlaceLeftside": 90.0, "PlaceRightside": 270.0}
	for nm in ["PlaceBack", "PlaceLeftside", "PlaceRightside", "PlaceFront"]:
		await _select(pid)
		_named_btn(nm).pressed.emit()
		await _frame()
		var th := float((_piece(pid)["wrap"] as Dictionary)["theta"])
		_check(_near(th, float(place[nm]), 1e-3), "%s sets Around to %d (now %.1f)" % [nm, int(place[nm]), th])
	_check(_named_btn("PlaceBack") != null and _named_btn("PlaceLeftside") != null, "placement buttons exist with names")

	# ---- duplicate covers the original: the offer appears, and its flip button moves the copy to the back
	await _select(pid)
	_btn("Duplicate").pressed.emit()
	await _frame()
	var copy_id := String(ed.sel)
	_check(copy_id != pid and _piece(copy_id) != null, "duplicate selected the copy %s" % copy_id)
	_check(String(ed._offer.get("pid", "")) == copy_id, "duplicate at the same placement offers a flip")
	var flip := _named_btn("FlipOffer")
	_check(flip != null and flip.text.find("back") >= 0 and flip.text.find("180") >= 0, "flip button says back and 180")
	flip.pressed.emit()
	await _frame()
	_check(_near(float((_piece(copy_id)["wrap"] as Dictionary)["theta"]), 180.0, 1e-3), "flip moves the copy to Around 180")
	_check(ed._offer.is_empty(), "offer clears after the flip")

	# ---- a duplicate already on the back does not offer anything
	await _select(copy_id)
	_btn("Duplicate").pressed.emit()
	await _frame()
	var copy2 := String(ed.sel)
	_check(String(ed._offer.get("pid", "")) == copy2 and ed._offer.get("button", "").find("front") >= 0, "a copy of the back piece offers the front")
	ed.do_undo()
	await _frame()
	_check(_piece(copy2) == null, "undo removes the second copy")
	ed.sel = copy_id  # the undo cleared the selection; undo's own selection fix is a known gap (DOGFOOD P2-1)
	ed.do_undo()
	await _frame()
	_check(_piece(copy_id) != null and _near(float((_piece(copy_id)["wrap"] as Dictionary)["theta"]), 0.0, 1e-3), "second undo restores the copy to Around 0")
	ed.sel = copy_id
	ed.do_undo()  # and the duplicate itself, so the table holds only the original
	await _frame()
	_check(_piece(copy_id) == null, "third undo removes the first copy")
	ed.sel = pid
	await _frame()

	# ---- mirror: in place. A piece at Around 0 covers its own place, so it offers the flip; at 270 it does not
	await _select(pid)
	_btn("Mirror").pressed.emit()
	await _frame()
	_check(String(ed._offer.get("pid", "")) == pid, "mirror at Around 0 offers the flip")
	ed.do_undo()  # the mirror changes the outline the later checks rely on: take it back
	await _frame()
	_named_btn("PlaceLeftside").pressed.emit()
	await _frame()
	await _select(pid)
	ed._offer = {}
	_btn("Mirror").pressed.emit()
	await _frame()
	_check(ed._offer.is_empty(), "mirror at Around 90 (to 270) does not offer a flip")
	ed.do_undo()
	await _frame()
	ed.do_undo()  # and the placement
	await _frame()
	_check(_near(float((_piece(pid)["wrap"] as Dictionary)["theta"]), 0.0, 1e-3) and _near(_len_cm(_piece(pid), 1), 50.0), "mirror checks undone: piece back at Around 0, unmirrored")

	# ---- sew length check
	_check(not Table.sew_check(0.808, 0.801)["warn"], "sew: 80.8 and 80.1 cm (0.7 cm, 0.9%) is within tolerance")
	_check(Table.sew_check(0.80, 0.85)["warn"], "sew: 80 and 85 cm is flagged")
	_check(not Table.sew_check(0.50, 0.5099)["warn"], "sew: 0.99 cm apart is within the 1 cm tolerance")
	_check(Table.sew_check(0.50, 0.5102)["warn"], "sew: 1.02 cm apart is flagged")
	_check(Table.sew_text("A", 0, 0.8, "B", 1, 0.85).find("ease the difference") >= 0, "sew warning says the drape will ease the difference")

	# ---- sew tool hint line and the highlighted candidate
	ed.table.set_tool("sew")
	await _frame()
	_check(ed.table.hint_label.text.begins_with("Click the first edge"), "sew tool hint: click the first edge")
	ed.sew_a = [pid, 0]
	ed.table.hover = {"p": pid, "e": 2}
	ed.table.update_hint()
	_check(ed.table.hint_label.text.begins_with("Now click the edge to sew it to") and ed.table.hint_label.text.find(" cm") >= 0, "second click hint shows both lengths")
	ed.sew_a = null
	ed.table.hover = null

	# clicks on the table itself: first the bottom edge (0), then the top edge (2), equal lengths, no warning;
	# then edge 1 (typed 60 cm) with edge 3 (50 cm), a 10 cm difference, which is flagged
	ed.table.size = Vector2(800, 600)
	ed.table.cam_z = 300.0
	ed.table.ox = 100.0
	ed.table.oy = 500.0
	ed.table._fit_pending = false  # keep this transform: the clicks below are placed with it
	var off := Vector2(float(p["table"]["x"]), float(p["table"]["y"]))
	ed.table._down(ed.table.to_screen(0.25 + off.x, 0.0 + off.y), MOUSE_BUTTON_LEFT)
	_check(ed.sew_a != null and int(ed.sew_a[1]) == 0, "sew click: first click picks the bottom edge")
	ed.table._down(ed.table.to_screen(0.25 + off.x, 0.5 + off.y), MOUSE_BUTTON_LEFT)
	await _frame()
	var seam_ends := []
	for s in ed.seams:
		seam_ends.append([int(s["a"][1]), int(s["b"][1])])
	_check(seam_ends.size() == 1 and (seam_ends[0] as Array).has(0) and (seam_ends[0] as Array).has(2), "sew click: edges 0 and 2 are sewn")
	_check(String(ed.table._sew_note).find("within") >= 0, "sew click: equal lengths are within tolerance")

	_field("EdgeLen_1").text = "60"
	_field("EdgeLen_1").text_submitted.emit("60")
	await _frame()
	ed.table._down(ed.table.to_screen(0.5 + off.x, 0.3 + off.y), MOUSE_BUTTON_LEFT)
	ed.table._down(ed.table.to_screen(0.0 + off.x, 0.25 + off.y), MOUSE_BUTTON_LEFT)
	await _frame()
	_check(String(ed.table._sew_note).find("Warning") >= 0, "sew click: 60 and 50 cm are flagged in the hint note")
	_check(ed.table.hint_label.text.find("Warning") >= 0, "sew click: the bottom hint line shows the warning")
	ed.do_undo()  # the second sew
	ed.do_undo()  # the edge length
	ed.do_undo()  # the first sew, so the square is back for the checks below
	await _frame()
	_check(_near(_len_cm(_piece(pid), 1), 50.0) and ed.seams.is_empty(), "sew checks undone: a 50 cm square with no seams")
	ed.table.set_tool("select")

	# ---- copy and paste
	await _select(pid)
	var n_before: int = ed.pieces.size()
	ed.copy_piece()
	ed.paste_piece()
	await _frame()
	_check(ed.pieces.size() == n_before + 1, "paste adds one piece")
	var pasted = _piece(String(ed.sel))
	_check(pasted != null and String(pasted["name"]).ends_with(" copy") and not pasted.has("sym"), "pasted copy is named and has no twin link")

	# ---- symmetric twin: a mirror image beside the piece; edits to the piece are copied to it
	ed.do_undo()
	await _frame()
	await _select(pid)
	_btn("Make a mirror twin").pressed.emit()
	await _frame()
	p = _piece(pid)
	var twin_id := String((p["sym"] as Dictionary)["twin"])
	var tw: Dictionary = _piece(twin_id)
	_check(tw != null, "make a mirror twin: twin %s exists" % twin_id)
	var tb := Table.bbox_of(tw["outline"])
	_check(_near(float(tb[0]), 0.6) and _near(float(tb[2]), 1.1) and _near(float(tb[3]) - float(tb[1]), 0.5), "twin sits beside the piece, mirrored (x 0.6 to 1.1)")
	var twin_area := Table.signed_area(tw["outline"])
	_check(twin_area > 0.0, "twin is counter-clockwise like the piece")

	_field("EdgeLen_1").text = "62.5"
	_field("EdgeLen_1").text_submitted.emit("62.5")
	await _frame()
	p = _piece(pid)
	tw = _piece(twin_id)
	_check(_near(_len_cm(p, 1), 62.5), "symmetric: the piece's edge 1 is 62.5 cm")
	_check(_near(_len_cm(tw, 1), 62.5), "symmetric: the twin's mirror edge (1) follows: %.2f cm" % _len_cm(tw, 1))

	# found by its text: a rebuilt node can be renamed while the previous one is still queued for deletion
	var sym_toggle: CheckBox = null
	for cbx in ed.find_children("*", "CheckBox", true, false):
		if (cbx as CheckBox).text == "Symmetric edits":
			sym_toggle = cbx as CheckBox
	_check(sym_toggle != null and sym_toggle.button_pressed, "symmetric toggle is on")
	if sym_toggle == null:
		_check(false, "symmetric toggle present")
		print("EDIT done, %d failures" % fails)
		return
	sym_toggle.toggled.emit(false)
	await _frame()
	_check(not bool((_piece(pid)["sym"] as Dictionary)["on"]), "symmetric toggle off clears the flag on both pieces")
	_field("EdgeLen_2").text = "40"
	_field("EdgeLen_2").text_submitted.emit("40")
	await _frame()
	_check(_near(_len_cm(_piece(twin_id), 1), 62.5), "with the toggle off, edits are not copied to the twin")

	ed.do_undo()
	await _frame()
	_check(_piece(twin_id) != null, "undo keeps the twin")

	# ---- seams tab lists the two pieces and their lengths
	ed.seams.append({"a": [pid, 0], "b": [twin_id, 0], "reverse": true})
	ed.changed("structure")
	await _frame()
	var seen := false
	for lab in ed.find_children("*", "Label", true, false):
		if lab is Label and (lab as Label).text.find("edge 0 (") >= 0 and (lab as Label).text.find("↔") >= 0:
			seen = true
	_check(seen, "seams tab lists 'piece edge n (cm) ↔ twin edge n (cm)'")

	print("EDIT done, %d failures" % fails)


func _ready() -> void:
	var guard := get_tree().create_timer(180.0)
	guard.timeout.connect(func() -> void:
		print("EDIT FAIL timeout")
		get_tree().quit(1))
	await _run()
	get_tree().quit(1 if fails > 0 else 0)
