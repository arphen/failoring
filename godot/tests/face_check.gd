# Face likeness check (face.gd, BodyGen.face): a synthetic face (offsets on a few nose vertices, written to a scratch
# faces folder, never the user's own) changes exactly those vertices, scaled with the head; the body key tells the face
# apart; a missing or broken face leaves the mannequin's; and the hairline that hair.gd builds the cap from matches the
# skin shader's (spot values).
#   godot --headless --path godot res://tests/face_check.tscn      (PASS/FAIL lines, then "RESULT: n failed")
extends Node

const BodyGen = preload("res://body_gen.gd")
const PlanLoader = preload("res://plan_loader.gd")
const FaceLikeness = preload("res://face.gd")
const Hair = preload("res://hair.gd")

var fails := 0


func check(name: String, ok: bool) -> void:
	print(("PASS  " if ok else "FAIL  ") + name)
	if not ok:
		fails += 1


func _ready() -> void:
	var dir := OS.get_user_data_dir().path_join("face_check")
	OS.set_environment("FL_FACES_DIR", dir)
	DirAccess.make_dir_recursive_absolute(dir.path_join("t"))
	var M: Dictionary = BodyGen.load_data(PlanLoader.load_json("res://assets/body_data.json"))
	var base: PackedFloat32Array = M["base"]
	# the nose's vertices: within 1 cm of the tip (skin.gd NOSE_TIP), pushed 5 mm forward
	var idx := []
	var d := []
	for i in int(M["nb"]):
		if Vector3(base[3 * i], base[3 * i + 1], base[3 * i + 2]).distance_to(Vector3(0.0, 0.637, 0.1518)) < 0.01:
			idx.append(i)
			d.append_array([0.0, 0.0, 0.005])
	var f := FileAccess.open(dir.path_join("t/face_shape.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify({"idx": idx, "d": d, "ref": [idx[0], idx[idx.size() - 1]]}))
	f.close()
	check("available faces: %s" % str(FaceLikeness.available()), FaceLikeness.available() == ["t"])
	check("a face loads", FaceLikeness.use("t") and BodyGen.face.get("id", "") == "t")

	var meas: Dictionary = (BodyGen.PRESETS["average"] as Dictionary).duplicate()
	var plain := BodyGen.generate(M, meas, null)
	var with := meas.duplicate()
	with["face"] = "t"
	var faced := BodyGen.generate(M, with, null)
	var a: PackedFloat32Array = plain["pos"]
	var b: PackedFloat32Array = faced["pos"]
	var moved := 0
	var others := 0
	var dz := 0.0
	var in_idx := {}
	for i in idx:
		in_idx[int(i)] = true
	for i in a.size() / 3:
		var dv := Vector3(b[3 * i] - a[3 * i], b[3 * i + 1] - a[3 * i + 1], b[3 * i + 2] - a[3 * i + 2])
		if in_idx.has(i):
			moved += 1
			dz = maxf(dz, dv.z)
		elif dv.length() > 1e-7:
			others += 1
	check("the face moves its %d vertices (by %.1f mm on this head) and nothing else (%d others)" % [moved, dz * 1000.0, others],
		moved == idx.size() and dz > 0.003 and dz < 0.008 and others == 0)
	check("the body key tells the face apart", String(plain["key"]) != String(faced["key"]))

	check("no face: the mannequin's", FaceLikeness.use("") and BodyGen.face.is_empty())
	check("a missing face does not load", not FaceLikeness.use("nobody") and BodyGen.face.is_empty())
	f = FileAccess.open(dir.path_join("t/face_shape.json"), FileAccess.WRITE)
	f.store_string("{\"idx\": [1, 2], \"d\": [0.1]}")
	f.close()
	check("a broken face does not load", not FaceLikeness.use("t") and BodyGen.face.is_empty())
	var again := BodyGen.generate(M, with, null)
	check("a request for a face that is not loaded gets the mannequin's", again["pos"] == plain["pos"])

	# hair.gd's hairline = skin.gdshader's hair_mask: on the crown, on the forehead, on the cheek, behind the ear
	check("hairline: crown 1, forehead 0, cheek 0, back of the head 1",
		Hair.hair_mask(Vector3(0.0, 0.76, 0.0)) > 0.99 and Hair.hair_mask(Vector3(0.0, 0.66, 0.13)) < 0.01
		and Hair.hair_mask(Vector3(0.05, 0.62, 0.09)) < 0.01 and Hair.hair_mask(Vector3(0.0, 0.66, -0.07)) > 0.99)
	for fn in DirAccess.get_files_at(dir.path_join("t")):
		DirAccess.remove_absolute(dir.path_join("t").path_join(fn))
	DirAccess.remove_absolute(dir.path_join("t"))
	print("RESULT: %d failed" % fails)
	get_tree().quit(1 if fails > 0 else 0)
