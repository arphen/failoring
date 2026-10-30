# Garment recipes in the editor (garment_recipe.gd, library.gd, editor.gd _poll_agent_garments): a recipe installed in
# the library loads as a garment, a wear request puts it on, and a changed recipe file is reloaded. Headless; uses a
# scratch library entry (agent-recipe-check.json) and removes it. Prints RECIPECHECK lines, exits 1 on a failure.
#   godot --headless --path godot res://tests/recipe_library_check.tscn
extends Node

const Library = preload("res://library.gd")

var fails := 0


func _check(ok: bool, what: String) -> void:
	print("RECIPECHECK %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


func _ready() -> void:
	var recipe := {"name": "Recipe check", "category": "skirts", "fabric": "cotton_poplin",
		"pieces": [{"id": "skirt", "shape": {"kind": "trapezoid", "top": "waist * 0.53", "bottom": "hip * 0.7", "h": 0.4},
			"around": 2, "wrap": {"theta": 0, "y": "y_waist - 0.4"}}], "seams": []}
	var file := Library.install_recipe(recipe)
	_check(file == "agent-recipe-check.json", "installed as %s" % file)
	var items := Library.user_items().filter(func(it): return it["id"] == "user:" + file)
	_check(items.size() == 1 and items[0]["category"] == "skirts", "listed in the library under skirts")
	var ed = load("res://editor.tscn").instantiate()
	add_child(ed)
	while ed.body_stand == null:
		await get_tree().process_frame
	var p: Dictionary = ed._plan_for_key("user:" + file)
	_check((p.get("pieces", []) as Array).size() == 2 and bool(p.get("recipe", false)), "expands on the body: %d pieces" % (p.get("pieces", []) as Array).size())
	var n0: int = ed.outfit.size()
	Library.request_wear(file)
	for i in 120:
		await get_tree().process_frame
		if ed.outfit.size() > n0:
			break
	await get_tree().create_timer(1.5).timeout
	_check(ed.outfit.size() == n0 + 1 and String(ed.outfit[ed.outfit.size() - 1]["key"]) == "user:" + file, "wear request put it on (outfit %d -> %d)" % [n0, ed.outfit.size()])
	# a changed recipe (longer skirt) is reloaded
	await get_tree().create_timer(1.2).timeout
	recipe["pieces"][0]["shape"]["h"] = 0.6
	recipe["pieces"][0]["wrap"]["y"] = "y_waist - 0.6"
	Library.install_recipe(recipe)
	var reloaded := false
	for i in 40:
		await get_tree().create_timer(0.1).timeout
		var e: Dictionary = ed.outfit[ed.outfit.size() - 1]
		var o: Array = e["pieces"][0]["outline"]
		var h := 0.0
		for q in o:
			h = maxf(h, float(q[1]))
		if absf(h - 0.6) < 0.01:
			reloaded = true
			break
	_check(reloaded, "a changed recipe file is reloaded")
	Library.delete(file)
	if Library.last_replaced != "":
		DirAccess.remove_absolute(Library.last_replaced)
	get_tree().quit(1 if fails > 0 else 0)
