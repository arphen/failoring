# Draft preview while editing: with the option on, an edit rebuilds the sim at Draft quality, and Play rebuilds it at the
# chosen quality before the walk starts. Run headless:
#   godot --headless --path godot res://tests/draft_edit_check.tscn
# Prints DRAFT lines and quits 0 when every check passes, 1 otherwise.
extends Node

var fails := 0
var ed: Node
var phase := 0
var t_start := 0.0


func _check(ok: bool, what: String) -> void:
	print("DRAFT %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


func _ready() -> void:
	t_start = Time.get_ticks_msec() / 1000.0
	ed = load("res://editor.tscn").instantiate()
	add_child(ed)


func _process(_delta: float) -> void:
	if Time.get_ticks_msec() / 1000.0 - t_start > 240.0:
		print("DRAFT FAIL timeout in phase %d" % phase)
		get_tree().quit(1)
		return
	if phase == 0 and ed.sim != null:
		_check(ed.quality == "preview", "the default quality is preview")
		var chosen_n: int = ed.sim.get_vertex_count()
		ed.draft_while_editing = true
		ed._on_debounce()  # an edit
		var draft_n: int = ed.sim.get_vertex_count()
		_check(ed._qual_key() == "draft", "an edit with the option on previews at Draft")
		_check(draft_n < chosen_n, "the Draft preview has fewer vertices (%d < %d)" % [draft_n, chosen_n])
		ed._on_play()  # Play rebuilds at the chosen quality before the walk
		_check(ed._qual_key() == ed.quality, "Play leaves the Draft preview")
		_check(ed.sim.get_vertex_count() == chosen_n, "Play rebuilds at the chosen quality (%d vertices)" % ed.sim.get_vertex_count())
		_check(ed.playing, "playback starts")
		ed.playing = false
		phase = 1
	elif phase == 1:
		remove_child(ed)
		ed.free()
		print("DRAFT %s" % ("PASSED" if fails == 0 else "FAILED with %d failure(s)" % fails))
		get_tree().quit(0 if fails == 0 else 1)
