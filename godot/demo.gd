# Drape prototype demo: two pattern panels sewn into a tube, dropped over an ellipsoid mannequin.
# Keys: 1-9 pick a fabric (restarts), R restarts. Mouse drag orbits, wheel zooms.
extends Node3D

const RADII := Vector3(0.17, 0.35, 0.11)
const W := 0.40
const H := 0.30
const Y0 := 0.25

var sim: FailoringSim
var mesh_instance: MeshInstance3D
var cam_pivot: Node3D
var label: Label
var fabric_ids: PackedStringArray
var fabric_index := 0
var yaw := 0.4
var pitch := -0.25
var dist := 1.6

func _ready() -> void:
	_build_scene()
	_restart()

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
	add_child(cam_pivot)
	var cam := Camera3D.new()
	cam.position = Vector3(0, 0, dist)
	cam_pivot.add_child(cam)
	cam_pivot.position = Vector3(0, 0.1, 0)
	var body := MeshInstance3D.new()          # mannequin
	var sph := SphereMesh.new()
	sph.radius = 1.0
	sph.height = 2.0
	body.mesh = sph
	body.scale = RADII
	var bmat := StandardMaterial3D.new()
	bmat.albedo_color = Color(0.75, 0.62, 0.55)
	body.material_override = bmat
	add_child(body)
	mesh_instance = MeshInstance3D.new()
	add_child(mesh_instance)
	var cl := CanvasLayer.new()
	label = Label.new()
	label.position = Vector2(12, 8)
	cl.add_child(label)
	add_child(cl)

func _restart() -> void:
	sim = FailoringSim.new()
	if not sim.load_materials("res://materials/fabrics.json"):
		return
	fabric_ids = sim.get_material_ids()
	var id := fabric_ids[fabric_index % fabric_ids.size()]
	sim.set_param("substeps", 4)
	sim.set_param("seam_ramp_frames", 0)
	sim.add_ellipsoid(Vector3.ZERO, RADII)
	var front := sim.add_rect_panel(W, H, 0.02, id, Vector3(-W / 2, Y0, 0.145), Vector3.RIGHT, Vector3.UP, PI / 2, "front")
	var back := sim.add_rect_panel(W, H, 0.02, id, Vector3(W / 2, Y0, -0.145), Vector3.LEFT, Vector3.UP, PI / 2, "back")
	sim.add_seam(front, 1, back, 3, true)
	sim.add_seam(front, 3, back, 1, true)
	# pre-wrap both panels onto a cylinder of circumference 2W so the seams start closed
	var rc := W / PI
	_prewrap(front, back, rc)
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.25, 0.45, 0.8)
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	mesh_instance.material_override = mat

func _prewrap(front: int, back: int, rc: float) -> void:
	# Panel vertices were placed flat; recompute their (x,y) panel coordinates from that placement:
	# front: x = world.x + W/2, y = world.y - Y0 ;  back: x = W/2 - world.x
	var pos := sim.get_positions()
	for p in [front, back]:
		var first := sim.get_first_vertex(p)
		var n := sim.get_panel_vertex_count(p)
		for i in n:
			var q := pos[first + i]
			var px := (q.x + W / 2) if p == front else (W / 2 - q.x)
			var py := q.y - Y0
			var th := PI * px / W
			var s := 1.0 if p == front else -1.0
			sim.set_vertex_position(first + i, Vector3(-s * rc * cos(th), Y0 + py, s * rc * sin(th)))

func _physics_process(_dt: float) -> void:
	if sim == null:
		return
	sim.step(1)
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = sim.get_positions()
	arr[Mesh.ARRAY_INDEX] = sim.get_indices()
	var m := ArrayMesh.new()
	m.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	mesh_instance.mesh = m
	label.text = "%s   verts %d   KE %.1e J   seam gap %.1f mm   clearance %.1f mm\n[1-9] fabric   [R] restart   drag: orbit" % [
		fabric_ids[fabric_index % fabric_ids.size()], sim.get_vertex_count(), sim.get_kinetic_energy(), sim.get_max_seam_gap() * 1000.0, sim.get_min_body_clearance() * 1000.0]
	cam_pivot.rotation = Vector3(pitch, yaw, 0)

func _unhandled_input(e: InputEvent) -> void:
	if e is InputEventMouseMotion and (e.button_mask & MOUSE_BUTTON_MASK_LEFT):
		yaw -= e.relative.x * 0.005
		pitch = clamp(pitch - e.relative.y * 0.005, -1.4, 1.4)
	elif e is InputEventMouseButton and e.pressed:
		if e.button_index == MOUSE_BUTTON_WHEEL_UP: dist = max(0.6, dist * 0.92)
		elif e.button_index == MOUSE_BUTTON_WHEEL_DOWN: dist = min(5.0, dist * 1.08)
		cam_pivot.get_child(0).position.z = dist
	elif e is InputEventKey and e.pressed:
		if e.keycode >= KEY_1 and e.keycode <= KEY_9:
			fabric_index = e.keycode - KEY_1
			_restart()
		elif e.keycode == KEY_R:
			_restart()
