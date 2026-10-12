# Builds a FailoringSim garment exactly like the browser worker's 'build' message
# (app/src/worker.js): one material, solver params, floor + baked mesh body, panels in plan
# order, seams by piece id, start positions from FailoringWrap, display mesh. Quality is the
# browser's Standard (edge 0.04, 2 substeps, 8 Newton, 48 settle frames).
class_name FailoringPlanLoader
extends RefCounted

const QUALITY := {"edge": 0.040, "sub": 2, "newton": 8, "frames": 48}
const SEAM_STIFFNESS := 20000.0
const RAMP_FRAMES := 20
const DISPLAY_MARGIN := 0.0008
const GRAVITY := -9.81


static func load_json(path: String) -> Dictionary:
	var text := FileAccess.get_file_as_string(path)
	if text.is_empty():
		return {}
	var v = JSON.parse_string(text)
	return v if v is Dictionary else {}


# plan pieces use [x, y] pairs; the sim takes PackedVector2Array.
static func to_outline(raw: Array) -> PackedVector2Array:
	var o := PackedVector2Array()
	o.resize(raw.size())
	for i in raw.size():
		o[i] = Vector2(float(raw[i][0]), float(raw[i][1]))
	return o


# Mirrors worker.js build(): returns {sim, order, first, n} or {} on error.
static func build(plan: Dictionary, sections: Dictionary, arms: Dictionary, standing: PackedVector3Array, body_tris: PackedInt32Array, fabric_id: String) -> Dictionary:
	var sim := FailoringSim.new()
	if not sim.load_materials("res://materials/fabrics.json"):
		push_error("plan_loader: cannot load materials")
		return {}
	sim.set_param("substeps", QUALITY["sub"])
	sim.set_param("newton_iterations", QUALITY["newton"])
	sim.set_param("seam_stiffness", SEAM_STIFFNESS)
	sim.set_param("seam_ramp_frames", RAMP_FRAMES)
	sim.set_param("self_collision", 0)
	sim.set_param("tension_stiffness", 1)  # the browser turns this on for garments
	sim.add_plane(Vector3.ZERO, Vector3.UP)  # feet stand on y = 0, like the browser floor
	if sim.set_body_mesh(standing, body_tris, 0.007) < 0.0:
		push_error("plan_loader: body bake failed")
		return {}
	sim.begin()
	var order: Array = []
	var first := {}
	for pc in plan["pieces"]:
		var idx := sim.add_polygon_panel(to_outline(pc["outline"]), QUALITY["edge"], fabric_id,
			Vector3.ZERO, Vector3.RIGHT, Vector3.UP, float(pc.get("grain", 0.0)), String(pc.get("id", "?")))
		if idx < 0:
			push_error("plan_loader: panel failed: " + String(pc.get("id", "?")))
			return {}
		order.append(String(pc["id"]))
		first[String(pc["id"])] = sim.get_first_vertex(idx)
	var index_of := {}
	for i in order.size():
		index_of[order[i]] = i
	for s in plan.get("seams", []):
		var a: String = s["a"][0]
		var b: String = s["b"][0]
		if not index_of.has(a) or not index_of.has(b):
			continue
		if a == b and int(s["a"][1]) == int(s["b"][1]):
			continue  # a seam may join two edges of one piece, but not an edge to itself
		sim.add_seam(index_of[a], int(s["a"][1]), index_of[b], int(s["b"][1]), bool(s.get("reverse", false)))
	# Start positions: the wrap evaluated at the solver's own mesh vertices (worker.js P2).
	var pos := sim.get_positions()
	for pc in plan["pieces"]:
		var pid := String(pc["id"])
		var wrap := FailoringWrap.new(pc, sections, arms)
		var uv := sim.get_panel_vertices_2d(index_of[pid])
		var f0: int = first[pid]
		for i in uv.size():
			pos[f0 + i] = wrap.point(uv[i].x, uv[i].y)
	sim.set_positions(pos)
	sim.setup_display()
	return {"sim": sim, "order": order, "first": first, "n": sim.get_vertex_count(), "warn": sim.get_warnings()}
