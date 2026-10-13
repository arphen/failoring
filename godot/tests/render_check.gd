# Headless check of the render layer (look.gd, cloth_shade.gd, shaders): no solver needed.
#   godot --headless --path godot --script res://tests/render_check.gd
# Builds a synthetic body (a cylinder of points) and a cloth tube just outside it, then checks that
# every quality level applies, that contact occlusion is darker next to the body than in the open,
# and that fill() writes one surface per fabric with finite data. Exits 0 on success, 1 on failure.
extends SceneTree

const Look = preload("res://look.gd")
const ClothShade = preload("res://cloth_shade.gd")

var _fails := 0


func _check(ok: bool, what: String) -> void:
	print(("PASS " if ok else "FAIL ") + what)
	if not ok:
		_fails += 1


func _init() -> void:
	# quality levels apply without error to the scene pieces
	var env := Environment.new()
	Look.environment(env)
	var sun := DirectionalLight3D.new()
	Look.key_light(sun)
	var vp := SubViewport.new()
	var bm := Look.body_material()
	for lvl in Look.QUALITY_LEVELS:
		Look.apply_quality(lvl, env, sun, vp, bm)
		var q := Look.quality(lvl)
		_check(vp.msaa_3d == int(q["msaa"]), "%s: msaa %d" % [lvl, vp.msaa_3d])
		_check(env.ssao_enabled == (bool(q["ssao"]) and RenderingServer.get_rendering_device() != null), "%s: ssao flag" % lvl)
	_check(Look.quality("nonsense") == Look.QUALITY["medium"], "unknown level falls back to medium")

	# synthetic body: points on a cylinder of radius 0.2 m, 1.6 m tall
	var body := PackedVector3Array()
	for i in 64:
		for j in 40:
			var a := TAU * float(i) / 64.0
			body.append(Vector3(0.2 * cos(a), 0.2 + 1.6 * float(j) / 39.0, 0.2 * sin(a)))
	# cloth: a tube at radius 0.215 (near the body) on the lower half, and at 0.30 (open air) on the top third
	var rows := 40
	var cols := 48
	var pos := PackedVector3Array()
	var uv := PackedVector2Array()
	for j in rows:
		for i in cols:
			var a := TAU * float(i) / float(cols)
			var r := 0.215 if j < rows / 2 else 0.30
			var y := 0.3 + 0.9 * float(j) / float(rows - 1)
			pos.append(Vector3(r * cos(a), y, r * sin(a)))
			uv.append(Vector2(float(i) * 0.02, float(j) * 0.02))
	var idx := PackedInt32Array()
	for j in rows - 1:
		for i in cols:
			var a0 := j * cols + i
			var a1 := j * cols + (i + 1) % cols
			var b0 := (j + 1) * cols + i
			var b1 := (j + 1) * cols + (i + 1) % cols
			idx.append_array([a0, a1, b0, a1, b1, b0])
	var n := pos.size()
	var half := n / 2  # two pieces: the lower rows are "p" (wool), the upper rows "q" (denim)
	var meta := {"n": n, "ranges": {"p": {"first": 0, "count": half}, "q": {"first": half, "count": n - half}}, "uv": {"p": uv.slice(0, half), "q": uv.slice(half)}}
	var pieces := [{"id": "p", "fabric": "wool_suiting"}, {"id": "q", "fabric": "denim_12oz"}]
	var shade := ClothShade.new()
	shade.set_quality("medium")
	shade.setup(meta, PackedInt32Array(), pieces, "cotton_poplin", body, idx)
	var am := ArrayMesh.new()
	var node := MeshInstance3D.new()
	var cols_arr := PackedColorArray()
	cols_arr.resize(n)
	cols_arr.fill(Color(0.3, 0.4, 0.5, 1.0))
	shade.fill(am, node, pos, cols_arr)
	_check(am.get_surface_count() == 2, "one surface per fabric (%d)" % am.get_surface_count())
	_check(node.get_surface_override_material_count() == 2 and node.get_surface_override_material(0) is ShaderMaterial and node.get_surface_override_material(0) != node.get_surface_override_material(1), "each surface has its own ShaderMaterial")
	var arrays := am.surface_get_arrays(0)
	var colors: PackedColorArray = arrays[Mesh.ARRAY_COLOR]
	var near := 0.0
	var far := 0.0
	for j in rows:
		for i in cols:
			var k := j * cols + i
			if j < rows / 2:
				near += colors[k].a
			else:
				far += colors[k].a
	near /= float((rows / 2) * cols)
	far /= float((rows - rows / 2) * cols)
	print("mean occlusion near body %.3f, in the open %.3f" % [near, far])
	_check(near < far - 0.02, "cloth touching the body is darker than cloth in the open")
	var finite := true
	var nrm: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
	var sc: PackedVector2Array = arrays[Mesh.ARRAY_TEX_UV2]
	for k in n:
		if not (is_finite(nrm[k].x) and is_finite(nrm[k].y) and is_finite(nrm[k].z) and is_finite(sc[k].x) and is_finite(sc[k].y)):
			finite = false
	_check(finite, "normals and strain/curvature are finite")
	_check(nrm[0].length() > 0.9, "normals are unit length")

	# off quality: no occlusion, no strain
	shade.set_quality("low")
	shade.fill(am, node, pos, cols_arr)
	arrays = am.surface_get_arrays(0)
	colors = arrays[Mesh.ARRAY_COLOR]
	_check(is_equal_approx(colors[0].a, 1.0) and is_equal_approx(colors[n - 1].a, 1.0), "low quality: no vertex occlusion")

	print("render_check: %s (%d failures)" % ["OK" if _fails == 0 else "FAILED", _fails])
	quit(0 if _fails == 0 else 1)
