# Checks the background step (editor.gd _submit_step / _poll_step / _drain). Run it twice and compare the output:
#   FL_SYNC=1 godot --headless --path godot res://tests/step_thread_check.tscn     (synchronous path)
#   FL_THREAD=1 godot --headless --path godot res://tests/step_thread_check.tscn   (worker thread)
# Both must print the same STEP lines. The run plays 30 frames, restarts the sim while a step may be in flight (the
# restart drains it), plays 30 more, then frees the editor (its worker must stop cleanly).
extends Node3D

var ed: Node
var phase := 0
var t_start := 0.0
var hashes_before: Array = []

func _ready() -> void:
	ed = load("res://editor.tscn").instantiate()
	add_child(ed)
	ed.tl_t = 3.0
	ed.playing = true
	t_start = Time.get_ticks_msec() / 1000.0

func _process(_delta: float) -> void:
	if Time.get_ticks_msec() / 1000.0 - t_start > 150.0:
		print("STEP timeout at phase %d, frames %d" % [phase, ed.frames.size()])
		get_tree().quit(1)
		return
	if phase == 0 and ed.frames.size() >= 30:
		ed.playing = false
		hashes_before = _hashes().slice(0, 30)  # frames may overshoot by a tick: compare the first 30
		ed.regen_and_rebuild()          # drains any in-flight step, then rebuilds the sim
		ed.tl_t = 3.0
		ed.playing = true
		phase = 1
	elif phase == 1 and ed.frames.size() >= 30:
		ed.playing = false
		print("STEP first run: %d frames" % hashes_before.size())
		for i in hashes_before.size():
			print("STEP a %d %d" % [i, hashes_before[i]])
		for i in mini(30, ed.frames.size()):
			print("STEP b %d %d" % [i, _hash(ed.frames[i])])
		phase = 2
	elif phase == 2:
		remove_child(ed)                # _exit_tree stops the worker (if any)
		ed.free()
		print("STEP OK")
		get_tree().quit(0)

func _hash(fr: Dictionary) -> int:
	return hash([fr["pos"], fr["cloth"], fr["body"]])

func _hashes() -> Array:
	var out: Array = []
	for fr in ed.frames:
		out.append(_hash(fr))
	return out
