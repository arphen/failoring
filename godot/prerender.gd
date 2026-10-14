# Pre-rendering of showcase scenarios (docs/SCENARIOS.md). A render builds the garment exactly as the editor's rebuild()
# does (PlanLoader.build), settles it on the standing body (the Sew result: gravity ramped in over the ramp frames, as
# the editor's walk does), then runs the scenario one solver step per 1/60 s with the body pose and the props set before
# each step. The run is one thread: `start` returns at once, `poll` reports progress, `cancel` stops it between steps.
# Every frame keeps the display positions and normals (float16 on disk, see save/load). Playback (scenario_panel.gd)
# replays those frames with no solver.
#
# The cache key (key_for) covers everything the result depends on: the plan (pieces, seams), the fabric, the simulation
# quality, the body (its measurements), the scenario and the solver settings. Files: user://showcase/<key>.bin.
class_name ShowcasePrerender
extends RefCounted

const PlanLoader = preload("res://plan_loader.gd")
const MAGIC := "FLSHOW01"
const VERSION := 2          # 2: display mesh with two subdivision levels (more vertices). Bump when the solver setup, the scenario data or the file format changes
const LEAD := 0.5           # seconds the body moves from the standing pose into the scenario's first pose
const SIM_DT := 1.0 / 60.0
const DISPLAY_MARGIN := 0.0008
const DIR := "user://showcase"

var _thread: Thread
var _mutex := Mutex.new()
var _progress := 0.0
var _stage := "idle"
var _cancel := false
var _done := false
var _error := ""
var _result: Dictionary = {}
var _started_ms := 0


# The cache key of a render: everything its frames depend on.
static func key_for(inp: Dictionary, sc: Dictionary) -> String:
	var text := JSON.stringify({"v": VERSION, "plan": inp["plan"], "fabric": inp["fabric_id"], "quality": inp["quality_key"],
		"body": inp["body_key"], "collide": inp["collide"], "seam": inp["seam_stiffness"], "ramp": inp["ramp_frames"],
		"scenario": sc["id"], "duration": sc["duration"], "frames": sc["frames"]})
	return text.sha256_text()


static func file_for(key: String) -> String:
	return "%s/%s.bin" % [DIR, key]


# Starts a render on a thread. job: {inputs (see editor.gd showcase_inputs), scenario, max_steps (optional, tests)}.
func start(job: Dictionary) -> bool:
	if _thread != null:
		return false
	_mutex.lock()
	_progress = 0.0
	_stage = "build"
	_cancel = false
	_done = false
	_error = ""
	_result = {}
	_mutex.unlock()
	_started_ms = Time.get_ticks_msec()
	_thread = Thread.new()
	_thread.start(work.bind(job))
	return true


func poll() -> Dictionary:
	_mutex.lock()
	var d := {"progress": _progress, "stage": _stage, "done": _done, "error": _error, "seconds": (Time.get_ticks_msec() - _started_ms) / 1000.0}
	_mutex.unlock()
	return d


func cancel() -> void:
	_mutex.lock()
	_cancel = true
	_mutex.unlock()


# Joins the thread (after it is done or cancelled) and returns the result: {} when it failed or was cancelled.
func finish() -> Dictionary:
	if _thread != null:
		_thread.wait_to_finish()
		_thread = null
	return _result


func is_running() -> bool:
	return _thread != null and not poll()["done"]


func _report(stage: String, progress: float) -> void:
	_mutex.lock()
	_stage = stage
	_progress = progress
	_mutex.unlock()


func _is_cancelled() -> bool:
	_mutex.lock()
	var c := _cancel
	_mutex.unlock()
	return c


func _fail(msg: String) -> Dictionary:
	_mutex.lock()
	_error = msg
	_done = true
	_mutex.unlock()
	return {}


# The render itself (runs on the thread; also callable directly, which is what the tests do).
func work(job: Dictionary) -> Dictionary:
	var sc: Dictionary = job["scenario"]
	var inp: Dictionary = job["inputs"]
	var q: Dictionary = inp["quality"]
	var max_steps := int(job.get("max_steps", -1))
	var ramp := int(inp["ramp_frames"])
	_report("build", 0.0)
	var built: Dictionary = PlanLoader.build(inp["plan"], inp["sections"], inp["arms"], inp["stand"], inp["tris"],
		String(inp["fabric_id"]), q, float(inp["seam_stiffness"]), ramp, bool(inp["collide"]))
	if built.is_empty():
		return _fail("the garment could not be built")
	var sim: FailoringSim = built["sim"]
	var disp_idx := sim.get_display_indices()
	var pose := ShowcaseScenarios.Pose.new(inp["M"], inp["body_rest"])
	var stand: PackedVector3Array = inp["stand"]

	# 1. settle on the standing body (what the Sew button does in the editor)
	var settle: int = int(q["frames"])
	for f in range(1, settle + 1):
		if _is_cancelled():
			return _fail("cancelled")
		if ramp > 0 and f <= ramp + 1:
			sim.set_param("gravity_y", -9.81 * minf(1.0, pow(float(f) / float(ramp), 2.0)))
		sim.step(1)
		_report("settle", float(f) / float(settle) * 0.25)

	# 2. props: the scenario's boxes and chair parts are colliders of the solver
	var props := ShowcaseScenarios.prop_boxes(sc, pose, 0.0, {"l-hand": Vector3(), "r-hand": Vector3()})
	var moving := []   # indices (in props) of the boxes that move
	for i in props.size():
		sim.add_box_prop(props[i]["transform"], props[i]["half"])
		if String(props[i]["name"]) == "box":
			moving.append(i)

	# 3. the scenario: lead-in (body from the standing pose into the first pose), then the motion
	var lead_steps := int(round(LEAD / SIM_DT))
	var n_steps := int(round(float(sc["duration"]) / SIM_DT))
	var total := lead_steps + n_steps
	if max_steps >= 0:
		total = mini(total, max_steps)
	var first := ShowcaseScenarios.sample(sc, 0.0)
	var pose0 := pose.evaluate(first["rot"], first["root"])
	var pos0: PackedVector3Array = pose0["verts"]
	var frames := []
	var nverts := 0
	for k in total:
		if _is_cancelled():
			return _fail("cancelled")
		var t := float(k - lead_steps) * SIM_DT
		var verts: PackedVector3Array
		var joints := {}
		if t < 0.0:
			var u := ShowcaseScenarios.smooth_step((t + LEAD) / LEAD)
			verts = PackedVector3Array()
			verts.resize(stand.size())
			for i in stand.size():
				verts[i] = stand[i].lerp(pos0[i], u)
		else:
			var s := ShowcaseScenarios.sample(sc, t)
			var P := pose.evaluate(s["rot"], s["root"])
			verts = P["verts"]
			joints = P["joints"]
		sim.set_body_positions(verts)
		if not joints.is_empty():
			var centers := ShowcaseScenarios.prop_boxes(sc, pose, t, joints)
			for i in moving:
				sim.set_box_prop(i, centers[i]["transform"])
		sim.step(1)
		var disp := sim.get_display_positions(DISPLAY_MARGIN)
		var nrm := sim.get_display_normals(disp)
		nverts = disp.size()
		var box := Vector3.ZERO
		if not moving.is_empty():
			box = props[moving[0]]["transform"].origin if joints.is_empty() else pose.box_center(sc, t, joints)
		frames.append({"pos": disp.to_byte_array().to_float32_array(), "nrm": nrm.to_byte_array().to_float32_array(), "box": box, "t": t})
		_report("run", 0.25 + 0.75 * float(k + 1) / float(total))
	_mutex.lock()
	_result = {"frames": frames, "disp_idx": disp_idx, "lead": lead_steps, "fps": 1.0 / SIM_DT, "scenario": String(sc["id"]),
		"quality": String(inp["quality_key"]), "vertices": nverts, "done_steps": total, "key": key_for(inp, sc)}
	_done = true
	_progress = 1.0
	_stage = "done"
	_mutex.unlock()
	return _result


# ---------------------------------------------------------------- disk
# Layout: "FLSHOW01" | u32 LE header length | header JSON | index int32 (3 per triangle) | per frame: positions float16
# (3 per display vertex), normals float16, box centre float32 (3). Little endian.
static func save(path: String, result: Dictionary, meta: Dictionary) -> bool:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var frames: Array = result["frames"]
	var nv: int = (frames[0]["pos"] as PackedFloat32Array).size() / 3 if not frames.is_empty() else 0
	var idx: PackedInt32Array = result["disp_idx"]
	var header := meta.duplicate()
	header.merge({"format": "failoring-showcase-render", "version": VERSION, "frames": frames.size(), "vertices": nv,
		"triangles": idx.size() / 3, "lead": int(result["lead"]), "fps": float(result["fps"]), "scenario": String(result["scenario"]),
		"quality": String(result["quality"]), "key": String(result["key"]), "created": Time.get_unix_time_from_system()})
	var hbytes := JSON.stringify(header).to_utf8_buffer()
	var out := PackedByteArray(MAGIC.to_ascii_buffer())
	var len_bytes := PackedByteArray()
	len_bytes.resize(4)
	len_bytes.encode_u32(0, hbytes.size())
	out.append_array(len_bytes)
	out.append_array(hbytes)
	out.append_array(idx.to_byte_array())
	var enc := FailoringSim.new()
	for f in frames:
		out.append_array(enc.encode_half(f["pos"]))
		out.append_array(enc.encode_half(f["nrm"]))
		var b: Vector3 = f["box"]
		out.append_array(PackedFloat32Array([b.x, b.y, b.z]).to_byte_array())
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		push_error("showcase: cannot write " + path)
		return false
	file.store_buffer(out)
	file.close()
	return true


# The header of a saved render (cheap: no frames), or {} when the file is not one.
static func read_header(path: String) -> Dictionary:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null or f.get_length() < 12:
		return {}
	var magic := f.get_buffer(8).get_string_from_ascii()
	if magic != MAGIC:
		return {}
	var hl := f.get_32()
	var h = JSON.parse_string(f.get_buffer(hl).get_string_from_utf8())
	f.close()
	return h if h is Dictionary else {}


# The whole render from disk: {frames: [{pos, nrm (flat float32), box}], disp_idx, lead, fps, scenario, quality, key}.
static func load(path: String) -> Dictionary:
	var bytes := FileAccess.get_file_as_bytes(path)
	if bytes.size() < 12 or bytes.slice(0, 8).get_string_from_ascii() != MAGIC:
		return {}
	var hl := bytes.decode_u32(8)
	var h = JSON.parse_string(bytes.slice(12, 12 + hl).get_string_from_utf8())
	if not (h is Dictionary) or String(h.get("format", "")) != "failoring-showcase-render" or int(h.get("version", 0)) != VERSION:
		return {}
	var o := 12 + hl
	var nt := int(h["triangles"]) * 3
	var idx := bytes.slice(o, o + 4 * nt).to_int32_array()
	o += 4 * nt
	var nv := int(h["vertices"])
	var dec := FailoringSim.new()
	var frames := []
	for _i in int(h["frames"]):
		var pos := dec.decode_half(bytes, o, 3 * nv)
		o += 6 * nv
		var nrm := dec.decode_half(bytes, o, 3 * nv)
		o += 6 * nv
		var bx := bytes.slice(o, o + 12).to_float32_array()
		o += 12
		frames.append({"pos": pos, "nrm": nrm, "box": Vector3(bx[0], bx[1], bx[2])})
	return {"frames": frames, "disp_idx": idx, "lead": int(h["lead"]), "fps": float(h["fps"]), "scenario": String(h["scenario"]),
		"quality": String(h["quality"]), "key": String(h["key"]), "header": h}


# Renders on disk: one row per file in user://showcase, newest first: {path, key, scenario, quality, frames, created, size}.
static func list_saved() -> Array:
	var out := []
	var da := DirAccess.open(DIR)
	if da == null:
		return out
	for name in da.get_files():
		if not name.ends_with(".bin"):
			continue
		var p := "%s/%s" % [DIR, name]
		var h := read_header(p)
		if h.is_empty():
			continue
		var fa := FileAccess.open(p, FileAccess.READ)
		var size := fa.get_length() if fa != null else 0
		out.append({"path": p, "key": String(h.get("key", "")), "scenario": String(h.get("scenario", "")),
			"quality": String(h.get("quality", "")), "frames": int(h.get("frames", 0)), "created": int(h.get("created", 0)),
			"size": size, "name": String(h.get("name", ""))})
	out.sort_custom(func(a, b): return a["created"] > b["created"])
	return out
