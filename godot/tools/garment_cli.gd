# The garment command line (./garment at the repo root runs this headless). For agents: every command prints plain
# text, problems as ERROR lines with the fix in the text, and exits 1 when there is an ERROR. docs/GARMENT_RECIPES.md.
#
#   ./garment body [--body B]                 body variables for expressions (girths, heights in metres)
#   ./garment list                            garments in the library (ids for export)
#   ./garment export ID [--body B]            a library garment as a plan (JSON on stdout), to start from
#   ./garment check FILE [--body B]           expand the recipe: pieces, edges (name, length), seams (gather ratio)
#   ./garment drape FILE [--body B] [--walk]  simulate it on the body and say whether it hangs right
#   ./garment install FILE                    put it in the editor's library (a running editor reloads it)
#   ./garment wear FILE                       install and put it on the mannequin in the running editor
# B: mine (the measurements CSV, the default when there is one) or a preset (average, petite, tall, curvy, plus, athletic).
extends Node

const BodyGen = preload("res://body_gen.gd")
const PlanLoader = preload("res://plan_loader.gd")
const Builtin = preload("res://builtin_plans.gd")
const Table = preload("res://table.gd")
const Outfit = preload("res://outfit.gd")
const Library = preload("res://library.gd")
const Recipe = preload("res://garment_recipe.gd")
const Health = preload("res://garment_health.gd")
const MeasurementsForm = preload("res://measurements_form.gd")
const Wrap = preload("res://wrap.gd")
const Seams = preload("res://seams.gd")

const QUALITY := {"edge": 0.040, "sub": 2, "newton": 8, "frames": 48}   # the editor's Standard quality
const SEAM_STIFFNESS := 20000.0
const RAMP := 20
const DRAPE_FRAMES := 120
const WALK_FRAMES := 96

var args := PackedStringArray()
var errors := 0


func _ready() -> void:
	# a script error stops _ready before quit(): the tree keeps running, so this ends the run instead of hanging
	get_tree().create_timer(600.0).timeout.connect(func() -> void:
		print("ERROR the garment tool stopped (a script error above, or a run over 10 minutes)")
		get_tree().quit(2))
	args = OS.get_cmdline_user_args()
	var cmd := args[0] if args.size() > 0 else "help"
	match cmd:
		"body":
			_cmd_body()
		"list":
			_cmd_list()
		"export":
			_cmd_export()
		"check":
			_cmd_check()
		"drape":
			_cmd_drape()
		"install", "wear":
			_cmd_install(cmd == "wear")
		_:
			print(FileAccess.get_file_as_string("res://tools/garment_help.txt"))
	get_tree().quit(1 if errors > 0 else 0)


func _err(t: String) -> void:
	print("ERROR " + t)
	errors += 1


func _opt(name: String, def: String) -> String:
	var i := args.find("--" + name)
	return args[i + 1] if i >= 0 and i + 1 < args.size() else def


func _pos(k: int) -> String:
	var out := []
	var skip := false
	for i in args.size():
		if skip:
			skip = false
			continue
		if args[i].begins_with("--"):
			skip = args[i] in ["--body"]
			continue
		out.append(args[i])
	return out[k] if k < out.size() else ""


# {M, rest, stand, helper, name}
func _body() -> Dictionary:
	var want := _opt("body", "mine")
	var meas: Dictionary = (BodyGen.PRESETS["average"] as Dictionary).duplicate()
	var name := "average"
	if want == "mine" and MeasurementsForm.has_file():
		meas.merge(MeasurementsForm.load_csv(), true)
		name = "mine (%s)" % MeasurementsForm.csv_path()
	elif BodyGen.PRESETS.has(want):
		meas = (BodyGen.PRESETS[want] as Dictionary).duplicate()
		name = want
	elif want != "mine":
		_err("body '%s' unknown: mine or one of %s" % [want, ", ".join(PackedStringArray(BodyGen.PRESETS.keys()))])
	var M: Dictionary = BodyGen.load_data(PlanLoader.load_json("res://assets/body_data.json"))
	var rest = BodyGen.generate(M, meas, null)
	var stand: Dictionary = BodyGen.stand(M, rest)
	var helper := Wrap.new({"id": "h", "outline": [[0.0, 0.0], [1.0, 0.0], [1.0, 1.0]],
		"wrap": {"mode": "arc", "theta": 0.0, "y": 0.0, "clear": 0.012}}, stand["sections"], stand["arms"])
	return {"M": M, "rest": rest, "stand": stand, "helper": helper, "name": name}


func _cmd_body() -> void:
	var b := _body()
	print("BODY " + b["name"])
	var v := Recipe.body_vars(b["stand"], b["helper"])
	for k in v:
		print("  %-10s = %.3f m" % [k, v[k]])


func _cmd_list() -> void:
	for it in Library.items("average"):
		print("%-28s %-8s %s" % [it["id"], it["category"], it["name"]])


func _cmd_export() -> void:
	var id := _pos(1)
	var b := _body()
	var p := {}
	if Builtin.all().has(id):
		p = Builtin.all()[id]
	elif id in Library.DRAFTED:
		p = Recipe._draft(id, b["stand"], b["helper"], {})
	elif id.begins_with("fs-"):
		p = PlanLoader.load_json("res://assets/average/plan_%s.json" % id.trim_prefix("fs-"))
	elif Library.is_user(id):
		p = PlanLoader.load_json(Library.DIR + "/" + id.trim_prefix("user:"))
	if p.is_empty():
		_err("no garment '%s' (./garment list)" % id)
		return
	print(JSON.stringify(p, " "))


# The recipe file -> {res, recipe} (res from Recipe.expand), or {} after printing the error.
func _load(path: String, b: Dictionary) -> Dictionary:
	if path == "":
		_err("give the recipe file: ./garment %s FILE" % args[0])
		return {}
	var abs := path if path.is_absolute_path() else OS.get_environment("FL_CWD").path_join(path)
	var text := FileAccess.get_file_as_string(abs)
	if text == "":
		_err("cannot read %s" % abs)
		return {}
	var j := JSON.new()
	if j.parse(text) != OK:
		_err("%s line %d: %s" % [path, j.get_error_line(), j.get_error_message()])
		return {}
	if not (j.data is Dictionary):
		_err("%s: the top level must be an object" % path)
		return {}
	var recipe: Dictionary = j.data
	if String(recipe.get("format", "")) != Recipe.FORMAT:
		print("WARNING %s: \"format\": \"%s\" missing, read as a recipe" % [path, Recipe.FORMAT])
	return {"recipe": recipe, "res": Recipe.expand(recipe, b["stand"], b["helper"]), "path": abs}


func _cmd_check() -> void:
	var b := _body()
	var l := _load(_pos(1), b)
	if l.is_empty():
		return
	print("BODY " + b["name"])
	print(Recipe.describe(l["res"]))
	errors += (l["res"]["errors"] as Array).size()
	if errors == 0:
		print("OK expands cleanly; next: ./garment drape %s" % _pos(1))


static func _v3(p: PackedFloat32Array) -> PackedVector3Array:
	var out := PackedVector3Array()
	out.resize(p.size() / 3)
	for i in p.size() / 3:
		out[i] = Vector3(p[3 * i], p[3 * i + 1], p[3 * i + 2])
	return out


func _cmd_drape() -> void:
	var b := _body()
	var l := _load(_pos(1), b)
	if l.is_empty():
		return
	var res: Dictionary = l["res"]
	if not (res["errors"] as Array).is_empty():
		print(Recipe.describe(res))
		errors += (res["errors"] as Array).size()
		return
	var plan := PlanLoader.normalize_plan(res["plan"])
	var valid := Table.valid_pieces(plan["pieces"])
	for pid in valid["bad"]:
		_err("piece '%s' cannot be simulated: %s" % [pid, str(valid["bad"][pid])])
	var merged := Outfit.merge([{"uid": 1, "fabric": String(plan["fabric"]), "pieces": valid["pieces"], "seams": plan["seams"]}])
	merged.merge(Seams.expand(merged["pieces"], merged["seams"]), true)   # part-edge seams: split the outlines (editor rebuild)
	var M: Dictionary = b["M"]
	var stand: Dictionary = b["stand"]
	var t0 := Time.get_ticks_msec()
	var built: Dictionary = PlanLoader.build({"pieces": merged["pieces"], "seams": merged["seams"]}, stand["sections"], stand["arms"],
		_v3(stand["pos"]), M["tris"], String(plan["fabric"]), QUALITY, SEAM_STIFFNESS, RAMP, false)
	if built.is_empty():
		_err("the solver could not build the garment (see the lines above)")
		return
	var sim: FailoringSim = built["sim"]
	for w in built.get("warn", []):
		print("WARNING solver: " + str(w))
	var health := Health.new(built, merged["pieces"], merged["seams"], {1: plan["name"]})
	var start := sim.get_positions()
	var hr := {}
	var prev := start
	var moving := 0.0
	for f in range(1, DRAPE_FRAMES + 1):
		if f <= RAMP + 1:
			sim.set_param("gravity_y", -9.81 * minf(1.0, pow(float(f) / float(RAMP), 2.0)))
		sim.step(1)
		var cur := sim.get_positions()
		if f == DRAPE_FRAMES:
			moving = 0.0
			for i in cur.size():
				moving = maxf(moving, (cur[i] - prev[i]).length())
		prev = cur
		hr = health.check(sim, cur, 0.0, f)
	var fin := sim.get_positions()
	print("DRAPE %s on body %s: %d frames in %.1f s" % [plan["name"], b["name"], DRAPE_FRAMES, (Time.get_ticks_msec() - t0) / 1000.0])
	var vars := Recipe.body_vars(stand, b["helper"])
	_report_pieces(built, merged, start, fin, vars)
	for p in res["plan"]["pieces"]:
		if (p["wrap"] as Dictionary).has("gather"):
			print("NOTE gathered pieces ride up and crumple on this coarse mesh (4 cm); ./garment shots drapes on 1.5 cm and prints where they really hang")
			break
	var gap := sim.get_max_seam_gap() * 1000.0
	print("SEAMS widest gap %.1f mm%s" % [gap, "  (closed)" if gap < 15.0 else "  (open: a seam joins edges that are far apart on the body, or pulls the wrong way: check reverse and which edges)"])
	print("SETTLE fastest point moves %.1f mm/frame at the end%s" % [moving * 1000.0, "" if moving < 0.004 else "  (still moving: sliding off, or too heavy for its seams)"])
	if String(hr.get("text", "")) != "":
		for line in String(hr["text"]).split("\n"):
			if line.strip_edges() != "":
				if bool(hr.get("error", false)):
					_err("health: " + line)
				else:
					print("WARNING health: " + line)
	if gap >= 40.0:
		_err("seam gap %.0f mm after the drape: the garment is not sewn together on the body" % gap)
	if args.has("--walk"):
		var worst := 0.0
		for k in range(1, WALK_FRAMES + 1):
			var stride := clampf(float(k) / 24.0, 0.0, 1.0)
			var pose: Dictionary = BodyGen.pose(M, b["rest"], fposmod(float(k) / 64.0, 1.0), {"stride": stride, "arms": stride})
			sim.set_body_positions(_v3(pose["pos"]))
			sim.step(1)
			worst = maxf(worst, sim.get_max_seam_gap())
		print("WALK %d frames: widest seam gap %.1f mm" % [WALK_FRAMES, worst * 1000.0])
		_report_pieces(built, merged, fin, sim.get_positions(), vars)
	if errors == 0:
		print("OK drapes; next: ./garment wear %s  (puts it on the mannequin in the editor)" % _pos(1))


# Per piece: where it hangs (top and bottom height, named by the nearest body landmark), how far it dropped from where
# it started, and how far it sits from the body. Drops and floor contact are the usual signs of a wrong wrap.y or seam.
func _report_pieces(built: Dictionary, merged: Dictionary, start: PackedVector3Array, fin: PackedVector3Array, vars: Dictionary) -> void:
	for p in merged["pieces"]:
		var pid := String(p["id"])
		var first := int(built["first"][pid])
		var n: int = (built["uv"][pid] as PackedVector2Array).size()
		var lo := 1e9
		var hi := -1e9
		var lo0 := 1e9
		var hi0 := -1e9
		for i in range(first, first + n):
			lo = minf(lo, fin[i].y)
			hi = maxf(hi, fin[i].y)
			lo0 = minf(lo0, start[i].y)
			hi0 = maxf(hi0, start[i].y)
		var line := "PIECE %-12s hangs %.2f..%.2f m (%s .. %s), top moved %+.1f cm" % [pid.trim_prefix(Outfit.prefix(1)),
			lo, hi, _near(lo, vars), _near(hi, vars), (hi - hi0) * 100.0]
		if lo < 0.03:
			line += "  ON THE FLOOR"
		if hi - hi0 < -0.06:
			line += "  SLID DOWN"
		print(line)


static func _near(y: float, v: Dictionary) -> String:
	var best := ""
	var bd := 1e9
	for k in ["y_neck", "y_shoulder", "y_bust", "y_waist", "y_hip", "y_crotch", "y_knee", "y_ankle"]:
		var d := absf(y - float(v[k]))
		if d < bd:
			bd = d
			best = k.trim_prefix("y_")
	if y < 0.03:
		return "floor"
	return "%s%+.0fcm" % [best, (y - float(v["y_" + best])) * 100.0]


func _cmd_install(wear: bool) -> void:
	var b := _body()
	var l := _load(_pos(1), b)
	if l.is_empty():
		return
	var res: Dictionary = l["res"]
	if not (res["errors"] as Array).is_empty():
		print(Recipe.describe(res))
		errors += (res["errors"] as Array).size()
		return
	var file := Library.install_recipe(l["recipe"])
	if file == "":
		_err("cannot write the library (%s)" % ProjectSettings.globalize_path(Library.DIR))
		return
	print("INSTALLED user:%s -> %s" % [file, ProjectSettings.globalize_path(Library.DIR + "/" + file)])
	print("  the editor's library shows it as \"%s\"; a running editor reloads it if it is being worn" % l["recipe"].get("name", file))
	if Library.last_replaced != "":
		print("  the version it replaced: %s (copy it back to the recipe to undo)" % ProjectSettings.globalize_path(Library.last_replaced))
	if wear:
		Library.request_wear(file)
		print("WEAR requested: a running editor puts it on the mannequin within a second (start one: ./garment editor)")
