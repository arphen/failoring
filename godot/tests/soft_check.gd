# Soft tissue check (soft_tissue.gd): walking the posed body for two cycles, the breasts move a few cm on curvy and less
# on athletic, the seat, thighs and arms a few mm, a 0 gain leaves the body untouched, and two runs give the same body.
#   godot --headless --path godot res://tests/soft_check.tscn
extends Node

const BodyGen = preload("res://body_gen.gd")
const PlanLoader = preload("res://plan_loader.gd")
const SoftTissue = preload("res://soft_tissue.gd")

var fails := 0


func _check(ok: bool, what: String) -> void:
	print("SOFT %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		fails += 1


# vertical peak-to-peak (m) of each region's offset over frames 65..128 of a walk, and the last body
func _walk(M: Dictionary, preset: String, gain: float) -> Dictionary:
	var meas: Dictionary = (BodyGen.PRESETS[preset] as Dictionary).duplicate()
	var rest = BodyGen.generate(M, meas, null)
	var stand = BodyGen.stand(M, rest)
	var soft = SoftTissue.new()
	soft.setup(M, meas, int(M["nb"]))
	soft.gain = gain
	var lo := {}
	var hi := {}
	var last := PackedVector3Array()
	var raw_last := PackedVector3Array()
	for k in range(1, 129):
		var s: float = minf(1.0, float(k) / 24.0)
		var pose := BodyGen.pose(M, rest, fposmod(float(k) / 64.0, 1.0), {"stride": s, "arms": s, "armAbduct": stand.get("armAbduct", BodyGen.ARM_ABDUCT)})
		var pp: PackedFloat32Array = pose["pos"]
		var raw := PackedVector3Array()
		for i in pp.size() / 3:
			raw.append(Vector3(pp[3 * i], pp[3 * i + 1], pp[3 * i + 2]))
		last = soft.apply(raw, true)
		raw_last = raw
		if k > 64:
			for r in soft.regions:
				var y: float = (r["x"] as Vector3).y * float(r["gain"]) * gain
				lo[r["name"]] = minf(float(lo.get(r["name"], 1e9)), y)
				hi[r["name"]] = maxf(float(hi.get(r["name"], -1e9)), y)
	var pp2 := {}
	for nm in lo.keys():
		pp2[nm] = float(hi[nm]) - float(lo[nm])
	return {"pp": pp2, "body": last, "raw": raw_last}


func _ready() -> void:
	var M := BodyGen.load_data(PlanLoader.load_json("res://assets/body_data.json"))
	var curvy := _walk(M, "curvy", 1.0)
	var ath := _walk(M, "athletic", 1.0)
	var bc: float = curvy["pp"].get("breast.L", 0.0)
	var ba: float = ath["pp"].get("breast.L", 0.0)
	_check(bc > 0.02 and bc < 0.08, "curvy breasts move a few cm vertically while walking (%.1f mm)" % (bc * 1000.0))
	_check(ba < bc, "athletic breasts move less than curvy (%.1f mm)" % (ba * 1000.0))
	for nm in ["seat.L", "thigh.L", "arm.L"]:
		var v: float = curvy["pp"].get(nm, 0.0)
		_check(v > 0.001 and v < 0.015, "%s moves a few mm (%.1f mm)" % [nm, v * 1000.0])
	var off := _walk(M, "curvy", 0.0)
	_check(off["body"] == off["raw"], "a 0 gain leaves the body as the skeleton posed it")
	var again := _walk(M, "curvy", 1.0)
	_check(again["body"] == curvy["body"], "the tissue is repeatable (same walk, same body)")
	print("SOFT %s" % ("PASSED" if fails == 0 else "FAILED with %d failure(s)" % fails))
	get_tree().quit(0 if fails == 0 else 1)
