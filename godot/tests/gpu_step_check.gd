# Whole-substep GPU check (gdextension/src/gpu_step.h): each scene is simulated twice with the fast solve (10 substeps,
# one linearised solve, block-Jacobi CG 100, 3 % strain limit, friction mode 2; self collision in the last two scenes),
# once on the CPU and once with whole frames on the GPU (set_param("gpu_step", 1)). The GPU runs in single precision, so the runs drift
# apart in folds; the checks compare where the cloth ends up (against a control: the CPU with gravity 1e-6 stronger),
# how much it moves, the seams and body clearance, and whether the layers stay apart.
# Scenes: a patch pinned at two corners, a square dropped on a sphere, the A-line skirt on the mannequin (Standard mesh),
# the tiered skirt at the Photo mesh size with self collision, two layers dropped on a sphere (self collision).
# Without a GPU device it prints SKIP and passes. Needs a window (Forward+):
#   godot --path godot res://tests/gpu_step_check.tscn
# FL_SCENES=0,1 restricts the scenes. Prints GPUSTEP lines and quits 0 when every check passes, 1 otherwise.
extends Node

const BodyGen = preload("res://body_gen.gd")
const PlanLoader = preload("res://plan_loader.gd")

const FRAMES := 90
var fails := 0


func _check(ok: bool, what: String) -> void:
	print("GPUSTEP %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


func _fast(sim: FailoringSim) -> void:
	sim.set_param("substeps", 10)
	sim.set_param("newton_iterations", 1)
	sim.set_param("linear_solver", 1)
	sim.set_param("cg_block_jacobi", 1)
	sim.set_param("cg_max_iterations", 100)
	sim.set_param("strain_limit", 0.03)
	sim.set_param("body_friction_mode", 2)
	sim.set_param("tension_stiffness", 1)
	sim.set_param("self_collision", 0)


func _make(gpu: bool, scene: int, body: Dictionary, twin := false) -> FailoringSim:
	var sim := FailoringSim.new()
	sim.load_materials("res://materials/fabrics.json")
	_fast(sim)
	if twin:
		sim.set_param("gravity_y", -9.81 * (1.0 + 1e-6))
	if gpu:
		sim.set_param("gpu_step", 1)
	sim.begin()
	if scene == 0:  # 0.5 m patch pinned at two corners
		var p := sim.add_rect_panel(0.5, 0.5, 0.025, "cotton_poplin", Vector3(-0.25, 1.2, 0), Vector3.RIGHT, Vector3.BACK, 0.0, "patch")
		var first := sim.get_first_vertex(p)
		var pos := sim.get_positions()
		for v in sim.get_panel_vertex_count(p):
			var q: Vector3 = pos[first + v]
			if q.z > 0.499 and (q.x < -0.249 or q.x > 0.249):
				sim.pin_vertex(first + v, true)
	elif scene == 4:  # two squares 8 mm apart dropped on the sphere: self collision keeps the layers apart
		sim.set_param("self_collision", 0 if OS.has_environment("FL_NOSELF") else 1)
		sim.add_plane(Vector3.ZERO, Vector3.UP)
		sim.add_sphere(Vector3(0, 0.8, 0), 0.2)
		sim.add_rect_panel(0.6, 0.6, 0.02, "cotton_poplin", Vector3(-0.30, 1.020, -0.30), Vector3.RIGHT, Vector3.BACK, 0.0, "under")
		sim.add_rect_panel(0.6, 0.6, 0.02, "cotton_poplin", Vector3(-0.27, 1.028, -0.33), Vector3.RIGHT, Vector3.BACK, 0.3, "over")
	elif scene == 1:  # a flat square dropped on a sphere, floor below
		sim.add_plane(Vector3.ZERO, Vector3.UP)
		sim.add_sphere(Vector3(0, 0.8, 0), 0.2)
		sim.add_rect_panel(0.7, 0.7, 0.025, "cotton_poplin", Vector3(-0.35, 1.05, -0.35), Vector3.RIGHT, Vector3.BACK, 0.0, "drape")
	else:  # a skirt on the standing mannequin (the editor's build: mesh body, floor, seams): A-line at Standard (scene 2),
		# tiered at the Photo mesh size (scene 3)
		var key := "aline" if scene == 2 else "tiered"
		var edge := 0.04 if scene == 2 else (float(OS.get_environment("FL_EDGE")) if OS.has_environment("FL_EDGE") else 0.025)
		var plan: Dictionary = PlanLoader.normalize_plan((preload("res://builtin_plans.gd").all()[key] as Dictionary).duplicate(true))
		sim = null
		var os_fast := OS.get_environment("FL_FAST")
		OS.set_environment("FL_FAST", "1")
		var built: Dictionary = PlanLoader.build({"pieces": plan["pieces"], "seams": [] if OS.has_environment("FL_NOSEAM") else plan["seams"]}, body["stand"]["sections"], body["stand"]["arms"],
			body["pos"], body["M"]["tris"], String(plan["fabric"]), {"edge": edge, "sub": 2, "newton": 8, "frames": 48, "body_h": 0.007}, 20000.0, 20, false)
		OS.set_environment("FL_FAST", os_fast)
		sim = built["sim"]
		# scene 3 is the Photo setting: self collision on
		sim.set_param("self_collision", 1 if scene == 3 and not OS.has_environment("FL_NOSELF") else 0)
		if twin:
			sim.set_param("gravity_y", -9.81 * (1.0 + 1e-6))
		if OS.has_environment("FL_NOAUTO"):
			sim.set_param("hessian_auto", 0)
		if OS.has_environment("FL_NOFRIC"):
			sim.set_param("body_friction_mode", 1)
		if OS.has_environment("FL_NOSTRAIN"):
			sim.set_param("strain_limit", 0.0)
		if gpu:
			sim.set_param("gpu_step", 1)
	return sim


# Scene 5, a box prop on the GPU (scenario_check.gd _check_props_native with the fast solve): a sheet falls onto a box
# and rests on it, then the box rises 0.4 m and carries it; no cloth vertex may end up inside the box.
func _check_box() -> void:
	var sim := FailoringSim.new()
	sim.load_materials("res://materials/fabrics.json")
	_fast(sim)
	sim.set_param("gpu_step", 1)
	sim.add_plane(Vector3.ZERO, Vector3.UP)
	sim.begin()
	sim.add_rect_panel(0.5, 0.5, 0.05, "cotton_poplin", Vector3(-0.25, 0.5, -0.25), Vector3.RIGHT, Vector3.BACK, 0.0, "sheet")
	var c := Vector3(0, 0.15, 0)
	var half := Vector3(0.15, 0.15, 0.15)
	var bi := sim.add_box_prop(Transform3D(Basis.IDENTITY, c), half)
	var measure := func() -> Array:
		var inside := 0
		var top := -INF
		for p in sim.get_positions():
			var q := Vector3(absf(p.x - c.x) - half.x, absf(p.y - c.y) - half.y, absf(p.z - c.z) - half.z)
			if maxf(q.x, maxf(q.y, q.z)) < -0.002:
				inside += 1
			if absf(p.x) < 0.1 and absf(p.z) < 0.1:
				top = maxf(top, p.y)
		return [inside, top]
	sim.step(90)
	var r0: Array = measure.call()
	for i in 60:
		c.y += 0.4 / 60.0
		sim.set_box_prop(bi, Transform3D(Basis.IDENTITY, c))
		sim.step(1)
	var r1: Array = measure.call()
	var st: Dictionary = sim.get_solver_stats()
	_check(int(st.get("gpu_step_frames", 0)) == 150, "box prop: every frame on the GPU (%d; CPU reason: %s)" % [int(st.get("gpu_step_frames", 0)), st.get("gpu_step_cpu_reason", "")])
	_check(int(r0[0]) == 0 and float(r0[1]) > 0.28, "box prop: the sheet rests on the box (top %.3f, box top 0.300, inside %d)" % [r0[1], r0[0]])
	_check(int(r1[0]) == 0 and float(r1[1]) > float(r0[1]) + 0.15, "box prop: the rising box carries the sheet (top %.3f -> %.3f, inside %d)" % [r0[1], r1[1], r1[0]])


# Vertices of the second panel that lie under the first one: for each, the first panel's nearest vertex seen from
# above (within 1.5 cm horizontally) is above it by more than 1 mm.
func _crossings(sim: FailoringSim) -> int:
	var pos := sim.get_positions()
	var n0 := sim.get_panel_vertex_count(0)
	var f1 := sim.get_first_vertex(1)
	var count := 0
	for k in sim.get_panel_vertex_count(1):
		var q: Vector3 = pos[f1 + k]
		var best := 1e9
		var by := 0.0
		for j in n0:
			var p: Vector3 = pos[sim.get_first_vertex(0) + j]
			var d := Vector2(p.x - q.x, p.z - q.z).length()
			if d < best:
				best = d
				by = p.y
		if best < 0.015 and by > q.y + 0.001:
			count += 1
	return count


func _body() -> Dictionary:
	var M: Dictionary = BodyGen.load_data(PlanLoader.load_json("res://assets/body_data.json"))
	var rest = BodyGen.generate(M, (BodyGen.PRESETS["average"] as Dictionary).duplicate(), null)
	var stand: Dictionary = BodyGen.stand(M, rest)
	var p: PackedFloat32Array = stand["pos"]
	var pos := PackedVector3Array()
	pos.resize(p.size() / 3)
	for i in pos.size():
		pos[i] = Vector3(p[3 * i], p[3 * i + 1], p[3 * i + 2])
	return {"M": M, "stand": stand, "pos": pos}


func _ready() -> void:
	var probe := _make(true, 0, {})
	probe.step(1)
	var st: Dictionary = probe.get_solver_stats()
	if int(st.get("gpu_step_frames", 0)) < 1:
		print("GPUSTEP SKIP the GPU did not step: %s / %s" % [st.get("gpu_step_error", "?"), st.get("gpu_step_cpu_reason", "?")])
		print("GPUSTEP PASSED (skipped)" if String(st.get("gpu_step_error", "")).begins_with("no ") else "GPUSTEP FAILED")
		get_tree().quit(0 if String(st.get("gpu_step_error", "")).begins_with("no ") else 1)
		return
	print("GPUSTEP device: %s" % st.get("gpu_step_device", "?"))
	var raw := OS.get_environment("FL_SCENES")
	if raw == "" or raw.split(",").has("5"):
		_check_box()
	var scenes := [0, 1, 2, 3, 4] if raw == "" else Array(raw.split(",")).map(func(s: String) -> int: return int(s)).filter(func(k: int) -> bool: return k < 5)
	var body := _body() if scenes.has(2) or scenes.has(3) else {}
	for scene in scenes:
		var name: String = ["hanging patch", "drape on a sphere", "A-line skirt on the body", "tiered skirt, Photo mesh + self collision", "two layers on a sphere"][scene]
		var cpu := _make(false, scene, body)
		var gpu := _make(true, scene, body)
		# control: the CPU again with gravity 1e-6 stronger (about float rounding); how far that drifts is how chaotic
		# the scene is
		var twin := _make(false, scene, body, true)
		if OS.has_environment("FL_TRACE"):
			# divergence over time: CPU vs its perturbed twin, CPU vs GPU
			var done := 0
			for f in [1, 2, 5, 10, 20, 45, 90]:
				cpu.step(f - done); twin.step(f - done); gpu.step(f - done)
				done = f
				var pa := cpu.get_positions(); var pb := gpu.get_positions(); var pc := twin.get_positions()
				var s1 := 0.0; var s2 := 0.0
				for i in pa.size():
					s1 += pa[i].distance_to(pc[i]); s2 += pa[i].distance_to(pb[i])
				print("GPUSTEP trace %s frame %d: twin %.4f mm, GPU %.4f mm (avg)" % [name, f, 1000 * s1 / pa.size(), 1000 * s2 / pa.size()])
			continue
		twin.step(FRAMES)
		var t0 := Time.get_ticks_usec()
		cpu.step(FRAMES)
		var t1 := Time.get_ticks_usec()
		var gms := 0.0
		var cg := 0
		var rej := 0
		for f in FRAMES:
			gpu.step(1)
			var gs: Dictionary = gpu.get_solver_stats()
			gms += float(gs.get("gpu_step_last_gpu_ms", 0.0))
			cg += int(gs.get("gpu_step_cg_iterations", 0))
			rej += int(gs.get("gpu_step_ls_rejects", 0))
		var t2 := Time.get_ticks_usec()
		var gs: Dictionary = gpu.get_solver_stats()
		var a := cpu.get_positions()
		var b := gpu.get_positions()
		var c := twin.get_positions()
		var tsum := 0.0
		for i in a.size():
			tsum += a[i].distance_to(c[i])
		var dmax := 0.0
		var dsum := 0.0
		var finite := true
		var ylo_a := 1e9
		var ylo_b := 1e9
		for i in a.size():
			var d := a[i].distance_to(b[i])
			dmax = maxf(dmax, d)
			dsum += d
			finite = finite and b[i].is_finite()
			ylo_a = minf(ylo_a, a[i].y)
			ylo_b = minf(ylo_b, b[i].y)
		print("GPUSTEP %s: %d verts, CPU %.1f ms/frame, GPU %.1f ms/frame (GPU busy %.1f ms, %d dispatches), CG its/frame %.0f, failed line searches %d" % [
			name, a.size(), (t1 - t0) / 1000.0 / FRAMES, (t2 - t1) / 1000.0 / FRAMES, gms / FRAMES, int(gs.get("gpu_step_dispatches", 0)), float(cg) / FRAMES, rej])
		print("GPUSTEP %s: apart %.2f cm avg, %.2f cm max; lowest point CPU %.3f GPU %.3f m; KE CPU %s GPU %s J; seam gap CPU %.2f GPU %.2f mm; clearance CPU %.2f GPU %.2f mm" % [
			name, 100 * dsum / a.size(), 100 * dmax, ylo_a, ylo_b, String.num_scientific(cpu.get_kinetic_energy()), String.num_scientific(gpu.get_kinetic_energy()),
			cpu.get_max_seam_gap() * 1000, gpu.get_max_seam_gap() * 1000, cpu.get_min_body_clearance() * 1000, gpu.get_min_body_clearance() * 1000])
		_check(int(gs.get("gpu_step_frames", 0)) == FRAMES, "%s: every frame on the GPU (%d; CPU reason: %s)" % [name, int(gs.get("gpu_step_frames", 0)), gs.get("gpu_step_cpu_reason", "")])
		_check(finite, "%s: GPU positions finite" % name)
		# the GPU may drift from the CPU as far as the CPU drifts from itself under the perturbation (x2), +1 cm
		var tol := 0.01 + 2.0 * tsum / a.size()
		print("GPUSTEP %s: control: CPU vs CPU with gravity x (1 + 1e-6) apart %.2f cm avg" % [name, 100 * tsum / a.size()])
		_check(dsum / a.size() < tol, "%s: GPU and CPU end within %.1f cm on average (1 cm + twice the control)" % [name, 100 * tol])
		_check(absf(ylo_a - ylo_b) < 0.02, "%s: lowest point within 2 cm" % name)
		_check(absf(gpu.get_kinetic_energy() - cpu.get_kinetic_energy()) < 0.01 + 0.5 * cpu.get_kinetic_energy(), "%s: kinetic energy alike" % name)
		if scene == 4:
			var ca := _crossings(cpu)
			var cb := _crossings(gpu)
			print("GPUSTEP %s: upper-layer vertices under the lower layer: CPU %d, GPU %d" % [name, ca, cb])
			_check(cb <= ca + 5, "%s: the GPU keeps the layers apart as well as the CPU (+5 vertices)" % name)
		if scene == 2 or scene == 3:
			_check(gpu.get_max_seam_gap() < cpu.get_max_seam_gap() + 0.002, "%s: seams as closed as on the CPU (+2 mm)" % name)
			_check(gpu.get_min_body_clearance() > -0.002, "%s: no vertex more than 2 mm inside the body" % name)
	print("GPUSTEP %s" % ("PASSED" if fails == 0 else "FAILED with %d failure(s)" % fails))
	get_tree().quit(0 if fails == 0 else 1)
