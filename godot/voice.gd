# Talk to the garment agent: the Talk button in the view bar (or hold V over the 3D view) records the microphone, the
# local voice server (tools/voice/server.py) turns it into text with LFM3-Audio on this machine and hands it to the
# coding agent, which changes the garment with ./garment wear; the editor then reloads it (editor.gd
# _poll_agent_garments). A bubble over the 3D view shows what was heard, what the agent is doing and its answer.
extends Node

const URL := "http://127.0.0.1:47615"
const BUS := "VoiceRecord"
const HELP := "Voice is off: start the voice server (tools/voice/README.md):\n  ~/.cache/failoring-voice/bin/python -I tools/voice/server.py"

var ed: Node
var btn: Button
var _bubble: PanelContainer
var _text: Label
var _rec: AudioEffectRecord
var _mic: AudioStreamPlayer
var recording := false
var _job := ""
var _poll := 0.0
var _hide_at := 0.0
var _http: HTTPRequest
var _busy := false


func setup(editor: Node, bar: Control, view_pane: Control) -> void:
	ed = editor
	btn = Button.new()
	btn.name = "TalkBtn"
	btn.text = "Talk"
	btn.toggle_mode = true
	btn.focus_mode = Control.FOCUS_NONE
	btn.tooltip_text = "Tell the tailor's assistant what to make or change (hold V over the view, or click to start and again to send). Needs the voice server: tools/voice/README.md"
	btn.toggled.connect(func(on: bool) -> void:
		if on:
			start()
		else:
			stop())
	bar.add_child(btn)
	_bubble = PanelContainer.new()
	_bubble.name = "VoiceBubble"
	_bubble.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_bubble.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_LEFT)
	_bubble.position = Vector2(16, -140)
	_bubble.custom_minimum_size = Vector2(420, 0)
	_bubble.visible = false
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.08, 0.08, 0.1, 0.82)
	sb.set_corner_radius_all(10)
	sb.set_content_margin_all(12)
	_bubble.add_theme_stylebox_override("panel", sb)
	_text = Label.new()
	_text.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_text.custom_minimum_size = Vector2(400, 0)
	_bubble.add_child(_text)
	view_pane.add_child(_bubble)
	_http = HTTPRequest.new()
	_http.timeout = 120.0
	add_child(_http)
	# the microphone goes to a muted bus whose record effect keeps it
	var bi := AudioServer.get_bus_index(BUS)
	if bi < 0:
		AudioServer.add_bus()
		bi = AudioServer.bus_count - 1
		AudioServer.set_bus_name(bi, BUS)
		AudioServer.set_bus_mute(bi, true)
		AudioServer.add_bus_effect(bi, AudioEffectRecord.new())
	_rec = AudioServer.get_bus_effect(bi, 0)
	_mic = AudioStreamPlayer.new()
	_mic.stream = AudioStreamMicrophone.new()
	_mic.bus = BUS
	add_child(_mic)


func say(t: String, hold: float = 0.0) -> void:
	_text.text = t
	_bubble.visible = true
	_bubble.position.y = _bubble.get_parent().size.y - _bubble.size.y - 70.0
	_hide_at = Time.get_ticks_msec() / 1000.0 + hold if hold > 0.0 else 0.0


func start() -> void:
	if recording or _busy:
		btn.set_pressed_no_signal(recording)
		return
	recording = true
	btn.set_pressed_no_signal(true)
	btn.text = "Listening..."
	_mic.play()
	_rec.set_recording_active(true)
	say("Listening... (let go of V, or click Talk, to send)")


func stop() -> void:
	if not recording:
		return
	recording = false
	btn.set_pressed_no_signal(false)
	btn.text = "Talk"
	_rec.set_recording_active(false)
	_mic.stop()
	var wav: AudioStreamWAV = _rec.get_recording()
	if wav == null or wav.data.size() < 8000:
		say("Too short - hold V while you speak.", 3.0)
		return
	var path := "user://voice_clip.wav"
	wav.save_to_wav(path)
	var bytes := FileAccess.get_file_as_bytes(path)
	_busy = true
	say("Transcribing...")
	var err := _http.request_raw(URL + "/transcribe", ["Content-Type: audio/wav"], HTTPClient.METHOD_POST, bytes)
	if err != OK:
		_busy = false
		say(HELP, 8.0)
		return
	var res: Array = await _http.request_completed
	var j = _json(res)
	if j == null or String(j.get("text", "")) == "":
		_busy = false
		say(String(j.get("error", "")) if j != null and j.has("error") else HELP, 8.0)
		return
	var heard := String(j["text"])
	say("You: %s\nThinking..." % heard)
	_http.request(URL + "/ask", ["Content-Type: application/json"], HTTPClient.METHOD_POST, JSON.stringify({"text": heard}))
	res = await _http.request_completed
	j = _json(res)
	_busy = false
	if j == null or not j.has("job"):
		say("You: %s\nThe agent did not start: %s" % [heard, HELP], 8.0)
		return
	_job = String(j["job"])
	_poll = 0.5


func _json(res: Array):
	if int(res[0]) != HTTPRequest.RESULT_SUCCESS:
		return null
	var v = JSON.parse_string((res[3] as PackedByteArray).get_string_from_utf8())
	return v if v is Dictionary else null


# Hold V over the view to talk (editor.gd passes key events here first).
func key(e: InputEventKey) -> bool:
	if e.keycode != KEY_V or e.echo or e.ctrl_pressed or e.meta_pressed:
		return false
	if e.pressed:
		start()
	else:
		stop()
	return true


func _process(delta: float) -> void:
	if _hide_at > 0.0 and Time.get_ticks_msec() / 1000.0 > _hide_at:
		_hide_at = 0.0
		_bubble.visible = false
	if _job == "" or _busy:
		return
	_poll -= delta
	if _poll > 0.0:
		return
	_poll = 1.0
	_busy = true
	_http.request(URL + "/job/" + _job)
	var res: Array = await _http.request_completed
	_busy = false
	var j = _json(res)
	if j == null:
		return
	var lines: Array = j.get("lines", [])
	var head := "You: %s" % String(j.get("text", ""))
	if String(j.get("state", "")) == "running":
		say("%s\n%s" % [head, String(lines[-1]) if not lines.is_empty() else "Thinking..."])
	else:
		_job = ""
		var reply := String(j.get("reply", ""))
		say("%s\n%s" % [head, reply if reply != "" else ("Done." if String(j["state"]) == "done" else "The agent stopped: " + (String(lines[-1]) if not lines.is_empty() else "?"))], 20.0)
