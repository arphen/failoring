# GPU conjugate gradient check (gdextension/src/gpu_cg.h): a hanging cotton patch and a tube draped on a sphere, each
# simulated twice with the fast solve (10 substeps, one linearised solve, block-Jacobi CG 100, 3% strain limit), once
# with the CPU CG and once with the GPU CG. The GPU solves in single precision, so the two runs drift apart slowly;
# the checks compare where the cloth ends up. Without a GPU device (headless, Compatibility renderer) it prints SKIP and
# passes. Run with a GPU renderer:
#   godot --path godot res://tests/gpu_check.tscn            (window; or under xvfb with --rendering-driver vulkan)
# Prints GPU lines and quits 0 when every check passes (or skips), 1 otherwise.
extends Node

var fails := 0


func _check(ok: bool, what: String) -> void:
	print("GPU %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


func _make(gpu: bool, scene: int) -> FailoringSim:
	var sim := FailoringSim.new()
	sim.load_materials("res://materials/fabrics.json")
	sim.set_param("substeps", 10)
	sim.set_param("newton_iterations", 1)
	sim.set_param("linear_solver", 1)
	sim.set_param("cg_block_jacobi", 1)
	sim.set_param("cg_max_iterations", 100)
	sim.set_param("strain_limit", 0.03)
	sim.set_param("body_friction_mode", 2)
	if gpu:
		sim.set_param("gpu", 1)
		sim.set_param("gpu_verify", 1)
	sim.begin()
	if scene == 0:  # 0.5 m patch pinned at two corners
		var p := sim.add_rect_panel(0.5, 0.5, 0.025, "cotton_poplin", Vector3(-0.25, 1.2, 0), Vector3.RIGHT, Vector3.BACK, 0.0, "patch")
		var first := sim.get_first_vertex(p)
		var pos := sim.get_positions()
		for v in sim.get_panel_vertex_count(p):
			var q: Vector3 = pos[first + v]
			if q.z > 0.499 and (q.x < -0.249 or q.x > 0.249):
				sim.pin_vertex(first + v, true)
	else:  # a flat square dropped on a sphere
		sim.add_sphere(Vector3(0, 0.8, 0), 0.2)
		sim.add_rect_panel(0.7, 0.7, 0.025, "cotton_poplin", Vector3(-0.35, 1.05, -0.35), Vector3.RIGHT, Vector3.BACK, 0.0, "drape")
	return sim


func _ready() -> void:
	var probe := _make(true, 0)
	probe.step(1)
	var st: Dictionary = probe.get_solver_stats()
	if not bool(st.get("gpu_ready", false)):
		print("GPU SKIP no GPU device: %s" % st.get("gpu_error", "?"))
		print("GPU PASSED (skipped)")
		get_tree().quit(0)
		return
	print("GPU device: %s" % st.get("gpu_device", "?"))
	for scene in 2:
		var frames := 90
		var cpu := _make(false, scene)
		var gpu := _make(true, scene)
		var t0 := Time.get_ticks_usec()
		cpu.step(frames)
		var t1 := Time.get_ticks_usec()
		gpu.step(frames)
		var t2 := Time.get_ticks_usec()
		var a := cpu.get_positions()
		var b := gpu.get_positions()
		var dmax := 0.0
		var dsum := 0.0
		var finite := true
		for i in a.size():
			var d := a[i].distance_to(b[i])
			dmax = maxf(dmax, d)
			dsum += d
			finite = finite and b[i].is_finite()
		var gs: Dictionary = gpu.get_solver_stats()
		var name: String = ["hanging patch", "drape on a sphere"][scene]
		print("GPU %s: %d verts, CPU %.1f ms/frame, GPU %.1f ms/frame (last solve %.2f ms, %d solves), CG its %d" % [name, a.size(),
			(t1 - t0) / 1000.0 / frames, (t2 - t1) / 1000.0 / frames, float(gs.get("gpu_last_ms", 0.0)), int(gs.get("gpu_solves", 0)), int(gs.get("cg_iterations", 0))])
		_check(finite, "%s: GPU positions finite" % name)
		_check(int(gs.get("gpu_solves", 0)) >= frames * 10, "%s: every substep solved on the GPU (%d)" % [name, int(gs.get("gpu_solves", 0))])
		# every solve against the same CG on the CPU, same iterations: in double precision (the exact answer of the
		# algorithm) and in single (what rounding alone does). The GPU must be about as accurate as the CPU float run.
		var rel := float(gs.get("gpu_verify_max_rel", 1.0))
		var ref := float(gs.get("gpu_verify_ref_rel", 1.0))
		var ratio := float(gs.get("gpu_verify_ratio", 99.0))
		print("GPU %s: worst solve vs double precision: GPU %s, CPU float %s of the solution; worst GPU/CPU-float error ratio %.2f" % [name,
			String.num_scientific(rel), String.num_scientific(ref), ratio])
		_check(ratio < 4.0, "%s: every GPU solve is as accurate as the CPU single-precision solve (ratio %.2f < 4)" % [name, ratio])
		_check(int(gs.get("gpu_verify_it_diff", 99)) == 0, "%s: the reference runs the same CG iterations (short by %d)" % [name, int(gs.get("gpu_verify_it_diff", 99))])
		# the whole run against the double-precision CPU CG: rounding grows in folds, so only where the cloth ends up
		print("GPU %s: after %d frames GPU and CPU (double) are %.2f cm apart on average, %.2f cm at most" % [name, frames, 100 * dsum / a.size(), 100 * dmax])
		_check(dsum / a.size() < 0.03, "%s: GPU and CPU runs end within 3 cm on average" % name)
		_check(absf(gpu.get_kinetic_energy() - cpu.get_kinetic_energy()) < 0.01 + 0.5 * cpu.get_kinetic_energy(), "%s: kinetic energy alike (CPU %s, GPU %s J)" % [name, String.num_scientific(cpu.get_kinetic_energy()), String.num_scientific(gpu.get_kinetic_energy())])
	print("GPU %s" % ("PASSED" if fails == 0 else "FAILED with %d failure(s)" % fails))
	get_tree().quit(0 if fails == 0 else 1)
