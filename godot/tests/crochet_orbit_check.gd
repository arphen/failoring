# Checks the crochet views' orbit camera (crochet/crochet_orbit.gd): framing, zoom towards the pointer, pan, orbit, limits, picks.
# Events go straight into Orbit.handle(); the camera is in a SubViewport of a known size so unproject_position can be asked.
#   godot --headless --path godot res://tests/crochet_orbit_check.tscn     (prints PASS/FAIL, then "RESULT: n failed")
extends Node3D

const Orbit = preload("res://crochet/crochet_orbit.gd")
const Look = preload("res://look.gd")

const VIEW := Vector2(800, 600)
var fails := 0
var picked: Array = []


func check(name: String, ok: bool) -> void:
	print(("PASS  " if ok else "FAIL  ") + name)
	if not ok:
		fails += 1


func _ready() -> void:
	var vp := SubViewport.new()
	vp.size = Vector2i(VIEW)
	add_child(vp)
	var stage := Node3D.new()
	vp.add_child(stage)
	var o := Orbit.new()
	o.setup(stage, Look)
	await get_tree().process_frame

	check("the field of view is wide, not the 27 degree lens of the studio shots (%.0f)" % o.cam.fov, o.cam.fov >= 50.0)
	check("the near plane is closer than a centimetre and a half (stitches are millimetres)", o.cam.near <= 0.015)

	# framing: a bag-sized box, a tall pouch with a strap, a swatch
	for box in [[Vector3(-0.15, 0.9, -0.04), Vector3(0.15, 1.35, 0.04)], [Vector3(-0.05, 0.6, -0.02), Vector3(0.05, 1.35, 0.02)], [Vector3(-0.02, 1.3, -0.01), Vector3(0.02, 1.35, 0.01)]]:
		var lo: Vector3 = box[0]
		var hi: Vector3 = box[1]
		o.frame(lo, hi, VIEW.x / VIEW.y)
		await get_tree().process_frame
		var inside := true
		var span := 0.0
		for cx in [lo.x, hi.x]:
			for cy in [lo.y, hi.y]:
				for cz in [lo.z, hi.z]:
					var p: Vector2 = o.cam.unproject_position(Vector3(cx, cy, cz))
					inside = inside and p.x >= 0.0 and p.x <= VIEW.x and p.y >= 0.0 and p.y <= VIEW.y
					span = maxf(span, maxf(absf(p.x - VIEW.x * 0.5) / (VIEW.x * 0.5), absf(p.y - VIEW.y * 0.5) / (VIEW.y * 0.5)))
		check("framing %s: every corner is on screen and the piece fills the view (%.2f of the half-width)" % [hi - lo, span], inside and span > 0.45)
	# a tall narrow pane: the width limits the distance
	o.frame(Vector3(-0.15, 0.9, -0.15), Vector3(0.15, 1.2, 0.15), 0.4)
	var d_narrow: float = o.dist
	o.frame(Vector3(-0.15, 0.9, -0.15), Vector3(0.15, 1.2, 0.15), 1.6)
	check("a narrow pane needs the camera further back than a wide one (%.2f > %.2f)" % [d_narrow, o.dist], d_narrow > o.dist * 1.5)

	# zoom towards the pointer: the point of the focal plane under the pointer stays under it
	o.frame(Vector3(-0.15, 0.9, -0.04), Vector3(0.15, 1.35, 0.04), VIEW.x / VIEW.y)
	o.set_view(0.7, -0.3)
	await get_tree().process_frame
	var b: Basis = o.pivot.transform.basis
	var q: Vector3 = o.target + b.x * 0.06 + b.y * 0.04
	var s0: Vector2 = o.cam.unproject_position(q)
	o.zoom_at(0.5, s0, VIEW)
	await get_tree().process_frame
	var s1: Vector2 = o.cam.unproject_position(q)
	check("zoom in towards the pointer: the point under it stays put (%s -> %s)" % [s0, s1], s0.distance_to(s1) < 0.6)
	var d_before: float = o.eff_dist()
	o.zoom_at(2.0, s0, VIEW)
	await get_tree().process_frame
	var s2: Vector2 = o.cam.unproject_position(q)
	check("zoom out from the same point: still under the pointer, and back to the distance (%.3f vs %.3f)" % [o.eff_dist(), d_before * 2.0], s0.distance_to(s2) < 0.6 and absf(o.eff_dist() - d_before * 2.0) < 1.0e-4)

	# the wheel: one notch is a constant ratio, up and down are inverse
	o.frame(Vector3(-0.15, 0.9, -0.04), Vector3(0.15, 1.35, 0.04), VIEW.x / VIEW.y)
	var d0: float = o.dist
	var up := InputEventMouseButton.new()
	up.button_index = MOUSE_BUTTON_WHEEL_UP
	up.pressed = true
	up.position = VIEW * 0.5
	up.factor = 1.0
	o.handle(up, VIEW)
	check("a wheel notch up brings the camera closer by one notch (%.3f -> %.3f)" % [d0, o.dist], absf(o.dist - d0 / Orbit.ZOOM_NOTCH) < 1.0e-4)
	var down := InputEventMouseButton.new()
	down.button_index = MOUSE_BUTTON_WHEEL_DOWN
	down.pressed = true
	down.position = VIEW * 0.5
	down.factor = 1.0
	o.handle(down, VIEW)
	check("a notch down puts it back", absf(o.dist - d0) < 1.0e-4)
	down.factor = 0.0   # a platform that leaves the factor unset
	o.handle(down, VIEW)
	check("a wheel event with no factor still zooms one notch", absf(o.dist - d0 * Orbit.ZOOM_NOTCH) < 1.0e-4)
	var pinch := InputEventMagnifyGesture.new()
	pinch.factor = 2.0
	pinch.position = VIEW * 0.5
	var d_p: float = o.dist
	o.handle(pinch, VIEW)
	check("a pinch outwards (factor 2) halves the distance", absf(o.dist - d_p * 0.5) < 1.0e-4)

	# limits
	o.frame(Vector3(-0.15, 0.9, -0.04), Vector3(0.15, 1.35, 0.04), VIEW.x / VIEW.y)
	for i in 200:
		o.zoom_at(0.8, VIEW * 0.5, VIEW)
	check("zooming in stops at %.0f cm, short of the cloth (%.3f)" % [Orbit.MIN_DIST * 100.0, o.eff_dist()], o.eff_dist() >= Orbit.MIN_DIST - 1.0e-6 and o.eff_dist() < 0.1)
	for i in 200:
		o.zoom_at(1.25, VIEW * 0.5, VIEW)
	await get_tree().process_frame
	check("zooming out stops inside the studio room (camera at %s)" % o.cam.global_position, absf(o.cam.global_position.x) < 4.8 and absf(o.cam.global_position.z) < 4.8 and o.cam.global_position.y < 3.6 and o.cam.global_position.y > 0.0)
	o.pitch = 9.0
	o.apply()
	check("the pitch is kept short of the poles", absf(o.pitch) <= Orbit.PITCH_LIMIT + 1.0e-6)

	# orbit
	o.frame(Vector3(-0.15, 0.9, -0.04), Vector3(0.15, 1.35, 0.04), VIEW.x / VIEW.y)
	var y0: float = o.yaw
	var p0: float = o.pitch
	_press(o, MOUSE_BUTTON_LEFT, Vector2(300, 300))
	_motion(o, Vector2(40, 20))
	check("left drag orbits (yaw %.3f -> %.3f, pitch %.3f -> %.3f)" % [y0, o.yaw, p0, o.pitch], absf(o.yaw - (y0 - 40.0 * Orbit.ORBIT_SPEED)) < 1.0e-5 and absf(o.pitch - (p0 - 20.0 * Orbit.ORBIT_SPEED)) < 1.0e-5)
	var tgt: Vector3 = o.target
	check("an orbit keeps the target", o.target == tgt)
	_release(o, MOUSE_BUTTON_LEFT, Vector2(340, 320))
	check("the user's move is remembered, so a finished run does not reframe over it", o.user_moved)
	o.frame(Vector3(-0.15, 0.9, -0.04), Vector3(0.15, 1.35, 0.04), VIEW.x / VIEW.y)
	check("framing again clears it", not o.user_moved)

	# pan: right drag, middle drag, shift + left drag; the scene follows the pointer
	for mode in ["right", "middle", "shift"]:
		o.frame(Vector3(-0.15, 0.9, -0.04), Vector3(0.15, 1.35, 0.04), VIEW.x / VIEW.y)
		o.set_view(0.5, -0.2)
		await get_tree().process_frame
		var w: Vector3 = o.target + o.pivot.transform.basis.x * 0.03
		var a: Vector2 = o.cam.unproject_position(w)
		match mode:
			"right": _press(o, MOUSE_BUTTON_RIGHT, Vector2(300, 300))
			"middle": _press(o, MOUSE_BUTTON_MIDDLE, Vector2(300, 300))
			"shift": _press(o, MOUSE_BUTTON_LEFT, Vector2(300, 300), true)
		_motion(o, Vector2(60, -30))
		await get_tree().process_frame
		var a2: Vector2 = o.cam.unproject_position(w)
		check("%s drag pans: a point of the scene moves with the pointer (%s -> %s)" % [mode, a, a2], absf((a2.x - a.x) - 60.0) < 1.5 and absf((a2.y - a.y) + 30.0) < 1.5)
		check("%s drag does not orbit" % mode, is_equal_approx(o.yaw, 0.5) and is_equal_approx(o.pitch, -0.2))
		match mode:
			"right": _release(o, MOUSE_BUTTON_RIGHT, Vector2(360, 270))
			"middle": _release(o, MOUSE_BUTTON_MIDDLE, Vector2(360, 270))
			"shift": _release(o, MOUSE_BUTTON_LEFT, Vector2(360, 270))
	# a two-finger scroll pans as well
	var before: Vector3 = o.target
	var pg := InputEventPanGesture.new()
	pg.delta = Vector2(5, 0)
	o.handle(pg, VIEW)
	check("a two-finger scroll pans", o.target.distance_to(before) > 1.0e-4)

	# picks: a click that did not move is reported, a drag is not
	picked.clear()
	var cb := func(p: Vector2) -> void: picked.append(p)
	_press(o, MOUSE_BUTTON_LEFT, Vector2(200, 150))
	_release(o, MOUSE_BUTTON_LEFT, Vector2(201, 150), false, cb)
	check("a click without a drag is a pick at its position", picked.size() == 1 and picked[0] == Vector2(201, 150))
	_press(o, MOUSE_BUTTON_LEFT, Vector2(200, 150))
	_motion(o, Vector2(30, 0))
	_release(o, MOUSE_BUTTON_LEFT, Vector2(230, 150), false, cb)
	check("a drag is not a pick", picked.size() == 1)
	var moved := InputEventMouseMotion.new()
	moved.relative = Vector2(50, 0)
	var yaw_idle: float = o.yaw
	o.handle(moved, VIEW)
	check("moving the pointer with no button down does nothing", is_equal_approx(o.yaw, yaw_idle))

	print("RESULT: %d failed" % fails)
	get_tree().quit(1 if fails > 0 else 0)


func _press(o, button: int, pos: Vector2, shift := false) -> void:
	var e := InputEventMouseButton.new()
	e.button_index = button
	e.pressed = true
	e.position = pos
	e.shift_pressed = shift
	o.handle(e, VIEW)


func _release(o, button: int, pos: Vector2, shift := false, cb := Callable()) -> void:
	var e := InputEventMouseButton.new()
	e.button_index = button
	e.pressed = false
	e.position = pos
	e.shift_pressed = shift
	o.handle(e, VIEW, cb)


func _motion(o, rel: Vector2) -> void:
	var e := InputEventMouseMotion.new()
	e.relative = rel
	o.handle(e, VIEW)
