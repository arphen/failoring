# Skirt back-coverage check (regression for the report "I can see the mannequin's backside" on the Pencil skirt).
#
# What a viewer behind the skirt sees: skin wherever no cloth lies between the body and the viewer. The check takes every
# trunk or leg vertex in the back seat band (28 cm to 6 cm below the waist landmark, normal facing the back) and casts a
# ray from it straight out to the back, RAY_LEN long. The vertex is covered when the ray meets a triangle of the display
# mesh, the surface the editor draws (exact Moller-Trumbore test, triangles binned in a uniform grid). A missing panel, a
# gap at a seam or a cloth pulled off the seat all show up as skin. The front seat is measured the same way as a control.
# FL_DEBUG=1 prints the covered share per 45 degree sector around the body, the uncovered points and the widest seams.
#
# The check rebuilds each case the way editor.gd rebuild() does (Table.valid_pieces, Outfit.merge, PlanLoader.build),
# drapes it with the editor's gravity ramp, then walks it with the editor's gait. Coverage is read at the start pose,
# after the drape and at four points of the walk. It prints COVERCHECK lines and exits 0 when every case reaches
# MIN_COVER on the back after the drape and during the walk, and no piece was dropped from the simulation, 1 otherwise.
# The start pose is reported but not gated: the cloth starts on the girth ring and falls onto the body. The KNOWN_FIT
# cases are reported but not gated. Run headless (needs libfailoring, see docs/NATIVE_NOTES.md):
#   godot --headless --path godot res://tests/skirt_cover_check.tscn
# FL_CASES=pencil:average,penelope:curvy restricts the cases (garment:preset, comma separated; garment pencil, penelope,
# teagan or charlie, or a built-in plan). FL_NOWALK=1 skips the walk. FL_DEBUG=1 prints the breakdown. FL_GPU_STEP=1 runs
# the frames on the GPU (needs a window) and prints how many did.
extends Node

const BodyGen = preload("res://body_gen.gd")
const PlanLoader = preload("res://plan_loader.gd")
const Builtin = preload("res://builtin_plans.gd")
const Table = preload("res://table.gd")
const Outfit = preload("res://outfit.gd")

const MIN_COVER := 0.95
const RAY_T0 := -0.004         # the view ray starts a little inside the body surface (m): cloth in contact counts ...
const RAY_LEN := 0.15          # ... and it reaches this far out (m)
const RAY_STEP := 0.01         # cell lookups along the ray (m)
const SEAT_TOP := 0.06         # seat band: from this far below the waist landmark ...
const SEAT_BOT := 0.28         # ... to this far below it (m)
const FACING := 0.35           # a vertex counts as facing the back when its normal points this far that way
const CELL := 0.02             # uniform grid of cloth triangle boxes (m)
const QUALITY := {"edge": 0.040, "sub": 2, "newton": 8, "frames": 48}   # the editor's Standard quality
const SEAM_STIFFNESS := 20000.0
const RAMP := 20               # gravity ramp frames (editor default ramp_frames)
const CYCLE := 64.0            # walk cycle frames
const WALK_RAMP := 24.0
const WALK_FRAMES := 96
const DEFAULT_CASES := "pencil:average,pencil:curvy,pencil:plus,pencil:petite,penelope:average,penelope:curvy,penelope:plus,penelope:petite"
# Reported but not gated: the built-in pencil is one fixed outline, 104 cm round the hip, sized for the average body
# (the wrap is re-evaluated on the body, the outline is not). On plus (about 124 cm hips) it stays at the waist and the
# seat shows on the walk; on petite (about 90 cm) it hangs loose. Fitting it to the body is a design change.
const KNOWN_FIT := {
	"pencil:plus": "outline too narrow for the hip, rides up at the waist on the walk",
	"pencil:petite": "outline too wide for the hip, hangs loose",
}

var fails := 0


func _check(ok: bool, what: String) -> void:
	print("COVERCHECK %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


func _ready() -> void:
	var raw := OS.get_environment("FL_CASES")
	if raw == "":
		raw = DEFAULT_CASES
	var no_walk := OS.has_environment("FL_NOWALK")
	var body_cache := {}
	for c in raw.split(","):
		var parts := c.split(":")
		if parts.size() != 2:
			continue
		var garment := parts[0]
		var preset := parts[1]
		if not body_cache.has(preset):
			body_cache[preset] = _body(preset)
		var res := _run_case(garment, preset, body_cache[preset], no_walk)
		var line := "%s/%s back start=%.3f drape=%.3f walk_min=%.3f pieces=%s" % [
			garment, preset, res["start"], res["drape"], res["walk_min"], res["pieces"]]
		if KNOWN_FIT.has(garment + ":" + preset):
			print("COVERCHECK known, not gated: %s (%s)" % [line, KNOWN_FIT[garment + ":" + preset]])
		else:
			_check(res["ok"], line)
	get_tree().quit(1 if fails > 0 else 0)


func _body(preset: String) -> Dictionary:
	var M: Dictionary = BodyGen.load_data(PlanLoader.load_json("res://assets/body_data.json"))
	var meas: Dictionary = (BodyGen.PRESETS.get(preset, BodyGen.PRESETS["average"]) as Dictionary).duplicate()
	var rest = BodyGen.generate(M, meas, null)
	var stand: Dictionary = BodyGen.stand(M, rest)
	return {"M": M, "rest": rest, "stand": stand}


static func _v3(p: PackedFloat32Array) -> PackedVector3Array:
	var out := PackedVector3Array()
	out.resize(p.size() / 3)
	for i in p.size() / 3:
		out[i] = Vector3(p[3 * i], p[3 * i + 1], p[3 * i + 2])
	return out


static func _smooth(x: float) -> float:
	x = clampf(x, 0.0, 1.0)
	return x * x * (3.0 - 2.0 * x)


# The plan as the editor has it: a built-in plan (pencil, tiered, ...), or a FreeSewing garment (penelope, teagan, charlie) fitted to the preset.
func _plan(garment: String, preset: String) -> Dictionary:
	var p := {}
	if Builtin.all().has(garment):
		p = (Builtin.all()[garment] as Dictionary).duplicate(true)
	else:
		p = PlanLoader.load_json("res://assets/%s/plan_%s.json" % [preset, garment])
	return PlanLoader.normalize_plan(p)


func _run_case(garment: String, preset: String, b: Dictionary, no_walk: bool) -> Dictionary:
	var M: Dictionary = b["M"]
	var stand: Dictionary = b["stand"]
	var plan := _plan(garment, preset)
	# editor.gd rebuild(): drop invalid pieces, then merge the visible garments
	var valid := Table.valid_pieces(plan["pieces"])
	var merged := Outfit.merge([{"uid": 1, "fabric": String(plan["fabric"]), "pieces": valid["pieces"], "seams": plan["seams"]}])
	var built: Dictionary = PlanLoader.build({"pieces": merged["pieces"], "seams": merged["seams"]}, stand["sections"], stand["arms"],
		_v3(stand["pos"]), M["tris"], String(plan["fabric"]), QUALITY, SEAM_STIFFNESS, RAMP, false)
	var ids := PackedStringArray()
	for p in valid["pieces"]:
		ids.append(String(p["id"]))
	var pieces_txt := ",".join(ids)
	var dropped := ",".join(PackedStringArray(valid["bad"].keys()))
	if built.is_empty():
		print("COVERCHECK %s/%s build failed" % [garment, preset])
		return {"ok": false, "start": -1.0, "drape": -1.0, "walk_min": -1.0, "pieces": pieces_txt}
	var sim: FailoringSim = built["sim"]
	var back := _seat_region(stand, M, 1.0)
	var front := _seat_region(stand, M, -1.0)
	var back_dir := _seat_dir(stand, 1.0)
	var front_dir := _seat_dir(stand, -1.0)
	print("COVERCHECK %s/%s pieces=[%s] dropped=[%s] seat back=%d front=%d verts" % [garment, preset, pieces_txt, dropped, back.size(), front.size()])

	var body0 := _v3(stand["pos"])
	var start := _coverage(sim, body0, back, back_dir, M)
	var start_front := _coverage(sim, body0, front, front_dir, M)
	# drape: the editor's gravity ramp, body still
	for f in range(1, QUALITY["frames"] + 1):
		if f <= RAMP + 1:
			sim.set_param("gravity_y", -9.81 * minf(1.0, pow(float(f) / float(RAMP), 2.0)))
		sim.step(1)
	var drape := _coverage(sim, body0, back, back_dir, M)
	var drape_front := _coverage(sim, body0, front, front_dir, M)
	var low := 1e9
	for c in sim.get_positions():
		low = minf(low, c.y)
	print("COVERCHECK %s/%s front control start=%.3f drape=%.3f seam_gap_mm=%.2f clearance_mm=%.2f cloth_min_y=%.3f" % [garment, preset,
		start_front, drape_front, sim.get_max_seam_gap() * 1000.0, sim.get_min_body_clearance() * 1000.0, low])
	if OS.has_environment("FL_DEBUG"):
		_report(sim, body0, back, back_dir, "%s/%s back" % [garment, preset])
		_report(sim, body0, front, front_dir, "%s/%s front" % [garment, preset])
		_seam_report(sim, built, merged, "%s/%s" % [garment, preset])
	var walk_min := 1.0
	var walk_txt := ""
	if not no_walk:
		var worst := 1.0
		for k in range(1, WALK_FRAMES + 1):
			var stride := _smooth(minf(1.0, float(k) / WALK_RAMP))
			var phase := fposmod(float(k) / CYCLE, 1.0)
			var pose: Dictionary = BodyGen.pose(M, b["rest"], phase, {"stride": stride, "arms": stride})
			var body := _v3(pose["pos"])
			sim.set_body_positions(body)
			sim.step(1)
			if k % 24 == 0:
				var c := _coverage(sim, body, back, back_dir, M)
				walk_txt += " k%d=%.3f" % [k, c]
				worst = minf(worst, c)
		walk_min = worst
	print("COVERCHECK %s/%s walk back:[%s]" % [garment, preset, walk_txt.strip_edges()])
	var st: Dictionary = sim.get_solver_stats()
	if st.has("gpu_step_frames"):
		print("COVERCHECK %s/%s GPU frames %d of %d, last %.1f ms (CPU reason: %s)" % [garment, preset, int(st["gpu_step_frames"]),
			sim.get_frames(), float(st["gpu_step_last_ms"]), st["gpu_step_cpu_reason"]])
	# a shipped pattern piece must reach the simulation: a dropped piece is the bug this check was written for
	var ok := dropped == "" and drape >= MIN_COVER and walk_min >= MIN_COVER
	if dropped != "":
		print("COVERCHECK %s/%s dropped from the simulation: %s" % [garment, preset, dropped])
	return {"ok": ok, "start": start, "drape": drape, "walk_min": walk_min, "pieces": pieces_txt}


# Horizontal unit vector from the body axis at waist height towards the back (side 1) or the front (side -1).
func _seat_dir(stand: Dictionary, side: float) -> Vector3:
	var pos := _v3(stand["pos"])
	var lm: Dictionary = stand["landmarks"]
	# the waist landmark sits on the back midline; the body's centre at waist height is the mean of the waist ring
	var waist := Vector3(lm["waist"][0], lm["waist"][1], lm["waist"][2])
	var centre := Vector3.ZERO
	var ring := 0
	for p in pos:
		if absf(p.y - waist.y) < 0.01:
			centre += p
			ring += 1
	centre /= float(maxi(ring, 1))
	return Vector3(waist.x - centre.x, 0.0, waist.z - centre.z).normalized() * side


# Body vertices in the seat band that face the view direction, trunk and legs only.
func _seat_region(stand: Dictionary, M: Dictionary, side: float) -> PackedInt32Array:
	var pos := _v3(stand["pos"])
	var lm: Dictionary = stand["landmarks"]
	var waist := Vector3(lm["waist"][0], lm["waist"][1], lm["waist"][2])
	var dir := _seat_dir(stand, side)
	var nrm := _vertex_normals(pos, M["tris"])
	# arms hang beside the hips and a skirt does not cover them
	var allowed := {}
	for name in ["trunk", "leg.L", "leg.R"]:
		allowed[(M["parts"] as Array).find(name)] = true
	var part: PackedInt32Array = M["part"]
	var out := PackedInt32Array()
	for i in pos.size():
		var y := pos[i].y
		if y > waist.y - SEAT_TOP or y < waist.y - SEAT_BOT:
			continue
		if i >= part.size() or not allowed.has(part[i]):
			continue
		if nrm[i].dot(dir) < FACING:
			continue
		out.append(i)
	return out


# Area-weighted vertex normals, oriented outward: the winding is not assumed, most normals decide the sign.
func _vertex_normals(pos: PackedVector3Array, tris: PackedInt32Array) -> PackedVector3Array:
	var n := PackedVector3Array()
	n.resize(pos.size())
	for t in range(0, tris.size() - 2, 3):
		var a := tris[t]
		var b := tris[t + 1]
		var c := tris[t + 2]
		var fn := (pos[b] - pos[a]).cross(pos[c] - pos[a])
		n[a] += fn
		n[b] += fn
		n[c] += fn
	var centre := Vector3.ZERO
	for p in pos:
		centre += p
	centre /= float(maxi(pos.size(), 1))
	var outward := 0
	for i in n.size():
		n[i] = n[i].normalized()
		outward += 1 if n[i].dot(pos[i] - centre) > 0.0 else -1
	if outward < 0:
		for i in n.size():
			n[i] = -n[i]
	return n


# Fraction of the region's body vertices covered (see the header).
func _coverage(sim: FailoringSim, body: PackedVector3Array, region: PackedInt32Array, dir: Vector3, M: Dictionary) -> float:
	if region.is_empty():
		return 0.0
	var flags := _flags(sim, body, region, dir, M)
	var hit := 0
	for f in flags:
		hit += f
	return float(hit) / float(region.size())


# Debug aid (FL_DEBUG=1): covered share per 45 degree sector around the body axis (label 0 = back, 180 = front), and
# the uncovered points.
func _report(sim: FailoringSim, body: PackedVector3Array, region: PackedInt32Array, dir: Vector3, tag: String) -> void:
	var flags := _flags(sim, body, region, dir, null)
	var centre := Vector3.ZERO
	for p in body:
		centre += p
	centre /= float(maxi(body.size(), 1))
	var sec_hit := PackedInt32Array([0, 0, 0, 0, 0, 0, 0, 0])
	var sec_n := PackedInt32Array([0, 0, 0, 0, 0, 0, 0, 0])
	for i in region.size():
		var p := body[region[i]]
		var a := atan2(p.x - centre.x, p.z - centre.z)   # 0 = front (+z), -PI = back
		var s := posmod(int(floor((a + PI) / (TAU / 8.0))), 8)
		sec_n[s] += 1
		sec_hit[s] += flags[i]
	var line := ""
	for s in 8:
		line += " %d:%d%%/%d" % [s * 45, int(round(100.0 * float(sec_hit[s]) / float(maxi(sec_n[s], 1)))), sec_n[s]]
	print("COVERDEBUG %s sectors(deg,%%covered/n):%s" % [tag, line])
	var miss := ""
	for i in region.size():
		if flags[i] == 0 and miss.length() < 900:
			var p := body[region[i]]
			miss += " (%.3f,%.3f,%.3f)" % [p.x, p.y, p.z]
	print("COVERDEBUG %s uncovered:%s" % [tag, miss])


# Debug aid (FL_DEBUG=1): the widest seams after the drape, paired the way the editor draws them (garment_health.gd
# _seam_gap), with the height range of the seam's cloth.
func _seam_report(sim: FailoringSim, built: Dictionary, merged: Dictionary, tag: String) -> void:
	var cloth := sim.get_positions()
	var chains: Dictionary = built["chains"]
	var rows := []
	for s in merged["seams"]:
		var ca: Array = chains[String(s["a"][0])][int(s["a"][1])]
		var cb: Array = chains[String(s["b"][0])][int(s["b"][1])]
		if ca.size() < 2 or cb.size() < 2:
			continue
		var m: int = maxi(ca.size(), cb.size())
		var worst := 0.0
		var at := Vector3.ZERO
		for k in m:
			var tt := float(k) / float(maxi(1, m - 1))
			var ia: int = ca[mini(ca.size() - 1, int(round(tt * float(ca.size() - 1))))]
			var tb := (1.0 - tt) if bool(s["reverse"]) else tt
			var ib: int = cb[mini(cb.size() - 1, int(round(tb * float(cb.size() - 1))))]
			var g: float = cloth[ia].distance_to(cloth[ib])
			if g > worst:
				worst = g
				at = (cloth[ia] + cloth[ib]) * 0.5
		rows.append([worst, "%s[%d]-%s[%d]" % [s["a"][0], s["a"][1], s["b"][0], s["b"][1]], at.y])
	rows.sort_custom(func(x, y): return x[0] > y[0])
	for i in mini(5, rows.size()):
		print("COVERDEBUG %s seam %.1f mm %s at y=%.3f" % [tag, float(rows[i][0]) * 1000.0, rows[i][1], float(rows[i][2])])


# One flag per region vertex: 1 when the ray from it along dir meets a cloth triangle within RAY_LEN.
func _flags(sim: FailoringSim, body: PackedVector3Array, region: PackedInt32Array, dir: Vector3, _M: Variant) -> PackedByteArray:
	# the surface that is drawn: the display mesh (cloth vertices and edge midpoints, 0.8 mm off the body as in the editor)
	var cloth := sim.get_display_positions(0.0008)
	var tris := sim.get_display_indices()
	var cells := {}
	for t in range(0, tris.size() - 2, 3):
		var a := cloth[tris[t]]
		var b := cloth[tris[t + 1]]
		var c := cloth[tris[t + 2]]
		var lo := Vector3(minf(minf(a.x, b.x), c.x), minf(minf(a.y, b.y), c.y), minf(minf(a.z, b.z), c.z))
		var hi := Vector3(maxf(maxf(a.x, b.x), c.x), maxf(maxf(a.y, b.y), c.y), maxf(maxf(a.z, b.z), c.z))
		var c0 := _cell_of(lo)
		var c1 := _cell_of(hi)
		for x in range(c0.x, c1.x + 1):
			for y in range(c0.y, c1.y + 1):
				for z in range(c0.z, c1.z + 1):
					var key := _key(Vector3i(x, y, z))
					if not cells.has(key):
						cells[key] = []
					(cells[key] as Array).append(t)
	var flags := PackedByteArray()
	flags.resize(region.size())
	var d := dir.normalized()
	for i in region.size():
		var o := body[region[i]]
		var found := false
		var s := RAY_T0
		while s <= RAY_LEN and not found:
			var q := o + d * s
			var cq := _cell_of(q)
			for dx in range(-1, 2):
				for dy in range(-1, 2):
					for dz in range(-1, 2):
						var key := _key(cq + Vector3i(dx, dy, dz))
						if not cells.has(key):
							continue
						for t in (cells[key] as Array):
							if _ray_hits(o, d, cloth[tris[t]], cloth[tris[t + 1]], cloth[tris[t + 2]]):
								found = true
								break
						if found:
							break
					if found:
						break
				if found:
					break
			s += RAY_STEP
		flags[i] = 1 if found else 0
	return flags


# Moller-Trumbore: does the ray o + d * t, t in [0, RAY_LEN], meet triangle abc? Two-sided.
static func _ray_hits(o: Vector3, d: Vector3, a: Vector3, b: Vector3, c: Vector3) -> bool:
	var e1 := b - a
	var e2 := c - a
	var p := d.cross(e2)
	var det := e1.dot(p)
	if absf(det) < 1e-14:
		return false
	var inv := 1.0 / det
	var s := o - a
	var u := s.dot(p) * inv
	if u < 0.0 or u > 1.0:
		return false
	var qv := s.cross(e1)
	var v := d.dot(qv) * inv
	if v < 0.0 or u + v > 1.0:
		return false
	var t := e2.dot(qv) * inv
	return t >= RAY_T0 and t <= RAY_LEN


static func _cell_of(p: Vector3) -> Vector3i:
	return Vector3i(floori(p.x / CELL), floori(p.y / CELL), floori(p.z / CELL))


static func _key(c: Vector3i) -> int:
	return (c.x * 73856093) ^ (c.y * 19349663) ^ (c.z * 83492791)
