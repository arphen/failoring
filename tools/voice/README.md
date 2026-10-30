# Voice: talk to the garment agent

Speech is turned into text on this machine by Liquid AI's LFM3-Audio-2B (no cloud); the text goes to a coding agent
(opencode, a cheap model by default) that makes or changes garments with `./garment` (see `AGENTS.md`), and the editor
shows the result on the mannequin.

## Set up (once)

```sh
uv venv --python 3.12 ~/.cache/failoring-voice
VIRTUAL_ENV=~/.cache/failoring-voice uv pip install "transformers>=5.20.2" torch accelerate librosa soundfile sounddevice
```

The model (about 4 GB) downloads on the first start.

## Use

```sh
~/.cache/failoring-voice/bin/python -I tools/voice/server.py     # leave it running (loads in ~15 s)
```

In the editor: hold **V** over the 3D view while you speak (or click **Talk**, speak, click again). The bubble shows
what was heard, what the agent is doing, and its answer; the garment updates on the mannequin when the agent runs
`./garment wear`. The first time, macOS asks to let Godot use the microphone.

From a terminal instead: `~/.cache/failoring-voice/bin/python -I tools/voice/talk.py`.

One conversation is kept, so follow-ups work ("make it shorter", "now in silk"). `curl -X POST 127.0.0.1:47615/reset`
starts a new one. `FL_AGENT_MODEL=provider/model` picks the agent's model (`opencode models` lists them);
`FL_VOICE_PORT` the port (47615).
