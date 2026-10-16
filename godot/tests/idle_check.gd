# Checks the idle layer (idle.gd) with numbers, headless, no window:
#   godot --headless --path godot --script res://tests/idle_check.gd     (prints PASS/FAIL lines, then "idle_check: n failed")
# Ten simulated minutes at 30 Hz: breath rate, blink intervals and doubles, weight-shift intervals, the gaze's range; then the
# bone transforms on the real rig: feet planted, no NaN, identity at zero intensity, the same seed gives the same life.
extends SceneTree

const Idle = preload("res://idle.gd")
const BodyGen = preload("res://body_gen.gd")
const PlanLoader = preload("res://plan_loader.gd")

var _fails := 0


func _check(ok: bool, what: String) -> void:
	print(("PASS " if ok else "FAIL ") + what)
	if not ok:
		_fails += 1


func _init() -> void:
	var M := BodyGen.load_data(PlanLoader.load_json("res://assets/body_data.json"))
	var rest := BodyGen.generate(M, BodyGen.DEFAULT_MEAS.duplicate(), null)
	var st := BodyGen.stand(M, rest)
	var joints: Dictionary = st["joints"]
	var bones: Array = M["bones"]

	var il := Idle.new(3)
	var dt := 1.0 / 30.0
	var steps := 600 * 30
	var breath_up := 0
	var prev_b := 0.0
	var blink_starts: Array = []
	var prev_blink := 0.0
	var shifts: Array = []
	var prev_dw := 0.0
	var prev_w := il.weight
	var gaze_max := 0.0
	var blink_max := 0.0
	var breath_max := 0.0
	var breath_min := 9.0
	var foot_err := 0.0
	var finite := true
	var max_shift := 0.0
	var max_roll := 0.0
	var min_knee := 9.0
	var t := 0.0
	for i in steps:
		il.tick(dt)
		t += dt
		if prev_b < 0.5 and il.breath >= 0.5:
			breath_up += 1
		prev_b = il.breath
		if prev_blink < 0.5 and il.blink >= 0.5:
			blink_starts.append(t)
		prev_blink = il.blink
		var dw := il.weight - prev_w
		# a shift starts when the weight begins to move after a still period
		if absf(dw) > 1.0e-4 and absf(prev_dw) <= 1.0e-4:
			shifts.append(t)
		prev_dw = dw
		prev_w = il.weight
		gaze_max = maxf(gaze_max, il.gaze.length())
		blink_max = maxf(blink_max, il.blink)
		breath_max = maxf(breath_max, il.breath)
		breath_min = minf(breath_min, il.breath)
		if i % 15 == 0:
			var xf: Array = il.solve(joints, bones)
			for s in ["l", "r"]:
				var ia := -1
				var ik := -1
				for b in bones.size():
					if bones[b]["joint"] == s + "-ankle":
						ia = b
					if bones[b]["joint"] == s + "-knee":
						ik = b
				var ap := Vector3(joints[s + "-ankle"][0], joints[s + "-ankle"][1], joints[s + "-ankle"][2])
				var moved: Vector3 = (xf[ia] as Transform3D) * ap
				foot_err = maxf(foot_err, moved.distance_to(ap))
				var kb: Basis = (xf[ik] as Transform3D).basis
				min_knee = minf(min_knee, kb.get_euler().length())
			for tr in xf:
				var o: Vector3 = (tr as Transform3D).origin
				if not (is_finite(o.x) and is_finite(o.y) and is_finite(o.z)):
					finite = false
			var pel: Transform3D = xf[0]
			var pj := Vector3(joints["pelvis"][0], joints["pelvis"][1], joints["pelvis"][2])
			max_shift = maxf(max_shift, (pel * pj).distance_to(pj))       # how far the pelvis joint itself goes
			max_roll = maxf(max_roll, rad_to_deg(pel.basis.get_euler().length()))

	var minutes := float(steps) * dt / 60.0
	var bpm := float(breath_up) / minutes
	_check(bpm > 12.0 and bpm < 16.5, "breathing %.1f breaths a minute (12-16)" % bpm)
	_check(breath_min >= 0.0 and breath_max > 0.8 and breath_max < 2.0, "breath stays in range (0 .. %.2f)" % breath_max)
	# blinks: groups of one or two; the gap between groups is 3-6 s (a glance may bring a blink forward)
	var gaps: Array = []
	var doubles := 0
	for k in range(1, blink_starts.size()):
		var g: float = blink_starts[k] - blink_starts[k - 1]
		if g < 0.6:
			doubles += 1
		else:
			gaps.append(g)
	var gmean := 0.0
	var gmin := 99.0
	var gmax := 0.0
	for g in gaps:
		gmean += float(g)
		gmin = minf(gmin, float(g))
		gmax = maxf(gmax, float(g))
	gmean /= float(maxi(gaps.size(), 1))
	_check(gmean > 3.0 and gmean < 6.0, "blinks every %.1f s on average (3-6)" % gmean)
	_check(gmax < 7.0 and gmin > 0.55, "blink gaps between %.1f and %.1f s" % [gmin, gmax])
	var dfrac := float(doubles) / float(maxi(blink_starts.size() - doubles, 1))
	_check(dfrac > 0.08 and dfrac < 0.4, "%.0f %% of blinks are doubles" % (dfrac * 100.0))
	_check(blink_max > 0.99 and blink_max <= 1.0, "a blink closes the lids fully (max %.2f)" % blink_max)
	var sh_gaps: Array = []
	for k in range(1, shifts.size()):
		sh_gaps.append(shifts[k] - shifts[k - 1])
	var smin := 99.0
	var smax := 0.0
	for g in sh_gaps:
		smin = minf(smin, float(g))
		smax = maxf(smax, float(g))
	print("first shifts at ", shifts.slice(0, 8))
	_check(shifts.size() > 40 and smin > 4.9 and smax < 12.1, "%d weight shifts, %.1f to %.1f s apart (5-12)" % [shifts.size(), smin, smax])
	_check(gaze_max < 0.32 and gaze_max > 0.05, "gaze stays within 18 degrees (%.1f)" % rad_to_deg(gaze_max))
	_check(finite, "every bone transform is finite")
	_check(foot_err < 0.004, "feet stay planted: the ankles move at most %.1f mm" % (foot_err * 1000.0))
	_check(max_shift < 0.03 and max_shift > 0.005, "the pelvis moves %.1f cm at most" % (max_shift * 100.0))
	_check(max_roll > 1.0 and max_roll < 5.0, "the pelvis tilts %.1f degrees at most" % max_roll)

	# intensity 0: a statue, identity transforms
	var z := Idle.new(3)
	z.intensity = 0.0
	var dev := 0.0
	for i in 600:
		z.tick(dt)
		if i % 30 == 0:
			for tr in z.solve(joints, bones):
				var tt: Transform3D = tr
				dev = maxf(dev, (tt.origin).length() + (tt.basis.x - Vector3.RIGHT).length() + (tt.basis.y - Vector3.UP).length() + (tt.basis.z - Vector3.BACK).length())
	_check(dev < 1.0e-5 and z.breath == 0.0 and z.blink == 0.0 and z.gaze.length() < 1.0e-6, "intensity 0 is a statue (deviation %f)" % dev)

	# body_gain 0 (garments on): the body bones stay, the head and eyes still live
	var g0 := Idle.new(3)
	g0.body_gain = 0.0
	var pel_dev := 0.0
	var head_dev := 0.0
	var hi := -1
	for b in bones.size():
		if bones[b]["joint"] == "head":
			hi = b
	for i in 900:
		g0.tick(dt)
		if i % 30 == 0:
			var x0: Array = g0.solve(joints, bones)
			pel_dev = maxf(pel_dev, (x0[0] as Transform3D).origin.length() + ((x0[0] as Transform3D).basis.x - Vector3.RIGHT).length())
			head_dev = maxf(head_dev, ((x0[hi] as Transform3D).basis.x - Vector3.RIGHT).length())
	_check(pel_dev < 1.0e-5 and head_dev > 1.0e-4, "body_gain 0: pelvis still, head still drifts (%f, %f)" % [pel_dev, head_dev])

	# the same seed gives the same life, another seed a different one
	var a := Idle.new(11)
	var b2 := Idle.new(11)
	var c := Idle.new(12)
	var same := true
	var diff := false
	for i in 900:
		a.tick(dt)
		b2.tick(dt)
		c.tick(dt)
		if a.breath != b2.breath or a.gaze != b2.gaze or a.weight != b2.weight:
			same = false
		if absf(a.weight - c.weight) > 0.05 or (a.gaze - c.gaze).length() > 0.01:
			diff = true
	_check(same, "same seed, same life")
	_check(diff, "another seed, another life")

	print("idle_check: %d failed" % _fails)
	quit(1 if _fails > 0 else 0)
