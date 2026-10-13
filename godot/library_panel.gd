# Library tab (left column): the garments the editor can put on the mannequin, each with a 2D preview of its pattern.
# Click a card to select it, double-click (or press Add to mannequin) to add it. The editor decides what adding does
# (signal add_requested). Saved garments can be renamed and deleted here; "Save current garment" stores the garment on
# the cutting table in user://library (signal save_requested, the editor has the working pieces).
extends VBoxContainer

const Library = preload("res://library.gd")
const GarmentPreview = preload("res://garment_preview.gd")

signal add_requested(id: String)
signal save_requested(category: String, name: String)

var _ed                         # editor: current_preset(), library_plan(id), current_garment_name()
var _filter: OptionButton
var _cards: VBoxContainer
var _add_btn: Button
var _selected := ""
var _card_of := {}              # id -> PanelContainer, for the selection highlight
var _save_name: LineEdit
var _save_cat: OptionButton
var _status: Label
var _rename_dlg: AcceptDialog
var _rename_edit: LineEdit
var _rename_file := ""
var _delete_dlg: ConfirmationDialog
var _delete_file := ""
var _preset := ""               # preset the cards were last drawn for


func setup(ed) -> void:
	_ed = ed
	add_theme_constant_override("separation", 6)
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	var head := Label.new()
	head.text = "Garment library"
	add_child(head)
	var row := HBoxContainer.new()
	_filter = OptionButton.new()
	_filter.name = "CategoryOpt"
	for c in Library.CATEGORIES:
		_filter.add_item(c)
	_filter.item_selected.connect(func(_i: int) -> void: _rebuild_cards())
	row.add_child(_filter)
	_add_btn = Button.new()
	_add_btn.name = "AddBtn"
	_add_btn.text = "Add to mannequin"
	_add_btn.disabled = true
	_add_btn.pressed.connect(_on_add_pressed)
	_add_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(_add_btn)
	add_child(row)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_cards = VBoxContainer.new()
	_cards.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_cards.add_theme_constant_override("separation", 6)
	scroll.add_child(_cards)
	add_child(scroll)
	add_child(HSeparator.new())
	var save_head := Label.new()
	save_head.text = "Save the garment on the table to the library"
	save_head.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	add_child(save_head)
	var srow := HBoxContainer.new()
	_save_name = LineEdit.new()
	_save_name.name = "SaveName"
	_save_name.placeholder_text = "Name"
	_save_name.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	srow.add_child(_save_name)
	_save_cat = OptionButton.new()
	_save_cat.name = "SaveCategory"
	for c in Library.CATEGORIES.slice(1):
		_save_cat.add_item(c)
	srow.add_child(_save_cat)
	add_child(srow)
	var save_btn := Button.new()
	save_btn.name = "SaveBtn"
	save_btn.text = "Save to library"
	save_btn.pressed.connect(_on_save_pressed)
	add_child(save_btn)
	_status = Label.new()
	_status.name = "LibraryStatus"
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_status.add_theme_font_size_override("font_size", 12)
	add_child(_status)
	_rename_dlg = AcceptDialog.new()
	_rename_dlg.title = "Rename garment"
	_rename_edit = LineEdit.new()
	_rename_dlg.add_child(_rename_edit)
	_rename_dlg.confirmed.connect(_on_rename_confirmed)
	add_child(_rename_dlg)
	_delete_dlg = ConfirmationDialog.new()
	_delete_dlg.title = "Delete garment"
	_delete_dlg.dialog_text = "Delete this saved garment? This cannot be undone."
	_delete_dlg.confirmed.connect(_on_delete_confirmed)
	add_child(_delete_dlg)


# Redraws the cards. Cheap enough to call after a body change: FreeSewing and drafted previews come from the editor's
# plan lookup, which uses the live body.
func refresh() -> void:
	if _ed == null:
		return
	_preset = _ed.current_preset()
	_rebuild_cards()
	_save_name.placeholder_text = "Name (%s)" % _ed.current_garment_name()


func set_status(text: String) -> void:
	_status.text = text


func _rebuild_cards() -> void:
	for c in _cards.get_children():
		_cards.remove_child(c)
		c.queue_free()
	_card_of.clear()
	var want: String = Library.CATEGORY_KEYS[_filter.selected]
	var shown := 0
	for it in Library.items(_preset):
		if want != "" and String(it["category"]) != want:
			continue
		_cards.add_child(_card(it, _ed.library_plan(String(it["id"]))))
		shown += 1
	if shown == 0:
		var none := Label.new()
		none.text = "No garments in this category yet."
		_cards.add_child(none)
	_update_selection()


func _card(it: Dictionary, plan: Dictionary) -> PanelContainer:
	var id := String(it["id"])
	var card := PanelContainer.new()
	card.mouse_filter = Control.MOUSE_FILTER_STOP
	card.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_card_of[id] = card
	var hb := HBoxContainer.new()
	hb.add_theme_constant_override("separation", 8)
	hb.mouse_filter = Control.MOUSE_FILTER_IGNORE
	card.add_child(hb)
	var prev := GarmentPreview.new()
	prev.custom_minimum_size = Vector2(88, 84)
	prev.mouse_filter = Control.MOUSE_FILTER_IGNORE
	prev.set_pieces(plan.get("pieces", []))
	hb.add_child(prev)
	var col := VBoxContainer.new()
	col.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	col.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hb.add_child(col)
	var name_l := Label.new()
	name_l.text = String(it["name"])
	name_l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	name_l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	col.add_child(name_l)
	var meta := Label.new()
	var fit: String = ("fitted: %s" % it["fitted"]) if String(it["fitted"]) != "" else "fixed outline"
	meta.text = "%s - %s" % [Library.category_name(String(it["category"])), fit]
	meta.add_theme_font_size_override("font_size", 12)
	meta.mouse_filter = Control.MOUSE_FILTER_IGNORE
	col.add_child(meta)
	var btns := HBoxContainer.new()
	var add := Button.new()
	add.text = "Add"
	add.pressed.connect(func() -> void: _select(id); add_requested.emit(id))
	btns.add_child(add)
	if bool(it["kind"] == "user"):
		var rn := Button.new()
		rn.text = "Rename"
		rn.pressed.connect(func() -> void: _open_rename(String(it["user_file"]), String(it["name"])))
		btns.add_child(rn)
		var dl := Button.new()
		dl.text = "Delete"
		dl.pressed.connect(func() -> void: _delete_file = String(it["user_file"]); _delete_dlg.popup_centered())
		btns.add_child(dl)
	col.add_child(btns)
	card.gui_input.connect(func(e: InputEvent) -> void:
		var mb := e as InputEventMouseButton
		if mb != null and mb.pressed and mb.button_index == MOUSE_BUTTON_LEFT:
			if mb.double_click:
				add_requested.emit(id)
			else:
				_select(id))
	return card


func _select(id: String) -> void:
	_selected = id
	_update_selection()


func _update_selection() -> void:
	for id in _card_of.keys():
		var card: PanelContainer = _card_of[id]
		card.self_modulate = Color(0.62, 0.82, 1.0) if id == _selected else Color(1, 1, 1)
	_add_btn.disabled = _selected == ""


func _on_add_pressed() -> void:
	if _selected != "":
		add_requested.emit(_selected)


func _on_save_pressed() -> void:
	var nm := _save_name.text.strip_edges()
	save_requested.emit(Library.CATEGORY_KEYS[_save_cat.selected + 1], nm)


func _open_rename(file: String, current: String) -> void:
	_rename_file = file
	_rename_edit.text = current
	_rename_dlg.popup_centered(Vector2i(360, 120))
	_rename_edit.grab_focus()


func _on_rename_confirmed() -> void:
	var nm := _rename_edit.text.strip_edges()
	if nm == "":
		return
	if Library.rename(_rename_file, nm):
		set_status("Renamed to %s." % nm)
	else:
		set_status("Could not rename that garment.")
	refresh()


func _on_delete_confirmed() -> void:
	var id := "user:" + _delete_file
	if Library.delete(_delete_file):
		set_status("Deleted.")
		if _selected == id:
			_selected = ""
	else:
		set_status("Could not delete that garment.")
	refresh()
