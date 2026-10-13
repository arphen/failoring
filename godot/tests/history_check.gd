# History and selection check (step 1 of the cutting table work): the action history (named actions, undo, redo, jump,
# the History panel rows, keyboard undo, survival across a rebuild) and the shared selection model (picks, hover,
# prune). Part one is pure data; part two drives the editor's own state and table. Run headless:
#   godot --headless --path godot res://tests/history_check.tscn
# Prints HISTCHECK lines and quits 0 when every check passes, 1 otherwise.
extends Node

const History = preload("res://history.gd")
const CutSelection = preload("res://selection.gd")

const SETTLE_S := 0.45
const TIMEOUT_S := 180.0

var fails := 0
var ed: Node
var t_start := 0.0


func _check(ok: bool, what: String) -> void:
	print("HISTCHECK %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


func _settle() -> void:
	await get_tree().create_timer(SETTLE_S).timeout


func _ready() -> void:
	t_start = Time.get_ticks_msec() / 1000.0
	_unit_history()
	_unit_selection()
	ed = load("res://editor.tscn").instantiate()
	add_child(ed)
	await _settle()
	while ed.sim == null and Time.get_ticks_msec() / 1000.0 - t_start < TIMEOUT_S:
		await get_tree().process_frame
	if ed.sim == null:
		print("HISTCHECK FAIL the editor did not boot a sim")
		get_tree().quit(1)
		return
	await _editor_part()
	print("HISTCHECK %s" % ("PASSED" if fails == 0 else "FAILED with %d failure(s)" % fails))
	remove_child(ed)
	ed.free()
	get_tree().quit(0 if fails == 0 else 1)


# ------------------------------------------------------------------ part one: the history data
func _unit_history() -> void:
	var h := History.new()
	_check(not h.can_undo() and not h.can_redo() and h.size() == 0, "an empty history has nothing to undo or redo")
	h.push("A", "s0", "s1")
	h.push("B", "s1", "s2")
	h.push("C", "s2", "s3")
	_check(h.cursor == 3 and h.size() == 3, "three actions put the cursor at state 3")
	_check(h.undo_name() == "C" and h.redo_name() == "", "undo names the last action, redo has none")
	_check(h.goto(h.cursor - 1) == "s2" and h.cursor == 2, "undo goes to the snapshot before C")
	_check(h.redo_name() == "C", "after undo the redo names C")
	_check(h.goto(3) == "s3", "redo goes to the snapshot after C")
	h.goto(1)
	_check(h.snapshot_at(0) == "s0" and h.goto(0) == "s0", "jump to the start gives the first before-snapshot")
	h.goto(1)
	h.push("D", "s1", "s1b")  # an edit in the middle drops the redo states B and C
	_check(h.size() == 2 and h.cursor == 2 and h.undo_name() == "D", "a new edit after undo drops the redo line")
	_check(h.snapshot_at(2) == "s1b" and h.snapshot_at(1) == "s1", "states follow the new line")
	h.clear()
	_check(h.size() == 0 and h.cursor == 0 and not h.can_undo(), "clear empties the history")
	# the cap keeps the start state consistent: after dropping old actions, state 0 is the snapshot they left behind
	var c := History.new()
	for i in History.MAX_ACTIONS + 5:
		c.push("n%d" % i, "v%d" % i, "v%d" % (i + 1))
	_check(c.size() == History.MAX_ACTIONS and c.cursor == History.MAX_ACTIONS, "the history is capped at %d actions" % History.MAX_ACTIONS)
	_check(c.snapshot_at(0) == "v5", "after the cap, the start is the state the dropped actions left (v5)")
	c.goto(0)
	_check(c.snapshot_at(0) == "v5" and c.snapshot_at(1) == "v6", "undo to the oldest kept state gives its snapshot")


# ------------------------------------------------------------------ part one: the selection data
func _unit_selection() -> void:
	var s := CutSelection.new()
	var n := [0]
	s.changed.connect(func() -> void: n[0] += 1)
	s.select("piece", "p1")
	_check(n[0] == 1 and s.primary_piece() == "p1", "selecting a piece emits changed and makes it primary")
	s.select("piece", "p1")
	_check(n[0] == 1, "selecting the same piece again emits nothing")
	s.add("piece", "p2")
	_check(s.primary_piece() == "p2" and s.count("piece") == 2, "add keeps both pieces, the last picked is primary")
	s.set_primary_corner(3)
	_check(s.primary_corner() == 3 and s.contains("corner", ["p2", 3]), "a corner belongs to the primary piece")
	s.replace("seam", 0)
	_check(s.contains("seam", 0) and s.count("piece") == 2, "replace picks a seam and keeps the pieces")
	s.toggle("seam", 0)
	_check(not s.contains("seam", 0), "toggle removes a picked seam")
	s.toggle("edge", ["p1", 2])
	_check(s.contains("edge", ["p1", 2]) and not s.contains("edge", ["p1", 1]), "edges are picked by piece and index")
	var pieces := [{"id": "p1", "outline": [[0, 0], [1, 0], [1, 1], [0, 1]]}, {"id": "p2", "outline": [[0, 0], [1, 0], [0, 1]]}]
	s.replace("seam", 1)  # replace keeps the pieces picked above (select would clear them)
	s.add("corner", ["p2", 2])
	s.prune(pieces, 1)  # seam 1 is past the end, corner 2 of p2 is valid, edge p1:2 is valid
	_check(not s.contains("seam", 1) and s.contains("corner", ["p2", 2]), "prune drops a seam index past the seam count")
	s.prune([pieces[0]], 1)
	_check(s.count("piece") == 1 and s.primary_piece() == "p1" and not s.contains("corner", ["p2", 2]), "prune drops a removed piece and its corners")
	var hn := [0]
	s.hover_changed.connect(func() -> void: hn[0] += 1)
	s.set_hover("edge", ["p1", 0])
	s.set_hover("edge", ["p1", 0])
	_check(hn[0] == 1 and s.hovers("edge", ["p1", 0]), "hover emits once per change")
	s.set_hover("piece", "p1")
	_check(hn[0] == 2 and s.hover_kind() == "piece", "a new hover replaces the old one")
	s.clear_hover()
	_check(hn[0] == 3 and not s.has_hover(), "clear_hover empties the hover")
	s.clear()
	_check(s.is_empty() and s.primary_piece() == null and s.primary_corner() == -1, "clear empties the selection")


# ------------------------------------------------------------------ part two: the editor
func _key(code: Key, ctrl := true, shift := false) -> InputEventKey:
	var ev := InputEventKey.new()
	ev.keycode = code
	ev.physical_keycode = code
	ev.pressed = true
	ev.ctrl_pressed = ctrl
	ev.shift_pressed = shift
	return ev


func _piece0() -> Dictionary:
	return ed.pieces[0]


func _history_rows() -> int:
	return ed.history_list.item_count


func _editor_part() -> void:
	var before_size: int = ed.history.size()
	var start_name := String(_piece0()["name"])
	var pid := String(_piece0()["id"])
	ed.sel = pid
	# one named edit: rename the piece, then end the edit
	ed.push_undo("Rename the test piece")
	_piece0()["name"] = "History test"
	ed.changed("structure")
	_check(ed.history.size() == before_size + 1, "an edit adds one action")
	_check(String(ed.history.entries[ed.history.size() - 1]["name"]) == "Rename the test piece", "the action has the name it was given")
	_check(ed.undo_depth() == ed.history.size(), "the cursor is at the newest action after an edit")
	# an edit that changes nothing is not recorded
	var n_before: int = ed.history.size()
	ed.push_undo("Nothing changed")
	ed.changed("structure")
	_check(ed.history.size() == n_before, "an edit that changes nothing is not recorded")
	# undo and redo through the keyboard path (the table's key handler)
	ed.table._unhandled_key_input(_key(KEY_Z))
	_check(String(_piece0()["name"]) == start_name, "Ctrl+Z restores the name")
	ed.table._unhandled_key_input(_key(KEY_Z, true, true))
	_check(String(_piece0()["name"]) == "History test", "Ctrl+Shift+Z redoes")
	ed.table._unhandled_key_input(_key(KEY_Z))
	ed.table._unhandled_key_input(_key(KEY_Y))
	_check(String(_piece0()["name"]) == "History test", "Ctrl+Y redoes")
	# the History panel lists the start and every action, and a row jumps to that state
	_check(_history_rows() == ed.history.size() + 1, "the panel has a start row and one row per action (%d rows)" % _history_rows())
	var last: int = ed.history.size()
	_check(String(ed.history_list.get_item_text(last)).find("Rename the test piece") >= 0, "the panel shows the action name")
	ed.jump_history(0)
	_check(String(_piece0()["name"]) == start_name and ed.undo_depth() == 0, "jumping to the start row restores the start")
	ed.jump_history(last)
	_check(String(_piece0()["name"]) == "History test" and ed.undo_depth() == last, "jumping to the last row redoes everything")
	# a rebuild (a body change, a debounced regen) keeps the history
	var size_now: int = ed.history.size()
	ed.changed("body")
	await _settle()
	_check(ed.history.size() == size_now, "a rebuild keeps the history (%d actions)" % ed.history.size())
	# a selected seam that is deleted by undo is pruned from the selection, and no error is raised
	ed.push_undo("Set the test piece back")
	_piece0()["name"] = start_name
	ed.changed("structure")
	ed.selection.select("seam", 99)  # no garment has 100 seams: this pick must not survive an undo
	ed.do_undo()
	_check(not ed.selection.contains("seam", 99), "undo prunes picks that no longer exist")
	# undo back past everything the test added, so the garment is as it was
	ed.jump_history(0)
	_check(String(_piece0()["name"]) == start_name, "back at the start, the test piece has its original name")
