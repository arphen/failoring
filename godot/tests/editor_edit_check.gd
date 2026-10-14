# Editor edit check: a drafted garment (T-shirt) is edited on the cutting table, then the body changes, the debounced
# rebuild runs, and the edit must survive (P0-1). Also checks the undo fixes and the edit guards. Run headless:
#   godot --headless --path godot res://tests/editor_edit_check.tscn
# Prints EDITCHECK lines and quits 0 when every check passes, 1 otherwise. Needs libfailoring (see docs/NATIVE_NOTES.md).
extends Node

const Table = preload("res://table.gd")
const Seams = preload("res://seams.gd")
const BodyGen = preload("res://body_gen.gd")
const SETTLE_S := 0.45  # the editor's debounce is 0.25 s; the rebuild runs inside it
const TIMEOUT_S := 180.0

var fails := 0
var ed: Node
var t_start := 0.0


func _check(ok: bool, what: String) -> void:
	print("EDITCHECK %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


func _settle() -> void:
	await get_tree().create_timer(SETTLE_S).timeout


func _timed_out() -> bool:
	return Time.get_ticks_msec() / 1000.0 - t_start > TIMEOUT_S


# Skips nodes queued for deletion: the tabs free their old controls at the end of the frame.
func _find_button(root: Node, text: String) -> Button:
	for c in root.get_children():
		if c.is_queued_for_deletion():
			continue
		if c is Button and (c as Button).text == text:
			return c
		var found := _find_button(c, text)
		if found != null:
			return found
	return null


# The slider of the Piece tab row whose label is `label_text` (rows are HBoxContainer: Label, HSlider, Label).
func _piece_slider(label_text: String) -> HSlider:
	for row in ed._tab_vbox("Piece").get_children():
		var found := _slider_in(row, label_text)
		if found != null:
			return found
	return null


func _slider_in(node: Node, label_text: String) -> HSlider:
	if node.is_queued_for_deletion():
		return null
	if node is HBoxContainer:
		var has_label := false
		var slider: HSlider = null
		for c in node.get_children():
			if c is Label and (c as Label).text == label_text:
				has_label = true
			if c is HSlider:
				slider = c
		if has_label and slider != null:
			return slider
	for c in node.get_children():
		var f := _slider_in(c, label_text)
		if f != null:
			return f
	return null


func _piece0() -> Dictionary:
	return ed.pieces[0]


func _outline_json() -> String:
	return JSON.stringify(_piece0()["outline"])


func _plan_index(key: String) -> int:
	var keys: Array = ed._plan_entries().map(func(e: Array) -> String: return e[1])
	return keys.find(key)


func _key_event(code: Key) -> InputEventKey:
	var ev := InputEventKey.new()
	ev.keycode = code
	ev.physical_keycode = code
	ev.pressed = true
	return ev


func _ready() -> void:
	t_start = Time.get_ticks_msec() / 1000.0
	ed = load("res://editor.tscn").instantiate()
	add_child(ed)
	await _settle()
	while ed.sim == null and not _timed_out():
		await get_tree().process_frame
	if _timed_out() or ed.sim == null:
		print("EDITCHECK FAIL the editor did not boot a sim")
		get_tree().quit(1)
		return
	await _run()
	print("EDITCHECK %s" % ("PASSED" if fails == 0 else "FAILED with %d failure(s)" % fails))
	remove_child(ed)
	ed.free()
	get_tree().quit(0 if fails == 0 else 1)


func _run() -> void:
	# --- set up a drafted garment (T-shirt) that was not edited yet
	ed.plan_opt.select(_plan_index("shirt"))
	ed.plan_opt.item_selected.emit(_plan_index("shirt"))
	await _settle()
	_check(String(ed.outfit[ed.active]["key"]) == "shirt", "T-shirt is the active garment")
	_check(String(ed.plan_name) == "T-shirt (drafted)", "the header name is the dropdown name (%s)" % ed.plan_name)
	_check(not bool(ed.outfit[ed.active]["edited"]), "a fresh draft is not edited")
	ed.sel = String(ed.pieces[0]["id"])
	ed._refresh_piece_tab()
	await get_tree().process_frame
	var before := _outline_json()
	var body_before := JSON.stringify(ed.meas)
	ed.meas = (BodyGen.PRESETS["tall"] as Dictionary).duplicate()
	ed._schedule()
	await _settle()
	_check(_outline_json() != before, "a body change re-drafts an unedited drafted garment")
	_check(JSON.stringify(ed.meas) != body_before, "body change applied")
	ed.history.clear()

	# --- P0-1: a structural edit survives the debounced rebuild
	before = _outline_json()
	ed._refresh_piece_tab()
	await get_tree().process_frame
	_find_button(ed._tab_vbox("Piece"), "Mirror").pressed.emit()
	var mirrored := _outline_json()
	_check(mirrored != before, "Mirror changed the outline")
	var sim_before = ed.sim
	await _settle()
	_check(ed.sim != sim_before, "the debounced rebuild ran after the edit")
	_check(_outline_json() == mirrored, "P0-1: the mirrored outline survives the rebuild")
	_check(bool(ed.outfit[ed.active]["edited"]), "the garment is marked edited")
	_check(ed.undo_depth() == 1, "the undo step survives the rebuild (%d)" % ed.undo_depth())

	# --- a body change keeps the edit and says so
	ed._on_preset_button("petite")
	await _settle()
	_check(_outline_json() == mirrored, "P0-1: a body change keeps the edited outline")
	_check(ed.undo_depth() == 1, "a body change does not clear undo")
	_check(String(ed.status_label.text).find("Body changed") >= 0, "the status line says the edits were kept")

	# --- undo and redo of the edit
	ed.do_undo()
	_check(_outline_json() == before, "undo restores the outline from before the Mirror")
	ed.do_redo()
	_check(_outline_json() == mirrored, "redo restores the Mirror")
	ed.do_undo()  # back to the draft for the rest of the checks

	# --- a piece slider drag is one undo step, and undo restores it
	ed.sel = String(ed.pieces[0]["id"])
	ed._refresh_piece_tab()
	await get_tree().process_frame
	var slider := _piece_slider("Around")
	_check(slider != null, "the Around slider is in the Piece tab")
	var theta_before := float((_piece0()["wrap"] as Dictionary)["theta"])
	var undo_before: int = ed.undo_depth()
	slider.drag_started.emit()
	slider.value = 90.0
	slider.value = 120.0
	slider.drag_ended.emit(true)
	_check(ed.undo_depth() == undo_before + 1, "a slider drag adds one undo step (%d -> %d)" % [undo_before, ed.undo_depth()])
	_check(is_equal_approx(float((_piece0()["wrap"] as Dictionary)["theta"]), 120.0), "the slider change applied")
	await _settle()
	ed.do_undo()
	_check(is_equal_approx(float((_piece0()["wrap"] as Dictionary)["theta"]), theta_before), "undo restores the slider value")
	_check(ed.undo_depth() == undo_before, "undo of the slider does not touch earlier steps")

	# --- deleting the selected piece is undoable, says so, and undo after it does not error
	var pid := String(ed.pieces[0]["id"])
	var n_pieces: int = ed.pieces.size()
	ed.sel = pid
	ed.sel_v = -1
	ed.table._unhandled_key_input(_key_event(KEY_DELETE))
	_check(ed.pieces.size() == n_pieces - 1, "Delete removes the selected piece")
	_check(String(ed.status_label.text).find("Ctrl+Z") >= 0, "the status line says the delete can be undone")
	_check(ed.sel == null, "nothing is selected after the delete")
	ed.do_undo()
	_check(ed.pieces.size() == n_pieces and Table.piece_by_id(ed.pieces, pid) != null, "undo restores the deleted piece")
	_check(ed.sel == null, "undo with no selection leaves it empty without an error")

	# --- Space is play/pause only
	var playing_before: bool = ed.playing
	ed.table._unhandled_key_input(_key_event(KEY_SPACE))
	_check(ed.playing != playing_before, "Space toggles playback")
	ed.table._unhandled_key_input(_key_event(KEY_SPACE))
	_check(ed.playing == playing_before, "Space toggles playback back")
	_check(ed.table.get("space_down") == null, "Space no longer pans the table")

	# --- switching the plan while edited asks first; cancelling keeps the edits and the dropdown
	ed.sel = pid
	ed._refresh_piece_tab()
	await get_tree().process_frame
	_find_button(ed._tab_vbox("Piece"), "Mirror").pressed.emit()
	await _settle()
	var edited_outline := _outline_json()
	var shirt_idx := _plan_index("shirt")
	var tank_idx := _plan_index("tank")
	ed.plan_opt.select(tank_idx)
	ed.plan_opt.item_selected.emit(tank_idx)
	var dlg := _find_dialog()
	_check(dlg != null, "switching plan while edited opens a confirmation")
	_check(String(ed.outfit[ed.active]["key"]) == "shirt", "the edited garment is still active before confirming")
	if dlg != null:
		dlg.canceled.emit()
		dlg.hide()  # the dialog closes itself on cancel or OK
		await get_tree().process_frame
	_check(ed.plan_opt.selected == shirt_idx, "cancelling puts the Plan dropdown back")
	_check(_outline_json() == edited_outline, "cancelling keeps the edit")

	# --- Re-draft asks first; confirming discards the edit and gives the fresh draft
	ed._on_redraft()
	dlg = _find_dialog()
	_check(dlg != null, "Re-draft while edited opens a confirmation")
	_check(_outline_json() == edited_outline, "Re-draft does not discard before confirming")
	if dlg != null:
		dlg.confirmed.emit()
		dlg.hide()
		await _settle()
	var fresh = ed._plan_for_key("shirt")["pieces"][0]["outline"]
	_check(JSON.stringify(fresh) == _outline_json(), "confirmed Re-draft gives the fresh draft")
	_check(not bool(ed.outfit[ed.active]["edited"]), "Re-draft clears the edited flag")

	# --- plan names: the dropdown and the header agree
	var fs_idx := _plan_index("fs-teagan")
	ed.plan_opt.select(fs_idx)
	ed.plan_opt.item_selected.emit(fs_idx)
	await _settle()
	_check(ed.outfit[ed.active]["name"] == ed.plan_opt.get_item_text(fs_idx), "FreeSewing tee: header name matches the dropdown (%s)" % ed.outfit[ed.active]["name"])

	# --- helpers that the table and seams use: counts of seams dropped or replaced
	var edref := {"pieces": [{"id": "a", "outline": [[0, 0], [1, 0], [1, 1], [0, 1]]}, {"id": "b", "outline": [[0, 0], [1, 0], [1, 1], [0, 1]]}],
		"seams": [], "sel": null, "sel_v": -1}
	_check(Table.sew_edges(edref, ["a", 0], ["b", 2]) == 0, "sewing two free edges replaces nothing")
	_check(Table.sew_edges(edref, ["a", 0], ["b", 1]) == 1, "re-sewing an edge reports the seam it replaced")
	_check(edref["seams"].size() == 1, "re-sewing leaves one seam")
	_check(Table.insert_vertex(edref, edref["pieces"][1], 2, [0.5, 1.0]) == 0, "splitting an edge with no seam drops none")
	_check(Table.insert_vertex(edref, edref["pieces"][1], 0, [0.5, -0.1]) == 0 and edref["seams"].size() == 1, "splitting an earlier edge keeps the seam")
	# splitting a sewn edge keeps the seam: it is cut at the new corner into one seam per sub-edge, and the partner's range
	# is cut at the same place (from its other end: sew_edges makes reversed seams). Was: the seam was dropped (changed
	# 2026-10-14 so a corner can be added on a sewn edge, as the user asked).
	var ed2 := {"pieces": [{"id": "a", "outline": [[0, 0], [1, 0], [1, 1], [0, 1]]}, {"id": "b", "outline": [[0, 0], [1, 0], [1, 1], [0, 1]]}],
		"seams": [], "sel": null, "sel_v": -1}
	Table.sew_edges(ed2, ["a", 0], ["b", 2])
	_check(Table.insert_vertex(ed2, ed2["pieces"][1], 2, [0.5, 1.0]) == 0, "splitting the seamed edge drops no seam")
	var sp: Array = ed2["seams"]
	_check(sp.size() == 2, "the split seam becomes two seams (%d)" % sp.size())
	if sp.size() == 2:
		var by_b := {}
		for s2 in sp:
			by_b[int(s2["b"][1])] = s2
		_check(by_b.has(2) and by_b.has(3) and Seams.is_full(by_b[2]["b"]) and Seams.is_full(by_b[3]["b"]), "each half of b's edge is sewn whole (edges 2 and 3)")
		var r2 := Seams.side_range(by_b[2]["a"])
		var r3 := Seams.side_range(by_b[3]["a"])
		_check(r2.is_equal_approx(Vector2(0.5, 1.0)) and r3.is_equal_approx(Vector2(0.0, 0.5)), "a's edge is cut at its middle, halves matched in reverse (%s, %s)" % [r2, r3])
		_check(bool(by_b[2].get("reverse", false)) and bool(by_b[3].get("reverse", false)), "both halves keep the seam's direction")
	# a range that ends before the new corner moves onto the first sub-edge with its fractions rescaled
	var ed3 := {"pieces": [{"id": "a", "outline": [[0, 0], [1, 0], [1, 1], [0, 1]]}, {"id": "b", "outline": [[0, 0], [1, 0], [1, 1], [0, 1]]}],
		"seams": [{"a": ["a", 0, 0.0, 0.25], "b": ["b", 0], "reverse": false}], "sel": null, "sel_v": -1}
	Table.insert_vertex(ed3, ed3["pieces"][0], 0, [0.5, 0.0])
	_check(ed3["seams"].size() == 1 and Seams.side_range(ed3["seams"][0]["a"]).is_equal_approx(Vector2(0.0, 0.5)) and int(ed3["seams"][0]["a"][1]) == 0, "a range before the new corner is rescaled onto the first half")
	Table.sew_edges(edref, ["a", 0], ["b", 3])
	_check(Table.remove_vertex(edref["pieces"][0], 0, edref["seams"]) == 1, "deleting a corner reports the seam on its edges")
	_check((edref["pieces"][0]["outline"] as Array).size() == 3, "the corner is gone")
	_check(Table.remove_piece(edref, "b") == 0, "deleting a piece with no seams left reports none")


func _find_dialog() -> ConfirmationDialog:
	for c in ed.get_children():
		if c is ConfirmationDialog and (c as Window).visible:
			return c
	return null
