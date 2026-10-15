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

	# the studio: one shadowed key, every lamp aimed at the subject, a fixture per lamp, a sky with every lamp in it
	var stage := Node3D.new()
	root.add_child(stage)
	var st: Dictionary = Studio.build(stage)
	var lights: Dictionary = st["lights"]
	_check(lights.size() == Studio.LAMPS.size() and st["key"] == lights["key"], "%d lamps, key first" % lights.size())
	var aimed := true
	var shadowed := 0
	for lamp in Studio.LAMPS:
		var l: SpotLight3D = lights[lamp["id"]]
		var to: Vector3 = (lamp["aim"] as Vector3) - l.position
		if (-l.transform.basis.z).normalized().dot(to.normalized()) < 0.999:
			aimed = false
		if l.shadow_enabled:
			shadowed += 1
	_check(aimed, "every lamp points at its aim")
	_check(shadowed == 1 and (lights["key"] as Light3D).shadow_enabled, "only the key casts shadows")
	_check((st["fixtures"] as Array).size() == Studio.LAMPS.size(), "a visible fixture per lamp")
	var sky_m := (st["sky"] as Sky).sky_material as ShaderMaterial
	_check(int(sky_m.get_shader_parameter("lamp_n")) == Studio.LAMPS.size(), "the sky holds every lamp")
	var cove: MeshInstance3D = (st["root"] as Node3D).get_node("Cove")
	var aabb := cove.get_aabb()
	_check(aabb.size.x > 2.0 * Studio.COVE_R - 0.1 and aabb.position.y < 0.0 and aabb.end.y > Studio.COVE_H - 0.1, "the cove surrounds the stage")
	# the floor of the cove faces up (inside of the studio)
	var arrc := (cove.mesh as ArrayMesh).surface_get_arrays(0)
	_check((arrc[Mesh.ARRAY_NORMAL] as PackedVector3Array)[0].y > 0.99, "the cove's floor faces up")

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
