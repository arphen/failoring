# Built-in open plans: port of app/src/patterns.js (metres, counter-clockwise).
# Drafted for the default body; the wrap is re-evaluated on the live body at load.
class_name FailoringBuiltinPlans

const Shapes = preload("res://shapes.gd")
const PONCHO_Y := 0.80  # m: the height of the square's lowest edge. The neck hole (24 cm, at the square's centre) then settles at the neck base, near the shoulders (tested in godot/tests/poncho_check.gd); at 1.00 m it slides down the arms.


static func all() -> Dictionary:
	return {
		"poncho": _poncho(),
		"pencil": {
			"name": "Pencil skirt - 2 shaped panels", "fabric": "cotton_poplin",
			"notes": "Front and back panels, 104 cm round the hip but only 80 cm at the top edge, so the skirt hangs from the waist. A real pattern would use darts for this shaping.",
			"pieces": [
				{"id": "front", "name": "Front", "outline": [[0.0, 0.0], [0.52, 0.0], [0.52, 0.37], [0.46, 0.55], [0.06, 0.55], [0.0, 0.37]], "grain": 1.5708, "table": {"x": 0.0, "y": 0.0}, "wrap": {"theta": 0.0, "y": 0.50, "clear": 0.012, "ring": 0.0}},
				{"id": "back", "name": "Back", "outline": [[0.0, 0.0], [0.52, 0.0], [0.52, 0.37], [0.46, 0.55], [0.06, 0.55], [0.0, 0.37]], "grain": 1.5708, "table": {"x": 0.65, "y": 0.0}, "wrap": {"theta": 180.0, "y": 0.50, "clear": 0.012, "ring": 0.0}},
			],
			"seams": [{"a": ["front", 1], "b": ["back", 5], "reverse": true}, {"a": ["front", 2], "b": ["back", 4], "reverse": true},
				{"a": ["front", 5], "b": ["back", 1], "reverse": true}, {"a": ["front", 4], "b": ["back", 2], "reverse": true}],
		},
		"aline": {
			"name": "A-line skirt - 4 gores", "fabric": "wool_suiting",
			"notes": "Four trapezoid gores; 80 cm at the waist line growing to 200 cm at the hem. Try bias grain (45 deg) for a softer fall.",
			"pieces": _gores(),
			"seams": _gore_seams(),
		},
		"tiered": _tiered(),
		"blank": {
			"name": "Blank - one rectangle", "fabric": "cotton_poplin",
			"notes": "Start from one rectangle: drag corners, add pieces, then use Sew to join edges.",
			"pieces": [
				{"id": "p1", "name": "Piece 1", "outline": [[0.0, 0.0], [0.5, 0.0], [0.5, 0.5], [0.0, 0.5]], "grain": 1.5708, "table": {"x": 0.0, "y": 0.0}, "wrap": {"theta": 0.0, "y": 0.6, "clear": 0.012, "ring": 0.0}},
			],
			"seams": [],
		},
	}


# One 140 cm square with a 24 cm neck hole at its centre: the head goes through the hole and the square falls over the
# shoulders. No seams (a poncho is one piece). Light cotton poplin.
static func _poncho() -> Dictionary:
	var s: Dictionary = Shapes.poncho(1.4, 0.24)
	return {"name": "Poncho - 140 cm square, 24 cm neck hole", "fabric": "cotton_poplin",
		"notes": "One square with a neck hole. Pull it over the head: the square hangs over the shoulders. No seams.",
		"pieces": [{"id": "poncho", "name": "Poncho", "outline": s["outline"], "holes": s["holes"], "grain": 1.5708,
			"table": {"x": 0.0, "y": 0.0}, "wrap": {"theta": 0.0, "y": PONCHO_Y, "clear": 0.012, "ring": 0.0}}],
		"seams": []}


static func _gores() -> Array:
	var out := []
	for i in 4:
		out.append({"id": "gore%d" % (i + 1), "name": "Gore %d" % (i + 1),
			"outline": [[-0.25, 0.0], [0.25, 0.0], [0.10, 0.62], [-0.10, 0.62]], "grain": 1.5708,
			"table": {"x": 0.35 + 0.55 * float(i), "y": 0.0},
			"wrap": {"theta": 45.0 + 90.0 * float(i), "y": 0.42, "clear": 0.012, "ring": 4.0}})
	return out


static func _gore_seams() -> Array:
	var out := []
	for i in 4:
		out.append({"a": ["gore%d" % (i + 1), 1], "b": ["gore%d" % ((i + 1) % 4 + 1), 3], "reverse": true})
	return out


# Tiered maxi skirt: three tiers, each a front and a back panel with side seams. The top tier is a trapezoid, 82 cm
# round its top edge so it hangs from the waist; each lower tier is a rectangle about 1.4 times as wide as the tier
# above, gathered into it (the tier seam joins edges of unequal length and the drape eases the difference). Waist to hem
# 96 cm. Cotton poplin. wrap.ring 2 opens each tier out to its own width (two panels round the body).
const TIERS := [
	# [top width, bottom width, height, wrap y (lowest edge above the floor)] per panel, metres
	[0.41, 0.55, 0.30, 0.75],
	[0.72, 0.72, 0.34, 0.41],
	[0.94, 0.94, 0.38, 0.03],
]


static func _tiered() -> Dictionary:
	var pieces := []
	var seams := []
	var tx := 0.0  # cutting table: the panels side by side, 10 cm apart
	for t in TIERS.size():
		var tier: Array = TIERS[t]
		var top: float = tier[0]
		var bot: float = tier[1]
		var h: float = tier[2]
		var dx: float = (bot - top) * 0.5
		var outline := [[0.0, 0.0], [bot, 0.0], [bot - dx, h], [dx, h]]
		for side in [["f", "Front", 0.0], ["b", "Back", 180.0]]:
			pieces.append({"id": "t%d%s" % [t + 1, side[0]], "name": "Tier %d %s" % [t + 1, side[1]], "outline": outline.duplicate(true),
				"grain": 1.5708, "table": {"x": tx, "y": 0.0},
				"wrap": {"theta": side[2], "y": tier[3], "clear": 0.012, "ring": 2.0}})
			tx += bot + 0.1
		var f := "t%df" % (t + 1)
		var b := "t%db" % (t + 1)
		seams.append({"a": [f, 1], "b": [b, 3], "reverse": true})
		seams.append({"a": [f, 3], "b": [b, 1], "reverse": true})
		if t > 0:
			# the upper tier's hem edge (edge 0) takes the gathered top edge (edge 2) of this tier
			for s in ["f", "b"]:
				seams.append({"a": ["t%d%s" % [t, s], 0], "b": ["t%d%s" % [t + 1, s], 2], "reverse": true})
	return {"name": "Tiered maxi skirt - 3 gathered tiers", "fabric": "linen_medium",
		"notes": "Three tiers, front and back panels each. The top tier hangs from the waist (82 cm round); each lower tier is about 1.4 times as wide as the one above and gathered into it. Waist to hem 96 cm.",
		"pieces": pieces, "seams": seams}
