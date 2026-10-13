# Basic shapes for the cutting table (Add shape menu) and the Poncho. Each shape is built from its sizes in cm and
# returns {"outline": [[x, y] ...], "holes": [[[x, y] ...] ...]} in metres: counter-clockwise outlines starting at the
# bottom-left (or the first corner), with the bounding box at the origin. The table places the shape where it is asked.
extends RefCounted

const ELLIPSE_POINTS := 24   # the outline of a circle or an ellipse: 24 points (the browser's polygon size for them too)
const PONCHO_HOLE_POINTS := 24


# The shapes the menu offers: id, label, and the size fields as [key, label, default cm, min cm].
static func kinds() -> Array:
	return [
		{"id": "rect", "label": "Rectangle", "params": [["w", "Width", 50.0, 1.0], ["h", "Height", 40.0, 1.0]]},
		{"id": "square", "label": "Square", "params": [["s", "Side", 50.0, 1.0]]},
		{"id": "ellipse", "label": "Circle or ellipse", "params": [["w", "Width", 40.0, 1.0], ["h", "Height", 40.0, 1.0]]},
		{"id": "triangle", "label": "Triangle", "params": [["b", "Base", 50.0, 1.0], ["h", "Height", 50.0, 1.0]]},
		{"id": "trapezoid", "label": "Trapezoid", "params": [["top", "Top width", 30.0, 1.0], ["bottom", "Bottom width", 50.0, 1.0], ["h", "Height", 50.0, 1.0]]},
		{"id": "poncho", "label": "Poncho (square with neck hole)", "params": [["s", "Side", 140.0, 20.0], ["neck", "Neck hole diameter", 24.0, 5.0]]},
	]


static func label_of(id: String) -> String:
	for k in kinds():
		if String(k["id"]) == id:
			return String(k["label"])
	return id


static func params_of(id: String) -> Array:
	for k in kinds():
		if String(k["id"]) == id:
			return k["params"]
	return []


static func default_params(id: String) -> Dictionary:
	var out := {}
	for k in kinds():
		if String(k["id"]) == id:
			for p in k["params"]:
				out[String(p[0])] = float(p[2])
	return out


# Sizes in cm (a missing key takes its default) -> the shape in metres. Unknown ids give a 50 cm square.
static func make(id: String, cm: Dictionary) -> Dictionary:
	var d := default_params(id)
	for k in cm:
		d[k] = float(cm[k])
	match id:
		"rect":
			return {"outline": rect(d["w"] / 100.0, d["h"] / 100.0), "holes": []}
		"square":
			return {"outline": rect(d["s"] / 100.0, d["s"] / 100.0), "holes": []}
		"ellipse":
			return {"outline": ellipse(d["w"] / 100.0, d["h"] / 100.0, ELLIPSE_POINTS), "holes": []}
		"triangle":
			return {"outline": triangle(d["b"] / 100.0, d["h"] / 100.0), "holes": []}
		"trapezoid":
			return {"outline": trapezoid(d["top"] / 100.0, d["bottom"] / 100.0, d["h"] / 100.0), "holes": []}
		"poncho":
			return poncho(d["s"] / 100.0, d["neck"] / 100.0)
	return {"outline": rect(0.5, 0.5), "holes": []}


static func rect(w: float, h: float) -> Array:
	return [[0.0, 0.0], [w, 0.0], [w, h], [0.0, h]]


# An ellipse with semi-axes w/2 and h/2, as n points counter-clockwise from the rightmost point.
static func ellipse(w: float, h: float, n: int) -> Array:
	var out := []
	for i in n:
		var t := TAU * float(i) / float(n)
		out.append([w * 0.5 + w * 0.5 * cos(t), h * 0.5 + h * 0.5 * sin(t)])
	return out


static func triangle(b: float, h: float) -> Array:
	return [[0.0, 0.0], [b, 0.0], [b * 0.5, h]]


# The wide edge at the bottom, as the table draws it (Trapezoid: the wide edge is at the bottom of the table).
static func trapezoid(top: float, bottom: float, h: float) -> Array:
	var x0 := (bottom - top) * 0.5
	return [[0.0, 0.0], [bottom, 0.0], [bottom - x0, h], [x0, h]]


# Square side s with a circular neck hole of diameter neck at its centre (the Poncho: head through the hole).
static func poncho(s: float, neck: float) -> Dictionary:
	var hole := ellipse(neck, neck, PONCHO_HOLE_POINTS)
	for q in hole:
		q[0] = float(q[0]) + s * 0.5 - neck * 0.5
		q[1] = float(q[1]) + s * 0.5 - neck * 0.5
	return {"outline": rect(s, s), "holes": [hole]}
