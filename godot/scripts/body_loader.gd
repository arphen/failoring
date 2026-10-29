# UNTESTED: written without a Godot binary (no editor or runtime was available), against the Godot 4.3 API from memory.
# Open it in the editor and check the console before relying on it.
#
# Loads one preset exported by tools/export_godot_assets.js (formats: docs/GODOT_ASSETS.md) into this MeshInstance3D
# and plays the baked walk:
#   <assets_dir>/<preset>/body.obj   standing mesh, 13380 vertices, metres, y up, body faces +Z
#   <assets_dir>/<preset>/walk.bin   one gait cycle, 24 frames, float16 offsets from the standing mesh
# The frames are already posed, so no skeleton is needed for the walk. Frames are interpolated linearly in time.
class_name FailoringBody
extends MeshInstance3D

@export var preset: String = "average"
@export var assets_dir: String = "res://assets"
@export var walking: bool = true
@export var speed: float = 1.0          # 1 = the browser app's walk (one cycle = 64/60 s)
# The app's triangles are counter-clockwise seen from outside; Godot's front faces are clockwise. Reversing the index
# order makes the outside visible. Set false if the body shows inside out.
@export var flip_winding: bool = true

var standing: PackedVector3Array = PackedVector3Array()   # body.obj vertices (also the walk's reference positions)
var indices: PackedInt32Array = PackedInt32Array()        # body.obj triangles, 0-based, original winding
var frames: Array = []                                    # one PackedVector3Array of absolute positions per walk frame
var fps: float = 22.5
var _t: float = 0.0
var _last_frame: float = -1.0
var _mesh: ArrayMesh = ArrayMesh.new()
var _draw_idx: PackedInt32Array = PackedInt32Array()     # indices as drawn (winding flipped once, not per frame)
var _shown: PackedVector3Array = PackedVector3Array()    # positions currently in the mesh, to skip repeat poses
# skin attributes (skin.gd build: rest position, cavity and colour masks per vertex) and the eyeballs that follow the head;
# both optional, set by set_skin_data for the generated mannequin
var _skin: Dictionary = {}
var _drawn_idx := PackedInt32Array()   # the drawn mesh's triangles (skin "drawn"), winding flipped like _draw_idx
var eyes: Node3D = null
var hair: Node3D = null         # hair.gd, when a likeness's long hair is shown (set_long_hair)

# The idle layer (idle.gd): breathing, weight shifts, micro motion, blinks and gaze. The mesh is not rebuilt for it: the skin
# shader skins it again with one small transform per bone (a 24 x 3 float texture) and displaces the chest for the breath.
var idle: RefCounted = null          # FailoringIdle; null: the body is still
var idle_pose_on := true             # the host turns the bone motion off while the drawn pose is not a standing one (a walk)
var idle_paused := false             # a render steps the idle layer itself, once per frame (render_mode.gd)
var breath_gain := 1.0               # the host lowers it when garments are on (the cloth does not follow the chest)
var _idle_img: Image
var _idle_tex: ImageTexture
var _idle_mat: ShaderMaterial
var _rig_joints: Dictionary = {}     # joint name -> position in the standing pose, for the bones' pivots
var _rig_bones: Array = []


func _ready() -> void:
	extra_cull_margin = 0.5          # the posed mesh moves; the AABB of the first surface does not follow it
	load_preset(preset)


# Returns false when body.obj cannot be read. Without walk.bin the body is only shown standing.
func load_preset(name: String) -> bool:
	var dir := "%s/%s" % [assets_dir, name]
	var obj := parse_obj(dir + "/body.obj")
	if obj.is_empty():
		return false
	standing = obj["vertices"]
	_set_topology(obj["indices"])
	mesh = _mesh
	_build(standing)
	frames = []
	if FileAccess.file_exists(dir + "/walk.bin"):
		var w := parse_walk(dir + "/walk.bin", standing)
		if not w.is_empty():
			frames = w["frames"]
			fps = w["fps"]
	return true


func show_standing() -> void:
	walking = false
	_build(standing)


# Long hair (hair.gd) on or off: m is the body data (body_data.json, BodyGen.load_data), colour the hair's. Built from
# the body as it is drawn next (the standing body, normally). Needs the skin data (set_skin_data: the eye anchors).
func set_long_hair(on: bool, m: Dictionary = {}, colour: Color = Color(0.31, 0.24, 0.18)) -> void:
	if not on or _skin.is_empty() or m.is_empty():
		if hair != null:
			hair.queue_free()
			hair = null
		return
	if hair == null:
		hair = preload("res://hair.gd").new()
		hair.name = "Hair"
		add_child(hair)
	hair.setup(_skin.get("eyes", {}), m["base"], m["tris"], int(m["nb"]))
	hair.set_color(colour)
	if not _shown.is_empty():
		hair.follow(_shown)


# Per-vertex skin attributes and eyes (skin.gd build output). Used while the vertex count matches.
func set_skin_data(skin: Dictionary) -> void:
	_skin = skin
	if eyes == null:
		eyes = preload("res://eyes.gd").new()
		eyes.name = "Eyes"
		add_child(eyes)
	eyes.setup(skin.get("eyes", {}))
	_drawn_idx = PackedInt32Array()
	var cur := _shown
	_shown = PackedVector3Array()
	if not cur.is_empty():
		_build(cur)
	elif not standing.is_empty():
		_build(standing)


# Shows an explicit pose (the editor drives the walk clock itself so the rendered
# body and the collider stay in sync).
func show_pose(v: PackedVector3Array) -> void:
	walking = false
	if v.size() == _shown.size() and v == _shown:
		return
	_build(v)


# Uses caller-supplied mesh data instead of files (live generated bodies).
func use_custom(verts: PackedVector3Array, tris: PackedInt32Array) -> void:
	standing = verts
	_set_topology(tris)
	frames = []
	mesh = _mesh
	_build(standing)


# The idle layer needs the standing pose's joint positions and the rig's bones (BodyGen.stand()["joints"], M["bones"]).
func set_idle(idle_layer: RefCounted, joints: Dictionary = {}, bones: Array = []) -> void:
	idle = idle_layer
	if not joints.is_empty():
		_rig_joints = joints
	if not bones.is_empty():
		_rig_bones = bones
	if _idle_img == null:
		_idle_img = Image.create(24, 3, false, Image.FORMAT_RGBAF)
		_idle_tex = ImageTexture.create_from_image(_idle_img)
		var m := material_override as ShaderMaterial
		if m != null:
			m.set_shader_parameter("idle_xf", _idle_tex)


func _idle_step(delta: float) -> void:
	var m := material_override as ShaderMaterial
	if m == null:
		return
	if _idle_mat != m:
		_idle_mat = m
		m.set_shader_parameter("idle_xf", _idle_tex)
	idle.tick(0.0 if idle_paused else delta)
	var rig_ok: bool = not _rig_bones.is_empty() and not _rig_joints.is_empty() and not _skin.is_empty() \
			and (_skin["drawn"] as Dictionary)["custom1"].size() > 0 and _rig_bones.size() <= 24
	var head := Transform3D.IDENTITY
	if idle.enabled and idle_pose_on and rig_ok:
		var xf: Array = idle.solve(_rig_joints, _rig_bones)
		for k in xf.size():
			var t: Transform3D = xf[k]
			var bs := t.basis
			_idle_img.set_pixel(k, 0, Color(bs.x.x, bs.y.x, bs.z.x, t.origin.x))
			_idle_img.set_pixel(k, 1, Color(bs.x.y, bs.y.y, bs.z.y, t.origin.y))
			_idle_img.set_pixel(k, 2, Color(bs.x.z, bs.y.z, bs.z.z, t.origin.z))
			if (_rig_bones[k] as Dictionary)["joint"] == "head":
				head = t
		_idle_tex.update(_idle_img)
		m.set_shader_parameter("idle_on", 1.0)
	else:
		m.set_shader_parameter("idle_on", 0.0)
	m.set_shader_parameter("breath", idle.breath * breath_gain if idle.enabled else 0.0)
	m.set_shader_parameter("blink", idle.blink)
	if eyes != null:
		eyes.idle(idle.gaze, head)
	if hair != null:
		hair.idle(head)


func _process(delta: float) -> void:
	if idle != null:
		_idle_step(delta)
	if not walking or frames.is_empty():
		return
	_t += delta * speed
	var nf := frames.size()
	var f := fposmod(_t * fps, float(nf))
	if absf(f - _last_frame) < 1e-4:
		return
	_last_frame = f
	var k := int(f)
	var a := f - float(k)
	var fa: PackedVector3Array = frames[k]
	var fb: PackedVector3Array = frames[(k + 1) % nf]
	var v := PackedVector3Array()
	v.resize(fa.size())
	for i in fa.size():
		v[i] = fa[i].lerp(fb[i], a)
	_build(v)


# ---------------------------------------------------------------- parsing (static: usable without a node)

# Reads the vertices ("v x y z") and triangles ("f a b c", 1-based) of a Wavefront OBJ.
static func parse_obj(path: String) -> Dictionary:
	var text := FileAccess.get_file_as_string(path)
	if text.is_empty():
		push_error("body_loader: cannot read " + path)
		return {}
	var v := PackedVector3Array()
	var f := PackedInt32Array()
	for raw in text.split("\n"):
		var line := raw.strip_edges()
		if line.begins_with("v "):
			var p := line.split(" ", false)
			v.append(Vector3(p[1].to_float(), p[2].to_float(), p[3].to_float()))
		elif line.begins_with("f "):
			var p := line.split(" ", false)
			f.append(p[1].to_int() - 1)
			f.append(p[2].to_int() - 1)
			f.append(p[3].to_int() - 1)
	return {"vertices": v, "indices": f}


# Decodes walk.bin: 'FLWALK01', u32 LE header length, JSON header, data. Frame k = ref + float16 offset.
# ref = the standing vertices from body.obj (the same 4-decimal values). Returns {frames, fps, cycle_seconds} or {}.
static func parse_walk(path: String, ref: PackedVector3Array) -> Dictionary:
	var b := FileAccess.get_file_as_bytes(path)
	if b.size() < 12 or b.slice(0, 8).get_string_from_ascii() != "FLWALK01":
		push_error("body_loader: not a walk file: " + path)
		return {}
	var hl := b.decode_u32(8)
	var h: Dictionary = JSON.parse_string(b.slice(12, 12 + hl).get_string_from_utf8())
	var data := 12 + hl
	var nv := int(h["vertices"])
	var nf := int(h["frames"])
	if ref.size() != nv:
		push_error("body_loader: walk has %d vertices, body.obj has %d" % [nv, ref.size()])
		return {}
	var sec: Dictionary = h["sections"]
	var fo := data + int(sec["frames"]["offset"])     # section offsets are relative to the data start
	var fb := int(sec["frames"]["frame_bytes"])       # 6 * nv
	var out: Array = []
	out.resize(nf)
	for k in nf:
		var fr := PackedVector3Array()
		fr.resize(nv)
		var base := fo + k * fb
		for i in nv:
			var o := base + 6 * i
			fr[i] = ref[i] + Vector3(b.decode_half(o), b.decode_half(o + 2), b.decode_half(o + 4))
		out[k] = fr
	return {"frames": out, "fps": float(h["fps"]), "cycle_seconds": float(h["cycle_seconds"])}


# ---------------------------------------------------------------- mesh

# Rebuilds the surface from vertex positions. Normals are area-weighted face normals from the original (outward)
# winding; the index order is reversed afterwards when flip_winding is set. Creating a surface per frame is simple;
# for speed later, update the vertex buffer with RenderingServer instead.
func _build(verts: PackedVector3Array) -> void:
	var n := PackedVector3Array()
	n.resize(verts.size())
	for t in range(0, indices.size(), 3):
		var a := indices[t]
		var b := indices[t + 1]
		var c := indices[t + 2]
		var fn := (verts[b] - verts[a]).cross(verts[c] - verts[a])
		n[a] += fn
		n[b] += fn
		n[c] += fn
	for i in n.size():
		n[i] = n[i].normalized()
	if _draw_idx.size() != indices.size():
		_draw_idx = indices.duplicate()
		if flip_winding:
			for t in range(0, _draw_idx.size(), 3):
				var tmp := _draw_idx[t + 1]
				_draw_idx[t + 1] = _draw_idx[t + 2]
				_draw_idx[t + 2] = tmp
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	var flags := 0
	if not _skin.is_empty() and int(_skin["n"]) == verts.size():
		# the mannequin with its skin data: drawn vertices gathered from the body (UV seams split some of them)
		var d: Dictionary = _skin["drawn"]
		var src: PackedInt32Array = d["src"]
		var dv := PackedVector3Array()
		var dn := PackedVector3Array()
		dv.resize(src.size())
		dn.resize(src.size())
		for i in src.size():
			dv[i] = verts[src[i]]
			dn[i] = n[src[i]]
		if _drawn_idx.is_empty():
			_drawn_idx = (d["tris"] as PackedInt32Array).duplicate()
			if flip_winding:
				for t in range(0, _drawn_idx.size(), 3):
					var tmp := _drawn_idx[t + 1]
					_drawn_idx[t + 1] = _drawn_idx[t + 2]
					_drawn_idx[t + 2] = tmp
		arrays[Mesh.ARRAY_VERTEX] = dv
		arrays[Mesh.ARRAY_NORMAL] = dn
		arrays[Mesh.ARRAY_INDEX] = _drawn_idx
		arrays[Mesh.ARRAY_TEX_UV] = d["uv"]
		arrays[Mesh.ARRAY_COLOR] = d["color"]
		arrays[Mesh.ARRAY_CUSTOM0] = d["custom0"]
		flags = Mesh.ARRAY_CUSTOM_RGBA_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM0_SHIFT
		if (d["custom1"] as PackedFloat32Array).size() == 4 * src.size():
			# the rig (bones and weights) and the breathing weights, for the idle layer in the vertex shader
			arrays[Mesh.ARRAY_CUSTOM1] = d["custom1"]
			arrays[Mesh.ARRAY_CUSTOM2] = d["custom2"]
			arrays[Mesh.ARRAY_CUSTOM3] = d["custom3"]
			flags |= Mesh.ARRAY_CUSTOM_RGBA_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM1_SHIFT
			flags |= Mesh.ARRAY_CUSTOM_RGBA_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM2_SHIFT
			flags |= Mesh.ARRAY_CUSTOM_RGBA_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM3_SHIFT
	else:
		arrays[Mesh.ARRAY_VERTEX] = verts
		arrays[Mesh.ARRAY_NORMAL] = n
		arrays[Mesh.ARRAY_INDEX] = _draw_idx
	_mesh.clear_surfaces()
	_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays, [], {}, flags)
	_shown = verts
	if eyes != null:
		eyes.follow(verts)
	if hair != null:
		hair.follow(verts)


# Triangle topology changed: the flipped draw order is rebuilt on the next _build.
func _set_topology(tris: PackedInt32Array) -> void:
	indices = tris
	_draw_idx = PackedInt32Array()
	_shown = PackedVector3Array()
