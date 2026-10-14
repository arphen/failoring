# The living mannequin's soft tissue: breasts, seat, belly, thighs and upper arms that lag and bounce as the body walks,
# and a slow breath. Applied to the posed body every frame, before the cloth sees it (the collision body and the drawn
# body are the same), so the cloth is pushed and rubbed by the moving tissue.
#
# Model: each region is a damped spring driven by the acceleration of its own centre (inertia: the flesh lags its
# skeleton), x'' = -w^2 x - 2 z w x' - a, and its offset x is added to each vertex with that vertex's weight. A region and
# its weights come from the body's morph targets (body_data.json): a vertex moves as much as the matching target moves it
# (cup-max for the breasts, buttocks-incr, belly-incr, thigh-circ-incr, upperarm-circ-incr), normalised to 1 at the
# region's most mobile point, so the tissue moves where the body has tissue and not over bone.
#
# Numbers. Natural frequency, damping and the per-region gains are estimates, not measurements: breast-motion studies
# (Scurr et al., Univ. of Portsmouth) report a few cm of vertical breast motion when walking unsupported, growing with
# cup size, and model the breast as a spring and damper fitted per bra; they do not give the values used here. Measured
# on this mannequin's walk (pelvis bob 38 mm peak to peak; godot/_scratch probe, 2026-10-14): breasts 3.5 Hz, damping
# 0.25 give about 25 mm vertical peak to peak on the average body, 20 mm athletic, 44-48 mm curvy and plus (breast gain
# from the bust and cup measurements; unsupported, no bra); the seat, thighs and upper arms (5 Hz, 0.35, gain 0.35) about
# 5 mm; the belly about 9 mm. `gain` scales everything (the Body tab's Soft tissue slider; 0 is the old rigid body).
extends RefCounted

const DT := 1.0 / 60.0          # the editor's frame (SolverParams.dt)
const SUB := 4                  # integration substeps per frame
const BREATH_HZ := 0.25         # resting breathing, 15 breaths a minute
const BREATH_WALK_HZ := 0.4
const BREATH_MM := 2.5          # chest wall excursion at the most mobile point, mm

var regions: Array = []         # {name, idx: PackedInt32Array, w: PackedFloat32Array, f, z, gain, x: Vector3, v: Vector3, c_prev, v_prev}
var breath_idx := PackedInt32Array()
var breath_dir := PackedVector3Array()
var gain := 1.0
var t := 0.0
var frames := 0


# m: BodyGen.load_data result; meas: the body measurements; nb: body vertex count.
func setup(m: Dictionary, meas: Dictionary, nb: int) -> void:
	regions = []
	var bust := float(meas.get("bust", 0.92))
	var cup := float(meas.get("cup", 0.0))
	var breast_gain := clampf(1.0 + cup + 2.5 * (bust - 0.92), 0.4, 2.2)
	var targets: Dictionary = m.get("targets", {})
	var spec := [
		["breast.L", "cup-max", 1, 3.5, 0.25, breast_gain],
		["breast.R", "cup-max", -1, 3.5, 0.25, breast_gain],
		["seat.L", "buttocks-incr", 1, 5.0, 0.35, 0.35],
		["seat.R", "buttocks-incr", -1, 5.0, 0.35, 0.35],
		["belly", "belly-incr", 0, 4.0, 0.30, 0.5],
		["thigh.L", "thigh-circ-incr", 1, 5.0, 0.35, 0.35],
		["thigh.R", "thigh-circ-incr", -1, 5.0, 0.35, 0.35],
		["arm.L", "upperarm-circ-incr", 1, 5.0, 0.35, 0.35],
		["arm.R", "upperarm-circ-incr", -1, 5.0, 0.35, 0.35],
	]
	for s in spec:
		if not targets.has(s[1]):
			continue
		var tg: Dictionary = targets[s[1]]
		var ti: PackedInt32Array = tg["idx"]
		var td: PackedFloat32Array = tg["d"]
		var idx := PackedInt32Array()
		var mag := PackedFloat32Array()
		var mx := 0.0
		for k in ti.size():
			var vi := ti[k]
			if vi >= nb:
				continue
			# left/right by the target's own displacement side is unreliable near the midline: use the rest x position later;
			# here keep every vertex and split by side in place()
			var l := Vector3(td[3 * k], td[3 * k + 1], td[3 * k + 2]).length()
			if l <= 0.0:
				continue
			idx.append(vi)
			mag.append(l)
			mx = maxf(mx, l)
		if idx.is_empty() or mx <= 0.0:
			continue
		var w := PackedFloat32Array()
		w.resize(mag.size())
		for k in mag.size():
			var u := mag[k] / mx
			w[k] = u * u * (3.0 - 2.0 * u)   # smoothstep: the edge of the region fades out
		regions.append({"name": s[0], "side": int(s[2]), "idx": idx, "w": w, "f": float(s[3]), "z": float(s[4]),
			"gain": float(s[5]), "x": Vector3.ZERO, "v": Vector3.ZERO, "c_prev": null, "vel_prev": null, "sided": false})
	# breathing: the front of the chest, along the frontchest target's direction
	breath_idx = PackedInt32Array()
	breath_dir = PackedVector3Array()
	if targets.has("frontchest-dist-incr"):
		var tg2: Dictionary = targets["frontchest-dist-incr"]
		var ti2: PackedInt32Array = tg2["idx"]
		var td2: PackedFloat32Array = tg2["d"]
		var mx2 := 0.0
		for k in ti2.size():
			mx2 = maxf(mx2, Vector3(td2[3 * k], td2[3 * k + 1], td2[3 * k + 2]).length())
		for k in ti2.size():
			if ti2[k] < nb and mx2 > 0.0:
				breath_idx.append(ti2[k])
				breath_dir.append(Vector3(td2[3 * k], td2[3 * k + 1], td2[3 * k + 2]) / mx2)
	reset()


func reset() -> void:
	t = 0.0
	frames = 0
	for r in regions:
		r["x"] = Vector3.ZERO
		r["v"] = Vector3.ZERO
		r["c_prev"] = null
		r["vel_prev"] = null
		r["sided"] = false


# Keeps only the vertices on the region's side (x > 0 is the body's left); called once with the first posed body.
func _split_sides(pos: PackedVector3Array) -> void:
	for r in regions:
		if bool(r["sided"]):
			continue
		r["sided"] = true
		var side := int(r["side"])
		if side == 0:
			continue
		var idx: PackedInt32Array = r["idx"]
		var w: PackedFloat32Array = r["w"]
		var ni := PackedInt32Array()
		var nw := PackedFloat32Array()
		for k in idx.size():
			if signf(pos[idx[k]].x) == float(side):
				ni.append(idx[k])
				nw.append(w[k])
		r["idx"] = ni
		r["w"] = nw


# One frame: advances the springs with the posed body `pos` (the skeleton's pose, before the tissue) and returns the body
# with the tissue offsets and the breath added. walking: true while the body walks (breath rate).
func apply(pos: PackedVector3Array, walking: bool) -> PackedVector3Array:
	t += DT
	frames += 1
	if gain <= 0.0 or regions.is_empty():
		return pos
	_split_sides(pos)
	var out := pos.duplicate()
	for r in regions:
		var idx: PackedInt32Array = r["idx"]
		var w: PackedFloat32Array = r["w"]
		if idx.is_empty():
			continue
		# the region's centre, weighted, follows the skeleton; its acceleration drives the spring
		var c := Vector3.ZERO
		var sw := 0.0
		for k in idx.size():
			c += pos[idx[k]] * w[k]
			sw += w[k]
		c /= maxf(sw, 1e-9)
		var acc := Vector3.ZERO
		if r["c_prev"] != null:
			var vel: Vector3 = (c - (r["c_prev"] as Vector3)) / DT
			if r["vel_prev"] != null:
				acc = (vel - (r["vel_prev"] as Vector3)) / DT
			r["vel_prev"] = vel
		r["c_prev"] = c
		var om := TAU * float(r["f"])
		var z := float(r["z"])
		var x: Vector3 = r["x"]
		var v: Vector3 = r["v"]
		var h := DT / float(SUB)
		for s in SUB:
			var a := -om * om * x - 2.0 * z * om * v - acc
			v += a * h
			x += v * h
		# a bounded excursion: tissue cannot leave the body by more than a few cm (a jolt on the first walking frame)
		x = x.limit_length(0.04)
		r["x"] = x
		r["v"] = v
		var g := gain * float(r["gain"])
		for k in idx.size():
			out[idx[k]] += x * (w[k] * g)
	# breath
	var hz := BREATH_WALK_HZ if walking else BREATH_HZ
	var b := 0.5 * (1.0 - cos(TAU * hz * t)) * BREATH_MM * 0.001 * gain
	for k in breath_idx.size():
		out[breath_idx[k]] += breath_dir[k] * b
	return out


# The largest offset of a region now (metres), for tests and the stats line.
func excursion(name: String) -> float:
	for r in regions:
		if String(r["name"]) == name:
			return (r["x"] as Vector3).length() * gain * float(r["gain"])
	return 0.0
