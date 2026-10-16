# Prints, for every studio lamp: where it is, where its -Z axis points, whether it casts shadows, the bias settings, and
# which way the mannequin's shadow falls on the floor (from the lamp through the subject's centre), as a compass bearing
# seen from above (+z is towards the default camera). Run: godot --headless --path godot --script res://tests/light_diag.gd
extends SceneTree

const Studio = preload("res://studio.gd")


func _init() -> void:
	var stage := Node3D.new()
	root.add_child(stage)
	var st: Dictionary = Studio.build(stage)
	print("renderer: ", ProjectSettings.get_setting("rendering/renderer/rendering_method"), "  godot ", Engine.get_version_info()["string"])
	print("DirectionalLight3D nodes: ", _count(stage, "DirectionalLight3D"), "   SpotLight3D: ", _count(stage, "SpotLight3D"), "   OmniLight3D: ", _count(stage, "OmniLight3D"))
	for id in (st["lights"] as Dictionary).keys():
		var l: SpotLight3D = st["lights"][id]
		var fwd := -l.transform.basis.z
		var to_c := (Studio.CENTER - l.position).normalized()
		var sh := Vector2(fwd.x, fwd.z).normalized()
		print("%-6s pos %s  axis %s  off-centre %.1f deg  shadow %s  bias %.3f nbias %.2f blur %.1f size %.2f  floor-shadow runs towards x %+.2f z %+.2f (%s)" % [
			id, l.position, fwd, rad_to_deg(fwd.angle_to(to_c)), l.shadow_enabled, l.shadow_bias, l.shadow_normal_bias,
			l.shadow_blur, l.light_size, sh.x, sh.y, "towards the default camera" if sh.y > 0.0 else "away from the default camera"])
	quit(0)


func _count(n: Node, cls: String) -> int:
	var c := 1 if n.is_class(cls) else 0
	for ch in n.get_children():
		c += _count(ch, cls)
	return c
