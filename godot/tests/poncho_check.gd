# Poncho drape check (step 2 of the cutting table work): the built-in Poncho (a 140 cm square with a 24 cm neck hole, no
# seams) is simulated on the average mannequin the way the editor builds a garment (Table.valid_pieces, Outfit.merge,
# PlanLoader.build with the editor's gravity ramp), draped, and measured. Checked: the piece is not dropped, the neck
# hole is at neck and shoulder height and round the neck, the hem is off the floor, and the centre of mass is high.
# Not checked: the upper half of the square, which lies over the head like a hood (docs/GODOT_EDITOR.md, known
# limitation). Prints PONCHO lines and exits 0 when every check passes, 1 otherwise. Run headless (needs libfailoring, see docs/NATIVE_NOTES.md):
#   godot --headless --path godot res://tests/poncho_check.tscn
# For a picture, run the editor with FL_AUTO=average,poncho,<frames> under a virtual display (docs/GODOT_EDITOR.md).
extends Node

const BodyGen = preload("res://body_gen.gd")
const PlanLoader = preload("res://plan_loader.gd")
const Builtin = preload("res://builtin_plans.gd")
const Table = preload("res://table.gd")
const Outfit = preload("res://outfit.gd")

const QUALITY := {"edge": 0.040, "sub": 2, "newton": 8, "frames": 48}   # the editor's Standard quality
const SETTLE_FRAMES := 240   # after the gravity ramp: the poncho settles on the body
const RAMP := 20
const SEAM_STIFFNESS := 20000.0

var fails := 0


func _check(ok: bool, what: String) -> void:
	print("PONCHO %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


static func _v3(p: PackedFloat32Array) -> PackedVector3Array:
	var out := PackedVector3Array()
	out.resize(p.size() / 3)
	for i in p.size() / 3:
		out[i] = Vector3(p[3 * i], p[3 * i + 1], p[3 * i + 2])
	return out


func _ready() -> void:
	var M: Dictionary = BodyGen.load_data(PlanLoader.load_json("res://assets/body_data.json"))
	var meas: Dictionary = (BodyGen.PRESETS["average"] as Dictionary).duplicate()
	var rest = BodyGen.generate(M, meas, null)
	var stand: Dictionary = BodyGen.stand(M, rest)
	var body_pts := _v3(stand["pos"])
	var lm: Dictionary = rest.get("landmarks", {})
	for k in ["cervicale", "jugular", "acromion.L", "bust.L", "waist"]:
		if lm.has(k):
			print("PONCHO landmark %s y %.3f m" % [k, float((lm[k] as Array)[1])])
	var top := -1e9
	var floor_low := 1e9
	for v in body_pts:
		top = maxf(top, v.y)
	print("PONCHO body height %.3f m" % top)

	var plan: Dictionary = PlanLoader.normalize_plan(Builtin.all()["poncho"])
	if OS.has_environment("FL_PONCHO_Y"):  # experiments: the wrap height (m) of the poncho
		((plan["pieces"][0] as Dictionary)["wrap"] as Dictionary)["y"] = float(OS.get_environment("FL_PONCHO_Y"))
	var valid := Table.valid_pieces(plan["pieces"])
	_check((valid["bad"] as Dictionary).is_empty(), "the poncho is valid for the simulator (no piece dropped)")
	var merged := Outfit.merge([{"uid": 1, "fabric": String(plan["fabric"]), "pieces": valid["pieces"], "seams": plan["seams"]}])
	var built: Dictionary = PlanLoader.build({"pieces": merged["pieces"], "seams": merged["seams"]}, stand["sections"], stand["arms"],
		body_pts, M["tris"], String(plan["fabric"]), QUALITY, SEAM_STIFFNESS, RAMP, false)
	_check(not built.is_empty(), "the poncho builds into a simulation")
	if built.is_empty():
		_finish()
		return
	var sim: FailoringSim = built["sim"]
	for f in range(1, SETTLE_FRAMES + 1):
		if f <= RAMP + 1:
			sim.set_param("gravity_y", -9.81 * minf(1.0, pow(float(f) / float(RAMP), 2.0)))
		sim.step(1)
	var pos: PackedVector3Array = sim.get_positions()
	var lo := 1e9
	var hi := -1e9
	var sum := Vector3.ZERO
	for c in pos:
		lo = minf(lo, c.y)
		hi = maxf(hi, c.y)
		sum += c
	var centroid := sum / float(pos.size())
	# the neck ring: the cloth vertices on the hole boundary (the poncho's panel is local vertices first..first+n)
	var first: int = built["first"].values()[0]  # the merged id carries the garment prefix
	var uv: PackedVector2Array = sim.get_panel_vertices_2d(0)
	var ring_y := 0.0
	var ring_r := 0.0
	var n_ring := 0
	for i in uv.size():
		var d := uv[i].distance_to(Vector2(0.7, 0.7))
		if absf(d - 0.12) < 0.004:
			var c: Vector3 = pos[first + i]
			ring_y += c.y
			ring_r += Vector2(c.x, c.z).length()
			n_ring += 1
	ring_y = ring_y / maxf(1.0, float(n_ring))
	ring_r = ring_r / maxf(1.0, float(n_ring))
	print("PONCHO drape: cloth y %.3f .. %.3f m, centroid y %.3f, neck ring %d vertices at y %.3f m, radius %.3f m" % [lo, hi, centroid.y, n_ring, ring_y, ring_r])
	print("PONCHO solver: ke %.4f J, warnings %d" % [sim.get_kinetic_energy() if sim.has_method("get_kinetic_energy") else -1.0, sim.get_warnings()])
	_check(n_ring > 10, "the neck hole has cloth around it (%d vertices on the ring)" % n_ring)
	_check(lo > 0.4, "the lowest point of the poncho is above 0.4 m, not on the floor (%.3f m)" % lo)
	_check(centroid.y > 1.0, "the poncho's centre of mass stays at shoulder height or above (%.3f m)" % centroid.y)
	_check(ring_r < 0.25, "the neck hole stays round the neck, not down the arms (radius %.3f m)" % ring_r)
	var acro := float((lm.get("acromion.L", [0.0, 1.4, 0.0]) as Array)[1])
	var jug := float((lm.get("jugular", [0.0, 1.35, 0.0]) as Array)[1])
	_check(ring_y > jug - 0.1 and ring_y < acro + 0.15, "the neck hole sits at neck and shoulder height (%.3f m; jugular %.3f, acromion %.3f)" % [ring_y, jug, acro])
	_finish()


func _finish() -> void:
	print("PONCHO %s" % ("PASSED" if fails == 0 else "FAILED with %d failure(s)" % fails))
	get_tree().quit(0 if fails == 0 else 1)
