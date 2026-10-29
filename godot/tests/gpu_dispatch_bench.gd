# GPU dispatch bench (docs/GPU_SOLVER_NOTES.md, "whole substep on the GPU", phase 0). Two numbers decide the layout of
# the GPU substep:
#   1. the cost of one compute dispatch inside one compute list (GPU time, submit to sync), for a tiny kernel over
#      N_ITEMS floats with a buffer barrier after each dispatch, as the CG iterations would chain them;
#   2. whether a local RenderingDevice created, used and freed on a worker Thread survives FRAMES frames (Render mode
#      simulates on a worker thread), and whether one made and used on a worker survives being freed on the main thread
#      (the editor builds sims on the main thread and steps them on a worker).
# Prints BENCH lines and quits 0. Needs a window (Forward+): godot --path godot res://tests/gpu_dispatch_bench.tscn
extends Node

const N_ITEMS := 4096          # one float per thread: a cloth of a few thousand vertices
const DISPATCHES := [1, 100, 1000, 3000]
const REPEAT := 5
const FRAMES := 1000           # worker-thread run: frames of 30 dispatches each

const SHADER := """
#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0, std430) restrict buffer A { float a[]; };
layout(push_constant, std430) uniform P { int n; int k; int p0; int p1; } pc;
void main() {
	uint i = gl_GlobalInvocationID.x;
	if (i >= uint(pc.n)) return;
	a[i] = a[i] * 0.999 + float(pc.k) * 1e-6;
}
"""


class Bench:
	var rd: RenderingDevice
	var shader: RID
	var pipe: RID
	var buf: RID
	var uset: RID

	func make() -> String:
		rd = RenderingServer.create_local_rendering_device()
		if rd == null:
			return "no local RenderingDevice"
		var src := RDShaderSource.new()
		src.language = RenderingDevice.SHADER_LANGUAGE_GLSL
		src.source_compute = SHADER
		var spirv := rd.shader_compile_spirv_from_source(src)
		if spirv.compile_error_compute != "":
			return spirv.compile_error_compute
		shader = rd.shader_create_from_spirv(spirv)
		pipe = rd.compute_pipeline_create(shader)
		var data := PackedFloat32Array()
		data.resize(N_ITEMS)
		data.fill(1.0)
		buf = rd.storage_buffer_create(N_ITEMS * 4, data.to_byte_array())
		var u := RDUniform.new()
		u.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
		u.binding = 0
		u.add_id(buf)
		uset = rd.uniform_set_create([u], shader, 0)
		return ""

	# Records n dispatches in one compute list, submits, waits. Returns [record ms, submit-to-sync ms].
	func run(n: int) -> Array:
		var t0 := Time.get_ticks_usec()
		var cl := rd.compute_list_begin()
		rd.compute_list_bind_compute_pipeline(cl, pipe)
		rd.compute_list_bind_uniform_set(cl, uset, 0)
		var pc := PackedInt32Array([N_ITEMS, 0, 0, 0])
		for k in n:
			pc[1] = k
			rd.compute_list_set_push_constant(cl, pc.to_byte_array(), 16)
			rd.compute_list_dispatch(cl, (N_ITEMS + 63) / 64, 1, 1)
			rd.compute_list_add_barrier(cl)
		rd.compute_list_end()
		var t1 := Time.get_ticks_usec()
		rd.submit()
		rd.sync()
		var t2 := Time.get_ticks_usec()
		return [(t1 - t0) / 1000.0, (t2 - t1) / 1000.0]

	func release() -> void:
		if rd == null:
			return
		for r in [uset, buf, pipe, shader]:
			if r.is_valid():
				rd.free_rid(r)
		rd.free()
		rd = null


var _thread_result := ""


func _ready() -> void:
	var b := Bench.new()
	var err := b.make()
	if err != "":
		print("BENCH SKIP ", err)
		get_tree().quit(0)
		return
	print("BENCH device: ", b.rd.get_device_name())
	b.run(10)  # warm-up
	var base_gpu := 0.0
	for n in DISPATCHES:
		var rec := 1e9
		var gpu := 1e9
		for r in REPEAT:
			var t: Array = b.run(n)
			rec = minf(rec, t[0])
			gpu = minf(gpu, t[1])
		if n == 1:
			base_gpu = gpu
		var per := (gpu - base_gpu) / float(maxi(n - 1, 1)) * 1000.0
		print("BENCH dispatches=%d record=%.2f ms gpu(submit..sync)=%.2f ms -> %.2f us/dispatch GPU, %.2f us/dispatch recording" % [
			n, rec, gpu, per, rec / float(n) * 1000.0])
	b.release()
	# worker thread: device made, used and freed on the thread while the main thread keeps drawing
	var th := Thread.new()
	th.start(_worker)
	while th.is_alive():
		await get_tree().process_frame
	th.wait_to_finish()
	print("BENCH worker thread: ", _thread_result)
	# cross-thread: the device made and used on a worker, freed on the main thread while it draws (the editor's pattern)
	for round in 20:
		var th2 := Thread.new()
		th2.start(_worker_keep)
		while th2.is_alive():
			await get_tree().process_frame
		th2.wait_to_finish()
		_kept.release()
		_kept = null
		await get_tree().process_frame
	print("BENCH cross-thread: ok, 20 devices made on a worker and freed on the main thread")
	get_tree().quit(0)


var _kept: Bench


func _worker_keep() -> void:
	var b := Bench.new()
	if b.make() == "":
		for f in 100:
			b.run(30)
	_kept = b


func _worker() -> void:
	var b := Bench.new()
	var err := b.make()
	if err != "":
		_thread_result = "SKIP " + err
		return
	var t0 := Time.get_ticks_msec()
	for f in FRAMES:
		b.run(30)
	_thread_result = "ok, %d frames of 30 dispatches in %d ms on the worker" % [FRAMES, Time.get_ticks_msec() - t0]
	b.release()
