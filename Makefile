GODOT := /Users/swozny/toolchain/godot-4.3-stable/Godot.app/Contents/MacOS/Godot
GODOT_PROJECT := godot/project.godot

.PHONY: run
run:
	"$(GODOT)" --path godot
