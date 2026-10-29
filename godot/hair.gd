# Long hair for the mannequin (a face likeness: Look > Hair > "Long (mine)"): built from the body itself, so it fits any
# preset, measurements and face. Look only: the cloth does not collide with it.
#
#   cap    the scalp (the skin shader's hairline, ported below) lifted off the head by 6 mm, 10 mm at the crown; its alpha
#          follows the hairline mask, so the hairline is soft
#   top    locks combed back from the front hairline over the scalp, on the scalp's own vertices (over the cap alone the
#          top read as a smooth helmet); their roots fade in at the hairline
#   locks  narrow ribbons in three layers round the head: from just below the crown, on the cap, they follow the outline of the head
#          outwards (never in under the jaw or round the neck: below the head's widest point a lock only falls or moves
#          out, at most 3.5 mm per cm at the sides and 8 behind), past the shoulders' top to a little below them; each
#          waves gently, more towards the end. Beside the neck they part at the shoulder (front or behind), and every lock
#          keeps clear of the body (leaning out over the shoulder ahead of time)
# Built once from the standing body in the body node's space, in the frame of the head (the eyes' lid rings, skin.gd
# head_frame), and placed on the posed head every frame by follow(), as the eyes are. The idle layer's head motion
# comes in through idle(), as for the eyes.
extends Node3D

const SkinMap = preload("res://skin.gd")
const SHADER := preload("res://shaders/hair.gdshader")
const SCALP_C := Vector3(0.0, 0.672, 0.030)   # skin.gd / skin.gdshader scalp_c (base mesh rest space)

var anchors := {}
var _mi: MeshInstance3D
var _mat: ShaderMaterial
var _rest_basis := Basis()
var _rest_mid := Vector3.ZERO
var _rest_scale := 1.0
var _built := false
var _base := PackedFloat32Array()
var _tris := PackedInt32Array()
var _nb := 0
var length_scale := 1.0     # 1: to a little below the shoulders


func _init() -> void:
	_mi = MeshInstance3D.new()
	_mi.name = "HairMesh"
	_mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	add_child(_mi)
	_mat = ShaderMaterial.new()
	_mat.shader = SHADER
	_mi.material_override = _mat


# eye_anchors: skin.gd eye_anchors (the frame); base, tris, nb: the body (body_data.json base, triangles, body vertices).
func setup(eye_anchors: Dictionary, base: PackedFloat32Array, tris: PackedInt32Array, nb: int) -> void:
	anchors = eye_anchors
	_base = base
	_tris = tris
	_nb = nb
	_built = false


func set_color(c: Color) -> void:
	_mat.set_shader_parameter("hair_col", c)


func idle(head: Transform3D) -> void:
	transform = head


# Places the hair on the posed body; the first call builds it (from the standing body: call it with that first).
func follow(verts: PackedVector3Array) -> void:
	if anchors.is_empty() or verts.size() < _nb or _nb == 0:
		visible = false
		return
	var fr: Dictionary = SkinMap.head_frame(verts, anchors["l"]["ring"], anchors["r"]["ring"], anchors["fore"])
	var mid := (SkinMap.centroid(verts, anchors["l"]["ring"]) + SkinMap.centroid(verts, anchors["r"]["ring"])) * 0.5
	if not _built:
		_rest_basis = fr["basis"]
		_rest_scale = float(fr["scale"])
		_rest_mid = mid
		_build(verts)
		_built = true
	visible = true
	var sc := float(fr["scale"]) / _rest_scale
	var b: Basis = (fr["basis"] as Basis) * _rest_basis.inverse()
	_mi.transform = Transform3D(b.scaled(Vector3(sc, sc, sc)), mid) * Transform3D(Basis(), -_rest_mid)


# The skin shader's scalp mask (skin.gdshader hair_mask) on a base-mesh rest position.
static func hair_mask(p: Vector3) -> float:
	var d := p - SCALP_C
	var r := maxf(d.length(), 1e-5)
	var dir := d / r
	var az := atan2(dir.x, dir.z)
	var el := asin(clampf(dir.y, -1.0, 1.0))
	var c1 := cos(az)
	var c2 := cos(2.0 * az)
	var line := deg_to_rad(-3.5 + 40.0 * c1 - 5.5 * c2) - deg_to_rad(6.0) * exp(-pow((absf(az) - 0.7) / 0.22, 2.0))
	var soft := deg_to_rad(3.0) + deg_to_rad(2.0) * maxf(c1, 0.0)
	var m := smoothstep(line - soft, line + soft, el)
	var ear := Vector3(signf(p.x) * 0.068, 0.633, 0.018)
	var de := (p - ear) / Vector3(0.03, 0.04, 0.034)
	m *= smoothstep(0.85, 1.15, de.length())
	m *= smoothstep(0.585, 0.60, p.y)
	return m


func _build(v: PackedVector3Array) -> void:
	var nb := _nb
	# vertex normals of the posed body
	var nrm := PackedVector3Array()
	nrm.resize(nb)
	for t in _tris.size() / 3:
		var a := _tris[3 * t]
		var b := _tris[3 * t + 1]
		var c := _tris[3 * t + 2]
		if a >= nb or b >= nb or c >= nb:
			continue
		var n := (v[b] - v[a]).cross(v[c] - v[a])
		nrm[a] += n
		nrm[b] += n
		nrm[c] += n
	var mask := PackedFloat32Array()
	mask.resize(nb)
	var top := -1e9
	var scalp_sum := Vector3.ZERO
	var scalp_n := 0
	for i in nb:
		var rp := Vector3(_base[3 * i], _base[3 * i + 1], _base[3 * i + 2])
		mask[i] = hair_mask(rp) if rp.y > 0.55 else 0.0
		if mask[i] > 0.5:
			scalp_sum += v[i]
			scalp_n += 1
			top = maxf(top, v[i].y)
	if scalp_n == 0:
		return
	var axis := scalp_sum / float(scalp_n)   # x, z: the head's vertical axis
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)

	# cap
	var cap_idx := {}
	var crown := top
	for t in _tris.size() / 3:
		var ids := [_tris[3 * t], _tris[3 * t + 1], _tris[3 * t + 2]]
		if ids[0] >= nb or ids[1] >= nb or ids[2] >= nb:
			continue
		if mask[ids[0]] < 0.05 or mask[ids[1]] < 0.05 or mask[ids[2]] < 0.05:
			continue
		for i in ids:
			var n := nrm[i].normalized()
			var lift := 0.006 + 0.004 * clampf((v[i].y - (crown - 0.12)) / 0.12, 0.0, 1.0)
			var p := v[i] + n * lift
			# strands of the cap run from the crown down and back: their direction is the gradient down the head
			var down := (Vector3(0, -1, 0) - n * n.dot(Vector3(0, -1, 0))).normalized()
			st.set_color(Color(0.5, 0.0, 0.0, mask[i]))
			st.set_normal(n)
			st.set_uv(Vector2(fposmod(atan2(p.x - axis.x, p.z - axis.z) * 6.0, 1.0), (crown - p.y) * 4.0))
			st.set_tangent(Plane(down, 1.0))
			st.add_vertex(p)

	# outline of the body round the head's axis: radius per azimuth bin and 1 cm height slab
	var bins := 72
	var y_lo := crown - 0.52
	var slabs := int(ceil((crown - y_lo) / 0.01)) + 1
	var rad := PackedFloat32Array()
	rad.resize(bins * slabs)
	for i in nb:
		var p := v[i]
		if p.y < y_lo or p.y > crown:
			continue
		var dx := p.x - axis.x
		var dz := p.z - axis.z
		var az := atan2(dx, dz)
		var bi := int(floor((az + PI) / TAU * float(bins))) % bins
		var si := int((crown - p.y) / 0.01)
		var r := sqrt(dx * dx + dz * dz)
		if r > rad[si * bins + bi]:
			rad[si * bins + bi] = r
	var radius_at := func(az: float, y: float) -> float:
		var bi := int(floor((az + PI) / TAU * float(bins))) % bins
		var si := clampi(int((crown - y) / 0.01), 0, slabs - 1)
		# a little wider than the bin (no gaps between bins), and averaged over two slabs up and down: slab by slab the
		# outline near the crown changes fast, and the locks stepped like stairs
		var acc := 0.0
		var cnt := 0
		for ds in range(-2, 3):
			var row := clampi(si + ds, 0, slabs - 1)
			var best := 0.0
			for db in [-1, 0, 1]:
				best = maxf(best, rad[row * bins + posmod(bi + db, bins)])
			if best > 0.0:
				acc += best
				cnt += 1
		return acc / float(maxi(cnt, 1))

	# locks: three layers of narrow ribbons. Normals and strand directions are per row and smooth along a lock (flat per
	# segment, each 1 cm segment lit differently: a checkerboard); the width tapers to the tip.
	var rng := RandomNumberGenerator.new()
	rng.seed = 2894
	var n_az := 120
	for layer in 3:
		var off := 0.010 + 0.008 * float(layer)
		for k in n_az:
			var az := -PI + (float(k) + 0.5 + 0.33 * float(layer) + rng.randf_range(-0.3, 0.3)) / float(n_az) * TAU
			var front := cos(az)          # 1 at the face, -1 at the back
			if front > 0.55:              # the face stays clear
				continue
			# the root: on the crown at the back, at the temples at the sides; the end: below the shoulders behind, at the
			# collarbones beside the face; the outer layers a little shorter, every lock a little different
			var y0 := crown - 0.03 - rng.randf_range(0.0, 0.015)
			var y1 := crown - (0.46 + 0.04 * clampf(-front, 0.0, 1.0) - 0.025 * float(layer) + rng.randf_range(-0.03, 0.02)) * length_scale
			var phase := rng.randf() * TAU
			var wl := rng.randf_range(0.07, 0.11)
			var amp := rng.randf_range(0.010, 0.019)
			# each lock drifts a little round the head as it falls (the frontmost all hung at one angle: a flat wall)
			var drift := rng.randf_range(-0.22, 0.22)
			var shade := rng.randf_range(0.0, 1.0)
			var half_w := 0.009 + 0.004 * rng.randf()
			var slope := lerpf(0.0035, 0.008, smoothstep(deg_to_rad(110.0), deg_to_rad(150.0), absf(az)))
			# beside the neck the hair parts at the shoulder, some to the front, most behind (falling straight, the locks
			# went through the shoulder)
			var split := 0.0
			var aa := absf(az)
			if aa > deg_to_rad(55.0) and aa < deg_to_rad(150.0):
				# hung over the shoulder's top they stood out like wings: they go round it, to where the body is narrower
				var to_front := aa < deg_to_rad(85.0) and rng.randf() < 0.5
				split = signf(az) * (-deg_to_rad(50.0) if to_front else deg_to_rad(65.0)) * (1.0 - smoothstep(deg_to_rad(120.0), deg_to_rad(150.0), aa))
			var segs := int((y0 - y1) / 0.01)
			# the lock's angle round the head per row, and the radius it needs there: clear of the body by a few mm (more
			# for the outer layers), reached ahead of time (up to 1.2 cm out per cm down) so the lock leans out over the
			# shoulder smoothly instead of standing out level
			var ang := PackedFloat32Array()
			var need := PackedFloat32Array()
			ang.resize(segs + 1)
			need.resize(segs + 1)
			for s in segs + 1:
				var y := lerpf(y0, y1, float(s) / float(segs))
				ang[s] = az + drift * smoothstep(0.05, 0.4, y0 - y) + split * smoothstep(0.08, 0.24, y0 - y)
				need[s] = float(radius_at.call(ang[s], y)) + 0.004 + 0.005 * float(layer)
			for s in range(segs - 1, -1, -1):
				need[s] = maxf(need[s], need[s + 1] - 0.012)
			var r_prev := 0.0
			var widest := false
			var pts := []
			for s in segs + 1:
				var f := float(s) / float(segs)
				var y := lerpf(y0, y1, f)
				# from the cap's surface out to the lock's layer over the first 6 cm (starting off the head, the locks' tops
				# stood out from the cap as a shelf)
				var r: float = float(radius_at.call(az, y)) + lerpf(0.007, off, smoothstep(0.0, 0.06, y0 - y))
				# below the head's widest point a lock only falls or moves out
				if s > 0 and r < r_prev:
					widest = true
				if widest:
					# and only out at a slope a falling lock can take: following the outline up the shoulders flung the locks
					# out level and splintered them (so 3.5 mm per cm at the sides: they pass behind the shoulder's top); the
					# upper back rises gently and needs more (8 mm per cm behind, or the locks sink into it)
					r = clampf(r, r_prev, r_prev + slope)
				r = maxf(r, need[s])
				r_prev = r
				var wave := amp * sin(TAU * (y0 - y) / wl + phase) * smoothstep(0.08, 0.3, y0 - y)
				var a2 := ang[s] + wave * 0.6 / maxf(r, 0.05)
				# the wave mostly sideways (a2): in and out, it tilted the ribbons into the light like curling paper
				pts.append(Vector3(axis.x + sin(a2) * (r + 0.3 * wave), y, axis.z + cos(a2) * (r + 0.3 * wave)))
			# per row: direction, outward normal, across, half width (tapering to a third at the tip)
			var rows := []
			for s in segs + 1:
				var pa: Vector3 = pts[maxi(s - 1, 0)]
				var pb: Vector3 = pts[mini(s + 1, segs)]
				var t := (pb - pa).normalized()
				var p: Vector3 = pts[s]
				var out := Vector3(p.x - axis.x, 0.0, p.z - axis.z).normalized()
				out = (out - t * out.dot(t)).normalized()
				var side := t.cross(out).normalized()
				var f := float(s) / float(segs)
				rows.append([p, t, out, side, half_w * lerpf(1.0, 0.35, f * f), f])
			for s in segs:
				var ra: Array = rows[s]
				var rb: Array = rows[s + 1]
				var verts := [[ra, -1.0, 0.0], [ra, 1.0, 1.0], [rb, 1.0, 1.0], [rb, -1.0, 0.0]]
				for q in [0, 1, 2, 0, 2, 3]:
					var row: Array = verts[q][0]
					st.set_color(Color(shade, 1.0, float(layer) / 2.0, 1.0))
					st.set_normal(row[2])
					st.set_uv(Vector2(verts[q][2], row[5]))
					st.set_tangent(Plane(row[1], 1.0))
					st.add_vertex((row[0] as Vector3) + (row[3] as Vector3) * (float(row[4]) * float(verts[q][1])))
	# top locks: combed back from the front hairline over the scalp (the cap alone read as a smooth helmet). Each follows
	# the scalp's own vertices in a slice x = x0 (they lie on the head's surface, whatever its shape), lifted 5 to 9 mm,
	# from the hairline over the crown to well behind it, where the falling locks take over
	var cy := crown - 0.10
	for layer in 2:
		var lift := 0.005 + 0.004 * float(layer)
		var n_x := 22
		for k in n_x:
			var x0 := axis.x + lerpf(-0.07, 0.07, (float(k) + 0.5 * float(layer) + rng.randf_range(-0.25, 0.25)) / float(n_x - 1))
			# the slice's scalp vertices, binned by their angle round the head's centre (0 on top, + to the front)
			var bins_t := {}
			for i in nb:
				if mask[i] < 0.3 or absf(v[i].x - x0) > 0.006:
					continue
				var th := atan2(v[i].z - axis.z, v[i].y - cy)
				var b := int(floor(th / 0.07))
				if not bins_t.has(b):
					bins_t[b] = [Vector3.ZERO, Vector3.ZERO, 0]
				bins_t[b][0] += v[i]
				bins_t[b][1] += nrm[i]
				bins_t[b][2] += 1
			var keys: Array = bins_t.keys()
			keys.sort()
			keys.reverse()   # front (the hairline) first
			var path := []
			for b in keys:
				if float(b) * 0.07 < -1.25:   # well behind the crown: the falling locks cover the rest
					break
				var e: Array = bins_t[b]
				var p: Vector3 = e[0] / float(e[2])
				path.append(p + (e[1] as Vector3).normalized() * lift)
			if path.size() < 4:
				continue
			var shade := rng.randf_range(0.0, 1.0)
			var half_w := 0.010 + 0.003 * rng.randf()
			var n := path.size()
			var rows := []
			for s2 in n:
				var pa: Vector3 = path[maxi(s2 - 1, 0)]
				var pb: Vector3 = path[mini(s2 + 1, n - 1)]
				var t := (pb - pa).normalized()
				var out: Vector3 = ((path[s2] as Vector3) - Vector3(axis.x, cy, axis.z)).normalized()
				out = (out - t * out.dot(t)).normalized()
				rows.append([path[s2], t, out, t.cross(out).normalized(), half_w, float(s2) / float(n - 1)])
			for s2 in n - 1:
				var ra: Array = rows[s2]
				var rb: Array = rows[s2 + 1]
				var verts := [[ra, -1.0, 0.0], [ra, 1.0, 1.0], [rb, 1.0, 1.0], [rb, -1.0, 0.0]]
				for q in [0, 1, 2, 0, 2, 3]:
					var row: Array = verts[q][0]
					st.set_color(Color(shade, 1.0, 0.5 + 0.5 * float(layer), 1.0))
					st.set_normal(row[2])
					st.set_uv(Vector2(verts[q][2], row[5]))
					st.set_tangent(Plane(row[1], 1.0))
					st.add_vertex((row[0] as Vector3) + (row[3] as Vector3) * (float(row[4]) * float(verts[q][1])))
	_mi.mesh = st.commit()
