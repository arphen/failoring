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
const Idle = preload("res://idle.gd")
const FaceLikeness = preload("res://face.gd")

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
	# acceptance views: a 50 mm lens (27 degrees vertical on a 24 mm sensor) from the front and a three-quarter angle, and a wide
	# shot of the room
	["front", Vector3(0.0, 1.25, 4.2), Vector3(0.0, 0.9, 0.0), 27.0],
	["q34", Vector3(-2.5, 1.3, 3.3), Vector3(0.0, 0.9, 0.0), 27.0],
	["wide", Vector3(1.8, 1.9, 4.6), Vector3(0.0, 1.1, -1.0), 55.0],
	["room_l", Vector3(2.5, 1.7, 1.0), Vector3(-3.0, 1.0, 0.0), 55.0],
	["room_r", Vector3(-2.5, 1.7, 1.0), Vector3(3.5, 1.0, -0.5), 55.0],
	["room_b", Vector3(0.5, 1.7, -4.4), Vector3(0.0, 1.0, 1.0), 55.0],
	["plan", Vector3(0.0, 3.3, 1.2), Vector3(0.0, 0.0, -0.4), 100.0],
	["eyes", Vector3(0.0, 1.56, 0.55), Vector3(0.0, 1.56, 0.0), 11.0],
	["desk", Vector3(2.4, 1.5, 3.3), Vector3(4.2, 0.85, 2.7), 40.0],
	["forms", Vector3(1.7, 1.5, -0.3), Vector3(4.05, 1.1, -1.6), 40.0],
	["tea", Vector3(-1.9, 1.4, -2.2), Vector3(-3.9, 0.7, -4.0), 38.0],
	["mirror", Vector3(-0.6, 1.4, 0.6), Vector3(-3.35, 1.0, -1.55), 38.0],
	["mouth", Vector3(0.0, 1.512, 0.42), Vector3(0.0, 1.512, 0.0), 14.0],
	["mouth34", Vector3(-0.2, 1.512, 0.36), Vector3(0.0, 1.512, 0.0), 14.0],
	["head_back", Vector3(-0.35, 1.62, -0.95), Vector3(0.0, 1.45, 0.0), 28.0],
	["head_side", Vector3(1.0, 1.55, 0.15), Vector3(0.0, 1.45, 0.0), 30.0],
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
	# FL_LAMPS_OFF=key,fill,rim_l,rim_r,top switches those lamps off (Studio.set_lamp, as the editor's Lamps menu does);
	# FL_NOFIXTURES=1 hides all the fixtures
	if OS.has_environment("FL_LAMPS_OFF"):
		for lid in OS.get_environment("FL_LAMPS_OFF").split(","):
			Studio.set_lamp(st, lid, false)
	if OS.has_environment("FL_NOFIXTURES"):
		for fx in st["fixtures"]:
			(fx as Node3D).visible = false
	stage.add_child(Look.contact_shadow())
	_cam = Camera3D.new()
	Look.camera(_cam)
	stage.add_child(_cam)
	Look.reflection_probe(stage)
	Look.post(_vp)

	Look.mirror_setup(stage, _cam, _vp.world_3d, we.environment)
	var M := BodyGen.load_data(PlanLoader.load_json("res://assets/body_data.json"))
	var meas: Dictionary = BodyGen.DEFAULT_MEAS.duplicate()
	if BodyGen.PRESETS.has(preset):
		meas = (BodyGen.PRESETS[preset] as Dictionary).duplicate()
	# FL_FACE=<id>: the face likeness (face.gd) with its skin and eyes; FL_HAIR=1 its long hair too
	var face_id := OS.get_environment("FL_FACE") if OS.has_environment("FL_FACE") and FaceLikeness.use(OS.get_environment("FL_FACE")) else ""
	if face_id != "":
		meas["face"] = face_id
	var rest := BodyGen.generate(M, meas, null)
	var stand := BodyGen.stand(M, rest)
	var pos: PackedFloat32Array = stand["pos"]
	var v := PackedVector3Array()
	v.resize(pos.size() / 3)
	for i in v.size():
		v[i] = Vector3(pos[3 * i], pos[3 * i + 1], pos[3 * i + 2])
	var body := BodyView.new()
	body.material_override = Look.body_material()
	body.layers = 2                                   # not in the reflection probe's capture
	body.gi_mode = GeometryInstance3D.GI_MODE_DYNAMIC
	stage.add_child(body)
	# the body's own _ready (it loads the baked walk of its preset) runs when the tree starts, after this: set the generated
	# standing body up after it, or the mannequin walks
	body.ready.connect(func() -> void:
		body.walking = false
		body.use_custom(v, M["tris"])
		body.set_skin_data(SkinMap.build(M))
		body.eyes.set_iris(eye_key)
		if face_id != "":
			var fi := FaceLikeness.info(face_id)
			if fi.get("iris", null) is Array:
				body.eyes.set_iris_rgb(Color(fi["iris"][0], fi["iris"][1], fi["iris"][2]))
			if OS.has_environment("FL_HAIR"):
				var hc = fi.get("hair", null)
				body.set_long_hair(true, M, Color(hc[0], hc[1], hc[2]) if hc is Array else Color(0.31, 0.24, 0.18))
		# FL_IDLE=0 leaves the mannequin still; FL_IDLE_T=12 lets the idle layer run for 12 s before the shots; FL_IDLE_SEED=n
		if OS.get_environment("FL_IDLE") != "0":
			var il := Idle.new(int(OS.get_environment("FL_IDLE_SEED")) if OS.has_environment("FL_IDLE_SEED") else 7)
			for _i in int(float(OS.get_environment("FL_IDLE_T")) * 30.0) if OS.has_environment("FL_IDLE_T") else 0:
				il.tick(1.0 / 30.0)
			if OS.has_environment("FL_BLINK"):
				il.hold_blink = float(OS.get_environment("FL_BLINK"))
			if OS.has_environment("FL_IDLE_GAIN"):
				il.intensity = float(OS.get_environment("FL_IDLE_GAIN"))
			body.set_idle(il, stand["joints"], M["bones"])
			if OS.has_environment("FL_IDLE_FREEZE"):                         # hold that moment: no further time passes
				il.intensity = il.intensity
		, CONNECT_ONE_SHOT)
	Look.set_skin_tone(body.material_override, skin_key)
	Look.set_hair(body.material_override, "none" if OS.has_environment("FL_HAIR") else hair_key)
	if face_id != "":
		Look.set_face(body.material_override, face_id)
	Look.apply_quality(qual, we.environment, st["key"], _vp, body.material_override)
	# FL_FX=nogi,nossr,nossil,nossao,noglow,nodof,notaa,noprobe,nosss turns those effects off, for finding which one draws an artifact
	if OS.has_environment("FL_FX"):
		var fx := OS.get_environment("FL_FX").split(",")
		var e := we.environment
		if fx.has("nogi"): e.sdfgi_enabled = false
		if fx.has("nossr"): e.ssr_enabled = false
		if fx.has("nossil"): e.ssil_enabled = false
		if fx.has("nossao"): e.ssao_enabled = false
		if fx.has("noglow"): e.glow_enabled = false
		if fx.has("nodof"): _cam.attributes = null
		if fx.has("notaa"): _vp.use_taa = false
		if fx.has("noprobe"): Look._probe.visible = false
		if fx.has("noadj"): e.adjustment_enabled = false
		if fx.has("sss"): (body.material_override as ShaderMaterial).set_shader_parameter("sss", 0.35)
		if fx.has("nosss"): (body.material_override as ShaderMaterial).set_shader_parameter("sss", 0.0)
		if fx.has("exp"):
			e.tonemap_exposure = float(OS.get_environment("FL_EXP"))
	# FL_ENV="sdfgi_use_occlusion=false;sdfgi_cascades=4" sets Environment properties (numbers and true/false)
	if OS.has_environment("FL_ENV"):
		for kv in OS.get_environment("FL_ENV").split(";", false):
			var pv := kv.split("=")
			var val: Variant = pv[1] == "true" if pv[1] in ["true", "false"] else (int(pv[1]) if pv[1].is_valid_int() else float(pv[1]))
			we.environment.set(pv[0], val)
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
	if OS.has_environment("FL_RIMSHADOW"):    # FL_RIMSHADOW=0: only the key casts a shadow (the old set-up)
		for id in ["rim_l", "rim_r", "top"]:
			(st["lights"][id] as Light3D).shadow_enabled = OS.get_environment("FL_RIMSHADOW") == "1"
	if OS.has_environment("FL_NOSHADOW"):
		for id in (st["lights"] as Dictionary).keys():
			(st["lights"][id] as Light3D).shadow_enabled = false
	if OS.has_environment("FL_SHOTS"):
		var want := OS.get_environment("FL_SHOTS").split(",")
		_list = SHOTS.filter(func(s: Array) -> bool: return want.has(s[0]))
	# FL_CAM="name:x,y,z:tx,ty,tz:fov" adds a camera of its own (a view seen in the editor)
	if OS.has_environment("FL_CAM"):
		for spec in OS.get_environment("FL_CAM").split(";", false):
			var c := spec.split(":")
			var a := c[1].split_floats(",")
			var b := c[2].split_floats(",")
			_list = _list + [[c[0], Vector3(a[0], a[1], a[2]), Vector3(b[0], b[1], b[2]), float(c[3])]]
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
		Look.mirror_update()
		Look.focus(_cam.position.distance_to(s[2]))
	# a few frames for the sky radiance, shadows and TAA to settle
	if _frames >= 40:
		var img := _vp.get_texture().get_image()
		var path := "%s/look_%s.png" % [_out, s[0]]
		img.save_png(path)
		print("LOOK saved ", path)
		_shot += 1
		_frames = 0
