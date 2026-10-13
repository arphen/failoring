# Action history for the cutting table: named, snapshot-based actions with a real undo and redo stack.
#
# Each edit is one action {name, before, after}: `before` and `after` are the garment's pattern snapshots (pieces and
# seams as JSON text) around the edit. The history is a line of states: state 0 is the start (`base`), state s is the
# snapshot after action s-1. `cursor` is the state on screen. Undo moves the cursor back one state, redo forward one,
# and jumping to any state (the History panel) is the same move. An edit made while the cursor is back in the line
# drops the states after it (the usual undo-tree-free behaviour).
#
# One history per garment (editor.gd keeps it in the outfit entry), so a rebuild, a body change or a debounced regen
# never touches it. Only a garment switch, a new plan or a re-draft starts a new line.
extends RefCounted

const MAX_ACTIONS := 200

var base: String = ""        # the snapshot of state 0
var entries: Array = []      # [{name, before, after}], oldest first
var cursor: int = 0          # the current state, 0 .. entries.size()


# The snapshot of state s (0 .. entries.size()).
func snapshot_at(s: int) -> String:
	if s <= 0:
		return base
	return String(entries[s - 1]["after"])


# Records one action that took the garment from `before` to `after`. Drops the redo states first.
func push(name: String, before: String, after: String) -> void:
	if cursor == 0:
		base = before  # the state on screen is the start: it is the new base, and every later state is dropped
		entries.clear()
	else:
		entries.resize(cursor)
	entries.append({"name": name, "before": before, "after": after})
	cursor = entries.size()
	while entries.size() > MAX_ACTIONS:
		base = String(entries[0]["after"])
		entries.pop_front()
		cursor -= 1


func can_undo() -> bool:
	return cursor > 0


func can_redo() -> bool:
	return cursor < entries.size()


# Moves the cursor to state s and returns its snapshot ("" when s is out of range).
func goto(s: int) -> String:
	if s < 0 or s > entries.size():
		return ""
	cursor = s
	return snapshot_at(s)


func undo_name() -> String:
	return String(entries[cursor - 1]["name"]) if can_undo() else ""


func redo_name() -> String:
	return String(entries[cursor]["name"]) if can_redo() else ""


func clear() -> void:
	base = ""
	entries = []
	cursor = 0


func size() -> int:
	return entries.size()
