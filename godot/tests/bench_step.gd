# Benchmark of the editor's playback step: Tee (teagan) + Pencil skirt (penelope) + Chinos (charlie) on the average
# body, then a 24-frame walk after the settle frames (48 at Standard, 36 at Draft, 48 at Preview). The frames are produced by the editor's own functions
# in the synchronous order (job -> run -> collect), one after another, so the numbers are not paced by the timeline.
#   godot --headless --path godot res://tests/bench_step.tscn            (no rendering; the mesh upload is a dummy)
#   xvfb-run -a godot --path godot res://tests/bench_step.tscn           (rendering on, software GL; timing of the draw)
# Prints BENCH lines: per walk frame (mean over the walk frames) the body bake, the solver step, the copies in
# _run_step, the whole worker share, the main-thread share (job, collect, mesh, body pose) and solver counters,
# then a checksum of every frame's cloth positions (compare between builds: an unchanged checksum means unchanged
# numbers). Options by environment:
#   BENCH_TEE_ONLY=1      the tee alone (default: tee + pencil skirt + chinos)
#   BENCH_QUALITY=<key>   Simulation quality (default: the editor's default, preview)
#   BENCH_BODY_H=<m>      body grid spacing override (debug: 0.007 is the reference, 0.010 the draft/preview grid)
#   BENCH_SETTLE, BENCH_WALK   frames before the walk (48) and walk frames (24)
#   BENCH_DUMP=<path>     writes the last frame's cloth positions as text (one "x y z" per line)
#   BENCH_NO_LOOKAHEAD=1  makes every job without the next-body prefetch (the synchronous bake of the old order)
extends Node3D

const TIMEOUT_S := 900.0

var ed: Node
var phase := 0
var t_start := 0.0
var settle := 48
var walk := 24
var rows: Array = []  # per frame: dictionary of timings
var checksum := 0


func _ready() -> void:
	t_start = Time.get_ticks_msec() / 1000.0
	settle = int(OS.get_environment("BENCH_SETTLE")) if OS.has_environment("BENCH_SETTLE") else 48
	walk = int(OS.get_environment("BENCH_WALK")) if OS.has_environment("BENCH_WALK") else 24
	ed = load("res://editor.tscn").instantiate()
	add_child(ed)
	ed.playing = false
	if OS.has_environment("BENCH_QUALITY"):
		ed.quality = OS.get_environment("BENCH_QUALITY")
	if OS.has_environment("BENCH_BODY_H"):
		ed.body_h_override = float(OS.get_environment("BENCH_BODY_H"))


func _process(_delta: float) -> void:
	if Time.get_ticks_msec() / 1000.0 - t_start > TIMEOUT_S:
		print("BENCH FAIL timeout in phase %d" % phase)
		get_tree().quit(1)
		return
	if phase == 0 and ed.sim != null and not ed.outfit.is_empty():
		# the editor boots with the standard quality: rebuild once so that the quality and body settings above apply
		if OS.has_environment("BENCH_QUALITY") or OS.has_environment("BENCH_BODY_H"):
			ed.rebuild()
		# BENCH_TEE_ONLY: the tee alone (the editor boots with it); otherwise the tee, the pencil skirt and the chinos
		var want := 1
		if not OS.has_environment("BENCH_TEE_ONLY"):
			ed._on_library_add("fs-penelope")
			ed._on_library_add("fs-charlie")
			want = 3
		if ed.outfit.size() != want or ed.sim == null:
			print("BENCH FAIL the outfit did not build (%d garments)" % ed.outfit.size())
			get_tree().quit(1)
			return
		ed.playing = false
		phase = 1
	elif phase == 1:
		_run_all()
		_report()
		get_tree().quit(0)


func _usec() -> int:
	return Time.get_ticks_usec()


func _run_all() -> void:
	var n := settle + walk
	var prev_newton := 0
	var prev_ls := 0
	# the sim is rebuilt by _on_library_add: start from frame 0 of the new sim
	ed.frames.clear()
	ed.frame = 0
	ed.walk_k = 0.0
	ed._planned = {}
	ed._jobs_made = 0
	ed.target = n
	for i in n:
		var t0 := _usec()
		var job: Dictionary = ed._take_job() if not OS.has_environment("BENCH_NO_LOOKAHEAD") else ed._make_job()
		var t1 := _usec()
		var res: Dictionary = ed._run_step(job)
		var t2 := _usec()
		ed._collect(res)
		var t3 := _usec()
		var st: Dictionary = res["stats"]
		var fr: Dictionary = res["frame"]
		var row := {
			"walk": i >= settle,
			"job": (t1 - t0) / 1000.0,
			"run": float(res["total_ms"]),
			"bake": float(fr["bake"]),
			"solve": float(fr["ms"]),
			"collect": (t3 - t2) / 1000.0,
			"newton": int(st["newton_iterations"]) - prev_newton,
			"ls_fail": int(st["line_search_failures"]) - prev_ls,
		}
		prev_newton = int(st["newton_iterations"])
		prev_ls = int(st["line_search_failures"])
		# the main-thread share of playback: the mesh the editor builds for this frame and the body pose it shows
		if row["walk"]:
			var m0 := _usec()
			ed._set_cloth_mesh(fr["pos"], fr["normal"])
			var m1 := _usec()
			ed.body_node.show_pose(fr["body"])
			var m2 := _usec()
			row["mesh"] = (m1 - m0) / 1000.0
			row["body"] = (m2 - m1) / 1000.0
		rows.append(row)
		checksum = hash([checksum, fr["pos"], fr["cloth"], fr["body"]])


func _mean(key: String) -> float:
	var s := 0.0
	var c := 0
	for r in rows:
		if r["walk"] and r.has(key):
			s += float(r[key])
			c += 1
	return s / maxf(1.0, float(c))


func _report() -> void:
	var sim: FailoringSim = ed.sim
	var names := []
	for e in ed.outfit:
		names.append(String(e["name"]))
	print("BENCH garments %s, body average, quality %s, body grid %.0f mm, settle %d, walk %d frames, %s"
		% [" + ".join(names), ed.quality, float(ed.QUALITIES[ed._qual_key()]["body_h"]) * 1000.0, settle, walk,
			"rendering" if DisplayServer.get_name() != "headless" else "headless"])
	print("BENCH mesh %d verts, %d triangles, %d panels" % [sim.get_vertex_count(), sim.get_triangle_count(), sim.get_panel_count()])
	var worker := _mean("run")
	print("BENCH walk mean ms/frame: bake %.1f | solve %.1f | copies %.1f | worker total %.1f | job %.1f | collect %.1f | mesh %.1f | body %.1f"
		% [_mean("bake"), _mean("solve"), worker - _mean("bake") - _mean("solve"), worker, _mean("job"), _mean("collect"), _mean("mesh"), _mean("body")])
	var newton := 0
	var lsf := 0
	for r in rows:
		if r["walk"]:
			newton += int(r["newton"])
			lsf += int(r["ls_fail"])
	print("BENCH walk newton iterations per frame %.1f, line-search failures %d over %d frames" % [float(newton) / walk, lsf, walk])
	print("BENCH walk sim frames per second (worker only) %.1f, with main thread serial %.1f"
		% [1000.0 / maxf(0.001, worker), 1000.0 / maxf(0.001, worker + _mean("job") + _mean("collect") + _mean("mesh") + _mean("body"))])
	print("BENCH settle mean ms/frame: bake %.1f | solve %.1f | run %.1f" % [_mean_settle("bake"), _mean_settle("solve"), _mean_settle("run")])
	print("BENCH checksum %d" % checksum)
	if OS.has_environment("BENCH_DUMP") and not ed.frames.is_empty():
		# the cloth positions of the last frame, for comparing two builds or two settings (tools in the PERF notes)
		var f := FileAccess.open(OS.get_environment("BENCH_DUMP"), FileAccess.WRITE)
		for v in ed.frames[-1]["cloth"]:
			f.store_line("%.9f %.9f %.9f" % [v.x, v.y, v.z])
		f.close()
	ed.frames.clear()


func _mean_settle(key: String) -> float:
	var s := 0.0
	var c := 0
	for r in rows:
		if not r["walk"] and r.has(key):
			s += float(r[key])
			c += 1
	return s / maxf(1.0, float(c))
