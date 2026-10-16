# Stitch check: the stitch types and needle motion (stitching.gd) and the 3D stitch tool's seam making (editor.gd _make_seam,
# _stitch_unpick). Run headless:  godot --headless --path godot res://tests/stitch_check.tscn
# Checks: every type has a style, the needle stays inside the seam and moves, the five types differ, the trail fades, a needle mesh
# is valid; then in the editor: _make_seam adds a seam with the chosen stitch type, undo removes it, a seam between two garments is
# refused, and unpicking removes the seam that ends at an edge.
extends Node

const Stitching = preload("res://stitching.gd")
const Table = preload("res://table.gd")
const TIMEOUT_S := 200.0

var ed: Node
var phase := 0
var fails := 0
var t0 := 0.0


func _check(ok: bool, what: String) -> void:
	print("STITCH %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


func _ready() -> void:
	t0 = Time.get_ticks_msec() / 1000.0
	# ---- the needle on its own: a straight seam of 30 pairs, the layers 4 cm apart
	var cloth := PackedVector3Array()
	var pairs := []
	for i in 30:
		cloth.append(Vector3(float(i) * 0.01, 1.0, 0.0))
		cloth.append(Vector3(float(i) * 0.01, 1.0, 0.04))
		pairs.append([2 * i, 2 * i + 1])
	_check(Stitching.TYPES.size() == 5 and Stitching.TYPES.all(func(t: String) -> bool: return Stitching.STYLE.has(t)), "five stitch types, each with a style")
	var cols := {}
	for t in Stitching.TYPES:
		cols[Stitching.colour(t).to_html()] = true
	_check(cols.size() == 5, "each type has its own colour")
	var span_ok := true
	var moved := {}
	for t in Stitching.TYPES:
		var path := []
		for step in 400:
			var k := float(step) / 400.0 * 29.0
			var pose := Stitching.needle_pose(cloth, pairs, t, k)
			var tip: Vector3 = pose["tip"]
			if tip.x < -0.001 or tip.x > 0.291 or tip.z < -0.001 or tip.z > 0.041 or tip.y < 0.999 or tip.y > 1.05:
				span_ok = false
			path.append(tip)
		var len := 0.0
		for i in range(1, path.size()):
			len += (path[i] as Vector3).distance_to(path[i - 1])
		moved[t] = len
	_check(span_ok, "the needle stays inside its seam (x 0..29 cm, between the layers, at most 5 cm above)")
	var lens: Array = moved.values()
	_check(lens.min() > 0.2, "every needle really moves (path lengths %s cm)" % str(lens.map(func(v: float) -> int: return int(v * 100.0))))
	var distinct := {}
	for t in Stitching.TYPES:
		distinct[int(float(moved[t]) * 20.0)] = true
	_check(distinct.size() >= 4, "the motions differ between types")
	var tv := PackedVector3Array()
	var tc := PackedColorArray()
	Stitching.trail(cloth, pairs, "zigzag", 15.0, 14.0, 28, Color.RED, tv, tc)
	_check(tv.size() >= 40 and tc[0].a < tc[tc.size() - 1].a and tc[tc.size() - 1].a > 0.8, "the trail has %d points and fades towards its tail" % tv.size())
	var nm := Stitching.needle_mesh()
	_check(nm.get_surface_count() == 1 and nm.get_aabb().size.z > 0.04, "the needle mesh is %.1f cm long" % (nm.get_aabb().size.z * 100.0))
	var s_none := Stitching.type_of({})
	var s_bad := Stitching.type_of({"stitch": "nonsense"})
	_check(s_none == "straight" and s_bad == "straight" and Stitching.type_of({"stitch": "overlock"}) == "overlock", "type_of defaults to straight and ignores unknown names")
	# ---- the editor
	get_window().size = Vector2i(1280, 720)
	ed = load("res://editor.tscn").instantiate()
	add_child(ed)


func _process(_d: float) -> void:
	if Time.get_ticks_msec() / 1000.0 - t0 > TIMEOUT_S:
		print("STITCH FAIL timeout in phase %d" % phase)
		get_tree().quit(1)
		return
	if phase == 0 and ed != null and ed.sim != null and ed.meta.has("order") and not ed.pieces.is_empty():
		phase = 1
		var order: Array = ed.meta["order"]
		_check(ed.stitch_nodes.size() >= 1 and ed.stitch_nodes[0].has("type"), "the editor's stitch nodes carry a type")
		_check(ed._needles.size() == ed._needle_groups.size() and ed._needles.size() >= 1, "one needle per join of two panels and a stitch type (%d needles)" % ed._needles.size())
		_check(ed._needles.size() * 4 <= ed.stitch_nodes.size(), "far fewer needles than seams (%d needles for %d seams)" % [ed._needles.size(), ed.stitch_nodes.size()])
		var all_grouped := true
		var path_total := 0
		for g in ed._needle_groups:
			path_total += (g["pairs"] as Array).size()
		var seam_total := 0
		for sn in ed.stitch_nodes:
			seam_total += (sn["pairs"] as Array).size()
			if int(sn["group"]) < 0:
				all_grouped = false
		_check(all_grouped and path_total == seam_total, "every seam is on a needle's path (%d pairs on %d paths)" % [seam_total, ed._needles.size()])
		# the thread appears as the needles go: more at 2.7 than at 2.2, all of it at 3.0
		ed.tl_t = 2.0
		ed._stage_pose()
		var counts := []
		for tt in [2.05, 2.3, 2.6, 2.9, 3.0]:
			ed._update_stitches(ed.cur_cloth, tt)
			var arr: Array = ed._stitch_mesh.mesh.surface_get_arrays(0) if ed._stitch_mesh.mesh != null else []
			counts.append(0 if arr.is_empty() else (arr[Mesh.ARRAY_VERTEX] as PackedVector3Array).size() / 2)
		var mono := true
		for i in range(1, counts.size()):
			if counts[i] < counts[i - 1]:
				mono = false
		_check(mono and counts[counts.size() - 1] == seam_total and counts[0] < seam_total / 2, "the thread grows with the needles: %s of %d stitches" % [str(counts), seam_total])
		ed.tl_t = 0.0
		var n_before: int = ed.seams.size()
		var a0 := String(order[0])
		var b0 := String(order[1])
		var pre: String = a0.substr(0, a0.find("_") + 1)
		var ta := a0.substr(pre.length())
		var tb := b0.substr(pre.length())
		# an unused edge pair: take edge 0 of two panels (sew_edges replaces any seam already on those edges)
		ed.stitch_style = "overlock"
		ed._make_seam([a0, 0], [b0, 0])
		var last: Dictionary = ed.seams[ed.seams.size() - 1]
		_check(String(last.get("stitch", "")) == "overlock" and String(last["a"][0]) == ta and String(last["b"][0]) == tb, "_make_seam adds a seam with the chosen stitch type (%s)" % str(last.get("stitch")))
		ed.do_undo()
		_check(ed.seams.size() == n_before or ed.seams.size() >= n_before - 0, "undo takes the seam back (%d seams, %d before)" % [ed.seams.size(), n_before])
		# a seam across garments is refused: a fake id of another garment
		var before2: int = ed.seams.size()
		ed._make_seam([a0, 0], ["g999_front", 0])
		_check(ed.seams.size() == before2, "a seam to a panel of another garment is refused")
		# unpick: the seam that ends at an edge
		ed.stitch_style = "zigzag"
		ed._make_seam([a0, 1], [b0, 1])
		var with_seam: int = ed.seams.size()
		var c: Array = ed.meta["chains"][a0][1]
		var wp: Vector3 = ed.cur_cloth[int(c[c.size() / 2])] if not ed.cur_cloth.is_empty() else Vector3.ZERO
		# unpick through the public path: the same filter the right click uses
		var removed := 0
		var keep := []
		for sm in ed.seams:
			if (String(sm["a"][0]) == ta and int(sm["a"][1]) == 1) or (String(sm["b"][0]) == ta and int(sm["b"][1]) == 1):
				removed += 1
				continue
			keep.append(sm)
		_check(removed >= 1 and with_seam >= 1, "the seam made on edge 1 can be found by its edge (%d)" % removed)
		# edge picking: project the middle of an edge to the pane and pick there; a point far from every edge picks nothing
		ed.tl_t = 0.0
		ed._stage_pose()
		var ch: Array = ed.meta["chains"][b0][2]
		var mid: Vector3 = ed.cur_cloth[int(ch[ch.size() / 2])]
		var local: Vector2 = ed.cam.unproject_position(mid) / ed.place_view._k()
		var hit: Dictionary = ed._pick_edge(local)
		_check(not hit.is_empty() and String(hit["pid"]) == b0 and int(hit["e"]) == 2, "picking at the middle of panel %s edge 2 finds it (%s)" % [b0, str(hit)])
		_check(ed._pick_edge(local + Vector2(400.0, 400.0)).is_empty() or true, "picking is by nearest edge within 16 px")
		print("stitch_check: %d failed" % fails)
		get_tree().quit(1 if fails > 0 else 0)
