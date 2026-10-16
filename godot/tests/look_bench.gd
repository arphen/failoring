# Frame time of the studio with the mannequin, per quality level (look.gd QUALITY), vsync off:
#   godot --path godot --script res://tests/look_bench.gd -- [width height]
# Renders in the main window (default 1000 x 1250 px, about the editor's 3D pane on a Retina screen) from the editor's default
# camera and prints, per level, the frames per second over 240 frames and the GPU time of the viewport.
extends SceneTree

const Look = preload("res://look.gd")
const Studio = preload("res://studio.gd")
const SkinMap = preload("res://skin.gd")
const BodyGen = preload("res://body_gen.gd")
const BodyView = preload("res://scripts/body_loader.gd")
const PlanLoader = preload("res://plan_loader.gd")
const Idle = preload("res://idle.gd")

var _vp: Viewport
var _env: Environment
var _key: Light3D
var _cam: Camera3D
var _mirror_cam := false
var _mat: Material
# level, then effects to switch off again after the level is applied (to see what each one costs)
var _levels := ["low", "medium", "high"]
var _li := 0
var _frame := 0
var _t0 := 0
var _gpu := 0.0
var _gpu_n := 0
const WARM := 240
const RUN := 240


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	var sz := Vector2i(int(args[0]) if args.size() > 1 else 1000, int(args[1]) if args.size() > 1 else 1250)
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	DisplayServer.window_set_size(sz)
	_vp = null
	_vp = root
	var stage := Node3D.new()
	root.add_child(stage)
	var we := WorldEnvironment.new()
	_env = Environment.new()
	we.environment = _env
	Look.environment(_env)
	stage.add_child(we)
	var st: Dictionary = Studio.build(stage)
	_env.sky = st["sky"]
	_key = st["key"]
	stage.add_child(Look.contact_shadow())
	var cam := Camera3D.new()
	_cam = cam
	Look.camera(cam)
	stage.add_child(cam)
	if OS.has_environment("FL_BENCH_MIRROR"):       # the camera in front of the fitting mirror, the three reflections on
		var mp := Studio.MIRROR_POS
		cam.look_at_from_position(Vector3(mp.x + 1.8, 1.4, mp.z + 1.9), Vector3(mp.x, 1.0, mp.z))
		cam.fov = 46.0
		Look.mirror_setup(stage, cam, root.world_3d, _env)
		_mirror_cam = true
	else:
		cam.look_at_from_position(Vector3(1.2, 1.3, 3.9), Vector3(0.0, 0.9, 0.0))
	Look.focus(cam.position.distance_to(Vector3(0.0, 0.9, 0.0)))
	Look.reflection_probe(stage)
	Look.post(_vp)
	var M := BodyGen.load_data(PlanLoader.load_json("res://assets/body_data.json"))
	var rest := BodyGen.generate(M, BodyGen.DEFAULT_MEAS.duplicate(), null)
	var stand := BodyGen.stand(M, rest)
	var pos: PackedFloat32Array = stand["pos"]
	var v := PackedVector3Array()
	v.resize(pos.size() / 3)
	for i in v.size():
		v[i] = Vector3(pos[3 * i], pos[3 * i + 1], pos[3 * i + 2])
	var body := BodyView.new()
	body.material_override = Look.body_material()
	body.layers = 2
	body.gi_mode = GeometryInstance3D.GI_MODE_DYNAMIC
	stage.add_child(body)
	body.use_custom(v, M["tris"])
	body.set_skin_data(SkinMap.build(M))
	body.set_idle(Idle.new(), stand["joints"], M["bones"])      # the idle layer runs during the measurement
	_mat = body.material_override
	RenderingServer.viewport_set_measure_render_time(root.get_viewport_rid(), true)
	print("BENCH ", Engine.get_version_info()["string"], "  ", RenderingServer.get_video_adapter_name(), "  window ", sz, " (the window actually is ", DisplayServer.window_get_size(), " px)")
	_apply()
	process_frame.connect(_tick)


func _apply() -> void:
	var parts: PackedStringArray = (_levels[_li] as String).split(" ")
	Look.apply_quality(parts[0], _env, _key, _vp, _mat)
	for fx in parts.slice(1):
		match fx:
			"nogi": _env.sdfgi_enabled = false
			"nossr": _env.ssr_enabled = false
			"nossil": _env.ssil_enabled = false
			"nossao": _env.ssao_enabled = false
			"noglow": _env.glow_enabled = false
			"nodof": _cam.attributes = null
			"nomsaa": _vp.msaa_3d = Viewport.MSAA_DISABLED
			"notaa": _vp.use_taa = false
			"nosss": (_mat as ShaderMaterial).set_shader_parameter("sss", 0.0)
			"noprobe": Look._probe.visible = false
	_frame = 0
	_gpu = 0.0
	_gpu_n = 0


func _tick() -> void:
	_frame += 1
	if _mirror_cam:
		Look.mirror_update()
	if _frame == WARM:
		_t0 = Time.get_ticks_usec()
	if _frame > WARM:
		_gpu += RenderingServer.viewport_get_measured_render_time_gpu(_vp.get_viewport_rid())
		_gpu_n += 1
	if _frame == WARM + RUN:
		var dt := float(Time.get_ticks_usec() - _t0) / 1.0e6
		print("BENCH %-14s %6.1f fps  %5.2f ms/frame   viewport GPU %5.2f ms" % [_levels[_li], float(RUN) / dt, dt * 1000.0 / float(RUN), _gpu / float(maxi(_gpu_n, 1))])
		_li += 1
		if _li >= _levels.size():
			quit(0)
			return
		_apply()
