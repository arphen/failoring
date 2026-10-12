# Checks the FailoringSim API fixes listed in docs/NATIVE_NOTES.md. Run headless:
#   godot --headless --path godot res://tests/api_smoke.tscn     (prints PASS/FAIL lines, then "RESULT: n failed")
# The push_error lines between them are expected: they come from the calls that are meant to be rejected.
extends Node3D
var fails := 0
func check(name: String, ok: bool) -> void:
	print(("PASS  " if ok else "FAIL  ") + name)
	if not ok: fails += 1
func _ready() -> void:
	var sim := FailoringSim.new()
	check("load_materials", sim.load_materials("res://materials/fabrics.json"))
	check("reset_velocities removed", not sim.has_method("reset_velocities"))
	check("get_solver_stats bound", sim.has_method("get_solver_stats") and sim.has_method("add_material"))
	# 6 custom material: round trip + use
	var m := sim.get_material("cotton_poplin")
	m["damping"] = 0.9
	check("add_material (new id)", sim.add_material("test_fabric", m))
	check("custom material in ids", "test_fabric" in sim.get_material_ids())
	check("custom damping kept", is_equal_approx(sim.get_material("test_fabric")["damping"], 0.9))
	check("add_material rejects bad value", not sim.add_material("bad", {"density_g_m2": -5}))
	check("add_material rejects empty id", not sim.add_material("", {}))
	sim.begin()
	# 1 clockwise polygon is reversed to counter-clockwise (edge 0 then runs along the TOP of the square)
	var cw := PackedVector2Array([Vector2(0,0), Vector2(0,0.2), Vector2(0.2,0.2), Vector2(0.2,0)])
	var p := sim.add_polygon_panel(cw, 0.02, "test_fabric", Vector3.ZERO, Vector3.RIGHT, Vector3.UP, 0.0, "cw")
	check("cw polygon accepted", p == 0)
	var uv := sim.get_panel_vertices_2d(p)
	var chain := sim.get_edge_chain(p, 0)
	var first := sim.get_first_vertex(p)
	var xs := 0.0
	for v in chain: xs += uv[v - first].x
	check("cw outline normalised: edge 0 is the right side x=0.2 (unreversed it would be x=0)", chain.size() > 1 and is_equal_approx(xs / chain.size(), 0.2))
	# 5 raw uv with a grain angle
	var r := sim.add_rect_panel(0.3, 0.1, 0.02, "test_fabric", Vector3(0,0,0.1), Vector3.RIGHT, Vector3.UP, PI/2, "rect")
	var ruv := sim.get_panel_vertices_2d(r)
	var mx := Vector2(-1e9,-1e9)
	for q in ruv: mx = mx.max(q)
	check("rect raw uv ignores grain (max = 0.3, 0.1)", is_equal_approx(mx.x, 0.3) and is_equal_approx(mx.y, 0.1))
	check("rect panel edge count 4 (edge 4 rejected)", not sim.add_seam(p, 0, r, 4, false))
	check("add_seam ok", sim.add_seam(p, 1, r, 3, true))
	check("bad panel index", sim.add_rect_panel(-1.0, 1.0, 0.02, "test_fabric", Vector3.ZERO, Vector3.RIGHT, Vector3.UP, 0.0, "x") == -1)
	# 7 display staleness
	print("expect 2 push_error lines next: display before setup")
	check("display before setup_display -> empty", sim.get_display_indices().size() == 0 and sim.get_display_positions(0.0008).size() == 0)
	sim.setup_display()
	check("display after setup ok", sim.get_display_indices().size() > 0 and sim.get_display_positions(0.0008).size() > sim.get_vertex_count())
	var extra := sim.add_rect_panel(0.1, 0.1, 0.02, "test_fabric", Vector3.ZERO, Vector3.RIGHT, Vector3.UP, 0.0, "late")
	check("panel after setup_display accepted", extra >= 0)
	print("expect push_error lines next: stale display")
	check("stale display detected -> empty", sim.get_display_indices().size() == 0 and sim.get_display_positions(0.0008).size() == 0)
	sim.setup_display()
	check("setup_display again restores", sim.get_display_indices().size() > 0)
	# 11 motion size
	print("expect push_error next: wrong motion size")
	sim.set_body_motion(PackedVector3Array([Vector3.ZERO]))
	sim.set_body_motion(PackedVector3Array())
	# 10 body_eval with a disabled body
	sim.set_body_mesh(PackedVector3Array([Vector3(0,1,0), Vector3(1,0,0), Vector3(0,0,1), Vector3(-1,0,0), Vector3(0,0,-1), Vector3(0,-1,0)]), PackedInt32Array([0,2,1, 0,3,2, 0,4,3, 0,1,4, 5,1,2, 5,2,3, 5,3,4, 5,4,1]), 0.05)
	sim.set_body_enabled(false)
	check("body_eval works with a disabled body (like wasm)", sim.body_eval(PackedVector3Array([Vector3(3,0,0)]))[0] < 100.0)
	# 6 exceptions after the first step must not crash
	sim.step(1)
	print("expect push_error lines next: panel/seam after start")
	check("add panel after step -> -1 (no crash)", sim.add_rect_panel(0.1, 0.1, 0.02, "test_fabric", Vector3.ZERO, Vector3.RIGHT, Vector3.UP, 0.0, "late2") == -1)
	check("add_seam after step -> false (no crash)", not sim.add_seam(p, 1, r, 3, true))
	check("solver stats after one frame", sim.get_solver_stats()["substeps"] > 0)
	print("RESULT: %d failed" % fails)
	get_tree().quit()
