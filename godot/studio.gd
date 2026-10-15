# The photo studio the mannequin stands in (look only): an infinity cove, a lighting rig you can see, and the lights
# that come out of it. Built once by editor.gd (_build_scene); look.gd holds the quality levels.
#
# The rig is a classic fashion set-up, as a studio with no budget limit would light a fitting:
#   key    150 cm octabox, front left of the default camera, high (about 35 degrees above the chest), soft shadows
#   fill   a white V-flat on the other side bouncing the key back into the shadows (a weak, shadowless light)
#   rims   two tall strip boxes behind, left and right, drawing a bright edge down the shoulders, arms and legs
#   top    a small box on a boom above and behind the head (hair and shoulder light)
# Each lamp is a real light (SpotLight3D at the diffuser, inverse-square falloff) and a visible fixture whose diffuser
# glows, and the same lamps are painted into the sky that feeds the ambient light and the reflections
# (shaders/studio_sky.gdshader), so a highlight on silk or in an eye sits where the softbox is.
extends RefCounted

const SKY_SHADER := preload("res://shaders/studio_sky.gdshader")

const CENTER := Vector3(0.0, 1.1, 0.0)
const SKY_GLOW := 0.3                         # a diffuser's radiance in the sky, per unit of its fixture glow        # the subject's chest: lamps aim here, the sky is seen from here

# kind: octa (octagonal softbox, size = width), strip / box (size = width, height), vflat (size = panel width, height)
# energy: SpotLight3D energy at the lamp; with inverse-square falloff the light on the subject is energy / distance^2.
const LAMPS := [
	{"id": "key", "kind": "octa", "pos": Vector3(-2.25, 2.35, 2.25), "aim": Vector3(0.0, 1.15, 0.0), "size": Vector2(1.5, 1.5),
		"color": Color(1.0, 0.955, 0.9), "energy": 8.0, "angle": 45.0, "soft": 0.9, "shadow": true, "glow": 5.0},
	{"id": "fill", "kind": "vflat", "pos": Vector3(2.55, 1.2, 1.55), "aim": Vector3(0.0, 1.0, 0.0), "size": Vector2(1.2, 2.4),
		"color": Color(1.0, 0.97, 0.94), "energy": 2.6, "angle": 75.0, "soft": 1.5, "shadow": false, "glow": 0.0},
	{"id": "rim_l", "kind": "strip", "pos": Vector3(-1.95, 1.3, -2.1), "aim": Vector3(0.0, 1.05, 0.0), "size": Vector2(0.35, 1.7),
		"color": Color(0.93, 0.96, 1.0), "energy": 4.5, "angle": 32.0, "soft": 0.35, "shadow": false, "glow": 6.0},
	{"id": "rim_r", "kind": "strip", "pos": Vector3(2.05, 1.3, -1.95), "aim": Vector3(0.0, 1.05, 0.0), "size": Vector2(0.35, 1.7),
		"color": Color(0.93, 0.96, 1.0), "energy": 3.8, "angle": 32.0, "soft": 0.35, "shadow": false, "glow": 6.0},
	{"id": "top", "kind": "box", "pos": Vector3(0.0, 2.95, -0.75), "aim": Vector3(0.0, 1.45, 0.0), "size": Vector2(0.6, 0.6),
		"color": Color(1.0, 0.97, 0.93), "energy": 1.2, "angle": 60.0, "soft": 0.4, "shadow": false, "glow": 4.0},
]

# infinity cove: floor, a curved sweep and a wall all round (the camera orbits), matte light grey paint
const COVE_R := 11.0            # m, radius of the wall
const COVE_BEND := 2.2          # m, radius of the curve between floor and wall
const COVE_H := 7.0             # m, wall height
const COVE_PAINT := Color("#9a968f")
const FLOOR_Y := -0.001         # just under the solver's floor (y = 0)

const METAL := Color("#1b1c1e")
const FABRIC := Color("#0f0f10")


# Builds everything under `parent`. Returns {lights: {id: SpotLight3D}, key: SpotLight3D, sky: Sky, nodes: [fixtures]}.
static func build(parent: Node3D) -> Dictionary:
	var root := Node3D.new()
	root.name = "Studio"
	parent.add_child(root)
	root.add_child(cove())
	var lights := {}
	var fixtures := []
	for lamp in LAMPS:
		var l := spot(lamp)
		root.add_child(l)
		lights[lamp["id"]] = l
		var fx := fixture(lamp)
		root.add_child(fx)
		fixtures.append(fx)
	return {"root": root, "lights": lights, "key": lights["key"], "sky": sky(), "fixtures": fixtures}


# The light itself: a spot at the diffuser's centre, aimed at the subject.
static func spot(lamp: Dictionary) -> SpotLight3D:
	var l := SpotLight3D.new()
	l.name = "Light_" + String(lamp["id"])
	var pos: Vector3 = lamp["pos"]
	var aim: Vector3 = lamp["aim"]
	l.transform = Transform3D(Basis.looking_at(aim - pos, Vector3.UP), pos)
	l.light_color = lamp["color"]
	l.light_energy = float(lamp["energy"])
	l.spot_range = 14.0
	l.spot_attenuation = 2.0                      # inverse square: the floor and walls fall off as in a real studio
	l.spot_angle = float(lamp["angle"])
	# The beam covers the subject and the floor round it, no more: the shadow map spans the beam, and a 140 degree beam
	# spread the key's shadow texels too thin (its shadow edge ran a few cm into the lit side). A slow falloff to the
	# edge hides where the beam ends, as a softbox's light has no edge.
	l.spot_angle_attenuation = 2.2
	l.light_size = float(lamp["soft"])            # soft shadows (Forward+), as from a diffuser this wide
	l.light_specular = 0.6
	l.shadow_enabled = bool(lamp["shadow"])
	# the body mesh is coarse on the limbs (edges up to 2.5 cm): with less normal bias its flat faces shadow themselves
	# near the terminator, which drew dark lines down the legs and the cheeks
	l.shadow_bias = 0.06
	l.shadow_normal_bias = 3.0
	l.shadow_blur = 2.5                            # wide filter: the OpenGL renderer has no light-size soft shadows
	return l


# A sky that is the studio seen from the subject (ambient and reflections only).
static func sky() -> Sky:
	var s := Sky.new()
	var m := ShaderMaterial.new()
	m.shader = SKY_SHADER
	var dirs := PackedVector3Array()
	var ups := PackedVector3Array()
	var halves := PackedVector3Array()
	var cols := PackedVector3Array()
	for lamp in LAMPS:
		var pos: Vector3 = lamp["pos"]
		var to := pos - CENTER
		var dist := to.length()
		var f := to / dist
		var up := (Vector3.UP - f * f.dot(Vector3.UP)).normalized()
		var sz: Vector2 = lamp["size"]
		var kind := String(lamp["kind"])
		dirs.append(f)
		ups.append(up)
		halves.append(Vector3(0.5 * sz.x / dist, 0.5 * sz.y / dist, 1.0 if kind == "octa" else 0.0))
		# radiance of the diffuser: its glow, or for the V-flat the key's light bounced off white paint
		var c: Color = lamp["color"]
		var rad := SKY_GLOW * float(lamp["glow"]) if kind != "vflat" else 0.25
		cols.append(Vector3(c.r, c.g, c.b) * rad)
	m.set_shader_parameter("lamp_n", LAMPS.size())
	m.set_shader_parameter("lamp_dir", dirs)
	m.set_shader_parameter("lamp_up", ups)
	m.set_shader_parameter("lamp_half", halves)
	m.set_shader_parameter("lamp_col", cols)
	s.sky_material = m
	s.radiance_size = Sky.RADIANCE_SIZE_256
	s.process_mode = Sky.PROCESS_MODE_QUALITY
	return s


# ---------------------------------------------------------------- the cove
static func cove() -> MeshInstance3D:
	# profile from the centre outwards: (radius, height, normal) with the normal facing into the studio
	var prof := []
	var floor_r := COVE_R - COVE_BEND
	for r in [0.0, 0.3, 0.7, 1.2, 1.8, 2.6, 3.6, 4.8, 6.2, 7.6, floor_r]:
		prof.append([float(r), 0.0, Vector2(0.0, 1.0)])
	var nb := 12
	for k in range(1, nb + 1):
		var a := 0.5 * PI * float(k) / float(nb)
		var r := floor_r + COVE_BEND * sin(a)
		var y := COVE_BEND * (1.0 - cos(a))
		prof.append([r, y, Vector2(-sin(a), cos(a))])
	for y in [COVE_BEND + 1.0, COVE_BEND + 2.5, COVE_H]:
		prof.append([COVE_R, float(y), Vector2(-1.0, 0.0)])
	var segs := 128
	var v := PackedVector3Array()
	var n := PackedVector3Array()
	var uv := PackedVector2Array()
	var idx := PackedInt32Array()
	var s_acc := 0.0
	for i in prof.size():
		if i > 0:
			s_acc += Vector2(prof[i][0], prof[i][1]).distance_to(Vector2(prof[i - 1][0], prof[i - 1][1]))
		for j in segs + 1:
			var ph := TAU * float(j) / float(segs)
			var c := cos(ph)
			var sn := sin(ph)
			var r: float = prof[i][0]
			var pn: Vector2 = prof[i][2]
			v.append(Vector3(r * c, float(prof[i][1]) + FLOOR_Y, r * sn))
			n.append(Vector3(pn.x * c, pn.y, pn.x * sn))
			uv.append(Vector2(float(j) / float(segs), s_acc))
	for i in prof.size() - 1:
		for j in segs:
			var a := i * (segs + 1) + j
			var b := a + segs + 1
			# clockwise seen from inside the studio (from above for the floor)
			idx.append_array([a, b, a + 1, a + 1, b, b + 1])
	var arr: Array = []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = v
	arr[Mesh.ARRAY_NORMAL] = n
	arr[Mesh.ARRAY_TEX_UV] = uv
	arr[Mesh.ARRAY_INDEX] = idx
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	var mi := MeshInstance3D.new()
	mi.name = "Cove"
	mi.mesh = mesh
	var m := StandardMaterial3D.new()
	m.albedo_color = COVE_PAINT
	m.roughness = 0.82
	m.metallic_specular = 0.3
	mi.material_override = m
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	return mi


# ---------------------------------------------------------------- fixtures
static func _mat(c: Color, rough: float, metal: float = 0.0) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = rough
	m.metallic = metal
	# the orbiting camera can come close to a stand: fade it out instead of clipping through it
	m.distance_fade_mode = BaseMaterial3D.DISTANCE_FADE_PIXEL_DITHER
	m.distance_fade_min_distance = 0.35
	m.distance_fade_max_distance = 1.1
	return m


static func _glow_mat(c: Color, energy: float) -> StandardMaterial3D:
	var m := _mat(Color(0.9, 0.9, 0.9), 1.0)
	m.emission_enabled = true
	m.emission = c
	m.emission_energy_multiplier = energy
	return m


static func _part(mesh: Mesh, mat: Material, xf: Transform3D, shadow := false) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.material_override = mat
	mi.transform = xf
	# fixtures stand outside the beams: only the V-flat's boards cast shadows (they catch the key's shadow of the mannequin)
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON if shadow else GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	return mi


static func _cyl(r_top: float, r_bot: float, h: float, segs: int = 12) -> CylinderMesh:
	var c := CylinderMesh.new()
	c.top_radius = r_top
	c.bottom_radius = r_bot
	c.height = h
	c.radial_segments = segs
	c.rings = 1
	return c


static func _box(s: Vector3) -> BoxMesh:
	var b := BoxMesh.new()
	b.size = s
	return b


# A light stand from the floor to `top`: a pole and three legs.
static func _stand(root: Node3D, top: Vector3, metal: Material) -> void:
	var base := Vector3(top.x, 0.0, top.z)
	var h := maxf(0.2, top.y - 0.05)
	root.add_child(_part(_cyl(0.014, 0.019, h, 10), metal, Transform3D(Basis(), base + Vector3(0, h * 0.5, 0)), false))
	for k in 3:
		var a := TAU * float(k) / 3.0 + 0.4
		var foot := base + Vector3(cos(a) * 0.5, 0.0, sin(a) * 0.5)
		var hip := base + Vector3(0.0, 0.55, 0.0)
		var mid := (foot + hip) * 0.5
		var len := foot.distance_to(hip)
		var up := (hip - foot).normalized()
		var bx := Basis(up.cross(Vector3.FORWARD).normalized() if absf(up.dot(Vector3.FORWARD)) < 0.99 else Vector3.RIGHT, up, Vector3.ZERO)
		bx.z = bx.x.cross(bx.y).normalized()
		root.add_child(_part(_cyl(0.01, 0.01, len, 8), metal, Transform3D(bx, mid), false))


static func fixture(lamp: Dictionary) -> Node3D:
	var root := Node3D.new()
	root.name = "Fixture_" + String(lamp["id"])
	var pos: Vector3 = lamp["pos"]
	var aim: Vector3 = lamp["aim"]
	var sz: Vector2 = lamp["size"]
	var kind := String(lamp["kind"])
	var metal := _mat(METAL, 0.45, 0.6)
	var fabric := _mat(FABRIC, 0.9)
	var glow := _glow_mat(lamp["color"], float(lamp["glow"]))
	# the head: -z points at the subject
	var head := Node3D.new()
	head.transform = Transform3D(Basis.looking_at(aim - pos, Vector3.UP), pos)
	root.add_child(head)
	match kind:
		"octa":
			var depth := 0.5
			# shell: an eight-sided cone from the speed ring (back) to the diffuser (front)
			var shell := _cyl(0.1, 0.5 * sz.x / 0.9238795, depth, 8)
			head.add_child(_part(shell, fabric, Transform3D(Basis(Vector3.RIGHT, PI * 0.5), Vector3(0, 0, depth * 0.5))))
			var face := _cyl(0.5 * sz.x / 0.9238795 - 0.02, 0.5 * sz.x / 0.9238795 - 0.02, 0.01, 8)
			head.add_child(_part(face, glow, Transform3D(Basis(Vector3.RIGHT, PI * 0.5), Vector3(0, 0, -0.006)), false))
			# speed ring and strobe head behind it
			head.add_child(_part(_cyl(0.075, 0.075, 0.28, 16), metal, Transform3D(Basis(Vector3.RIGHT, PI * 0.5), Vector3(0, 0, depth + 0.14))))
			_stand(root, pos + (pos - aim).normalized() * 0.65 - Vector3(0, 0.25, 0), metal)
		"strip", "box":
			var depth := 0.35 if kind == "strip" else 0.3
			head.add_child(_part(_box(Vector3(sz.x + 0.04, sz.y + 0.04, depth)), fabric, Transform3D(Basis(), Vector3(0, 0, depth * 0.5))))
			var q := QuadMesh.new()
			q.size = sz
			# QuadMesh faces +z; turn it to face the subject (-z)
			head.add_child(_part(q, glow, Transform3D(Basis(Vector3.UP, PI), Vector3(0, 0, -0.004)), false))
			head.add_child(_part(_cyl(0.07, 0.07, 0.22, 16), metal, Transform3D(Basis(Vector3.RIGHT, PI * 0.5), Vector3(0, 0, depth + 0.11))))
			if kind == "strip":
				_stand(root, Vector3(pos.x, pos.y - sz.y * 0.5 - 0.05, pos.z) + (pos - aim).normalized() * 0.15, metal)
			else:
				# boom: a horizontal arm from a stand further back
				var back := pos + Vector3(0.0, 0.0, -1.4)
				_stand(root, back, metal)
				var arm_len := back.distance_to(pos + Vector3(0, 0.15, 0))
				var dir := (pos + Vector3(0, 0.15, 0) - back).normalized()
				var bx := Basis.looking_at(dir, Vector3.UP) * Basis(Vector3.RIGHT, PI * 0.5)
				root.add_child(_part(_cyl(0.015, 0.015, arm_len, 10), metal, Transform3D(bx, (back + pos + Vector3(0, 0.15, 0)) * 0.5), false))
				root.add_child(_part(_cyl(0.06, 0.06, 0.25, 12), metal, Transform3D(Basis(), back + (back - pos).normalized() * 0.4 + Vector3(0, 0.05, 0)), false))
		"vflat":
			# two white boards hinged at 90 degrees, open towards the subject, standing on the floor
			var white := _mat(Color("#ecebe8"), 0.95)
			var to := (aim - pos)
			to.y = 0.0
			to = to.normalized()
			var hinge := Vector3(pos.x, 0.0, pos.z) - to * 0.45
			for side in [-1.0, 1.0]:
				var leaf_dir := to.rotated(Vector3.UP, side * PI * 0.25)
				var c := hinge + leaf_dir * sz.x * 0.5 + Vector3(0, sz.y * 0.5, 0)
				var bx := Basis(leaf_dir, Vector3.UP, leaf_dir.cross(Vector3.UP))
				root.add_child(_part(_box(Vector3(sz.x, sz.y, 0.05)), white, Transform3D(bx, c), true))
	return root
