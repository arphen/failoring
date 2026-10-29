#!/bin/sh
# Builds a face likeness from photos: the head's shape, its skin, iris and hair colour, into
# ~/Documents/Failoring/faces/<id>/ (face.gd loads it; Body tab > Look > Face / Hair / Eyes).
#
#   tools/face/build_face.sh <id> <front photo> [<photo with the hair down>] [more photos...]
#
# The front photo: straight on, neutral face, hair off the forehead, even light (it is the shape and the skin). The
# photo with the hair down gives the hair colour. HEIC, JPEG or PNG. Everything stays on this Mac: the photos are
# copied into the face folder's work/ directory and nothing is uploaded (the landmark model is downloaded once).
#
# Steps: Python 3.12 environment with MediaPipe in ~/.cache/failoring-face (uv); landmarks of the photos
# (landmarks.py); the mannequin's head rendered and measured (godot/tests/face_capture.gd); the shape fitted and refined
# once (fit_shape.py; a second refinement only adds the detector's noise, see there); the skin baked (bake_skin.py).
# Needs: uv, Godot 4.3 (GODOT=path, default the toolchain's), a window (the head renders are GPU renders).
set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GODOT="${GODOT:-/Users/swozny/toolchain/godot-4.3-stable/Godot.app/Contents/MacOS/Godot}"
VENV="$HOME/.cache/failoring-face"
PY="$VENV/bin/python"
MODEL="$VENV/models/face_landmarker.task"

[ $# -ge 2 ] || { sed -n '2,8p' "$0"; exit 1; }
ID="$1"; shift
FRONT="$1"; shift
HAIR="${1:-}"
OUT="$HOME/Documents/Failoring/faces/$ID"
WORK="$OUT/work"
mkdir -p "$WORK/photos"

if [ ! -x "$PY" ]; then
	uv venv --python 3.12 "$VENV"
	VIRTUAL_ENV="$VENV" uv pip install mediapipe opencv-python-headless numpy scipy
fi
if [ ! -f "$MODEL" ]; then
	mkdir -p "$(dirname "$MODEL")"
	curl -fsSL -o "$MODEL" https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task
fi

name() { basename "$1" | sed 's/\.[^.]*$//'; }
for f in "$FRONT" "$@"; do
	sips -s format jpeg "$f" --out "$WORK/photos/$(name "$f").jpg" >/dev/null
done
F="$(name "$FRONT")"
H="${HAIR:+$(name "$HAIR")}"

cd "$ROOT"
"$PY" -I tools/face/landmarks.py "$WORK/photo_landmarks.json" "$WORK/overlays" "$WORK"/photos/*.jpg
"$GODOT" --path godot --script res://tests/face_capture.gd -- "$WORK/mannequin_front.png" "$WORK/mannequin_front.json"
"$PY" -I tools/face/landmarks.py "$WORK/mannequin_landmarks.json" "$WORK/overlays" "$WORK/mannequin_front.png"

capture() {
	FL_FACES_DIR="$(dirname "$OUT")" FL_FACE="$ID" "$GODOT" --path godot --script res://tests/face_capture.gd -- "$WORK/fitted_front.png" "$WORK/fitted_front.json"
	"$PY" -I tools/face/landmarks.py "$WORK/fitted_landmarks.json" "$WORK/overlays" "$WORK/fitted_front.png"
	"$PY" -I tools/face/eval_fit.py "$WORK/photo_landmarks.json" "$F" "$WORK/mannequin_landmarks.json" "$WORK/fitted_landmarks.json"
}
"$PY" -I tools/face/fit_shape.py godot/assets/body_data.json "$WORK" "$F" "$OUT/face_shape.json"
capture
"$PY" -I tools/face/fit_shape.py godot/assets/body_data.json "$WORK" "$F" "$OUT/face_shape_new.json" "$OUT/face_shape.json" fitted_front
mv "$OUT/face_shape_new.json" "$OUT/face_shape.json"
capture
"$PY" -I tools/face/bake_skin.py godot/assets/body_data.json godot/assets/body_uv.json "$WORK" "$F" "$OUT" $H
echo "face '$ID' written to $OUT (Body tab > Look > Face)"
