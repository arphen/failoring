# Cloth display data for the editor view. Per display vertex: normals (every update, from the sim's C++ display normals),
# the pattern (u,v) in metres, strain (area against the flat pattern) and curvature (folds), and an
# occlusion value in the vertex colour's alpha from the distance to the body surface. The triangles are
# split into one surface per fabric, each with its own shader material (look.gd, shaders/cloth.gdshader).
#
# Strain and curvature are recomputed every n-th update (quality heavy_every); the normals always are.
# The body distance grid is built once per body build from the standing pose; during the walk the body
# moves by a few centimetres and the contact term is not updated to follow it.
class_name ClothShade
extends RefCounted

const Look = preload("res://look.gd")

const AO_CELL := 0.02     # m, body distance grid cell
const AO_FAR := 0.06      # m, distances past this count as open air
const AO_GAP := 0.035     # m, distance at which the contact term is fully open

var _q: Dictionary = Look.quality("medium")
var _tick := 0
var _nd := 0
var _idx := PackedInt32Array()          # display triangles (sim.get_display_indices)
var _rest := PackedVector2Array()       # pattern (u,v) per display vertex, TEX_UV
var _s2 := PackedFloat32Array()         # flat-pattern area summed over each display vertex's triangles
var _h := 0.04                          # typical display triangle size, m
var _groups: Array = []                 # [{idx: PackedInt32Array, mat: ShaderMaterial}], one per fabric
var _mats := {}                         # fabric id -> ShaderMaterial
var _org := Vector3.ZERO                # body distance grid origin
var _dims := Vector3i.ZERO
var _grid := PackedFloat32Array()       # distance to the body surface, in grid cells (6-neighbour chamfer)
var _ao := PackedFloat32Array()         # per display vertex, colour alpha
var _sc := PackedVector2Array()         # per display vertex (strain, curvature), each in [-1, 1]
var _cached := false


func set_quality(level: String) -> void:
	_q = Look.quality(level)
	_tick = 0
	_cached = false
	for fid in _mats.keys():
		(_mats[fid] as ShaderMaterial).set_shader_parameter("weave_on", 1.0 if bool(_q["weave"]) else 0.0)


# Once per build. meta: plan_loader build result (n, ranges, uv); edges: midpoint edge pairs (editor mesh_edges);
# pieces: the merged pieces with their fabric; body: standing body positions; disp_idx: display triangles.
func setup(meta: Dictionary, edges: PackedInt32Array, pieces: Array, default_fabric: String, body: PackedVector3Array, disp_idx: PackedInt32Array) -> void:
	var n: int = meta["n"]
	var ecount := edges.size() / 2
	_nd = n + ecount
	_idx = disp_idx
	_rest = PackedVector2Array()
	_rest.resize(_nd)
	var piece_of := PackedInt32Array()
	piece_of.resize(_nd)
	var fabric_of: Array = []
	for pi in pieces.size():
		var pc: Dictionary = pieces[pi]
		var pid := String(pc["id"])
		fabric_of.append(String(pc.get("fabric", default_fabric)))
		var r: Dictionary = meta["ranges"][pid]
		var uv: PackedVector2Array = meta["uv"][pid]
		var f0: int = r["first"]
		for k in uv.size():
			_rest[f0 + k] = uv[k]
			piece_of[f0 + k] = pi
	for e in ecount:
		var a: int = edges[2 * e]
		var b: int = edges[2 * e + 1]
		_rest[n + e] = (_rest[a] + _rest[b]) * 0.5
		piece_of[n + e] = piece_of[a]
	# flat-pattern areas, per triangle and summed per vertex; fabric groups
	var ntri := _idx.size() / 3
	_s2 = PackedFloat32Array()
	_s2.resize(_nd)
	var len_sum := 0.0
	var by_fab := {}
	var order: Array = []
	for t in ntri:
		var a := _idx[3 * t]
		var b := _idx[3 * t + 1]
		var c := _idx[3 * t + 2]
		var p := _rest[a]
		var q := _rest[b] - p
		var r := _rest[c] - p
		var a2 := 0.5 * absf(q.x * r.y - q.y * r.x)
		_s2[a] += a2
		_s2[b] += a2
		_s2[c] += a2
		len_sum += sqrt(2.0 * a2)
		var fid := default_fabric
		if piece_of[a] < fabric_of.size():
			fid = fabric_of[piece_of[a]]
		if not by_fab.has(fid):
			by_fab[fid] = PackedInt32Array()
			order.append(fid)
		var arr: PackedInt32Array = by_fab[fid]
		arr.append(a)
		arr.append(b)
		arr.append(c)
		by_fab[fid] = arr
	_groups = []
	for fid in order:
		_groups.append({"idx": by_fab[fid], "mat": _mat(String(fid))})
	_h = maxf(len_sum / maxf(float(ntri), 1.0), 0.005)
	_build_field(body)
	_ao = PackedFloat32Array()
	_ao.resize(_nd)
	_ao.fill(1.0)
	_sc = PackedVector2Array()
	_sc.resize(_nd)
	_tick = 0
	_cached = false


func _mat(fid: String) -> ShaderMaterial:
	if not _mats.has(fid):
		var m := Look.cloth_material(fid)
		m.set_shader_parameter("weave_on", 1.0 if bool(_q["weave"]) else 0.0)
		_mats[fid] = m
	return _mats[fid]


# Writes the cloth into the ArrayMesh (one surface per fabric) and sets each surface's material on the node.
# colors: the editor's per-vertex albedo (not modified). nrm: the display normals from the sim (C++
# get_display_normals); when missing or the wrong size, look.gd computes them (Look.cloth_normals).
func fill(am: ArrayMesh, node: MeshInstance3D, disp: PackedVector3Array, colors: PackedColorArray, nrm := PackedVector3Array()) -> void:
	if _groups.is_empty() or disp.size() != _nd or colors.size() != _nd:
		return
	_tick += 1
	if nrm.size() != _nd:
		nrm = Look.cloth_normals(disp, _idx)
	var every := int(_q["heavy_every"])
	var cols := colors
	if every > 0:
		if not _cached or (_tick - 1) % every == 0:
			_update_cache(disp, nrm)
			_cached = true
		cols = colors.duplicate()
		for i in _nd:
			var c := cols[i]
			c.a = _ao[i]
			cols[i] = c
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = disp
	arr[Mesh.ARRAY_NORMAL] = nrm
	arr[Mesh.ARRAY_COLOR] = cols
	arr[Mesh.ARRAY_TEX_UV] = _rest
	arr[Mesh.ARRAY_TEX_UV2] = _sc
	am.clear_surfaces()
	for s in _groups.size():
		arr[Mesh.ARRAY_INDEX] = _groups[s]["idx"]
		am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	node.mesh = am  # the node sizes its surface overrides from the mesh, so it must see the surfaces first
	for s in _groups.size():
		node.set_surface_override_material(s, _groups[s]["mat"])


# Strain, curvature and the contact/fold occlusion. Curvature is the offset of each vertex from the average of its
# neighbours along the normal: positive on a ridge (convex), negative in a fold valley (concave).
func _update_cache(disp: PackedVector3Array, nrm: PackedVector3Array) -> void:
	var s3 := PackedFloat32Array()
	s3.resize(_nd)
	var nb := PackedVector3Array()
	nb.resize(_nd)
	var cnt := PackedFloat32Array()
	cnt.resize(_nd)
	var t := 0
	while t < _idx.size():
		var a := _idx[t]
		var b := _idx[t + 1]
		var c := _idx[t + 2]
		var pa := disp[a]
		var pb := disp[b]
		var pc := disp[c]
		var a3 := 0.5 * (pb - pa).cross(pc - pa).length()
		s3[a] += a3
		s3[b] += a3
		s3[c] += a3
		nb[a] += pb + pc
		nb[b] += pa + pc
		nb[c] += pa + pb
		cnt[a] += 2.0
		cnt[b] += 2.0
		cnt[c] += 2.0
		t += 3
	var curv_scale := 1.0 / (0.5 * _h)
	var use_ao: bool = bool(_q["ao"])
	for i in _nd:
		var strain := 0.0
		if _s2[i] > 1e-12:
			strain = s3[i] / _s2[i] - 1.0
		var sn := clampf(strain / 0.1, -1.0, 1.0)
		var cn := 0.0
		if cnt[i] > 0.0:
			var lap := nb[i] / cnt[i] - disp[i]
			cn = clampf(-lap.dot(nrm[i]) * curv_scale, -1.0, 1.0)
		_sc[i] = Vector2(sn, cn)
		var a := 1.0
		if use_ao:
			a *= 0.75 + 0.25 * smoothstep(0.0, AO_GAP, _body_dist(disp[i]))
		a *= 1.0 - 0.35 * clampf(-cn, 0.0, 1.0)
		a *= 1.0 - 0.25 * clampf(-sn, 0.0, 1.0)
		_ao[i] = a


# Distance from p to the body surface, metres, capped at AO_FAR. Nearest grid cell.
func _body_dist(p: Vector3) -> float:
	if _dims == Vector3i.ZERO:
		return AO_FAR
	var c := ((p - _org) / AO_CELL).round()
	var ix := int(c.x)
	var iy := int(c.y)
	var iz := int(c.z)
	if ix < 0 or iy < 0 or iz < 0 or ix >= _dims.x or iy >= _dims.y or iz >= _dims.z:
		return AO_FAR
	return minf(_grid[(iz * _dims.y + iy) * _dims.x + ix] * AO_CELL, AO_FAR)


# Distance grid around the body: seeds at the body vertices, then a forward and a backward raster pass
# with one-cell steps (6-neighbour chamfer). The result over-estimates diagonal distances by up to ~40%,
# which is fine for an occlusion term. Built once per body build.
func _build_field(body: PackedVector3Array) -> void:
	_grid = PackedFloat32Array()
	_dims = Vector3i.ZERO
	if body.is_empty():
		return
	var lo := body[0]
	var hi := body[0]
	for p in body:
		lo = lo.min(p)
		hi = hi.max(p)
	var pad := Vector3.ONE * (AO_FAR + AO_CELL)
	lo -= pad
	hi += pad
	_org = lo
	var span := (hi - lo) / AO_CELL
	_dims = Vector3i(ceili(span.x) + 1, ceili(span.y) + 1, ceili(span.z) + 1)
	var nx := _dims.x
	var ny := _dims.y
	var nz := _dims.z
	_grid.resize(nx * ny * nz)
	_grid.fill(AO_FAR / AO_CELL + 1.0)
	for p in body:
		var c := ((p - lo) / AO_CELL).round()
		var ix := clampi(int(c.x), 0, nx - 1)
		var iy := clampi(int(c.y), 0, ny - 1)
		var iz := clampi(int(c.z), 0, nz - 1)
		_grid[(iz * ny + iy) * nx + ix] = 0.0
	for z in nz:
		for y in ny:
			for x in nx:
				var i := (z * ny + y) * nx + x
				var d := _grid[i]
				if x > 0:
					d = minf(d, _grid[i - 1] + 1.0)
				if y > 0:
					d = minf(d, _grid[i - nx] + 1.0)
				if z > 0:
					d = minf(d, _grid[i - nx * ny] + 1.0)
				_grid[i] = d
	for z in range(nz - 1, -1, -1):
		for y in range(ny - 1, -1, -1):
			for x in range(nx - 1, -1, -1):
				var i := (z * ny + y) * nx + x
				var d := _grid[i]
				if x < nx - 1:
					d = minf(d, _grid[i + 1] + 1.0)
				if y < ny - 1:
					d = minf(d, _grid[i + nx] + 1.0)
				if z < nz - 1:
					d = minf(d, _grid[i + nx * ny] + 1.0)
				_grid[i] = d
