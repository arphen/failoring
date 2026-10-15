# Renders the mannequin in the studio, without the editor or the solver, for look work (skin, eyes, lighting):
#   godot --path godot --script res://tests/look_shot.gd -- <out_dir> [preset] [quality] [skin] [hair] [eyes]
# Needs a window (pixels): on a headless Linux machine run it under xvfb-run. Writes <out_dir>/look_<shot>.png for a
# few fixed cameras (full length, face, three-quarter face, back, arm and thigh close-ups, the studio from afar).
# FL_SHOTS=face,full renders only those; FL_SKIN_DEBUG=n shows a skin mask (skin.gdshader debug_view).
extends SceneTree

const Look = preload("res://look.gd")
const Studio = preload("res://studio.gd")
const SkinMap = preload("res://skin.gd")
const BodyGen = preload("res://body_gen.gd")
const BodyView = preload("res://scripts/body_loader.gd")
const PlanLoader = preload("res://plan_loader.gd")

const SHOTS := [
	# name, camera position, target, vertical fov
	["full", Vector3(1.1, 1.05, 3.6), Vector3(0.0, 0.88, 0.0), 32.0],
	["face", Vector3(0.0, 1.56, 0.62), Vector3(0.0, 1.55, 0.0), 20.0],
	["face34", Vector3(-0.42, 1.58, 0.48), Vector3(0.0, 1.54, 0.03), 20.0],
	["back", Vector3(-1.3, 1.2, -2.9), Vector3(0.0, 0.9, 0.0), 34.0],
	["arm", Vector3(-0.9, 1.15, 0.9), Vector3(-0.2, 1.05, 0.0), 26.0],
	["thigh", Vector3(0.55, 0.7, 1.15), Vector3(0.0, 0.62, 0.0), 30.0],
	["studio", Vector3(0.6, 2.6, 7.5), Vector3(0.0, 1.0, 0.0), 50.0],
	["eye", Vector3(-0.03, 1.565, 0.32), Vector3(0.028, 1.555, 0.1), 9.0],
	["forearm", Vector3(-0.55, 0.95, 0.55), Vector3(-0.25, 0.9, 0.05), 18.0],
]

var _out := "."
var _vp: SubViewport
var _cam: Camera3D
var _frames := 0
var _shot := 0
var _list: Array = SHOTS


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	_out = args[0] if args.size() > 0 else "."
	var preset := args[1] if args.size() > 1 else "average"
	var qual := args[2] if args.size() > 2 else "high"
	var skin_key := args[3] if args.size() > 3 else Look.SKIN_DEFAULT
	var hair_key := args[4] if args.size() > 4 else Look.HAIR_DEFAULT
	var eye_key := args[5] if args.size() > 5 else Look.EYE_DEFAULT
	RenderingServer.set_default_clear_color(Look.BG)
	_vp = SubViewport.new()
	_vp.size = Vector2i(1000, 1250)
	_vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_vp.own_world_3d = true
	root.add_child(_vp)
	var stage := Node3D.new()
	_vp.add_child(stage)
	var we := WorldEnvironment.new()
	we.environment = Environment.new()
	Look.environment(we.environment)
	stage.add_child(we)
	var st: Dictionary = Studio.build(stage)
	we.environment.sky = st["sky"]
	stage.add_child(Look.contact_shadow())
	_cam = Camera3D.new()
	Look.camera(_cam)
	stage.add_child(_cam)

	var M := BodyGen.load_data(PlanLoader.load_json("res://assets/body_data.json"))
	var meas: Dictionary = BodyGen.DEFAULT_MEAS.duplicate()
	if BodyGen.PRESETS.has(preset):
		meas = (BodyGen.PRESETS[preset] as Dictionary).duplicate()
	var rest := BodyGen.generate(M, meas, null)
	var stand := BodyGen.stand(M, rest)
	var pos: PackedFloat32Array = stand["pos"]
	var v := PackedVector3Array()
	v.resize(pos.size() / 3)
	for i in v.size():
		v[i] = Vector3(pos[3 * i], pos[3 * i + 1], pos[3 * i + 2])
	var body := BodyView.new()
	body.material_override = Look.body_material()
	stage.add_child(body)
	body.use_custom(v, M["tris"])
	body.set_skin_data(SkinMap.build(M))
	Look.set_skin_tone(body.material_override, skin_key)
	Look.set_hair(body.material_override, hair_key)
	body.eyes.set_iris(eye_key)
	Look.apply_quality(qual, we.environment, st["key"], _vp, body.material_override)
	if OS.has_environment("FL_SKIN_DEBUG"):
		(body.material_override as ShaderMaterial).set_shader_parameter("debug_view", int(OS.get_environment("FL_SKIN_DEBUG")))
	# FL_SKIN_PARAMS="name=v;name=x,y,z" overrides skin shader uniforms (floats or vec3)
	if OS.has_environment("FL_SKIN_PARAMS"):
		for kv in OS.get_environment("FL_SKIN_PARAMS").split(";", false):
			var p := kv.split("=")
			var vals := p[1].split(",")
			var val: Variant = float(vals[0]) if vals.size() == 1 else Vector3(float(vals[0]), float(vals[1]), float(vals[2]))
			(body.material_override as ShaderMaterial).set_shader_parameter(p[0], val)
	if OS.has_environment("FL_LIGHTS"):
		# FL_LIGHTS=key,rim_l keeps only those lamps on
		var keep := OS.get_environment("FL_LIGHTS").split(",")
		for id in (st["lights"] as Dictionary).keys():
			(st["lights"][id] as Light3D).visible = keep.has(id)
	if OS.has_environment("FL_SHADOW_BIAS"):
		var bb := OS.get_environment("FL_SHADOW_BIAS").split(",")
		(st["key"] as Light3D).shadow_bias = float(bb[0])
		(st["key"] as Light3D).shadow_normal_bias = float(bb[1])
		if bb.size() > 2:
			(st["key"] as Light3D).shadow_blur = float(bb[2])
	if OS.has_environment("FL_SPHERE"):
		# a sphere and a cylinder beside the body, in plain grey (std) or skin: what the lamps do on simple shapes
		for k in 2:
			var mi := MeshInstance3D.new()
			if k == 0:
				var sm := SphereMesh.new()
				sm.radius = 0.12
				sm.height = 0.24
				mi.mesh = sm
				mi.position = Vector3(0.45, 0.62, 0.15)
			else:
				var cm := CylinderMesh.new()
				cm.top_radius = 0.07
				cm.bottom_radius = 0.07
				cm.height = 0.8
				mi.mesh = cm
				mi.position = Vector3(0.75, 0.4, 0.15)
			mi.material_override = StandardMaterial3D.new() if OS.get_environment("FL_SPHERE") == "std" else body.material_override
			stage.add_child(mi)
	if OS.has_environment("FL_NOSHADOW"):
		for id in (st["lights"] as Dictionary).keys():
			(st["lights"][id] as Light3D).shadow_enabled = false
	if OS.has_environment("FL_SHOTS"):
		var want := OS.get_environment("FL_SHOTS").split(",")
		_list = SHOTS.filter(func(s: Array) -> bool: return want.has(s[0]))
	print("LOOK body ", v.size(), " verts, height ", float(meas.get("height", 1.68)), ", quality ", qual)
	process_frame.connect(_tick)


func _tick() -> void:
	_frames += 1
	if _shot >= _list.size():
		quit(0)
		return
	var s: Array = _list[_shot]
	if _frames == 1:
		_cam.position = s[1]
		_cam.fov = float(s[3])
		_cam.look_at(s[2])
	# a few frames for the sky radiance, shadows and TAA to settle
	if _frames >= 8:
		var img := _vp.get_texture().get_image()
		var path := "%s/look_%s.png" % [_out, s[0]]
		img.save_png(path)
		print("LOOK saved ", path)
		_shot += 1
		_frames = 0
