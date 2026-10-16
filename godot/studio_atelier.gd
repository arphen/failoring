# The lived-in part of the atelier (studio.gd places these): an oriental rug, wooden chairs, a desk with a lamp, make-up and a
# lipstick-smeared hand mirror, a tea corner with a steaming kettle, and "dead" mannequins in half-finished garments held
# with safety pins, tape measures round their necks and customers' names scribbled on tags, plus a half-cut cloth on a
# table. Look only: nothing here reaches the solver. Same conventions as studio_props.gd (metres, y up, origin on the floor).
extends RefCounted

const Props = preload("res://studio_props.gd")
const SMEAR := preload("res://shaders/smear.gdshader")

# the dress form's torso: (half width, height above the form's bottom); the elliptical section is 0.72 as deep as wide
const FORM_PROFILE := [Vector2(0.001, 0.0), Vector2(0.11, 0.0), Vector2(0.15, 0.03), Vector2(0.168, 0.11), Vector2(0.158, 0.2),
	Vector2(0.128, 0.3), Vector2(0.122, 0.34), Vector2(0.135, 0.42), Vector2(0.152, 0.5), Vector2(0.148, 0.56), Vector2(0.15, 0.62),
	Vector2(0.135, 0.66), Vector2(0.095, 0.69), Vector2(0.052, 0.71), Vector2(0.043, 0.74)]
const FORM_Y0 := 0.86
const FORM_DEPTH := 0.72


static func oak(dark := false, axis := Vector3(1, 0, 0)) -> ShaderMaterial:
	var a := Color("#5a3d25") if dark else Color("#9a7448")
	var b := Color("#7a5632") if dark else Color("#b8905c")
	return Props.surf(3, {"col_a": a, "col_b": b, "col_c": Color("#a89a86"), "wear": 0.8, "dust": 0.6, "grain_axis": axis, "seed": 5.0 if dark else 2.0})


# ---------------------------------------------------------------- floor and furniture
static func rug(w: float, d: float, y: float) -> MeshInstance3D:
	var q := QuadMesh.new()
	q.size = Vector2(w, d)
	var m := Props.surf(9, {"col_a": Color("#6b1410"), "col_b": Color("#141a33"), "col_c": Color("#c9b98a"), "dims": Vector2(w, d), "wear": 0.8, "dust": 0.6, "seed": 2.0})
	return Props.mi(q, m, Transform3D(Basis(Vector3.RIGHT, -PI * 0.5), Vector3(0.0, y, 0.0)), false)


# A wooden chair facing +z (its back at -z).
static func chair() -> Node3D:
	var root := Node3D.new()
	var w := oak(true, Vector3(0, 1, 0))
	root.add_child(Props.mi(Props.box(Vector3(0.44, 0.04, 0.42)), w, Props.at(Vector3(0.0, 0.45, 0.0))))
	for sx in [-1.0, 1.0]:
		for sz in [-1.0, 1.0]:
			root.add_child(Props.mi(Props.box(Vector3(0.04, 0.45, 0.04)), w, Props.at(Vector3(sx * 0.19, 0.225, sz * 0.18))))
		root.add_child(Props.mi(Props.box(Vector3(0.04, 0.48, 0.04)), w, Props.at(Vector3(sx * 0.19, 0.69, -0.18))))
	root.add_child(Props.mi(Props.box(Vector3(0.42, 0.08, 0.03)), w, Props.at(Vector3(0.0, 0.88, -0.18))))
	for k in 2:
		root.add_child(Props.mi(Props.box(Vector3(0.34, 0.05, 0.02)), w, Props.at(Vector3(0.0, 0.68 + 0.09 * float(k), -0.18))))
	root.add_child(Props.mi(Props.box(Vector3(0.4, 0.02, 0.02)), w, Props.at(Vector3(0.0, 0.2, 0.18))))
	return root


# A desk facing +z with a lamp (and its warm light), make-up and a lipstick-smeared hand mirror on it.
static func desk(rng: RandomNumberGenerator) -> Node3D:
	var root := Node3D.new()
	var w := oak(false, Vector3(1, 0, 0))
	var dk := oak(true, Vector3(0, 1, 0))
	var L := 1.3
	var D := 0.65
	var H := 0.76
	root.add_child(Props.mi(Props.box(Vector3(L, 0.04, D)), w, Props.at(Vector3(0.0, H - 0.02, 0.0))))
	for sx in [-1.0, 1.0]:
		root.add_child(Props.mi(Props.box(Vector3(0.05, H - 0.04, D - 0.06)), dk, Props.at(Vector3(sx * (L * 0.5 - 0.04), (H - 0.04) * 0.5, 0.0))))
	root.add_child(Props.mi(Props.box(Vector3(L - 0.1, 0.5, 0.02)), dk, Props.at(Vector3(0.0, H - 0.3, -D * 0.5 + 0.05))))
	for k in 2:     # drawers
		root.add_child(Props.mi(Props.box(Vector3(0.45, 0.14, 0.02)), w, Props.at(Vector3(0.3, H - 0.14 - 0.17 * float(k), D * 0.5 - 0.05))))
		root.add_child(Props.mi(Props.cyl(0.012, 0.012, 0.03, 10), Props.metal(Color("#b08d4a"), 0.3), Transform3D(Basis(Vector3.RIGHT, PI * 0.5), Vector3(0.3, H - 0.14 - 0.17 * float(k), D * 0.5 - 0.02))))
	# the lamp: weighted base, a jointed arm, a green enamel shade, a warm bulb, a spot of light on the desk
	var brass := Props.metal(Color("#b08d4a"), 0.3)
	var enamel := Props.surf(8, {"col_a": Color("#1d3a34"), "col_b": Color("#3a5a50"), "wear": 0.4, "grain_axis": Vector3(0, 1, 0)})
	var lp := Vector3(-0.46, H, -0.12)
	root.add_child(Props.mi(Props.cyl(0.08, 0.09, 0.03, 20), brass, Props.at(lp + Vector3(0, 0.015, 0))))
	var elbow := lp + Vector3(0.08, 0.34, 0.0)
	var head := lp + Vector3(0.22, 0.4, 0.06)
	root.add_child(Props.rod(lp + Vector3(0, 0.03, 0), elbow, 0.006, brass))
	root.add_child(Props.rod(elbow, head, 0.006, brass))
	var shade := [Vector2(0.12, -0.12), Vector2(0.11, -0.1), Vector2(0.06, -0.02), Vector2(0.025, 0.04), Vector2(0.02, 0.05)]
	var shade_node := Node3D.new()
	shade_node.position = head
	shade_node.rotation = Vector3(deg_to_rad(18.0), 0.0, deg_to_rad(-12.0))
	shade_node.add_child(Props.mi(Props.lathe(shade, 24), enamel, Transform3D.IDENTITY, false))
	var inner := [Vector2(0.025, 0.04), Vector2(0.06, -0.02), Vector2(0.11, -0.1), Vector2(0.118, -0.118)]
	shade_node.add_child(Props.mi(Props.lathe(inner, 24), Props.glow(Color(1.0, 0.8, 0.5), 0.8), Transform3D.IDENTITY, false))
	var bulb := SphereMesh.new()
	bulb.radius = 0.03
	bulb.height = 0.06
	shade_node.add_child(Props.mi(bulb, Props.glow(Color(1.0, 0.72, 0.4), 12.0), Props.at(Vector3(0, -0.04, 0)), false))
	var light := SpotLight3D.new()
	light.name = "DeskLamp"
	light.position = Vector3(0, -0.06, 0)
	light.rotation = Vector3(-PI * 0.5, 0, 0)
	light.light_color = Color(1.0, 0.72, 0.42)
	light.light_energy = 3.0
	light.spot_range = 3.0
	light.spot_angle = 52.0
	light.spot_attenuation = 1.6
	light.spot_angle_attenuation = 1.4
	light.shadow_enabled = false
	light.light_specular = 0.6
	shade_node.add_child(light)
	root.add_child(shade_node)
	# make-up and brushes
	var gold := Props.metal(Color("#c9a24a"), 0.2)
	var black := Props.metal(Color("#161617"), 0.35, 0.2)
	var reds := [Color("#a3121f"), Color("#c24a5a"), Color("#6d0d1f")]
	for k in 4:
		var tx := -0.05 + 0.045 * float(k)
		root.add_child(Props.mi(Props.cyl(0.0095, 0.0095, 0.04, 12), gold if k % 2 == 0 else black, Props.at(Vector3(tx, H + 0.02, 0.12 + 0.01 * float(k % 2)))))
		root.add_child(Props.mi(Props.cyl(0.0085, 0.0085, 0.028, 12), Props.surf(6, {"col_a": reds[k % 3], "col_b": reds[k % 3], "col_c": reds[k % 3], "wear": 0.0}), Props.at(Vector3(tx, H + 0.054, 0.12 + 0.01 * float(k % 2)))))
	root.add_child(Props.mi(Props.cyl(0.05, 0.05, 0.016, 24), black, Props.at(Vector3(0.22, H + 0.008, 0.08))))          # a compact
	root.add_child(Props.mi(Props.cyl(0.042, 0.042, 0.002, 24), Props.metal(Color("#d8d8dc"), 0.04), Props.at(Vector3(0.22, H + 0.017, 0.08))))
	root.add_child(Props.mi(Props.cyl(0.035, 0.03, 0.09, 16), Props.surf(8, {"col_a": Color("#e8e4da"), "col_b": Color("#b8b2a2"), "wear": 0.4}), Props.at(Vector3(0.4, H + 0.045, -0.1))))   # a brush cup
	for k in 5:
		var a := TAU * float(k) / 5.0
		root.add_child(Props.rod(Vector3(0.4 + 0.01 * cos(a), H + 0.08, -0.1 + 0.01 * sin(a)), Vector3(0.4 + 0.03 * cos(a), H + 0.17 + 0.01 * float(k), -0.1 + 0.03 * sin(a)), 0.004, Props.surf(3, {"col_a": Color("#5a3d25"), "col_b": Color("#7a5632"), "col_c": Color("#a89a86")}), false))
	# a stand mirror, smeared with lipstick
	root.add_child(_hand_mirror(Vector3(0.05, H, -0.2)))
	# a few scraps and a dish of pins
	var paper := Props.surf(7, {"col_a": Color("#d8d0bc"), "col_b": Color("#b8ae98"), "dust": 0.3})
	root.add_child(Props.mi(Props.box(Vector3(0.18, 0.002, 0.12)), paper, Props.at(Vector3(-0.2, H + 0.002, 0.15), 18.0), false))
	root.add_child(Props.mi(Props.cyl(0.04, 0.03, 0.012, 16), Props.metal(Color("#c8c8cc"), 0.2), Props.at(Vector3(-0.1, H + 0.006, 0.22)), false))
	return root


static func _hand_mirror(at: Vector3) -> Node3D:
	var root := Node3D.new()
	root.position = at
	var frame := Props.surf(6, {"col_a": Color("#c9a24a"), "col_b": Color("#8c6a46"), "col_c": Color("#8a8378"), "wear": 0.6, "dust": 0.4})
	root.add_child(Props.mi(Props.box(Vector3(0.3, 0.04, 0.12)), frame, Props.at(Vector3(0, 0.02, 0)), false))
	var tilt := Node3D.new()
	tilt.position = Vector3(0, 0.12, 0)
	tilt.rotation = Vector3(deg_to_rad(-12.0), 0, 0)
	tilt.add_child(Props.mi(Props.box(Vector3(0.28, 0.36, 0.02)), frame, Props.at(Vector3(0, 0.12, -0.012)), false))
	var glass := StandardMaterial3D.new()
	glass.albedo_color = Color(0.8, 0.82, 0.82)
	glass.metallic = 1.0
	glass.roughness = 0.03
	var q := QuadMesh.new()
	q.size = Vector2(0.25, 0.33)
	var gm := Props.mi(q, glass, Props.at(Vector3(0, 0.12, 0.0)), false)
	gm.layers = 4
	tilt.add_child(gm)
	tilt.add_child(smear(0.25, 0.33, Vector3(0, 0.12, 0.002), 1.0))
	root.add_child(tilt)
	return root


# Lipstick smeared on a mirror: a clear quad whose shader draws the swipes (layer 4 like the glass).
static func smear(w: float, h: float, pos: Vector3, seed_v: float) -> MeshInstance3D:
	var q := QuadMesh.new()
	q.size = Vector2(w, h)
	var m := ShaderMaterial.new()
	m.shader = SMEAR
	m.set_shader_parameter("seed", seed_v)
	var n := Props.mi(q, m, Props.at(pos), false)
	n.layers = 4
	return n


# A small round table with a kettle, steaming, and a cup on a saucer.
static func tea_corner() -> Node3D:
	var root := Node3D.new()
	var w := oak(true, Vector3(0, 1, 0))
	root.add_child(Props.mi(Props.cyl(0.32, 0.32, 0.035, 28), w, Props.at(Vector3(0, 0.72, 0))))
	root.add_child(Props.mi(Props.cyl(0.04, 0.05, 0.7, 12), w, Props.at(Vector3(0, 0.36, 0))))
	root.add_child(Props.mi(Props.cyl(0.22, 0.25, 0.04, 20), w, Props.at(Vector3(0, 0.02, 0))))
	var copper := Props.metal(Color("#a8602f"), 0.28)
	var kettle := [Vector2(0.001, 0.0), Vector2(0.1, 0.0), Vector2(0.125, 0.04), Vector2(0.13, 0.1), Vector2(0.11, 0.17), Vector2(0.06, 0.21), Vector2(0.03, 0.22), Vector2(0.001, 0.225)]
	var kn := Node3D.new()
	kn.position = Vector3(-0.08, 0.7375, 0.02)
	kn.add_child(Props.mi(Props.lathe(kettle, 28), copper, Transform3D.IDENTITY))
	kn.add_child(Props.mi(Props.cyl(0.018, 0.018, 0.03, 10), Props.metal(Color("#161617"), 0.4, 0.1), Props.at(Vector3(0, 0.235, 0))))
	var spout := Props.rod(Vector3(0.1, 0.1, 0), Vector3(0.2, 0.2, 0), 0.016, copper)
	kn.add_child(spout)
	var handle := TorusMesh.new()
	handle.inner_radius = 0.085
	handle.outer_radius = 0.095
	kn.add_child(Props.mi(handle, Props.metal(Color("#161617"), 0.4, 0.1), Transform3D(Basis(Vector3.RIGHT, PI * 0.5), Vector3(-0.08, 0.13, 0.0)), false))
	root.add_child(kn)
	# the cup and its saucer
	var china := Props.surf(6, {"col_a": Color("#efe9dd"), "col_b": Color("#efe9dd"), "col_c": Color("#d8d0c0"), "wear": 0.05, "dust": 0.1})
	root.add_child(Props.mi(Props.cyl(0.07, 0.05, 0.012, 24), china, Props.at(Vector3(0.14, 0.7425, -0.06)), false))
	root.add_child(Props.mi(Props.lathe([Vector2(0.03, 0.0), Vector2(0.042, 0.01), Vector2(0.05, 0.06), Vector2(0.055, 0.065)], 20), china, Props.at(Vector3(0.14, 0.75, -0.06)), false))
	root.add_child(Props.mi(Props.cyl(0.046, 0.046, 0.003, 20), Props.surf(6, {"col_a": Color("#5a2a14"), "col_b": Color("#5a2a14"), "col_c": Color("#5a2a14"), "wear": 0.0}), Props.at(Vector3(0.14, 0.808, -0.06)), false))
	# steam from the spout
	var ps := GPUParticles3D.new()
	ps.name = "Steam"
	ps.position = Vector3(-0.08 + 0.2, 0.7375 + 0.21, 0.02)
	ps.amount = 28
	ps.lifetime = 3.2
	ps.local_coords = false
	ps.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var pm := ParticleProcessMaterial.new()
	pm.direction = Vector3(0.25, 1.0, 0.0)
	pm.spread = 14.0
	pm.initial_velocity_min = 0.16
	pm.initial_velocity_max = 0.26
	pm.gravity = Vector3(0.0, 0.05, 0.0)
	pm.turbulence_enabled = true
	pm.turbulence_noise_strength = 0.25
	pm.turbulence_noise_scale = 1.5
	pm.scale_min = 0.6
	pm.scale_max = 1.4
	var sc := Curve.new()
	sc.add_point(Vector2(0.0, 0.3))
	sc.add_point(Vector2(1.0, 2.2))
	var sct := CurveTexture.new()
	sct.curve = sc
	pm.scale_curve = sct
	var gr := Gradient.new()
	gr.offsets = PackedFloat32Array([0.0, 0.2, 1.0])
	gr.colors = PackedColorArray([Color(1, 1, 1, 0.0), Color(1, 1, 1, 0.22), Color(1, 1, 1, 0.0)])
	var grt := GradientTexture1D.new()
	grt.gradient = gr
	pm.color_ramp = grt
	ps.process_material = pm
	var quad := QuadMesh.new()
	quad.size = Vector2(0.14, 0.14)
	var qm := StandardMaterial3D.new()
	qm.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	qm.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	qm.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	qm.vertex_color_use_as_albedo = true
	var rg := GradientTexture2D.new()
	var rgg := Gradient.new()
	rgg.offsets = PackedFloat32Array([0.0, 1.0])
	rgg.colors = PackedColorArray([Color(1, 1, 1, 1), Color(1, 1, 1, 0)])
	rg.gradient = rgg
	rg.fill = GradientTexture2D.FILL_RADIAL
	rg.fill_from = Vector2(0.5, 0.5)
	rg.fill_to = Vector2(1.0, 0.5)
	qm.albedo_texture = rg
	quad.material = qm
	ps.draw_pass_1 = quad
	root.add_child(ps)
	return root


# ---------------------------------------------------------------- the dead mannequins
# The radius of the form (half width) at a height above its bottom.
static func form_r(y: float) -> float:
	for i in FORM_PROFILE.size() - 1:
		var a: Vector2 = FORM_PROFILE[i]
		var b: Vector2 = FORM_PROFILE[i + 1]
		if y <= b.y:
			return lerpf(a.x, b.x, clampf((y - a.y) / maxf(b.y - a.y, 1.0e-6), 0.0, 1.0))
	return (FORM_PROFILE[FORM_PROFILE.size() - 1] as Vector2).x


# A cloth shell: a surface of revolution with its radius rippled into folds, smooth normals computed from the result.
static func cloth_shell(profile: Array, segs: int, amp: float, freq: float, seed_v: float, sx: float, sz: float) -> ArrayMesh:
	var v := PackedVector3Array()
	var idx := PackedInt32Array()
	var np := profile.size()
	for i in np:
		var p: Vector2 = profile[i]
		for j in segs + 1:
			var a := TAU * float(j) / float(segs)
			var r := p.x * (1.0 + amp * sin(a * freq + p.y * 11.0 + seed_v) * (0.55 + 0.45 * sin(p.y * 23.0 + seed_v * 2.0)))
			v.append(Vector3(r * cos(a) * sx, p.y, r * sin(a) * sz))
	for i in np - 1:
		for j in segs:
			var a := i * (segs + 1) + j
			var b := a + segs + 1
			idx.append_array([a, b, a + 1, a + 1, b, b + 1])
	var n := PackedVector3Array()
	n.resize(v.size())
	for t in range(0, idx.size(), 3):
		var fn := (v[idx[t + 1]] - v[idx[t]]).cross(v[idx[t + 2]] - v[idx[t]])
		for k in 3:
			n[idx[t + k]] += fn
	for i in np:     # the seam: both ends of a ring share one normal
		var s := n[i * (segs + 1)] + n[i * (segs + 1) + segs]
		n[i * (segs + 1)] = s
		n[i * (segs + 1) + segs] = s
	var out_sign := 0.0
	for i in v.size():
		n[i] = n[i].normalized()
		out_sign += n[i].dot(Vector3(v[i].x, 0.0, v[i].z))
	if out_sign < 0.0:
		for i in n.size():
			n[i] = -n[i]
	return Props.finish(v, n, idx)


static func _pin(root: Node3D, pos: Vector3, dir: Vector3, head: Color) -> void:
	var tip := pos + dir * 0.028
	root.add_child(Props.rod(pos, tip, 0.0007, Props.metal(Color("#cfcfd4"), 0.2), false))
	var s := SphereMesh.new()
	s.radius = 0.0035
	s.height = 0.007
	root.add_child(Props.mi(s, Props.surf(6, {"col_a": head, "col_b": head, "col_c": head, "wear": 0.0}), Props.at(pos), false))


# A name tag on a thread: a card with scribbled writing, hanging from `from`.
static func tag(from: Vector3, rng: RandomNumberGenerator) -> Node3D:
	var root := Node3D.new()
	var len := rng.randf_range(0.05, 0.09)
	root.add_child(Props.rod(from, from + Vector3(0, -len, 0), 0.0005, Props.surf(4, {"col_a": Color("#d8d0bc"), "col_b": Color("#d8d0bc"), "col_c": Color("#d8d0bc"), "scale": 900.0}), false))
	var q := QuadMesh.new()
	q.size = Vector2(0.07, 0.105)
	var m := Props.surf(10, {"col_a": Color("#e3d9bf"), "col_c": Color("#2a2a2a"), "seed": rng.randf_range(0.0, 20.0)})
	var card := Props.mi(q, m, Transform3D(Basis(Vector3.UP, rng.randf_range(-0.5, 0.5)) * Basis(Vector3.RIGHT, rng.randf_range(-0.1, 0.1)), from + Vector3(0, -len - 0.05, 0.004)), false)
	root.add_child(card)
	# the back of the card
	root.add_child(Props.mi(q, Props.surf(7, {"col_a": Color("#cbbf9f"), "col_b": Color("#b8ae98"), "dust": 0.4}), Transform3D(card.transform.basis * Basis(Vector3.UP, PI), card.position - card.transform.basis.z * 0.001), false))
	return root


# A dress form in a half-finished garment: "blouse" (cloth over the torso, open at the neck and the hem), "pants" (a waistband and
# two legs of trousers, cut short) or "bare" (only the tape round its neck). Pins, a tape measure and a tag on each.
static func dead_form(kind: String, rng: RandomNumberGenerator) -> Node3D:
	var root := Props.dress_form(["#cdbf9f", "#c3b79b", "#b9b5ae"][rng.randi() % 3])
	var y0 := FORM_Y0
	var hexes := ["#e4dfd2", "#aab4c4", "#c9a9a0", "#a6b59f", "#e0d6ac"] if kind != "pants" else ["#3d4a5c", "#4a3a30", "#2e3a34", "#5a5a60"]
	var hex: String = hexes[rng.randi() % hexes.size()]
	var mat := Props.cloth_mat(hex, [0, 0, 1, 3][rng.randi() % 4], 340.0, 0.018)
	var seed_v := rng.randf_range(0.0, 20.0)
	if kind == "blouse":
		var prof: Array = []
		for k in 14:
			var y := 0.2 + 0.5 * float(k) / 13.0                       # from the hips to the shoulders
			prof.append(Vector2(form_r(y) * 1.06 + 0.004, y))
		var shell := cloth_shell(prof, 36, 0.035, 5.0, seed_v, 1.0, FORM_DEPTH)
		root.add_child(Props.mi(shell, mat, Props.at(Vector3(0, y0, 0))))
		# pinned down the middle front and at the shoulders, one side still unpinned (the cloth lifts a little)
		var colours := [Color("#d02020"), Color("#e0b020"), Color("#2060d0"), Color("#d8d8d8")]
		for k in 9:
			var y := 0.27 + 0.052 * float(k)
			var zf := form_r(y) * 1.06 * FORM_DEPTH + 0.006
			_pin(root, Vector3(rng.randf_range(-0.004, 0.004), y0 + y, zf), Vector3(rng.randf_range(-0.2, 0.2), rng.randf_range(-0.3, 0.3), 0.9).normalized(), colours[k % 4])
		for sx in [-1.0, 1.0]:
			_pin(root, Vector3(sx * 0.115, y0 + 0.66, form_r(0.66) * FORM_DEPTH * 1.06 * 0.8), Vector3(sx * 0.3, 0.5, 0.8).normalized(), Color("#d02020"))
		root.add_child(tag(Vector3(0.1, y0 + 0.62, form_r(0.62) * 1.06 * FORM_DEPTH * 0.9 + 0.01), rng))
	elif kind == "pants":
		var waist := Props.mi(cloth_shell([Vector2(0.125, 0.0), Vector2(0.13, 0.03), Vector2(0.135, 0.09), Vector2(0.15, 0.2), Vector2(0.16, 0.3)], 36, 0.03, 4.0, seed_v, 1.0, FORM_DEPTH), mat, Props.at(Vector3(0, y0 + 0.08, 0)))
		root.add_child(waist)
		for sx in [-1.0, 1.0]:
			var leg: Array = []
			for k in 12:
				var t := float(k) / 11.0
				leg.append(Vector2(lerpf(0.095, 0.072, t) * (1.0 + 0.05 * sin(t * 9.0)), 0.0 - t * 0.5))
			var lm := cloth_shell(leg, 28, 0.06, 4.0, seed_v + sx, 1.0, 1.0)
			root.add_child(Props.mi(lm, mat, Props.at(Vector3(sx * 0.085, y0 + 0.3, 0.01))))
		# the legs hang from the waist, cut short: nothing below them but the pole and the tripod
		for k in 6:
			var a := -0.55 + 0.22 * float(k)
			_pin(root, Vector3(sin(a) * 0.14, y0 + 0.37, cos(a) * 0.14 * FORM_DEPTH * 1.2), Vector3(sin(a), 0.4, cos(a)).normalized(), [Color("#d02020"), Color("#e0b020"), Color("#2060d0")][k % 3])
		root.add_child(tag(Vector3(0.12, y0 + 0.4, 0.13), rng))
	# the tape measure round the neck, its ends hanging
	var tape := Props.surf(4, {"col_a": Color("#e8dcb0"), "col_b": Color("#e8dcb0"), "col_c": Color("#2a2a2a"), "scale": 700.0, "pattern": 1, "pat_size": 0.01})
	var ring := TorusMesh.new()
	ring.inner_radius = 0.064
	ring.outer_radius = 0.07
	root.add_child(Props.mi(ring, tape, Props.at(Vector3(0, y0 + 0.715, 0.0)).scaled_local(Vector3(1.2, 0.5, 1.0)), false))
	for s in [-1.0, 1.0]:
		root.add_child(Props.mi(Props.box(Vector3(0.016, 0.34 + 0.08 * s, 0.0012)), tape, Transform3D(Basis(Vector3.FORWARD, deg_to_rad(5.0 * s)), Vector3(s * 0.04, y0 + 0.58, 0.075)), false))
	return root


# A cloth cut part-way across a table: it lies flat on the top, one edge hangs over, chalk lines mark the cut. The origin is
# at the table top's centre; w along x, d along z.
static func half_cut_cloth(w: float, d: float, hang: float, hex: String, seed_v: float) -> MeshInstance3D:
	var nx := 44
	var nz := 30
	var v := PackedVector3Array()
	var idx := PackedInt32Array()
	var uv := PackedVector2Array()
	var droop_from := d * 0.5 - 0.06
	for j in nz + 1:
		var t := float(j) / float(nz)
		var z0 := -d * 0.5 + t * (d + hang * 0.6)
		var y0 := 0.0
		var z := z0
		if z0 > droop_from:
			var s := (z0 - droop_from) / maxf(hang * 0.6 + 0.06, 0.001)       # 0..1 round the edge and down
			var ang := clampf(s * PI * 0.5, 0.0, PI * 0.5)
			var rr := hang * 0.32
			z = droop_from + sin(ang) * rr + maxf(s - 1.0, 0.0) * 0.1
			y0 = -(1.0 - cos(ang)) * rr - maxf(s - 0.5, 0.0) * hang * 0.8
		for i in nx + 1:
			var x := -w * 0.5 + w * float(i) / float(nx)
			var wr := 0.012 * sin(x * 11.0 + seed_v) * sin(z0 * 7.0 + seed_v * 1.7) + 0.006 * sin(x * 29.0 + z0 * 13.0)
			v.append(Vector3(x, y0 + 0.004 + wr, z))
			uv.append(Vector2(x, z0))
	for j in nz:
		for i in nx:
			var a := j * (nx + 1) + i
			var b := a + nx + 1
			idx.append_array([a, b, a + 1, a + 1, b, b + 1])
	var n := PackedVector3Array()
	n.resize(v.size())
	for t in range(0, idx.size(), 3):
		var fn := (v[idx[t + 1]] - v[idx[t]]).cross(v[idx[t + 2]] - v[idx[t]])
		for k in 3:
			n[idx[t + k]] += fn
	var up := 0.0
	for i in n.size():
		n[i] = n[i].normalized()
		up += n[i].y
	if up < 0.0:
		for i in n.size():
			n[i] = -n[i]
	var mesh := Props.finish(v, n, idx)
	var m := Props.world_surf(4, {"col_a": Color(hex), "col_b": Color(hex).lightened(0.2), "col_c": Color(hex).darkened(0.4), "scale": 300.0, "pattern": 0, "dust": 0.5, "sheen": 0.3})
	var node := Props.mi(mesh, m, Transform3D.IDENTITY, false)
	return node
