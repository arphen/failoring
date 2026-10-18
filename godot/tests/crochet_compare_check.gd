# Checks the compare view (docs/CROCHET_PLAN.md 9.3, ticket T8.4): A and B side by side, the five hinge-round variants, the table.
# Run headless:  godot --headless --path godot res://tests/crochet_compare_check.tscn     (PASS/FAIL lines, then "RESULT: n failed")
# The pieces are stepped only a few frames (a bag takes minutes to relax), so the numbers checked are that they exist, not what they mean.
extends Node
var fails := 0
func check(name: String, ok: bool) -> void:
	print(("PASS  " if ok else "FAIL  ") + name)
	if not ok: fails += 1

func _ready() -> void:
	var c = (load("res://crochet/crochet.tscn") as PackedScene).instantiate()
	add_child(c)
	await get_tree().process_frame
	var cv = c.compare_view
	check("the crochet scene has the compare view, hidden at first", cv != null and not cv.visible)
	var compare_btn: Button
	for b in c.find_children("*", "Button", true, false):
		if (b as Button).text == "Compare":
			compare_btn = b
	check("the Compare button is enabled and opens the view", compare_btn != null and not compare_btn.disabled)
	compare_btn.pressed.emit()
	check("...the view is visible", cv.visible)

	# A and B: the pouch with acrylic, against the pouch with cotton on a bigger hook
	var cotton := -1
	for i in cv._yarn_ids.size():
		if cv._yarn_ids[i] == "cotton_worsted":
			cotton = i + 1
	cv.b_yarn.select(cotton)
	cv.b_hook.value = 6.0
	cv.setup_ab()
	check("setup_ab makes two slots, A and B, with their own yarn and hook", cv.slots.size() == 2 and cv.slots[0].label == "A" and cv.slots[1].label == "B" and cv.slots[0].yarn != cv.slots[1].yarn and cv.slots[1].hook == 6.0)
	check("the two pieces stand 0.5 m apart, side by side", absf(cv.slot_offset(1).x - cv.slot_offset(0).x - 0.5) < 1e-6 and cv.slot_offset(0).y == 0.0)
	cv.run_sync(30)
	check("both ran: state done, 1250 vertices each", cv.slots[0].state == "done" and cv.slots[1].state == "done" and cv.slots[0].vertices.size() == 1250 and cv.slots[1].vertices.size() == 1250)
	var mass: Array = cv.values_of("mass_g")
	check("the table has the mass of each (%s g and %s g), different" % [mass[0], mass[1]], mass[0] is float and mass[1] is float and absf(mass[0] - mass[1]) > 0.5)
	var bold := {}
	for r in cv.table_rows():
		bold[r["key"]] = r["bold"]
	check("mass is in bold (the pieces differ), the frame count is not", bold.get("mass_g", false) and not bold.get("frames", true))
	var code: String = cv.table_bbcode()
	check("two pieces: the table has A, B and the difference column", code.contains("[b]A[/b]") and code.contains("[b]B[/b]") and code.contains("B - A") and code.contains("[b]mass_g[/b]") and not code.contains("[b]frames[/b]"))
	check("the positions of A and B differ (different hook, different size)", cv.slots[0].vertices != cv.slots[1].vertices)
	cv.clear_slots()
	check("clear_slots empties the view and the table", cv.slots.is_empty() and cv.table_bbcode().contains("Nothing to compare"))

	# a piece that does not compile must not stop the other
	var good: String = c.editor.text
	cv.add_slot("good", good, "acrylic_worsted", 0.0, 1.0)
	cv.add_slot("bad", "ch,sc,xyz@[3,4]\n", "acrylic_worsted", 0.0, 1.0)
	cv.run_sync(5)
	check("a bad pattern fails on its own with its message, the good one finishes (%s)" % cv.slots[1].error, cv.slots[0].state == "done" and cv.slots[1].state == "failed" and cv.slots[1].error.length() > 0)
	cv.clear_slots()

	# the five hinge-round variants of the laptop bag
	cv.setup_variants()
	var labels := []
	for s in cv.slots:
		labels.append(s.label)
	check("five variants: %s" % [labels], labels == ["plain", "BLO round", "FLO round", "sl st round", "surface sl st"])
	check("they stand in a row 0.5 m apart", absf(cv.slot_offset(4).x - cv.slot_offset(0).x - 2.0) < 1e-6)
	check("their texts are the bundled fixtures (the BLO one has the hinge round written BLO)", cv.slots[1].text.contains("114scbl") and cv.slots[2].text.contains("114scfl") and cv.slots[3].text.contains("114ssbl") and not cv.slots[0].text.contains("114scbl"))
	cv.run_sync(4)
	var states := []
	for s in cv.slots:
		states.append(s.state)
	check("all five ran (%s)" % [states], states == ["done", "done", "done", "done", "done"])
	var corner: Array = cv.values_of("corner_angle_deg")
	var ok := corner.size() == 5
	for v in corner:
		ok = ok and v is float and is_finite(v)
	check("the table has five corner angles: %s" % [corner], ok)
	var code5: String = cv.table_bbcode()
	check("five pieces: one column each, no difference column", code5.contains("[b]sl st round[/b]") and not code5.contains("B - A"))
	print("RESULT: %d failed" % fails)
	get_tree().quit(1 if fails > 0 else 0)
