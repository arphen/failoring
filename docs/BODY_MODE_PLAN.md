# Body mode (2026-10-30)

Goal (from the user): turn the Body tab into an interactive mode on the mannequin: click parts of the body and be guided
through each measurement, with a tape that wraps round the body and a reading that lights up when it is taken; previews
for hair and the other looks; character-editor previews for the presets.

## What it is

`godot/body_mode.gd`, entered with the 3D view's **Body** button, **B**, or "Open body mode" on the Body tab; B, Esc or Done
leaves. The 3D view takes the whole window (as render mode), the garment is hidden, the mannequin stands still (the idle
sway is off so the tape stays on the skin; head, eyes and breath go on). A card on the right has three pages; the camera
frames the subject in the space left of it (`h_offset`).

* **Measure**: a dot on the body per measurement (green: yours, amber: preset or estimate). Click a dot or a row: the camera
  glides there, a tailor's tape (`shaders/tape.gdshader`, cm ticks, red every 10 cm, a metal tip) unrolls along the
  measurement, the card shows how to take it and a tailor's tip. Step the value (0.1 cm, -/+ 0.5) and **Use this** (Enter):
  it is saved to the measurements file, the body is generated again, the reading glows. **Guided measuring** goes through
  the eleven in a tailor's order (height, shoulder, neck, bust, waist, hip, back, arm, upper arm, thigh, inseam), skipping
  those already taken. Optional ones can go back to **Estimate**.
* **Shape**: the six presets as figures (each preset generated on a worker thread once, splatted front view, same scale),
  and the body-type sliders.
* **Look**: faces (thumbnails from each likeness's fitted render), hair, eyes and skin swatches. Pointing at a swatch tries
  it on; clicking keeps it. Faces apply on click (they regenerate the body). The camera takes a portrait lens.

## Tape geometry

From the standing body (`body_stand`): girths are the convex hull of the section where `BodyGen.measure` takes them (bust,
waist, hip, thigh, neck tilted with the neck, upper arm across the arm), lifted 4 mm and running 4 cm past the start.
Lengths: height on the wall behind the heels; shoulder across the top of the back; back length down the spine; arm over the
outside of the arm from the shoulder tip by the elbow to the wrist; inseam down the inside of the left leg.

## Also in this change

* Face: `tools/face/soften.py` takes a `delight` argument: the photo's ceiling light (dark sockets, under-eye shadow, the lit
  nose ridge) is taken out of the baked skin, the photo's eye (larger than the lid opening) is painted out of the lids, and
  a soft lash line runs along the upper lid edge; the lids rest a little more open. Lips are less glossy (skin.gdshader).
* Camera: closer than 0.6 m the editor camera stays at 0.6 m and narrows the lens instead (a camera 30 cm from a face draws
  a selfie's big nose).

Check: `godot/tests/body_mode_shot.tscn` (windowed; FL_MEAS_CSV to a scratch file).
