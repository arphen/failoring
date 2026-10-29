# Eyeballs for the mannequin: the MakeHuman body has open sockets behind its lids and no eyes. Two spheres with a
# cornea bulge sit in the sockets and follow the head every frame from the lid-margin vertices (skin.gd eye_anchors),
# so they stay in place on every preset, while walking and with the soft-tissue motion. Look only.
extends Node3D

const EYE_SHADER := preload("res://shaders/eye.gdshader")
const SkinMap = preload("res://skin.gd")
const IRIS := {
	"brown": [Color("#3d2412"), Color("#7a5426")],
	"hazel": [Color("#4a3a1c"), Color("#8f7a3a")],
	"green": [Color("#3c5a3a"), Color("#8a8a4a")],
	"blue": [Color("#3e5f80"), Color("#9ab7cf")],
	"grey": [Color("#55606a"), Color("#a6aeb3")],
}
const CONVERGE := 0.035          # rad: each eye turns this much inwards (a gaze at about 1 m)
const DROP := 0.06               # rad: a slightly lowered gaze, as a relaxed person looks ahead

var anchors: Dictionary = {}
var _eyes := {}
var _mat: ShaderMaterial
var gaze := Vector2.ZERO          # radians added to the resting gaze: x to the body's left, y up (idle.gd)
var _frame := {}                  # per eye: centre and head orientation of the last follow(), so the gaze can move without it


func setup(a: Dictionary) -> void:
	anchors = a
	if _mat == null:
		_mat = ShaderMaterial.new()
		_mat.shader = EYE_SHADER
		set_iris("brown")
	var mesh := eyeball_mesh()
	for s in ["l", "r"]:
		if not _eyes.has(s):
			var mi := MeshInstance3D.new()
			mi.name = "Eye_" + s
			mi.mesh = mesh
			mi.material_override = _mat
			mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF  # tiny, and its shadow would land on the lids
			add_child(mi)
			_eyes[s] = mi


func set_iris(key: String) -> void:
	if _mat == null:
		return
	var c: Array = IRIS.get(key, IRIS["brown"])
	_mat.set_shader_parameter("iris_out", c[0])
	_mat.set_shader_parameter("iris_in", c[1])


# An iris of one colour (a face likeness: the colour sampled from photos): darker at the rim, lighter round the pupil.
func set_iris_rgb(c: Color) -> void:
	if _mat == null:
		return
	# photographed irises read dark (shaded by the lids, a dim room): lift to a believable lightness, keep the hue
	var l := maxf(c.get_luminance(), 0.01)
	var lit := Color(c.r, c.g, c.b) * clampf(0.36 / l, 1.0, 3.0)
	_mat.set_shader_parameter("iris_out", lit * 0.55)
	_mat.set_shader_parameter("iris_in", lit * 1.1)


# Places both eyes on a posed body (vertex positions in the body node's space).
func follow(verts: PackedVector3Array) -> void:
	if anchors.is_empty():
		return
	var ring_l: PackedInt32Array = anchors["l"]["ring"]
	var ring_r: PackedInt32Array = anchors["r"]["ring"]
	var fore: PackedInt32Array = anchors["fore"]
	if verts.size() <= _max_index():
		visible = false
		return
	visible = true
	var fr: Dictionary = SkinMap.head_frame(verts, ring_l, ring_r, fore)
	var b: Basis = fr["basis"]
	var sc: float = float(fr["scale"]) / float(anchors["rest_scale"])
	var r: float = float(anchors["radius"]) * sc
	var rb_inv: Basis = (anchors["rest_basis"] as Basis).inverse()
	for s in ["l", "r"]:
		var ring: PackedInt32Array = anchors[s]["ring"]
		var lc: Vector3 = SkinMap.centroid(verts, ring)
		var off: Vector3 = anchors[s]["off"]
		var c := lc + b * off * float(fr["scale"])
		# the head's rotation since the rest pose; the gaze (straight ahead, +z, plus the resting convergence) goes on top
		_frame[s] = {"c": c, "hb": b * rb_inv, "r": r}
	# the skin shader swings the lids about the eyes: it needs the rest-to-drawn map of the head (rotation and scale)
	var bm := (get_parent() as MeshInstance3D).material_override as ShaderMaterial if get_parent() is MeshInstance3D else null
	if bm != null:
		bm.set_shader_parameter("lid_basis", (b * rb_inv).scaled(Vector3(sc, sc, sc)))
	_place()


# Puts the eyeballs where the last follow() found the sockets, looking where `gaze` says.
func _place() -> void:
	for s in ["l", "r"]:
		if not _frame.has(s):
			continue
		var f: Dictionary = _frame[s]
		var yaw := (-CONVERGE if s == "l" else CONVERGE) + gaze.x
		var eb: Basis = (f["hb"] as Basis) * Basis.from_euler(Vector3(DROP - gaze.y, yaw, 0.0))
		var r: float = f["r"]
		(_eyes[s] as MeshInstance3D).transform = Transform3D(eb.scaled(Vector3(r, r, r)), f["c"])


# The idle layer's eyes (idle.gd): a new gaze, and the head's motion on top of the pose (the eyes follow the skinned head).
func idle(g: Vector2, head: Transform3D) -> void:
	transform = head
	if g != gaze:
		gaze = g
		_place()


var _maxi := -1
func _max_index() -> int:
	if _maxi < 0:
		for k in ["fore"]:
			for i in (anchors[k] as PackedInt32Array):
				_maxi = maxi(_maxi, i)
		for s in ["l", "r"]:
			for i in (anchors[s]["ring"] as PackedInt32Array):
				_maxi = maxi(_maxi, i)
	return _maxi


# Unit eyeball looking down +z, with the cornea: a sphere of 0.78 radii whose apex stands 0.09 radii proud of the
# eyeball; it meets the eyeball at the limbus, about 38 degrees off the axis (the iris edge, iris_r in eye.gdshader).
static func eyeball_mesh() -> ArrayMesh:
	var rings := 40
	var segs := 36
	var v := PackedVector3Array()
	var n := PackedVector3Array()
	var idx := PackedInt32Array()
	for i in rings + 1:
		var th := PI * float(i) / float(rings)      # 0 at the front (+z)
		for j in segs + 1:
			var ph := TAU * float(j) / float(segs)
			var d := Vector3(sin(th) * cos(ph), sin(th) * sin(ph), cos(th))
			var rc := 0.78
			var cz := 1.09 - rc
			var tc := cz * cos(th) + sqrt(maxf(rc * rc - cz * cz * sin(th) * sin(th), 0.0))
			# smooth maximum of the eyeball (1) and the cornea, over 0.01 radii: no crease at the limbus
			var k := 0.01
			var hmax := clampf(0.5 + 0.5 * (tc - 1.0) / k, 0.0, 1.0)
			var rr := lerpf(1.0, tc, hmax) + k * hmax * (1.0 - hmax)
			v.append(d * rr)
			n.append(d)
	for i in rings:
		for j in segs:
			var a := i * (segs + 1) + j
			var b := a + segs + 1
			# clockwise seen from outside (Godot's front faces)
			idx.append_array([a, a + 1, b, a + 1, b + 1, b])
	# normals from the bulged surface
	var nn := PackedVector3Array()
	nn.resize(v.size())
	for t in range(0, idx.size(), 3):
		var fn := (v[idx[t + 2]] - v[idx[t]]).cross(v[idx[t + 1]] - v[idx[t]])
		for k in 3:
			nn[idx[t + k]] += fn
	for i in nn.size():
		nn[i] = nn[i].normalized() if nn[i].length() > 1e-9 else n[i]
	var arr: Array = []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = v
	arr[Mesh.ARRAY_NORMAL] = nn
	arr[Mesh.ARRAY_INDEX] = idx
	var m := ArrayMesh.new()
	m.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	return m
