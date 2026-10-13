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
	indices = obj["indices"]
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


# Shows an explicit pose (the editor drives the walk clock itself so the rendered
# body and the collider stay in sync).
func show_pose(v: PackedVector3Array) -> void:
	walking = false
	_build(v)


# Uses caller-supplied mesh data instead of files (live generated bodies).
func use_custom(verts: PackedVector3Array, tris: PackedInt32Array) -> void:
	standing = verts
	indices = tris
	frames = []
	mesh = _mesh
	_build(standing)


func _process(delta: float) -> void:
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
	var idx := indices.duplicate()
	if flip_winding:
		for t in range(0, idx.size(), 3):
			var tmp := idx[t + 1]
			idx[t + 1] = idx[t + 2]
			idx[t + 2] = tmp
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = n
	arrays[Mesh.ARRAY_INDEX] = idx
	_mesh.clear_surfaces()
	_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
