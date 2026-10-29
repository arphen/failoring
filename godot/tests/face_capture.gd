# Front view of the mannequin's head for the face likeness tool (tools/face/build_face.sh): renders the average body's
# head straight on, in flat even light (so a face detector reads it), and writes where every front-facing head vertex
# lands in the image, so the detector's landmarks can be tied to vertices of the base mesh.
#   godot --path godot --script res://tests/face_capture.gd -- <out.png> <out.json>
# JSON: {w, h, verts: {body vertex index: [x px, y px, depth m]}} for the vertices above the chin line that face the
# camera. Needs a window (pixels).
extends SceneTree

const BodyGen = preload("res://body_gen.gd")
const PlanLoader = preload("res://plan_loader.gd")
const BodyView = preload("res://scripts/body_loader.gd")
const SkinMap = preload("res://skin.gd")
const Look = preload("res://look.gd")
const FaceLikeness = preload("res://face.gd")

const SIZE := Vector2i(1200, 1500)

var _vp: SubViewport
var _cam: Camera3D
var _frames := 0
var _proj := {}
var _out_png := ""
var _out_json := ""
var _v := PackedVector3Array()
var _tris := PackedInt32Array()
var _top := 0.0


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	_out_png = args[0]
	_out_json = args[1]
	RenderingServer.set_default_clear_color(Color(0.55, 0.55, 0.55))
	_vp = SubViewport.new()
	_vp.size = SIZE
	_vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_vp.own_world_3d = true
	root.add_child(_vp)
	var stage := Node3D.new()
	_vp.add_child(stage)
	var we := WorldEnvironment.new()
	we.environment = Environment.new()
	we.environment.background_mode = Environment.BG_COLOR
	we.environment.background_color = Color(0.55, 0.55, 0.55)
	we.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	we.environment.ambient_light_color = Color(1, 1, 1)
	we.environment.ambient_light_energy = 0.6
	stage.add_child(we)
	var key := DirectionalLight3D.new()
	key.rotation_degrees = Vector3(-15, 10, 0)
	key.light_energy = 0.9
	stage.add_child(key)
	var M := BodyGen.load_data(PlanLoader.load_json("res://assets/body_data.json"))
	var meas: Dictionary = (BodyGen.PRESETS["average"] as Dictionary).duplicate()
	# FL_FACE=<id>: with that face likeness (face.gd), to compare the fitted head with the photos
	if OS.has_environment("FL_FACE") and FaceLikeness.use(OS.get_environment("FL_FACE")):
		meas["face"] = OS.get_environment("FL_FACE")
	var rest := BodyGen.generate(M, meas, null)
	var stand := BodyGen.stand(M, rest)
	var pos: PackedFloat32Array = stand["pos"]
	var v := PackedVector3Array()
	v.resize(pos.size() / 3)
	for i in v.size():
		v[i] = Vector3(pos[3 * i], pos[3 * i + 1], pos[3 * i + 2])
	# the camera: straight at the face, 0.75 m away, level with the middle of the head (12 cm below its top)
	var top := -1e9
	for i in v.size():
		top = maxf(top, v[i].y)
	var head_y := top - 0.12
	_cam = Camera3D.new()
	_cam.fov = 22.0
	stage.add_child(_cam)
	# FL_CAM_YAW=<degrees>: from the side instead (look checks; the landmark fit uses the front)
	var yaw := deg_to_rad(float(OS.get_environment("FL_CAM_YAW"))) if OS.has_environment("FL_CAM_YAW") else 0.0
	var dist := 0.75 if not OS.has_environment("FL_HAIR") else 1.15
	_cam.look_at_from_position(Vector3(sin(yaw) * dist, head_y - (0.08 if OS.has_environment("FL_HAIR") else 0.0), cos(yaw) * dist), Vector3(0.0, head_y - (0.08 if OS.has_environment("FL_HAIR") else 0.0), 0.0))
	var body := BodyView.new()
	body.material_override = Look.body_material()
	stage.add_child(body)
	body.ready.connect(func() -> void:
		body.walking = false
		body.use_custom(v, M["tris"])
		body.set_skin_data(SkinMap.build(M))
		if OS.has_environment("FL_HAIR"):   # the likeness's long hair too
			var hc = FaceLikeness.info(OS.get_environment("FL_FACE")).get("hair", null)
			body.set_long_hair(true, M, Color(hc[0], hc[1], hc[2]) if hc is Array else Color(0.31, 0.24, 0.18))
		var iris = FaceLikeness.info(OS.get_environment("FL_FACE")).get("iris", null) if OS.has_environment("FL_FACE_SKIN") else null
		if iris is Array:
			body.eyes.set_iris_rgb(Color(iris[0], iris[1], iris[2]))
		else:
			body.eyes.set_iris("brown"), CONNECT_ONE_SHOT)
	Look.set_skin_tone(body.material_override, Look.SKIN_DEFAULT)
	Look.set_hair(body.material_override, "none")
	# FL_FACE_SKIN=1 with FL_FACE: the face's skin too (look check of the whole likeness; the landmark fit uses the shape alone)
	if OS.has_environment("FL_FACE") and OS.has_environment("FL_FACE_SKIN"):
		Look.set_face(body.material_override, OS.get_environment("FL_FACE"))
	_v = v
	_tris = M["tris"]
	_top = top


# Front-facing head vertices and where they land (the camera has to be in the tree, so after the first frame).
func _project() -> void:
	var tris: PackedInt32Array = _tris
	var v := _v
	var top := _top
	var nrm := PackedVector3Array()
	nrm.resize(v.size())
	for t in tris.size() / 3:
		var a := tris[3 * t]
		var b := tris[3 * t + 1]
		var c := tris[3 * t + 2]
		var n := (v[b] - v[a]).cross(v[c] - v[a])
		nrm[a] += n
		nrm[b] += n
		nrm[c] += n
	for i in v.size():
		if v[i].y < top - 0.30:
			continue
		var to_cam := (_cam.global_position - v[i]).normalized()
		if nrm[i].normalized().dot(to_cam) <= 0.05:
			continue
		var sp := _cam.unproject_position(v[i])
		_proj[str(i)] = [snappedf(sp.x, 0.01), snappedf(sp.y, 0.01), snappedf(_cam.global_position.distance_to(v[i]), 0.0001)]


func _process(_delta: float) -> bool:
	_frames += 1
	if _frames == 2:
		_project()
	if _frames == 30:
		var img := _vp.get_texture().get_image()
		img.save_png(_out_png)
		var f := FileAccess.open(_out_json, FileAccess.WRITE)
		f.store_string(JSON.stringify({"w": SIZE.x, "h": SIZE.y, "verts": _proj}))
		f.close()
		print("FACECAPTURE saved %d vertices" % _proj.size())
		quit()
	return false
