# Walking-body demo: the call sequence for FailoringSim (gdextension/src/failoring_sim.h) with a mesh body that moves
# every frame, the same flow as the browser's walking mannequin (app/src/worker.js).
#
# Run headless (30+ frames verified Linux and macOS) or with a real window. The body pose below is a placeholder
# (a slow sway of the rest mesh); a real walk needs the skinned body's vertices each frame.
# Per-frame stats print as FRAME lines (total/bake/step ms, Newton, substeps, line-search failures, CG, clearance,
# seam gap, kinetic energy); FL_SHOOT=1 saves godot/docs-shots/front.png + side.png and quits (needs a real window).
# A skinned body would feed its vertices each frame, e.g. from a Skeleton3D or a precomputed gait sheet.
#
# Call order:
#   1. sim = FailoringSim.new(); sim.load_materials(path)            fabrics.json
#   2. sim.set_param(...)                                             solver parameters
#   3. sim.set_body_mesh(verts, tris, 0.007)                          bake the body grid once (returns the bake ms)
#   4. sim.begin()                                                    a fresh simulation (panels, seams, display cleared)
#   5. sim.add_rect_panel(...) x2                                     pattern pieces
#   6. sim.add_seam(panel_a, edge_a, panel_b, edge_b, reverse)        sewing
#   7. sim.set_positions(positions)                                   place the pieces (here: pre-wrapped on the body)
#   8. sim.setup_display()                                            display mesh topology, after the panels exist
#   9. every physics frame:
#        sim.set_body_positions(body_pose)                            re-bake the moved body and set its surface motion
#        sim.step(1)                                                  one solver frame (clears the surface motion)
#        sim.get_display_positions(0.0008)                            draw these with get_display_indices()
#
# The surface motion describes ONE frame and step() clears it afterwards, so a moving body is baked before every step,
# as the browser does. Skipping bakes makes the body jump and leaves the friction without surface motion in between.
#
# Cost: set_body_positions bakes the whole grid every call (about 0.2-0.4 s per mannequin in the browser build, less
# native), and a solver frame is 0.1-0.3 s for a garment of this size. This demo therefore runs far slower than 60 fps;
# for real time move the body only as often as the picture needs (or bake on a worker thread, never touching sim from
# two threads) and accept that the cloth lags the body.
extends Node3D

const RADII := Vector3(0.17, 0.35, 0.11)  # body: vertical ellipsoid (placeholder for a scanned or parametric mannequin)
const W := 0.40                            # tube circumference is 2 W
const H := 0.30
const Y0 := 0.25
const BODY_EDGE := 0.007                   # SDF grid spacing (m)
const BAKE_EVERY := 1                      # frames between body re-bakes; keep 1 (see the cost note above)

var sim: FailoringSim
var body_verts: PackedVector3Array
var body_tris: PackedInt32Array
var rest_verts: PackedVector3Array
var frame := 0
var cloth_mesh: ArrayMesh
var cloth_instance: MeshInstance3D
var body_mesh: ArrayMesh
var body_instance: MeshInstance3D
var body_pose: PackedVector3Array   # current body verts for the visible mesh (rest pose until the first bake)
var cam: Camera3D
var startup_usec := 0
var shoot := false   # FL_SHOOT=1: save front/side PNGs to res://docs-shots and quit (real window, not --headless)


func _ready() -> void:
	startup_usec = Time.get_ticks_usec()
	shoot = OS.has_environment("FL_SHOOT")
	_build_scene()
	_start()
	print("STARTUP engine_to_ready_ms=%.1f scene_build_ms=%.1f" % [Time.get_ticks_msec(), (Time.get_ticks_usec() - startup_usec) / 1000.0])


func _build_scene() -> void:
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	add_child(sun)
	cam = Camera3D.new()
	cam.position = Vector3(0, 0.3, 1.6)
	add_child(cam)
	cam.look_at(Vector3(0, 0.1, 0))
	cloth_mesh = ArrayMesh.new()
	cloth_instance = MeshInstance3D.new()
	cloth_instance.mesh = cloth_mesh
	add_child(cloth_instance)
	body_mesh = ArrayMesh.new()
	body_instance = MeshInstance3D.new()
	body_instance.mesh = body_mesh
	var skin := StandardMaterial3D.new()
	skin.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	skin.albedo_color = Color(0.85, 0.68, 0.58)
	body_instance.material_override = skin
	add_child(body_instance)


func _start() -> void:
	# 1. materials and parameters
	sim = FailoringSim.new()
	if not sim.load_materials("res://materials/fabrics.json"):
		return
	sim.set_param("substeps", 4)
	sim.set_param("seam_ramp_frames", 0)
	sim.set_param("tension_stiffness", 1)   # the browser app turns this on for garments

	# 3. body: a closed mesh, counter-clockwise seen from outside, baked into the cloth collider
	_make_body_ellipsoid(RADII, 128, 64)
	var bake_ms := sim.set_body_mesh(body_verts, body_tris, BODY_EDGE)
	if bake_ms < 0.0:
		push_error("body bake failed")
		return
	print("body baked in %.1f ms: %s" % [bake_ms, sim.get_body_stats()])

	# 4-6. pattern pieces and seams (the same tube as the drape test)
	sim.begin()
	var id := "cotton_poplin"
	var front := sim.add_rect_panel(W, H, 0.02, id, Vector3(-W / 2, Y0, 0.145), Vector3.RIGHT, Vector3.UP, PI / 2, "front")
	var back := sim.add_rect_panel(W, H, 0.02, id, Vector3(W / 2, Y0, -0.145), Vector3.LEFT, Vector3.UP, PI / 2, "back")
	if front < 0 or back < 0:
		push_error("panel creation failed (material id %s?)" % id)
		return
	sim.add_seam(front, 1, back, 3, true)
	sim.add_seam(front, 3, back, 1, true)

	# 7. start the panels pre-wrapped on a tube of circumference 2 W, so the seams begin closed.
	# get_panel_vertices_2d returns the pattern (u,v) of each vertex (u in 0..W across, v in 0..H up), before the grain.
	var pos := sim.get_positions()
	var rc := W / PI
	for p in [front, back]:
		var first := sim.get_first_vertex(p)
		var uv := sim.get_panel_vertices_2d(p)
		for i in range(uv.size()):
			var th := PI * uv[i].x / W
			var y := Y0 + uv[i].y
			var q: Vector3
			if p == front:
				q = Vector3(-rc * cos(th), y, rc * sin(th))
			else:
				q = Vector3(rc * cos(th), y, -rc * sin(th))
			pos[first + i] = q
	sim.set_positions(pos)

	# 8. display mesh
	sim.setup_display()
	rest_verts = body_verts.duplicate()
	body_pose = rest_verts.duplicate()
	_draw_body()


func _physics_process(_delta: float) -> void:
	if sim == null:
		return
	frame += 1
	var t0 := Time.get_ticks_usec()
	# 9. body moves; re-bake every BAKE_EVERY frames (placeholder pose: rest mesh swaying 2 cm)
	var bake_ms := 0.0
	if frame % BAKE_EVERY == 0:
		var pose := PackedVector3Array()
		pose.resize(rest_verts.size())
		var dx := 0.02 * sin(frame / 30.0)
		for i in range(rest_verts.size()):
			pose[i] = rest_verts[i] + Vector3(dx, 0, 0)
		bake_ms = sim.set_body_positions(pose)
		body_pose = pose
	var t1 := Time.get_ticks_usec()
	sim.step(1)
	var t2 := Time.get_ticks_usec()
	_draw_cloth()
	_draw_body()
	var st := sim.get_solver_stats()
	print("FRAME %d total_ms=%.1f bake_ms=%.1f step_ms=%.1f newton=%d substeps=%d lsfail=%d cg=%d clear_mm=%.2f gap_mm=%.2f ke=%s" % [
		frame, (t2 - t0) / 1000.0, bake_ms, (t2 - t1) / 1000.0,
		int(st.get("newton_iterations", -1)), int(st.get("substeps", -1)),
		int(st.get("line_search_failures", -1)), int(st.get("cg_iterations", -1)),
		sim.get_min_body_clearance() * 1000.0, sim.get_max_seam_gap() * 1000.0, str(sim.get_kinetic_energy())])
	if shoot:
		if frame == 30:
			_shot("front")
		elif frame == 31:
			cam.position = Vector3(1.6, 0.3, 0)
			cam.look_at(Vector3(0, 0.1, 0))
		elif frame == 33:
			_shot("side")
			get_tree().quit()


func _draw_body() -> void:
	if body_pose.is_empty() or body_tris.is_empty():
		return
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = body_pose
	arr[Mesh.ARRAY_INDEX] = body_tris
	body_mesh.clear_surfaces()
	body_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)


func _shot(tag: String) -> void:
	DirAccess.make_dir_recursive_absolute("res://docs-shots")
	var img := get_viewport().get_texture().get_image()
	var p := "res://docs-shots/" + tag + ".png"
	if img.save_png(p) == OK:
		print("SHOT saved ", p, " ", img.get_size())
	else:
		push_error("SHOT failed for " + p)


func _draw_cloth() -> void:
	var disp: PackedVector3Array = sim.get_display_positions(0.0008)
	var idx: PackedInt32Array = sim.get_display_indices()
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = disp
	arr[Mesh.ARRAY_INDEX] = idx
	cloth_mesh.clear_surfaces()
	cloth_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)


# UV-sphere ellipsoid (placeholder for a real body mesh). Fills body_verts and body_tris (3 indices per triangle).
func _make_body_ellipsoid(r: Vector3, nu: int, nv: int) -> void:
	body_verts = PackedVector3Array()
	body_tris = PackedInt32Array()
	body_verts.append(Vector3(0, r.y, 0))
	for j in range(1, nv):
		var th := PI * j / nv
		for i in range(nu):
			var ph := 2.0 * PI * i / nu
			body_verts.append(Vector3(r.x * sin(th) * cos(ph), r.y * cos(th), r.z * sin(th) * sin(ph)))
	body_verts.append(Vector3(0, -r.y, 0))
	var south := body_verts.size() - 1
	for i in range(nu):
		body_tris.append_array(PackedInt32Array([0, _ring(1, i + 1, nu), _ring(1, i, nu)]))
	for j in range(1, nv - 1):
		for i in range(nu):
			var a := _ring(j, i, nu)
			var b := _ring(j, i + 1, nu)
			var c := _ring(j + 1, i, nu)
			var d := _ring(j + 1, i + 1, nu)
			body_tris.append_array(PackedInt32Array([a, b, c, b, d, c]))
	for i in range(nu):
		body_tris.append_array(PackedInt32Array([_ring(nv - 1, i, nu), _ring(nv - 1, i + 1, nu), south]))


func _ring(j: int, i: int, nu: int) -> int:
	return 1 + (j - 1) * nu + (i % nu)
