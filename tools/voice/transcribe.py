"""Speech to text with Liquid AI's LFM3-Audio-2B, on this machine (no cloud).

    ~/.cache/failoring-voice/bin/python -I tools/voice/transcribe.py clip.wav [more.wav ...]

Prints one transcript per file. The model (about 4 GB) downloads to the Hugging Face cache on first use. Runs on the
Apple GPU (mps) when there is one, else the CPU. tools/voice/README.md sets up the venv.
"""
import sys
import time

import librosa
import soundfile as sf
import torch
from transformers import AutoModelForMultimodalLM, AutoProcessor

MODEL = "LiquidAI/LFM3-Audio-2B"
RATE = 16000


def device():
    if torch.cuda.is_available():
        return "cuda", torch.bfloat16
    if torch.backends.mps.is_available():
        return "mps", torch.float16
    return "cpu", torch.float32


class Transcriber:
    def __init__(self):
        self.dev, dtype = device()
        self.model = AutoModelForMultimodalLM.from_pretrained(MODEL, dtype=dtype, device_map=self.dev,
                                                              trust_remote_code=True).eval()
        self.proc = AutoProcessor.from_pretrained(MODEL, trust_remote_code=True)

    def __call__(self, wav_path, language="English"):
        # the model takes 16 kHz mono; resample whatever the recorder gave
        audio, sr = sf.read(wav_path, dtype="float32", always_2d=True)
        audio = audio.mean(axis=1)
        if sr != RATE:
            audio = librosa.resample(audio, orig_sr=sr, target_sr=RATE)
        conv = [{"role": "system", "content": f"Perform ASR in {language}." if language else "Perform ASR."},
                {"role": "user", "content": [{"type": "audio", "audio": audio}]}]
        inputs = self.proc.apply_chat_template(conv, add_generation_prompt=True, tokenize=True, return_dict=True,
                                               return_tensors="pt").to(self.dev)
        with torch.no_grad():
            out = self.model.generate(**inputs, max_new_tokens=1024, suppress_mixed=True, suppress_audio_out=True,
                                      return_dict_in_generate=True)
        return self.proc.decode(out.sequences[0, inputs["input_ids"].shape[1]:], skip_special_tokens=True).strip()


if __name__ == "__main__":
    t0 = time.time()
    tr = Transcriber()
    print(f"[loaded {MODEL} on {tr.dev} in {time.time() - t0:.1f} s]", file=sys.stderr)
    for p in sys.argv[1:]:
        t0 = time.time()
        print(tr(p))
        print(f"[{p}: {time.time() - t0:.1f} s]", file=sys.stderr)
