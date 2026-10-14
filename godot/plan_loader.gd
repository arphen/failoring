# Builds a FailoringSim garment exactly like the browser worker's 'build' message
# (app/src/worker.js): one material, solver params, floor + baked mesh body, panels in plan
# order, seams by piece id, start positions from FailoringWrap, display mesh. Quality is the
# browser's Standard (edge 0.04, 2 substeps, 8 Newton, 48 settle frames).
class_name FailoringPlanLoader
extends RefCounted

const Wrap = preload("res://wrap.gd")
const Seams = preload("res://seams.gd")
const Place3D = preload("res://place3d.gd")

const DISPLAY_MARGIN := 0.0008


static func load_json(path: String) -> Dictionary:
	var text := FileAccess.get_file_as_string(path)
	if text.is_empty():
		return {}
	var v = JSON.parse_string(text)
	return v if v is Dictionary else {}


static func signed_area(outline: Array) -> float:
	var a := 0.0
	for i in outline.size():
		var q: Array = outline[(i + 1) % outline.size()]
		a += float(outline[i][0]) * float(q[1]) - float(q[0]) * float(outline[i][1])
	return a * 0.5


# Mirrors App.loadPattern: counter-clockwise outlines (remapping seam edges when a
# clockwise outline is reversed) plus piece defaults. Returns a deep copy.
static func normalize_plan(plan: Dictionary) -> Dictionary:
	var o: Dictionary = plan.duplicate(true)
	var seams: Array = o.get("seams", [])
	for p in o.get("pieces", []):
		var n: int = (p["outline"] as Array).size()
		if signed_area(p["outline"]) < 0.0:
			(p["outline"] as Array).reverse()
			for s in seams:
				for side in ["a", "b"]:
					if String(s[side][0]) == String(p["id"]):
						s[side] = Seams.reversed_side(s[side], n)
						s["reverse"] = not bool(s.get("reverse", false))
		p["grain"] = float(p.get("grain", 0.0))
		if not (p.get("table") is Dictionary):
			p["table"] = {"x": 0.0, "y": 0.0}
		if not (p.get("wrap") is Dictionary):
			p["wrap"] = {"theta": 0.0, "y": 0.5, "clear": 0.012}
		p["name"] = String(p.get("name", p.get("id", "?")))
	o["seams"] = seams.filter(func(s) -> bool: return s.has("a") and s.has("b"))
	return o


# plan pieces use [x, y] pairs; the sim takes PackedVector2Array.
static func to_outline(raw: Array) -> PackedVector2Array:
	var o := PackedVector2Array()
	o.resize(raw.size())
	for i in raw.size():
		o[i] = Vector2(float(raw[i][0]), float(raw[i][1]))
	return o


# Mirrors worker.js build(): returns {sim, order, first, n} or {} on error.
static func build(plan: Dictionary, sections: Dictionary, arms: Dictionary, standing: PackedVector3Array, body_tris: PackedInt32Array, fabric_id: String, quality: Dictionary, seam_stiffness: float, ramp_frames: int, self_collision: bool) -> Dictionary:
	var sim := FailoringSim.new()
	if not sim.load_materials("res://materials/fabrics.json"):
		push_error("plan_loader: cannot load materials")
		return {}
	sim.set_param("substeps", quality["sub"])
	sim.set_param("newton_iterations", quality["newton"])
	sim.set_param("seam_stiffness", seam_stiffness)
	sim.set_param("seam_ramp_frames", ramp_frames)
	sim.set_param("self_collision", 1 if self_collision else 0)
	sim.set_param("tension_stiffness", 1)  # the browser turns this on for garments
	# Skin friction inside the Newton solve (cloth_friction.cpp, mode 2): Coulomb with the contact force. The solver's
	# default (mode 0) scales friction with how deep a vertex sank into the body after the solve, and the contact keeps
	# that depth near zero, so it was almost frictionless: on a 15 degree slope a cotton patch slid 290 mm in 0.5 s where
	# Coulomb says 0 (test_friction). Walking average body, preview: cloth on the skin slid 4.4 -> 1.1 mm/frame (chinos)
	# and 3.2 -> 1.6 (tee), the tee sagged 18 -> 9 mm over the walk, its standing motion 2.1 -> 0.3 mm/frame.
	# FL_FRICTION=0 restores the old mode (comparisons).
	sim.set_param("body_friction_mode", float(OS.get_environment("FL_FRICTION")) if OS.has_environment("FL_FRICTION") else 2.0)
	if OS.has_environment("FL_SLIP"):
		sim.set_param("friction_slip_velocity", float(OS.get_environment("FL_SLIP")))
	sim.add_plane(Vector3.ZERO, Vector3.UP)  # feet stand on y = 0, like the browser floor
	if sim.set_body_mesh(standing, body_tris, float(quality.get("body_h", 0.007))) < 0.0:
		push_error("plan_loader: body bake failed")
		return {}
	sim.begin()
	var order: Array = []
	var first := {}
	for pc in plan["pieces"]:
		# an outfit's merged plan carries each piece's own fabric (outfit.gd); a single garment uses fabric_id
		var fab := String(pc.get("fabric", fabric_id))
		var holes: Array = pc.get("holes", [])
		var idx := -1
		if holes.is_empty():
			idx = sim.add_polygon_panel(to_outline(pc["outline"]), quality["edge"], fab,
				Vector3.ZERO, Vector3.RIGHT, Vector3.UP, float(pc.get("grain", 0.0)), String(pc.get("id", "?")))
		else:
			# holes (a neck hole, a vent): the mesh has no cloth inside them and their edges are free
			var hole_outlines := []
			for h in holes:
				hole_outlines.append(to_outline(h))
			idx = sim.add_polygon_panel_with_holes(to_outline(pc["outline"]), hole_outlines, quality["edge"], fab,
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
	var uvs := {}
	var ranges := {}
	for pc in plan["pieces"]:
		var pid := String(pc["id"])
		var wrap := Wrap.new(pc, sections, arms)
		var uv := sim.get_panel_vertices_2d(index_of[pid])
		var f0: int = first[pid]
		uvs[pid] = uv
		ranges[pid] = {"first": f0, "count": uv.size()}
		for i in uv.size():
			pos[f0 + i] = wrap.point(uv[i].x, uv[i].y)
		Place3D.apply(pos, f0, uv.size(), pc)  # the piece's free move and turn in 3D, if it has one (place3d.gd)
	sim.set_positions(pos)
	sim.setup_display()
	var chains := {}
	for pc in plan["pieces"]:
		var pid := String(pc["id"])
		var edge_lists := []
		for e in (pc["outline"] as Array).size():
			edge_lists.append(sim.get_edge_chain(index_of[pid], e))
		chains[pid] = edge_lists
	return {"sim": sim, "order": order, "first": first, "n": sim.get_vertex_count(), "warn": sim.get_warnings(),
		"uv": uvs, "ranges": ranges, "chains": chains, "p2": pos.duplicate(), "tris": sim.get_indices()}
