extends RefCounted
# What the crochet view needs to draw one simulated piece (docs/CROCHET_PLAN.md 8.3), shared by the scene and the compare view:
# the two-shell mesh, the fibre look of the yarn, and the box / peg props.

const SURFACE_SHADER := preload("res://crochet/crochet_surface.gdshader")

static func make_material(side: float) -> ShaderMaterial:
	var m := ShaderMaterial.new()
	m.shader = SURFACE_SHADER
	m.set_shader_parameter("side", side)
	return m

# Fibre look: cotton matte, acrylic with a slight sheen, wool fuzzy. `info` is CrochetPattern.get_yarn_info() (empty = the default red).
static func apply_look(materials: Array, info: Dictionary) -> void:
	var colour := Color(0.82, 0.32, 0.23)
	var rough := 0.85
	var sheen := 0.2
	if info.size() > 0:
		colour = Color.html(String(info.get("colour", "#b5523b")))
		match String(info.get("fibre", "acrylic")):
			"cotton":
				rough = 0.9
				sheen = 0.05
			"wool":
				rough = 1.0
				sheen = 0.6
			"tshirt":
				rough = 0.7
				sheen = 0.15
			_:
				rough = 0.6
				sheen = 0.25
	for m in materials:
		m.set_shader_parameter("albedo", colour)
		m.set_shader_parameter("rough", rough)
		m.set_shader_parameter("sheen", sheen)

# Two shells: the right side outside (offset along the normal by half the thickness), the wrong side inside (offset against it, winding
# reversed). `positions` are the world positions of the vertices, `colours` the vertex colours (alpha 0 = selected).
static func build_mesh(sim: CrochetScene, positions: PackedVector3Array, colours: PackedColorArray, mat_rs: Material, mat_ws: Material) -> ArrayMesh:
	var normals := sim.get_normals()
	var half := 0.5 * sim.get_thickness()
	var indices := sim.get_indices()
	var uv := sim.get_vertex_uv()
	var outer := PackedVector3Array()
	var inner := PackedVector3Array()
	var inner_n := PackedVector3Array()
	outer.resize(positions.size())
	inner.resize(positions.size())
	inner_n.resize(positions.size())
	for i in positions.size():
		outer[i] = positions[i] + normals[i] * half
		inner[i] = positions[i] - normals[i] * half
		inner_n[i] = -normals[i]
	var flipped := PackedInt32Array()
	flipped.resize(indices.size())
	for t in range(0, indices.size(), 3):
		flipped[t] = indices[t]
		flipped[t + 1] = indices[t + 2]
		flipped[t + 2] = indices[t + 1]
	var mesh := ArrayMesh.new()
	var a0 := []
	a0.resize(Mesh.ARRAY_MAX)
	a0[Mesh.ARRAY_VERTEX] = outer
	a0[Mesh.ARRAY_NORMAL] = normals
	a0[Mesh.ARRAY_TEX_UV] = uv
	a0[Mesh.ARRAY_COLOR] = colours
	a0[Mesh.ARRAY_INDEX] = indices
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, a0)
	mesh.surface_set_material(0, mat_rs)
	var a1 := []
	a1.resize(Mesh.ARRAY_MAX)
	a1[Mesh.ARRAY_VERTEX] = inner
	a1[Mesh.ARRAY_NORMAL] = inner_n
	a1[Mesh.ARRAY_TEX_UV] = uv
	a1[Mesh.ARRAY_COLOR] = colours
	a1[Mesh.ARRAY_INDEX] = flipped
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, a1)
	mesh.surface_set_material(1, mat_ws)
	return mesh

# The load box once it is inserted and the peg of the hook scene; `shift` moves them into the studio like the cloth.
static func place_props(sim: CrochetScene, load_node: MeshInstance3D, peg_node: MeshInstance3D, shift: Vector3) -> void:
	var ld := sim.get_load()
	if ld.size() > 0 and bool(ld.get("inserted", false)):
		var box := BoxMesh.new()
		box.size = ld["size"]
		load_node.mesh = box
		var m := StandardMaterial3D.new()
		m.albedo_color = Color(0.62, 0.63, 0.66)
		m.metallic = 0.8
		m.roughness = 0.35
		load_node.material_override = m
		var t := sim.get_load_transform()
		load_node.transform = Transform3D(t.basis, t.origin + shift)
		load_node.visible = true
	var peg := sim.get_peg()
	if peg.size() > 0:
		var cap := CapsuleMesh.new()
		cap.radius = float(peg["radius"])
		cap.height = float(peg["length"])
		peg_node.mesh = cap
		var pm := StandardMaterial3D.new()
		pm.albedo_color = Color(0.4, 0.3, 0.2)
		peg_node.material_override = pm
		var c: Vector3 = peg["center"]
		peg_node.transform = Transform3D(Basis(Vector3(0, 0, 1), PI * 0.5), Vector3(shift.x, c.y + shift.y, shift.z))
		peg_node.visible = true
