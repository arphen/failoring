# Development aid (needs a real renderer, not --headless): runs the crochet scene for some frames and saves a screenshot of the window.
#   FL_SHOT_PATTERN=pouch FL_SHOT_SCENE=hang_pin FL_SHOT_LOAD=none FL_SHOT_FRAMES=450 FL_SHOT_OUT=/tmp/x.png \
#     godot --path godot res://tests/crochet_shot.tscn
extends Node
func _ready() -> void:
	var c = (load("res://crochet/crochet.tscn") as PackedScene).instantiate()
	add_child(c)
	await get_tree().process_frame
	var pat := OS.get_environment("FL_SHOT_PATTERN") if OS.has_environment("FL_SHOT_PATTERN") else "pouch"
	c._load_fixture_named(pat)
	c.compile_now()
	var scene := OS.get_environment("FL_SHOT_SCENE") if OS.has_environment("FL_SHOT_SCENE") else "hang_pin"
	var load_id := OS.get_environment("FL_SHOT_LOAD") if OS.has_environment("FL_SHOT_LOAD") else "none"
	if c.start_run(scene, load_id):
		var frames := int(OS.get_environment("FL_SHOT_FRAMES")) if OS.has_environment("FL_SHOT_FRAMES") else 450
		c.run_frames(frames)
		c._show_metrics()
	for i in 4:
		await get_tree().process_frame
	var out := OS.get_environment("FL_SHOT_OUT") if OS.has_environment("FL_SHOT_OUT") else "/tmp/crochet_shot.png"
	get_viewport().get_texture().get_image().save_png(out)
	print("wrote ", out, " phase ", c.sim.phase() if c.sim != null else "-")
	get_tree().quit()
