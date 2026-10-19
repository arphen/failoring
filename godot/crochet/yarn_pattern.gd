extends RefCounted
# Pattern text <-> yarn mode actions (docs/YARN_MODE_PLAN.md: the action list is the pattern). One row per line; the items of a row are separated by
# commas or semicolons:
#   ch N, sc N, hdc N, dc N, sl st N (or slst, ss), sk N        N is how many (default 1)
#   sc flo N / sc blo N                                          into the front / the back loop only (also hdc, dc, sl st)
#   colour orange   colour #f2931f   colour orange stitch        the next stitch changes colour (at its last pull-through; "stitch": from its start)
# A row after the first that begins with "ch k" has k turning chains (the turn is made with them); otherwise the turn has none after the foundation
# chain and one after a row. The result is the list of actions the editor's history holds: [kind, argument] and ["colour", Color, when].

const COLOURS := {"red": Color(0.80, 0.30, 0.22), "orange": Color(0.95, 0.58, 0.18), "cream": Color(0.93, 0.88, 0.76), "blue": Color(0.25, 0.45, 0.78),
		"green": Color(0.30, 0.62, 0.38), "black": Color(0.14, 0.14, 0.16)}
const STITCHES := {"ch": "chain", "sc": "sc", "hdc": "hdc", "dc": "dc", "slst": "slst", "ss": "slst", "sk": "skip"}

# Returns {ok, actions, error, line}.
static func parse(text: String) -> Dictionary:
	var actions: Array = []
	var rows := text.split("\n", false)
	var row_no := 0
	for line_no in rows.size():
		var line := rows[line_no].strip_edges()
		if line == "" or line.begins_with("#"):
			continue
		var items: Array = []
		for part in line.replace(";", ",").split(",", false):
			var it := part.strip_edges().to_lower().replace("sl st", "slst")
			if it != "":
				items.append(it)
		if items.size() == 0:
			continue
		if row_no > 0:
			var turning := 0 if row_no == 1 else 1   # after the foundation chain no turning chain; after a row one
			var first := String(items[0])
			if first == "ch" or first.begins_with("ch "):
				turning = int(first.substr(2)) if first.length() > 2 else 1
				items.remove_at(0)
			actions.append(["turn", turning])
		for it in items:
			var r := _item(String(it), actions)
			if r != "":
				return {"ok": false, "actions": [], "error": r, "line": line_no + 1}
		row_no += 1
	return {"ok": true, "actions": actions, "error": "", "line": 0}

static func _item(it: String, actions: Array) -> String:
	var words := it.split(" ", false)
	if words.size() == 0:
		return ""
	var head := String(words[0])
	if head == "colour" or head == "color":
		if words.size() < 2:
			return "colour needs a name or #rrggbb"
		var c: Color
		var name := String(words[1])
		if COLOURS.has(name):
			c = COLOURS[name]
		elif name.begins_with("#") and Color.html_is_valid(name):
			c = Color.html(name)
		else:
			return "unknown colour '%s' (red, orange, cream, blue, green, black or #rrggbb)" % name
		actions.append(["colour", c, "stitch" if words.size() > 2 and String(words[2]) == "stitch" else "pull"])
		return ""
	if not STITCHES.has(head):
		return "unknown stitch '%s'" % head
	var kind := String(STITCHES[head])
	var count := 1
	var suffix := ""
	for i in range(1, words.size()):
		var w := String(words[i])
		if w == "flo":
			suffix = "_front"
		elif w == "blo":
			suffix = "_back"
		elif w.is_valid_int():
			count = int(w)
		else:
			return "cannot read '%s'" % w
	if suffix != "" and kind in ["chain", "skip"]:
		return "'%s' has no loops to choose between" % head
	for i in count:
		actions.append([kind + suffix if kind != "chain" and kind != "skip" else kind, -1])
	return ""

# The pattern as text, from the editor's rows ([[{kind, count, colour}]]): a "colour #rrggbb" item wherever the yarn changes.
static func text_from_rows(rows: Array) -> String:
	var out := ""
	var current := Color(0, 0, 0, 0)
	var first := true
	for row in rows:
		var items: PackedStringArray = []
		for e in row:
			var col: Color = e["colour"]
			if String(e["kind"]) != "sk" and not first_colour_matches(current, col, first):
				items.append("colour #%s" % col.to_html(false))
			if String(e["kind"]) != "sk":
				current = col
				first = false
			items.append("%s %d" % [String(e["kind"]), int(e["count"])])
		if items.size() > 0:
			out += ", ".join(items) + "\n"
	return out

static func first_colour_matches(current: Color, col: Color, first: bool) -> bool:
	return first or current.is_equal_approx(col)
