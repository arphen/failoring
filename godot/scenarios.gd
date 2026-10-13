# Showcase scenarios: body motions and props (docs/SCENARIOS.md). Data: godot/assets/scenarios.json, written by
# tools/export_scenarios.js. A scenario is a list of per-bone rotations (bone order of the mannequin, FL.Body.fk) and a
# pelvis translation, sampled at the solver's 60 steps per second and interpolated. The pose is evaluated here, for any mannequin:
#   1. FK of the bones (BodyGen.fk, the same as the browser's FL.Body.fk)
#   2. linear blend skinning of the bind (A-pose) vertices (FailoringSim.skin, C++)
#   3. the floor shift: the feet's lowest vertex goes to y = 0, as FL.Gait.pose does (the shift is a root translation)
# Props: a box follows the midpoint of the two wrists while it is carried (an offset fixed when the grip starts), so it
# stays in the hands for any body; a chair is a set of static boxes.
class_name ShowcaseScenarios
extends RefCounted

const BodyGen = preload("res://body_gen.gd")
const DATA_PATH := "res://assets/scenarios.json"
const FORMAT := "failoring-scenarios"
const VERSION := 1
const SIM_FPS := 60.0   # the solver's steps per second: one step is one 1/60 s of the scenario


# Loads the library: {order: [ids], by_id: {id: scenario}} or {} on error. A scenario has id, name, duration (s),
# frames, fps, loop, props (dicts), rot (PackedFloat32Array per frame, 3 per bone), root (Vector3 per frame).
static func load_library(path: String = DATA_PATH) -> Dictionary:
	var text := FileAccess.get_file_as_string(path)
	if text.is_empty():
		push_error("scenarios: cannot read " + path)
		return {}
	var v = JSON.parse_string(text)
	if not (v is Dictionary) or String(v.get("format", "")) != FORMAT or int(v.get("version", 0)) != VERSION:
		push_error("scenarios: not a %s v%d file" % [FORMAT, VERSION])
		return {}
	var lib := {"order": [], "by_id": {}, "store_fps": float(v.get("store_fps", 60.0)), "reference": String(v.get("reference", ""))}
	for s in v["scenarios"]:
		var sc := {"id": String(s["id"]), "name": String(s["name"]), "duration": float(s["duration"]), "frames": int(s["frames"]),
			"fps": float(s.get("fps", v.get("store_fps", 60.0))), "loop": bool(s["loop"]), "props": s["props"], "checks": s.get("checks", {}),
			"reference": s.get("reference", {}), "rot": [], "root": []}
		for f in sc["frames"]:
			sc["rot"].append(PackedFloat32Array(s["rot"][f]))
			var r: Array = s["root"][f]
			sc["root"].append(Vector3(float(r[0]), float(r[1]), float(r[2])))
		lib["order"].append(sc["id"])
		lib["by_id"][sc["id"]] = sc
	return lib


# The scenario's rotations and pelvis translation at time t (seconds). Looping scenarios wrap, others clamp.
static func sample(sc: Dictionary, t: float) -> Dictionary:
	var n: int = sc["frames"]
	var f := t * float(sc["fps"])
	if bool(sc["loop"]):
		f = fposmod(f, float(n))
	else:
		f = clampf(f, 0.0, float(n - 1))
	var i0 := int(floor(f))
	var i1 := (i0 + 1) % n if bool(sc["loop"]) else mini(i0 + 1, n - 1)
	var u := f - float(i0)
	var a: PackedFloat32Array = sc["rot"][i0]
	var b: PackedFloat32Array = sc["rot"][i1]
	var rot := PackedFloat32Array()
	rot.resize(a.size())
	for k in a.size():
		rot[k] = a[k] + (b[k] - a[k]) * u
	var ra: Vector3 = sc["root"][i0]
	var rb: Vector3 = sc["root"][i1]
	return {"rot": rot, "root": ra.lerp(rb, u)}


# Evaluates poses of one mannequin. Holds the skinning context (the bind vertices and skin weights) and the feet.
class Pose:
	extends RefCounted

	var M: Dictionary
	var body: Dictionary
	var nb := 0
	var nbones := 0
	var skin := FailoringSim.new()
	var feet := PackedInt32Array()
	var bone_of := {}           # joint name -> bone index
	var _anchor := {}           # scenario id -> offset of a carried box from the wrists' midpoint
	var _hold := {}             # scenario id -> the box centre after the release

	func _init(m: Dictionary, b: Dictionary) -> void:
		M = m
		body = b
		nb = int(m["nb"])
		nbones = (m["bones"] as Array).size()
		var all: PackedFloat32Array = b["allPos"]
		var bind := PackedVector3Array()
		bind.resize(nb)
		for i in nb:
			bind[i] = Vector3(all[3 * i], all[3 * i + 1], all[3 * i + 2])
		skin.set_skin(bind, m["skinIdx"], m["skinW"])
		var part: PackedInt32Array = m["part"]
		var parts: Array = m["parts"]
		var fl := parts.find("foot.L")
		var fr := parts.find("foot.R")
		for i in nb:
			if part[i] == fl or part[i] == fr:
				feet.append(i)
		for k in nbones:
			bone_of[(m["bones"][k] as Dictionary)["joint"]] = k

	# Rotations (3 per bone) and the pelvis translation -> the body's vertices (nb, on the floor), the joints the
	# showcase uses (wrists, ankles, pelvis) and the floor shift. want_body false skips nothing: the vertices are needed
	# for the feet either way; the shift is applied to the wrists' joints directly.
	func evaluate(rot: PackedFloat32Array, root: Vector3) -> Dictionary:
		var rr: Array = []
		for k in nbones:
			rr.append([rot[3 * k], rot[3 * k + 1], rot[3 * k + 2]])
		var W: Array = BodyGen.fk(M, body, rr, [root.x, root.y, root.z])
		var verts := skin.skin(_flat(W))
		var fmin := INF
		for i in feet:
			fmin = minf(fmin, verts[i].y)
		var shift := -fmin
		var up := Vector3(0.0, shift, 0.0)
		for i in nb:
			verts[i] += up
		var joints := {}
		for name in ["l-hand", "r-hand", "l-ankle", "r-ankle", "pelvis", "head"]:
			var k: int = bone_of[name]
			var J: Array = body["joints"][name]
			var A: Array = W[k]["A"]
			var t: Array = W[k]["t"]
			joints[name] = Vector3(A[0] * J[0] + A[1] * J[1] + A[2] * J[2] + t[0], A[3] * J[0] + A[4] * J[1] + A[5] * J[2] + t[1] + shift,
				A[6] * J[0] + A[7] * J[1] + A[8] * J[2] + t[2])
		return {"verts": verts, "joints": joints, "shift": shift}

	static func _flat(W: Array) -> PackedFloat32Array:
		var out := PackedFloat32Array()
		out.resize(12 * W.size())
		for k in W.size():
			var A: Array = W[k]["A"]
			var t: Array = W[k]["t"]
			for j in 9:
				out[12 * k + j] = A[j]
			for j in 3:
				out[12 * k + 9 + j] = t[j]
		return out

	# The centre of a box prop at time t, given the joints of the pose at t (evaluate). Before the grip it rests where the
	# scenario puts it; from the grip to the release it moves with the wrists (the offset is taken at the grip, once);
	# after the release it stays where it was put down (the release time's centre).
	func box_center(sc: Dictionary, t: float, joints_at_t: Dictionary) -> Vector3:
		var box := ShowcaseScenarios.box_prop(sc)
		if box.is_empty():
			return Vector3.ZERO
		var c0: Vector3 = box["center"]
		var a: float = float(box["attach"][0])
		var b: float = float(box["attach"][1])
		if t < a:
			return c0
		if not _anchor.has(sc["id"]):
			var s := ShowcaseScenarios.sample(sc, a)
			var ja: Dictionary = evaluate(s["rot"], s["root"])["joints"]
			_anchor[sc["id"]] = c0 - (ja["l-hand"] + ja["r-hand"]) * 0.5
		if t > b:
			# carried until the release, then held where it was put down: the centre at the release, computed once
			if not _hold.has(sc["id"]):
				var sb := ShowcaseScenarios.sample(sc, b)
				var jb: Dictionary = evaluate(sb["rot"], sb["root"])["joints"]
				_hold[sc["id"]] = (jb["l-hand"] + jb["r-hand"]) * 0.5 + (_anchor[sc["id"]] as Vector3)
			return _hold[sc["id"]]
		return (joints_at_t["l-hand"] + joints_at_t["r-hand"]) * 0.5 + (_anchor[sc["id"]] as Vector3)


static func smooth_step(x: float) -> float:
	var u := clampf(x, 0.0, 1.0)
	return u * u * (3.0 - 2.0 * u)


static func box_prop(sc: Dictionary) -> Dictionary:
	for p in sc["props"]:
		if String(p["type"]) == "box":
			return {"half": _v3(p["half"]), "center": _v3(p["center"]), "attach": p["attach"]}
	return {}


static func chair_parts(sc: Dictionary) -> Array:
	for p in sc["props"]:
		if String(p["type"]) == "chair":
			var out := []
			for part in p["parts"]:
				out.append({"half": _v3(part["half"]), "center": _v3(part["center"])})
			return out
	return []


static func _v3(a: Array) -> Vector3:
	return Vector3(float(a[0]), float(a[1]), float(a[2]))


# The props of a scenario at time t as {name, transform, half} entries, for the solver and the view. Chair parts are
# static (same at every t).
static func prop_boxes(sc: Dictionary, pose: Pose, t: float, joints_at_t: Dictionary) -> Array:
	var out := []
	var box := box_prop(sc)
	if not box.is_empty():
		out.append({"name": "box", "half": box["half"], "transform": Transform3D(Basis.IDENTITY, pose.box_center(sc, t, joints_at_t))})
	for part in chair_parts(sc):
		out.append({"name": "chair", "half": part["half"], "transform": Transform3D(Basis.IDENTITY, part["center"])})
	return out


# Signed distance from p to an axis-aligned box (outside positive), the check's measure of hands on the box.
static func box_distance(p: Vector3, center: Vector3, half: Vector3) -> float:
	var q := Vector3(absf(p.x - center.x) - half.x, absf(p.y - center.y) - half.y, absf(p.z - center.z) - half.z)
	var o := Vector3(maxf(q.x, 0.0), maxf(q.y, 0.0), maxf(q.z, 0.0)).length()
	if o > 0.0:
		return o
	return maxf(q.x, maxf(q.y, q.z))
