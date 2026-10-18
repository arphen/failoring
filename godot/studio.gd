# The tailor's fitting studio the mannequin stands in (look only): a room with a raised fitting platform and a runway, a
# lighting rig you can see, the lights that come out of it, warm practical lamps and the furniture of a tailor's shop.
# Built once by editor.gd (_build_scene); look.gd holds the environment and the quality levels, studio_props.gd the props.
#
# The rig is a classic fashion set-up, as a studio with no budget limit would light a fitting:
#   key    150 cm octabox, front left of the default camera, high (about 35 degrees above the chest), soft shadows
#   fill   a white V-flat on the other side bouncing the key back into the shadows (a weak, shadowless light)
#   rims   two tall strip boxes behind, left and right, drawing a bright edge down the shoulders, arms and legs
#   top    a small box on a boom above and behind the head (hair and shoulder light)
# Each lamp is a real light (SpotLight3D at the diffuser, inverse-square falloff) and a visible fixture whose diffuser
# glows; every lamp that is shown casts a shadow from where it stands (rims and top throw theirs towards the camera, the
# key throws its own back and to the right). The same lamps are painted into the sky that feeds the ambient light and the
# reflections (shaders/studio_sky.gdshader). Warm pendant lamps and cool window light mix with the softboxes.
#
# Coordinates: the mannequin stands at the origin with its feet on y = 0, which is the top of the platform (the
# solver's floor); the room's floor is PODIUM_H lower. +z points at the default camera.
extends RefCounted

const SKY_SHADER := preload("res://shaders/studio_sky.gdshader")
const Props = preload("res://studio_props.gd")
const Atelier = preload("res://studio_atelier.gd")

const CENTER := Vector3(0.0, 1.1, 0.0)        # the subject's chest: lamps aim here, the sky is seen from here
const LAMP_FLOOR := 0.02                      # a lamp that is switched off keeps this share of its energy: with every spot at exactly 0 the
#                                              global illumination (SDFGI) turned the whole room black, windows and bulbs included
const SKY_GLOW := 0.3                         # a diffuser's radiance in the sky, per unit of its fixture glow

# kind: octa (octagonal softbox, size = width), strip / box (size = width, height), vflat (size = panel width, height)
# energy: SpotLight3D energy at the lamp; with inverse-square falloff the light on the subject is energy / distance^2.
const LAMPS := [
	{"id": "key", "label": "Key softbox", "kind": "octa", "pos": Vector3(-2.25, 2.35, 2.25), "aim": Vector3(0.0, 1.15, 0.0), "size": Vector2(1.5, 1.5),
		"color": Color(1.0, 0.955, 0.9), "energy": 8.0, "angle": 45.0, "soft": 0.9, "shadow": true, "glow": 5.0},
	{"id": "fill", "label": "Fill (V-flat)", "kind": "vflat", "pos": Vector3(2.55, 1.2, 1.55), "aim": Vector3(0.0, 1.0, 0.0), "size": Vector2(1.2, 2.4),
		"color": Color(1.0, 0.97, 0.94), "energy": 2.6, "angle": 75.0, "soft": 1.5, "shadow": false, "glow": 0.0},
	{"id": "rim_l", "label": "Rim light, left", "kind": "strip", "pos": Vector3(-1.95, 1.3, -2.1), "aim": Vector3(0.0, 1.05, 0.0), "size": Vector2(0.35, 1.7),
		"color": Color(0.93, 0.96, 1.0), "energy": 4.5, "angle": 32.0, "soft": 0.35, "shadow": true, "glow": 6.0},
	{"id": "rim_r", "label": "Rim light, right", "kind": "strip", "pos": Vector3(2.05, 1.3, -1.95), "aim": Vector3(0.0, 1.05, 0.0), "size": Vector2(0.35, 1.7),
		"color": Color(0.93, 0.96, 1.0), "energy": 3.8, "angle": 32.0, "soft": 0.35, "shadow": true, "glow": 6.0},
	{"id": "top", "label": "Top light", "kind": "box", "pos": Vector3(0.0, 2.95, -0.75), "aim": Vector3(0.0, 1.45, 0.0), "size": Vector2(0.6, 0.6),
		"color": Color(1.0, 0.97, 0.93), "energy": 1.2, "angle": 60.0, "soft": 0.4, "shadow": true, "glow": 4.0},
]

# Warm practical lamps (pendants, 2700 K) and cool daylight from the windows.
const WARM := Color(1.0, 0.68, 0.38)
const DAYLIGHT := Color(0.72, 0.84, 1.0)
const PENDANTS := [
	{"pos": Vector3(-3.95, 0.0, 0.0), "energy": 2.6}, {"pos": Vector3(-3.95, 0.0, 1.7), "energy": 2.6},
	{"pos": Vector3(-1.6, 0.0, -2.2), "energy": 1.8}, {"pos": Vector3(1.6, 0.0, -2.35), "energy": 1.8},
	{"pos": Vector3(3.9, 0.0, -1.6), "energy": 2.0},
]
# windows: wall (b back z-, l left x-, f front z+), centre along the wall, sill height above the floor, size
const WINDOWS := [
	{"wall": "b", "at": 0.0, "sill": 0.8, "size": Vector2(2.4, 2.4)},
	{"wall": "l", "at": -1.7, "sill": 1.0, "size": Vector2(1.3, 2.0)}, {"wall": "l", "at": 3.1, "sill": 1.0, "size": Vector2(1.3, 2.0)},
	{"wall": "f", "at": -2.2, "sill": 0.9, "size": Vector2(1.2, 2.1)}, {"wall": "f", "at": 2.2, "sill": 0.9, "size": Vector2(1.2, 2.1)},
]

# the room: a plastered box with a boarded floor; the platform stands PODIUM_H above the floor, its top at y = 0
const ROOM_X := 4.8             # m, half width (x)
const ROOM_Z := 4.8             # m, half depth (z)
const CEIL_Y := 3.6             # m, ceiling height above the platform top
const PODIUM_H := 0.18          # m
const PODIUM := Vector2(1.8, 1.8)
const RUNWAY_W := 1.1
const RUNWAY_END := 3.45        # m, front edge of the runway on z
const FLOOR_Y := -PODIUM_H      # the room's floor
const MIRROR_POS := Vector3(-3.35, FLOOR_Y, -1.55)    # the three-panel fitting mirror, turned towards the platform
const TABLE_POS := Vector3(-4.05, FLOOR_Y, 0.85)      # the cutting table against the left wall

const METAL := Color("#1b1c1e")
const FABRIC := Color("#0f0f10")


# Builds everything under `parent`. Returns {lights: {id: SpotLight3D}, key: SpotLight3D, sky: Sky, fixtures: [Node3D],
# practicals: [Light3D], root: Node3D}.
static func build(parent: Node3D) -> Dictionary:
	var root := Node3D.new()
	root.name = "Studio"
	parent.add_child(root)
	root.add_child(shell())
	root.add_child(platform())
	var practicals := []
	root.add_child(furnish(practicals))
	var lights := {}
	var fixtures := []
	var fixture_of := {}
	for lamp in LAMPS:
		var l := spot(lamp)
		root.add_child(l)
		lights[lamp["id"]] = l
		var fx := fixture(lamp)
		root.add_child(fx)
		fixtures.append(fx)
		fixture_of[lamp["id"]] = fx
	return {"root": root, "lights": lights, "key": lights["key"], "sky": sky(), "fixtures": fixtures, "fixture_of": fixture_of, "practicals": practicals}


# Switches one lamp on or off, so that the picture shows it: its light (down to LAMP_FLOOR of its energy), the glow of its
# diffuser, and its share of the studio sky (the ambient light and the reflections).
static func set_lamp(st: Dictionary, id: String, on: bool) -> void:
	for i in LAMPS.size():
		var lamp: Dictionary = LAMPS[i]
		if String(lamp["id"]) != id:
			continue
		var k := 1.0 if on else LAMP_FLOOR
		(st["lights"][id] as Light3D).light_energy = float(lamp["energy"]) * k
		var fx := (st["fixture_of"] as Dictionary).get(id) as Node3D
		if fx != null and fx.has_meta("glow_mat"):
			(fx.get_meta("glow_mat") as StandardMaterial3D).emission_energy_multiplier = float(lamp["glow"]) * (1.0 if on else 0.0)
		var sky_res := st.get("sky") as Sky
		if sky_res != null and sky_res.sky_material is ShaderMaterial and sky_res.has_meta("lamp_col0"):
			var base: PackedVector3Array = sky_res.get_meta("lamp_col0")
			var cols := base.duplicate()
			var m := sky_res.sky_material as ShaderMaterial
			var now = m.get_shader_parameter("lamp_col")
			if now is PackedVector3Array:
				cols = (now as PackedVector3Array).duplicate()
			cols[i] = base[i] * k
			m.set_shader_parameter("lamp_col", cols)
		return


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
	l.shadow_blur = 2.5                            # wide filter
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
	s.set_meta("lamp_col0", cols)
	s.sky_material = m
	s.radiance_size = Sky.RADIANCE_SIZE_256
	s.process_mode = Sky.PROCESS_MODE_QUALITY
	return s


# ---------------------------------------------------------------- the room
# Floor, walls, ceiling, skirting and beams. The walls are thick boxes (no light leaks through them into the GI).
static func shell() -> Node3D:
	var root := Node3D.new()
	root.name = "Room"
	var plaster := Props.world_surf(1, {"col_a": Color("#d9d2c5"), "col_b": Color("#a99f8e"), "wear": 0.7, "dust": 0.8, "scale": 1.0})
	var ceiling := Props.world_surf(1, {"col_a": Color("#e4dfd5"), "col_b": Color("#c9c1b1"), "wear": 0.4, "dust": 0.4, "seed": 5.0})
	var boards := Props.world_surf(0, {"col_a": Color("#5f4c39"), "col_b": Color("#85694a"), "col_c": Color("#a99c88"), "wear": 1.0, "dust": 0.8, "scale": 1.0, "seed": 1.0})
	var paint := Props.world_surf(6, {"col_a": Color("#e8e4da"), "col_b": Color("#8c6a46"), "col_c": Color("#8a8378"), "wear": 0.7, "dust": 0.7})
	var beam := Props.surf(3, {"col_a": Color("#4a3322"), "col_b": Color("#6a4a30"), "col_c": Color("#8a7a66"), "wear": 0.5, "dust": 0.7, "grain_axis": Vector3(1, 0, 0)})
	var t := 0.4
	var hx := ROOM_X
	var hz := ROOM_Z
	var top := CEIL_Y
	var fy := FLOOR_Y
	var h := top - fy
	# floor and ceiling slabs
	root.add_child(Props.mi(Props.box(Vector3(2.0 * hx + 2.0 * t, t, 2.0 * hz + 2.0 * t)), boards, Props.at(Vector3(0.0, fy - t * 0.5, 0.0)), false))
	root.add_child(Props.mi(Props.box(Vector3(2.0 * hx + 2.0 * t, t, 2.0 * hz + 2.0 * t)), ceiling, Props.at(Vector3(0.0, top + t * 0.5, 0.0)), false))
	# four walls
	root.add_child(Props.mi(Props.box(Vector3(2.0 * hx + 2.0 * t, h + 2.0 * t, t)), plaster, Props.at(Vector3(0.0, (top + fy) * 0.5, -hz - t * 0.5)), false))
	root.add_child(Props.mi(Props.box(Vector3(2.0 * hx + 2.0 * t, h + 2.0 * t, t)), plaster, Props.at(Vector3(0.0, (top + fy) * 0.5, hz + t * 0.5)), false))
	root.add_child(Props.mi(Props.box(Vector3(t, h + 2.0 * t, 2.0 * hz)), plaster, Props.at(Vector3(-hx - t * 0.5, (top + fy) * 0.5, 0.0)), false))
	root.add_child(Props.mi(Props.box(Vector3(t, h + 2.0 * t, 2.0 * hz)), plaster, Props.at(Vector3(hx + t * 0.5, (top + fy) * 0.5, 0.0)), false))
	# skirting boards and a picture rail along every wall
	var sk := 0.16
	for z in [-hz + 0.02, hz - 0.02]:
		root.add_child(Props.mi(Props.box(Vector3(2.0 * hx, sk, 0.04)), paint, Props.at(Vector3(0.0, fy + sk * 0.5, float(z))), false))
		root.add_child(Props.mi(Props.box(Vector3(2.0 * hx, 0.06, 0.04)), paint, Props.at(Vector3(0.0, top - 0.5, float(z))), false))
	for x in [-hx + 0.02, hx - 0.02]:
		root.add_child(Props.mi(Props.box(Vector3(0.04, sk, 2.0 * hz)), paint, Props.at(Vector3(float(x), fy + sk * 0.5, 0.0)), false))
		root.add_child(Props.mi(Props.box(Vector3(0.04, 0.06, 2.0 * hz)), paint, Props.at(Vector3(float(x), top - 0.5, 0.0)), false))
	# ceiling beams across the room
	for z in [-3.2, -1.1, 1.1, 3.2]:
		root.add_child(Props.mi(Props.box(Vector3(2.0 * hx, 0.28, 0.2)), beam, Props.at(Vector3(0.0, top - 0.14, float(z))), false))
	return root


# The fitting platform and the runway that leads to it: painted sides, a rounded edge, a dark wool top.
static func platform() -> Node3D:
	var root := Node3D.new()
	root.name = "Platform"
	var paint := Props.surf(6, {"col_a": Color("#c9c2b4"), "col_b": Color("#8c6a46"), "col_c": Color("#8a8378"), "wear": 0.8, "dust": 0.8, "seed": 4.0})
	var felt := Props.world_surf(2, {"col_a": Color("#2f3338"), "col_b": Color("#4a4e54"), "col_c": Color("#5a5e62"), "wear": 0.9, "dust": 0.6})
	var ph := PODIUM_H
	# platform: bottom at the floor, top at y = 0, 2.5 cm bevel
	root.add_child(Props.mi(Props.rounded_slab(PODIUM.x, PODIUM.y, ph, 0.025, 0.06, FLOOR_Y, 0), paint))
	root.add_child(Props.mi(Props.rounded_slab(PODIUM.x, PODIUM.y, ph, 0.025, 0.06, FLOOR_Y, 1), felt))
	# runway: from inside the platform to RUNWAY_END, its top 3 mm lower (no z-fighting, and a believable seam)
	var rl := RUNWAY_END - 0.5
	var r_off := Transform3D(Basis(), Vector3(0.0, -0.003, 0.5 + rl * 0.5))
	root.add_child(Props.mi(Props.rounded_slab(RUNWAY_W, rl, ph, 0.02, 0.04, FLOOR_Y, 0), paint, r_off))
	root.add_child(Props.mi(Props.rounded_slab(RUNWAY_W, rl, ph, 0.02, 0.04, FLOOR_Y, 1), felt, r_off))
	# a step at the front of the runway
	var st := Transform3D(Basis(), Vector3(0.0, 0.0, RUNWAY_END + 0.15))
	root.add_child(Props.mi(Props.rounded_slab(RUNWAY_W, 0.34, ph * 0.5, 0.015, 0.03, FLOOR_Y, 0), paint, st))
	root.add_child(Props.mi(Props.rounded_slab(RUNWAY_W, 0.34, ph * 0.5, 0.015, 0.03, FLOOR_Y, 1), felt, st))
	return root


# Furniture, props and lamps: a tailor's atelier round the platform. Cloth on shelves along the back wall under a tall window,
# the cutting table with a half-cut cloth on it against the left wall, a desk with a lamp, make-up and a lipstick-smeared hand
# mirror at the right, dead mannequins in pinned half-garments along the right wall, a tea corner, chairs, an oriental rug.
static func furnish(practicals: Array) -> Node3D:
	var root := Node3D.new()
	root.name = "Furniture"
	var rng := RandomNumberGenerator.new()
	rng.seed = 20261016
	var fy := FLOOR_Y

	root.add_child(Atelier.rug(5.4, 3.9, fy + 0.006))
	root.get_child(0).position.z = 0.5

	# cubby shelving of cloth either side of the back window (it faces the room, +z)
	for sx in [-1.0, 1.0]:
		var sh := Props.shelving(3, 4, rng)
		sh.position = Vector3(sx * 2.95, fy, -ROOM_Z + 0.52)
		root.add_child(sh)
	# garment racks along the left wall, in the back corner
	var rk2 := Props.rack(rng, 1.5)
	rk2.transform = Transform3D(Basis(Vector3.UP, PI * 0.5), Vector3(-4.35, fy, -3.1))
	root.add_child(rk2)
	# the cutting table against the left wall, a cloth half cut on its mat
	var ct := Props.cutting_table(rng)
	ct.transform = Transform3D(Basis(Vector3.UP, PI * 0.5), TABLE_POS)
	var cloth := Atelier.half_cut_cloth(0.95, 0.8, 0.45, "#7f8aa0", 3.0)
	cloth.position = Vector3(-0.45, 0.923, 0.2)
	ct.add_child(cloth)
	root.add_child(ct)
	for k in 2:
		var ch := Atelier.chair()
		ch.transform = Transform3D(Basis(Vector3.UP, -PI * 0.5 + 0.35 * float(k) - 0.2), Vector3(-3.05, fy, 0.15 + 1.75 * float(k)))
		root.add_child(ch)
	# dead mannequins along the right wall, turned towards the platform
	var kinds := ["blouse", "pants", "bare"]
	for k in 3:
		var df := Atelier.dead_form(kinds[k], rng)
		df.transform = Transform3D(Basis(Vector3.UP, -1.26 + 0.2 * float(k - 1)), Vector3(4.05, fy, -3.1 + 1.55 * float(k)))
		root.add_child(df)
	# the desk, with its chair
	var dk := Atelier.desk(rng)
	dk.transform = Transform3D(Basis(Vector3.UP, -PI * 0.5), Vector3(4.3, fy, 2.7))
	root.add_child(dk)
	var dch := Atelier.chair()
	dch.transform = Transform3D(Basis(Vector3.UP, PI * 0.5 - 0.25), Vector3(3.35, fy, 2.85))
	root.add_child(dch)
	# the tea corner, back left
	var tea := Atelier.tea_corner()
	tea.position = Vector3(-3.9, fy, -4.0)
	root.add_child(tea)
	# the fitting mirror, turned towards the platform, with lipstick smeared on its middle panel
	var mr := Props.mirror3()
	var mp := MIRROR_POS
	mr.transform = Transform3D(Basis(Vector3.UP, atan2(-mp.x, -mp.z)), mp)
	mr.add_child(Atelier.smear(0.52, 1.64, Vector3(0.0, 0.08 + 1.75 * 0.5, 0.0145), 3.0))
	root.add_child(mr)

	# pendant lamps: warm light from the ceiling
	var pi := 0
	for pd in PENDANTS:
		var drop := rng.randf_range(0.85, 1.35)
		var pnd := Props.pendant(drop, WARM, float(pd["energy"]), float(pi))
		var pp: Vector3 = pd["pos"]
		(pnd["node"] as Node3D).position = Vector3(pp.x, CEIL_Y - drop, pp.z)
		root.add_child(pnd["node"])
		practicals.append(pnd["light"])
		pi += 1

	# windows, each with a cool shadowless omni just inside the glass
	for w in WINDOWS:
		var sz: Vector2 = w["size"]
		var win := Props.window(sz.x, sz.y, DAYLIGHT, 3.0)
		var along: float = w["at"]
		var y: float = fy + float(w["sill"])
		match String(w["wall"]):
			"b":
				win.transform = Transform3D(Basis(), Vector3(along, y, -ROOM_Z + 0.07))
			"l":
				win.transform = Transform3D(Basis(Vector3.UP, PI * 0.5), Vector3(-ROOM_X + 0.07, y, along))
			"f":
				win.transform = Transform3D(Basis(Vector3.UP, PI), Vector3(along, y, ROOM_Z - 0.07))
		root.add_child(win)
		var dl := OmniLight3D.new()
		dl.name = "Daylight"
		dl.light_color = DAYLIGHT
		dl.light_energy = 1.4
		dl.omni_range = 6.5
		dl.omni_attenuation = 2.0
		dl.shadow_enabled = false
		dl.light_specular = 0.3
		dl.position = win.position + Vector3(0.0, sz.y * 0.5, 0.0) + win.transform.basis * Vector3(0.0, 0.0, 0.5)
		root.add_child(dl)
		practicals.append(dl)
	return root


# ---------------------------------------------------------------- camera containment
# The orbit camera lives inside the room. How far from `pivot` can it go along the unit vector `dir` before it meets a
# wall, the floor or the ceiling (keeping `margin` away)?
static func max_orbit_dist(pivot: Vector3, dir: Vector3, margin := 0.35) -> float:
	var t := 1.0e9
	var lo := Vector3(-ROOM_X + margin, FLOOR_Y + margin, -ROOM_Z + margin)
	var hi := Vector3(ROOM_X - margin, CEIL_Y - margin, ROOM_Z - margin)
	for a in 3:
		if absf(dir[a]) < 1.0e-5:
			continue
		t = minf(t, ((hi[a] if dir[a] > 0.0 else lo[a]) - pivot[a]) / dir[a])
	return maxf(t, 0.3)


# A camera at `pos` looking at `tgt` with vertical field of view `fov` (degrees), pulled in along its line of sight until it
# is inside the room, with the field of view widened so that the framing at the target stays the same. {pos, fov}.
static func fit_camera(pos: Vector3, tgt: Vector3, fov: float) -> Dictionary:
	var d := pos - tgt
	var len := d.length()
	if len < 1.0e-4:
		return {"pos": pos, "fov": fov}
	var t := minf(len, max_orbit_dist(tgt, d / len))
	if t >= len - 1.0e-4:
		return {"pos": pos, "fov": fov}
	var k := t / len
	return {"pos": tgt + d * k, "fov": rad_to_deg(2.0 * atan(tan(deg_to_rad(fov) * 0.5) / k))}


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
	var base := Vector3(top.x, FLOOR_Y, top.z)
	var h := maxf(0.2, top.y - FLOOR_Y - 0.05)
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
	root.set_meta("glow_mat", glow)   # set_lamp() turns the diffuser's glow off with the lamp
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
			var hinge := Vector3(pos.x, FLOOR_Y, pos.z) - to * 0.45
			for side in [-1.0, 1.0]:
				var leaf_dir := to.rotated(Vector3.UP, side * PI * 0.25)
				var c := hinge + leaf_dir * sz.x * 0.5 + Vector3(0, sz.y * 0.5, 0)
				var bx := Basis(leaf_dir, Vector3.UP, leaf_dir.cross(Vector3.UP))
				root.add_child(_part(_box(Vector3(sz.x, sz.y, 0.05)), white, Transform3D(bx, c), true))
	return root
