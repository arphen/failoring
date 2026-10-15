#!/bin/sh
# Downloads MakeHuman's photographic skins (CC0 1.0, part of the MakeHuman 1.1 system assets) into
# godot/assets/skin/photo/, where the editor's Body tab lists them under Look > Skin ("Photo: ...").
# They are painted for MakeHuman's UV layout, which the mannequin uses (tools/export_body_uv.py).
#
# Usage (from the repo root):  tools/fetch_makehuman_skins.sh [name ...]
# Default: the young and middle-aged female skins. Names are the files without _diffuse.png, e.g. young_darkskinned_female.
# Each file is about 4 MB (2048 px). The folder is git-ignored; nothing here is committed.
set -eu
BASE=https://download.tuxfamily.org/makehuman/assets/1.1/base/skins/textures
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/godot/assets/skin/photo"
mkdir -p "$OUT"
if [ "$#" -eq 0 ]; then
	set -- young_lightskinned_female young_darkskinned_female middleage_lightskinned_female middleage_darkskinned_female
fi
for n in "$@"; do
	f="${n}_diffuse.png"
	echo "fetching $f"
	curl -fL --retry 2 -o "$OUT/$f" "$BASE/$f"
done
echo "done: $(ls "$OUT" | wc -l) skins in godot/assets/skin/photo (restart the editor to list new ones)"
