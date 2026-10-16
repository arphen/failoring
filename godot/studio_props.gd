# The tailor's studio's furniture and props (studio.gd places them): meshes built in code, surfaces from
# shaders/studio_surface.gdshader. Look only. Nothing here reaches the solver, the collision body or a test number.
# Conventions: metres, y up, +z towards the default camera. Each builder returns a Node3D with its origin on the floor
# under the object's centre (or at its anchor), so studio.gd only has to position and turn it.
extends RefCounted

const SURF := preload("res://shaders/studio_surface.gdshader")

static var _cache := {}


# ---------------------------------------------------------------- materials
# kind: see studio_surface.gdshader. p: uniform name -> value (col_a, col_b, col_c, scale, wear, dust, seed, pattern, ...).
# Props pattern in object space (use_world false) unless the caller says otherwise.
static func surf(kind: int, p: Dictionary = {}) -> ShaderMaterial:
	var key := str(kind) + str(p)
	if _cache.has(key):
		return _cache[key]
	var m := ShaderMaterial.new()
	m.shader = SURF
	m.set_shader_parameter("kind", kind)
	m.set_shader_parameter("use_world", false)
	for k in p.keys():
		m.set_shader_parameter(k, p[k])
	_cache[key] = m
	return m


static func world_surf(kind: int, p: Dictionary = {}) -> ShaderMaterial:
	var q := p.duplicate()
	q["use_world"] = true
	return surf(kind, q)


static func metal(c: Color, rough: float, metallic: float = 1.0) -> StandardMaterial3D:
	var key := "m" + str(c) + str(rough) + str(metallic)
	if _cache.has(key):
		return _cache[key]
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.metallic = metallic
	m.roughness = rough
	_cache[key] = m
	return m


static func glow(c: Color, energy: float) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c * 0.25
	m.roughness = 0.9
	m.emission_enabled = true
	m.emission = c
	m.emission_energy_multiplier = energy
	return m


static func mi(mesh: Mesh, mat: Material, xf: Transform3D = Transform3D.IDENTITY, shadow := true) -> MeshInstance3D:
	var n := MeshInstance3D.new()
	n.mesh = mesh
	n.material_override = mat
	n.transform = xf
	n.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON if shadow else GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	return n


static func box(s: Vector3) -> BoxMesh:
	var b := BoxMesh.new()
	b.size = s
	return b


static func cyl(r_top: float, r_bot: float, h: float, segs: int = 24) -> CylinderMesh:
	var c := CylinderMesh.new()
	c.top_radius = r_top
	c.bottom_radius = r_bot
	c.height = h
	c.radial_segments = segs
	c.rings = 1
	return c


static func at(p: Vector3, rot_y_deg := 0.0) -> Transform3D:
	return Transform3D(Basis(Vector3.UP, deg_to_rad(rot_y_deg)), p)


# a box between two points, `w` wide in the plane perpendicular to the segment (rails, struts, legs)
static func strut(a: Vector3, b: Vector3, w: float, mat: Material, shadow := true) -> MeshInstance3D:
	var d := b - a
	var len := d.length()
	var up := d / len
	var ref := Vector3.RIGHT if absf(up.x) < 0.9 else Vector3.FORWARD
	var x := up.cross(ref).normalized()
	var z := x.cross(up)
	var bm := box(Vector3(w, len, w))
	return mi(bm, mat, Transform3D(Basis(x, up, z), (a + b) * 0.5), shadow)


static func rod(a: Vector3, b: Vector3, r: float, mat: Material, shadow := true) -> MeshInstance3D:
	var d := b - a
	var len := d.length()
	var up := d / len
	var ref := Vector3.RIGHT if absf(up.x) < 0.9 else Vector3.FORWARD
	var x := up.cross(ref).normalized()
	var z := x.cross(up)
	return mi(cyl(r, r, len, 12), mat, Transform3D(Basis(x, up, z), (a + b) * 0.5), shadow)


# ---------------------------------------------------------------- mesh builders
# Godot's front faces are clockwise seen from the front: flip any triangle that runs the other way round, judged
# against the vertex normals, so the builders below need not care about winding.
static func finish(v: PackedVector3Array, n: PackedVector3Array, idx: PackedInt32Array, uv: PackedVector2Array = PackedVector2Array()) -> ArrayMesh:
	var ix := idx.duplicate()
	for t in range(0, ix.size(), 3):
		var a := v[ix[t]]
		var fn := (v[ix[t + 1]] - a).cross(v[ix[t + 2]] - a)
		if fn.dot(n[ix[t]] + n[ix[t + 1]] + n[ix[t + 2]]) > 0.0:
			var tmp := ix[t + 1]
			ix[t + 1] = ix[t + 2]
			ix[t + 2] = tmp
	var arr: Array = []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = v
	arr[Mesh.ARRAY_NORMAL] = n
	if uv.size() == v.size():
		arr[Mesh.ARRAY_TEX_UV] = uv
	arr[Mesh.ARRAY_INDEX] = ix
	var m := ArrayMesh.new()
	m.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	return m


# A surface of revolution about y. profile: Vector2(radius, y) from the bottom up. sx, sz stretch it (elliptical sections).
static func lathe(profile: Array, segs: int = 32, sx := 1.0, sz := 1.0) -> ArrayMesh:
	var v := PackedVector3Array()
	var n := PackedVector3Array()
	var idx := PackedInt32Array()
	var np := profile.size()
	for i in np:
		var p: Vector2 = profile[i]
		var d: Vector2 = (profile[mini(i + 1, np - 1)] as Vector2) - (profile[maxi(i - 1, 0)] as Vector2)
		var nr := Vector2(d.y, -d.x).normalized()
		for j in segs + 1:
			var a := TAU * float(j) / float(segs)
			var c := cos(a)
			var s := sin(a)
			v.append(Vector3(p.x * c * sx, p.y, p.x * s * sz))
			n.append(Vector3(nr.x * c / sx, nr.y, nr.x * s / sz).normalized())
	for i in np - 1:
		for j in segs:
			var a := i * (segs + 1) + j
			var b := a + segs + 1
			idx.append_array([a, b, a + 1, a + 1, b, b + 1])
	return finish(v, n, idx)


# A slab with vertical sides, rounded vertical corners and a rounded top edge, bottom at y0 (a platform, a step).
# part 0: the sides and the bevel; part 1: the flat top (so the two can take different materials).
static func rounded_slab(w: float, d: float, h: float, bev: float, corner: float, y0 := 0.0, part := 0, nb := 5, nc := 6) -> ArrayMesh:
	var pts: Array = []
	var nrm: Array = []
	var cxz := [Vector2(w * 0.5 - corner, d * 0.5 - corner), Vector2(-w * 0.5 + corner, d * 0.5 - corner),
		Vector2(-w * 0.5 + corner, -d * 0.5 + corner), Vector2(w * 0.5 - corner, -d * 0.5 + corner)]
	for k in 4:
		for s in nc + 1:
			var a := deg_to_rad(90.0 * float(k) + 90.0 * float(s) / float(nc))
			var dir := Vector2(cos(a), sin(a))
			pts.append((cxz[k] as Vector2) + dir * corner)
			nrm.append(dir)
	var rings: Array = [[0.0, y0, 0.0], [0.0, y0 + h - bev, 0.0]]     # inset, y, bevel angle
	for s in range(1, nb + 1):
		var a := 0.5 * PI * float(s) / float(nb)
		rings.append([bev * (1.0 - cos(a)), y0 + h - bev + bev * sin(a), a])
	var v := PackedVector3Array()
	var n := PackedVector3Array()
	var idx := PackedInt32Array()
	var np := pts.size()
	for r in rings:
		for i in np:
			var p: Vector2 = (pts[i] as Vector2) - (nrm[i] as Vector2) * float(r[0])
			var nn: Vector2 = nrm[i]
			var ca := cos(float(r[2]))
			v.append(Vector3(p.x, float(r[1]), p.y))
			n.append(Vector3(nn.x * ca, sin(float(r[2])), nn.y * ca))
	if part == 0:
		for r in rings.size() - 1:
			for i in np:
				var a := r * np + i
				var b := r * np + (i + 1) % np
				idx.append_array([a, a + np, b, b, a + np, b + np])
		return finish(v, n, idx)
	# top cap: a fan from the centre over the last ring
	var top_c := v.size()
	v.append(Vector3(0.0, y0 + h, 0.0))
	n.append(Vector3.UP)
	var base := v.size()
	var last := (rings.size() - 1) * np
	for i in np:
		v.append(v[last + i])
		n.append(Vector3.UP)
	for i in np:
		idx.append_array([top_c, base + i, base + (i + 1) % np])
	return finish(v, n, idx)


# A 2D profile (z, y) extruded along x: the backdrop sweep. Normals face +z/+y (into the room).
static func sweep(profile: Array, width: float) -> ArrayMesh:
	var v := PackedVector3Array()
	var n := PackedVector3Array()
	var idx := PackedInt32Array()
	var np := profile.size()
	for i in np:
		var p: Vector2 = profile[i]
		var d: Vector2 = (profile[mini(i + 1, np - 1)] as Vector2) - (profile[maxi(i - 1, 0)] as Vector2)
		var nn := Vector2(-d.y, d.x).normalized()
		if nn.x < 0.0 and nn.y < 0.0:
			nn = -nn
		for s in 2:
			v.append(Vector3(width * (float(s) - 0.5), p.y, p.x))
			n.append(Vector3(0.0, nn.y, nn.x))
	for i in np - 1:
		var a := 2 * i
		idx.append_array([a, a + 2, a + 1, a + 1, a + 2, a + 3])
	return finish(v, n, idx)


# ---------------------------------------------------------------- fabric
const BOLT_COLORS := [
	["#2a3550", 0], ["#6b2d3a", 0], ["#8d8576", 0], ["#31402f", 0], ["#c9b68e", 0], ["#14161a", 0],
	["#5a6f86", 1], ["#3b3a3f", 3], ["#7a4b2a", 2], ["#a9a7a0", 3], ["#9a3b2a", 0], ["#d8d2c4", 0],
	["#46564f", 2], ["#23283a", 3], ["#a68b4f", 0], ["#6d6a8a", 1],
]


static func cloth_mat(hex: String, pattern: int, scale := 420.0, ps := 0.012) -> ShaderMaterial:
	var c := Color(hex)
	return surf(4, {"col_a": c, "col_b": c.lightened(0.28) if pattern != 3 else c.lightened(0.7), "col_c": c.darkened(0.35), "scale": scale,
		"pattern": pattern, "pat_size": ps, "dust": 0.4, "sheen": 0.3})


# A bolt of cloth lying end-on in a cubby: a cardboard core and the wound cloth, `len` deep, axis along z.
static func bolt(r: float, len: float, hex: String, pattern: int) -> Node3D:
	var root := Node3D.new()
	var body := cyl(r, r, len, 28)
	root.add_child(mi(body, cloth_mat(hex, pattern), Transform3D(Basis(Vector3.RIGHT, PI * 0.5), Vector3.ZERO)))
	var core := cyl(0.026, 0.026, len + 0.05, 14)
	root.add_child(mi(core, surf(7, {"col_a": Color("#b49a72"), "col_b": Color("#8a7350"), "dust": 0.6}), Transform3D(Basis(Vector3.RIGHT, PI * 0.5), Vector3.ZERO), false))
	# the loose end: a fold of the cloth hanging a little over the front of the shelf
	return root


static func folded_stack(w: float, d: float, rng: RandomNumberGenerator) -> Node3D:
	var root := Node3D.new()
	var y := 0.0
	var layers := rng.randi_range(4, 9)
	for i in layers:
		var c: Array = BOLT_COLORS[rng.randi() % BOLT_COLORS.size()]
		var th := rng.randf_range(0.03, 0.055)
		var ww := w * rng.randf_range(0.9, 1.0)
		root.add_child(mi(box(Vector3(ww, th, d * rng.randf_range(0.9, 1.0))), cloth_mat(String(c[0]), int(c[1]), 360.0), at(Vector3(rng.randf_range(-0.012, 0.012), y + th * 0.5, 0.0), rng.randf_range(-2.0, 2.0)), false))
		y += th
	return root


# Cubby shelving against a wall: `cols` x `rows` compartments, rolls of cloth end-on and folded stacks. The open front
# faces +z; the origin is on the floor at the front centre of the unit. Floor y is `fy`.
static func shelving(cols: int, rows: int, rng: RandomNumberGenerator) -> Node3D:
	var root := Node3D.new()
	var cw := 0.68
	var ch := 0.5
	var depth := 0.5
	var t := 0.03
	var w := float(cols) * cw + t
	var h := float(rows) * ch + t
	var wood := surf(3, {"col_a": Color("#6a4a2e"), "col_b": Color("#8a6644"), "col_c": Color("#a89a86"), "scale": 1.0, "wear": 0.6, "dust": 0.6, "grain_axis": Vector3(1, 0, 0)})
	var wood_v := surf(3, {"col_a": Color("#6a4a2e"), "col_b": Color("#8a6644"), "col_c": Color("#a89a86"), "scale": 1.0, "wear": 0.6, "dust": 0.6, "grain_axis": Vector3(0, 1, 0)})
	for i in cols + 1:
		root.add_child(mi(box(Vector3(t, h, depth)), wood_v, at(Vector3(-w * 0.5 + t * 0.5 + float(i) * cw, h * 0.5, -depth * 0.5))))
	for j in rows + 1:
		root.add_child(mi(box(Vector3(w, t, depth)), wood, at(Vector3(0.0, t * 0.5 + float(j) * ch, -depth * 0.5))))
	root.add_child(mi(box(Vector3(w, h, 0.012)), wood, at(Vector3(0.0, h * 0.5, -depth + 0.006)), false))
	for j in rows:
		for i in cols:
			var cx := -w * 0.5 + t * 0.5 + (float(i) + 0.5) * cw
			var cy := t + float(j) * ch
			var roll := rng.randf()
			if roll < 0.62:
				var r := rng.randf_range(0.1, 0.2)
				var c: Array = BOLT_COLORS[rng.randi() % BOLT_COLORS.size()]
				var b := bolt(r, depth - 0.06, String(c[0]), int(c[1]))
				b.position = Vector3(cx + rng.randf_range(-0.06, 0.06), cy + r + 0.002, -depth * 0.5 + 0.01 + rng.randf_range(-0.01, 0.02))
				root.add_child(b)
				if rng.randf() < 0.35:   # a second, slimmer roll beside it
					var r2 := rng.randf_range(0.06, 0.09)
					var c2: Array = BOLT_COLORS[rng.randi() % BOLT_COLORS.size()]
					var b2 := bolt(r2, depth - 0.08, String(c2[0]), int(c2[1]))
					b2.position = Vector3(cx + r + r2 * 0.7, cy + r2 + 0.002, -depth * 0.5)
					if b2.position.x < cx + cw * 0.5 - r2:
						root.add_child(b2)
			elif roll < 0.92:
				var st := folded_stack(cw * 0.78, depth * 0.8, rng)
				st.position = Vector3(cx, cy, -depth * 0.5 + 0.02)
				root.add_child(st)
	return root


# ---------------------------------------------------------------- garments on a rack
# A jacket or coat on a hanger, facing +x, hanging from its hook at the origin: a body of elliptical sections with
# folds, two sleeves, a hanger.
static func garment(rng: RandomNumberGenerator, long_coat: bool) -> Node3D:
	var root := Node3D.new()
	var hex: String = (BOLT_COLORS[rng.randi() % BOLT_COLORS.size()] as Array)[0]
	var pat: int = (BOLT_COLORS[rng.randi() % BOLT_COLORS.size()] as Array)[1]
	var mat := cloth_mat(hex, pat, 300.0, 0.02)
	var length := rng.randf_range(1.05, 1.2) if long_coat else rng.randf_range(0.68, 0.8)
	var sw := rng.randf_range(0.2, 0.23)         # half shoulder width
	var hw := sw * (1.35 if long_coat else 1.15)
	var rings := 14
	var segs := 28
	var v := PackedVector3Array()
	var n := PackedVector3Array()
	var idx := PackedInt32Array()
	var top := -0.2                               # shoulder line, below the hook
	for i in rings + 1:
		var t := float(i) / float(rings)
		var y := top - t * length
		var a: float = lerpf(sw * 0.45, hw, smoothstep(0.0, 0.6, t))    # half width across the shoulders
		var shoulder := smoothstep(0.0, 0.18, t)
		var hw_t: float = lerpf(0.05, a, shoulder)
		var hd_t: float = lerpf(0.045, 0.085 + 0.012 * t, shoulder)
		for j in segs + 1:
			var th := TAU * float(j) / float(segs)
			var fold := 1.0 + 0.05 * sin(th * 6.0 + t * 2.0 + float(i)) * smoothstep(0.1, 0.6, t)
			v.append(Vector3(hd_t * cos(th) * fold, y, hw_t * sin(th) * fold))
			n.append(Vector3(cos(th) / hd_t, 0.15, sin(th) / hw_t).normalized())
	for i in rings:
		for j in segs:
			var a2 := i * (segs + 1) + j
			var b2 := a2 + segs + 1
			idx.append_array([a2, b2, a2 + 1, a2 + 1, b2, b2 + 1])
	root.add_child(mi(finish(v, n, idx), mat))
	for s in [-1.0, 1.0]:
		var sh := Vector3(0.0, top - 0.03, s * (sw - 0.01))
		var hand := sh + Vector3(rng.randf_range(-0.02, 0.05), -0.62 - (0.05 if long_coat else 0.0), s * rng.randf_range(0.05, 0.1))
		var sl := rod(sh, hand, 0.04, mat)
		(sl.mesh as CylinderMesh).top_radius = 0.052
		(sl.mesh as CylinderMesh).bottom_radius = 0.036
		root.add_child(sl)
	# the hanger: a wooden bar across the shoulders under the cloth and a hook
	var wood := surf(3, {"col_a": Color("#7a5632"), "col_b": Color("#9a7448"), "col_c": Color("#a89a86"), "scale": 1.0})
	root.add_child(mi(box(Vector3(0.012, 0.012, sw * 2.0 + 0.06)), wood, at(Vector3(0.0, top + 0.04, 0.0)), false))
	root.add_child(rod(Vector3(0.0, top + 0.04, 0.0), Vector3(0.0, 0.0, 0.0), 0.0025, metal(Color("#9a9aa0"), 0.35), false))
	return root


# A rolling garment rack: two end frames with casters, a rail along x at 1.62 m, garments on hangers. Origin on the floor
# under the middle.
static func rack(rng: RandomNumberGenerator, length := 1.7) -> Node3D:
	var root := Node3D.new()
	var steel := metal(Color("#bdbec2"), 0.28)
	var dark := metal(Color("#202124"), 0.5, 0.6)
	var h := 1.62
	for s in [-1.0, 1.0]:
		var x: float = s * length * 0.5
		root.add_child(rod(Vector3(x, 0.07, -0.3), Vector3(x, h, 0.0), 0.014, steel))
		root.add_child(rod(Vector3(x, 0.07, 0.3), Vector3(x, h, 0.0), 0.014, steel))
		root.add_child(rod(Vector3(x, 0.07, -0.3), Vector3(x, 0.07, 0.3), 0.014, steel))
		for z in [-0.3, 0.3]:
			root.add_child(mi(cyl(0.035, 0.035, 0.05, 16), dark, Transform3D(Basis(Vector3.RIGHT, PI * 0.5), Vector3(x, 0.035, z))))
	root.add_child(rod(Vector3(-length * 0.5, h, 0.0), Vector3(length * 0.5, h, 0.0), 0.0125, steel))
	root.add_child(rod(Vector3(-length * 0.5, 0.45, 0.0), Vector3(length * 0.5, 0.45, 0.0), 0.009, steel))
	var count := int(length / 0.115)
	for i in count:
		var g := garment(rng, rng.randf() < 0.35)
		g.position = Vector3(-length * 0.5 + 0.1 + float(i) * (length - 0.2) / float(maxi(count - 1, 1)), h - 0.02, rng.randf_range(-0.01, 0.01))
		g.rotation.y = deg_to_rad(rng.randf_range(-8.0, 8.0))
		root.add_child(g)
	return root


# ---------------------------------------------------------------- cutting table
static func cutting_table(rng: RandomNumberGenerator) -> Node3D:
	var root := Node3D.new()
	var L := 2.4
	var W := 1.1
	var H := 0.92
	var wood := surf(3, {"col_a": Color("#9a7448"), "col_b": Color("#b8905c"), "col_c": Color("#a89a86"), "wear": 0.8, "dust": 0.5, "grain_axis": Vector3(1, 0, 0), "seed": 3.0})
	var dark := surf(3, {"col_a": Color("#4a3322"), "col_b": Color("#6a4a30"), "col_c": Color("#8a7a66"), "wear": 0.6, "dust": 0.6, "grain_axis": Vector3(0, 1, 0)})
	root.add_child(mi(box(Vector3(L, 0.055, W)), wood, at(Vector3(0.0, H - 0.0275, 0.0))))
	root.add_child(mi(box(Vector3(L - 0.14, 0.07, W - 0.14)), dark, at(Vector3(0.0, H - 0.09, 0.0))))
	for sx in [-1.0, 1.0]:
		for sz in [-1.0, 1.0]:
			root.add_child(mi(box(Vector3(0.08, H - 0.06, 0.08)), dark, at(Vector3(sx * (L * 0.5 - 0.1), (H - 0.06) * 0.5, sz * (W * 0.5 - 0.1)))))
	root.add_child(mi(box(Vector3(L - 0.2, 0.03, W - 0.2)), wood, at(Vector3(0.0, 0.22, 0.0))))
	# a roll of pattern paper and some bolts on the lower shelf
	root.add_child(mi(cyl(0.07, 0.07, 0.9, 20), surf(7, {"col_a": Color("#d9d2c0"), "col_b": Color("#b8ae98"), "dust": 0.5}), Transform3D(Basis(Vector3.FORWARD, PI * 0.5), Vector3(0.3, 0.3, 0.0))))
	var b := bolt(0.12, 0.8, "#2a3550", 0)
	b.transform = Transform3D(Basis(Vector3.UP, PI * 0.5), Vector3(-0.5, 0.35, 0.05))
	root.add_child(b)
	# on the top: the cutting mat, tape, shears, spools, chalk, pins
	var mat := surf(5, {"col_a": Color("#27553f"), "col_b": Color("#dfe6d8"), "wear": 0.9, "dust": 0.3, "scale": 1.0})
	root.add_child(mi(box(Vector3(1.5, 0.004, 0.9)), mat, at(Vector3(-0.2, H + 0.002, 0.0)), false))
	var paper := surf(7, {"col_a": Color("#d8d0bc"), "col_b": Color("#b8ae98"), "dust": 0.3})
	root.add_child(mi(box(Vector3(0.7, 0.002, 0.5)), paper, at(Vector3(-0.35, H + 0.006, 0.05), 12.0), false))
	root.add_child(mi(box(Vector3(0.5, 0.002, 0.7)), paper, at(Vector3(-0.1, H + 0.008, -0.1), -20.0), false))
	var tape_mat := surf(4, {"col_a": Color("#e8dcb0"), "col_b": Color("#e8dcb0"), "col_c": Color("#2a2a2a"), "scale": 700.0, "pattern": 1, "pat_size": 0.01})
	var tape_coil := TorusMesh.new()
	tape_coil.inner_radius = 0.032
	tape_coil.outer_radius = 0.05
	root.add_child(mi(tape_coil, tape_mat, at(Vector3(0.52, H + 0.012, 0.28)), false))
	root.add_child(mi(box(Vector3(0.5, 0.0015, 0.016)), tape_mat, at(Vector3(0.25, H + 0.0095, 0.28), 8.0), false))
	var steel := metal(Color("#a8a9ad"), 0.25)
	var shear_black := metal(Color("#1b1c1e"), 0.5, 0.2)
	for k in 2:
		var blade := mi(box(Vector3(0.2, 0.0035, 0.018)), steel, at(Vector3(0.62 + 0.0, H + 0.012 + 0.003 * float(k), -0.24 + 0.002 * float(k)), 18.0 - 6.0 * float(k)), false)
		root.add_child(blade)
		var ring := TorusMesh.new()
		ring.inner_radius = 0.016
		ring.outer_radius = 0.026
		root.add_child(mi(ring, shear_black, Transform3D(Basis(Vector3.UP, deg_to_rad(18.0 - 6.0 * float(k))), Vector3(0.505 + 0.01 * float(k), H + 0.012, -0.21 - 0.02 * float(k))), false))
	for k in 5:
		var sp := Node3D.new()
		var hexc := Color.from_hsv(rng.randf(), rng.randf_range(0.3, 0.8), rng.randf_range(0.4, 0.8))
		sp.add_child(mi(cyl(0.011, 0.011, 0.04, 12), surf(4, {"col_a": hexc, "col_b": hexc, "col_c": hexc, "scale": 900.0}), at(Vector3.ZERO), false))
		sp.add_child(mi(cyl(0.015, 0.015, 0.004, 12), surf(3, {"col_a": Color("#b8905c"), "col_b": Color("#9a7448"), "col_c": Color("#a89a86")}), at(Vector3(0.0, 0.022, 0.0)), false))
		sp.add_child(mi(cyl(0.015, 0.015, 0.004, 12), surf(3, {"col_a": Color("#b8905c"), "col_b": Color("#9a7448"), "col_c": Color("#a89a86")}), at(Vector3(0.0, -0.022, 0.0)), false))
		sp.position = Vector3(0.55 + 0.04 * float(k), H + 0.024, 0.02 + 0.02 * float(k % 2))
		root.add_child(sp)
	root.add_child(mi(box(Vector3(0.07, 0.012, 0.012)), surf(7, {"col_a": Color("#e8e4da"), "col_b": Color("#c8c2b2")}), at(Vector3(0.05, H + 0.008, -0.38), 25.0), false))
	var ruler := surf(3, {"col_a": Color("#c9a56a"), "col_b": Color("#b58f52"), "col_c": Color("#a89a86"), "grain_axis": Vector3(1, 0, 0)})
	root.add_child(mi(box(Vector3(0.9, 0.004, 0.045)), ruler, at(Vector3(-0.1, H + 0.007, 0.36), -3.0), false))
	return root


# ---------------------------------------------------------------- dress form
# A tailor's dress form on a pole and a tripod base, covered in linen. Origin on the floor; about 1.6 m tall.
static func dress_form(linen_hex := "#cdbf9f") -> Node3D:
	var root := Node3D.new()
	var linen := surf(4, {"col_a": Color(linen_hex), "col_b": Color(linen_hex).lightened(0.1), "col_c": Color(linen_hex).darkened(0.3), "scale": 380.0, "dust": 0.5, "sheen": 0.2})
	var y0 := 0.86
	# half-width profile (radius, y above the form's bottom); sections are wider than deep
	var prof := [Vector2(0.001, 0.0), Vector2(0.11, 0.0), Vector2(0.15, 0.03), Vector2(0.168, 0.11), Vector2(0.158, 0.2), Vector2(0.128, 0.3),
		Vector2(0.122, 0.34), Vector2(0.135, 0.42), Vector2(0.152, 0.5), Vector2(0.148, 0.56), Vector2(0.15, 0.62), Vector2(0.135, 0.66),
		Vector2(0.095, 0.69), Vector2(0.052, 0.71), Vector2(0.043, 0.74), Vector2(0.001, 0.745)]
	root.add_child(mi(lathe(prof, 40, 1.0, 0.72), linen, at(Vector3(0.0, y0, 0.0))))
	var wood := surf(3, {"col_a": Color("#7a5632"), "col_b": Color("#9a7448"), "col_c": Color("#a89a86"), "grain_axis": Vector3(0, 1, 0)})
	root.add_child(mi(cyl(0.03, 0.036, 0.07, 18), wood, at(Vector3(0.0, y0 + 0.78, 0.0))))
	var cap := SphereMesh.new()
	cap.radius = 0.034
	cap.height = 0.05
	root.add_child(mi(cap, wood, at(Vector3(0.0, y0 + 0.83, 0.0))))
	var brass := metal(Color("#b08d4a"), 0.3)
	root.add_child(mi(cyl(0.013, 0.013, 0.95, 14), brass, at(Vector3(0.0, 0.475, 0.0))))
	root.add_child(mi(cyl(0.03, 0.04, 0.05, 14), brass, at(Vector3(0.0, 0.1, 0.0))))
	for k in 3:
		var a := TAU * float(k) / 3.0 + 0.5
		var foot := Vector3(cos(a) * 0.3, 0.025, sin(a) * 0.3)
		root.add_child(rod(Vector3(0.0, 0.09, 0.0), foot, 0.011, brass))
		root.add_child(mi(cyl(0.026, 0.026, 0.03, 14), metal(Color("#202124"), 0.5, 0.4), at(foot - Vector3(0.0, 0.01, 0.0)), false))
	return root


# ---------------------------------------------------------------- mirror
# A three-panel fitting mirror: a wide centre panel and two side wings folded in, facing +z at the origin.
static func mirror3() -> Node3D:
	var root := Node3D.new()
	var frame := surf(6, {"col_a": Color("#2a2420"), "col_b": Color("#6a4a2e"), "col_c": Color("#4a443c"), "wear": 0.5, "dust": 0.5})
	var glass := StandardMaterial3D.new()
	glass.albedo_color = Color(0.78, 0.8, 0.8)
	glass.metallic = 1.0
	glass.metallic_specular = 1.0
	glass.roughness = 0.03
	var panels := [[0.0, 0.0, 0.62], [-1.0, 24.0, 0.5], [1.0, -24.0, 0.5]]
	var h := 1.75
	var y0 := 0.08
	var hinge_x := 0.0
	for pnl in panels:
		var side: float = pnl[0]
		var rot: float = pnl[1]
		var w: float = pnl[2]
		var p := Node3D.new()
		var ctr_x := side * (0.31 + 0.25 * cos(deg_to_rad(absf(rot))))
		var ctr_z := 0.0 if side == 0.0 else 0.25 * sin(deg_to_rad(absf(rot)))
		p.position = Vector3(ctr_x, 0.0, ctr_z)
		p.rotation.y = deg_to_rad(rot)
		var f := 0.045
		p.add_child(mi(box(Vector3(w, f, 0.04)), frame, at(Vector3(0.0, y0 + f * 0.5, 0.0))))
		p.add_child(mi(box(Vector3(w, f, 0.04)), frame, at(Vector3(0.0, y0 + h - f * 0.5, 0.0))))
		p.add_child(mi(box(Vector3(f, h, 0.04)), frame, at(Vector3(-w * 0.5 + f * 0.5, y0 + h * 0.5, 0.0))))
		p.add_child(mi(box(Vector3(f, h, 0.04)), frame, at(Vector3(w * 0.5 - f * 0.5, y0 + h * 0.5, 0.0))))
		var q := QuadMesh.new()
		q.size = Vector2(w - 2.0 * f, h - 2.0 * f)
		var gm := mi(q, glass, at(Vector3(0.0, y0 + h * 0.5, 0.012)), false)
		gm.name = "Glass%d" % int(pnl[0] + 1.0)
		gm.layers = 4                  # layer 3: the reflection camera leaves the glass (and what is smeared on it) out (mirror.gd)
		p.add_child(gm)
		# a foot under each panel
		p.add_child(mi(box(Vector3(0.06, 0.08, 0.34)), frame, at(Vector3(-w * 0.3, 0.04, 0.0))))
		p.add_child(mi(box(Vector3(0.06, 0.08, 0.34)), frame, at(Vector3(w * 0.3, 0.04, 0.0))))
		root.add_child(p)
	return root


# ---------------------------------------------------------------- lamps, windows, backdrop
# A pendant lamp: cord from the ceiling, an enamel shade, a warm bulb and its OmniLight3D. Returns {node, light}.
static func pendant(drop: float, warm: Color, energy: float, rng_seed := 0.0) -> Dictionary:
	var root := Node3D.new()
	var enamel := surf(8, {"col_a": Color("#1d3a34"), "col_b": Color("#3a5a50"), "wear": 0.4, "seed": rng_seed, "grain_axis": Vector3(0, 1, 0)})
	root.add_child(mi(cyl(0.004, 0.004, drop, 6), metal(Color("#111112"), 0.6, 0.0), at(Vector3(0.0, drop * 0.5, 0.0)), false))
	var shade := [Vector2(0.225, -0.21), Vector2(0.21, -0.2), Vector2(0.11, -0.09), Vector2(0.05, 0.02), Vector2(0.03, 0.0)]
	root.add_child(mi(lathe(shade, 32), enamel, at(Vector3.ZERO), false))
	var inner := [Vector2(0.03, 0.0), Vector2(0.05, 0.02), Vector2(0.11, -0.09), Vector2(0.21, -0.2), Vector2(0.222, -0.208)]
	root.add_child(mi(lathe(inner, 32), glow(warm.lightened(0.5), 0.9), at(Vector3.ZERO), false))
	var bulb := SphereMesh.new()
	bulb.radius = 0.05
	bulb.height = 0.1
	root.add_child(mi(bulb, glow(warm, 14.0), at(Vector3(0.0, -0.11, 0.0)), false))
	var l := OmniLight3D.new()
	l.name = "Practical"
	l.position = Vector3(0.0, -0.13, 0.0)
	l.light_color = warm
	l.light_energy = energy
	l.omni_range = 5.5
	l.omni_attenuation = 2.0
	l.shadow_enabled = false
	l.light_specular = 0.5
	root.add_child(l)
	return {"node": root, "light": l}


# A tall window with a painted frame, glazing bars and bright cool glass (daylight). Faces +z (turn it for the other walls).
static func window(w: float, h: float, daylight: Color, energy: float) -> Node3D:
	var root := Node3D.new()
	var paint := surf(6, {"col_a": Color("#d9d5cc"), "col_b": Color("#8c6a46"), "col_c": Color("#8a8378"), "wear": 0.6, "dust": 0.5})
	var f := 0.06
	root.add_child(mi(box(Vector3(w, f, 0.12)), paint, at(Vector3(0.0, f * 0.5, 0.0)), false))
	root.add_child(mi(box(Vector3(w, f, 0.12)), paint, at(Vector3(0.0, h - f * 0.5, 0.0)), false))
	root.add_child(mi(box(Vector3(f, h, 0.12)), paint, at(Vector3(-w * 0.5 + f * 0.5, h * 0.5, 0.0)), false))
	root.add_child(mi(box(Vector3(f, h, 0.12)), paint, at(Vector3(w * 0.5 - f * 0.5, h * 0.5, 0.0)), false))
	root.add_child(mi(box(Vector3(0.025, h - 2.0 * f, 0.05)), paint, at(Vector3(0.0, h * 0.5, 0.0)), false))
	for k in 3:
		root.add_child(mi(box(Vector3(w - 2.0 * f, 0.025, 0.05)), paint, at(Vector3(0.0, f + (h - 2.0 * f) * float(k + 1) / 4.0, 0.0)), false))
	var q := QuadMesh.new()
	q.size = Vector2(w - 2.0 * f, h - 2.0 * f)
	root.add_child(mi(q, glow(daylight, energy), at(Vector3(0.0, h * 0.5, -0.02)), false))
	var sill := surf(3, {"col_a": Color("#d9d5cc"), "col_b": Color("#c8c2b4"), "col_c": Color("#a89a86"), "wear": 0.5})
	root.add_child(mi(box(Vector3(w + 0.16, 0.04, 0.2)), sill, at(Vector3(0.0, -0.02, 0.06)), false))
	return root


# The seamless paper backdrop: hung from a roll at the top, falling to the floor, curving onto it and running forward
# along it. Faces +z; the origin is on the floor at the point where the curve meets it.
static func backdrop(width: float, top: float, curve_r: float, run: float, hex: String) -> Node3D:
	var root := Node3D.new()
	var prof: Array = []
	prof.append(Vector2(-curve_r, top))
	var n := 16
	for k in range(0, n + 1):
		var a := 0.5 * PI * float(k) / float(n)
		prof.append(Vector2(-curve_r + curve_r * sin(a), curve_r * cos(a)))     # from the wall's foot down to the floor
	prof.append(Vector2(run, 0.0))
	var paper := world_surf(7, {"col_a": Color(hex), "col_b": Color(hex).darkened(0.18), "dust": 0.5, "seed": 2.0})
	var pm := mi(sweep(prof, width), paper, Transform3D.IDENTITY, false)
	root.add_child(pm)
	# the roll and its brackets
	root.add_child(mi(cyl(0.06, 0.06, width + 0.3, 20), surf(7, {"col_a": Color(hex).lightened(0.05), "col_b": Color(hex), "dust": 0.4}), Transform3D(Basis(Vector3.FORWARD, PI * 0.5), Vector3(0.0, top + 0.02, -curve_r - 0.06)), false))
	var steel := metal(Color("#1f2022"), 0.45, 0.6)
	root.add_child(mi(cyl(0.012, 0.012, width + 0.5, 10), steel, Transform3D(Basis(Vector3.FORWARD, PI * 0.5), Vector3(0.0, top + 0.02, -curve_r - 0.06)), false))
	return root
