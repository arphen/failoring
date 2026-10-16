# Moving and turning panels in the 3D view (the "Move panels" mode). The editor shows the garment's start positions
# (timeline stage 3, before the drape) and a gizmo on the picked panel:
#   - drag the panel itself: it moves in the plane facing the camera
#   - drag a coloured arrow (red across, green up, blue forward): it moves along that axis
#   - drag a coloured ring: it turns about that axis, through the panel's centre
# The drag previews on the start positions; the release stores the move in the piece (place3d.gd: p["place"], saved with
# the pattern) as one undoable action, and the simulation is rebuilt from there.
# Orbit, pan and zoom keep working: a press that is not on a panel or the gizmo goes to the camera.
extends RefCounted

const Place3D = preload("res://place3d.gd")
const Outfit = preload("res://outfit.gd")
const Table = preload("res://table.gd")
const Arrange = preload("res://arrange.gd")
const SewCheck = preload("res://sew_check.gd")
const Wrap = preload("res://wrap.gd")

const AXIS_COL := [Color("#ff5a5a"), Color("#5ad35a"), Color("#5a9bff")]
const AXES := [Vector3.RIGHT, Vector3.UP, Vector3.BACK]
const GRAB_PX := 9.0

var ed                         # the editor (editor.gd)
var active := false
var pid := ""                  # merged id (garment prefix + piece id) of the picked panel, "" for none
var gizmo: MeshInstance3D
var _im := ImmediateMesh.new()
var drag := {}                 # {mode: "move"|"axis"|"ring", axis, start (pane px), base (positions), c (centre), ...}
var hover := ""                # "axis0".."axis2", "ring0".."ring2", or ""
var preview := PackedVector3Array()
# Arrange mode (arrange.gd): dots round the body; hovering one with a piece picked shows the piece there (a ghost),
# clicking puts it there.
var arrange := false
var arr = Arrange.new()
var _arr_key = null            # the start body the dots were built for
var dots: MeshInstance3D
var _dim := ImmediateMesh.new()
var hover_pt := -1
const DOT_PX := 12.0


func setup(editor) -> void:
	ed = editor
	gizmo = MeshInstance3D.new()
	gizmo.mesh = _im
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	mat.no_depth_test = true           # drawn over the cloth and the body
	mat.render_priority = 10
	gizmo.material_override = mat
	gizmo.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	gizmo.visible = false
	ed.stage.add_child(gizmo)
	dots = MeshInstance3D.new()
	dots.mesh = _dim
	var dmat := StandardMaterial3D.new()
	dmat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	dmat.vertex_color_use_as_albedo = true
	dmat.no_depth_test = true
	dmat.render_priority = 10
	dots.material_override = dmat
	dots.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	dots.visible = false
	ed.stage.add_child(dots)


# Arrange mode: the dots, and a click on one puts the picked piece there. It shows the start positions like Move panels.
func set_arrange(on: bool) -> void:
	arrange = on
	hover_pt = -1
	if on:
		set_active(true)
	else:
		_draw_dots()


func set_active(on: bool) -> void:
	active = on
	if not on:
		arrange = false
	drag = {}
	if on:
		ed._drain()
		ed.playing = false
		ed.tl_t = 2.0          # stage 3 "Sew": the start positions, where the placement shows
		ed._sync_tl_ui()
		ed._last_shown = ""
		ed._show_current()
	refresh()


# ------------------------------------------------------------------ geometry helpers
func _range(merged: String) -> Dictionary:
	if ed.meta.is_empty() or not (ed.meta["ranges"] as Dictionary).has(merged):
		return {}
	return ed.meta["ranges"][merged]


# The start positions the mode shows (meta p2), or the last drag's preview until the rebuild replaces meta.
var _preview_meta = null


func _positions() -> PackedVector3Array:
	if _preview_meta != null and is_same(_preview_meta, ed.meta) and not preview.is_empty():
		return preview
	# before the panels are fitted they hover far from the body (stage 0): the panel on screen is the one to pick and to move. A
	# rigid move or a turn about the panel's own centre is the same on the far pose as on the fitted one, so the placement it stores
	# is the same too.
	var n: int = int(ed.meta.get("n", 0))
	if ed.tl_t < 2.0 and ed.cur_cloth.size() >= n and n > 0:
		return ed.cur_cloth
	return ed.meta.get("p2", PackedVector3Array())


func centre() -> Variant:
	var r := _range(pid)
	var pos := _positions()
	if r.is_empty() or pos.size() < int(r["first"]) + int(r["count"]):
		return null
	return Place3D.centre(pos, int(r["first"]), int(r["count"]))


# Pane-local pixel <-> 3D view pixel (the SubViewport renders at the window's scale, see editor.gd _sync_view_px)
func _k() -> Vector2:
	var ps: Vector2 = ed.view_pane.size
	var vs := Vector2(ed.sub_view.size)
	return Vector2(vs.x / maxf(1.0, ps.x), vs.y / maxf(1.0, ps.y))


func _ray(local: Vector2) -> Array:
	var px := local * _k()
	return [ed.cam.project_ray_origin(px), ed.cam.project_ray_normal(px)]


func _to_pane(w: Vector3) -> Vector2:
	return ed.cam.unproject_position(w) / _k()


# The panel under a pane point: [merged piece id, hit point] or [] when the ray misses the cloth.
func pick(local: Vector2) -> Array:
	if ed.meta.is_empty():
		return []
	var ray := _ray(local)
	var o: Vector3 = ray[0]
	var d: Vector3 = ray[1]
	var pos := _positions()
	var tris: PackedInt32Array = ed.meta["tris"]
	var best_t := 1e9
	var best_v := -1
	for k in tris.size() / 3:
		var ia := tris[3 * k]
		var ib := tris[3 * k + 1]
		var ic := tris[3 * k + 2]
		if ic >= pos.size():
			continue
		var t := _ray_tri(o, d, pos[ia], pos[ib], pos[ic])
		if t > 0.0 and t < best_t:
			best_t = t
			best_v = ia
	if best_v < 0:
		return []
	for m in ed.meta["ranges"]:
		var r: Dictionary = ed.meta["ranges"][m]
		if best_v >= int(r["first"]) and best_v < int(r["first"]) + int(r["count"]):
			return [String(m), o + d * best_t]
	return []


static func _ray_tri(o: Vector3, d: Vector3, a: Vector3, b: Vector3, c: Vector3) -> float:
	var e1 := b - a
	var e2 := c - a
	var p := d.cross(e2)
	var det := e1.dot(p)
	if absf(det) < 1e-12:
		return -1.0
	var inv := 1.0 / det
	var s := o - a
	var u := s.dot(p) * inv
	if u < 0.0 or u > 1.0:
		return -1.0
	var q := s.cross(e1)
	var v := d.dot(q) * inv
	if v < 0.0 or u + v > 1.0:
		return -1.0
	return e2.dot(q) * inv


# Gizmo sizes in metres: about 90 px on screen at the panel's distance.
func _size(c: Vector3) -> float:
	var dist: float = (ed.cam.global_position - c).length()
	var fov := deg_to_rad(float(ed.cam.fov))
	var px_per_m: float = float(ed.sub_view.size.y) / (2.0 * dist * tan(fov * 0.5)) / _k().y
	return 90.0 / maxf(1e-6, px_per_m)


func _ring_pts(c: Vector3, axis: int, r: float) -> Array:
	var a: Vector3 = AXES[axis]
	var u: Vector3 = AXES[(axis + 1) % 3]
	var v: Vector3 = a.cross(u)
	var out := []
	for i in 49:
		var th := TAU * float(i) / 48.0
		out.append(c + (u * cos(th) + v * sin(th)) * r)
	return out


# The gizmo handle under a pane point: "axis0".."axis2", "ring0".."ring2" or "".
func handle_at(local: Vector2) -> String:
	var c = centre()
	if c == null:
		return ""
	var L := _size(c)
	var best := ""
	var bd := GRAB_PX
	for i in 3:
		var a := _to_pane(c)
		var b := _to_pane(c + AXES[i] * L)
		var d := _seg_dist(local, a, b)
		if d < bd and a.distance_to(b) > 8.0:
			bd = d
			best = "axis%d" % i
	for i in 3:
		var pts := _ring_pts(c, i, L * 0.75)
		for k in pts.size() - 1:
			var d2 := _seg_dist(local, _to_pane(pts[k]), _to_pane(pts[k + 1]))
			if d2 < bd:
				bd = d2
				best = "ring%d" % i
	return best


static func _seg_dist(p: Vector2, a: Vector2, b: Vector2) -> float:
	var ab := b - a
	var t := clampf((p - a).dot(ab) / maxf(ab.length_squared(), 1e-9), 0.0, 1.0)
	return (a + ab * t).distance_to(p)


# ------------------------------------------------------------------ input
# Returns true when the event was used (the camera does not get it).
func input(e: InputEvent) -> bool:
	if not active or ed.meta.is_empty():
		return false
	if arrange:
		return _arrange_input(e)
	if e is InputEventMouseMotion:
		var m := e as InputEventMouseMotion
		if not drag.is_empty():
			_drag_to(m.position)
			return true
		var h := handle_at(m.position) if pid != "" else ""
		if h != hover:
			hover = h
			refresh()
		return false
	if e is InputEventMouseButton:
		var b := e as InputEventMouseButton
		if b.button_index != MOUSE_BUTTON_LEFT:
			return false
		if b.pressed and not b.double_click:
			return _press(b.position)
		if not b.pressed and not drag.is_empty():
			_release()
			return true
	return false


func _press(local: Vector2) -> bool:
	var h := handle_at(local) if pid != "" else ""
	var c = centre()
	if h != "" and c != null:
		_start(h.substr(0, 4) if h.begins_with("axis") else "ring", int(h.right(1)), local, c)
		return true
	var hit := pick(local)
	if hit.is_empty():
		return false   # empty space: the camera orbits
	var m := String(hit[0])
	if not _select(m):
		return true
	c = centre()
	if c == null:
		return true
	_start("move", -1, local, c)
	drag["hit"] = hit[1]
	return true


# Makes merged piece m the picked panel: its garment becomes the active one, the piece the table's selection.
# False when the garment is locked.
func _select(m: String) -> bool:
	var uid := Outfit.uid_of(m)
	var gi: int = ed._index_of_uid(uid)
	if gi < 0:
		return false
	if gi != ed.active:
		ed._outfit_select(gi)
	pid = m
	ed.sel = m.substr(Outfit.prefix(uid).length())
	ed.sel_v = -1
	ed.changed("ui")
	if ed.active_locked():
		ed.notify("This garment is locked: unlock it in the Outfit tab to move its panels.")
		refresh()
		return false
	refresh()
	return true


func _start(mode: String, axis: int, local: Vector2, c: Vector3) -> void:
	drag = {"mode": mode, "axis": axis, "start": local, "c": c, "base": _positions().duplicate(),
		"dq": Quaternion.IDENTITY, "dt": Vector3.ZERO}
	ed.user_cam_until = Time.get_ticks_msec() + 3000


func _drag_to(local: Vector2) -> void:
	var c: Vector3 = drag["c"]
	var mode := String(drag["mode"])
	var dq := Quaternion.IDENTITY
	var dt := Vector3.ZERO
	if mode == "move":
		# the plane through the grabbed point, facing the camera
		var n: Vector3 = -ed.cam.global_transform.basis.z
		var p0: Vector3 = drag.get("hit", c)
		var r0 := _ray(drag["start"])
		var r1 := _ray(local)
		var a = Plane(n, p0).intersects_ray(r0[0], r0[1])
		var b = Plane(n, p0).intersects_ray(r1[0], r1[1])
		if a != null and b != null:
			dt = b - a
	elif mode == "axis":
		var ax: Vector3 = AXES[int(drag["axis"])]
		dt = ax * (_axis_param(local, c, ax) - _axis_param(drag["start"], c, ax))
	else:
		var ax2: Vector3 = AXES[int(drag["axis"])]
		var cs := _to_pane(c)
		var a0 := ((drag["start"] as Vector2) - cs).angle()
		var a1 := (local - cs).angle()
		var ang := a1 - a0
		# screen angles turn clockwise on screen; flip when the axis points away from the camera
		var toward: bool = ax2.dot(ed.cam.global_position - c) > 0.0
		ang = -ang if toward else ang
		if Input.is_key_pressed(KEY_SHIFT):
			ang = snappedf(ang, deg_to_rad(15.0))
		dq = Quaternion(ax2, ang)
	if Input.is_key_pressed(KEY_SHIFT) and mode != "ring":
		dt = dt.snapped(Vector3(0.01, 0.01, 0.01))  # Shift: whole centimetres
	drag["dq"] = dq
	drag["dt"] = dt
	var r := _range(pid)
	var base: PackedVector3Array = drag["base"]
	preview = base.duplicate()
	for i in int(r["count"]):
		var vi := int(r["first"]) + i
		preview[vi] = dq * (base[vi] - c) + c + dt
	_preview_meta = ed.meta
	ed.cur_cloth = preview
	ed._set_cloth_mesh(ed._with_midpoints(preview))
	ed._update_stitches(preview, ed.tl_t)
	refresh(c + dt)


# The parameter along the line c + s*ax closest to the pointer's ray.
func _axis_param(local: Vector2, c: Vector3, ax: Vector3) -> float:
	var r := _ray(local)
	var o: Vector3 = r[0]
	var d: Vector3 = r[1]
	var w := c - o
	var b := ax.dot(d)
	var den := 1.0 - b * b
	if absf(den) < 1e-6:
		return 0.0
	return (b * d.dot(w) - ax.dot(w)) / den


func _release() -> void:
	var dq: Quaternion = drag["dq"]
	var dt: Vector3 = drag["dt"]
	var mode := String(drag["mode"])
	drag = {}
	if dt.length() < 1e-5 and dq.is_equal_approx(Quaternion.IDENTITY):
		return
	var local_id: String = pid.substr(Outfit.prefix(Outfit.uid_of(pid)).length())
	var p = Table.piece_by_id(ed.pieces, local_id)
	if p == null:
		return
	ed.push_undo(("Turn %s in 3D" if mode == "ring" else "Move %s in 3D") % String(p.get("name", local_id)))
	Place3D.compose(p, dq, dt)
	ed.changed("structure")


# ------------------------------------------------------------------ arrange
func _ensure_points() -> void:
	var body = ed._start_body
	if body == null:
		return
	if _arr_key == null or not is_same(_arr_key, body):
		arr.build(body)
		_arr_key = body


func dot_at(local: Vector2) -> int:
	_ensure_points()
	var best := -1
	var bd := DOT_PX
	for i in arr.points.size():
		var d := _to_pane(arr.points[i]["pos"]).distance_to(local)
		if d < bd:
			bd = d
			best = i
	return best


func _arrange_input(e: InputEvent) -> bool:
	if e is InputEventMouseMotion:
		if (e as InputEventMouseMotion).button_mask != 0:
			return false   # a drag orbits the camera
		var h := dot_at((e as InputEventMouseMotion).position)
		if h != hover_pt:
			hover_pt = h
			_draw_dots()
		return false
	if e is InputEventMouseButton:
		var b := e as InputEventMouseButton
		if b.button_index != MOUSE_BUTTON_LEFT or not b.pressed or b.double_click:
			return false
		var h2 := dot_at(b.position)
		if h2 >= 0:
			put_on(h2)
			return true
		var hit := pick(b.position)
		if not hit.is_empty():
			_select(String(hit[0]))
			_draw_dots()
			return true
	return false


# Puts the selected piece (the table's selection) on arrangement point i: its wrap is set (arrange.gd), any free 3D move
# is dropped, the mirror twin follows. One undoable action; the garment is rebuilt from there.
func put_on(i: int) -> bool:
	_ensure_points()
	var p = ed._piece()
	if p == null:
		ed.notify("Arrange: pick a piece first (click it on the cutting table or in the 3D view), then click a dot.")
		return false
	if ed.active_locked():
		ed.notify("This garment is locked: unlock it in the Outfit tab to arrange its pieces.")
		return false
	var pt: Dictionary = arr.points[i]
	ed.push_undo("Arrange %s on %s" % [String(p.get("name", p["id"])), String(pt["label"]).to_lower()])
	var w := Arrange.wrap_for(p, pt, ed._start_body["sections"])
	if String(pt["kind"]) == "arm":   # slide it along the arm to where its seams start closest (arrange.gd)
		w = Arrange.fit_sleeve(p, w, ed.pieces, ed.seams, ed._start_body["sections"], ed._start_body["arms"])
	p["wrap"] = w
	p.erase("place")
	ed._sym_sync(p)
	ed.changed("structure")
	ed.notify("%s arranged on %s." % [String(p.get("name", p["id"])), String(pt["label"]).to_lower()])
	return true


# The dots (a small diamond each; bigger and white when hovered) and, while one is hovered with a piece picked, the piece
# drawn there as a wire ghost.
func _draw_dots() -> void:
	_dim.clear_surfaces()
	dots.visible = active and arrange
	if not dots.visible:
		return
	_ensure_points()
	if arr.points.is_empty():
		return
	_dim.surface_begin(Mesh.PRIMITIVE_LINES)
	for i in arr.points.size():
		var pt: Dictionary = arr.points[i]
		var c: Vector3 = pt["pos"]
		var hot: bool = i == hover_pt
		var r: float = (0.022 if hot else 0.013) * (_size(c) / 0.25)
		var col: Color = Color.WHITE if hot else {"torso": Color("#7dd3fc"), "arm": Color("#fbbf24"), "leg": Color("#c4b5fd")}[String(pt["kind"])]
		var tips := [Vector3(r, 0, 0), Vector3(0, r, 0), Vector3(0, 0, r), Vector3(-r, 0, 0), Vector3(0, -r, 0), Vector3(0, 0, -r)]
		for a in 6:
			for b in range(a + 1, 6):
				if a + 3 == b:
					continue  # opposite tips: the octahedron's edges only
				_im_line(_dim, c + tips[a], c + tips[b], col)
	if hover_pt >= 0:
		_ghost(hover_pt)
	_dim.surface_end()


func _im_line(im: ImmediateMesh, a: Vector3, b: Vector3, col: Color) -> void:
	im.surface_set_color(col)
	im.surface_add_vertex(a)
	im.surface_set_color(col)
	im.surface_add_vertex(b)


# The selected piece as it would start on point i (its mesh edges, white).
func _ghost(i: int) -> void:
	var p = ed._piece()
	var m: String = ed._merged_sel()
	if p == null or m == "" or not (ed.meta["uv"] as Dictionary).has(m):
		return
	var q: Dictionary = (p as Dictionary).duplicate(true)
	q["wrap"] = Arrange.wrap_for(q, arr.points[i], ed._start_body["sections"])
	var w = Wrap.new(q, ed._start_body["sections"], ed._start_body["arms"])
	var uv: PackedVector2Array = ed.meta["uv"][m]
	var r: Dictionary = ed.meta["ranges"][m]
	var f0 := int(r["first"])
	var pos := PackedVector3Array()
	pos.resize(uv.size())
	for k in uv.size():
		pos[k] = w.point(uv[k].x, uv[k].y)
	var tris: PackedInt32Array = ed.meta["tris"]
	var col := Color(1, 1, 1, 0.85)
	for t in tris.size() / 3:
		var a := tris[3 * t] - f0
		if a < 0 or a >= uv.size():
			continue
		var b := tris[3 * t + 1] - f0
		var c := tris[3 * t + 2] - f0
		_im_line(_dim, pos[a], pos[b], col)
		_im_line(_dim, pos[b], pos[c], col)


# ------------------------------------------------------------------ drawing
func refresh(at: Variant = null) -> void:
	if arrange:
		_draw_dots()
	if drag.is_empty():
		pid = ed._merged_sel()   # the picked panel is the table's selected piece (of the active garment)
	_im.clear_surfaces()
	var c = at if at != null else centre()
	gizmo.visible = active and not arrange and pid != "" and c != null
	if not gizmo.visible:
		return
	var L := _size(c)
	_im.surface_begin(Mesh.PRIMITIVE_LINES)
	for i in 3:
		var col: Color = AXIS_COL[i]
		var hot := hover == "axis%d" % i or (not drag.is_empty() and String(drag["mode"]) == "axis" and int(drag["axis"]) == i)
		if hot:
			col = col.lightened(0.5)
		var tip: Vector3 = c + AXES[i] * L
		# a thick look from a few parallel lines
		for off in [Vector3.ZERO, AXES[(i + 1) % 3] * L * 0.006, AXES[(i + 2) % 3] * L * 0.006]:
			_im.surface_set_color(col)
			_im.surface_add_vertex(c + off)
			_im.surface_set_color(col)
			_im.surface_add_vertex(tip + off)
		# arrow head
		var u: Vector3 = AXES[(i + 1) % 3] * L * 0.06
		var v: Vector3 = AXES[(i + 2) % 3] * L * 0.06
		for side in [u, -u, v, -v]:
			_im.surface_set_color(col)
			_im.surface_add_vertex(tip)
			_im.surface_set_color(col)
			_im.surface_add_vertex(tip - AXES[i] * L * 0.15 + side)
	for i in 3:
		var col2: Color = AXIS_COL[i]
		var hot2 := hover == "ring%d" % i or (not drag.is_empty() and String(drag["mode"]) == "ring" and int(drag["axis"]) == i)
		col2 = col2.lightened(0.5) if hot2 else Color(col2, 0.85)
		var pts := _ring_pts(c, i, L * 0.75)
		for k in pts.size() - 1:
			_im.surface_set_color(col2)
			_im.surface_add_vertex(pts[k])
			_im.surface_set_color(col2)
			_im.surface_add_vertex(pts[k + 1])
	_im.surface_end()
