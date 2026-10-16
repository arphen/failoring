# Procedural idle life for the mannequin, on top of whatever pose is drawn (look only: nothing here reaches the solver or the
# collision body). A person standing still is never still:
#   breathing     12-16 breaths a minute, the rate and depth wandering with noise and a new depth every breath; the ribcage
#                 and belly swell (the skin shader displaces them with `breath`), the spine extends and the shoulders rise
#   weight shifts every 5-12 s (random) the weight moves to the other leg: the pelvis tilts and shifts over the loaded foot, the
#                 spine counter-rotates, the head follows a moment later, the relaxed knee softens; both feet stay planted
#                 (a two-bone IK on each leg), everything eased in and out
#   micro motion  the head drifts, a shoulder or an arm now and then settles into a new place, the wrists relax
#   eyes          blinks every 3-6 s (a double blink one time in five), small fast saccades, a gaze that wanders
#
# The motion is a set of small rotations (Euler, radians, about world axes through each joint) that `solve` turns into one
# world transform per bone, relative to the pose being drawn. BodyView writes them into a 24 x 3 texture and the skin's vertex
# shader skins the mesh with them (CUSTOM1/2 = bones and weights): the mesh itself is not rebuilt, so the idle layer costs
# a few microseconds on the CPU. Eyes and lids take `gaze` and `blink` (eyes.gd, skin.gdshader).
#
# Every parameter is a plain member, so a UI or a test can set it; `defs()` lists them with ranges for building sliders.
extends RefCounted

var enabled := true
var intensity := 1.0          # master gain on all of it: 0 still .. 2 exaggerated
var breath_rate := 14.0       # breaths a minute (the mean; it wanders about 18 % round it)
var breath_depth := 1.0
var sway := 1.0               # weight shifts: how far the pelvis and spine go
var sway_min := 5.0           # s between weight shifts
var sway_max := 12.0
var micro := 1.0              # head drift, shoulder and arm settling, wrists
var blink_min := 3.0          # s between blinks
var blink_max := 6.0
var eye_motion := 1.0         # saccades and gaze drift
var body_gain := 1.0          # how much of the body moves at all (BodyView lowers it when garments are on: the cloth does not follow)
var hold_blink := -1.0        # >= 0 holds the lids at that closure (for looking at them); -1 blinks normally

# outputs, after tick()
var breath := 0.0             # 0 .. 1, exhaled .. inhaled (scaled by breath_depth)
var blink := 0.0              # 0 open .. 1 closed
var gaze := Vector2.ZERO      # radians: x to the body's left, y up; added to the eyes' resting gaze
var weight := 0.0             # -1 .. 1: -1 the weight on the right leg, +1 on the left

var _t := 0.0
var _n := FastNoiseLite.new()
var _rng := RandomNumberGenerator.new()
var _rot: Dictionary = {}     # joint name -> Euler (rx pitch forward, ry turn to the left, rz roll: left side up)
var _root := Vector3.ZERO
var _idx: Dictionary = {}     # joint name -> bone index
var _w_from := 0.5
var _w_to := 0.5
var _w_t := 1.0
var _w_dur := 1.8
var _w_next := 4.0
var _hf := 0.0                # the head's lagging copy of the weight shift
var _phase := 0.0             # breath phase 0..1
var _b_depth := 1.0           # depth of this breath
var _blink_t := -1.0          # seconds since the blink started (-1: none)
var _blink_next := 3.5
var _blink_again := false
var _gz_target := Vector2.ZERO
var _gz_next := 1.0
var _arm_next := 5.0
var _arm := {"l": Vector3.ZERO, "r": Vector3.ZERO}         # current shoulder roll, elbow pitch, wrist roll
var _arm_tgt := {"l": Vector3.ZERO, "r": Vector3.ZERO}


func _init(seed_value: int = 7) -> void:
	_rng.seed = seed_value
	_n.seed = seed_value
	_n.noise_type = FastNoiseLite.TYPE_PERLIN
	_n.frequency = 1.0
	_w_from = 0.45                    # the weight starts a little on the left leg, not exactly centred
	_w_to = 0.45
	weight = 0.45
	_hf = 0.45
	_w_next = _rng.randf_range(2.0, 5.0)
	_blink_next = _rng.randf_range(1.5, 3.5)
	_phase = _rng.randf()
	_gz_next = _rng.randf_range(0.5, 1.5)
	_arm_next = _rng.randf_range(3.0, 7.0)


# Parameters for building controls: [member, label, min, max, tooltip]
static func defs() -> Array:
	return [
		["intensity", "Idle life", 0.0, 2.0, "Master gain on breathing, weight shifts, micro motion, blinks and gaze. 0 is a statue."],
		["breath_rate", "Breaths / min", 8.0, 24.0, "Resting adults take 12 to 16."],
		["breath_depth", "Breath depth", 0.0, 2.0, "How far the chest and belly move."],
		["sway", "Weight shifts", 0.0, 2.0, "How far the pelvis and spine go when the weight moves to the other leg."],
		["micro", "Micro motion", 0.0, 2.0, "Head drift, shoulders and arms settling, wrists."],
		["eye_motion", "Eye motion", 0.0, 2.0, "Saccades and gaze drift."],
	]


# ---------------------------------------------------------------- time
func tick(dt: float) -> void:
	dt = minf(dt, 0.1)
	_t += dt
	var k := intensity if enabled else 0.0
	_tick_breath(dt, k)
	_tick_weight(dt)
	_tick_blink(dt, k)
	_tick_gaze(dt, k)
	_tick_arms(dt)
	_pose(k)


func _tick_breath(dt: float, k: float) -> void:
	var rate := breath_rate * (1.0 + 0.18 * _n.get_noise_2d(_t * 0.06, 11.0))     # the rate wanders
	_phase += dt * rate / 60.0
	if _phase >= 1.0:
		_phase -= 1.0
		_b_depth = _rng.randf_range(0.75, 1.3)                                  # a new depth with every breath
	# inhale 38 %, a longer exhale, a short pause before the next breath
	var b := 0.0
	if _phase < 0.38:
		b = smoothstep(0.0, 1.0, _phase / 0.38)
	elif _phase < 0.9:
		b = 1.0 - smoothstep(0.0, 1.0, (_phase - 0.38) / 0.52)
	var amp := breath_depth * _b_depth * (1.0 + 0.25 * _n.get_noise_2d(_t * 0.1, 31.0))
	breath = b * amp * k


func _tick_weight(dt: float) -> void:
	_w_next -= dt
	if _w_next <= 0.0 and _w_t >= 1.0:
		var cur := weight
		_w_from = cur
		# to the other leg, not always all the way
		var sgn := -1.0 if cur >= 0.0 else 1.0
		_w_to = sgn * _rng.randf_range(0.55, 1.0)
		_w_dur = _rng.randf_range(1.3, 2.4)
		_w_t = 0.0
		_w_next = _rng.randf_range(sway_min, sway_max)
	if _w_t < 1.0:
		_w_t = minf(1.0, _w_t + dt / _w_dur)
	weight = lerpf(_w_from, _w_to, _smoother(_w_t))
	_hf += (weight - _hf) * (1.0 - exp(-dt / 0.4))                                 # the head follows through


func _tick_blink(dt: float, k: float) -> void:
	_blink_next -= dt
	if _blink_next <= 0.0 and _blink_t < 0.0:
		_blink_t = 0.0
		_blink_again = _rng.randf() < 0.2
		_blink_next = _rng.randf_range(blink_min, blink_max)
	var v := 0.0
	if _blink_t >= 0.0:
		_blink_t += dt
		var d := 0.17                                                              # a blink lasts about 170 ms: shut fast, open slower
		var u := _blink_t / d
		if u < 1.0:
			v = smoothstep(0.0, 0.38, u) * (1.0 - smoothstep(0.42, 1.0, u))
		else:
			if _blink_again and _blink_t < d + 0.09:
				pass
			elif _blink_again:
				_blink_t = 0.0                                                     # the second blink of a double
				_blink_again = false
			else:
				_blink_t = -1.0
	blink = v * clampf(k, 0.0, 1.0)
	if hold_blink >= 0.0:
		blink = hold_blink


func _tick_gaze(dt: float, k: float) -> void:
	_gz_next -= dt
	if _gz_next <= 0.0:
		# mostly small shifts, now and then a real glance to the side or down
		var big := _rng.randf() < 0.12
		var amp := Vector2(0.17, 0.1) if big else Vector2(0.05, 0.03)
		_gz_target = Vector2(_rng.randfn(0.0, amp.x), _rng.randfn(-0.01, amp.y)).clampf(-0.3, 0.3)
		_gz_next = _rng.randf_range(0.6, 3.2) if not big else _rng.randf_range(1.0, 2.5)
		if big and _blink_t < 0.0 and _rng.randf() < 0.5:
			_blink_next = minf(_blink_next, 0.08)                                  # a big glance is often caught in a blink
	var drift := Vector2(_n.get_noise_2d(_t * 0.7, 51.0), _n.get_noise_2d(_t * 0.6, 71.0)) * 0.006
	# saccades are fast: the eye is there within a few frames
	gaze += ((_gz_target + drift) * eye_motion * k - gaze) * (1.0 - exp(-dt / 0.03))


func _tick_arms(dt: float) -> void:
	_arm_next -= dt
	if _arm_next <= 0.0:
		_arm_next = _rng.randf_range(6.0, 14.0)
		var s := "l" if _rng.randf() < 0.5 else "r"
		var sg := 1.0 if s == "l" else -1.0
		# a shoulder settles a little, the elbow and the wrist follow
		_arm_tgt[s] = Vector3(_rng.randf_range(-0.03, 0.04) * sg, _rng.randf_range(-0.03, 0.05), _rng.randf_range(-0.05, 0.05))
	for s in ["l", "r"]:
		_arm[s] = (_arm[s] as Vector3).lerp(_arm_tgt[s], 1.0 - exp(-dt / 0.9))
		# the settled pose relaxes back after a while
		_arm_tgt[s] = (_arm_tgt[s] as Vector3).lerp(Vector3.ZERO, 1.0 - exp(-dt / 7.0))


# ---------------------------------------------------------------- the rotations
func _pose(k: float) -> void:
	_rot.clear()
	var g := k * body_gain
	var d := PI / 180.0
	var w := weight * sway * g
	var roll := 2.4 * d * w
	var yaw := 1.0 * d * w
	var drop := -0.006 * absf(w)                                                   # the pelvis sinks as the weight comes onto one leg
	_root = Vector3(0.017 * w, drop, 0.0)
	_rot["pelvis"] = Vector3(0.0, yaw, roll)
	# the spine counter-rotates (the shoulders stay level-ish: contrapposto)
	var b := breath
	for jn in ["spine-4", "spine-3", "spine-2", "spine-1"]:
		_rot[jn] = Vector3(0.0, -0.4 * yaw, -0.4 * roll)
	_rot["spine-2"] = (_rot["spine-2"] as Vector3) + Vector3(-0.35 * d * b * body_gain, 0.0, 0.0)     # the chest rises and extends
	_rot["spine-1"] = (_rot["spine-1"] as Vector3) + Vector3(-0.45 * d * b * body_gain, 0.0, 0.0)
	_rot["l-clavicle"] = Vector3(0.0, 0.0, 0.7 * d * b * body_gain)                                # the shoulders rise a little
	_rot["r-clavicle"] = Vector3(0.0, 0.0, -0.7 * d * b * body_gain)
	# head: follows the weight shift a moment late, drifts, nods with the breath
	var wf := _hf * sway * g
	var m := micro * k
	var hy := (1.4 * d * _nz(0.13, 3.0) + 0.5 * d * wf) * m + 0.5 * d * wf * (1.0 - m)
	var hp := 0.9 * d * _nz(0.11, 5.0) * m + 0.12 * d * b * body_gain
	var hr := 0.6 * d * _nz(0.09, 7.0) * m + (0.3 * 2.4 * d * wf) * 1.0
	_rot["neck"] = Vector3(0.4 * hp, 0.4 * hy, 0.35 * hr)
	_rot["head"] = Vector3(0.6 * hp - 0.2 * d * b * body_gain, 0.6 * hy, 0.65 * hr)
	# arms and wrists
	for s in ["l", "r"]:
		var a: Vector3 = _arm[s]
		var sg := 1.0 if s == "l" else -1.0
		var wr := 2.2 * d * _nz(0.17, 13.0 if s == "l" else 17.0) * m * body_gain
		_rot[s + "-shoulder"] = Vector3(0.0, 0.0, a.x) * m * body_gain
		_rot[s + "-elbow"] = Vector3(a.y, 0.0, 0.0) * m * body_gain
		_rot[s + "-hand"] = Vector3(0.0, 0.0, a.z * m * body_gain + wr)


func _nz(freq: float, seed_offset: float) -> float:
	return _n.get_noise_2d(_t * freq, seed_offset)


static func _smoother(t: float) -> float:
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


# ---------------------------------------------------------------- the transforms
# One world transform per bone (the order of `bones`: dictionaries with "joint" and "parent"), mapping a point of the pose that
# is drawn to where the idle layer puts it. `joints`: joint name -> position (Vector3 or [x, y, z]) in the drawn pose.
func solve(joints: Dictionary, bones: Array) -> Array:
	var n := bones.size()
	if _idx.size() != n:
		_idx.clear()
		for i in n:
			_idx[(bones[i] as Dictionary)["joint"]] = i
	var xf: Array = []
	xf.resize(n)
	for i in n:
		var bd: Dictionary = bones[i]
		var jn: String = bd["joint"]
		var par := int(bd["parent"])
		var piv := _jp(joints, jn)
		var r := Basis.from_euler(_rot.get(jn, Vector3.ZERO))
		var local := Transform3D(r, piv - r * piv)
		var up: Transform3D = xf[par] if par >= 0 else Transform3D(Basis(), _root)
		xf[i] = up * local
	for s in ["l", "r"]:
		_leg(xf, joints, s)
	return xf


static func _jp(joints: Dictionary, name: String) -> Vector3:
	var v: Variant = joints.get(name, Vector3.ZERO)
	return v if v is Vector3 else Vector3(float(v[0]), float(v[1]), float(v[2]))


# The leg of one side, solved again from the moved pelvis so that the ankle stays where it was: a two-bone IK that keeps the
# bend in the plane it had. The relaxed knee softens, the loaded leg stays straight, the foot is planted.
func _leg(xf: Array, joints: Dictionary, s: String) -> void:
	var iu: int = _idx.get(s + "-upper-leg", -1)
	var ik: int = _idx.get(s + "-knee", -1)
	var ia: int = _idx.get(s + "-ankle", -1)
	var it: int = _idx.get(s + "-toe", -1)
	if iu < 0 or ik < 0 or ia < 0:
		return
	var hp := _jp(joints, s + "-upper-leg")
	var kp := _jp(joints, s + "-knee")
	var ap := _jp(joints, s + "-ankle")
	var h2: Vector3 = (xf[_idx["pelvis"]] as Transform3D) * hp                      # the hip, carried by the pelvis
	var l1 := hp.distance_to(kp)
	var l2 := kp.distance_to(ap)
	var to_a := ap - h2
	var dist := clampf(to_a.length(), absf(l1 - l2) + 1.0e-4, l1 + l2 - 1.0e-4)
	var u := to_a.normalized()
	var a2 := ap if to_a.length() <= l1 + l2 - 1.0e-4 else h2 + u * dist          # the ankle, if the leg cannot reach it
	var a_len := (l1 * l1 - l2 * l2 + dist * dist) / (2.0 * dist)
	var hgt := sqrt(maxf(l1 * l1 - a_len * a_len, 0.0))
	var u0 := (ap - hp).normalized()
	var pole := (kp - hp) - u0 * (kp - hp).dot(u0)
	if pole.length() < 1.0e-4:
		pole = Vector3(0.0, 0.0, 1.0)
	pole = pole - u * pole.dot(u)
	pole = pole.normalized()
	var k2 := h2 + u * a_len + pole * hgt
	var q1 := Quaternion((kp - hp).normalized(), (k2 - h2).normalized())
	var q2 := Quaternion((ap - kp).normalized(), (a2 - k2).normalized())
	var b1 := Basis(q1)
	var b2 := Basis(q2)
	xf[iu] = Transform3D(b1, h2 - b1 * hp)
	xf[ik] = Transform3D(b2, k2 - b2 * kp)
	var plant := Transform3D(Basis(), a2 - ap)
	xf[ia] = plant
	if it >= 0:
		xf[it] = plant
