# Editor layout: four panes in nested splits, each one resizable by dragging the gap between panes, and each one
# can be shown or hidden.
#
#   Main (H):  Left (library / controls) | Rest
#   Rest (H):  Work | Right (inspector tabs)
#   Work:      TablePane | ViewPane   (side by side, or stacked with the table on top; Swap puts the 3D view first)
#
# The LayoutBar above the panes holds the toggles. Ctrl/Cmd+1..4 toggle Library, Table, 3D view and Inspector;
# Ctrl/Cmd+5 switches side by side / stacked. The layout (shown panes, split positions, stacking, order) is kept in
# settings.cfg [layout] and restored on the next start. A SplitContainer gives the whole space to its other child when
# one child is hidden, so hiding a pane hands its room to its neighbours.
extends RefCounted

const SETTINGS_PATH := "user://settings.cfg"
const PANES := ["left", "table", "view", "right"]
const LABELS := {"left": "Library", "table": "Table", "view": "3D view", "right": "Inspector"}
const TIPS := {
	"left": "Show or hide the garment library and controls (Ctrl+1)",
	"table": "Show or hide the cutting table (Ctrl+2)",
	"view": "Show or hide the 3D view (Ctrl+3)",
	"right": "Show or hide the inspector: outfit, piece, seams, body, fabric... (Ctrl+4)",
}
# split_offset defaults (design units). Main and Rest place their split from the fixed pane's minimum width (Left
# 200, Right 280), so these give a 260-wide library and a 340-wide inspector; Work splits from its middle.
const DEFAULTS := {"main": 60, "rest": -60, "work": 0, "stacked": false, "swapped": false,
	"left": true, "table": true, "view": true, "right": true}

# First run on this layout version (and after "Atelier" in the bar): the 3D view fills the window, the other panes are one click
# (or Ctrl+1..4) away. "Reset layout" brings back the classic arrangement above.
const ATELIER := {"main": 60, "rest": -60, "work": 0, "stacked": false, "swapped": false,
	"left": false, "table": false, "view": true, "right": false}
const VERSION := 2

var main: SplitContainer
var rest: SplitContainer
var work: SplitContainer
var panes := {}      # pane key -> Control
var buttons := {}    # pane key -> toggle Button
var stack_btn: Button
var swap_btn: Button
var state := {}
var on_change: Callable  # called after any change (the editor redraws the table, resizes the 3D view)


func setup(root: Control, bar: HBoxContainer, changed: Callable) -> void:
	main = root.get_node("%Main")
	rest = root.get_node("%Rest")
	work = root.get_node("%Work")
	panes = {"left": root.get_node("%Left"), "table": root.get_node("%TablePane"),
		"view": root.get_node("%ViewPane"), "right": root.get_node("%Right")}
	on_change = changed
	state = DEFAULTS.duplicate()
	_load()
	_build_bar(bar)
	for sc in [main, rest, work]:
		(sc as SplitContainer).dragged.connect(func(_o: int) -> void: _store_offsets())
	apply()


func _build_bar(bar: HBoxContainer) -> void:
	var title := Label.new()
	title.text = "Panes"
	title.add_theme_font_size_override("font_size", 12)
	title.modulate = Color(1, 1, 1, 0.6)
	bar.add_child(title)
	for k in PANES:
		var b := _small_button(LABELS[k], TIPS[k], true)
		b.name = "Pane_" + k
		var key: String = k
		b.toggled.connect(func(on: bool) -> void: set_shown(key, on))
		bar.add_child(b)
		buttons[k] = b
	bar.add_child(VSeparator.new())
	stack_btn = _small_button("Stacked", "Table above the 3D view instead of side by side (Ctrl+5)", true)
	stack_btn.name = "StackBtn"
	stack_btn.toggled.connect(func(on: bool) -> void: state["stacked"] = on; apply(); _save())
	bar.add_child(stack_btn)
	swap_btn = _small_button("Swap", "Put the 3D view before the table", true)
	swap_btn.name = "SwapBtn"
	swap_btn.toggled.connect(func(on: bool) -> void: state["swapped"] = on; apply(); _save())
	bar.add_child(swap_btn)
	var atelier := _small_button("Atelier", "The 3D view fills the window; the other panes stay one click away", false)
	atelier.name = "AtelierBtn"
	atelier.pressed.connect(func() -> void:
		state = ATELIER.duplicate()
		apply()
		_save())
	bar.add_child(atelier)
	var focus := _small_button("Focus 3D", "Only the 3D view (press again for the full layout)", false)
	focus.name = "Focus3DBtn"
	focus.pressed.connect(focus_view)
	bar.add_child(focus)
	var reset := _small_button("Reset layout", "Show every pane at its default size", false)
	reset.name = "ResetLayoutBtn"
	reset.pressed.connect(reset_layout)
	bar.add_child(reset)
	var fill := Control.new()
	fill.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar.add_child(fill)
	var hint := Label.new()
	hint.text = "drag the gaps between panes to resize"
	hint.add_theme_font_size_override("font_size", 11)
	hint.modulate = Color(1, 1, 1, 0.45)
	bar.add_child(hint)


func _small_button(text: String, tip: String, toggle: bool) -> Button:
	var b := Button.new()
	b.text = text
	b.tooltip_text = tip
	b.toggle_mode = toggle
	b.focus_mode = Control.FOCUS_NONE
	b.add_theme_font_size_override("font_size", 12)
	return b


func shown(k: String) -> bool:
	return bool(state.get(k, true))


# Shows or hides one pane. The table and the 3D view cannot both be hidden: hiding the last one shows the other.
func set_shown(k: String, on: bool) -> void:
	state[k] = on
	if not on and k in ["table", "view"] and not shown("table" if k == "view" else "view"):
		state["table" if k == "view" else "view"] = true
	apply()
	_save()


func toggle(k: String) -> void:
	set_shown(k, not shown(k))


# Only the 3D view; a second press (when only the 3D view is shown) brings every pane back.
func focus_view() -> void:
	var only_view := shown("view") and not shown("left") and not shown("table") and not shown("right")
	for k in PANES:
		state[k] = only_view or k == "view"
	apply()
	_save()


# Render mode (render_mode.gd): only the 3D view, for as long as the mode lasts. The saved layout is not touched, so leaving brings
# the panes back exactly as they were.
var _before_max := {}


func maximize() -> void:
	if not _before_max.is_empty():
		return
	_before_max = state.duplicate()
	for k in PANES:
		state[k] = k == "view"
	apply()


func restore() -> void:
	if _before_max.is_empty():
		return
	state = _before_max
	_before_max = {}
	apply()


func reset_layout() -> void:
	state = DEFAULTS.duplicate()
	apply()
	_save()


func apply() -> void:
	for k in PANES:
		(panes[k] as Control).visible = shown(k)
		if buttons.has(k):
			(buttons[k] as Button).set_pressed_no_signal(shown(k))
	work.vertical = bool(state["stacked"])
	var view: Control = panes["view"]
	work.move_child(view, 0 if bool(state["swapped"]) else 1)
	# the Rest split holds Work and Right: with both table and 3D view hidden Work would be empty (prevented above)
	main.split_offset = int(state["main"])
	rest.split_offset = int(state["rest"])
	work.split_offset = int(state["work"])
	if stack_btn != null:
		stack_btn.set_pressed_no_signal(bool(state["stacked"]))
		swap_btn.set_pressed_no_signal(bool(state["swapped"]))
	if on_change.is_valid():
		on_change.call()


func _store_offsets() -> void:
	state["main"] = main.split_offset
	state["rest"] = rest.split_offset
	state["work"] = work.split_offset
	_save()
	if on_change.is_valid():
		on_change.call()


# Ctrl/Cmd + 1..5. Returns true when the key was a layout shortcut.
func handle_key(e: InputEventKey) -> bool:
	match e.keycode:
		KEY_1:
			toggle("left")
		KEY_2:
			toggle("table")
		KEY_3:
			toggle("view")
		KEY_4:
			toggle("right")
		KEY_5:
			state["stacked"] = not bool(state["stacked"])
			apply()
			_save()
		_:
			return false
	return true


func _load() -> void:
	if OS.has_environment("FL_LAYOUT_DEFAULT"):
		return  # tests and screenshots start from the default layout
	var cf := ConfigFile.new()
	if cf.load(SETTINGS_PATH) != OK or int(cf.get_value("layout", "version", 1)) < VERSION:
		state = ATELIER.duplicate()   # a first run, or a layout saved before the atelier: start 3D first
		return
	for k in DEFAULTS.keys():
		var v = cf.get_value("layout", k, DEFAULTS[k])
		if typeof(v) == typeof(DEFAULTS[k]):
			state[k] = v
	if not shown("table") and not shown("view"):
		state["view"] = true


func _save() -> void:
	if OS.has_environment("FL_LAYOUT_DEFAULT") or not _before_max.is_empty():
		return
	var cf := ConfigFile.new()
	cf.load(SETTINGS_PATH)  # keep the other sections
	for k in state.keys():
		cf.set_value("layout", k, state[k])
	cf.set_value("layout", "version", VERSION)
	if cf.save(SETTINGS_PATH) != OK:
		push_warning("could not save %s" % SETTINGS_PATH)
