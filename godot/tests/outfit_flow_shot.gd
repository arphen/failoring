# The user's flow, scripted with a window (screenshots into FL_SHOTDIR): the default tee removed, the crop top and the
# tiered skirt added, the timeline played to the drape, front/back/three-quarter looks, then Render. Prints what it saw.
#   FL_SHOTDIR=/tmp/x godot --path godot res://tests/outfit_flow_shot.tscn
extends Node

var ed: Node
var dir := ""


func _shot(name: String) -> void:
	await RenderingServer.frame_post_draw
	var img := get_viewport().get_texture().get_image()
	img.save_png("%s/%s.png" % [dir, name])
	print("FLOW shot %s | %s | %s" % [name, ed.status_label.text.replace("\n", " / "), ed.health_text])


func _wait(sec: float) -> void:
	await get_tree().create_timer(sec).timeout


func _ready() -> void:
	dir = OS.get_environment("FL_SHOTDIR")
	get_window().always_on_top = true   # macOS stops drawing a covered window, and the shots would wait for ever
	ed = load("res://editor.tscn").instantiate()
	add_child(ed)
	while ed.sim == null:
		await get_tree().process_frame
	await _wait(2.0)
	await _shot("0_start")
	var t0 := Time.get_ticks_msec()
	# 1. remove the tee
	for i in ed.outfit.size():
		print("FLOW outfit %d: %s" % [i, ed.outfit[i]["name"]])
	ed._outfit_remove_now(0)
	await _wait(1.5)
	print("FLOW removed: outfit=%d cloth visible=%s" % [ed.outfit.size(), ed.cloth_node.visible])
	await _shot("1_removed")
	# 2. crop top and tiered skirt
	ed._on_library_add("crop")
	ed._on_library_add("tiered")
	await _wait(1.0)
	while ed.debounce.time_left > 0.0 or ed.sim == null:
		await get_tree().process_frame
	await _wait(1.5)
	print("FLOW added: %s (%.1f s)" % [str(ed.outfit.map(func(e): return e["name"])), (Time.get_ticks_msec() - t0) / 1000.0])
	await _shot("2_added")
	# 3. play to the drape
	t0 = Time.get_ticks_msec()
	ed.tl_t = 0.0
	ed._on_play()
	while (ed.playing or ed.frames.size() < ed.target) and Time.get_ticks_msec() - t0 < 300000:
		await _wait(0.5)
		if ed.frames.size() in [20, 60, 100]:
			print("FLOW frames %d/%d at %.1f s" % [ed.frames.size(), ed.target, (Time.get_ticks_msec() - t0) / 1000.0])
	print("FLOW drape done: %d frames in %.1f s" % [ed.frames.size(), (Time.get_ticks_msec() - t0) / 1000.0])
	ed.tl_t = 4.0
	ed._last_shown = ""
	await _wait(1.0)
	for v in ["front", "q_l", "back"]:
		ed._cam_goto(v)
		await _wait(2.5)
		await _shot("3_drape_" + v)
	if OS.has_environment("FL_FLOW_NORENDER"):
		get_tree().quit()
		return
	# 4. render
	t0 = Time.get_ticks_msec()
	ed.render_mode.start()
	while ed.render_mode.state != ed.render_mode.S.PLAY and Time.get_ticks_msec() - t0 < 400000:
		await _wait(2.0)
		if int((Time.get_ticks_msec() - t0) / 1000) % 20 < 2:
			print("FLOW render state=%d msg=%s" % [ed.render_mode.state, ed.render_mode._msg.text])
	print("FLOW render: state=%d after %.1f s" % [ed.render_mode.state, (Time.get_ticks_msec() - t0) / 1000.0])
	await _wait(3.0)
	await _shot("4_render")
	await _wait(4.0)
	await _shot("5_render_later")
	get_tree().quit()
