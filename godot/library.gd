# Garment library: the garments the editor can put on the mannequin, and the user's saved garments in user://library/.
# Pure data (no editor state, no drawing). The editor resolves an item's id to a plan with its _plan_for_key().
#
# Item: {id, name, category, kind, fitted, user_file, redraft}
#   id        plan key: a built-in key ("pencil", "aline", "blank"), "fs-<garment>" (FreeSewing, nearest preset),
#             "pants" / "tank" / "shirt" / "crop" (drafted on the live body), or "user:<file>" (saved garment)
#   category  tops | bottoms | skirts | custom
#   kind      builtin | freesewing | drafted | user
#   fitted    body preset the outline is fitted for ("" = fixed outline)
#   redraft   true when the outline can be drafted again for another body (drafted, FreeSewing, or a saved
#             garment that remembers one of those)
const DIR := "user://library"
const FORMAT := "failoring-library/1"
const CATEGORIES := ["All", "Tops", "Bottoms", "Skirts", "Custom"]
const CATEGORY_KEYS := ["", "tops", "bottoms", "skirts", "custom"]
const DRAFTED := ["pants", "tank", "shirt", "crop"]


static func _item(id: String, name: String, category: String, kind: String, fitted: String, redraft: bool) -> Dictionary:
	return {"id": id, "name": name, "category": category, "kind": kind, "fitted": fitted, "user_file": "", "redraft": redraft}


# Built-in, FreeSewing and drafted garments. FreeSewing and drafted items are fitted for the nearest preset.
static func static_items(preset: String) -> Array:
	return [
		_item("pencil", "Pencil skirt (2 panels)", "skirts", "builtin", "", false),
		_item("aline", "A-line skirt (4 gores)", "skirts", "builtin", "", false),
		_item("tiered", "Tiered maxi skirt (3 tiers)", "skirts", "builtin", "", false),
		_item("blank", "Blank rectangle", "custom", "builtin", "", false),
		_item("poncho", "Poncho (square, neck hole)", "tops", "builtin", "", false),
		_item("fs-teagan", "Tee (FreeSewing)", "tops", "freesewing", preset, true),
		_item("fs-penelope", "Pencil skirt (FreeSewing)", "skirts", "freesewing", preset, true),
		_item("fs-charlie", "Chinos (FreeSewing)", "bottoms", "freesewing", preset, true),
		_item("shirt", "T-shirt (drafted)", "tops", "drafted", preset, true),
		_item("tank", "Tank (drafted)", "tops", "drafted", preset, true),
		_item("crop", "Crop top (drafted)", "tops", "drafted", preset, true),
		_item("pants", "Trousers (drafted)", "bottoms", "drafted", preset, true),
	]


# All items for the editor's library panel: static ones, then the saved garments sorted by name.
static func items(preset: String) -> Array:
	var out := static_items(preset)
	out.append_array(user_items())
	return out


static func user_items() -> Array:
	var out := []
	if not DirAccess.dir_exists_absolute(DIR):
		return out
	var d := DirAccess.open(DIR)
	if d == null:
		return out
	for f in d.get_files():
		if not f.ends_with(".json"):
			continue
		var j := _read(DIR + "/" + f)
		if String(j.get("format", "")) != FORMAT:
			continue
		var from := String(j.get("from", ""))
		var it := _item("user:" + f, String(j.get("name", f)), _category_key(String(j.get("category", "custom"))),
			"user", String(j.get("fitted", "")), from in DRAFTED or from.begins_with("fs-"))
		it["user_file"] = f
		out.append(it)
	out.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return String(a["name"]) < String(b["name"]))
	return out


static func is_user(id: String) -> bool:
	return id.begins_with("user:")


static func category_name(key: String) -> String:
	var i := CATEGORY_KEYS.find(key)
	return CATEGORIES[i] if i > 0 else "Custom"


static func _category_key(c: String) -> String:
	return c if c in ["tops", "bottoms", "skirts", "custom"] else "custom"


# Plan dictionary for a saved garment (un-normalized: the caller normalizes). {} if the file is missing.
static func plan_for_user(id: String) -> Dictionary:
	var j := _read(DIR + "/" + id.trim_prefix("user:"))
	if j.is_empty() or String(j.get("format", "")) != FORMAT:
		return {}
	return {"name": String(j.get("name", "Saved garment")), "notes": String(j.get("notes", "")),
		"fabric": String(j.get("fabric", "")), "from": String(j.get("from", "")),
		"pieces": j.get("pieces", []), "seams": j.get("seams", [])}


# Writes a saved garment. `from` is the plan key it was drafted from ("" if none): kept so the garment can be
# drafted again for another body. Returns the file name, or "" on failure.
static func save(name: String, category: String, fitted: String, from: String, notes: String, fabric: String,
		pieces: Array, seams: Array) -> String:
	DirAccess.make_dir_recursive_absolute(DIR)
	var file := "%s-%d.json" % [slug(name), Time.get_unix_time_from_system()]
	var f := FileAccess.open(DIR + "/" + file, FileAccess.WRITE)
	if f == null:
		return ""
	f.store_string(JSON.stringify({"format": FORMAT, "name": name, "category": _category_key(category), "fitted": fitted,
		"from": from, "notes": notes, "fabric": fabric, "pieces": pieces, "seams": seams}, " "))
	f.close()
	return file


static func rename(file: String, name: String) -> bool:
	var path := DIR + "/" + file
	var j := _read(path)
	if j.is_empty():
		return false
	j["name"] = name
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return false
	f.store_string(JSON.stringify(j, " "))
	f.close()
	return true


static func delete(file: String) -> bool:
	return DirAccess.remove_absolute(DIR + "/" + file) == OK


static func slug(name: String) -> String:
	var s := name.to_lower().replace(" ", "-")
	var re := RegEx.create_from_string("[^a-z0-9-]+")
	s = re.sub(s, "", true).strip_edges(true, true).trim_prefix("-").trim_suffix("-")
	return s if s != "" else "garment"


static func _read(path: String) -> Dictionary:
	var v = JSON.parse_string(FileAccess.get_file_as_string(path))
	return v if v is Dictionary else {}
