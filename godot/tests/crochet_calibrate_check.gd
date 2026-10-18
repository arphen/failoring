# Checks the calibration wizard (docs/CROCHET_PLAN.md section 10, ticket T9.1): the records file, the wizard's numbers, and that a saved record
# changes the stitch model and the banner. Run headless:
#   godot --headless --path godot res://tests/crochet_calibrate_check.tscn     (PASS/FAIL lines, then "RESULT: n failed")
extends Node
const Records = preload("res://crochet/crochet_records.gd")
const TEST_PATH := "user://crochet_test/swatches.json"
var fails := 0
func check(name: String, ok: bool) -> void:
	print(("PASS  " if ok else "FAIL  ") + name)
	if not ok: fails += 1

func _ready() -> void:
	OS.set_environment("FL_RECORDS", TEST_PATH)
	DirAccess.remove_absolute(ProjectSettings.globalize_path(TEST_PATH))

	# the records file
	var a := {"yarn": "acrylic_worsted", "hook_mm": 5.0, "stitch": "sc", "w": 0.007, "h": 0.006, "date": "2026-10-18", "hinge": {"blo": {"bendScale": 0.2, "restAngleDeg": 80.0}}}
	var b := {"yarn": "acrylic_worsted", "hook_mm": 4.5, "stitch": "sc", "w": 0.0065, "h": 0.0055}
	var recs := Records.upsert(Records.upsert([], a), b)
	check("records: two hooks of one yarn are two records", recs.size() == 2 and Records.find(recs, "acrylic_worsted", 5.0)["w"] == 0.007 and Records.find(recs, "acrylic_worsted", 4.5)["w"] == 0.0065)
	check("records: another yarn or hook finds nothing", Records.find(recs, "cotton_worsted", 5.0).is_empty() and Records.find(recs, "acrylic_worsted", 6.0).is_empty())
	var again := Records.upsert(recs, {"yarn": "acrylic_worsted", "hook_mm": 5.0, "stitch": "sc", "w": 0.0071, "h": 0.006})
	check("records: measuring again replaces the record but keeps its hinge fits", again.size() == 2 and Records.find(again, "acrylic_worsted", 5.0)["w"] == 0.0071 and Records.find(again, "acrylic_worsted", 5.0).has("hinge"))
	check("records: save and load give the same records back", Records.save_all(TEST_PATH, again) and Records.load_all(TEST_PATH) == again)
	check("records: remove drops one", Records.remove(again, "acrylic_worsted", 4.5).size() == 1)
	DirAccess.remove_absolute(ProjectSettings.globalize_path(TEST_PATH))
	check("records: a missing file is no records", Records.load_all(TEST_PATH).is_empty())

	# the scene: starter values first
	var c = (load("res://crochet/crochet.tscn") as PackedScene).instantiate()
	add_child(c)
	await get_tree().process_frame
	var s0: Dictionary = c.pattern.get_summary()
	check("before: not calibrated, the status line and the banner say so", not bool(s0["calibrated"]) and not c.summary_text().contains("calibrated") and c.calibration_banner("acrylic_worsted", 0.0).contains("UNVERIFIED"))
	var calb: Button
	for bt in c.find_children("*", "Button", true, false):
		if (bt as Button).text == "Calibrate":
			calb = bt
	check("the Calibrate button exists and opens the wizard for the panel's yarn and hook", calb != null and not c.calibrate_view.visible)
	calb.pressed.emit()
	var w = c.calibrate_view
	check("...yarn %s, hook %.2f mm (the yarn's own), nothing to save yet" % [w.yarn_id, w.hook_mm], w.visible and w.yarn_id == "acrylic_worsted" and w.hook_mm == 5.0 and w._save_btn.disabled and not w.save())

	# the example numbers of section 10
	for kv in {"stitches_per_10cm": 14, "rows_per_10cm": 16, "mass_g": 9.0, "width_cm": 12, "height_cm": 12, "overhang_wale_mm": 52, "overhang_course_mm": 38,
			"wale_w1_g": 250, "wale_l1_mm": 104, "wale_w2_g": 1000, "wale_l2_mm": 108, "course_w1_g": 250, "course_l1_mm": 110, "course_w2_g": 1000, "course_l2_mm": 150}.keys():
		w.set_value(kv, {"stitches_per_10cm": 14, "rows_per_10cm": 16, "mass_g": 9.0, "width_cm": 12, "height_cm": 12, "overhang_wale_mm": 52, "overhang_course_mm": 38,
			"wale_w1_g": 250, "wale_l1_mm": 104, "wale_w2_g": 1000, "wale_l2_mm": 108, "course_w1_g": 250, "course_l1_mm": 110, "course_w2_g": 1000, "course_l2_mm": 150}[kv])
	var fit: Dictionary = w.current_fit()
	var r: Dictionary = fit["record"]
	check("the example numbers give all four parts and no errors (%s)" % [r.keys()], fit["errors"].is_empty() and r.has("w") and r.has("density") and r.has("bendWale") and r.has("kWale") and not w._save_btn.disabled)
	check("gauge 14 x 16 in 10 cm: w = %.3f mm, h = %.3f mm; 9 g on 144 cm^2: %.3f kg/m^2" % [r["w"] * 1e3, r["h"] * 1e3, r["density"]], absf(r["w"] - 0.1 / 14) < 1e-12 and absf(r["h"] - 0.1 / 16) < 1e-12 and absf(r["density"] - 0.625) < 1e-9)
	check("the course strip reaches no lock: the notes say to use a heavier W2", not bool(fit["lock_reached_course"]) and bool(fit["lock_reached_wale"]) and str(fit["notes"]).contains("heavier W2"))
	check("the result panel shows the stitch before and after (%d chars)" % w.result_text().length(), w.result_text().contains("One single crochet stitch") and w.result_text().contains("starter") and w.result_text().contains("with this record"))

	# save: the model uses it
	var yarn0 := float(s0["yarn_m"])
	var saved := []
	w.records_changed.connect(func(): saved.append(1))
	check("save writes the file and tells the scene", w.save() and FileAccess.file_exists(TEST_PATH) and saved.size() == 1)
	c.compile_now()
	var s1: Dictionary = c.pattern.get_summary()
	check("after: the pattern is calibrated, yarn %.1f m -> %.1f m, mass %.1f g -> %.1f g" % [yarn0, s1["yarn_m"], s0["mass_g"], s1["mass_g"]], bool(s1["calibrated"]) and absf(float(s1["yarn_m"]) - yarn0) > 0.5 and absf(float(s1["mass_g"]) - float(s0["mass_g"])) > 0.5)
	check("...the status line says (calibrated) and the banner names the swatch and its parts", c.summary_text().contains("(calibrated)") and c.calibration_banner("acrylic_worsted", 0.0).contains("Calibrated") and c.calibration_banner("acrylic_worsted", 0.0).contains("gauge, weight, cantilever, stretch"))
	check("...and the pattern carries the record (%s)" % [c.pattern.get_calibration().keys()], c.pattern.get_calibration().has("kWale"))
	check("start_run builds the scene with it: the run's yarn is the calibrated one", c.start_run("hang_pin", "none") and absf(float(c.sim.get_metrics()["yarn_m"]) - float(s1["yarn_m"])) < 1e-6)
	c._stop_requested = true
	c.run_frames(1)

	# only for that yarn on that hook
	c.hook_spin.value = 6.0
	c.compile_now()
	check("another hook: the record does not apply", not bool(c.pattern.get_summary()["calibrated"]) and c.calibration_banner("acrylic_worsted", 6.0).contains("UNVERIFIED"))
	c.hook_spin.value = 5.0
	c.compile_now()
	check("the yarn's own size by number: it does again", bool(c.pattern.get_summary()["calibrated"]))
	c.hook_spin.value = 0.0
	for i in c.yarn_opt.item_count:
		if String(c.yarn_opt.get_item_metadata(i)) == "cotton_worsted":
			c.yarn_opt.select(i)
			c.yarn_opt.item_selected.emit(i)
	c.compile_now()
	check("another yarn: it does not", not bool(c.pattern.get_summary()["calibrated"]))
	c.yarn_opt.select(0)
	c.yarn_opt.item_selected.emit(0)
	c.compile_now()

	# the wizard shows the inputs of its record again
	c.open_calibration()
	check("reopened: the inputs of the record are back (%.0f stitches, %.1f g, W2 %.0f g)" % [w.value("stitches_per_10cm"), w.value("mass_g"), w.value("wale_w2_g")], w.value("stitches_per_10cm") == 14 and w.value("mass_g") == 9.0 and w.value("wale_w2_g") == 1000 and w.result_text().contains("A record of"))

	# the hinge fit (section 10.5): a known bendScale comes back from its simulated droop, and the model uses it
	c._load_fixture_named("facing")
	c.compile_now()
	var hs0: Dictionary = c.pattern.get_summary()
	var truth := 0.1
	var droop: float = w._calib.hinge_droop("blo", truth, 0.0, "acrylic_worsted", 5.0, w.current_fit()["record"])
	w.set_value("hinge_rest_deg", 0)
	w.set_value("hinge_droop_deg", droop)
	var hr: Dictionary = w.fit_hinge_now()
	check("hinge: bendScale %.2f droops %.2f deg in the simulator; the wizard fits %.4f from that (%d runs)" % [truth, droop, hr["bend_scale"], hr["runs"]], bool(hr["bracketed"]) and absf(float(hr["bend_scale"]) / truth - 1.0) < 0.05)
	check("hinge: the wizard holds the entry for blo and says so (%s)" % w.hinge_text().substr(0, 60), w.hinge_entries().has("blo") and absf(float(w.hinge_entries()["blo"]["bendScale"]) - truth) < 0.005 * truth * 10 and w.hinge_text().contains("bendScale"))
	w.save()
	var hrec: Dictionary = Records.find(Records.load_all(TEST_PATH), "acrylic_worsted", 5.0)
	check("hinge: the saved record has hinge.blo with the rest angle (%s)" % [hrec.get("hinge", {})], hrec.has("hinge") and hrec["hinge"].has("blo") and hrec["hinge"]["blo"]["restAngleDeg"] == 0.0)
	c.compile_now()
	var hs1: Dictionary = c.pattern.get_summary()
	check("hinge: the facing swatch's smallest hinge bendScale goes from %.2f to %.3f" % [hs0["hinge_scale_min"], hs1["hinge_scale_min"]], absf(float(hs0["hinge_scale_min"]) - 0.3) < 1e-9 and absf(float(hs1["hinge_scale_min"]) - float(hr["bend_scale"])) < 1e-9)
	# the threaded fit (the Fit button), for flo; a droop no hinge gives is not stored
	w._hinge_opt.select(1)
	w.set_value("hinge_droop_deg", w._calib.hinge_droop("flo", 0.25, 0.0, "acrylic_worsted", 5.0, w.current_fit()["record"]))
	w.start_hinge_fit()
	var waited := 0.0
	while waited < 120.0 and (w._hinge_btn.disabled or not w.hinge_entries().has("flo")):
		await get_tree().create_timer(0.2).timeout
		waited += 0.2
	check("hinge: the Fit button runs on a thread and stores flo (%.1f s): %s" % [waited, w.hinge_text().substr(0, 50)], w.hinge_entries().has("flo") and absf(float(w.hinge_entries()["flo"]["bendScale"]) / 0.25 - 1.0) < 0.05 and not w._hinge_btn.disabled)
	w.set_value("hinge_droop_deg", 4)
	w.fit_hinge_now()
	check("hinge: a droop that no bendScale gives is reported and the entry for flo stays (%s)" % w.hinge_text().substr(0, 40), w.hinge_text().begins_with("No fit") and absf(float(w.hinge_entries()["flo"]["bendScale"]) / 0.25 - 1.0) < 0.05)
	w.open("acrylic_worsted", 5.0)
	check("hinge: reopened, the wizard has the saved blo entry again and the inputs", w.hinge_entries().has("blo") and w.value("hinge_rest_deg") == 0 and w.value("hinge_droop_deg") > 0)
	c._load_fixture_named("pouch")
	c.compile_now()

	# the compare view takes it too
	var cv = c.compare_view
	cv.clear_slots()
	var good = cv.add_slot("calibrated", c.editor.text, "acrylic_worsted", 0.0, 1.0)
	var plain = cv.add_slot("starter", c.editor.text, "acrylic_worsted", 6.0, 1.0)
	check("compare: a slot gets the record of its yarn and hook, the other none", not good.record.is_empty() and plain.record.is_empty())
	cv.run_sync(3)
	var yarn_m: Array = cv.values_of("yarn_m")
	check("compare: the calibrated piece differs from the starter one (%.1f m, %.1f m) and the table says what is calibrated" % [yarn_m[0], yarn_m[1]], absf(yarn_m[0] - yarn_m[1]) > 0.5 and cv.table_bbcode().contains("except calibrated"))
	cv.clear_slots()

	# delete: back to the starter values
	check("delete removes the record and tells the scene", w.delete() and Records.load_all(TEST_PATH).is_empty())
	c.compile_now()
	var s2: Dictionary = c.pattern.get_summary()
	check("after delete: starter values again (%.1f m)" % float(s2["yarn_m"]), not bool(s2["calibrated"]) and absf(float(s2["yarn_m"]) - yarn0) < 1e-9)
	DirAccess.remove_absolute(ProjectSettings.globalize_path(TEST_PATH))
	print("RESULT: %d failed" % fails)
	get_tree().quit(1 if fails > 0 else 0)
