# ./garment shots FILE [--body B] [--out DIR]: the editor in a window with only this garment on a standing mannequin,
# draped, then photographed from the front, three-quarter, side and back. Saves each view and a contact sheet (all four
# side by side) and prints their paths as SHOT lines. A window is needed (the GPU draws the photos).
extends Node

const BodyGen = preload("res://body_gen.gd")
const CamViews = preload("res://cam_views.gd")
const VIEWS := ["front", "q_l", "left", "back"]

var ed: Node


func _wait(sec: float) -> void:
	await get_tree().create_timer(sec).timeout


func _ready() -> void:
	# a stuck run (a script error stops the coroutine, the window would stay open) ends with an ERROR instead
	get_tree().create_timer(300.0).timeout.connect(func() -> void:
		print("ERROR shots timed out after 300 s (see the Godot log above)")
		get_tree().quit(1))
	var args := OS.get_cmdline_user_args()
	var file := ""
	var body := ""
	var out := ""
	var i := 1
	while i < args.size():
		if args[i] == "--body" and i + 1 < args.size():
			body = args[i + 1]
			i += 1
		elif args[i] == "--out" and i + 1 < args.size():
			out = args[i + 1]
			i += 1
		elif file == "":
			file = args[i]
		i += 1
	var cwd := OS.get_environment("FL_CWD")
	if file == "" or not FileAccess.file_exists(file if file.is_absolute_path() else cwd.path_join(file)):
		print("ERROR give the recipe file: ./garment shots FILE")
		get_tree().quit(1)
		return
	file = file if file.is_absolute_path() else cwd.path_join(file)
	if out == "":
		out = cwd.path_join("shots").path_join(file.get_file().get_basename())
	elif not out.is_absolute_path():
		out = cwd.path_join(out)
	DirAccess.make_dir_recursive_absolute(out)
	get_window().always_on_top = true   # macOS stops drawing a covered window
	OS.set_environment("FL_STAND", "1")
	OS.set_environment("FL_GARMENT", file)
	ed = load("res://editor.tscn").instantiate()
	add_child(ed)
	while ed.sim == null or ed.body_stand == null:
		await get_tree().process_frame
	if body != "" and body != "mine":
		if not BodyGen.PRESETS.has(body):
			print("ERROR body '%s' unknown: mine or one of %s" % [body, ", ".join(PackedStringArray(BodyGen.PRESETS.keys()))])
			get_tree().quit(1)
			return
		ed.meas = (BodyGen.PRESETS[body] as Dictionary).duplicate()
		ed._need_outfit = true
		ed.regen_and_rebuild()
	await _wait(1.0)
	while ed.debounce.time_left > 0.0 or ed.sim == null:
		await get_tree().process_frame
	if ed.outfit.is_empty():
		print("ERROR the editor could not load the recipe: run ./garment check %s" % file)
		get_tree().quit(1)
		return
	ed.layout.maximize()
	var t0 := Time.get_ticks_msec()
	ed.tl_t = 0.0
	ed._on_play()
	while (ed.playing or ed.frames.size() < ed.target) and Time.get_ticks_msec() - t0 < 240000:
		await _wait(0.5)
	print("DRAPE %d frames in %.1f s  %s" % [ed.frames.size(), (Time.get_ticks_msec() - t0) / 1000.0, ed.health_text])
	ed.tl_t = 4.0
	ed._last_shown = ""
	var imgs := []
	for v in VIEWS:
		ed._cam_goto(v)
		# straight to the view (the camera eases towards it otherwise)
		var cv: Dictionary = CamViews.by_id(v)
		ed.yaw = float(cv["yaw"])
		ed.pitch = float(cv["pitch"])
		ed.zoom = 1.0
		ed._cam_goal = {}
		await _wait(1.0)
		RenderingServer.force_draw(false)
		RenderingServer.force_draw(false)
		var img: Image = ed.sub_view.get_texture().get_image()
		var path := out.path_join("%s.png" % v)
		img.save_png(path)
		imgs.append(img)
		print("SHOT %s %s" % [v, path])
	# contact sheet: the views side by side, each cropped to the middle of the frame (the mannequin)
	var h: int = imgs[0].get_height()
	var w: int = mini(imgs[0].get_width(), int(h * 0.6))
	var sheet := Image.create(w * imgs.size(), h, false, imgs[0].get_format())
	for k in imgs.size():
		var im: Image = imgs[k]
		sheet.blit_rect(im, Rect2i((im.get_width() - w) / 2, 0, w, h), Vector2i(k * w, 0))
	var sp := out.path_join("sheet.png")
	sheet.save_png(sp)
	print("SHOT sheet %s  (front, three-quarter, side, back)" % sp)
	get_tree().quit(0)
