# The wearer's own face (and hair), made from their photos by tools/face/build_face.sh. Look only; nothing in the
# repository or the app's data: the files live next to the measurements, in Documents/Failoring/faces/<id>/:
#   face_shape.json   per-vertex offsets of the head (tools/face/fit_shape.py), applied by BodyGen.generate
#   face_skin.png     the face's skin in MakeHuman's UV layout, alpha = where it covers (tools/face/bake_skin.py)
#   face.json         iris colour and hair colour sampled from the photos
#   hair.glb          the modelled hair (tools/face/hair_blender.py)
# FL_FACES_DIR=<dir> uses another folder (tests).
class_name FaceLikeness
extends RefCounted

const BodyGen = preload("res://body_gen.gd")


static func faces_dir() -> String:
	if OS.has_environment("FL_FACES_DIR"):
		return OS.get_environment("FL_FACES_DIR")
	return OS.get_system_dir(OS.SYSTEM_DIR_DOCUMENTS).path_join("Failoring").path_join("faces")


# The faces there with at least a shape: [id, ...].
static func available() -> Array:
	var out := []
	var d := DirAccess.open(faces_dir())
	if d == null:
		return out
	for sub in d.get_directories():
		if FileAccess.file_exists(faces_dir().path_join(sub).path_join("face_shape.json")):
			out.append(sub)
	out.sort()
	return out


static func path_of(id: String, file: String) -> String:
	return faces_dir().path_join(id).path_join(file)


# Puts face `id` into BodyGen.face (or clears it for ""). False when its shape cannot be read; the mannequin's face
# stays then.
static func use(id: String) -> bool:
	if id == "":
		BodyGen.face = {}
		return true
	var f := FileAccess.open(path_of(id, "face_shape.json"), FileAccess.READ)
	if f == null:
		BodyGen.face = {}
		return false
	var j = JSON.parse_string(f.get_as_text())
	if not (j is Dictionary) or not (j as Dictionary).has("idx") or not (j as Dictionary).has("d"):
		BodyGen.face = {}
		return false
	var idx := PackedInt32Array(j["idx"])
	var d := PackedFloat32Array(j["d"])
	if d.size() != 3 * idx.size():
		BodyGen.face = {}
		return false
	BodyGen.face = {"id": id, "idx": idx, "d": d, "ref": j["ref"]}
	return true


# face.json of face `id` ({} when there is none): iris [r, g, b], hair [r, g, b] (sRGB 0..1).
static func info(id: String) -> Dictionary:
	var f := FileAccess.open(path_of(id, "face.json"), FileAccess.READ)
	if f == null:
		return {}
	var j = JSON.parse_string(f.get_as_text())
	return j if j is Dictionary else {}


static var _skin_cache := {}


# The face skin of `id` as a texture with mipmaps, or null.
static func skin_texture(id: String) -> Texture2D:
	var p := path_of(id, "face_skin.png")
	if _skin_cache.has(p):
		return _skin_cache[p]
	if not FileAccess.file_exists(p):
		return null
	var img := Image.load_from_file(p)
	if img == null or img.is_empty():
		return null
	img.generate_mipmaps()
	var tex := ImageTexture.create_from_image(img)
	_skin_cache[p] = tex
	return tex
