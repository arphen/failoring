# Outfit check: a tee and chinos on the average preset, merged into ONE simulation (outfit.gd), stepped a few frames, then
# the same outfit run through the editor (add a garment, hide one, rebuild). Run headless:
#   godot --headless --path godot res://tests/outfit_check.tscn
# Prints OUTFIT lines and quits 0 when every check passes, 1 otherwise. Checks: the merged plan keeps every piece and
# seam of both garments (counts add up, ids unique), the solver has one panel per merged piece and the cloth vertex
# count adds up, each piece keeps its garment's fabric, each outer layer's wrap clearance is 8 mm more than the one
# under it, every cloth position is finite after the frames, and the editor's outfit path rebuilds and steps.
extends Node

const PlanLoader = preload("res://plan_loader.gd")
const BodyGen = preload("res://body_gen.gd")
const Outfit = preload("res://outfit.gd")
const Table = preload("res://table.gd")
const Library = preload("res://library.gd")

const FRAMES := 3
const QUALITY := {"edge": 0.040, "sub": 2, "newton": 8}
const TIMEOUT_S := 240.0

var fails := 0
var ed: Node
var phase := 0
var t_start := 0.0


func _check(ok: bool, what: String) -> void:
	print("OUTFIT %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


func _load(path: String) -> Dictionary:
	var v = JSON.parse_string(FileAccess.get_file_as_string(path))
	return v if v is Dictionary else {}


func _finite(p: PackedVector3Array) -> bool:
	for v in p:
		if not (is_finite(v.x) and is_finite(v.y) and is_finite(v.z)):
			return false
	return true


func _ready() -> void:
	t_start = Time.get_ticks_msec() / 1000.0
	var data := _load("res://assets/body_data.json")
	if data.is_empty():
		print("OUTFIT FAIL missing body_data.json")
		get_tree().quit(1)
		return
	var M := BodyGen.load_data(data)
	var stand := BodyGen.stand(M, BodyGen.generate(M, BodyGen.PRESETS["average"], null))
	var tee := PlanLoader.normalize_plan(_load("res://assets/average/plan_teagan.json"))
	var chinos := PlanLoader.normalize_plan(_load("res://assets/average/plan_charlie.json"))
	_check(not tee.is_empty() and not chinos.is_empty(), "plans load: tee %d pieces, chinos %d pieces" % [tee.get("pieces", []).size(), chinos.get("pieces", []).size()])
	if fails > 0:
		get_tree().quit(1)
		return

	# layer 0 (tee) is the innermost, layer 1 (chinos) is over it
	var garments := [
		{"uid": 1, "fabric": String(tee.get("fabric", "cotton_poplin")), "pieces": tee["pieces"], "seams": tee["seams"]},
		{"uid": 2, "fabric": String(chinos.get("fabric", "cotton_poplin")), "pieces": chinos["pieces"], "seams": chinos["seams"]},
	]
	var m := Outfit.merge(garments)
	var n_t: int = tee["pieces"].size()
	var n_c: int = chinos["pieces"].size()
	_check(m["pieces"].size() == n_t + n_c, "pieces add up: %d = %d + %d" % [m["pieces"].size(), n_t, n_c])
	_check(m["seams"].size() == tee["seams"].size() + chinos["seams"].size(), "seams add up: %d" % m["seams"].size())
	var ids := {}
	for p in m["pieces"]:
		ids[String(p["id"])] = true
	_check(ids.size() == m["pieces"].size(), "merged piece ids are unique")

	# per piece: its own fabric, its layer's clearance, and the uid in the id matches the garment it came from
	var fab_ok := true
	var clear_ok := true
	var uid_ok := true
	for g in garments:
		var layer: int = int(m["layer"][int(g["uid"])])
		for src in g["pieces"]:
			var want_id := Outfit.prefix(int(g["uid"])) + String(src["id"])
			var got = Table.piece_by_id(m["pieces"], want_id)
			if got == null:
				uid_ok = false
				continue
			if String(got["fabric"]) != String(g["fabric"]) or int(got["garment"]) != int(g["uid"]):
				fab_ok = false
			var base := float((src.get("wrap", {}) as Dictionary).get("clear", 0.012))
			if absf(float(got["wrap"]["clear"]) - (base + Outfit.LAYER_CLEAR * float(layer))) > 1e-9:
				clear_ok = false
			if Outfit.uid_of(String(got["id"])) != int(g["uid"]):
				uid_ok = false
	_check(uid_ok, "every merged piece is found under its garment prefix")
	_check(fab_ok, "each piece keeps its garment's fabric and uid")
	_check(clear_ok, "outer layer clearance = inner + %.0f mm per layer" % (Outfit.LAYER_CLEAR * 1000.0))

	# one simulation: standing body from the preset, both garments in one FailoringSim
	var standing := PackedVector3Array()
	var sp: PackedFloat32Array = stand["pos"]
	standing.resize(sp.size() / 3)
	for i in standing.size():
		standing[i] = Vector3(sp[3 * i], sp[3 * i + 1], sp[3 * i + 2])
	var built := PlanLoader.build({"pieces": m["pieces"], "seams": m["seams"]}, stand["sections"], stand["arms"],
		standing, M["tris"], "cotton_poplin", QUALITY, 20000.0, 20, true)
	_check(not built.is_empty(), "outfit builds into one simulation")
	if built.is_empty():
		get_tree().quit(1)
		return
	var sim: FailoringSim = built["sim"]
	_check(sim.get_panel_count() == m["pieces"].size(), "solver has one panel per merged piece: %d" % sim.get_panel_count())
	var verts := 0
	for i in sim.get_panel_count():
		verts += sim.get_panel_vertex_count(i)
	_check(verts == int(built["n"]) and verts > 0, "cloth vertices add up: %d" % verts)
	for k in FRAMES:
		sim.step(1)
	_check(_finite(sim.get_positions()), "cloth positions finite after %d frames" % FRAMES)
	_check(sim.get_warnings() == 0, "no meshing warnings (bits %d)" % sim.get_warnings())
	print("OUTFIT frames %d  KE %.4f J  gap %.1f mm  clearance %.1f mm" % [FRAMES, sim.get_kinetic_energy(),
		sim.get_max_seam_gap() * 1000.0, sim.get_min_body_clearance() * 1000.0])
	sim = null

	# the editor's outfit path: boot (tee), add the chinos as the outer layer, hide the tee, rebuild, step
	ed = load("res://editor.tscn").instantiate()
	add_child(ed)
	phase = 1


func _process(_delta: float) -> void:
	if phase == 0:
		return
	if Time.get_ticks_msec() / 1000.0 - t_start > TIMEOUT_S:
		print("OUTFIT FAIL timeout in phase %d (frames %d)" % [phase, ed.frames.size()])
		get_tree().quit(1)
		return
	if phase == 1 and ed.sim != null:
		_check(ed.outfit.size() == 1, "editor boots with one garment (%d)" % ed.outfit.size())
		ed._on_library_add("fs-charlie")
		_check(ed.outfit.size() == 2 and ed.active == 1, "library add appends a garment and activates it")
		var want: int = Table.valid_pieces(ed.outfit[0]["pieces"])["pieces"].size() + Table.valid_pieces(ed.outfit[1]["pieces"])["pieces"].size()
		_check(ed._sim_pieces.size() == want, "editor merges both garments: %d pieces" % ed._sim_pieces.size())
		_check(ed.sim != null and ed.sim.get_panel_count() == ed._sim_pieces.size(), "editor builds one sim for the outfit")
		ed.tl_t = 3.0
		ed.playing = true
		phase = 2
	elif phase == 2 and ed.frames.size() >= FRAMES:
		ed.playing = false
		_check(_finite(ed.frames[-1]["cloth"]), "editor frames finite (%d frames)" % ed.frames.size())
		ed._outfit_set(0, "visible", false)
		ed.regen_and_rebuild()
		var shown: int = Table.valid_pieces(ed.outfit[1]["pieces"])["pieces"].size()
		_check(ed._sim_pieces.size() == shown and ed.sim.get_panel_count() == shown,
			"hiding the tee rebuilds with the chinos only (%d pieces)" % ed._sim_pieces.size())
		phase = 3
		_outfit_actions()
	elif phase == 4:
		remove_child(ed)
		ed.free()
		print("OUTFIT %s" % ("PASSED" if fails == 0 else "FAILED with %d failure(s)" % fails))
		get_tree().quit(0 if fails == 0 else 1)


# The outfit handlers the panel calls: show, reorder (layer clearance follows the order), fabric, re-draft, lock,
# save to the library, remove down to an empty outfit, and a drafted garment added back.
func _outfit_actions() -> void:
	ed._outfit_set(0, "visible", true)
	ed.regen_and_rebuild()
	_check(ed._sim_pieces.size() == 8, "showing the tee again restores both garments")
	var tee_uid: int = int(ed.outfit[0]["uid"])
	var active_before: int = ed.active_uid()
	ed._outfit_move(0, 1)  # chinos inner, tee outer
	_check(int(ed.outfit[1]["uid"]) == tee_uid, "move swaps the layers")
	_check(ed.active_uid() == active_before, "move keeps the active garment")
	ed.regen_and_rebuild()
	var clear_of := {}
	for p in ed._sim_pieces:
		clear_of[String(p["id"])] = float(p["wrap"]["clear"])
	var tee_piece := Outfit.prefix(tee_uid) + String(ed.outfit[1]["pieces"][0]["id"])
	var tee_base := float(ed.outfit[1]["pieces"][0]["wrap"]["clear"])
	_check(absf(float(clear_of.get(tee_piece, -1.0)) - (tee_base + Outfit.LAYER_CLEAR)) < 1e-9, "after the move the tee is the outer layer")
	ed._set_garment_fabric(1, "wool_suiting")
	_check(String(ed.outfit[1]["fabric"]) == "wool_suiting" and String(ed.outfit[1]["color"]) != "", "fabric change is per garment")
	ed._redraft_entry(1)
	_check(String(ed.outfit[1]["fitted"]) == ed.current_preset(), "re-draft fits the garment to the current body")
	ed._on_library_save("tops", "Outfit test tee")
	var saved: Array = []
	for it in Library.user_items():
		if String(it["name"]) == "Outfit test tee":
			saved.append(String(it["user_file"]))
	_check(saved.size() == 1, "save to library writes one user garment (%d)" % saved.size())
	if saved.size() == 1:
		_check(Library.rename(saved[0], "Renamed test tee"), "rename a saved garment")
		var renamed := 0
		for it in Library.user_items():
			if String(it["name"]) == "Renamed test tee":
				renamed += 1
		_check(renamed == 1, "the renamed garment is listed under its new name")
		_check(Library.delete(saved[0]), "delete a saved garment")
		var left := 0
		for it in Library.user_items():
			if String(it["name"]).begins_with("Renamed test") or String(it["name"]).begins_with("Outfit test"):
				left += 1
		_check(left == 0, "the test garment is gone from the library")
	ed._outfit_set(1, "locked", true)
	_check(bool(ed.outfit[1]["locked"]), "lock is stored per garment")
	ed._outfit_remove(1)
	_check(ed.outfit.size() == 1, "remove drops a garment from the outfit")
	ed._outfit_remove(0)
	ed.regen_and_rebuild()
	_check(ed.outfit.is_empty() and ed.sim == null, "an empty outfit leaves the mannequin empty without errors")
	ed._on_library_add("pants")
	_check(ed.outfit.size() == 1 and ed.sim != null, "a drafted garment can be added to an empty outfit")
	phase = 4
