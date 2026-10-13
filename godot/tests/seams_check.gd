# Partial seams check (step 4): the range model (seams.gd), its expansion for the simulator, and the drape test the spec
# asks for: a tube of a front and a back panel, sewn down both sides, with its top seam either sewn all the way or only
# half way. After the drape the open half of the top must stay open (a gap of centimetres between the two panels'
# edges), and a fully sewn top must close (millimetres). Runs on the average mannequin through the editor's build path.
# Run headless:
#   godot --headless --path godot res://tests/seams_check.tscn
# Prints SEAMCHECK lines and quits 0 when every check passes, 1 otherwise.
extends Node

const Seams = preload("res://seams.gd")
const BodyGen = preload("res://body_gen.gd")
const PlanLoader = preload("res://plan_loader.gd")
const Outfit = preload("res://outfit.gd")

const QUALITY := {"edge": 0.040, "sub": 2, "newton": 8, "frames": 48}
const SETTLE_FRAMES := 200
const RAMP := 20
const SEAM_STIFFNESS := 20000.0
const TUBE_W := 0.44   # m: each panel is 44 cm wide, round the torso
const TUBE_H := 0.40   # m: and 40 cm tall
const TUBE_Y := 0.95   # m: the lower edge at about hip height (the top is 40 cm above it)

var fails := 0


func _check(ok: bool, what: String) -> void:
	print("SEAMCHECK %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


static func _v3(p: PackedFloat32Array) -> PackedVector3Array:
	var out := PackedVector3Array()
	out.resize(p.size() / 3)
	for i in p.size() / 3:
		out[i] = Vector3(p[3 * i], p[3 * i + 1], p[3 * i + 2])
	return out


func _ready() -> void:
	_unit_checks()
	var M: Dictionary = BodyGen.load_data(PlanLoader.load_json("res://assets/body_data.json"))
	var rest = BodyGen.generate(M, (BodyGen.PRESETS["average"] as Dictionary).duplicate(), null)
	var stand: Dictionary = BodyGen.stand(M, rest)
	# full: the top sewn all the way (edge 2 of each panel); half: sewn from 0 to 0.5, so the open half is sub-edge 3
	var full := _drape(stand, M, 1.0)
	var half := _drape(stand, M, 0.5)
	print("SEAMCHECK nearest distance across the top: sewn all the way %.2f mm, open half of a half-sewn top %.1f mm" % [full * 1000.0, half * 1000.0])
	_check(full >= 0.0 and full < 0.005, "a fully sewn top closes: its two edges are within 5 mm (%.2f mm)" % (full * 1000.0))
	_check(half > 0.02, "a half sewn top stays open on the other half: its edges are more than 2 cm apart (%.1f mm)" % (half * 1000.0))
	print("SEAMCHECK %s" % ("PASSED" if fails == 0 else "FAILED with %d failure(s)" % fails))
	get_tree().quit(0 if fails == 0 else 1)


func _unit_checks() -> void:
	var sq := [[0.0, 0.0], [0.4, 0.0], [0.4, 0.4], [0.0, 0.4]]
	_check(Seams.make_side("p", 1, 0.0, 1.0) == ["p", 1], "a whole-edge range is stored as the old two-element side")
	var part := Seams.make_side("p", 1, 0.25, 0.75)
	_check(part.size() == 4 and _near(float(part[2]), 0.25) and _near(float(part[3]), 0.75), "a part of an edge is stored with its two fractions")
	_check(_near(Seams.range_length(sq, part), 0.2), "the length of a half range of a 40 cm edge is 20 cm")
	var mid := Seams.range_mid(sq, ["p", 1, 0.0, 0.5])
	_check(_near(mid.x, 0.4, 1e-6) and _near(mid.y, 0.1, 1e-6), "the middle of the first half of the right edge is (40, 10) cm")
	var seams := [{"a": ["p", 1, 0.0, 0.5], "b": ["q", 3], "reverse": true}]
	_check(Seams.overlaps(seams, "p", 1, 0.25, 0.9, -1), "a range overlapping another range on its edge is detected")
	_check(not Seams.overlaps(seams, "p", 1, 0.5, 1.0, -1), "ranges that only touch do not overlap")
	_check(not Seams.overlaps(seams, "p", 1, 0.25, 0.9, 0), "a seam does not overlap itself")
	_check(Seams.has_partial(seams) and not Seams.has_partial([{"a": ["p", 1], "b": ["q", 3], "reverse": true}]), "has_partial tells part seams from whole ones")
	# expansion: the right edge of p is split at 0.5, so p has 5 corners; the seam's side is its first sub-edge (edge 1),
	# the whole left edge of q (edge 3) is untouched but after the split of q's top it moves to edge 4
	var pieces := [{"id": "p", "outline": sq.duplicate(true)}, {"id": "q", "outline": sq.duplicate(true)}]
	var ex := Seams.expand(pieces, [{"a": ["p", 1, 0.0, 0.5], "b": ["q", 1, 0.5, 1.0], "reverse": false}])
	_check((ex["pieces"][0]["outline"] as Array).size() == 5, "a split at 0.5 adds one corner to the piece (5 corners)")
	_check((ex["pieces"][1]["outline"] as Array).size() == 5, "the other piece is split at its own range's start too")
	var es: Dictionary = ex["seams"][0]
	_check(String(es["a"][0]) == "p" and int(es["a"][1]) == 1 and es["a"].size() == 2, "side a becomes the first whole sub-edge of p's right edge")
	_check(String(es["b"][0]) == "q" and int(es["b"][1]) == 2, "side b becomes the second half of q's right edge (edge 2 after the split)")
	var full_ex := Seams.expand(pieces, [{"a": ["p", 1], "b": ["q", 3], "reverse": true}])
	_check((full_ex["pieces"][0]["outline"] as Array).size() == 4, "a whole-edge seam splits nothing")


static func _near(a: float, b: float, tol := 1e-4) -> bool:
	return absf(a - b) <= tol


# The tube: a front and a back panel, the sides sewn whole, the top sewn to top_fraction of its length.
func _tube(top_fraction: float) -> Dictionary:
	var sq := [[0.0, 0.0], [TUBE_W, 0.0], [TUBE_W, TUBE_H], [0.0, TUBE_H]]
	var front := {"id": "front", "name": "Front", "outline": sq.duplicate(true), "grain": 1.5708, "table": {"x": 0.0, "y": 0.0},
		"wrap": {"theta": 0.0, "y": TUBE_Y, "clear": 0.012, "ring": 0.0}}
	var back := {"id": "back", "name": "Back", "outline": sq.duplicate(true), "grain": 1.5708, "table": {"x": 0.6, "y": 0.0},
		"wrap": {"theta": 180.0, "y": TUBE_Y, "clear": 0.012, "ring": 0.0}}
	# edges: 0 bottom, 1 right, 2 top, 3 left. The sides are sewn front right to back left, and front left to back right.
	# The top runs right to left on both panels, and the seam is reversed, so the front's first part (from its right
	# corner) meets the back's last part: the back's range is 1 - top_fraction .. 1.
	var seams := [
		{"a": ["front", 1], "b": ["back", 3], "reverse": true},
		{"a": ["front", 3], "b": ["back", 1], "reverse": true},
		{"a": Seams.make_side("front", 2, 0.0, top_fraction), "b": Seams.make_side("back", 2, 1.0 - top_fraction, 1.0), "reverse": true},
	]
	return {"pieces": [front, back], "seams": seams}


# Drapes the tube with its top sewn to top_fraction and returns the largest nearest distance between the front's and the
# back's open top edge (metres): for a fully sewn top the top edges (edge 2), for a half-sewn top the open half (edge 3).
func _drape(stand: Dictionary, M: Dictionary, top_fraction: float) -> float:
	var t := _tube(top_fraction)
	var plan := PlanLoader.normalize_plan({"name": "tube", "fabric": "cotton_poplin", "pieces": t["pieces"], "seams": t["seams"]})
	var ex := Seams.expand(plan["pieces"], plan["seams"])
	var merged := Outfit.merge([{"uid": 1, "fabric": "cotton_poplin", "pieces": ex["pieces"], "seams": ex["seams"]}])
	var built: Dictionary = PlanLoader.build({"pieces": merged["pieces"], "seams": merged["seams"]}, stand["sections"], stand["arms"],
		_v3(stand["pos"]), M["tris"], "cotton_poplin", QUALITY, SEAM_STIFFNESS, RAMP, false)
	if built.is_empty():
		print("SEAMCHECK build failed")
		return -1.0
	var sim: FailoringSim = built["sim"]
	for f in range(1, SETTLE_FRAMES + 1):
		if f <= RAMP + 1:
			sim.set_param("gravity_y", -9.81 * minf(1.0, pow(float(f) / float(RAMP), 2.0)))
		sim.step(1)
	var pos: PackedVector3Array = sim.get_positions()
	var chains: Dictionary = built["chains"]
	# the open part of each top: whole (edge 2) when fully sewn; when half sewn the front's open part is its second half
	# (sub-edge 3) and the back's open part its first half (sub-edge 2), since the back's sewn part is its second half
	var fe := 2 if top_fraction >= 1.0 else 3
	var be := 2 if top_fraction >= 1.0 else 2
	var fv: Array = chains.get("g1_front", [])[fe]
	var bv: Array = chains.get("g1_back", [])[be]
	var worst := 0.0
	for v in fv:
		var best := 1e9
		for w in bv:
			best = minf(best, pos[int(v)].distance_to(pos[int(w)]))
		worst = maxf(worst, best)
	return worst
