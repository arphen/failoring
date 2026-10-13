# Checks the 3D view camera input (editor.gd _camera_input / _update_camera). Events are pushed into the root viewport, so
# they take the same GUI routing as a real pointer (ViewPane.gui_input). Run headless:
#   godot --headless --path godot res://tests/camera_check.tscn     (prints PASS/FAIL, then "RESULT: n failed")
extends Node3D

var ed: Node
var fails := 0


func check(name: String, ok: bool) -> void:
	print(("PASS  " if ok else "FAIL  ") + name)
	if not ok:
		fails += 1


func _ready() -> void:
	get_tree().create_timer(180.0).timeout.connect(func() -> void:
		print("RESULT: watchdog timeout")
		get_tree().quit(2))
	ed = load("res://editor.tscn").instantiate()
	add_child(ed)
	await _frames(3)
	await _run()
	print("RESULT: %d failed" % fails)
	get_tree().quit(1 if fails > 0 else 0)


func _frames(n: int) -> void:
	for i in n:
		await get_tree().physics_frame


func _center() -> Vector2:
	return ed.view_pane.get_global_rect().get_center()


func _press(pos: Vector2, idx: int, dbl := false) -> void:
	var b := InputEventMouseButton.new()
	b.position = pos
	b.global_position = pos
	b.button_index = idx
	b.pressed = true
	b.double_click = dbl
	get_viewport().push_input(b)


func _release(pos: Vector2, idx: int) -> void:
	var b := InputEventMouseButton.new()
	b.position = pos
	b.global_position = pos
	b.button_index = idx
	b.pressed = false
	get_viewport().push_input(b)


func _wheel(pos: Vector2, up: bool) -> void:
	var idx := MOUSE_BUTTON_WHEEL_UP if up else MOUSE_BUTTON_WHEEL_DOWN
	_press(pos, idx)
	_release(pos, idx)


func _motion(pos: Vector2, rel: Vector2, mask: int, shift := false) -> void:
	var m := InputEventMouseMotion.new()
	m.position = pos
	m.global_position = pos
	m.relative = rel
	m.button_mask = mask
	m.shift_pressed = shift
	get_viewport().push_input(m)


# A full drag: press at c, one motion step of rel, release.
func _drag(c: Vector2, rel: Vector2, idx: int, shift := false) -> void:
	var masks := {MOUSE_BUTTON_LEFT: MOUSE_BUTTON_MASK_LEFT, MOUSE_BUTTON_RIGHT: MOUSE_BUTTON_MASK_RIGHT,
		MOUSE_BUTTON_MIDDLE: MOUSE_BUTTON_MASK_MIDDLE}
	var mask: int = masks[idx]
	_press(c, idx)
	_motion(c + rel, rel, mask, shift)
	_release(c + rel, idx)


func _run() -> void:
	var c := _center()
	var cam: Camera3D = ed.cam
	var pivot: Node3D = ed.cam_pivot
	await _frames(2)

	# Sign regression: the camera sits above the pivot at the default view and in the table view (tl_t = 0).
	check("default view: camera above the pivot", cam.global_position.y > pivot.global_position.y)
	ed.tl_t = 0.0
	await _frames(2)
	check("table view: camera above the pivot and the floor", cam.global_position.y > pivot.global_position.y and cam.global_position.y > 0.5)

	# Left-drag orbits.
	var y0: float = ed.yaw
	var p0: float = ed.pitch
	_drag(c, Vector2(40, 0), MOUSE_BUTTON_LEFT)
	await _frames(1)
	check("left-drag orbits yaw (40 px = -0.2 rad)", absf(ed.yaw - (y0 - 0.2)) < 1e-4)
	check("left-drag keeps pitch", is_equal_approx(ed.pitch, p0))
	check("left-drag keeps the target", ed.pan_off == Vector3.ZERO)

	# Pitch clamps (browser range: elevation -0.15..1.5, i.e. pitch -1.5..0.15).
	_drag(c, Vector2(0, -5000), MOUSE_BUTTON_LEFT)
	check("drag up clamps pitch at max (camera low)", is_equal_approx(ed.pitch, ed.CAM_PITCH_MAX))
	_drag(c, Vector2(0, 5000), MOUSE_BUTTON_LEFT)
	check("drag down clamps pitch at min (camera high)", is_equal_approx(ed.pitch, ed.CAM_PITCH_MIN))
	await _frames(1)
	check("clamped pitch keeps the camera above the floor", cam.global_position.y > 0.0)
	ed._reset_view()
	await _frames(1)
	var base: Vector3 = pivot.position

	# Right-drag, middle-drag and shift+left-drag pan the target in the camera plane; the scene follows the pointer.
	var right: Vector3 = cam.global_transform.basis.x
	var up: Vector3 = cam.global_transform.basis.y
	_drag(c, Vector2(60, 0), MOUSE_BUTTON_RIGHT)
	await _frames(1)
	check("right-drag pans the target", ed.pan_off.length() > 0.01)
	check("right-drag: drag right moves target left (scene follows)", ed.pan_off.dot(right) < 0.0)
	check("right-drag does not orbit", is_equal_approx(ed.yaw, 0.3))
	check("pivot moves with the pan", pivot.position.is_equal_approx(base + ed.pan_off))

	ed._reset_view()
	_drag(c, Vector2(0, 40), MOUSE_BUTTON_MIDDLE)
	await _frames(1)
	check("middle-drag pans: drag down moves target up", ed.pan_off.length() > 0.01 and ed.pan_off.dot(up) > 0.0)
	check("middle-drag does not orbit", is_equal_approx(ed.yaw, 0.3) and is_equal_approx(ed.pitch, -0.12))

	ed._reset_view()
	_drag(c, Vector2(30, 0), MOUSE_BUTTON_LEFT, true)
	await _frames(1)
	check("shift+left-drag pans", ed.pan_off.length() > 0.01 and is_equal_approx(ed.yaw, 0.3))
	ed._reset_view()

	# Wheel dollies around the target, clamped; it does not move the target.
	var z0: float = ed.zoom
	_wheel(c, false)
	var zd: float = ed.zoom
	check("wheel down zooms out", zd > z0)
	_wheel(c, true)
	check("wheel up zooms back in", ed.zoom < zd)
	for i in 80:
		_wheel(c, false)
	check("zoom clamps at max (3.0)", is_equal_approx(ed.zoom, ed.CAM_ZOOM_MAX))
	for i in 120:
		_wheel(c, true)
	check("zoom clamps at min (0.25)", is_equal_approx(ed.zoom, ed.CAM_ZOOM_MIN))
	await _frames(1)
	check("zoom keeps the target", ed.pan_off == Vector3.ZERO)
	check("zoom dollies the camera (distance follows zoom)", absf(cam.position.length() - ed.view_dist) < 1e-3)
	ed._reset_view()
	await _frames(1)

	# Trackpad: two-finger scroll pans, pinch zooms.
	var pg := InputEventPanGesture.new()
	pg.position = c
	pg.delta = Vector2(0, 20)
	get_viewport().push_input(pg)
	check("trackpad pan gesture pans", ed.pan_off.length() > 0.01)
	ed._reset_view()
	var mg := InputEventMagnifyGesture.new()
	mg.position = c
	mg.factor = 2.0
	get_viewport().push_input(mg)
	check("trackpad pinch out zooms in (factor 2 -> zoom 0.5)", is_equal_approx(ed.zoom, 0.5))
	ed._reset_view()

	# Input outside the viewport container is ignored.
	var outside := Vector2(4, 4)
	_motion(outside, Vector2(50, 0), MOUSE_BUTTON_MASK_LEFT)
	await _frames(1)
	check("pointer outside the view: no pointer-over flag", not ed.pointer_over_view)
	check("pointer outside the view: no orbit", is_equal_approx(ed.yaw, 0.3))
	var k := InputEventKey.new()
	k.keycode = KEY_F
	k.pressed = true
	ed.yaw = 1.0
	get_viewport().push_input(k)
	check("F outside the view does not reset", is_equal_approx(ed.yaw, 1.0))

	# Pointer enters the view: F and Home reset, double-click resets.
	_motion(c, Vector2.ZERO, 0)
	await _frames(1)
	check("pointer over the view sets the flag", ed.pointer_over_view)
	k.keycode = KEY_F
	get_viewport().push_input(k)
	check("F resets the view", is_equal_approx(ed.yaw, 0.3) and is_equal_approx(ed.pitch, -0.12) and is_equal_approx(ed.zoom, 1.0))
	ed.yaw = 1.0
	k.keycode = KEY_HOME
	get_viewport().push_input(k)
	check("Home resets the view", is_equal_approx(ed.yaw, 0.3))
	ed.yaw = 1.0
	_press(c, MOUSE_BUTTON_LEFT, true)
	_release(c, MOUSE_BUTTON_LEFT)
	check("double-click resets the view", is_equal_approx(ed.yaw, 0.3))

	# Auto-orbit (demo) pauses while the user is working the camera and resumes after the idle time.
	_drag(c, Vector2(10, 0), MOUSE_BUTTON_LEFT)
	var y1: float = ed.yaw
	ed._auto_orbit_step()
	check("auto-orbit paused right after user input", is_equal_approx(ed.yaw, y1))
	ed.user_cam_until = 0
	ed._auto_orbit_step()
	check("auto-orbit steps when the user is idle", absf(ed.yaw - (y1 + 0.12 / 60.0)) < 1e-6)

	# Long pans stay inside the clamp.
	ed._reset_view()
	for i in 300:
		_drag(c, Vector2(400, 300), MOUSE_BUTTON_RIGHT)
	check("long pan stays inside the clamp", ed.pan_off.length() <= ed.CAM_PAN_MAX + 1e-4)
	ed._reset_view()
	await _frames(1)
	check("reset clears the pan", ed.pan_off == Vector3.ZERO and is_equal_approx(ed.yaw, 0.3))
