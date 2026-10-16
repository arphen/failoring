# Stitch types and the needle that sews them (editor.gd draws it in the Sew stage, stage 3). Look only: the type is stored in the seam
# (seam["stitch"], default "straight") and does not reach the solver.
#
# A seam is drawn as stitch pairs: one cloth vertex on each side of the seam, in order along it. The needle visits the pairs one
# after the other and goes in and out through the two layers; each type moves differently and leaves a trail of its own colour:
#   straight   in and out through both layers at every pair, a crisp white thread
#   zigzag     sweeps from one side of the seam to the other and back, an orange thread
#   overlock   loops over the edge (a circle through the two layers), a teal thread
#   topstitch  stays on one side, small quick stitches, a gold thread
#   basting    long loose stitches (every fourth pair), a red thread
extends RefCounted

const TYPES := ["straight", "zigzag", "overlock", "topstitch", "basting"]
const STYLE := {
	"straight": {"label": "Straight", "col": Color("#f2efe6"), "tip": "A plain seam: the needle goes in and out through both layers"},
	"zigzag": {"label": "Zigzag", "col": Color("#ff9d4d"), "tip": "Sweeps across the seam: stretchy joins and raw edges"},
	"overlock": {"label": "Overlock", "col": Color("#3fd3c0"), "tip": "Loops over the edge: seams in knits, no fraying"},
	"topstitch": {"label": "Topstitch", "col": Color("#e8c34a"), "tip": "Small stitches on one side: a visible finishing line"},
	"basting": {"label": "Basting", "col": Color("#ff5a5a"), "tip": "Long loose stitches that hold the pieces for now"},
}
const NEEDLE_LEN := 0.045
const MAX_STITCHES := 28.0
const NEEDLE_SCALE := 3.2        # drawn this much larger than a real needle: they are a picture of the sewing, and must read from a metre or two


static func type_of(seam: Dictionary) -> String:
	var t := String(seam.get("stitch", "straight"))
	return t if STYLE.has(t) else "straight"


static func colour(type: String) -> Color:
	return (STYLE[type] as Dictionary)["col"] if STYLE.has(type) else Color.WHITE


# Where the needle is, for a seam of `m` pairs at the continuous pair position `k` (0 .. m): returns Vector3(pair position along the seam
# 0..m-1, side 0 (on the A layer) .. 1 (on the B layer), lift in metres above the cloth).
static func needle(type: String, k: float, m: int) -> Vector3:
	# A needle makes at most MAX_STITCHES stitches over a path, however many pairs the path has (a long seam would otherwise be
	# sewn faster than the screen can show): it moves in "stitch" units, and a unit is span / stitches pairs long.
	var span := float(maxi(m - 1, 1))
	var cyc := minf(span, MAX_STITCHES)
	var v := _needle_units(type, k / span * cyc, cyc)
	return Vector3(clampf(v.x * span / cyc, 0.0, span), v.y, v.z)


static func _needle_units(type: String, k: float, m: float) -> Vector3:
	var last := maxf(m, 1.0)
	match type:
		"zigzag":
			# one sweep across per two pairs
			return Vector3(minf(k, last), 0.5 - 0.5 * cos(k * PI), 0.004 * maxf(sin(k * PI), 0.0))
		"overlock":
			var a := k * TAU * 0.5
			return Vector3(minf(k, last), 0.5 - 0.5 * cos(a), 0.012 * maxf(sin(a), 0.0))
		"topstitch":
			var ph := fposmod(k * 2.0, 2.0)
			return Vector3(minf(k, last), 0.18 + 0.1 * (0.5 - 0.5 * cos(ph * PI)), 0.007 * maxf(sin(ph * PI), 0.0))
		"basting":
			# the needle goes through the layers at every fourth pair, then flies on in a long arc above the cloth
			var seg := k / 4.0
			var base := floorf(seg) * 4.0
			var f := fposmod(seg, 1.0)
			if f < 0.35:
				return Vector3(minf(base, last), 0.5 - 0.5 * cos(f / 0.35 * TAU), 0.004 * maxf(sin(f / 0.35 * TAU), 0.0))
			var g := (f - 0.35) / 0.65
			return Vector3(minf(base + 4.0 * smoothstep(0.0, 1.0, g), last), 0.0, 0.03 * sin(g * PI))
		_:
			# straight: in through A, out through B, at every pair
			return Vector3(minf(k, last), 0.5 - 0.5 * cos(k * TAU), 0.006 * maxf(sin(k * TAU), 0.0))


# The needle's world position and axis for the pair list `pairs` ([vertex a, vertex b] indices into `cloth`), at pair position k.
static func needle_pose(cloth: PackedVector3Array, pairs: Array, type: String, k: float) -> Dictionary:
	var m := pairs.size()
	if m == 0:
		return {}
	var nd := needle(type, k, m)
	var f := clampf(nd.x, 0.0, float(m - 1))
	var i0 := int(floor(f))
	var i1 := mini(i0 + 1, m - 1)
	var u := f - float(i0)
	var a := (cloth[int(pairs[i0][0])] as Vector3).lerp(cloth[int(pairs[i1][0])], u)
	var b := (cloth[int(pairs[i0][1])] as Vector3).lerp(cloth[int(pairs[i1][1])], u)
	var across := b - a
	var axis := across.normalized() if across.length() > 1.0e-5 else Vector3.DOWN
	# the tip is at the point between the layers, the needle stands over it along the axis through both
	var tip := a.lerp(b, nd.y) + Vector3.UP * nd.z
	return {"tip": tip, "axis": axis}


# A needle: a thin steel cone with its eye, tip at the origin and pointing along -z (so look_at turns the tip towards the target).
static func needle_mesh() -> ArrayMesh:
	var v := PackedVector3Array()
	var n := PackedVector3Array()
	var idx := PackedInt32Array()
	var segs := 8
	var rings := [[0.0, 0.00004], [0.012, 0.0004], [0.03, 0.0008], [NEEDLE_LEN, 0.0007]]
	for i in rings.size():
		var z: float = rings[i][0]
		var r: float = rings[i][1]
		for j in segs:
			var a := TAU * float(j) / float(segs)
			v.append(Vector3(r * cos(a), r * sin(a), z))
			n.append(Vector3(cos(a), sin(a), 0.0))
	for i in rings.size() - 1:
		for j in segs:
			var a := i * segs + j
			var b := i * segs + (j + 1) % segs
			idx.append_array([a, a + segs, b, b, a + segs, b + segs])
	var arr: Array = []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = v
	arr[Mesh.ARRAY_NORMAL] = n
	arr[Mesh.ARRAY_INDEX] = idx
	var m := ArrayMesh.new()
	m.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	return m


# The needle's trail: the positions it had over the last `span` pair steps, as line segments with a fading alpha (a trace of its motion).
static func trail(cloth: PackedVector3Array, pairs: Array, type: String, k: float, span: float, steps: int, col: Color, verts: PackedVector3Array, cols: PackedColorArray, right := Vector3.RIGHT) -> void:
	var prev := Vector3.ZERO
	var have := false
	for j in range(steps, -1, -1):
		var kk := k - span * float(j) / float(steps)
		if kk < 0.0:
			continue
		var pose := needle_pose(cloth, pairs, type, kk)
		if pose.is_empty():
			continue
		var p: Vector3 = pose["tip"]
		if have and p.distance_to(prev) > 0.12:
			have = false                  # the needle hopped to a distant seam of the join: no trace across the gap
		if have:
			var al := 1.0 - float(j) / float(steps)
			for off in [-1.0, 0.0, 1.0]:                  # three parallel lines: a trace 3 px wide
				verts.append(prev + right * 0.0016 * off)
				verts.append(p + right * 0.0016 * off)
				cols.append(Color(col, 0.05 + 0.9 * al * al))
				cols.append(Color(col, 0.05 + 0.9 * al * al))
		prev = p
		have = true
