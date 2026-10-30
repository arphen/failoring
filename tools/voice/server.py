"""Voice for the Failoring editor: speech to text on this machine (LFM3-Audio, tools/voice/transcribe.py) and a coding
agent (opencode) that makes the change. The editor's Talk button (godot/voice.gd) records the microphone and talks to
this server over HTTP on localhost; a terminal works too (tools/voice/talk.py).

    ~/.cache/failoring-voice/bin/python -I tools/voice/server.py      # from the repo root; tools/voice/README.md

POST /transcribe   body: a WAV file             -> {"text"}
POST /ask          {"text"}                     -> {"job"}       the agent starts on the request
GET  /job/<id>                                  -> {"state": running|done|failed, "lines": [...], "reply"}
POST /reset                                     -> a new agent conversation from the next /ask on
GET  /health                                    -> {"ok", "model", "agent_model", "session"}

One agent conversation is kept (opencode --session), so "make it shorter" follows "make me a frilly dress". The agent
runs in the repo root, where AGENTS.md sends it to ./garment.
"""
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(os.environ.get("FL_AGENT_DIR", Path(__file__).resolve().parents[2]))   # where the agent works
PORT = int(os.environ.get("FL_VOICE_PORT", "47615"))
AGENT_MODEL = os.environ.get("FL_AGENT_MODEL", "opencode/glm-5.3-flash-free")
STATE = Path.home() / ".cache" / "failoring-voice" / "session.json"
# what the agent is told besides the user's words: short spoken-style answers, the toolkit, no code changes
BRIEF = ("You are the tailor's assistant for the Failoring editor; the user is speaking to you (transcribed speech, "
         "so read past small transcription errors). Garments are made and changed with ./garment as AGENTS.md and "
         "docs/GARMENT_RECIPES.md describe; finish every garment change with ./garment wear so the user sees it on the "
         "mannequin. Answer in two or three plain sentences: what you made or changed and what drape said. "
         "The user said: ")

sys.path.insert(0, str(Path(__file__).parent))
transcriber = None
jobs = {}
lock = threading.Lock()


def session_id():
    try:
        return json.loads(STATE.read_text()).get("session", "")
    except (OSError, ValueError):
        return ""


def set_session(sid):
    STATE.parent.mkdir(parents=True, exist_ok=True)
    STATE.write_text(json.dumps({"session": sid}))


def summarize(ev):
    """One short line per agent event for the editor's status (tool calls and text), or None."""
    p = ev.get("part", {})
    if ev.get("type") == "tool_use":
        inp = p.get("state", {}).get("input", {})
        what = inp.get("command") or inp.get("path") or inp.get("filePath") or inp.get("pattern") or ""
        return f"{p.get('tool', 'tool')}: {str(what)[:90]}"
    if ev.get("type") == "text":
        return p.get("text", "").strip()
    if ev.get("type") == "error":
        return "error: " + json.dumps(ev.get("error", {}))[:160]
    return None


def run_agent(job, text):
    sid = session_id()
    cmd = ["opencode", "run", "--auto", "--format", "json", "-m", AGENT_MODEL]
    if sid:
        cmd += ["--session", sid]
    else:
        cmd += ["--title", "failoring voice"]
    cmd.append((BRIEF if not sid else "The user said: ") + text)
    j = jobs[job]
    try:
        proc = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        texts = []
        for line in proc.stdout:
            try:
                ev = json.loads(line)
            except ValueError:
                continue
            if ev.get("sessionID") and not sid:
                sid = ev["sessionID"]
                set_session(sid)
            s = summarize(ev)
            if s:
                with lock:
                    j["lines"].append(s)
                if ev.get("type") == "text":
                    texts.append(s)
        proc.wait()
        with lock:
            j["reply"] = texts[-1] if texts else ""
            j["state"] = "done" if proc.returncode == 0 else "failed"
    except OSError as e:
        with lock:
            j["lines"].append(f"cannot start opencode: {e}")
            j["state"] = "failed"


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _body(self):
        return self.rfile.read(int(self.headers.get("Content-Length", "0")))

    def log_message(self, fmt, *args):
        pass

    def do_GET(self):
        if self.path == "/health":
            return self._send(200, {"ok": transcriber is not None, "model": "LiquidAI/LFM3-Audio-2B",
                                    "agent_model": AGENT_MODEL, "session": session_id()})
        if self.path.startswith("/job/"):
            j = jobs.get(self.path[5:])
            if j is None:
                return self._send(404, {"error": "no such job"})
            with lock:
                return self._send(200, dict(j))
        self._send(404, {"error": "unknown path"})

    def do_POST(self):
        if self.path == "/transcribe":
            if transcriber is None:
                return self._send(503, {"error": "the speech model is still loading"})
            with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
                f.write(self._body())
            t0 = time.time()
            try:
                text = transcriber(f.name)
            finally:
                os.unlink(f.name)
            print(f"heard ({time.time() - t0:.1f} s): {text}", flush=True)
            return self._send(200, {"text": text})
        if self.path == "/ask":
            text = json.loads(self._body() or b"{}").get("text", "").strip()
            if not text:
                return self._send(400, {"error": "empty request"})
            job = uuid.uuid4().hex[:8]
            jobs[job] = {"state": "running", "lines": [], "reply": "", "text": text}
            threading.Thread(target=run_agent, args=(job, text), daemon=True).start()
            print(f"agent job {job}: {text}", flush=True)
            return self._send(200, {"job": job})
        if self.path == "/reset":
            set_session("")
            return self._send(200, {"ok": True})
        self._send(404, {"error": "unknown path"})


def load():
    global transcriber
    from transcribe import Transcriber
    t0 = time.time()
    transcriber = Transcriber()
    print(f"speech model ready on {transcriber.dev} ({time.time() - t0:.0f} s)", flush=True)


if __name__ == "__main__":
    threading.Thread(target=load, daemon=True).start()
    print(f"voice server on http://127.0.0.1:{PORT} (agent: {AGENT_MODEL}, repo: {ROOT})", flush=True)
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
