# Free 3D placement of a pattern piece: a rigid move and turn applied on top of its wrap (wrap.gd), so a panel can be put
# anywhere on or around the body, at any angle, before it is sewn. Kept in the piece as
#   p["place"] = {"t": [x, y, z], "q": [x, y, z, w]}
# (metres, and a unit quaternion), saved with the pattern. The turn is about the piece's own centre (the mean of its
# wrapped start positions), then the move is added:  x' = q * (x - c) + c + t.  No "place" means no change.
# Drags in the 3D view compose onto it (place_view.gd); the Piece tab edits it as move (cm) and turn (degrees).
extends RefCounted


static func has(p: Dictionary) -> bool:
	return p.get("place", null) is Dictionary


static func t_of(p: Dictionary) -> Vector3:
	var pl = p.get("place", null)
	if not (pl is Dictionary):
		return Vector3.ZERO
	var t: Array = pl.get("t", [0, 0, 0])
	return Vector3(float(t[0]), float(t[1]), float(t[2]))


static func q_of(p: Dictionary) -> Quaternion:
	var pl = p.get("place", null)
	if not (pl is Dictionary):
		return Quaternion.IDENTITY
	var q: Array = pl.get("q", [0, 0, 0, 1])
	var out := Quaternion(float(q[0]), float(q[1]), float(q[2]), float(q[3]))
	return out.normalized() if out.length() > 1e-9 else Quaternion.IDENTITY


# Stores a placement (dropped when it is no move and no turn, so untouched pieces stay as they were).
static func set_place(p: Dictionary, t: Vector3, q: Quaternion) -> void:
	q = q.normalized()
	if t.length() < 1e-7 and q.is_equal_approx(Quaternion.IDENTITY):
		p.erase("place")
		return
	p["place"] = {"t": [t.x, t.y, t.z], "q": [q.x, q.y, q.z, q.w]}


# Composes a further turn dq about the piece's current centre and a move dt onto the stored placement.
static func compose(p: Dictionary, dq: Quaternion, dt: Vector3) -> void:
	set_place(p, t_of(p) + dt, (dq * q_of(p)).normalized())


static func centre(pos: PackedVector3Array, first: int, count: int) -> Vector3:
	var c := Vector3.ZERO
	for i in count:
		c += pos[first + i]
	return c / maxf(1.0, float(count))


# Applies piece p's placement to its wrapped start positions pos[first .. first+count) in place.
static func apply(pos: PackedVector3Array, first: int, count: int, p: Dictionary) -> void:
	if not has(p) or count <= 0:
		return
	var c := centre(pos, first, count)
	var q := q_of(p)
	var t := t_of(p)
	for i in count:
		pos[first + i] = q * (pos[first + i] - c) + c + t


# The turn as degrees about up (yaw), across (pitch) and forward (roll), Godot's YXZ order, for the Piece tab.
static func euler_deg(p: Dictionary) -> Vector3:
	var e := Basis(q_of(p)).get_euler(EULER_ORDER_YXZ)
	return Vector3(rad_to_deg(e.x), rad_to_deg(e.y), rad_to_deg(e.z))


static func q_from_euler_deg(e: Vector3) -> Quaternion:
	return Basis.from_euler(Vector3(deg_to_rad(e.x), deg_to_rad(e.y), deg_to_rad(e.z)), EULER_ORDER_YXZ).get_rotation_quaternion()
