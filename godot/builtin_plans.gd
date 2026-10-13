# Built-in open plans: port of app/src/patterns.js (metres, counter-clockwise).
# Drafted for the default body; the wrap is re-evaluated on the live body at load.
class_name FailoringBuiltinPlans


static func all() -> Dictionary:
	return {
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
		"blank": {
			"name": "Blank - one rectangle", "fabric": "cotton_poplin",
			"notes": "Start from one rectangle: drag corners, add pieces, then use Sew to join edges.",
			"pieces": [
				{"id": "p1", "name": "Piece 1", "outline": [[0.0, 0.0], [0.5, 0.0], [0.5, 0.5], [0.0, 0.5]], "grain": 1.5708, "table": {"x": 0.0, "y": 0.0}, "wrap": {"theta": 0.0, "y": 0.6, "clear": 0.012, "ring": 0.0}},
			],
			"seams": [],
		},
	}


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
