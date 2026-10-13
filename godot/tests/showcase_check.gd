# Showcase check (docs/SCENARIOS.md): the Showcase tab in the editor renders the catwalk at Draft for the garment on the
# mannequin, the render is cached on disk and in memory, playback replays it without the editor's solver (the editor's
# sim does not step), scrubbing and leaving playback work. Run headless (a minute or two):
#   godot --headless --path godot res://tests/showcase_check.tscn      (SHOWCHECK lines, then the result; exit 0 = pass)
extends Node

const TIMEOUT_RENDER := 240.0
const TIMEOUT_BOOT := 240.0

var fails := 0
var ed: Node
var panel
var t0 := 0.0


func _check(ok: bool, what: String) -> void:
	print("SHOWCHECK %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


func _now() -> float:
	return Time.get_ticks_msec() / 1000.0


func _ready() -> void:
	_run()


func _run() -> void:
	ed = load("res://editor.tscn").instantiate()
	add_child(ed)
	t0 = _now()
	while ed.sim == null and _now() - t0 < TIMEOUT_BOOT:
		await get_tree().physics_frame
	await get_tree().physics_frame
	_check(ed.sim != null, "editor built a garment")
	panel = ed._showcase
	_check(panel != null, "the Showcase panel exists")
	_check(ed.tabs.get_node_or_null("Showcase") != null, "the Showcase tab exists")
	var inputs: Dictionary = ed.showcase_inputs("draft")
	_check(not inputs.is_empty() and (inputs["plan"]["pieces"] as Array).size() > 0, "showcase inputs describe the garment")

	# render the catwalk at Draft
	panel._scen_opt.select(0)
	panel._qual_opt.select(0)
	panel._on_render()
	t0 = _now()
	while panel._pre != null and _now() - t0 < TIMEOUT_RENDER:
		await get_tree().physics_frame
	_check(panel._pre == null, "the render finished (%.0f s)" % (_now() - t0))
	var key: String = ShowcasePrerender.key_for(inputs, panel._scenario())
	_check(panel._mem.has(key), "the render is kept in memory")
	_check(FileAccess.file_exists(ShowcasePrerender.file_for(key)), "the render is cached on disk")

	# play: the editor's solver must not step while the showcase plays
	var sim_frames_before: int = ed.sim.get_frames()
	panel._on_play()
	_check(panel._view and panel._playing, "playback is on")
	var t_start: float = panel._t
	for _i in 30:
		await get_tree().physics_frame
	_check(panel._t > t_start + 0.2, "the playhead advances (%.2f -> %.2f s)" % [t_start, panel._t])
	_check(ed.sim.get_frames() == sim_frames_before, "the editor's solver is not stepped during playback")
	_check(ed.body_node.get_mesh() != null, "the body is drawn during playback")

	# scrub and leave
	panel._on_seek(0.4)
	_check(absf(panel._t - 0.4) < 1e-6, "scrubbing moves the playhead")
	panel._set_playing(false)
	for _i in 3:
		await get_tree().physics_frame
	_check(not panel._playing and panel._view, "pause keeps the frame on the view")
	panel._exit_view()
	_check(not panel._view, "back to editing hands the view back")
	for _i in 3:
		await get_tree().physics_frame
	_check(ed.sim != null, "the editor is still running")

	# the box through the whole render: the box is carried up and down, the cloth stays finite
	panel._scen_opt.select(3)
	panel._qual_opt.select(0)
	panel._on_render()
	t0 = _now()
	while panel._pre != null and _now() - t0 < TIMEOUT_RENDER:
		await get_tree().physics_frame
	_check(panel._pre == null, "the box render finished (%.0f s)" % (_now() - t0))
	var box_key: String = ShowcasePrerender.key_for(inputs, panel._scenario())
	var box_r: Dictionary = panel._mem.get(box_key, {})
	_check(not box_r.is_empty(), "the box render is kept")
	if not box_r.is_empty():
		var top := -INF
		var low_after := INF
		var finite := true
		for i in (box_r["frames"] as Array).size():
			var fr: Dictionary = box_r["frames"][i]
			var y: float = (fr["box"] as Vector3).y
			if i > 0 and float(box_r["lead"]) < i:
				top = maxf(top, y)
			if i == (box_r["frames"] as Array).size() - 1:
				low_after = y
			for v in (fr["pos"] as PackedFloat32Array):
				if is_nan(v) or is_inf(v) or absf(v) > 5.0:
					finite = false
		_check(top > 0.6, "the box is lifted to %.2f m" % top)
		_check(low_after < 0.2, "the box is put back down (%.2f m)" % low_after)
		_check(finite, "the cloth stays finite through the box scenario")

	# Preview, the panel's default quality, for the catwalk
	panel._scen_opt.select(0)
	panel._qual_opt.select(1)
	panel._on_render()
	t0 = _now()
	while panel._pre != null and _now() - t0 < TIMEOUT_RENDER:
		await get_tree().physics_frame
	_check(panel._pre == null and panel._mem.size() >= 3, "the Preview catwalk render finished (%.0f s)" % (_now() - t0))
	# leave the user's cache as it was: the renders of this run are removed
	for k in panel._mem.keys():
		var p := ShowcasePrerender.file_for(String(k))
		if FileAccess.file_exists(p):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(p))
	print("RESULT: %d failed" % fails)
	get_tree().quit(1 if fails > 0 else 0)
