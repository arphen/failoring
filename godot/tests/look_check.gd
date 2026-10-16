# Headless check of the mannequin's look layer (skin.gd, eyes.gd, studio.gd, look.gd): no window, no solver.
#   godot --headless --path godot --script res://tests/look_check.gd
# Exits 0 when every check passes, 1 otherwise.
extends SceneTree

const Look = preload("res://look.gd")
const Studio = preload("res://studio.gd")
const SkinMap = preload("res://skin.gd")
const Eyes = preload("res://eyes.gd")
const BodyGen = preload("res://body_gen.gd")
const BodyView = preload("res://scripts/body_loader.gd")
const PlanLoader = preload("res://plan_loader.gd")

var _fails := 0


func _check(ok: bool, what: String) -> void:
	print(("PASS " if ok else "FAIL ") + what)
	if not ok:
		_fails += 1


func _init() -> void:
	var M := BodyGen.load_data(PlanLoader.load_json("res://assets/body_data.json"))
	var nb: int = int(M["nb"])
	var t0 := Time.get_ticks_msec()
	var sk := SkinMap.build(M)
	var ms := Time.get_ticks_msec() - t0
	_check(int(sk["n"]) == nb and (sk["rest_xy"] as PackedVector2Array).size() == nb and (sk["rest_z_cav"] as PackedVector2Array).size() == nb
		and (sk["color"] as PackedColorArray).size() == nb, "skin attributes: one per body vertex (%d), built in %d ms" % [nb, ms])
	_check(SkinMap.build(M) == sk, "skin attributes are cached in the body data")

	# attribute ranges, and where the masks land
	var uv: PackedVector2Array = sk["rest_xy"]
	var uv2: PackedVector2Array = sk["rest_z_cav"]
	var col: PackedColorArray = sk["color"]
	var ok_range := true
	var red_knee := 0.0
	var red_belly := 0.0
	var thin_ear := 0.0
	var thin_trunk := 0.0
	var nails := 0
	var areola := 0
	var creases := 0
	var parts: Array = M["parts"]
	var part: PackedInt32Array = M["part"]
	var knee := SkinMap.joint_rest(M, "l-knee")
	for i in nb:
		var c := col[i]
		if not (is_finite(uv2[i].y) and absf(uv2[i].y) <= 1.0 and c.r >= 0.0 and c.r <= 1.0 and c.g >= 0.0 and c.g <= 1.0
				and c.b >= 0.0 and c.b <= 1.0 and c.a >= 0.0 and c.a <= 1.0):
			ok_range = false
		var p := Vector3(uv[i].x, uv[i].y, uv2[i].x)
		if p.distance_to(knee + Vector3(0, 0, 0.03)) < 0.03:
			red_knee = maxf(red_knee, c.r)
		if parts[part[i]] == "trunk" and absf(p.x) < 0.03 and p.z > 0.0 and absf(p.y - 0.05) < 0.03:
			red_belly = maxf(red_belly, c.r)
		if parts[part[i]] == "head" and absf(p.x) > 0.07:
			thin_ear = maxf(thin_ear, c.g)
		if parts[part[i]] == "trunk":
			thin_trunk = maxf(thin_trunk, c.g)
		if c.a > 0.5:
			nails += 1
		if c.b > 0.5:
			areola += 1
		if uv2[i].y > 0.3:
			creases += 1
	_check(ok_range, "cavity in [-1, 1], masks in [0, 1]")
	_check(red_knee > 0.3 and red_belly < 0.1, "redness on the kneecap (%.2f), not on the belly (%.2f)" % [red_knee, red_belly])
	_check(thin_ear > 0.8 and thin_trunk < 0.01, "ears are thin (light passes), the trunk is not")
	_check(nails > 20 and nails < 1500, "nails: %d vertices" % nails)
	_check(areola > 10, "areola from the nipple masks: %d vertices" % areola)
	_check(creases > 100 and creases < nb / 5, "creases: %d vertices" % creases)

	# eye anchors: a lid ring per eye, a forehead group, the eye centre behind the lid ring
	var ea: Dictionary = sk["eyes"]
	for s in ["l", "r"]:
		var ring: PackedInt32Array = ea[s]["ring"]
		_check(ring.size() >= 12, "eye %s: lid ring of %d vertices" % [s, ring.size()])
	_check((ea["fore"] as PackedInt32Array).size() >= 6, "forehead anchor")

	# a posed body: the eyes sit in their sockets and turn with the head
	var meas: Dictionary = (BodyGen.PRESETS["average"] as Dictionary).duplicate()
	var rest := BodyGen.generate(M, meas, null)
	var stand := BodyGen.stand(M, rest)
	var v := _v3(stand["pos"])
	var body := BodyView.new()
	root.add_child(body)
	body.use_custom(v, M["tris"])
	body.set_skin_data(sk)
	var eyes: Node3D = body.eyes
	_check(eyes != null and eyes.get_child_count() == 2 and eyes.visible, "two eyeballs")
	var el := (eyes.get_node("Eye_l") as Node3D).transform
	var er := (eyes.get_node("Eye_r") as Node3D).transform
	var lid_l := SkinMap.centroid(v, ea["l"]["ring"])
	var lid_r := SkinMap.centroid(v, ea["r"]["ring"])
	_check(el.origin.x > er.origin.x, "left eye on the body's left (+x)")
	var depth_l := (lid_l - el.origin).dot(el.basis.z.normalized())
	_check(depth_l > 0.004 and depth_l < 0.014, "eyeball centre %.1f mm behind the lids" % (depth_l * 1000.0))
	var r := el.basis.x.length()
	_check(r > 0.009 and r < 0.013, "eyeball radius %.1f mm" % (r * 1000.0))
	_check(el.basis.z.normalized().z > 0.95, "the eyes look ahead (gaze z %.3f)" % el.basis.z.normalized().z)
	# turn the whole body: the eyes follow
	var turn := Basis(Vector3.UP, 0.6)
	var v2 := PackedVector3Array()
	v2.resize(v.size())
	for i in v.size():
		v2[i] = turn * v[i]
	body.show_pose(v2)
	var el2 := (eyes.get_node("Eye_l") as Node3D).transform
	_check(el2.origin.distance_to(turn * el.origin) < 0.0005, "a turned head carries the eyes (%.2f mm)" % (el2.origin.distance_to(turn * el.origin) * 1000.0))
	_check(el2.basis.z.normalized().distance_to(turn * el.basis.z.normalized()) < 0.01, "and turns the gaze")
	# the body's mesh carries the skin attributes
	var arr := (body.mesh as ArrayMesh).surface_get_arrays(0)
	var dr: Dictionary = sk["drawn"]
	var nd: int = (dr["src"] as PackedInt32Array).size()
	_check(bool(dr["textured"]) and nd > nb and nd < nb + 2000, "MakeHuman UVs: %d drawn vertices for %d body vertices" % [nd, nb])
	_check((arr[Mesh.ARRAY_TEX_UV] as PackedVector2Array).size() == nd and (arr[Mesh.ARRAY_COLOR] as PackedColorArray).size() == nd
		and (arr[Mesh.ARRAY_CUSTOM0] as PackedFloat32Array).size() == 4 * nd, "body mesh has UV, rest position (CUSTOM0) and colour")
	# drawn vertices sit on their body vertex, and seams do not break the normals
	var dv: PackedVector3Array = arr[Mesh.ARRAY_VERTEX]
	var dn: PackedVector3Array = arr[Mesh.ARRAY_NORMAL]
	var srcs: PackedInt32Array = dr["src"]
	var seam_ok := true
	var nrm_of := {}
	for i in nd:
		if dv[i] != v2[srcs[i]]:
			seam_ok = false
		if nrm_of.has(srcs[i]) and (nrm_of[srcs[i]] as Vector3).distance_to(dn[i]) > 1e-6:
			seam_ok = false
		nrm_of[srcs[i]] = dn[i]
	_check(seam_ok, "split vertices share position and normal")
	var uvs: PackedVector2Array = dr["uv"]
	var uv_in := true
	for t in uvs:
		if t.x < 0.0 or t.x > 1.0 or t.y < 0.0 or t.y > 1.0:
			uv_in = false
	_check(uv_in, "UVs inside the texture")
	# a body with another vertex count draws without the attributes and hides the eyes
	body.use_custom(PackedVector3Array([Vector3.ZERO, Vector3.RIGHT, Vector3.UP]), PackedInt32Array([0, 1, 2]))
	_check(not eyes.visible, "eyes hidden on a body that is not the mannequin")

	# looks
	var m := Look.body_material()
	for k in Look.SKIN_TONES.keys():
		Look.set_skin_tone(m, k)
	Look.set_hair(m, "none")
	_check(is_zero_approx(float(m.get_shader_parameter("hair"))), "no hair")
	Look.set_hair(m, "black")
	_check(is_equal_approx(float(m.get_shader_parameter("hair")), 1.0), "hair on")
	_check(Eyes.IRIS.has(Look.EYE_DEFAULT) and Look.SKIN_TONES.has(Look.SKIN_DEFAULT) and Look.HAIR_COLORS.has(Look.HAIR_DEFAULT), "defaults exist")

	# the studio: every lamp aimed at the subject and shadowed from where it stands (the fill is a bounce card), a fixture
	# per lamp, a sky with every lamp in it, a room round a platform whose top is the solver's floor
	var stage := Node3D.new()
	root.add_child(stage)
	var st: Dictionary = Studio.build(stage)
	var lights: Dictionary = st["lights"]
	_check(lights.size() == Studio.LAMPS.size() and st["key"] == lights["key"], "%d lamps, key first" % lights.size())
	var aimed := true
	var shadowed := 0
	var away := true
	for lamp in Studio.LAMPS:
		var l: SpotLight3D = lights[lamp["id"]]
		var to: Vector3 = (lamp["aim"] as Vector3) - l.position
		if (-l.transform.basis.z).normalized().dot(to.normalized()) < 0.999:
			aimed = false
		if l.shadow_enabled:
			shadowed += 1
			# the shadow runs along the lamp's axis: on the floor it points from the lamp across the subject
			var ax := -l.transform.basis.z
			if Vector2(l.position.x, l.position.z).dot(Vector2(ax.x, ax.z)) >= 0.0:
				away = false
	_check(aimed, "every lamp points at its aim")
	_check(shadowed == Studio.LAMPS.size() - 1 and not (lights["fill"] as Light3D).shadow_enabled, "every lamp casts a shadow except the fill's bounce card")
	_check(away, "every shadow falls away from its lamp, across the subject")
	_check((st["fixtures"] as Array).size() == Studio.LAMPS.size(), "a visible fixture per lamp")
	var sky_m := (st["sky"] as Sky).sky_material as ShaderMaterial
	_check(int(sky_m.get_shader_parameter("lamp_n")) == Studio.LAMPS.size(), "the sky holds every lamp")
	var sroot: Node3D = st["root"]
	for nm in ["Room", "Platform", "Furniture"]:
		_check(sroot.has_node(nm), "the studio has its " + nm)
	var plat_top: MeshInstance3D = sroot.get_node("Platform").get_child(1)     # the felt top of the platform
	_check(absf(plat_top.get_aabb().end.y) < 1.0e-4 and absf(plat_top.get_aabb().size.x - Studio.PODIUM.x) < 1.0e-3, "the platform's top is at y = 0 (the solver's floor)")
	_check(is_equal_approx(Studio.FLOOR_Y, -Studio.PODIUM_H) and Studio.PODIUM_H > 0.14 and Studio.PODIUM_H < 0.21, "the platform stands 15-20 cm above the floor")
	_check((st["practicals"] as Array).size() >= 6, "warm pendants and cool window light: %d practical lights" % (st["practicals"] as Array).size())
	var fit: Dictionary = Studio.fit_camera(Vector3(0.0, 1.0, 6.8), Vector3(0.0, 0.86, 0.0), 18.0)
	_check((fit["pos"] as Vector3).z < Studio.ROOM_Z and float(fit["fov"]) > 18.0, "a camera outside the room is pulled in and the lens widened")
	var fit2: Dictionary = Studio.fit_camera(Vector3(0.0, 1.0, 3.0), Vector3(0.0, 0.86, 0.0), 18.0)
	_check((fit2["pos"] as Vector3).is_equal_approx(Vector3(0.0, 1.0, 3.0)) and is_equal_approx(float(fit2["fov"]), 18.0), "a camera inside the room is left alone")
	_check(absf(Look.CAM_FOV - rad_to_deg(2.0 * atan(12.0 / 50.0))) < 0.1, "the camera is a 50 mm lens")
	# the atelier: lived-in props, the mirror with three planar-reflection panels, camera views that all fit the room
	for nm in ["Furniture"]:
		var fur: Node3D = sroot.get_node(nm)
		_check(fur.get_child_count() >= 18, "the atelier holds %d props" % fur.get_child_count())
	var glass := sroot.find_children("Glass*", "MeshInstance3D", true, false)
	_check(glass.size() == 3, "the fitting mirror has three glass panels")
	var CamViews = load("res://cam_views.gd")
	var ids := []
	for vw in CamViews.VIEWS:
		ids.append(vw["id"])
		var fo: Dictionary = CamViews.focus(String(vw["focus"]), 1.68, 1.6, float(CamViews.fov_of(vw, Look.CAM_FOV)), 1.0)
		var dir := Vector3(sin(float(vw["yaw"])), 0.0, cos(float(vw["yaw"])))
		_check(float(fo["d"]) > 0.3 and float(fo["d"]) < 9.0 and (fo["p"] as Vector3).y > 0.0, "view %s: focus %s at %.1f m" % [vw["id"], vw["focus"], float(fo["d"])])
	_check(ids.has("fit") and ids.has("back") and ids.has("mirror") and ids.has("atelier"), "views include fit, back, mirror, atelier")
	# fitting a 1.8 m figure: the 50 mm lens must stand far enough back to see it whole in a landscape pane and in a tall one
	var d_land: float = CamViews.fit(1.8, 1.0, 1.8, Look.CAM_FOV)
	var d_tall: float = CamViews.fit(1.8, 1.0, 0.5, Look.CAM_FOV)
	_check(d_land > 3.5 and d_tall > d_land, "the fit distance follows the pane: %.1f m wide, %.1f m narrow" % [d_land, d_tall])
	for lv in Look.QUALITY_LEVELS:
		_check((Look.QUALITY[lv] as Dictionary).has_all(["gi", "ssil", "ssr", "dof", "probe"]), "quality %s sets the room's effects" % lv)

	body.queue_free()
	stage.queue_free()
	print("look_check: %d failed" % _fails)
	quit(1 if _fails > 0 else 0)


static func _v3(p: PackedFloat32Array) -> PackedVector3Array:
	var out := PackedVector3Array()
	out.resize(p.size() / 3)
	for i in out.size():
		out[i] = Vector3(p[3 * i], p[3 * i + 1], p[3 * i + 2])
	return out
