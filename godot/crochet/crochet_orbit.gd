# The orbit camera of the crochet views (the editor, the compare overlay): a pivot node that carries the camera.
#   left drag          orbit
#   right / middle drag, shift + left drag     pan: the scene follows the pointer
#   wheel, pinch       zoom towards the pointer (the point under it stays under it), a constant ratio per step
#   two-finger scroll  pan
# The limits come from the size of what is looked at (`radius`), so a 4 cm swatch and a 35 cm bag both zoom from a few
# centimetres to well back, and the camera never leaves the studio room.
extends RefCounted

const Studio = preload("res://studio.gd")

const FOV := 55.0              # vertical degrees. The studio's 27 degree "50 mm" lens made the small pieces look cramped
const NEAR := 0.01             # stitches are millimetres: the editor's 5 cm near plane cut the cloth away close up
const FAR := 40.0
const ZOOM_NOTCH := 1.12       # distance ratio per wheel notch
const MIN_DIST := 0.03
const PITCH_LIMIT := 1.5
const ORBIT_SPEED := 0.005     # radians per pixel
const FRAME_MARGIN := 1.12     # room round the subject when framing, as a ratio of its bounding sphere

var pivot: Node3D
var cam: Camera3D
var yaw := 0.4
var pitch := -0.15
var dist := 1.4                # what the user asked for; the camera stands at min(dist, what the room allows)
var target := Vector3(0.0, 1.0, 0.0)
var radius := 0.3              # half the size of the subject: sets the farthest zoom and the pan reach
var user_moved := false        # the user has orbited, panned or zoomed since the last frame(): a new framing must not undo that
var _drag := ""                # "" | "orbit" | "pan"
var _press_pos := Vector2.ZERO


# Adds the pivot and the camera under `parent`. `look` is the Look script (its camera setup is shared with the editor's).
func setup(parent: Node3D, look: Script) -> void:
	pivot = Node3D.new()
	parent.add_child(pivot)
	cam = Camera3D.new()
	look.camera(cam)
	cam.fov = FOV
	cam.near = NEAR
	cam.far = FAR
	pivot.add_child(cam)
	apply()


# The distance the camera really stands at: the user's, kept inside the room and inside the zoom limits.
func eff_dist() -> float:
	var back := pivot.transform.basis.z if pivot != null else Vector3.BACK
	var room := Studio.max_orbit_dist(target, back)
	return clampf(minf(dist, room), MIN_DIST, maxf(MIN_DIST, max_dist()))


func max_dist() -> float:
	return maxf(radius * 40.0, 1.0)


func apply() -> void:
	pitch = clampf(pitch, -PITCH_LIMIT, PITCH_LIMIT)
	dist = clampf(dist, MIN_DIST, max_dist())
	pivot.position = target
	pivot.rotation = Vector3(pitch, yaw, 0.0)
	cam.position = Vector3(0.0, 0.0, eff_dist())


# Looks at a box (`lo`..`hi`): the target at its middle, the camera far enough back to take its bounding sphere in, whatever the
# window's shape. `aspect` is width over height.
func frame(lo: Vector3, hi: Vector3, aspect: float) -> void:
	user_moved = false
	target = (lo + hi) * 0.5
	radius = maxf(0.02, (hi - lo).length() * 0.5)
	var half := tan(deg_to_rad(FOV) * 0.5) * minf(1.0, maxf(0.2, aspect))   # the narrower side limits
	dist = radius * FRAME_MARGIN * sqrt(1.0 + half * half) / half              # the bounding sphere just fits
	apply()


func set_view(new_yaw: float, new_pitch: float) -> void:
	user_moved = true
	yaw = new_yaw
	pitch = new_pitch
	apply()


# Handles a pointer event from the view's control. `view_size` is the control's size in the pointer's units. Returns true
# when it was used; `clicked` (when given) is called with the position of a left click that did not move (a pick, not a drag).
func handle(event: InputEvent, view_size: Vector2, clicked: Callable = Callable()) -> bool:
	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		match mb.button_index:
			MOUSE_BUTTON_LEFT:
				if mb.pressed:
					_press_pos = mb.position
					_drag = "pan" if mb.shift_pressed else "orbit"
				else:
					var was := _drag
					_drag = ""
					if was != "" and clicked.is_valid() and mb.position.distance_to(_press_pos) < 4.0:
						clicked.call(mb.position)
				return true
			MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE:
				_drag = "pan" if mb.pressed else ""
				return true
			MOUSE_BUTTON_WHEEL_UP:
				if mb.pressed:
					zoom_at(1.0 / pow(ZOOM_NOTCH, _notches(mb)), mb.position, view_size)
				return true
			MOUSE_BUTTON_WHEEL_DOWN:
				if mb.pressed:
					zoom_at(pow(ZOOM_NOTCH, _notches(mb)), mb.position, view_size)
				return true
		return false
	if event is InputEventMouseMotion and _drag != "":
		var rel := (event as InputEventMouseMotion).relative
		if _drag == "pan":
			pan_by(rel, view_size)
		else:
			user_moved = true
			yaw -= rel.x * ORBIT_SPEED
			pitch -= rel.y * ORBIT_SPEED
			apply()
		return true
	if event is InputEventMagnifyGesture:
		zoom_at(1.0 / maxf(0.05, (event as InputEventMagnifyGesture).factor), (event as InputEventMagnifyGesture).position, view_size)
		return true
	if event is InputEventPanGesture:
		pan_by((event as InputEventPanGesture).delta, view_size)
		return true
	return false


# A wheel event is one notch, or a fraction of one for a smooth (trackpad) wheel; some platforms leave the factor at 0.
static func _notches(mb: InputEventMouseButton) -> float:
	return 1.0 if mb.factor <= 0.0 else mb.factor


# Dollies by `ratio` (below 1: closer) so that the point of the focal plane under `at` stays under it.
func zoom_at(ratio: float, at: Vector2, view_size: Vector2) -> void:
	var d0 := eff_dist()
	var d1 := clampf(d0 * ratio, MIN_DIST, max_dist())
	var k := d1 / d0
	if absf(k - 1.0) < 1.0e-6:
		return
	user_moved = true
	# the point of the plane through the target (square to the view) that lies under the pointer, from the camera's own geometry
	# (the camera's projection is not available before the first frame, so it is worked out here)
	var b := pivot.transform.basis
	var half_v := tan(deg_to_rad(FOV) * 0.5) * d0
	var half_h := half_v * (view_size.x / maxf(1.0, view_size.y))
	var nx := (at.x / maxf(1.0, view_size.x) - 0.5) * 2.0
	var ny := (0.5 - at.y / maxf(1.0, view_size.y)) * 2.0
	var p := target + b.x * (nx * half_h) + b.y * (ny * half_v)
	target = target + (p - target) * (1.0 - k)
	dist = d1
	apply()


# Moves the target so the scene follows the pointer by `rel` pixels.
func pan_by(rel: Vector2, view_size: Vector2) -> void:
	user_moved = true
	var b := pivot.transform.basis
	var per_px := 2.0 * tan(deg_to_rad(FOV) * 0.5) * eff_dist() / maxf(1.0, view_size.y)
	target += (-b.x * rel.x + b.y * rel.y) * per_px
	apply()
