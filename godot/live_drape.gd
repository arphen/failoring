# Live drape (docs/LIVE_DRAPE_PLAN.md): the garment runs live on the mannequin while the user works on it, the way one
# works on a dress form. The simulator steps every drawn frame on the editor's worker (the same job/collect path as the
# timeline, only the newest frame is kept); the body stands still.
#
# Entered before the seams close (timeline stages 0-2) the seams are off and each panel hangs from its top edge (a hold),
# which can be slid out from the body and back (it swings and settles); Sew ramps the seams in and lets the holds go.
# Entered while sewing or draping, the cloth goes on from the shown frame.
#
# Tools on the active panel (palette at the top of the 3D view): Grab drags the cloth (a release throws it), Pin pins it
# where it is, Tack sews two points together (saved in the pattern: p["tacks"]), Length grows or shrinks the panel below
# the press with a stiff rod drag (written to the pattern on release), Link joins panels' lengths (p["length_group"]).
extends RefCounted

const Outfit = preload("res://outfit.gd")
const Table = preload("res://table.gd")
const Geom = preload("res://cut_geom.gd")
const BodyGen = preload("res://body_gen.gd")

const TOOLS := ["grab", "pin", "tack", "length", "link"]
const TOOL_LABEL := {"grab": "Grab", "pin": "Pin", "tack": "Tack", "length": "Length", "link": "Link"}
const TOOL_KEY := {"grab": KEY_G, "pin": KEY_P, "tack": KEY_T, "length": KEY_Y, "link": KEY_K}
const TOOL_TIP := {
	"grab": "Grab (G): drag the cloth; let go while moving to throw it",
	"pin": "Pin (P): click the cloth to pin it where it is, like a pin into a dress form; click a pin to take it out",
	"tack": "Tack (T): click a point of the cloth, then another: they are sewn together (saved with the pattern). Click a tack's end to unpick it",
	"length": "Length (Y): press on the panel and pull toward the hem to lengthen it below that point, toward the top to shorten it. The pull bends a stiff rod: the more it bends, the faster the cloth grows or shrinks. Written to the pattern on release",
	"link": "Link (K): click another panel to link its length to this one (front and back change together); click a linked panel to unlink it",
}
const GRAB_R := 0.03          # m: the grabbed patch round the hit (at least 0.6 edge lengths)
const PIN_R := 0.015
const TACK_SHARE := 0.02      # m of seam a tack stands for (its stiffness)
const HOLD_BAND := 0.02       # m: a panel's hold is its vertices within this of its top
const HOLD_FREQ := 0.7        # Hz and damping ratio of a hold sliding to its distance: it overshoots a little, so the cloth swings
const HOLD_ZETA := 0.7
const HOLD_SPEED := 0.6       # m/s: the fastest a hold slides (a hand carrying the panel, not a kick)
const DIST_MAX := 1.2         # m
const SEW_FRAMES := 40        # frames over which Sew ramps the seams in; the holds let go half way
const ROD_PX := 70.0          # the Length rod: pointer travel (px) at which it is bent all the way
const LEN_RATE := 0.15        # m/s of length change with the rod bent all the way
const K_MIN := 0.3            # a length change stretches the part below the line by k = (L + d) / L in [K_MIN, K_MAX]
const K_MAX := 3.0
const LIVE_SUBSTEPS := 5      # the GPU fast solve while live (10 and 100 on the timeline): its cost is the dispatch count
const LIVE_CG := 50
const SETTLE_FRAMES := 45     # pre-sew entry: the panels drop onto their holds with heavy damping, then swing freely
const SETTLE_DAMP := 25.0

var ed                        # the editor (editor.gd)
var active := false
var tool := "grab"
var pid := ""                 # merged id of the active panel
var sewn := true
var _sew_t := -1              # frames into a Sew ramp, -1 when none runs
var _settle := -1             # frames left of the settle after entering (heavy damping), -1 when none
var body := PackedVector3Array()
var _abd := 0.0               # arm abduction of the body now (a dressing pose comes down after Sew)
var _lower_t := -1
var ops: Array = []           # [method, args] for the sim, run on the worker before the next step
var latest := {}              # the newest frame: {pos, normal, cloth, ...}
var frames_run := 0
var holds := {}               # pid -> {verts, base, dir, d, v, goal}
var pins: Array = []          # {verts: PackedInt32Array}
var tacks: Array = []         # {a, b: global vertices, pa, pb: [merged pid, Vector2 pattern point]} (live and loaded)
var grab := {}                # {verts, offs, plane_p, plane_n, local}
var tack_a := {}              # first point of a tack in progress: {v, pid, uv}
var len_drag := {}            # the Length tool's drag (see _len_press)
var rest := {}                # pid -> PackedVector2Array: each vertex's rest point now (pattern coordinates)
var _fit := {}                # pid -> {g: Vector2, c: float, a: Vector2, centre: Vector2}: world height over the pattern
var changed_pattern := false
var palette: Control
var _tool_btns := {}
var _sew_btn: Button
var _dist_slider: HSlider
var _dist_label: Label
var _name_label: Label
var mini: Control             # the mini pattern (bottom left)
var overlay: Control          # the Length rod and hints, drawn over the view
var marks: MeshInstance3D     # pins, tacks and the grab line in 3D
var _im := ImmediateMesh.new()
var _shown := -1


func setup(editor) -> void:
	ed = editor
	marks = MeshInstance3D.new()
	marks.mesh = _im
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	mat.no_depth_test = true
	mat.render_priority = 10
	marks.material_override = mat
	marks.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	marks.visible = false
	ed.stage.add_child(marks)


# ------------------------------------------------------------------ UI
func build_ui(pane: Control) -> void:
	palette = PanelContainer.new()
	palette.name = "LivePalette"
	palette.visible = false
	var row := HFlowContainer.new()   # wraps in a narrow 3D pane
	row.add_theme_constant_override("h_separation", 4)
	row.add_theme_constant_override("v_separation", 4)
	palette.add_child(row)
	_name_label = Label.new()
	_name_label.custom_minimum_size.x = 90
	_name_label.add_theme_font_size_override("font_size", 12)
	row.add_child(_name_label)
	var group := ButtonGroup.new()
	for t in TOOLS:
		var b := Button.new()
		b.name = "Live" + String(TOOL_LABEL[t])
		b.text = String(TOOL_LABEL[t])
		b.toggle_mode = true
		b.button_group = group
		b.focus_mode = Control.FOCUS_NONE
		b.tooltip_text = String(TOOL_TIP[t])
		b.button_pressed = t == tool
		b.pressed.connect(func() -> void: set_tool(t))
		row.add_child(b)
		_tool_btns[t] = b
	row.add_child(VSeparator.new())
	_sew_btn = Button.new()
	_sew_btn.name = "LiveSew"
	_sew_btn.focus_mode = Control.FOCUS_NONE
	_sew_btn.pressed.connect(func() -> void: set_sewn(not sewn))
	row.add_child(_sew_btn)
	var dl := Label.new()
	dl.text = "Distance"
	dl.add_theme_font_size_override("font_size", 12)
	row.add_child(dl)
	_dist_slider = HSlider.new()
	_dist_slider.name = "LiveDistance"
	_dist_slider.min_value = 0.0
	_dist_slider.max_value = DIST_MAX * 100.0
	_dist_slider.step = 1.0
	_dist_slider.custom_minimum_size.x = 110
	_dist_slider.focus_mode = Control.FOCUS_NONE
	_dist_slider.tooltip_text = "How far the active panel hangs from the body (cm), while it is not sewn. Alt + wheel over the view does the same"
	_dist_slider.value_changed.connect(func(v: float) -> void: set_distance(pid, v / 100.0))
	row.add_child(_dist_slider)
	_dist_label = Label.new()
	_dist_label.custom_minimum_size.x = 44
	_dist_label.add_theme_font_size_override("font_size", 12)
	row.add_child(_dist_label)
	var leave_b := Button.new()
	leave_b.text = "Done"
	leave_b.focus_mode = Control.FOCUS_NONE
	leave_b.tooltip_text = "Leave live drape (L or Esc): back to the timeline"
	leave_b.pressed.connect(func() -> void: ed._set_live_mode(false))
	row.add_child(leave_b)
	pane.add_child(palette)
	mini = Control.new()
	mini.name = "LiveMiniPattern"
	mini.visible = false
	mini.custom_minimum_size = Vector2(190, 210)
	mini.size = Vector2(190, 210)
	mini.mouse_filter = Control.MOUSE_FILTER_STOP
	mini.tooltip_text = "The active panel's cut shape. Press and pull up or down here to shorten or lengthen it below that point (as the Length tool)"
	mini.draw.connect(_draw_mini)
	mini.gui_input.connect(_mini_input)
	pane.add_child(mini)
	overlay = Control.new()
	overlay.name = "LiveOverlay"
	overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
	overlay.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	overlay.draw.connect(_draw_overlay)
	pane.add_child(overlay)
	_sync_ui()


func _sync_ui() -> void:
	if palette == null:
		return
	palette.visible = active
	mini.visible = active and pid != ""
	marks.visible = active
	if not active:
		return
	_layout()
	for t in _tool_btns:
		(_tool_btns[t] as Button).set_pressed_no_signal(t == tool)
	_sew_btn.text = "Unsew" if sewn else "Sew"
	_sew_btn.tooltip_text = "Open the seams again: each panel hangs from its top edge" if sewn else "Sew: the seams pull the panels together and the panels are let go"
	var h: Dictionary = holds.get(pid, {})
	_dist_slider.editable = not h.is_empty()
	if not h.is_empty():
		_dist_slider.set_value_no_signal(float(h["goal"]) * 100.0)
		_dist_label.text = "%.0f cm" % (float(h["d"]) * 100.0)
	else:
		_dist_label.text = "sewn" if sewn else "-"
	var nm := "pick a panel"
	if pid != "":
		var tp = _table_piece(pid)
		nm = String(tp.get("name", tp.get("id", pid))) if tp != null else pid
	_name_label.text = nm
	mini.queue_redraw()


# Under the 3D view's bar on the left, as wide as the pane allows; the mini pattern below it (clear of the view chips).
func _layout() -> void:
	var pane: Control = ed.view_pane
	var w := minf(pane.size.x - 16.0, 640.0)
	palette.position = Vector2(8.0, 44.0)
	palette.size = Vector2(w, 0.0)
	palette.custom_minimum_size.x = w
	mini.position = Vector2(8.0, palette.position.y + palette.get_combined_minimum_size().y + 8.0)


func set_tool(t: String) -> void:
	tool = t
	tack_a = {}
	_sync_ui()
	overlay.queue_redraw()


# ------------------------------------------------------------------ enter, leave, step
func enter() -> void:
	ed._drain()
	ed.playing = false
	active = true
	latest = {}
	frames_run = 0
	holds.clear()
	pins.clear()
	grab = {}
	tack_a = {}
	len_drag = {}
	ops = []
	_fit.clear()
	rest.clear()
	_lower_t = -1
	_sew_t = -1
	_settle = -1
	changed_pattern = false
	for m in ed.meta["order"]:
		rest[String(m)] = (ed.meta["uv"][m] as PackedVector2Array).duplicate()
	ops.append(["set_param", ["gravity_y", -9.81]])
	if ed.solver_gpu:   # the basic live setting: half the GPU solve's substeps and CG budget (the rebuild on leaving restores it)
		ops.append(["set_param", ["substeps", LIVE_SUBSTEPS]])
		ops.append(["set_param", ["cg_max_iterations", LIVE_CG]])
	var pre_sew: bool = ed.tl_t < 3.0 or ed.frames.is_empty()
	if pre_sew:
		var shown: PackedVector3Array = ed.cur_cloth if ed.cur_cloth.size() >= int(ed.meta["n"]) else ed.meta["p2"]
		ops.append(["set_positions", [shown.slice(0, int(ed.meta["n"]))]])
		body = ed._start_v3
		_abd = ed._dress_abd
		ops.append(["set_body_positions", [body]])
		sewn = false
		ops.append(["set_param", ["seam_scale", 0.0]])
		_hold_all(shown)
		_settle = SETTLE_FRAMES
		ops.append(["set_param", ["damping_scale", SETTLE_DAMP]])
	else:
		var i: int = ed._frame_idx()
		var fr: Dictionary = ed.frames[i]
		body = fr["body"]
		_abd = float(ed.body_stand.get("armAbduct", BodyGen.ARM_ABDUCT)) if i > 0 else ed._dress_abd
		if i != ed.frames.size() - 1:   # scrubbed back: the simulator is further on, put it back where the view is
			ops.append(["set_positions", [fr["cloth"]]])
			ops.append(["set_body_positions", [body]])
		sewn = true
	_load_tacks()
	var sel_m: String = ed._sim_id_of(ed.sel) if ed.sel != null else ""
	pid = sel_m
	_sync_ui()


# Leaves live drape. The simulator's state is the live one, not the timeline's, so the garment is rebuilt.
func leave() -> void:
	ed._drain()
	active = false
	grab = {}
	len_drag = {}
	tack_a = {}
	holds.clear()
	pins.clear()
	tacks.clear()
	_im.clear_surfaces()
	_sync_ui()
	overlay.queue_redraw()


# Called by the editor after a rebuild while live drape is on (an edit on the table): the new garment starts live from
# its start positions, sewn or not as before.
func restart() -> void:
	var was_sewn := sewn
	var keep_tool := tool
	var keep_pid := pid
	ed.tl_t = 2.0
	ed._last_shown = ""
	ed._show_current()
	enter()
	tool = keep_tool
	if (ed.meta["order"] as Array).has(keep_pid):
		pid = keep_pid
	if was_sewn:
		set_sewn(true)
	_sync_ui()


# Every physics frame: the holds' springs, the drags, then a step when the worker is free.
func tick(delta: float) -> void:
	if not active or ed.sim == null or ed.meta.is_empty():
		return
	for m in holds:
		var h: Dictionary = holds[m]
		var w := TAU * HOLD_FREQ
		var acc := w * w * (float(h["goal"]) - float(h["d"])) - 2.0 * HOLD_ZETA * w * float(h["v"])
		h["v"] = clampf(float(h["v"]) + acc * delta, -HOLD_SPEED, HOLD_SPEED)
		h["d"] = float(h["d"]) + float(h["v"]) * delta
	if not len_drag.is_empty():
		_len_tick(delta)
	if Engine.get_physics_frames() % 15 == 0:
		_layout()
	if ed._in_flight:
		return
	ed._submit_step(_job())
	_sync_dist_label()


func _sync_dist_label() -> void:
	var h: Dictionary = holds.get(pid, {})
	if not h.is_empty() and _dist_label != null:
		_dist_label.text = "%.0f cm" % (float(h["d"]) * 100.0)


func _job() -> Dictionary:
	var job := {"body": body, "move": false, "live": true, "next_move": false, "next_body": body}
	if _settle >= 0:
		_settle -= 1
		if _settle < 0:
			ops.append(["set_param", ["damping_scale", 1.0]])
	if _sew_t >= 0:
		_sew_t += 1
		ops.append(["set_param", ["seam_scale", minf(1.0, float(_sew_t) / float(SEW_FRAMES))]])
		if _sew_t == SEW_FRAMES / 2:
			_release_holds()
		if _sew_t >= SEW_FRAMES:
			_sew_t = -1
			if _abd > float(ed.body_stand.get("armAbduct", BodyGen.ARM_ABDUCT)) + 1e-3:
				_lower_t = 0
	if _lower_t >= 0:
		_lower_t += 1
		var u: float = ed._smooth(float(_lower_t) / float(ed.DRESS_LOWER))
		var stand_abd := float(ed.body_stand.get("armAbduct", BodyGen.ARM_ABDUCT))
		var abd := lerpf(_abd, stand_abd, u)
		body = ed._to_v3a(BodyGen.pose(ed.M, ed.body_rest, 0.0, {"stride": 0.0, "arms": 0.0, "armAbduct": abd})["pos"]) if _lower_t < ed.DRESS_LOWER else ed._stand_v3
		job["body"] = body
		job["move"] = true
		if _lower_t >= ed.DRESS_LOWER:
			_abd = stand_abd
			_lower_t = -1
	# kinematic targets: holds, the grab
	var dv := PackedInt32Array()
	var dp := PackedVector3Array()
	for m in holds:
		var h: Dictionary = holds[m]
		var verts: PackedInt32Array = h["verts"]
		var base: PackedVector3Array = h["base"]
		var off: Vector3 = (h["dir"] as Vector3) * (float(h["d"]) - float(h["d0"]))
		for k in verts.size():
			dv.append(verts[k])
			dp.append(base[k] + off)
	if not grab.is_empty() and grab.has("target"):
		var gv: PackedInt32Array = grab["verts"]
		var offs: PackedVector3Array = grab["offs"]
		for k in gv.size():
			dv.append(gv[k])
			dp.append((grab["target"] as Vector3) + offs[k])
	if dv.size() > 0:
		ops.append(["drive_vertices", [dv, dp, 0.0]])
	job["live_ops"] = ops
	ops = []
	return job


# Worker side: the queued edits, before the step (the only place live drape calls the simulator).
static func run_ops(sim, live_ops: Array) -> void:
	for op in live_ops:
		var args: Array = op[1]
		if String(op[0]) == "drive_vertices":
			sim.drive_vertices(args[0], args[1], 0.0)
		else:
			sim.callv(String(op[0]), args)


func collect(res: Dictionary) -> void:
	latest = res["frame"]
	frames_run += 1
	ed._solver_stats = res["stats"]
	ed._vcount = res["vcount"]
	ed.step_ms = float(latest["ms"])
	if frames_run % 30 == 0:
		ed._render_stats(float(res["total_ms"]), latest)


func show() -> void:
	if latest.is_empty() or _shown == frames_run:
		return
	_shown = frames_run
	ed._set_cloth_mesh(latest["pos"], latest["normal"])
	ed.cur_cloth = latest["cloth"]
	_hide_timeline_lines()
	ed.body_node.show_pose(body)
	ed._body_key = ""
	_draw_marks()
	if not len_drag.is_empty() or not tack_a.is_empty():
		overlay.queue_redraw()


# The timeline's stitch guides, seam lines and needles belong to its own poses: hidden while live (the rebuild on leaving
# draws them again).
func _hide_timeline_lines() -> void:
	for n in [ed._stitch_mesh, ed._seam_mesh, ed._trail_mesh, ed._hl_mesh]:
		if n != null:
			(n as Node3D).visible = false
	for g in ed._needle_groups:
		(g["needle"] as Node3D).visible = false


func positions() -> PackedVector3Array:
	if not latest.is_empty():
		return latest["cloth"]
	return ed.cur_cloth


# ------------------------------------------------------------------ holds and seams
func _hold_all(shown: PackedVector3Array) -> void:
	ed._refresh_far_offsets()
	for m in ed.meta["order"]:
		_hold(String(m), shown)


func _hold(m: String, shown: PackedVector3Array) -> void:
	var r: Dictionary = ed.meta["ranges"][m]
	var f0 := int(r["first"])
	var p2: PackedVector3Array = ed.meta["p2"]
	# where a panel hangs from: its top; a sleeve (wrapped round an arm, often raised) its cap, the end nearest the body's
	# axis on the fitted pose (its top runs the whole arm and would hold it out stiff)
	var sleeve := _is_sleeve(m)
	var key := func(v: int) -> float: return -Vector2(p2[v].x, p2[v].z).length() if sleeve else shown[v].y
	var top := -1e9
	for j in int(r["count"]):
		top = maxf(top, key.call(f0 + j))
	var band := HOLD_BAND * (2.0 if sleeve else 1.0)
	var verts := PackedInt32Array()
	var base := PackedVector3Array()
	for j in int(r["count"]):
		if key.call(f0 + j) > top - band:
			verts.append(f0 + j)
			base.append(shown[f0 + j])
	var far: Vector3 = ed._far_off.get(m, Vector3(0, 0, 1))
	var dir := far.normalized() if far.length() > 1e-6 else Vector3(0, 0, 1)
	# how far the shown pose is from the fitted one, along dir (the far/close blend of the stage view)
	var dsum := 0.0
	for v in verts:
		dsum += (shown[v] - p2[v]).dot(dir)
	var d0 := maxf(0.0, dsum / maxf(1.0, float(verts.size())))
	holds[m] = {"verts": verts, "base": base, "dir": dir, "d0": d0, "d": d0, "v": 0.0, "goal": d0}
	ops.append(["pin_vertices", [verts, true]])


func _is_sleeve(m: String) -> bool:
	var sp = Table.piece_by_id(ed._sim_pieces, m)
	return sp != null and String(((sp as Dictionary).get("wrap", {}) as Dictionary).get("mode", "")) == "arm"


func _release_holds() -> void:
	for m in holds:
		ops.append(["pin_vertices", [holds[m]["verts"], false]])
	holds.clear()
	_repin()
	_sync_ui()


# Pins that share vertices with a released hold or grab are pinned again (a release unpins its vertices).
func _repin() -> void:
	for p in pins:
		ops.append(["pin_vertices", [p["verts"], true]])


func set_sewn(on: bool) -> void:
	if on == sewn and _sew_t < 0:
		return
	sewn = on
	if on:
		_sew_t = 0
	else:
		_sew_t = -1
		ops.append(["set_param", ["seam_scale", 0.0]])
		_hold_all(positions())
		for m in holds:   # held where they are now
			holds[m]["d0"] = 0.0
			holds[m]["d"] = 0.0
			holds[m]["goal"] = 0.0
	_sync_ui()


func set_distance(m: String, d: float) -> void:
	if not holds.has(m):
		return
	holds[m]["goal"] = clampf(d, 0.0, DIST_MAX)


# ------------------------------------------------------------------ picking
func _ray(local: Vector2) -> Array:
	return ed.place_view._ray(local)


# The cloth under a pane point: {pid, v (nearest vertex of the hit triangle), p (hit point)} or {}.
func pick(local: Vector2) -> Dictionary:
	if ed.meta.is_empty():
		return {}
	var ray := _ray(local)
	var o: Vector3 = ray[0]
	var d: Vector3 = ray[1]
	var pos := positions()
	var tris: PackedInt32Array = ed.meta["tris"]
	var best_t := 1e9
	var best := -1
	for k in tris.size() / 3:
		var ia := tris[3 * k]
		var ib := tris[3 * k + 1]
		var ic := tris[3 * k + 2]
		if ic >= pos.size():
			continue
		var t: float = ed.place_view._ray_tri(o, d, pos[ia], pos[ib], pos[ic])
		if t > 0.0 and t < best_t:
			best_t = t
			best = k
	if best < 0:
		return {}
	var hp := o + d * best_t
	var v := tris[3 * best]
	for c in 3:
		var w := tris[3 * best + c]
		if pos[w].distance_to(hp) < pos[v].distance_to(hp):
			v = w
	return {"pid": _pid_of(v), "v": v, "p": hp}


func _pid_of(v: int) -> String:
	for m in ed.meta["ranges"]:
		var r: Dictionary = ed.meta["ranges"][m]
		if v >= int(r["first"]) and v < int(r["first"]) + int(r["count"]):
			return String(m)
	return ""


func _verts_near(m: String, p: Vector3, radius: float) -> PackedInt32Array:
	var r: Dictionary = ed.meta["ranges"][m]
	var pos := positions()
	var out := PackedInt32Array()
	var best := -1
	var bd := 1e9
	for j in int(r["count"]):
		var v := int(r["first"]) + j
		var dd := pos[v].distance_to(p)
		if dd < radius:
			out.append(v)
		if dd < bd:
			bd = dd
			best = v
	if out.is_empty() and best >= 0:
		out.append(best)
	return out


func _edge_len() -> float:
	return float(ed.QUALITIES[ed._qual_key()]["edge"])


func _activate(m: String) -> void:
	if m == "" or m == pid:
		return
	pid = m
	ed.place_view._select(m)
	_sync_ui()


# ------------------------------------------------------------------ input
func input(e: InputEvent) -> bool:
	if not active:
		return false
	if e is InputEventMouseButton:
		var mb := e as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_WHEEL_UP or mb.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			if mb.alt_pressed and holds.has(pid):
				set_distance(pid, float(holds[pid]["goal"]) + (0.03 if mb.button_index == MOUSE_BUTTON_WHEEL_UP else -0.03))
				_sync_ui()
				return true
			return false
		if mb.button_index != MOUSE_BUTTON_LEFT:
			return false
		if mb.pressed:
			return _press(mb.position)
		return _release(mb.position)
	if e is InputEventMouseMotion:
		var mm := e as InputEventMouseMotion
		if not grab.is_empty():
			_grab_move(mm.position)
			return true
		if not len_drag.is_empty() and String(len_drag.get("src", "")) == "view":
			len_drag["mouse"] = mm.position
			overlay.queue_redraw()
			return true
	return false


func key(e: InputEventKey) -> bool:
	for t in TOOL_KEY:
		if e.keycode == int(TOOL_KEY[t]):
			set_tool(t)
			return true
	return false


func _press(local: Vector2) -> bool:
	var hit := pick(local)
	if tool == "pin" and _unpin_at(local):
		return true
	if tool == "tack" and _untack_at(local):
		return true
	if hit.is_empty():
		return false
	var m := String(hit["pid"])
	if tool == "link":
		if pid != "" and m != pid:
			_toggle_link(pid, m)
			return true
		_activate(m)
		return true
	_activate(m)
	match tool:
		"grab":
			_grab_start(hit, local)
		"pin":
			_pin_at(hit)
		"tack":
			_tack_click(hit)
		"length":
			_len_press(m, int(hit["v"]), hit["p"], local, "view")
	return true


func _release(_local: Vector2) -> bool:
	if not grab.is_empty():
		ops.append(["pin_vertices", [grab["verts"], false]])
		_repin()
		grab = {}
		return true
	if not len_drag.is_empty() and String(len_drag.get("src", "")) == "view":
		_len_commit()
		return true
	return false


# ------------------------------------------------------------------ Grab
func _grab_start(hit: Dictionary, local: Vector2) -> void:
	var p: Vector3 = hit["p"]
	var verts := _verts_near(String(hit["pid"]), p, maxf(GRAB_R, 0.6 * _edge_len()))
	var pos := positions()
	var offs := PackedVector3Array()
	for v in verts:
		offs.append(pos[v] - p)
	var n: Vector3 = -ed.cam.global_transform.basis.z
	grab = {"verts": verts, "offs": offs, "plane_p": p, "plane_n": n, "target": p, "local": local}
	ops.append(["pin_vertices", [verts, true]])
	ed.user_cam_until = Time.get_ticks_msec() + 3000


func _grab_move(local: Vector2) -> void:
	var ray := _ray(local)
	var o: Vector3 = ray[0]
	var d: Vector3 = ray[1]
	var n: Vector3 = grab["plane_n"]
	var den := d.dot(n)
	if absf(den) < 1e-6:
		return
	var t := ((grab["plane_p"] as Vector3) - o).dot(n) / den
	if t > 0.0:
		grab["target"] = o + d * t
	grab["local"] = local


# ------------------------------------------------------------------ Pin
func _pin_at(hit: Dictionary) -> void:
	var verts := _verts_near(String(hit["pid"]), hit["p"], maxf(PIN_R, 0.35 * _edge_len()))
	pins.append({"verts": verts})
	ops.append(["pin_vertices", [verts, true]])
	ed.notify("Pinned: click the pin again (Pin tool) to take it out")


func _unpin_at(local: Vector2) -> bool:
	var pos := positions()
	for i in pins.size():
		var verts: PackedInt32Array = pins[i]["verts"]
		if verts.is_empty():
			continue
		if ed.place_view._to_pane(pos[verts[0]]).distance_to(local) < 10.0:
			ops.append(["pin_vertices", [verts, false]])
			pins.remove_at(i)
			_repin()
			return true
	return false


# ------------------------------------------------------------------ Tack
func _tack_click(hit: Dictionary) -> void:
	var v := int(hit["v"])
	var m := String(hit["pid"])
	if tack_a.is_empty():
		tack_a = {"v": v, "pid": m}
		ed.notify("Tack: now click the point to sew it to")
		return
	var a := int(tack_a["v"])
	tack_a = {}
	if a == v:
		return
	var ma := _pid_of(a)
	if Outfit.uid_of(ma) != Outfit.uid_of(m):
		ed.notify("Tack: both points must be on the same garment")
		return
	var t := {"a": a, "b": v, "pa": [ma, _uv(a)], "pb": [m, _uv(v)]}
	tacks.append(t)
	ops.append(["add_point_seam", [a, v, TACK_SHARE]])
	_save_tack(t, true)


func _untack_at(local: Vector2) -> bool:
	var pos := positions()
	for i in tacks.size():
		var t: Dictionary = tacks[i]
		for end in ["a", "b"]:
			if ed.place_view._to_pane(pos[int(t[end])]).distance_to(local) < 9.0:
				ops.append(["remove_point_seam", [int(t["a"]), int(t["b"])]])
				tacks.remove_at(i)
				_save_tack(t, false)
				return true
	return false


# The pattern point of vertex v (its rest point now), in its piece's coordinates.
func _uv(v: int) -> Vector2:
	var m := _pid_of(v)
	return (rest[m] as PackedVector2Array)[v - int(ed.meta["ranges"][m]["first"])]


# Tacks are stored on the first piece: p["tacks"] = [[x, y, other piece id, ox, oy]] (plan_loader.gd sews them).
func _save_tack(t: Dictionary, add: bool) -> void:
	var ma := String(t["pa"][0])
	var mb := String(t["pb"][0])
	var pa = _table_piece(ma)
	if pa == null:
		return
	var a2: Vector2 = t["pa"][1]
	var b2: Vector2 = t["pb"][1]
	var entry := [snappedf(a2.x, 1e-4), snappedf(a2.y, 1e-4), _table_id(mb), snappedf(b2.x, 1e-4), snappedf(b2.y, 1e-4)]
	ed.push_undo("Tack" if add else "Unpick tack")
	var list: Array = (pa as Dictionary).get("tacks", [])
	if add:
		list.append(entry)
	else:
		var keep := []
		for q in list:
			if not (absf(float(q[0]) - entry[0]) < 2e-3 and absf(float(q[1]) - entry[1]) < 2e-3 and String(q[2]) == entry[2]):
				keep.append(q)
		list = keep
	if list.is_empty():
		(pa as Dictionary).erase("tacks")
	else:
		pa["tacks"] = list
	_pattern_edited()


# The tacks the simulator was built with (plan_loader.gd sews them from p["tacks"]), so they can be shown and unpicked.
func _load_tacks() -> void:
	tacks.clear()
	for pair in ed.meta.get("tacks", []):
		var a := int(pair[0])
		var b := int(pair[1])
		tacks.append({"a": a, "b": b, "pa": [_pid_of(a), _uv(a)], "pb": [_pid_of(b), _uv(b)]})


# ------------------------------------------------------------------ Length
# World height over a panel's pattern coordinates, least squares on its start pose: y = c + g . uv. The length axis a
# (pointing to the hem) is -g / |g|.
func fit(m: String) -> Dictionary:
	if _fit.has(m):
		return _fit[m]
	var uv: PackedVector2Array = ed.meta["uv"][m]
	var r: Dictionary = ed.meta["ranges"][m]
	var p2: PackedVector3Array = ed.meta["p2"]
	var f0 := int(r["first"])
	var n := uv.size()
	var cu := Vector2.ZERO
	var cy := 0.0
	for j in n:
		cu += uv[j]
		cy += p2[f0 + j].y
	cu /= maxf(1.0, float(n))
	cy /= maxf(1.0, float(n))
	var sxx := 0.0
	var sxy := 0.0
	var syy := 0.0
	var bx := 0.0
	var by := 0.0
	for j in n:
		var du := uv[j] - cu
		var dy := p2[f0 + j].y - cy
		sxx += du.x * du.x
		sxy += du.x * du.y
		syy += du.y * du.y
		bx += du.x * dy
		by += du.y * dy
	var det := sxx * syy - sxy * sxy
	var g := Vector2(0.0, 1.0)
	if absf(det) > 1e-12:
		g = Vector2((syy * bx - sxy * by) / det, (sxx * by - sxy * bx) / det)
	if g.length() < 1e-6:
		g = Vector2(0.0, 1.0)
	var out := {"g": g, "c": cy - g.dot(cu), "a": -g.normalized(), "centre": cu}
	_fit[m] = out
	return out


# The linked group of a panel (merged ids), the panel first.
func group_of(m: String) -> Array:
	var out := [m]
	var tp = _table_piece(m)
	if tp == null or String((tp as Dictionary).get("length_group", "")) == "":
		return out
	var gname := String(tp["length_group"])
	var pre := Outfit.prefix(Outfit.uid_of(m))
	for q in ed.pieces:
		if String((q as Dictionary).get("length_group", "")) == gname and pre + String(q["id"]) != m:
			var qm: String = pre + String(q["id"])
			if (ed.meta["order"] as Array).has(qm):
				out.append(qm)
	return out


func _len_press(m: String, v: int, _p: Vector3, local: Vector2, src: String) -> void:
	var f := fit(m)
	var uv0: Vector2 = (ed.meta["uv"][m] as PackedVector2Array)[v - int(ed.meta["ranges"][m]["first"])]
	var h: float = float(f["c"]) + (f["g"] as Vector2).dot(uv0)   # the press's height on the start pose
	var parts := []
	for q in group_of(m):
		var line := _line_in(q, h)
		var base: PackedVector2Array = (rest[q] as PackedVector2Array).duplicate()
		var a: Vector2 = fit(q)["a"]
		var below := 0.0
		for u in base:
			below = maxf(below, (u - line).dot(a))
		if below < 0.01:
			continue
		parts.append({"pid": q, "line": line, "a": a, "base": base, "below": below})
	if parts.is_empty():
		ed.notify("Length: press higher on the panel (there is no cloth below this point)")
		return
	# the rod's bending direction on screen: the panel's length axis at the press, projected
	var down: Vector3 = Vector3.DOWN
	var p0: Vector3 = positions()[v]
	var s0: Vector2 = ed.place_view._to_pane(p0)
	var s1: Vector2 = ed.place_view._to_pane(p0 + down * 0.1)
	var sdir := (s1 - s0).normalized() if s1.distance_to(s0) > 1.0 else Vector2(0, 1)
	if src == "mini":
		sdir = Vector2(0, 1)
	len_drag = {"src": src, "parts": parts, "anchor": local, "mouse": local, "sdir": sdir, "d": 0.0, "sent": 0.0, "frames": 0}
	ed.user_cam_until = Time.get_ticks_msec() + 3000


# The line point (pattern coordinates, rest now) of panel q at start-pose height h.
func _line_in(q: String, h: float) -> Vector2:
	var f := fit(q)
	var g: Vector2 = f["g"]
	var a: Vector2 = f["a"]
	var c: Vector2 = f["centre"]
	var t := (float(f["c"]) + g.dot(c) - h) / maxf(1e-6, g.length())
	var p0 := c + a * t     # in the build's pattern coordinates
	# into the rest now: the nearest vertex's displacement since the build
	var uv: PackedVector2Array = ed.meta["uv"][q]
	var now: PackedVector2Array = rest[q]
	var best := 0
	for j in uv.size():
		if uv[j].distance_squared_to(p0) < uv[best].distance_squared_to(p0):
			best = j
	return p0 + (now[best] - uv[best])


# The rod: pointer travel along the length axis bends it (saturating); the bend sets the rate.
func rod_bend() -> float:
	if len_drag.is_empty():
		return 0.0
	var along: float = ((len_drag["mouse"] as Vector2) - (len_drag["anchor"] as Vector2)).dot(len_drag["sdir"])
	return tanh(along / ROD_PX)


func _len_tick(delta: float) -> void:
	var b := rod_bend()
	var rate := signf(b) * LEN_RATE * b * b
	var d := float(len_drag["d"]) + rate * delta
	# clamp so that every part keeps k in [K_MIN, K_MAX]
	for part in len_drag["parts"]:
		var below := float(part["below"])
		d = clampf(d, (K_MIN - 1.0) * below, (K_MAX - 1.0) * below)
	len_drag["d"] = d
	len_drag["frames"] = int(len_drag["frames"]) + 1
	if absf(d - float(len_drag["sent"])) > 0.002 and int(len_drag["frames"]) % 2 == 0:
		_send_rest(d)
	overlay.queue_redraw()
	mini.queue_redraw()


static func stretch(u: Vector2, line: Vector2, a: Vector2, k: float) -> Vector2:
	var s := (u - line).dot(a)
	return u + a * s * (k - 1.0) if s > 0.0 else u


func _send_rest(d: float) -> void:
	len_drag["sent"] = d
	for part in len_drag["parts"]:
		var k := (float(part["below"]) + d) / float(part["below"])
		var base: PackedVector2Array = part["base"]
		var out := PackedVector2Array()
		out.resize(base.size())
		for j in base.size():
			out[j] = stretch(base[j], part["line"], part["a"], k)
		rest[String(part["pid"])] = out
		ops.append(["set_panel_rest_2d", [(ed.meta["order"] as Array).find(part["pid"]), out]])


# The release: the change goes into the pattern (one undoable action) without a rebuild; the simulator has it already.
func _len_commit() -> void:
	var d := float(len_drag["d"])
	var parts: Array = len_drag["parts"]
	len_drag = {}
	overlay.queue_redraw()
	if absf(d) < 0.002:
		for part in parts:   # too small to keep: back to where it was
			rest[String(part["pid"])] = part["base"]
			ops.append(["set_panel_rest_2d", [(ed.meta["order"] as Array).find(part["pid"]), part["base"]]])
		return
	len_drag = {"parts": parts}   # _send_rest reads the parts
	_send_rest(d)                 # the last value exactly
	len_drag = {}
	var names := []
	for part in parts:
		var tp = _table_piece(String(part["pid"]))
		if tp != null:
			names.append(String(tp.get("name", tp["id"])))
	ed.push_undo("%s %s %.1f cm" % ["Lengthen" if d > 0.0 else "Shorten", ", ".join(names), absf(d) * 100.0])
	for part in parts:
		var tp = _table_piece(String(part["pid"]))
		if tp == null:
			continue
		var k := (float(part["below"]) + d) / float(part["below"])
		apply_length(tp, part["line"], part["a"], k, ed)
	_pattern_edited()
	ed.notify("%s by %.1f cm%s" % ["Lengthened" if d > 0.0 else "Shortened", absf(d) * 100.0,
		(" (%d linked panels)" % parts.size()) if parts.size() > 1 else ""])


# Pattern makers' lengthen/shorten line on a cutting-table piece: corners where the outline crosses the line, then every
# point below the line (along a, towards the hem) moved by s (k - 1). Seams on a split edge are kept (Table.insert_points).
static func apply_length(p: Dictionary, line: Vector2, a: Vector2, k: float, edref) -> void:
	var ol: Array = p["outline"]
	var cuts := []   # [edge, point]
	for e in ol.size():
		var pa := Vector2(float(ol[e][0]), float(ol[e][1]))
		var pb := Vector2(float(ol[(e + 1) % ol.size()][0]), float(ol[(e + 1) % ol.size()][1]))
		var sa := (pa - line).dot(a)
		var sb := (pb - line).dot(a)
		if (sa < -1e-3 and sb > 1e-3) or (sa > 1e-3 and sb < -1e-3):
			var t := sa / (sa - sb)
			var q := pa.lerp(pb, t)
			cuts.append([e, [q.x, q.y]])
	cuts.reverse()   # from the last edge, so earlier edge numbers stay valid
	for c in cuts:
		Table.insert_points(edref, p, int(c[0]), [c[1]])
	var o := Geom.offset_of(p)
	Geom._map_piece(p, func(w: Vector2) -> Vector2: return stretch(w - o, line, a, k) + o)


# ------------------------------------------------------------------ Link
func _toggle_link(m: String, other: String) -> void:
	if Outfit.uid_of(m) != Outfit.uid_of(other):
		ed.notify("Link: both panels must be in the same garment")
		return
	var pa = _table_piece(m)
	var pb = _table_piece(other)
	if pa == null or pb == null:
		return
	var ga := String(pa.get("length_group", ""))
	var gb := String(pb.get("length_group", ""))
	if ga != "" and ga == gb:
		ed.push_undo("Unlink length")
		pb.erase("length_group")
		# a group of one is no group
		var left := 0
		for q in ed.pieces:
			if String((q as Dictionary).get("length_group", "")) == ga:
				left += 1
		if left < 2:
			pa.erase("length_group")
		ed.notify("Unlinked: %s changes length on its own" % String(pb.get("name", pb["id"])))
	else:
		ed.push_undo("Link lengths")
		var g := ga if ga != "" else (gb if gb != "" else "len_" + String(pa["id"]))
		if gb != "" and gb != g:   # merge b's group into a's
			for q in ed.pieces:
				if String((q as Dictionary).get("length_group", "")) == gb:
					q["length_group"] = g
		pa["length_group"] = g
		pb["length_group"] = g
		ed.notify("Linked: %s and %s change length together" % [String(pa.get("name", pa["id"])), String(pb.get("name", pb["id"]))])
	_pattern_edited()
	mini.queue_redraw()
	_draw_marks()


# ------------------------------------------------------------------ pattern access
func _table_id(m: String) -> String:
	return m.substr(Outfit.prefix(Outfit.uid_of(m)).length())


# The active garment's cutting-table piece of a merged id (place_view._select made its garment active), or null.
func _table_piece(m: String):
	var uid := Outfit.uid_of(m)
	if ed.active < 0 or ed.active >= ed.outfit.size() or int(ed.outfit[ed.active]["uid"]) != uid:
		return null
	return Table.piece_by_id(ed.pieces, _table_id(m))


func _pattern_edited() -> void:
	changed_pattern = true
	ed.changed("live")


# ------------------------------------------------------------------ drawing
func _draw_marks() -> void:
	_im.clear_surfaces()
	var pos := positions()
	if pos.is_empty():
		return
	_im.surface_begin(Mesh.PRIMITIVE_LINES)
	var s := 0.012
	var any := false
	for p in pins:
		for v in p["verts"]:
			var c: Vector3 = pos[v]
			for ax in [Vector3.RIGHT, Vector3.UP, Vector3.BACK]:
				_im.surface_set_color(Color(1.0, 0.25, 0.3))
				_im.surface_add_vertex(c - ax * s)
				_im.surface_set_color(Color(1.0, 0.25, 0.3))
				_im.surface_add_vertex(c + ax * s)
				any = true
	for t in tacks:
		_im.surface_set_color(Color(1.0, 0.85, 0.2))
		_im.surface_add_vertex(pos[int(t["a"])])
		_im.surface_set_color(Color(1.0, 0.85, 0.2))
		_im.surface_add_vertex(pos[int(t["b"])])
		any = true
	if not tack_a.is_empty():
		var c2: Vector3 = pos[int(tack_a["v"])]
		for ax in [Vector3.RIGHT, Vector3.UP, Vector3.BACK]:
			_im.surface_set_color(Color(1.0, 0.85, 0.2))
			_im.surface_add_vertex(c2 - ax * s)
			_im.surface_set_color(Color(1.0, 0.85, 0.2))
			_im.surface_add_vertex(c2 + ax * s)
		any = true
	if not grab.is_empty() and grab.has("target"):
		for v in grab["verts"]:
			_im.surface_set_color(Color(1, 1, 1, 0.8))
			_im.surface_add_vertex(pos[v])
			_im.surface_set_color(Color(1, 1, 1, 0.8))
			_im.surface_add_vertex(grab["target"])
			any = true
	for m in holds:
		var h: Dictionary = holds[m]
		var off: Vector3 = (h["dir"] as Vector3) * (float(h["d"]) - float(h["d0"]))
		var base: PackedVector3Array = h["base"]
		for k in base.size():   # a thin hanger line above each held vertex
			_im.surface_set_color(Color(0.7, 0.8, 1.0, 0.6))
			_im.surface_add_vertex(base[k] + off)
			_im.surface_set_color(Color(0.7, 0.8, 1.0, 0.0))
			_im.surface_add_vertex(base[k] + off + Vector3(0, 0.05, 0))
			any = true
	if not any:   # an empty surface is not allowed
		_im.surface_add_vertex(Vector3.ZERO)
		_im.surface_add_vertex(Vector3.ZERO)
	_im.surface_end()


func _draw_overlay() -> void:
	if not active or len_drag.is_empty() or String(len_drag["src"]) != "view":
		return
	_draw_rod(overlay, len_drag["anchor"], len_drag["sdir"])


# The rod: fixed at the press, lying across the length axis, its tip bent along the axis by the pull (a cantilever curve),
# and the change so far next to it.
func _draw_rod(cv: CanvasItem, anchor: Vector2, sdir: Vector2) -> void:
	var b := rod_bend()
	var across := Vector2(-sdir.y, sdir.x)
	var L := 60.0
	var pts := PackedVector2Array()
	for i in 17:
		var x := float(i) / 16.0
		var y := b * 0.5 * L * (3.0 * x * x - x * x * x) / 2.0
		pts.append(anchor + across * (x * L) + sdir * y)
	var col := Color(0.55, 0.9, 0.55) if b >= 0.0 else Color(1.0, 0.6, 0.35)
	cv.draw_polyline(pts, Color(0, 0, 0, 0.5), 6.0, true)
	cv.draw_polyline(pts, col, 3.0, true)
	cv.draw_circle(anchor, 5.0, Color(1, 1, 1))
	cv.draw_line(anchor - across * 8.0, anchor + across * 8.0, Color(0, 0, 0, 0.8), 3.0)
	var d := float(len_drag["d"])
	var font := ThemeDB.fallback_font
	var txt := "%+.1f cm" % (d * 100.0)
	var tp := anchor + across * (L + 10.0) + sdir * (b * 0.5 * L)
	cv.draw_string(font, tp + Vector2(1, 1), txt, HORIZONTAL_ALIGNMENT_LEFT, -1, 14, Color(0, 0, 0, 0.7))
	cv.draw_string(font, tp, txt, HORIZONTAL_ALIGNMENT_LEFT, -1, 14, col)


# ------------------------------------------------------------------ mini pattern
var _mini_xf := {}   # {s, o, line?}: pattern -> mini pixels of the last draw


func _draw_mini() -> void:
	if pid == "":
		return
	var tp = _table_piece(pid)
	if tp == null:
		return
	var sz := mini.size
	mini.draw_rect(Rect2(Vector2.ZERO, sz), Color(0.08, 0.09, 0.11, 0.82))
	mini.draw_rect(Rect2(Vector2.ZERO, sz), Color(1, 1, 1, 0.15), false, 1.0)
	var ol: Array = tp["outline"]
	var pts := []
	for q in ol:
		pts.append(Vector2(float(q[0]), float(q[1])))
	# the live change while dragging, on this panel's part
	var part := {}
	if not len_drag.is_empty():
		for pt in len_drag["parts"]:
			if String(pt["pid"]) == pid:
				part = pt
	var k := 1.0
	if not part.is_empty():
		k = (float(part["below"]) + float(len_drag["d"])) / float(part["below"])
	var shown := []
	for p in pts:
		shown.append(stretch(p, part["line"], part["a"], k) if not part.is_empty() else p)
	# fit into the box with the length axis pointing down the screen: rotate pattern so a -> +y
	var a: Vector2 = fit(pid)["a"]
	var rot := Vector2(0, 1).angle() - a.angle()
	var mn := Vector2(1e9, 1e9)
	var mx := Vector2(-1e9, -1e9)
	var rp := []
	for p in shown:
		var r: Vector2 = (p as Vector2).rotated(rot)
		rp.append(r)
		mn = mn.min(r)
		mx = mx.max(r)
	var box := Rect2(Vector2(14, 26), sz - Vector2(28, 52))
	var ext := (mx - mn)
	var s := minf(box.size.x / maxf(1e-3, ext.x), box.size.y / maxf(1e-3, ext.y))
	var o := box.position + (box.size - ext * s) * 0.5 - mn * s
	_mini_xf = {"s": s, "o": o, "rot": rot}
	var poly := PackedVector2Array()
	for r in rp:
		poly.append(o + (r as Vector2) * s)
	if poly.size() >= 3:
		var col := Color(0.55, 0.75, 1.0, 0.35)
		if Geometry2D.triangulate_polygon(poly).size() > 0:
			mini.draw_colored_polygon(poly, col)
		var closed := poly.duplicate()
		closed.append(poly[0])
		mini.draw_polyline(closed, Color(0.75, 0.88, 1.0), 1.5, true)
	if not part.is_empty():
		var lp: Vector2 = o + (part["line"] as Vector2).rotated(rot) * s
		mini.draw_line(Vector2(box.position.x - 6, lp.y), Vector2(box.end.x + 6, lp.y), Color(1.0, 0.85, 0.3), 1.5)
	var font := ThemeDB.fallback_font
	var name := String(tp.get("name", tp["id"]))
	var grp := String(tp.get("length_group", ""))
	mini.draw_string(font, Vector2(8, 16), name + ("  (linked)" if grp != "" else ""), HORIZONTAL_ALIGNMENT_LEFT, sz.x - 16, 12, Color(1, 1, 1, 0.85))
	var len_cm := ext.y * 100.0
	var txt := "%.1f cm long" % len_cm
	if not len_drag.is_empty():
		txt += "  %+.1f" % (float(len_drag["d"]) * 100.0)
	mini.draw_string(font, Vector2(8, sz.y - 8), txt, HORIZONTAL_ALIGNMENT_LEFT, sz.x - 16, 12, Color(1, 1, 1, 0.7))
	if not len_drag.is_empty() and String(len_drag["src"]) == "mini":
		_draw_rod(mini, len_drag["anchor"], len_drag["sdir"])


func _mini_input(e: InputEvent) -> void:
	if not active or pid == "" or _mini_xf.is_empty():
		return
	if e is InputEventMouseButton and (e as InputEventMouseButton).button_index == MOUSE_BUTTON_LEFT:
		var mb := e as InputEventMouseButton
		if mb.pressed:
			# the pattern point under the press -> the nearest vertex of the panel
			var pp: Vector2 = ((mb.position - (_mini_xf["o"] as Vector2)) / float(_mini_xf["s"])).rotated(-float(_mini_xf["rot"]))
			var now: PackedVector2Array = rest[pid]
			var best := 0
			for j in now.size():
				if now[j].distance_squared_to(pp) < now[best].distance_squared_to(pp):
					best = j
			var v := int(ed.meta["ranges"][pid]["first"]) + best
			_len_press(pid, v, positions()[v], mb.position, "mini")
		elif not len_drag.is_empty() and String(len_drag["src"]) == "mini":
			_len_commit()
		mini.accept_event()
	elif e is InputEventMouseMotion and not len_drag.is_empty() and String(len_drag["src"]) == "mini":
		len_drag["mouse"] = (e as InputEventMouseMotion).position
		mini.queue_redraw()
		mini.accept_event()
