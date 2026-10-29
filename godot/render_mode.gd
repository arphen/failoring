# Render mode: pick an activity (walk, lift a box, sit on a chair, ...), press Render, and the 3D view takes the whole window while
#   1. the cloth is simulated for the whole activity in the background (the Showcase machinery, docs/SCENARIOS.md), then
#   2. every frame is rendered offline at the highest fidelity (4x MSAA, global illumination, SSR, SSAO, SSIL, subsurface scattering,
#      the mirror, drawn at twice the size and scaled down) - as slowly as that takes, with no real-time budget - and stored as a
#      movie (JPEG frames, cached on disk, replayed instantly the next time), then
#   3. the movie plays: play and pause, scrub, speed, loop.
# Leaving the mode (Back to editing, or Esc) restores the panes, the quality and the camera exactly as they were. While editing the
# 3D view stays a real-time, lower-fidelity preview.
extends RefCounted

const Look = preload("res://look.gd")

enum S { EDIT, SIM, FRAMES, PLAY }

const OUTPUTS := [["720p", 1280, 720], ["1080p", 1920, 1080]]
const SSAA := 2
const MOVIE_FPS := 30
const JPEG_Q := 0.92
const MOVIE_VERSION := 2
const DIR := "user://renders"
const CAMERAS := ["Current view", "Slow orbit"]
const SIM_QUALITY := ["preview", "standard", "fine", "photo", "ultra"]
const SIM_QUALITY_LABELS := ["Preview sim", "Standard sim", "Fine sim (slow)", "Photo sim (slowest)", "Ultra sim (GPU solver)"]
const SPEEDS := [0.25, 0.5, 1.0, 2.0]
# rad, the camera's view of an activity that walks somewhere: mostly from the right side. At 0.95 (front and side) the
# camera following the runway walk reached the front wall (the orbit camera stops there) and closed in from 4.1 to 2.2 m.
const TRAVEL_YAW := 1.35
const SHARE_MIN_S := 6.0        # a looping movie is repeated to at least this long when saved as a video
const FFMPEG_PATHS := ["/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg", "/usr/bin/ffmpeg"]
const SPEED_LABELS := ["0.25x", "0.5x", "1x", "2x"]

var _ed: Node
var _sc: ShowcasePanel
var state := S.EDIT
var _view_pane: Control
var _bar: PanelContainer         # the Render control, top right of the 3D view (editing)
var _hud: PanelContainer         # the render HUD, bottom of the 3D view (render mode)
var _movie_rect: TextureRect
var _movie_bg: ColorRect
var _act_opt: OptionButton
var _out_opt: OptionButton
var _cam_opt: OptionButton
var _sim_opt: OptionButton
var _go_btn: Button
var _title: Label
var _progress: ProgressBar
var _msg: Label
var _cancel_btn: Button
var _play_btn: Button
var _slider: HSlider
var _time: Label
var _speed_opt: OptionButton
var _loop: CheckBox
var _back_btn: Button
var _save_btn: Button
var _gear: Button
var _scenarios: Array = []
var _hidden: Array = []          # chrome hidden while the mode lasts
var _cur_id := ""
var _cur_q := "preview"
var _movie := {}                 # {frames: Array of PackedByteArray, fps, w, h, duration}
var _movie_key := ""
var _t := 0.0                    # the movie's playhead, seconds
var _playing := false
var _shown_idx := -1
var _tex: ImageTexture
var _cancel := false
var _saved := {}                 # what render mode changed, to give back


func _init(ed: Node, showcase: ShowcasePanel, view_pane: Control) -> void:
	_ed = ed
	_sc = showcase
	_view_pane = view_pane
	_scenarios = _sc.api_scenarios()
	_build_bar()
	_build_hud()


# ---------------------------------------------------------------- UI
func _small(b: Control) -> void:
	if b is Button or b is OptionButton:
		b.add_theme_font_size_override("font_size", 12)
		b.focus_mode = Control.FOCUS_NONE


func _panel_style() -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.06, 0.07, 0.09, 0.78)
	sb.set_corner_radius_all(8)
	sb.content_margin_left = 8
	sb.content_margin_right = 8
	sb.content_margin_top = 5
	sb.content_margin_bottom = 5
	return sb


func _build_bar() -> void:
	_bar = PanelContainer.new()
	_bar.name = "RenderBar"
	_bar.add_theme_stylebox_override("panel", _panel_style())
	_bar.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT, Control.PRESET_MODE_MINSIZE, 8)
	_bar.grow_horizontal = Control.GROW_DIRECTION_BEGIN
	var h := HBoxContainer.new()
	h.add_theme_constant_override("separation", 5)
	_bar.add_child(h)
	_act_opt = OptionButton.new()
	_act_opt.name = "ActivityOpt"
	for sc in _scenarios:
		_act_opt.add_item(String(sc["name"]))
	_act_opt.tooltip_text = "What the mannequin does in the render"
	_small(_act_opt)
	h.add_child(_act_opt)
	# the movie's options live in a popup, so the bar stays small in a narrow pane
	_out_opt = OptionButton.new()
	for o in OUTPUTS:
		_out_opt.add_item(String(o[0]))
	_out_opt.tooltip_text = "Size of the movie (drawn at twice this size and scaled down)"
	_cam_opt = OptionButton.new()
	for c in CAMERAS:
		_cam_opt.add_item(c)
	_cam_opt.tooltip_text = "The camera of the movie: as you have it now, or slowly orbiting the mannequin"
	_sim_opt = OptionButton.new()
	for c in SIM_QUALITY_LABELS:
		_sim_opt.add_item(c)
	_sim_opt.tooltip_text = "The cloth simulation's quality: each step down the list is slower and finer. Photo and Ultra use small triangles and self-collision, for soft gathers and folds; Ultra is meant for the GPU solver."
	var gear := Button.new()
	gear.text = "..."
	gear.tooltip_text = "Movie size, camera and simulation quality"
	_small(gear)
	var pop := PopupPanel.new()
	var pv := VBoxContainer.new()
	pv.add_theme_constant_override("separation", 6)
	for pair in [["Size", _out_opt], ["Camera", _cam_opt], ["Cloth", _sim_opt]]:
		var row := HBoxContainer.new()
		var l := Label.new()
		l.text = String(pair[0])
		l.custom_minimum_size = Vector2(60, 0)
		row.add_child(l)
		_small(pair[1])
		row.add_child(pair[1])
		pv.add_child(row)
	pop.add_child(pv)
	var pop_style := _panel_style()
	pop_style.bg_color.a = 0.96   # a menu over the scene: nearly opaque
	pop_style.set_content_margin_all(10)
	pop.add_theme_stylebox_override("panel", pop_style)
	_bar.add_child(pop)
	# under the bar, right-aligned with the button, in the window's (scaled) coordinates: placed from the button's screen
	# position it landed off to the side on a scaled UI, and left-aligned it hung past the bar's right edge
	gear.pressed.connect(func() -> void:
		var r := gear.get_global_rect()
		var sz := Vector2i(pop.get_contents_minimum_size())
		pop.popup(Rect2i(Vector2i(int(r.end.x) - sz.x, int(r.end.y) + 6), sz)))
	h.add_child(gear)
	_gear = gear
	_go_btn = Button.new()
	_go_btn.name = "RenderBtn"
	_go_btn.text = "Render"
	_go_btn.tooltip_text = "Simulate the activity in the background, render every frame at full quality, then play the movie. The 3D view takes the whole window until you go back to editing (Esc)."
	_small(_go_btn)
	_go_btn.pressed.connect(start)
	h.add_child(_go_btn)
	_view_pane.add_child(_bar)


func _build_hud() -> void:
	_movie_bg = ColorRect.new()
	_movie_bg.name = "MovieBg"
	_movie_bg.color = Color.BLACK
	_movie_bg.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_movie_bg.mouse_filter = Control.MOUSE_FILTER_STOP
	_movie_bg.visible = false
	_view_pane.add_child(_movie_bg)
	_movie_rect = TextureRect.new()
	_movie_rect.name = "Movie"
	_movie_rect.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_movie_rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_movie_rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_movie_rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_movie_bg.add_child(_movie_rect)
	_hud = PanelContainer.new()
	_hud.name = "RenderHud"
	_hud.add_theme_stylebox_override("panel", _panel_style())
	_hud.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_WIDE, Control.PRESET_MODE_MINSIZE, 12)
	_hud.grow_vertical = Control.GROW_DIRECTION_BEGIN
	_hud.mouse_filter = Control.MOUSE_FILTER_STOP
	_hud.visible = false
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 4)
	_hud.add_child(v)
	_title = Label.new()
	_title.add_theme_font_size_override("font_size", 14)
	v.add_child(_title)
	_progress = ProgressBar.new()
	_progress.min_value = 0.0
	_progress.max_value = 100.0
	_progress.custom_minimum_size = Vector2(0, 14)
	v.add_child(_progress)
	_msg = Label.new()
	_msg.add_theme_font_size_override("font_size", 12)
	_msg.modulate = Color(1, 1, 1, 0.75)
	_msg.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	v.add_child(_msg)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 6)
	v.add_child(row)
	_play_btn = Button.new()
	_play_btn.text = "Pause"
	_small(_play_btn)
	_play_btn.pressed.connect(func() -> void: _set_playing(not _playing))
	row.add_child(_play_btn)
	_slider = HSlider.new()
	_slider.min_value = 0.0
	_slider.max_value = 1.0
	_slider.step = 0.001
	_slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_slider.value_changed.connect(_on_seek)
	row.add_child(_slider)
	_time = Label.new()
	_time.add_theme_font_size_override("font_size", 12)
	_time.custom_minimum_size = Vector2(90, 0)
	row.add_child(_time)
	_speed_opt = OptionButton.new()
	for l in SPEED_LABELS:
		_speed_opt.add_item(l)
	_speed_opt.select(2)
	_small(_speed_opt)
	row.add_child(_speed_opt)
	_loop = CheckBox.new()
	_loop.text = "Loop"
	_loop.button_pressed = true
	_loop.focus_mode = Control.FOCUS_NONE
	row.add_child(_loop)
	_save_btn = Button.new()
	_save_btn.name = "SaveVideoBtn"
	_save_btn.text = "Save video..."
	_save_btn.tooltip_text = "Save the movie as an MP4 (H.264) file, which plays in Discord, Signal and most players. A looping activity is repeated to at least %d s." % int(SHARE_MIN_S)
	_small(_save_btn)
	_save_btn.pressed.connect(_on_save_video)
	row.add_child(_save_btn)
	_cancel_btn = Button.new()
	_cancel_btn.text = "Cancel"
	_small(_cancel_btn)
	_cancel_btn.pressed.connect(cancel)
	row.add_child(_cancel_btn)
	_back_btn = Button.new()
	_back_btn.name = "BackBtn"
	_back_btn.text = "Back to editing  (Esc)"
	_small(_back_btn)
	_back_btn.pressed.connect(leave)
	row.add_child(_back_btn)
	_view_pane.add_child(_hud)


func active() -> bool:
	return state != S.EDIT


# ---------------------------------------------------------------- the mode
func _selected() -> Dictionary:
	return _scenarios[maxi(_act_opt.selected, 0)] if not _scenarios.is_empty() else {}


func _hide(c: Control) -> void:
	if c != null and c.visible:
		c.visible = false
		_hidden.append(c)


# Enters render mode: only the 3D view, none of the editing chrome.
func _enter_mode() -> void:
	_ed.layout.maximize()
	_hide(_ed.get_node_or_null("%LayoutBar"))
	_hide(_ed.get_node_or_null("%TimelineBar"))
	_hide(_view_pane.get_node_or_null("ViewBar"))
	_hide(_view_pane.get_node_or_null("ViewChips"))
	_hide(_bar)
	_hud.visible = true
	_saved = {"yaw": _ed.yaw, "pitch": _ed.pitch, "zoom": _ed.zoom, "pan": _ed.pan_off, "update": _ed.sub_view.render_target_update_mode}


func start() -> void:
	if state != S.EDIT or _scenarios.is_empty():
		return
	var sc := _selected()
	_cur_id = String(sc["id"])
	_cur_q = SIM_QUALITY[maxi(_sim_opt.selected, 0)]
	_enter_mode()
	_set_hud(S.SIM, "%s" % sc["name"], 0.0, "Starting...")
	_cancel = false
	# a movie rendered before for this garment, body, activity, size and camera plays at once
	var sim_key := _sc.api_key(_cur_id, _cur_q)
	if sim_key != "":
		_movie_key = _key(sim_key)
		var path := "%s/%s.flmovie" % [DIR, _movie_key]
		if FileAccess.file_exists(path):
			var mv := _load_movie(path)
			if not mv.is_empty():
				_movie = mv
				_begin_play()
				return
	var r := _sc.api_begin(_cur_id, _cur_q)
	match String(r["state"]):
		"cached":
			_capture()
		"started":
			state = S.SIM
		_:
			state = S.SIM                    # nothing runs: the message stays up and only Back to editing is left
			_set_hud(S.SIM, String(sc["name"]), 0.0, String(r.get("message", "Could not start.")))
			_cancel_btn.visible = false
			_progress.visible = false


func _key(sim_key: String) -> String:
	var o: Array = OUTPUTS[maxi(_out_opt.selected, 0)]
	return "%s_%dx%d_c%d_v%d" % [sim_key, int(o[1]), int(o[2]), _cam_opt.selected, MOVIE_VERSION]


func _set_hud(st: S, title: String, progress: float, msg: String) -> void:
	state = st
	_title.text = title
	_progress.value = progress * 100.0
	_progress.visible = st == S.SIM or st == S.FRAMES
	_msg.text = msg
	_cancel_btn.visible = st == S.SIM or st == S.FRAMES
	_play_btn.visible = st == S.PLAY
	_slider.visible = st == S.PLAY
	_time.visible = st == S.PLAY
	_speed_opt.visible = st == S.PLAY
	_loop.visible = st == S.PLAY
	_save_btn.visible = st == S.PLAY


func cancel() -> void:
	if state == S.SIM:
		_sc.api_cancel()
		_msg.text = "Cancelling..."
	elif state == S.FRAMES:
		_cancel = true


# Back to editing: everything render mode changed is given back.
func leave() -> void:
	if state == S.EDIT:
		return
	_cancel = true
	if _sc.api_status()["busy"]:
		_sc.api_cancel()
	_playing = false
	_movie_bg.visible = false
	_movie = {}
	_tex = null
	_shown_idx = -1
	_sc.api_exit()
	_ed.sub_view.render_target_update_mode = _saved.get("update", SubViewport.UPDATE_ALWAYS)
	_ed.yaw = float(_saved.get("yaw", _ed.yaw))
	_ed.pitch = float(_saved.get("pitch", _ed.pitch))
	_ed.zoom = float(_saved.get("zoom", _ed.zoom))
	_ed.pan_off = _saved.get("pan", _ed.pan_off)
	_hud.visible = false
	for c in _hidden:
		if is_instance_valid(c):
			(c as Control).visible = true
	_hidden = []
	_ed.layout.restore()
	_ed._last_shown = ""
	state = S.EDIT


# ---------------------------------------------------------------- frames
var _auto_done := false
var _log_t := 0.0
var _auto_play := 0.0
var _shot_pending := false
var _auto_saved := false
var _menu_shown := false
var _menu_at := 0


# Called by the editor every frame.
func tick(delta: float) -> void:
	# FL_RENDER_AUTO=<activity id>: start that render once the garment is on (look work and tests); it logs each phase
	# look work: FL_RENDER_MENU=<png>: the menu open, the window saved there half a second later
	if OS.has_environment("FL_RENDER_MENU") and _gear != null:
		var ms := Time.get_ticks_msec()
		if not _menu_shown and ms > 4000:
			_menu_shown = true
			_menu_at = ms
			_gear.pressed.emit()
		elif _menu_shown and _menu_at > 0 and ms > _menu_at + 500:
			_menu_at = -1
			_ed.get_viewport().get_texture().get_image().save_png(OS.get_environment("FL_RENDER_MENU"))
			print("RENDERMODE menu shot saved")
	if not _auto_done and OS.has_environment("FL_RENDER_AUTO") and Time.get_ticks_msec() > 7000 and state == S.EDIT:
		for i in _scenarios.size():
			if String(_scenarios[i]["id"]) == OS.get_environment("FL_RENDER_AUTO"):
				_act_opt.select(i)
		if OS.has_environment("FL_RENDER_OUT"):
			_out_opt.select(int(OS.get_environment("FL_RENDER_OUT")))
		if OS.has_environment("FL_RENDER_CAM"):
			_cam_opt.select(int(OS.get_environment("FL_RENDER_CAM")))
		if OS.has_environment("FL_RENDER_SIM"):  # index into SIM_QUALITY
			_sim_opt.select(int(OS.get_environment("FL_RENDER_SIM")))
		_auto_done = true
		start()
	if OS.has_environment("FL_RENDER_AUTO") and state != S.EDIT:
		_log_t += delta
		if _log_t > 1.0:
			_log_t = 0.0
			print("RENDERMODE state=%s %.0f%% %s | %s" % [S.keys()[state], _progress.value, _title.text, _msg.text])
			# FL_RENDER_SHOT=<prefix>: the window, once a second, as <prefix>_<state>.jpg (JPEG: fast to encode). One save at a time: when a frame
			# takes over a second (Ultra at 1080p), a save per frame starved the movie of the main thread.
			if OS.has_environment("FL_RENDER_SHOT") and not _shot_pending:
				var path := "%s_%s.png" % [OS.get_environment("FL_RENDER_SHOT"), S.keys()[state]]
				_shot_pending = true
				RenderingServer.frame_post_draw.connect(func() -> void:
					var img: Image = _ed.get_viewport().get_texture().get_image()
					if img != null and not img.is_empty():
						img.save_jpg(path.get_basename() + ".jpg", 0.85)
					_shot_pending = false, CONNECT_ONE_SHOT)
	if state == S.PLAY and OS.has_environment("FL_RENDER_SAVE") and not _auto_saved:  # tests: save the movie as a video
		_auto_saved = true
		_save_video(OS.get_environment("FL_RENDER_SAVE"))
	if state == S.PLAY and OS.has_environment("FL_RENDER_LEAVE"):       # tests: leave 3 s into the movie and report what came back
		_auto_play += delta
		if _auto_play > 3.0:
			_auto_play = -1000.0
			leave()
			print("RENDERMODE left: panes %s, layout bar %s, timeline %s, quality %s, view update %d, yaw %.2f" % [
				[_ed.layout.shown("left"), _ed.layout.shown("table"), _ed.layout.shown("view"), _ed.layout.shown("right")],
				_ed.get_node("%LayoutBar").visible, _ed.get_node("%TimelineBar").visible, _ed.render_q, _ed.sub_view.render_target_update_mode, _ed.yaw])
	match state:
		S.SIM:
			var st := _sc.api_status()
			if bool(st["busy"]):
				_progress.value = float(st["progress"]) * 100.0
				_title.text = "%s: simulating the cloth" % _selected()["name"]
				_msg.text = "%d%%, %s, %.0f s. The movie is rendered frame by frame next." % [int(float(st["progress"]) * 100.0), String(st["stage"]), float(st["seconds"])]
			elif _sc.api_ready(_cur_id, _cur_q):
				_capture()
			elif _cancel_btn.visible:
				_msg.text = String(st["status"])
				_cancel_btn.visible = false
		S.PLAY:
			_play_tick(delta)


# Renders every frame of the activity offline, at full quality and at twice the size, into the movie.
func _capture() -> void:
	if state == S.FRAMES:
		return
	state = S.FRAMES
	var sc := _selected()
	var o: Array = OUTPUTS[maxi(_out_opt.selected, 0)]
	var w := int(o[1])
	var h := int(o[2])
	_set_hud(S.FRAMES, "%s: rendering" % sc["name"], 0.0, "Frame 0")
	if not _sc.api_enter(_cur_id, _cur_q):
		_set_hud(S.SIM, String(sc["name"]), 0.0, "The simulated frames could not be put on the view.")
		_cancel_btn.visible = false
		return
	var ed := _ed
	var q_before: String = ed.render_q
	ed.shoot_busy = true                          # the editor leaves the viewport's size to us
	var size_before: Vector2i = ed.sub_view.size
	ed.sub_view.size = Vector2i(w * SSAA, h * SSAA)
	var vi: TextureRect = ed.view_image
	var stretch_before := vi.stretch_mode
	vi.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	vi.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	Look.apply_quality("high", ed.render_env, ed.render_key, ed.sub_view, ed.body_node.material_override)
	ed._shade.set_quality("high")
	ed.body_node.idle_paused = true
	var idle = ed.idle
	var yaw0: float = ed.yaw
	var yaw_restore: float = ed.yaw
	var dur := _sc.api_duration()
	var lead := ShowcasePanel.LEAD
	var n := int(ceil((dur + lead) * float(MOVIE_FPS)))
	var frames: Array = []
	var t0 := Time.get_ticks_msec()
	var orbit := _cam_opt.selected == 1
	var tree: SceneTree = ed.get_tree()
	# the camera follows the mannequin across the floor (activities that walk somewhere; the others keep it still). A walk
	# towards the camera ran the orbit camera into the room's wall (it stops there), so a walk is filmed from the front
	# and the side (TRAVEL_YAW) without orbiting, a view the room has space for
	var pan0: Vector3 = ed.pan_off
	var root0 := _sc.api_root(-lead)
	var travel := _sc.api_root(dur).distance_to(root0) > 0.5
	# the editor's seam and stitch lines stay where the garment was built; the movie shows the cloth only
	var lines_shown := {}
	for nm in ["_stitch_mesh", "_seam_mesh", "_trail_mesh", "_hl_mesh"]:
		var ln = ed.get(nm)
		if ln != null and is_instance_valid(ln):
			lines_shown[nm] = ln.visible
			ln.visible = false
	if travel:
		orbit = false
		ed.yaw = TRAVEL_YAW
		yaw0 = ed.yaw
	for i in n:
		if _cancel:
			break
		_sc.api_show(-lead + float(i) / float(MOVIE_FPS))
		if travel:  # the target is where the pelvis is (the walk starts off the platform's centre)
			ed.pan_off = pan0 + _sc.api_root(-lead + float(i) / float(MOVIE_FPS))
		idle.tick(1.0 / float(MOVIE_FPS))
		if orbit:
			ed.yaw = yaw0 + 0.9 * float(i) / float(maxi(n - 1, 1))
		ed._update_camera()
		if OS.has_environment("FL_RENDER_AUTO") and i % 20 == 0:
			print("RENDERMODE camera frame %d: pivot %s pan %s dist %.2f yaw %.2f cam %s" % [i, ed.cam_pivot.position, ed.pan_off, ed.view_dist, ed.yaw, ed.cam.global_position])
		# drawn here, not awaited: a window macOS hides (covered, minimised, another Space) is not drawn at all, and a
		# render waiting for its frames stopped until the window showed again. Two draws, as the two frames it awaited
		# before (temporal effects settle). A tick in between keeps the HUD and Cancel working.
		RenderingServer.force_draw(false)
		RenderingServer.force_draw(false)
		var img: Image = ed.sub_view.get_texture().get_image()
		if img == null or img.is_empty():
			continue
		img.convert(Image.FORMAT_RGB8)
		img.resize(w, h, Image.INTERPOLATE_CUBIC)
		frames.append(img.save_jpg_to_buffer(JPEG_Q))
		await tree.process_frame
		if i % 3 == 0 or i == n - 1:
			var done := float(i + 1) / float(n)
			var el := float(Time.get_ticks_msec() - t0) / 1000.0
			_progress.value = done * 100.0
			_msg.text = "Frame %d of %d, %.0f s so far, about %.0f s to go. Full quality, %dx%d drawn at %dx%d." % [i + 1, n, el, el * (1.0 - done) / maxf(done, 0.01), w, h, w * SSAA, h * SSAA]
	# give everything back
	ed.body_node.idle_paused = false
	ed.shoot_busy = false
	ed.sub_view.size = size_before
	vi.stretch_mode = stretch_before
	ed.render_q = q_before
	ed._apply_render_quality()
	ed.yaw = yaw_restore
	ed.pan_off = pan0
	for nm in lines_shown:
		var ln = ed.get(nm)
		if ln != null and is_instance_valid(ln):
			ln.visible = lines_shown[nm]
	if state != S.FRAMES or _cancel or frames.is_empty():
		if state == S.FRAMES:
			state = S.SIM
			_set_hud(S.SIM, String(sc["name"]), 0.0, "Render cancelled.")
			_cancel_btn.visible = false
			_progress.visible = false
		return
	_movie = {"frames": frames, "fps": MOVIE_FPS, "w": w, "h": h, "duration": float(frames.size()) / float(MOVIE_FPS), "name": String(sc["name"])}
	_save_movie()
	_begin_play()


# ---------------------------------------------------------------- the movie
func _save_movie() -> void:
	if _movie_key == "":
		return
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(DIR))
	var f := FileAccess.open("%s/%s.flmovie" % [DIR, _movie_key], FileAccess.WRITE)
	if f == null:
		push_warning("could not save the movie")
		return
	f.store_var({"version": MOVIE_VERSION, "fps": _movie["fps"], "w": _movie["w"], "h": _movie["h"], "duration": _movie["duration"], "name": _movie.get("name", ""), "frames": _movie["frames"]})
	f.close()


static func _load_movie(path: String) -> Dictionary:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return {}
	var d = f.get_var()
	f.close()
	if not (d is Dictionary) or int(d.get("version", 0)) != MOVIE_VERSION or (d.get("frames", []) as Array).is_empty():
		return {}
	return d


func _begin_play() -> void:
	state = S.PLAY
	_set_hud(S.PLAY, "%s" % _movie.get("name", _selected().get("name", "")), 0.0, "")
	_ed.sub_view.render_target_update_mode = SubViewport.UPDATE_DISABLED    # the movie is a picture: the GPU rests
	_movie_bg.visible = true
	_tex = null
	_shown_idx = -1
	_t = 0.0
	_slider.max_value = float(_movie["duration"])
	_set_playing(true)
	_show_movie_frame(0)


func _set_playing(on: bool) -> void:
	_playing = on
	_play_btn.text = "Pause" if on else "Play"


func _play_tick(delta: float) -> void:
	if _movie.is_empty():
		return
	if _playing:
		_t += delta * float(SPEEDS[clampi(_speed_opt.selected, 0, SPEEDS.size() - 1)])
		var dur := float(_movie["duration"])
		if _t >= dur:
			if _loop.button_pressed:
				_t = fposmod(_t, dur)
			else:
				_t = dur
				_set_playing(false)
	_show_movie_frame(clampi(int(_t * float(_movie["fps"])), 0, (_movie["frames"] as Array).size() - 1))


func _show_movie_frame(i: int) -> void:
	if i != _shown_idx:
		_shown_idx = i
		var img := Image.new()
		if img.load_jpg_from_buffer((_movie["frames"] as Array)[i]) == OK:
			if _tex == null:
				_tex = ImageTexture.create_from_image(img)
				_movie_rect.texture = _tex
			else:
				_tex.update(img)
	_slider.set_value_no_signal(_t)
	_time.text = "%.2f / %.2f s" % [_t, float(_movie["duration"])]


func _on_seek(v: float) -> void:
	if state == S.PLAY:
		_t = v
		_show_movie_frame(clampi(int(_t * float(_movie["fps"])), 0, (_movie["frames"] as Array).size() - 1))


# ---------------------------------------------------------------- saving the movie as a video
# ffmpeg encodes the movie's JPEG frames to H.264 in an MP4 (yuv420p in video range, +faststart: what chat apps play
# inline; the JPEG frames are full range, which some players show with wrong contrast). An app
# started from the Finder does not get the shell's PATH, so the usual install places are tried first.
func _ffmpeg() -> String:
	for p in FFMPEG_PATHS:
		if FileAccess.file_exists(p):
			return p
	var out := []
	if OS.execute("ffmpeg", ["-version"], out) == 0:
		return "ffmpeg"
	return ""


func _on_save_video() -> void:
	if _movie.is_empty():
		return
	if _ffmpeg() == "":
		_msg.text = "Saving a video needs ffmpeg (on a Mac: brew install ffmpeg)."
		return
	var dlg := FileDialog.new()
	dlg.use_native_dialog = true
	dlg.access = FileDialog.ACCESS_FILESYSTEM
	dlg.file_mode = FileDialog.FILE_MODE_SAVE_FILE
	dlg.filters = PackedStringArray(["*.mp4 ; MP4 video"])
	dlg.current_dir = OS.get_system_dir(OS.SYSTEM_DIR_MOVIES)
	dlg.current_file = "%s.mp4" % String(_movie.get("name", "render")).to_lower().replace(" ", "-")
	dlg.file_selected.connect(func(path: String) -> void: _save_video(path); dlg.queue_free())
	dlg.canceled.connect(dlg.queue_free)
	_ed.add_child(dlg)
	dlg.popup_centered()


# Writes the frames to a scratch folder, runs ffmpeg, reports the file. Returns true when the video was written.
func _save_video(path: String) -> bool:
	if not path.to_lower().ends_with(".mp4"):
		path += ".mp4"
	var ff := _ffmpeg()
	var frames: Array = _movie["frames"]
	var fps := int(_movie["fps"])
	var reps := 1
	if bool(_selected().get("loop", false)):
		reps = maxi(1, int(ceil(SHARE_MIN_S * float(fps) / float(maxi(frames.size(), 1)))))
	_msg.text = "Saving the video..."
	await _ed.get_tree().process_frame  # the message shows before the encoder blocks
	await _ed.get_tree().process_frame
	var tmp := OS.get_user_data_dir().path_join("renders/video_tmp")
	DirAccess.make_dir_recursive_absolute(tmp)
	for f in DirAccess.get_files_at(tmp):
		DirAccess.remove_absolute(tmp.path_join(f))
	var k := 0
	for r in reps:
		for buf in frames:
			var fa := FileAccess.open(tmp.path_join("f%05d.jpg" % k), FileAccess.WRITE)
			fa.store_buffer(buf)
			fa.close()
			k += 1
	var out := []
	var code := OS.execute(ff, ["-y", "-loglevel", "error", "-framerate", str(fps), "-i", tmp.path_join("f%05d.jpg"),
		"-vf", "scale=out_range=tv,format=yuv420p", "-c:v", "libx264", "-preset", "medium", "-crf", "20", "-movflags", "+faststart", path], out, true)
	for f in DirAccess.get_files_at(tmp):
		DirAccess.remove_absolute(tmp.path_join(f))
	if code != 0 or not FileAccess.file_exists(path):
		_msg.text = "ffmpeg could not write the video (exit %d): %s" % [code, "".join(out).strip_edges().left(200)]
		print("RENDERMODE video failed: ", _msg.text)
		return false
	var mb := float(FileAccess.get_file_as_bytes(path).size()) / 1048576.0
	_msg.text = "Saved %s (%.1f MB, %.1f s)." % [path, mb, float(k) / float(fps)]
	print("RENDERMODE video saved: ", _msg.text)
	return true
