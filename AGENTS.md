# Agents working on Failoring

Failoring is a sewing simulator: flat pattern pieces are sewn together and draped as cloth on a body (Godot 4.3 editor
in `godot/`, native solver in `godot/bin/libfailoring.dylib`, already built).

## Making or changing a garment

Use `./garment`; do not read `godot/editor.gd` or the solver for this. The guide is `docs/GARMENT_RECIPES.md`
(read it first, it is short), working examples are in `garments/`.

1. Write a recipe `garments/<name>.json` (copy an example; sizes in metres, expressions over the body like `"waist * 0.55"`).
2. `./garment check garments/<name>.json` until there is no ERROR.
3. `./garment drape garments/<name>.json` (quick, coarse) until it says OK.
4. `./garment shots garments/<name>.json` (the real drape): check the `hangs` lines put each piece where you meant, and
   look at the printed `sheet.png`: does it look like what was asked?
5. `./garment wear garments/<name>.json`: the user's editor shows it on the mannequin.

To change a garment the user is wearing, edit its recipe in `garments/` and run `./garment wear` again: the editor
reloads it. `wear` prints where the version it replaced was kept: copy that back to undo. Report what the garment is (pieces, fabric, lengths) and what `drape` said.

## Rules

- Do not change Godot code (`godot/*.gd`) to make a garment. If the tool cannot express something, say what is missing.
- Do not rebuild the native library or run the full test suite; `./garment` is the check.
- Godot: `/Users/swozny/toolchain/godot-4.3-stable/Godot.app/Contents/MacOS/Godot` (`./garment` finds it).
- Do not commit `godot/bin/libfailoring.dylib`.
