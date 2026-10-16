# Camera views for the 3D pane (editor.gd): what each preset looks at and from where, and how far back the camera must
# stand to fit a thing of a given size in the pane (the pane's shape matters: a tall narrow pane is limited by width).
# yaw: 0 looks at the mannequin's front (the camera on +z), +90 degrees puts the camera on the mannequin's left (+x), 180 behind.
# pitch: negative looks down from above.
extends RefCounted

const Studio = preload("res://studio.gd")

const VIEWS := [
	{"id": "fit", "label": "Fit", "focus": "auto", "yaw": 0.3, "pitch": -0.12,
		"tip": "The whole mannequin, from the default angle (F, Home, double-click)"},
	{"id": "front", "label": "Front", "focus": "body", "yaw": 0.0, "pitch": -0.05, "tip": "Front, level (arrow keys orbit, wheel zooms)"},
	{"id": "q_l", "label": "¾ left", "focus": "body", "yaw": 0.75, "pitch": -0.1, "tip": "Three-quarter view from the mannequin's left"},
	{"id": "q_r", "label": "¾ right", "focus": "body", "yaw": -0.75, "pitch": -0.1, "tip": "Three-quarter view from the mannequin's right"},
	{"id": "left", "label": "Left", "focus": "body", "yaw": PI * 0.5, "pitch": -0.05, "tip": "Side view, the mannequin's left"},
	{"id": "right", "label": "Right", "focus": "body", "yaw": -PI * 0.5, "pitch": -0.05, "tip": "Side view, the mannequin's right"},
	{"id": "back", "label": "Back", "focus": "body", "yaw": PI, "pitch": -0.05, "tip": "From behind"},
	{"id": "face", "label": "Face", "focus": "head", "yaw": 0.25, "pitch": -0.02, "tip": "Head and shoulders"},
	{"id": "torso", "label": "Torso", "focus": "torso", "yaw": 0.35, "pitch": -0.05, "tip": "Chest and waist: bodices, collars, darts"},
	{"id": "legs", "label": "Legs", "focus": "legs", "yaw": 0.35, "pitch": -0.04, "tip": "Hips to feet: trousers, skirts, hems"},
	{"id": "atelier", "label": "Atelier", "focus": "room", "yaw": -0.3, "pitch": -0.25, "fov": 50.0, "tip": "Step back: the whole room"},
	{"id": "mirror", "label": "Mirror", "focus": "mirror", "yaw": 0.64, "pitch": -0.08, "fov": 46.0,
		"tip": "Look into the fitting mirror from in front of it: the back of the mannequin and of the garment"},
	{"id": "table", "label": "Table", "focus": "table", "yaw": PI * 0.5, "pitch": -0.3, "fov": 40.0, "tip": "The cutting table"},
]


static func fov_of(v: Dictionary, default_fov: float) -> float:
	return float(v.get("fov", default_fov))


static func by_id(id: String) -> Dictionary:
	for v in VIEWS:
		if String(v["id"]) == id:
			return v
	return VIEWS[0]


# Distance at which a thing `height` tall and `width` wide fills the pane (with a margin), for a vertical field of view
# `fov_deg` and a pane of this aspect (width over height).
static func fit(height: float, width: float, aspect: float, fov_deg: float) -> float:
	var tf := tan(deg_to_rad(fov_deg) * 0.5)
	return maxf(height * 0.5 / tf, width * 0.5 / (tf * maxf(aspect, 0.2))) * 1.08


# {p: the point to orbit round, d: the distance for zoom 1}. stage: 0 while the pieces are still laid out, 1 once they are on
# the body (only the "auto" focus follows it).
static func focus(name: String, h: float, aspect: float, fov_deg: float, stage: float) -> Dictionary:
	match name:
		"body":
			return {"p": Vector3(0.0, h * 0.52, 0.0), "d": fit(h * 1.08, 1.0, aspect, fov_deg)}
		"head":
			return {"p": Vector3(0.0, h * 0.9, 0.02), "d": fit(0.55, 0.5, aspect, fov_deg)}
		"torso":
			return {"p": Vector3(0.0, h * 0.7, 0.0), "d": fit(0.85, 0.75, aspect, fov_deg)}
		"legs":
			return {"p": Vector3(0.0, h * 0.27, 0.0), "d": fit(h * 0.58, 0.6, aspect, fov_deg)}
		"room":
			return {"p": Vector3(0.0, 1.1, 0.0), "d": 7.5}
		"mirror":
			# in front of the mirror, a little off its axis, looking into it: the mannequin (behind the camera) shows in the glass
			# from the back and the side. With the camera 2 m out and 29 degrees off the axis her reflection falls inside the
			# middle panel (the mirror is 3.7 m from her).
			var m := Studio.MIRROR_POS
			return {"p": Vector3(m.x, 1.0, m.z), "d": 2.0}
		"table":
			var t := Studio.TABLE_POS
			return {"p": Vector3(t.x + 0.4, 0.95, t.z), "d": 3.3}
	# auto: the whole mannequin, centred; while the panels hover far from the body (stage 0), further back to take them in
	var base := fit(h * 1.08, 1.0, aspect, fov_deg)
	return {"p": Vector3(0.0, h * 0.5, 0.0), "d": base * lerpf(1.55, 1.0, stage)}
