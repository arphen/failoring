# Verifies godot/draft.gd against JS reference outlines (tools/export_godot_plans.js
# writes them with the real FL.draft). Run headless:
#   godot --headless --path godot res://tests/draft_check.tscn
#   FL_PRESET=tall godot --headless --path godot res://tests/draft_check.tscn
# Prints DRAFTCHECK max_err_mm and quits 0 below 0.01 mm, 1 above.
extends Node

const BodyGen = preload("res://body_gen.gd")
const Draft = preload("res://draft.gd")
const Wrap = preload("res://wrap.gd")
const TOL_MM := 0.01


func _ready() -> void:
	var preset := OS.get_environment("FL_PRESET")
	if preset == "":
		preset = "average"
	var data = JSON.parse_string(FileAccess.get_file_as_string("res://assets/body_data.json"))
	var m = BodyGen.load_data(data)
	var rest = BodyGen.generate(m, BodyGen.PRESETS[preset], null)
	var stand = BodyGen.stand(m, rest)
	var helper = Wrap.new({"id": "h", "outline": [[0.0, 0.0], [1.0, 0.0], [1.0, 1.0]],
		"wrap": {"mode": "arc", "theta": 0.0, "y": 0.0, "clear": 0.012}}, stand["sections"], stand["arms"])
	var refs = JSON.parse_string(FileAccess.get_file_as_string("res://assets/%s/draft_ref.json" % preset))
	var plans := {
		"pants": Draft.pants(stand, helper, {}),
		"tank": Draft.tank(stand, helper, {}),
		"shirt": Draft.shirt(stand, helper, {}),
	}
	var worst := 0.0
	var n := 0
	for dn in (refs["drafts"] as Dictionary).keys():
		var want: Dictionary = refs["drafts"][dn]
		var got: Dictionary = plans[dn]
		var want_pieces: Array = want["pieces"]
		var got_pieces: Array = got["pieces"]
		if want_pieces.size() != got_pieces.size():
			push_error("draft_check %s: piece count %d != %d" % [dn, got_pieces.size(), want_pieces.size()])
			get_tree().quit(1)
			return
		for i in want_pieces.size():
			var wo: Array = want_pieces[i]["outline"]
			var go: Array = got_pieces[i]["outline"]
			if wo.size() != go.size():
				push_error("draft_check %s/%s: outline len %d != %d" % [dn, String(want_pieces[i]["id"]), go.size(), wo.size()])
				get_tree().quit(1)
				return
			for k in wo.size():
				var e: float = Vector2(wo[k][0], wo[k][1]).distance_to(Vector2(go[k][0], go[k][1]))
				worst = maxf(worst, e)
				n += 1
		if int((got["seams"] as Array).size()) != int(want["seams"]):
			push_error("draft_check %s: seam count differs" % dn)
			get_tree().quit(1)
			return
	print("DRAFTCHECK %s points=%d max_err_mm=%.6f" % [preset, n, worst * 1000.0])
	get_tree().quit(0 if worst * 1000.0 < TOL_MM else 1)
