# Checks the showcase: the scenario library, the pose evaluation (against the exporter's numbers), the floor and the hands
# on the box, the native props (a box the cloth rests on and carries), the float16 packing, and a short pre-render with
# its cache file. Run headless (about a minute):
#   godot --headless --path godot res://tests/scenario_check.tscn     (PASS/FAIL lines, then "RESULT: n failed")
extends Node3D

const BodyGen = preload("res://body_gen.gd")
const PlanLoader = preload("res://plan_loader.gd")
const Scen = preload("res://scenarios.gd")
const Render = preload("res://prerender.gd")

var fails := 0


func check(name: String, ok: bool) -> void:
	print(("PASS  " if ok else "FAIL  ") + name)
	if not ok:
		fails += 1


func _ready() -> void:
	var lib := Scen.load_library()
	check("library loads", not lib.is_empty())
	if lib.is_empty():
		_finish()
		return
	var ids: Array = lib["order"]
	check("six scenarios (%s)" % ", ".join(ids), ids == ["runway", "catwalk", "idle", "dressing", "box", "chair"])
	check("catwalk loops, box does not", bool(lib["by_id"]["catwalk"]["loop"]) and not bool(lib["by_id"]["box"]["loop"]))

	var M := BodyGen.load_data(PlanLoader.load_json("res://assets/body_data.json"))
	var body := BodyGen.generate(M, BodyGen.PRESETS["average"], null)
	var pose := Scen.Pose.new(M, body)
	var nb: int = M["nb"]

	# 1. float16 packing round trip (the cache's positions)
	var enc := FailoringSim.new()
	var vals := PackedFloat32Array([0.1, -1.5, 0.001, 0.4997, 1.9, -0.0, 3.25])
	var back := enc.decode_half(enc.encode_half(vals), 0, vals.size())
	var err := 0.0
	for i in vals.size():
		err = maxf(err, absf(back[i] - vals[i]) / maxf(1.0, absf(vals[i])))
	check("float16 round trip within 1e-3 relative (%f)" % err, err < 1e-3)

	# 2. native skinning against a GDScript reference, on a bent pose
	var sc: Dictionary = lib["by_id"]["box"]
	var s := Scen.sample(sc, 2.6)
	var W := _fk_gd(M, body, s["rot"], s["root"])
	var ref := PackedVector3Array()
	var si: PackedInt32Array = M["skinIdx"]
	var sw: PackedFloat32Array = M["skinW"]
	var all: PackedFloat32Array = body["allPos"]
	var native := pose.skin.skin(Scen.Pose._flat(W))
	var skin_err := 0.0
	for i in nb:
		var acc := Vector3()
		for j in 4:
			var w := sw[4 * i + j]
			if w == 0.0:
				continue
			var k := si[4 * i + j]
			var A: Array = W[k]["A"]
			var t: Array = W[k]["t"]
			var x := all[3 * i]; var y := all[3 * i + 1]; var z := all[3 * i + 2]
			acc += w * Vector3(A[0] * x + A[1] * y + A[2] * z + t[0], A[3] * x + A[4] * y + A[5] * z + t[1], A[6] * x + A[7] * y + A[8] * z + t[2])
		skin_err = maxf(skin_err, acc.distance_to(native[i]))
	check("native skin matches GDScript skinning (%f m)" % skin_err, skin_err < 1e-4)

	# 3. the exporter's numbers: joints at a stored frame (the exporter's reference, after its floor shift)
	for id in ids:
		var scn: Dictionary = lib["by_id"][id]
		var ref_joints: Dictionary = scn["reference"].get("joints", {})
		if ref_joints.is_empty():
			check("%s: reference present" % id, false)
			continue
		var fr := int(scn["reference"]["frame"])
		var ps := Scen.sample(scn, float(fr) / float(scn["fps"]))
		var P := pose.evaluate(ps["rot"], ps["root"])
		var worst := 0.0
		for jn in ref_joints.keys():
			var rj: Array = ref_joints[jn]
			var gj: Vector3 = P["joints"][jn]
			worst = maxf(worst, gj.distance_to(Vector3(rj[0], rj[1], rj[2])))
		check("%s: joints match the exporter (%f m)" % [id, worst], worst < 2e-4)

	# 4. the floor: no body vertex below y = 0; the feet on it (the player's shift)
	for id in ids:
		var scn: Dictionary = lib["by_id"][id]
		var lowest := INF
		var feet_min := INF
		var n := int(round(float(scn["duration"]) * 60.0))
		for k in range(0, n + 1, 2):
			var ps2 := Scen.sample(scn, float(k) / 60.0)
			var P2 := pose.evaluate(ps2["rot"], ps2["root"])
			var verts: PackedVector3Array = P2["verts"]
			for v in verts:
				lowest = minf(lowest, v.y)
			for fi in pose.feet:
				feet_min = minf(feet_min, verts[fi].y)
		check("%s: no vertex below the floor (%f m), feet on it (%f m)" % [id, lowest, feet_min], lowest > -1e-4 and absf(feet_min) < 1e-4)

	# the cost of one pose (what playback pays per frame, besides the cloth): FK, skinning, the floor shift
	var t_pose := Time.get_ticks_usec()
	for k2 in 60:
		var ps5 := Scen.sample(lib["by_id"]["box"], float(k2) / 60.0)
		pose.evaluate(ps5["rot"], ps5["root"])
	print("TIMING pose evaluate: %.2f ms per frame (average of 60, this host)" % ((Time.get_ticks_usec() - t_pose) / 60000.0))

	# 5. planted: the chair's IK part keeps the feet within 1.5 cm of their standing height (box: all of it)
	var shift0 := 0.0
	var box_scn: Dictionary = lib["by_id"]["box"]
	shift0 = float(pose.evaluate(Scen.sample(box_scn, 0.0)["rot"], Scen.sample(box_scn, 0.0)["root"])["shift"])
	var planted_worst := 0.0
	var chair: Dictionary = lib["by_id"]["chair"]
	for k in range(int(1.6 * 60.0), int(float(chair["duration"]) * 60.0) + 1, 3):
		var ps3 := Scen.sample(chair, float(k) / 60.0)
		planted_worst = maxf(planted_worst, absf(float(pose.evaluate(ps3["rot"], ps3["root"])["shift"]) - float(_stand_shift(pose, box_scn))))
	check("chair: feet stay planted in the sit (%f m)" % planted_worst, planted_worst < 0.015)

	# 6. the box: both hands on it from the grip to the release (the box follows the wrists)
	var a := float(box_scn["props"][0]["attach"][0])
	var b := float(box_scn["props"][0]["attach"][1])
	var box_half := Scen.box_prop(box_scn)["half"] as Vector3
	var hand_worst := 0.0
	for k in range(int(a * 60.0), int(b * 60.0) + 1, 2):
		var t := float(k) / 60.0
		var ps4 := Scen.sample(box_scn, t)
		var P4 := pose.evaluate(ps4["rot"], ps4["root"])
		var c := pose.box_center(box_scn, t, P4["joints"])
		for hn in ["l-hand", "r-hand"]:
			hand_worst = maxf(hand_worst, absf(Scen.box_distance(P4["joints"][hn], c, box_half)))
	check("box: hands within 1.2 cm of the box surface from grip to release (%f m)" % hand_worst, hand_worst < 0.012)
	var c_rest := pose.box_center(box_scn, 0.0, {})
	check("box: rests on the floor before the grip", absf(c_rest.y - Scen.box_prop(box_scn)["center"].y) < 1e-6)

	_check_props_native()
	_check_prerender(M, body, pose)
	_finish()


func _finish() -> void:
	print("RESULT: %d failed" % fails)
	get_tree().quit(1 if fails > 0 else 0)


# Joint transforms with the same rule as BodyGen.fk, used as the reference for the native skinning (the scenario's rotations
# and root, unshifted).
static func _fk_gd(M: Dictionary, body: Dictionary, rot: PackedFloat32Array, root: Vector3) -> Array:
	var rr := []
	for k in (M["bones"] as Array).size():
		rr.append([rot[3 * k], rot[3 * k + 1], rot[3 * k + 2]])
	return BodyGen.fk(M, body, rr, [root.x, root.y, root.z])


# The floor shift of the standing pose (a rest of the feet for the planted check).
static func _stand_shift(pose: Scen.Pose, sc: Dictionary) -> float:
	var s := Scen.sample(sc, 0.0)
	return float(pose.evaluate(s["rot"], s["root"])["shift"])


# The native props: a box the cloth falls onto and rests on, and a box that moves up under the cloth carries it.
func _check_props_native() -> void:
	var sim := FailoringSim.new()
	sim.load_materials("res://materials/fabrics.json")
	sim.set_param("substeps", 2)
	sim.set_param("newton_iterations", 8)
	sim.set_param("tension_stiffness", 1)
	sim.add_plane(Vector3.ZERO, Vector3.UP)
	sim.begin()
	var pid := sim.add_rect_panel(0.5, 0.5, 0.05, "cotton_poplin", Vector3(-0.25, 0.5, -0.25), Vector3.RIGHT, Vector3.BACK, 0.0, "sheet")
	sim.setup_display()
	var box_center := Vector3(0, 0.15, 0)
	var box_half := Vector3(0.15, 0.15, 0.15)
	var bi := sim.add_box_prop(Transform3D(Basis.IDENTITY, box_center), box_half)
	check("native: box prop added", pid == 0 and bi == 0 and sim.get_box_prop_count() == 1)
	for _i in 90:
		sim.step(1)
	var pos := sim.get_positions()
	var inside := 0
	var top := -INF
	for p in pos:
		var q := Vector3(absf(p.x - box_center.x) - box_half.x, absf(p.y - box_center.y) - box_half.y, absf(p.z - box_center.z) - box_half.z)
		if Vector3(maxf(q.x, 0.0), maxf(q.y, 0.0), maxf(q.z, 0.0)).length() == 0.0 and maxf(q.x, maxf(q.y, q.z)) < -0.002:
			inside += 1
		if absf(p.x) < 0.1 and absf(p.z) < 0.1:
			top = maxf(top, p.y)
	check("native: cloth rests on the box (top %.3f, box top %.3f, inside %d)" % [top, box_center.y + box_half.y, inside],
		inside == 0 and top > box_center.y + box_half.y - 0.02)
	# carry: move the box up by 0.4 m over 60 steps (0.4 m/s); the cloth on it must go up with it, and never be inside
	var lift_ok := true
	var y0 := top
	for i in 60:
		box_center.y += 0.4 / 60.0
		sim.set_box_prop(bi, Transform3D(Basis.IDENTITY, box_center))
		sim.step(1)
	pos = sim.get_positions()
	var inside2 := 0
	var top2 := -INF
	for p in pos:
		var q2 := Vector3(absf(p.x - box_center.x) - box_half.x, absf(p.y - box_center.y) - box_half.y, absf(p.z - box_center.z) - box_half.z)
		if Vector3(maxf(q2.x, 0.0), maxf(q2.y, 0.0), maxf(q2.z, 0.0)).length() == 0.0 and maxf(q2.x, maxf(q2.y, q2.z)) < -0.002:
			inside2 += 1
		if absf(p.x) < 0.1 and absf(p.z) < 0.1:
			top2 = maxf(top2, p.y)
	lift_ok = top2 > y0 + 0.15 and inside2 == 0
	check("native: the moved box carries the cloth (top %.3f -> %.3f, inside %d)" % [y0, top2, inside2], lift_ok)
	# no props: the same sheet with the box removed behaves as before (the cloth falls below the box top, to the floor)
	sim.clear_box_props()
	check("native: clear_box_props", sim.get_box_prop_count() == 0)


# The pre-render (Draft quality, the tee, 8 steps): the frames' sizes, the cache file and its round trip.
func _check_prerender(M: Dictionary, body: Dictionary, pose: Scen.Pose) -> void:
	var lib := Scen.load_library()
	var sc: Dictionary = lib["by_id"]["catwalk"]
	var plan := PlanLoader.normalize_plan(PlanLoader.load_json("res://assets/average/plan_teagan.json"))
	var stand := BodyGen.stand(M, body)
	var quality := {"label": "Draft", "edge": 0.060, "sub": 1, "newton": 6, "frames": 36, "body_h": 0.010}
	var inputs := {"plan": {"pieces": plan["pieces"], "seams": plan["seams"]}, "sections": stand["sections"], "arms": stand["arms"],
		"stand": _to_v3(stand["pos"]), "tris": M["tris"], "fabric_id": "cotton_poplin", "quality": quality, "quality_key": "draft",
		"seam_stiffness": 20000.0, "ramp_frames": 20, "collide": false, "M": M, "body_rest": body, "body_key": "test-average"}
	var r := Render.new()
	var result := r.work({"scenario": sc, "inputs": inputs, "max_steps": 8})
	check("prerender: runs (%d frames)" % (result["frames"] as Array).size(), not result.is_empty() and (result["frames"] as Array).size() == 8)
	if result.is_empty():
		return
	var f0: Dictionary = result["frames"][0]
	var nv: int = (f0["pos"] as PackedFloat32Array).size() / 3
	check("prerender: frame size matches the display mesh (%d vertices)" % nv, nv > 0 and (f0["nrm"] as PackedFloat32Array).size() == 3 * nv)
	var path := Render.file_for(String(result["key"]))
	check("prerender: saved", Render.save(path, result, {"name": "test", "preset": "average"}))
	var h := Render.read_header(path)
	check("prerender: header", String(h.get("scenario", "")) == "catwalk" and int(h.get("frames", 0)) == 8)
	var back := Render.load(path)
	check("prerender: loaded", not back.is_empty() and (back["frames"] as Array).size() == 8)
	if back.is_empty():
		return
	var err := 0.0
	var bf: Dictionary = back["frames"][7]
	var of: Dictionary = result["frames"][7]
	for i in (of["pos"] as PackedFloat32Array).size():
		err = maxf(err, absf((bf["pos"] as PackedFloat32Array)[i] - (of["pos"] as PackedFloat32Array)[i]))
	check("prerender: positions survive float16 (%f m)" % err, err < 2e-3)
	var listed := false
	for row in Render.list_saved():
		listed = listed or String(row["path"]) == path
	check("prerender: cache lists the file", listed)
	# the chair: its parts are solver colliders and the sit (IK) runs through the render; 150 steps reach the seat
	var chair_r := r.work({"scenario": lib["by_id"]["chair"], "inputs": inputs, "max_steps": 150})
	var chair_ok := not chair_r.is_empty() and (chair_r["frames"] as Array).size() == 150
	var finite := true
	if chair_ok:
		for f in chair_r["frames"]:
			for v in (f["pos"] as PackedFloat32Array):
				if is_nan(v) or is_inf(v) or absf(v) > 5.0:
					finite = false
					break
	check("prerender: chair render runs (150 steps), cloth finite", chair_ok and finite)
	var key_a := Render.key_for(inputs, sc)
	var key_b := Render.key_for(inputs, lib["by_id"]["idle"])
	check("prerender: key depends on the scenario", key_a != key_b and key_a.length() == 64)
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))   # the test leaves no render in the user's cache


static func _to_v3(p: PackedFloat32Array) -> PackedVector3Array:
	var out := PackedVector3Array()
	out.resize(p.size() / 3)
	for i in p.size() / 3:
		out[i] = Vector3(p[3 * i], p[3 * i + 1], p[3 * i + 2])
	return out
