# Grain demo: why a flared skirt is cut from 3 panels rather than 2.
#
# Two half-circle skirts with the same waist, length, fabric and dress form, side by side:
#   left:  2 panels (front + back). Each panel is a 90 deg sector, so the cloth at the side seams hangs on TRUE BIAS
#          (45 deg off grain).
#   right: 3 panels (front + two back gores, centre-back seam). Each panel is a 60 deg sector, so no part of the
#          skirt hangs more than 30 deg off grain. Use the "Right skirt" menu for 4 or 6 gores.
# Every panel is cut with the warp down its centre line. The dress form is rotationally symmetric, so the
# only thing that differs around the body is the grain direction of the hanging cloth.
#
# What to look at: the hem graph at the bottom (hem height around the body, seams marked). Bias stretches under
# the skirt's own weight (shear is ~100x softer than warp/weft in the presets), so the 2-panel hem drops at its
# two side seams; the 3-panel hem drops less, at more places, and stays more level.
#
# Controls (toolbar): fabric, shear softness, right-skirt panel count, isotropic control, colour (plain / grain /
# live stretch), restart, pause. Keys: R restart, Space pause, C colour mode, I isotropic control. Mouse: left-drag orbit, right-drag pan, wheel zoom.
#
# Honest limits: the solver is linear-elastic per direction. Real bias drop is mostly slow creep over hours or days
# of hanging (why dressmakers let a bias skirt hang overnight before hemming), which this model does not have.
# "Shear softness" divides the fabric's shear stiffness to stand in for that relaxed state; x1 is the preset as-is.
# "Isotropic control" gives the fabric the shear stiffness an isotropic sheet would have (G = E / 2(1+nu)), which
# should make both hems level: if it does, the difference you see is the grain, not the panel geometry.
extends Node3D

const WAIST_C := 0.70          # waist circumference, m
const LENGTH := 0.55           # skirt length (waist to hem along the fall line), m
const FLARE := PI              # total flare angle of the flat pattern: PI = half-circle skirt
const WAIST_Y := 1.00          # waist height on the dress form, m
const EDGE := 0.03             # mesh edge length, m
const GAP := 0.95              # distance between the two skirts, m
const HIP_RADII := Vector3(0.155, 0.24, 0.155)   # dress form hips (rotationally symmetric)
const HIP_DROP := 0.19         # hip centre below the waist
const LEG_RADIUS := 0.125      # one cylinder below the hips (keeps the skirt from wrapping under)
const COL_LEFT := Color(0.93, 0.45, 0.25)
const COL_RIGHT := Color(0.25, 0.55, 0.95)
const SOFTNESS := [1.0, 4.0, 16.0, 64.0]
const RIGHT_PANELS := [3, 4, 6]
const NU := 0.3

var skirts: Array = []         # [{sim, n, x, mi, uvs, hem, grain_bias, col}]
var fabric_ids := PackedStringArray()
var fabric_index := 0
var softness_index := 2
var right_index := 0
var isotropic := false
var colour_mode := 1           # 0 plain, 1 grain (static), 2 stretch (live)
var paused := false
var _task := -1
var _frame := 0
var _ms := 0.0
var _t0 := 0

# camera
var cam: Camera3D
var yaw := 0.0
var pitch := -0.12
var dist := 3.1
var target := Vector3(0, 0.56, 0)

# UI
var info: Label
var graph: Control
var fabric_menu: OptionButton
var soft_menu: OptionButton
var right_menu: OptionButton
var iso_box: CheckBox
var colour_menu: OptionButton
var pause_btn: Button


func _ready() -> void:
	# headless checks: FL_SOFT=<menu index> FL_ISO=1 FL_RIGHT=<menu index> FL_FABRIC=<id> FL_FRAMES=<quit after n sim frames>
	softness_index = int(OS.get_environment("FL_SOFT")) if OS.has_environment("FL_SOFT") else softness_index
	right_index = int(OS.get_environment("FL_RIGHT")) if OS.has_environment("FL_RIGHT") else right_index
	isotropic = OS.has_environment("FL_ISO")
	colour_mode = int(OS.get_environment("FL_COLOUR")) if OS.has_environment("FL_COLOUR") else colour_mode
	_build_world()
	_build_ui()
	_restart()


# ---------------------------------------------------------------- scene
func _build_world() -> void:
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.11, 0.115, 0.13)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.55, 0.57, 0.62)
	e.ambient_light_energy = 0.45
	e.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.environment = e
	add_child(env)
	var key := DirectionalLight3D.new()
	key.rotation_degrees = Vector3(-38, 28, 0)
	key.light_energy = 1.1
	key.shadow_enabled = true
	add_child(key)
	var rim := DirectionalLight3D.new()
	rim.rotation_degrees = Vector3(-20, 200, 0)
	rim.light_energy = 0.45
	add_child(rim)
	cam = Camera3D.new()
	cam.fov = 35
	add_child(cam)
	_update_camera()
	var floor_mi := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(6, 4)
	floor_mi.mesh = pm
	var fm := StandardMaterial3D.new()
	fm.albedo_color = Color(0.2, 0.205, 0.22)
	fm.roughness = 0.9
	floor_mi.material_override = fm
	add_child(floor_mi)
	for side in [-1, 1]:
		_add_form(Vector3(side * GAP * 0.5, 0, 0))


func _add_form(at: Vector3) -> void:
	var m := StandardMaterial3D.new()
	m.albedo_color = Color(0.5, 0.47, 0.44)
	m.roughness = 0.75
	var hip := MeshInstance3D.new()
	var s := SphereMesh.new()
	s.radius = 1.0
	s.height = 2.0
	hip.mesh = s
	hip.scale = HIP_RADII * 0.99
	hip.position = at + Vector3(0, WAIST_Y - HIP_DROP, 0)
	hip.material_override = m
	add_child(hip)
	var waist := MeshInstance3D.new()             # torso stub above the waist (display only)
	var c := CylinderMesh.new()
	c.top_radius = 0.10
	c.bottom_radius = WAIST_C / TAU - 0.004
	c.height = 0.30
	waist.mesh = c
	waist.position = at + Vector3(0, WAIST_Y + 0.15, 0)
	waist.material_override = m
	add_child(waist)
	var leg := MeshInstance3D.new()               # stand
	var lc := CylinderMesh.new()
	lc.top_radius = 0.02
	lc.bottom_radius = 0.02
	lc.height = WAIST_Y - HIP_DROP
	leg.mesh = lc
	leg.position = at + Vector3(0, (WAIST_Y - HIP_DROP) * 0.5, 0)
	leg.material_override = m
	add_child(leg)


# ---------------------------------------------------------------- UI
func _build_ui() -> void:
	var cl := CanvasLayer.new()
	add_child(cl)
	var root := VBoxContainer.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	cl.add_child(root)
	var bar := HBoxContainer.new()
	bar.add_theme_constant_override("separation", 6)
	root.add_child(bar)
	fabric_menu = OptionButton.new()
	bar.add_child(_lbl("Fabric"))
	bar.add_child(fabric_menu)
	fabric_menu.item_selected.connect(func(i): fabric_index = i; _restart())
	bar.add_child(_lbl("Shear softness"))
	soft_menu = OptionButton.new()
	for f in SOFTNESS:
		soft_menu.add_item("x%d" % int(f) if f > 1.0 else "x1 preset")
	soft_menu.selected = softness_index
	soft_menu.item_selected.connect(func(i): softness_index = i; _restart())
	bar.add_child(soft_menu)
	bar.add_child(_lbl("Right skirt"))
	right_menu = OptionButton.new()
	for n in RIGHT_PANELS:
		right_menu.add_item("%d panels" % n)
	right_menu.item_selected.connect(func(i): right_index = i; _restart())
	right_menu.selected = right_index
	bar.add_child(right_menu)
	iso_box = CheckBox.new()
	iso_box.text = "Isotropic"
	iso_box.set_pressed_no_signal(isotropic)
	iso_box.toggled.connect(func(on): isotropic = on; _restart())
	bar.add_child(iso_box)
	bar.add_child(_lbl("Colour"))
	colour_menu = OptionButton.new()
	for t in ["Plain", "Grain", "Stretch"]:
		colour_menu.add_item(t)
	colour_menu.selected = colour_mode
	colour_menu.item_selected.connect(func(i): colour_mode = i; _recolour())
	bar.add_child(colour_menu)
	var rb := Button.new()
	rb.text = "Restart"
	rb.pressed.connect(_restart)
	bar.add_child(rb)
	pause_btn = Button.new()
	pause_btn.text = "Pause"
	pause_btn.pressed.connect(func(): paused = not paused; pause_btn.text = "Run" if paused else "Pause")
	bar.add_child(pause_btn)
	info = Label.new()
	info.add_theme_font_size_override("font_size", 13)
	info.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(info)
	var spacer := Control.new()
	spacer.size_flags_vertical = Control.SIZE_EXPAND_FILL
	spacer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(spacer)
	graph = Control.new()
	graph.custom_minimum_size = Vector2(0, 200)
	graph.mouse_filter = Control.MOUSE_FILTER_IGNORE
	graph.draw.connect(_draw_graph)
	root.add_child(graph)


func _lbl(t: String) -> Label:
	var l := Label.new()
	l.text = t
	return l


# ---------------------------------------------------------------- simulation set-up
func _restart() -> void:
	if _task >= 0:
		WorkerThreadPool.wait_for_task_completion(_task)
		_task = -1
	for s in skirts:
		(s["mi"] as MeshInstance3D).queue_free()
	skirts.clear()
	_frame = 0
	_t0 = Time.get_ticks_msec()
	var probe := FailoringSim.new()
	if not probe.load_materials("res://materials/fabrics.json"):
		info.text = "Cannot load res://materials/fabrics.json"
		return
	if fabric_ids.is_empty():
		fabric_ids = probe.get_material_ids()
		var pick := fabric_ids.find(OS.get_environment("FL_FABRIC") if OS.has_environment("FL_FABRIC") else "silk_charmeuse")
		fabric_index = pick if pick >= 0 else 0
		for id in fabric_ids:
			fabric_menu.add_item(String(probe.get_material(id).get("name", id)))
		fabric_menu.selected = fabric_index
	skirts.append(_make_skirt(2, -GAP * 0.5, COL_LEFT))
	skirts.append(_make_skirt(RIGHT_PANELS[right_index], GAP * 0.5, COL_RIGHT))
	_recolour()
	graph.queue_redraw()


func _material_props(sim: FailoringSim) -> Dictionary:
	var base: Dictionary = sim.get_material(fabric_ids[fabric_index])
	var d := base.duplicate()
	d.erase("id")
	var g := float(base["shear_N_m_rad"]) / float(SOFTNESS[softness_index])
	if isotropic:
		g = sqrt(float(base["tensile_warp_N_m"]) * float(base["tensile_weft_N_m"])) / (2.0 * (1.0 + NU))
	d["shear_N_m_rad"] = g
	return d


func _make_skirt(n: int, x: float, col: Color) -> Dictionary:
	var sim := FailoringSim.new()
	sim.load_materials("res://materials/fabrics.json")
	sim.add_material("demo_fabric", _material_props(sim))
	sim.set_param("substeps", 2)
	sim.set_param("seam_ramp_frames", 0)
	sim.set_param("tension_stiffness", 1)
	var hip_c := Vector3(x, WAIST_Y - HIP_DROP, 0)
	sim.add_ellipsoid(hip_c, HIP_RADII)
	sim.add_cylinder(Vector3(x, (WAIST_Y - HIP_DROP) * 0.5, 0), LEG_RADIUS, (WAIST_Y - HIP_DROP) * 0.5)
	sim.add_plane(Vector3.ZERO, Vector3.UP)

	var phi := FLARE / n                    # flat sector angle of one panel
	var r_in := WAIST_C / FLARE             # pattern radius of the waist arc
	var r_out := r_in + LENGTH
	var k_out := maxi(2, int(ceil(r_out * phi / EDGE)))
	var k_in := maxi(2, int(ceil(r_in * phi / EDGE)))
	# outline (counter-clockwise): outer arc left->right, right side up, inner arc right->left, left side down.
	# alpha is measured from the panel's centre line (the grain), the panel hangs along -y.
	var outline := PackedVector2Array()
	for i in k_out + 1:
		outline.append(_polar(r_out, -phi / 2 + phi * i / k_out))
	for i in k_in + 1:
		outline.append(_polar(r_in, phi / 2 - phi * i / k_in))
	var e_right := k_out                   # outer(+phi/2) -> inner(+phi/2)
	var e_left := k_out + k_in + 1         # inner(-phi/2) -> outer(-phi/2), closing edge
	var panels := []
	for p in n:
		panels.append(sim.add_polygon_panel(outline, EDGE, "demo_fabric", Vector3.ZERO, Vector3.RIGHT, Vector3.UP, PI / 2, "gore%d" % p))
	for p in n:
		sim.add_seam(panels[p], e_right, panels[(p + 1) % n], e_left, true)

	# start pose: the flat sectors rolled onto the cone the finished skirt makes (sin(cone half-angle) = FLARE / TAU),
	# panel p centred at body angle TAU * p / n (0 = front, +z). Waist vertices are pinned (the waistband).
	var sin_b := FLARE / TAU
	var cos_b := sqrt(1.0 - sin_b * sin_b)
	var pos := sim.get_positions()
	var uvs := PackedVector2Array()
	uvs.resize(pos.size())
	var hem := PackedInt32Array()
	var bias := PackedFloat32Array()
	bias.resize(pos.size())
	var hem_tol := r_out * cos(phi / (2.0 * k_out)) - 1e-4
	var pins := 0
	for p in n:
		var first := sim.get_first_vertex(panels[p])
		var uv := sim.get_panel_vertices_2d(panels[p])
		for i in uv.size():
			var v := first + i
			var q := uv[i]
			var rho := q.length()
			var alpha := atan2(q.x, -q.y)
			var th := TAU * p / n + alpha * TAU / FLARE
			var rad := rho * sin_b
			pos[v] = Vector3(x + rad * sin(th), WAIST_Y - (rho - r_in) * cos_b, rad * cos(th))
			uvs[v] = q
			# how much of the fall-line stretch comes from shear: sin^2(2 alpha), 0 on the straight grain, 1 on true bias
			bias[v] = pow(sin(2.0 * alpha), 2)
			if rho >= hem_tol:
				hem.append(v)
			if rho <= r_in * 1.0005 + 1e-4:
				sim.pin_vertex(v, true)
				pins += 1
	sim.set_positions(pos)

	# Per triangle: w = Dm^-1 d, so that F d = (x_b - x_a) w.x + (x_c - x_a) w.y is the deformed image of the unit
	# fall-line direction d (radial in the pattern, pointing to the hem); |F d| - 1 is the stretch along the fall line.
	var idx := sim.get_indices()
	var tw := PackedVector2Array()
	var tbin := PackedInt32Array()
	var tarea := PackedFloat32Array()
	for t in range(0, idx.size(), 3):
		var a := uvs[idx[t]]
		var e1 := uvs[idx[t + 1]] - a
		var e2 := uvs[idx[t + 2]] - a
		var det := e1.x * e2.y - e1.y * e2.x
		var cen := a + (e1 + e2) / 3.0
		var d := cen.normalized()
		tw.append(Vector2(e2.y * d.x - e2.x * d.y, -e1.y * d.x + e1.x * d.y) / det)
		tarea.append(absf(det) * 0.5)
		# body angle of the centroid: the panel is known from the vertex's 3D start position
		var q := pos[idx[t]]
		var th := rad_to_deg(atan2(q.x - x, q.z))
		tbin.append(clampi(int(floor((th + 180.0) / 5.0)), 0, 71))

	var mi := MeshInstance3D.new()
	var mat := StandardMaterial3D.new()
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	mat.vertex_color_use_as_albedo = true
	mat.roughness = 0.6
	mat.specular_mode = BaseMaterial3D.SPECULAR_SCHLICK_GGX
	mi.material_override = mat
	add_child(mi)
	var sk := {"sim": sim, "n": n, "x": x, "mi": mi, "hem": hem, "bias": bias, "col": col, "pins": pins,
		"seams": [], "idx": idx, "pos": pos, "colors": PackedColorArray(), "tw": tw, "tbin": tbin, "tarea": tarea}
	for p in n:
		sk["seams"].append(fposmod(rad_to_deg(TAU * p / n + PI / n) + 180.0, 360.0) - 180.0)
	_upload(sk)
	return sk


func _polar(rho: float, alpha: float) -> Vector2:
	return Vector2(rho * sin(alpha), -rho * cos(alpha))


func _recolour(live := false) -> void:
	for sk in skirts:
		var bias: PackedFloat32Array = sk["bias"]
		var cols := PackedColorArray()
		cols.resize(bias.size())
		var base: Color = sk["col"]
		var vs: PackedFloat32Array = sk.get("vstretch", PackedFloat32Array())
		for i in bias.size():
			match colour_mode:
				1:  # straight grain = pale, true bias = saturated warm
					cols[i] = Color(0.86, 0.86, 0.84).lerp(Color(0.95, 0.35, 0.12), bias[i])
				2:  # fall-line stretch, 0 .. 4 %
					var f := clampf(vs[i] / 0.04, 0.0, 1.0) if i < vs.size() else 0.0
					cols[i] = Color(0.86, 0.86, 0.84).lerp(Color(0.85, 0.12, 0.35), f)
				_:
					cols[i] = base
		sk["colors"] = cols
		if not live:
			_upload(sk)


# ---------------------------------------------------------------- per frame
func _process(_dt: float) -> void:
	_update_camera()
	if skirts.is_empty():
		return
	if _task >= 0:
		if not WorkerThreadPool.is_task_completed(_task):
			return
		WorkerThreadPool.wait_for_task_completion(_task)
		_task = -1
		_frame += 1
		for sk in skirts:
			sk["pos"] = (sk["sim"] as FailoringSim).get_positions()
		_measure()
		if colour_mode == 2:
			_recolour(true)
		for sk in skirts:
			_upload(sk)
		graph.queue_redraw()
	if not paused:
		var t := Time.get_ticks_usec()
		var list := skirts.duplicate()
		_task = WorkerThreadPool.add_task(func():
			var t1 := Time.get_ticks_usec()
			for sk in list:
				(sk["sim"] as FailoringSim).step(1)
			_ms = lerpf(_ms, (Time.get_ticks_usec() - t1) / 1000.0, 0.2 if _ms > 0.0 else 1.0))


func _upload(sk: Dictionary) -> void:
	var pos: PackedVector3Array = sk["pos"]
	var idx: PackedInt32Array = sk["idx"]
	var nrm := PackedVector3Array()
	nrm.resize(pos.size())
	for t in range(0, idx.size(), 3):
		var a := idx[t]
		var b := idx[t + 1]
		var c := idx[t + 2]
		var fn := (pos[b] - pos[a]).cross(pos[c] - pos[a])
		nrm[a] += fn
		nrm[b] += fn
		nrm[c] += fn
	for i in nrm.size():
		nrm[i] = nrm[i].normalized()
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = pos
	arr[Mesh.ARRAY_NORMAL] = nrm
	arr[Mesh.ARRAY_INDEX] = idx
	var cols: PackedColorArray = sk["colors"]
	if cols.size() == pos.size():
		arr[Mesh.ARRAY_COLOR] = cols
	var m := ArrayMesh.new()
	m.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	(sk["mi"] as MeshInstance3D).mesh = m


# Hem height around the body, binned by body angle (5 deg bins), and the summary numbers.
func _measure() -> void:
	var lines := []
	for sk in skirts:
		var pos: PackedVector3Array = sk["pos"]
		var hem: PackedInt32Array = sk["hem"]
		var x: float = sk["x"]
		var bins := PackedFloat32Array()
		var cnt := PackedInt32Array()
		bins.resize(72)
		cnt.resize(72)
		var lo := 1e9
		var hi := -1e9
		for v in hem:
			var q := pos[v]
			var th := rad_to_deg(atan2(q.x - x, q.z))
			var b := clampi(int(floor((th + 180.0) / 5.0)), 0, 71)
			bins[b] += q.y
			cnt[b] += 1
		var curve := PackedVector2Array()
		var at_seam := 0.0
		var at_mid := 0.0
		var ns := 0
		var nm := 0
		var n: int = sk["n"]
		for b in 72:
			if cnt[b] == 0:
				continue
			var y := bins[b] / cnt[b]
			var th := -180.0 + 5.0 * b + 2.5
			curve.append(Vector2(th, y))
			lo = minf(lo, y)
			hi = maxf(hi, y)
			# angle to the nearest seam vs to the nearest panel centre (seams are PI/n away from centres)
			var rel := fposmod(deg_to_rad(th), TAU / n)          # 0 = panel centre ... TAU/n = next centre
			var d_mid := minf(rel, TAU / n - rel)
			if absf(d_mid - PI / n) < deg_to_rad(7.5):
				at_seam += y
				ns += 1
			elif d_mid < deg_to_rad(7.5):
				at_mid += y
				nm += 1
		sk["curve"] = curve
		# fall-line stretch, area-weighted per 5 deg bin, as extra length over the skirt length (mm)
		var idx: PackedInt32Array = sk["idx"]
		var tw: PackedVector2Array = sk["tw"]
		var tbin: PackedInt32Array = sk["tbin"]
		var tarea: PackedFloat32Array = sk["tarea"]
		var vst := PackedFloat32Array()
		var vw := PackedFloat32Array()
		vst.resize(pos.size())
		vw.resize(pos.size())
		var sb := PackedFloat32Array()
		var sa := PackedFloat32Array()
		sb.resize(72)
		sa.resize(72)
		for t in tw.size():
			var pa := pos[idx[3 * t]]
			var fd := (pos[idx[3 * t + 1]] - pa) * tw[t].x + (pos[idx[3 * t + 2]] - pa) * tw[t].y
			var st := fd.length() - 1.0
			sb[tbin[t]] += st * tarea[t]
			sa[tbin[t]] += tarea[t]
			for k in 3:
				vst[idx[3 * t + k]] += st * tarea[t]
				vw[idx[3 * t + k]] += tarea[t]
		var scurve := PackedVector2Array()
		var smax := 0.0
		var smean := 0.0
		var sn := 0
		for b in 72:
			# 3-bin window (15 deg) so bins that straddle a seam do not jump
			var num := sb[(b + 71) % 72] + sb[b] + sb[(b + 1) % 72]
			var den := sa[(b + 71) % 72] + sa[b] + sa[(b + 1) % 72]
			if sa[b] <= 0.0 or den <= 0.0:
				continue
			var ext := num / den * LENGTH
			scurve.append(Vector2(-180.0 + 5.0 * b + 2.5, ext))
			smax = maxf(smax, ext)
			smean += ext
			sn += 1
		sk["scurve"] = scurve
		for i in vst.size():
			vst[i] = vst[i] / vw[i] if vw[i] > 0.0 else 0.0
		sk["vstretch"] = vst
		sk["smax"] = smax * 1000.0
		sk["smean"] = smean / maxi(sn, 1) * 1000.0
		var seam_y := at_seam / maxi(ns, 1)
		var mid_y := at_mid / maxi(nm, 1)
		sk["stats"] = {"range": (hi - lo) * 1000.0, "seam_drop": (mid_y - seam_y) * 1000.0, "mean": 0.0}
		var s := sk["sim"] as FailoringSim
		lines.append("%d panels (max %d deg off grain):  fall-line stretch max %.1f mm, mean %.1f mm   hem uneven by %.1f mm (folds included)   KE %s J   seam gap %.1f mm" % [
			n, int(round(rad_to_deg(FLARE / n / 2.0))), sk["smax"], sk["smean"], (hi - lo) * 1000.0,
			String.num_scientific(snappedf(s.get_kinetic_energy(), 1e-7)), s.get_max_seam_gap() * 1000.0])
	var shear: float = float((skirts[0]["sim"] as FailoringSim).get_material("demo_fabric")["shear_N_m_rad"])
	var head := "%s   shear %.2f N/m/rad%s   frame %d   %.0f ms/frame   (half-circle skirt, waist %.0f cm, length %.0f cm)" % [
		fabric_menu.get_item_text(fabric_index), shear,
		"  [isotropic control]" if isotropic else ("" if softness_index == 0 else "  [softened x%d: stands in for creep]" % int(SOFTNESS[softness_index])),
		_frame, _ms, WAIST_C * 100, LENGTH * 100]
	info.text = head + "\n" + "\n".join(lines)
	if OS.has_environment("FL_BIAS_LOG") and _frame % 20 == 0:
		print("BIAS frame=%d %s" % [_frame, info.text.replace("\n", " | ")])
		if _frame == 20:
			print("VERTS %d %d" % [(skirts[0]["sim"] as FailoringSim).get_vertex_count(), (skirts[1]["sim"] as FailoringSim).get_vertex_count()])
	if OS.has_environment("FL_FRAMES") and _frame >= int(OS.get_environment("FL_FRAMES")):
		for sk in skirts:
			var c: PackedVector2Array = sk.get("curve", PackedVector2Array())
			var row := []
			for p in c:
				row.append("%d:%.1f" % [int(p.x), p.y * 1000.0])
			print("CURVE n=%d %s" % [sk["n"], " ".join(row)])
			row = []
			for p in sk.get("scurve", PackedVector2Array()):
				row.append("%d:%.2f" % [int(p.x), p.y * 1000.0])
			print("STRETCH n=%d %s" % [sk["n"], " ".join(row)])
		if OS.has_environment("FL_SHOT"):      # real window only: save a screenshot and quit
			paused = true
			await RenderingServer.frame_post_draw
			await RenderingServer.frame_post_draw
			get_viewport().get_texture().get_image().save_png(OS.get_environment("FL_SHOT"))
		get_tree().quit()


func _draw_graph() -> void:
	graph.draw_rect(Rect2(Vector2.ZERO, graph.size), Color(0, 0, 0, 0.55))
	if skirts.size() < 2:
		return
	var half := graph.size.x * 0.5
	_plot(Rect2(0, 0, half, graph.size.y), "scurve", "Extra length along the fall line (cloth stretch), mm", 1000.0, "%.0f", true)
	_plot(Rect2(half, 0, half, graph.size.y), "curve", "Hem height above the floor, cm (folds included)", 100.0, "%.1f", false)


# One plot: value per body angle for both skirts. zero_base: the axis starts at 0 (stretch), else it fits the data.
func _plot(r: Rect2, key: String, title: String, unit: float, fmt: String, zero_base: bool) -> void:
	var font := ThemeDB.fallback_font
	var lo := 1e9
	var hi := -1e9
	for sk in skirts:
		for p in sk.get(key, PackedVector2Array()):
			lo = minf(lo, p.y)
			hi = maxf(hi, p.y)
	if lo > hi:
		return
	if zero_base:
		lo = minf(lo, 0.0)
		hi = maxf(hi * 1.1, lo + 0.005)            # at least 5 mm of axis
	else:
		var mid := (lo + hi) * 0.5
		var span := maxf(hi - lo, 0.02) * 1.15     # at least 2 cm of axis
		lo = mid - span * 0.5
		hi = mid + span * 0.5
	var x0 := r.position.x + 52.0
	var x1 := r.end.x - 14.0
	var y0 := r.end.y - 24.0
	var y1 := r.position.y + 40.0
	var to_px := func(th: float, y: float) -> Vector2:
		return Vector2(x0 + (th + 180.0) / 360.0 * (x1 - x0), y0 - (y - lo) / (hi - lo) * (y0 - y1))
	graph.draw_string(font, Vector2(x0, r.position.y + 16), title, HORIZONTAL_ALIGNMENT_LEFT, -1, 13, Color(1, 1, 1, 0.9))
	var names := {-180: "back", -90: "left", 0: "front", 90: "right", 180: "back"}
	for th in [-180, -90, 0, 90, 180]:
		var a: Vector2 = to_px.call(th, lo)
		var b: Vector2 = to_px.call(th, hi)
		graph.draw_line(a, b, Color(1, 1, 1, 0.12))
		var txt: String = names[th]
		graph.draw_string(font, a + Vector2(-14, 16), txt, HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color(1, 1, 1, 0.6))
	for k in 5:
		var y := lo + (hi - lo) * k / 4.0
		var p: Vector2 = to_px.call(-180, y)
		graph.draw_line(p, Vector2(x1, p.y), Color(1, 1, 1, 0.06))
		graph.draw_string(font, Vector2(r.position.x + 6, p.y + 4), fmt % (y * unit), HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color(1, 1, 1, 0.6))
	var ly := r.position.y + 30
	var lx := x0
	for sk in skirts:
		var col: Color = sk["col"]
		for sa in sk["seams"]:
			var a: Vector2 = to_px.call(sa, hi)
			graph.draw_line(a + Vector2(0, -6), a + Vector2(0, 8), col, 3.0)
		var pts := PackedVector2Array()
		for p in sk.get(key, PackedVector2Array()):
			pts.append(to_px.call(p.x, p.y))
		if pts.size() > 1:
			graph.draw_polyline(pts, col, 2.0, true)
		var leg := "%d panels" % sk["n"]
		graph.draw_line(Vector2(lx, ly - 4), Vector2(lx + 16, ly - 4), col, 3.0)
		graph.draw_string(font, Vector2(lx + 20, ly), leg, HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color(1, 1, 1, 0.75))
		lx += 90
	graph.draw_string(font, Vector2(lx, ly), "(ticks = seams)", HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color(1, 1, 1, 0.5))


# ---------------------------------------------------------------- camera
func _update_camera() -> void:
	var dir := Vector3(sin(yaw) * cos(pitch), -sin(pitch), cos(yaw) * cos(pitch))
	cam.position = target + dir * dist
	cam.look_at(target)


func _unhandled_input(ev: InputEvent) -> void:
	if ev is InputEventMouseMotion:
		if ev.button_mask & MOUSE_BUTTON_MASK_LEFT:
			yaw -= ev.relative.x * 0.006
			pitch = clampf(pitch + ev.relative.y * 0.006, -1.3, 1.3)
		elif ev.button_mask & (MOUSE_BUTTON_MASK_RIGHT | MOUSE_BUTTON_MASK_MIDDLE):
			var right := cam.global_transform.basis.x
			var up := cam.global_transform.basis.y
			target += (-right * ev.relative.x + up * ev.relative.y) * dist * 0.0012
	elif ev is InputEventMouseButton and ev.pressed:
		if ev.button_index == MOUSE_BUTTON_WHEEL_UP:
			dist = maxf(0.5, dist * 0.9)
		elif ev.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			dist = minf(8.0, dist * 1.1)
	elif ev is InputEventKey and ev.pressed and not ev.echo:
		match ev.keycode:
			KEY_R: _restart()
			KEY_SPACE: pause_btn.pressed.emit()
			KEY_C:
				colour_mode = (colour_mode + 1) % 3
				colour_menu.selected = colour_mode
				_recolour()
			KEY_I: iso_box.button_pressed = not iso_box.button_pressed


func _exit_tree() -> void:
	if _task >= 0:
		WorkerThreadPool.wait_for_task_completion(_task)
