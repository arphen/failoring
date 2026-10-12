# Godot editor: the browser app's core loop with the native solver. Pick a body preset and
# a FreeSewing garment, watch it drape and walk, read the pattern info.
#   godot --path godot res://editor.tscn
# Headless shot matrix: FL_AUTO=preset,garment,frames runs that many sim frames, saves
# godot/docs-shots/<preset>_<garment>.png and quits (needs a real window for pixels).
extends Node3D

const PRESETS := ["petite", "average", "tall", "curvy", "athletic", "plus"]
const GARMENTS := [["Tee", "teagan"], ["Pencil skirt", "penelope"], ["Chinos", "charlie"]]
const SETTLE := 48        # standing frames before the walk starts (browser Standard quality frames)
const CYCLE := 64.0       # gait frames per cycle at 60 fps sim (browser WALK.cycle)
const RAMP := 24.0        # stride ease-in frames (browser WALK.ramp)
const STAND_FRAMES := 96  # standing mode stops after this many frames

var sim: FailoringSim
var body_node: FailoringBody
var cloth_node: MeshInstance3D
var cam_pivot: Node3D
var cam: Camera3D

var preset := "average"
var garment := "teagan"
var fabric_id := ""
var plan: Dictionary = {}
var standing := PackedVector3Array()
var body_tris := PackedInt32Array()
var walk_frames: Array = []
var walk_nf := 0

var playing := true
var walking := true
var speed := 1.0
var frame := 0
var walk_k := 0.0       # gait clock in sim frames (float: speed scales it)
var bake_ms := 0.0
var step_ms := 0.0

@onready var preset_opt: OptionButton = $UI/Left/VBox/PresetOpt
@onready var garment_opt: OptionButton = $UI/Left/VBox/GarmentOpt
@onready var fabric_opt: OptionButton = $UI/Left/VBox/FabricOpt
@onready var play_btn: Button = $UI/Left/VBox/PlayBtn
@onready var restart_btn: Button = $UI/Left/VBox/RestartBtn
@onready var walk_check: CheckBox = $UI/Left/VBox/WalkCheck
@onready var speed_slider: HSlider = $UI/Left/VBox/SpeedSlider
@onready var status_label: Label = $UI/Left/VBox/StatusLabel
@onready var info_text: RichTextLabel = $UI/Right/InfoText

var yaw := 0.3
var pitch := -0.12
var dist := 2.4


func _ready() -> void:
	_build_scene()
	_fill_ui()
	if OS.has_environment("FL_STAND"):
		walking = false
		walk_check.button_pressed = false
	if OS.has_environment("FL_AUTO"):
		var parts := OS.get_environment("FL_AUTO").split(",")
		if parts.size() >= 3:
			preset = parts[0]
			garment = parts[1]
			preset_opt.select(PRESETS.find(preset))
			for i in GARMENTS.size():
				if GARMENTS[i][1] == garment:
					garment_opt.select(i)
	rebuild()
	if OS.has_environment("FL_AUTO"):
		playing = true


func _build_scene() -> void:
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.12, 0.13, 0.15)
	env.environment.ambient_light_color = Color(0.6, 0.6, 0.65)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	add_child(sun)
	cam_pivot = Node3D.new()
	cam_pivot.position = Vector3(0, 0.9, 0)
	add_child(cam_pivot)
	cam = Camera3D.new()
	cam.position = Vector3(0, 0, dist)
	cam_pivot.add_child(cam)
	body_node = FailoringBody.new()
	var skin := StandardMaterial3D.new()
	skin.albedo_color = Color(0.85, 0.68, 0.58)
	body_node.material_override = skin
	add_child(body_node)
	cloth_node = MeshInstance3D.new()
	cloth_node.extra_cull_margin = 0.5
	var cmat := StandardMaterial3D.new()
	cmat.albedo_color = Color(0.25, 0.45, 0.8)
	cmat.cull_mode = BaseMaterial3D.CULL_DISABLED
	cloth_node.material_override = cmat
	add_child(cloth_node)


func _fill_ui() -> void:
	for p in PRESETS:
		preset_opt.add_item(p.capitalize(), PRESETS.find(p))
	preset_opt.select(PRESETS.find(preset))
	for g in GARMENTS:
		garment_opt.add_item(g[0], GARMENTS.find(g))
	garment_opt.select(0)
	var fab: Dictionary = FailoringPlanLoader.load_json("res://materials/fabrics.json")
	for f in fab.get("fabrics", []):
		fabric_opt.add_item(String(f["name"]), fabric_opt.item_count)
		fabric_opt.set_item_metadata(fabric_opt.item_count - 1, String(f["id"]))
	preset_opt.item_selected.connect(_on_preset)
	garment_opt.item_selected.connect(_on_garment)
	fabric_opt.item_selected.connect(_on_fabric)
	play_btn.pressed.connect(_on_play)
	restart_btn.pressed.connect(func() -> void: rebuild())
	walk_check.toggled.connect(func(on: bool) -> void: walking = on; rebuild())
	speed_slider.value_changed.connect(func(v: float) -> void: speed = v)


func _asset(preset_name: String, file: String) -> String:
	return "res://assets/%s/%s" % [preset_name, file]


func rebuild() -> void:
	sim = null
	frame = 0
	walk_k = 0.0
	var dir := "res://assets/%s" % preset
	var obj: Dictionary = FailoringBody.parse_obj(_asset(preset, "body.obj"))
	if obj.is_empty():
		status_label.text = "no body.obj for " + preset
		return
	standing = obj["vertices"]
	body_tris = obj["indices"]
	var w: Dictionary = FailoringBody.parse_walk(_asset(preset, "walk.bin"), standing)
	walk_frames = w.get("frames", [])
	walk_nf = walk_frames.size()
	body_node.assets_dir = "res://assets"
	if not body_node.load_preset(preset):
		status_label.text = "cannot load " + preset
		return
	body_node.show_pose(standing)
	plan = FailoringPlanLoader.load_json(_asset(preset, "plan_%s.json" % garment))
	if plan.is_empty():
		status_label.text = "no plan for %s on %s" % [garment, preset]
		return
	var sections: Dictionary = FailoringPlanLoader.load_json(_asset(preset, "sections.json"))
	if sections.is_empty():
		status_label.text = "no sections for " + preset
		return
	if fabric_id == "":
		fabric_id = String(plan.get("fabric", "cotton_poplin"))
		_sync_fabric_ui()
	var built: Dictionary = FailoringPlanLoader.build(plan, sections["sections"], sections["arms"], standing, body_tris, fabric_id)
	if built.is_empty():
		status_label.text = "sim build failed"
		return
	sim = built["sim"]
	playing = true
	play_btn.text = "Pause"
	_render_info()
	status_label.text = "%d verts - simulating" % int(built["n"])


func _sync_fabric_ui() -> void:
	for i in fabric_opt.item_count:
		if String(fabric_opt.get_item_metadata(i)) == fabric_id:
			fabric_opt.select(i)
			return


func _on_preset(i: int) -> void:
	preset = PRESETS[i]
	fabric_id = ""
	rebuild()


func _on_garment(i: int) -> void:
	garment = GARMENTS[i][1]
	fabric_id = ""
	rebuild()


func _on_fabric(i: int) -> void:
	fabric_id = String(fabric_opt.get_item_metadata(i))
	rebuild()


func _on_play() -> void:
	playing = not playing
	play_btn.text = "Pause" if playing else "Resume"


func _walk_pose() -> PackedVector3Array:
	# Browser walkStep: stride eases 0->1 over RAMP frames, phase advances 1/CYCLE per frame.
	var stride: float = _smooth(minf(1.0, walk_k / RAMP))
	var phase: float = fposmod(walk_k / CYCLE, 1.0) * float(walk_nf)
	var k0 := int(phase) % walk_nf
	var k1 := (k0 + 1) % walk_nf
	var a: float = phase - floor(phase)
	var fa: PackedVector3Array = walk_frames[k0]
	var fb: PackedVector3Array = walk_frames[k1]
	var v := PackedVector3Array()
	v.resize(fa.size())
	for i in fa.size():
		v[i] = standing[i].lerp(fa[i].lerp(fb[i], a), stride)
	return v


static func _smooth(x: float) -> float:
	x = clampf(x, 0.0, 1.0)
	return x * x * (3.0 - 2.0 * x)


func _physics_process(_delta: float) -> void:
	cam_pivot.rotation = Vector3(pitch, yaw, 0)
	if sim == null or not playing:
		return
	frame += 1
	var t0 := Time.get_ticks_usec()
	bake_ms = 0.0
	if walking and walk_nf > 0 and frame > SETTLE:
		walk_k += speed
		var pose := _walk_pose()
		bake_ms = sim.set_body_positions(pose)
		body_node.show_pose(pose)
	var f := float(frame)
	if f <= 21.0:
		sim.set_param("gravity_y", -9.81 * minf(1.0, pow(f / 20.0, 2.0)))
	var t1 := Time.get_ticks_usec()
	sim.step(1)
	var t2 := Time.get_ticks_usec()
	step_ms = (t2 - t1) / 1000.0
	_draw_cloth()
	_render_stats((t2 - t0) / 1000.0)
	if frame % 30 == 0:
		print("EDITOR ", status_label.text.replace("\n", " | "))
	if not walking and frame >= STAND_FRAMES:
		playing = false
		play_btn.text = "Resume"
		status_label.text = status_label.text + " - done"
	if OS.has_environment("FL_AUTO"):
		var want := int(OS.get_environment("FL_AUTO").split(",")[2])
		if frame >= want:
			_shot(OS.get_environment("FL_AUTO").split(",")[0] + "_" + OS.get_environment("FL_AUTO").split(",")[1])


func _draw_cloth() -> void:
	var disp: PackedVector3Array = sim.get_display_positions(0.0008)
	var idx: PackedInt32Array = sim.get_display_indices()
	if disp.is_empty() or idx.is_empty():
		return
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = disp
	arr[Mesh.ARRAY_INDEX] = idx
	var m := ArrayMesh.new()
	m.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	cloth_node.mesh = m


func _render_stats(total_ms: float) -> void:
	var st: Dictionary = sim.get_solver_stats()
	status_label.text = "%s / %s / %s   frame %d   verts %d   KE %s J   gap %.1f mm   clear %.1f mm\nbake %.0f ms   step %.0f ms (newton %d, ls-fail %d)" % [
		preset, _garment_name(), fabric_id, frame, sim.get_vertex_count(), str(sim.get_kinetic_energy()),
		sim.get_max_seam_gap() * 1000.0, sim.get_min_body_clearance() * 1000.0,
		bake_ms, step_ms, int(st.get("newton_iterations", 0)), int(st.get("line_search_failures", 0))]


func _garment_name() -> String:
	for g in GARMENTS:
		if g[1] == garment:
			return g[0]
	return garment


func _render_info() -> void:
	# Pattern info (cutting list, seams, sewing order) arrives with the info panel commit.
	info_text.text = "[b]%s[/b]\n%s\n\n%s pieces, %s seams" % [String(plan.get("name", garment)), String(plan.get("notes", "")), (plan.get("pieces", []) as Array).size(), (plan.get("seams", []) as Array).size()]


func _shot(tag: String) -> void:
	DirAccess.make_dir_recursive_absolute("res://docs-shots")
	var img := get_viewport().get_texture().get_image()
	if img.save_png("res://docs-shots/%s.png" % tag) == OK:
		print("SHOT saved ", tag)
	else:
		push_error("SHOT failed for " + tag)
	get_tree().quit()


func _unhandled_input(e: InputEvent) -> void:
	if e is InputEventMouseMotion and (e.button_mask & MOUSE_BUTTON_MASK_LEFT):
		yaw -= e.relative.x * 0.005
		pitch = clampf(pitch - e.relative.y * 0.005, -1.4, 1.4)
	elif e is InputEventMouseButton and e.pressed:
		if e.button_index == MOUSE_BUTTON_WHEEL_UP:
			dist = maxf(0.6, dist * 0.92)
		elif e.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			dist = minf(5.0, dist * 1.08)
		cam.position.z = dist
