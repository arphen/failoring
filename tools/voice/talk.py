"""Talk to the garment agent from a terminal (the editor's Talk button does the same): Enter starts recording, Enter
again sends it. Needs tools/voice/server.py running. Type a line instead of pressing Enter to send text.

    ~/.cache/failoring-voice/bin/python -I tools/voice/talk.py
"""
import io
import json
import time
import urllib.request

import numpy as np
import sounddevice as sd
import soundfile as sf

URL = "http://127.0.0.1:47615"
RATE = 16000


def post(path, data, ctype):
    req = urllib.request.Request(URL + path, data=data, headers={"Content-Type": ctype}, method="POST")
    return json.loads(urllib.request.urlopen(req, timeout=300).read())


def record():
    chunks = []
    with sd.InputStream(samplerate=RATE, channels=1, dtype="float32", callback=lambda d, *_: chunks.append(d.copy())):
        input("  listening... Enter to send ")
    buf = io.BytesIO()
    sf.write(buf, np.concatenate(chunks) if chunks else np.zeros((1, 1), "float32"), RATE, format="WAV")
    return buf.getvalue()


def main():
    while True:
        line = input("\nEnter to talk, or type: ").strip()
        if line in ("q", "quit", "exit"):
            return
        text = line or post("/transcribe", record(), "audio/wav").get("text", "")
        print(f"  you: {text}")
        if not text:
            continue
        job = post("/ask", json.dumps({"text": text}).encode(), "application/json")["job"]
        seen = 0
        while True:
            j = json.loads(urllib.request.urlopen(f"{URL}/job/{job}", timeout=30).read())
            for ln in j["lines"][seen:]:
                print("  . " + ln.splitlines()[0][:140] if ln else "")
            seen = len(j["lines"])
            if j["state"] != "running":
                print(f"\n  agent: {j['reply']}")
                break
            time.sleep(1.0)


if __name__ == "__main__":
    main()
