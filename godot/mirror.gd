# The fitting mirror's reflection: a true planar mirror for each of its three panels. A panel's reflection is the scene seen by a
# camera placed where the viewer's eye would be if it stood behind the glass (the eye reflected across the panel's plane), looking
# through the panel as through a window (an off-axis frustum that is exactly the panel's rectangle). The image is drawn on the panel's
# glass by shaders/mirror.gdshader. Unlike a reflection probe, the parallax is right and the mannequin's back and the garment on it
# really show in the glass.
#
# It costs one extra render of the room per panel, so a panel renders only while it faces the camera and is in view, and not at all
# on the low quality level (the glass is then a dark mirror). Look only.
extends Node

const GLASS_SHADER := preload("res://shaders/mirror.gdshader")
const REFLECT_H := 640                       # px: height of a panel's reflection image
const NEAR_FAR := 40.0

var panels: Array = []     # {glass, vp, cam, w, h, mat}
var enabled := true
var _main_cam: Camera3D
var _env_src: Environment


# glass: the panels' glass quads (MeshInstance3D with a QuadMesh; +z is the way the glass faces). world: the 3D world to render.
func setup(glass: Array, main_cam: Camera3D, world: World3D, env: Environment) -> void:
	_main_cam = main_cam
	_env_src = env
	for g in glass:
		var gm := g as MeshInstance3D
		var q := gm.mesh as QuadMesh
		var sz := q.size
		var vp := SubViewport.new()
		vp.name = "Reflection"
		vp.own_world_3d = false
		vp.world_3d = world
		vp.size = Vector2i(maxi(64, roundi(float(REFLECT_H) * sz.x / sz.y)), REFLECT_H)
		vp.render_target_update_mode = SubViewport.UPDATE_DISABLED
		vp.use_hdr_2d = OS.get_environment("FL_MIRROR_HDR") != "0"
		vp.transparent_bg = false
		vp.msaa_3d = Viewport.MSAA_2X
		vp.positional_shadow_atlas_size = 2048
		vp.positional_shadow_atlas_quad_0 = Viewport.SHADOW_ATLAS_QUADRANT_SUBDIV_1
		vp.positional_shadow_atlas_quad_1 = Viewport.SHADOW_ATLAS_QUADRANT_SUBDIV_4
		vp.positional_shadow_atlas_quad_2 = Viewport.SHADOW_ATLAS_QUADRANT_SUBDIV_4
		vp.positional_shadow_atlas_quad_3 = Viewport.SHADOW_ATLAS_QUADRANT_SUBDIV_16
		add_child(vp)
		var cam := Camera3D.new()
		cam.keep_aspect = Camera3D.KEEP_HEIGHT
		cam.cull_mask = ((1 << 20) - 1) & ~4          # everything but layer 3, which holds the glass and what is smeared on it
		if OS.get_environment("FL_MIRROR_ENV") != "world":
			cam.environment = _reflection_env()
		vp.add_child(cam)
		var m := ShaderMaterial.new()
		m.shader = GLASS_SHADER
		m.set_shader_parameter("reflection", vp.get_texture())
		m.set_shader_parameter("live", 0.0)
		gm.material_override = m
		panels.append({"glass": gm, "vp": vp, "cam": cam, "w": sz.x, "h": sz.y, "mat": m})


# The main environment with the display steps left out: the reflection is raw radiance (HDR, linear), and the main camera's tone
# mapping, glow and grade then act on it as on every other surface.
func _reflection_env() -> Environment:
	var e := _env_src.duplicate() as Environment
	e.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	e.tonemap_exposure = 1.0
	e.adjustment_enabled = false
	e.glow_enabled = false
	e.ssr_enabled = false
	e.ssil_enabled = false
	e.ssao_enabled = false
	e.sdfgi_enabled = false          # a second set of GI cascades is not worth it: the ambient light comes from the sky
	e.ambient_light_energy = _env_src.ambient_light_energy + 0.15
	return e


# Call every frame after the main camera has moved.
func update() -> void:
	if _main_cam == null:
		return
	var eye := _main_cam.global_position
	for p in panels:
		var gm: MeshInstance3D = p["glass"]
		var vp: SubViewport = p["vp"]
		var cam: Camera3D = p["cam"]
		var gt := gm.global_transform
		var n := gt.basis.z.normalized()
		var u := gt.basis.y.normalized()
		var c := gt.origin
		var d := (eye - c).dot(n)
		var live: bool = enabled and d > 0.05 and gm.is_visible_in_tree() and _in_view(gt, p["w"], p["h"])
		if OS.has_environment("FL_MIRROR_DEBUG"):
			print("MIRROR ", gm.name, " d=%.2f enabled=%s vis=%s inview=%s" % [d, enabled, gm.is_visible_in_tree(), _in_view(gt, p["w"], p["h"])])
		(p["mat"] as ShaderMaterial).set_shader_parameter("live", 1.0 if live else 0.0)
		vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS if live else SubViewport.UPDATE_DISABLED
		if not live:
			continue
		var e2 := eye - n * (2.0 * d)                       # the eye behind the glass
		var zc := -n
		var xc := u.cross(zc)
		cam.global_transform = Transform3D(Basis(xc, u, zc), e2)
		var v := c - e2
		# the window is the panel itself, at distance d from the reflected eye: near plane = the glass plane
		cam.set_frustum(float(p["h"]), Vector2(v.dot(xc), v.dot(u)), d, NEAR_FAR)


func _in_view(gt: Transform3D, w: float, h: float) -> bool:
	for sx in [-0.5, 0.5]:
		for sy in [-0.5, 0.5]:
			if _main_cam.is_position_in_frustum(gt * Vector3(sx * w, sy * h, 0.0)):
				return true
	return _main_cam.is_position_in_frustum(gt.origin)
