extends RefCounted
# The calibration records of docs/CROCHET_PLAN.md section 10: a JSON array in user://crochet/swatches.json, one record per (yarn, hook,
# stitch). A record is what CrochetCalibration.fit() returns under "record" plus {yarn, hook_mm, stitch, date, notes} (SI units: w and h
# in metres, density kg/m^2, bendWale and bendCourse N*m, kWale and kCourse N/m, lock strains and ratio dimensionless, and the optional
# hinge: {blo: {bendScale, restAngleDeg}, ...}).

const DEFAULT_PATH := "user://crochet/swatches.json"

# FL_RECORDS overrides the file (tests, screenshots)
static func path() -> String:
	return OS.get_environment("FL_RECORDS") if OS.has_environment("FL_RECORDS") else DEFAULT_PATH

static func key_of(yarn: String, hook_mm: float, stitch: String) -> String:
	return "%s|%.2f|%s" % [yarn, hook_mm, stitch]

static func load_all(p: String) -> Array:
	if not FileAccess.file_exists(p):
		return []
	var parsed = JSON.parse_string(FileAccess.get_file_as_string(p))
	var out: Array = []
	if parsed is Array:
		for r in parsed:
			if r is Dictionary and r.has("yarn") and r.has("hook_mm"):
				out.append(r)
	return out

static func save_all(p: String, records: Array) -> bool:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(p).get_base_dir())
	var f := FileAccess.open(p, FileAccess.WRITE)
	if f == null:
		return false
	f.store_string(JSON.stringify(records, "  "))
	return true

# the record for this yarn on this hook (mm, already the real size: the yarn's own when the panel says 0), or {}
static func find(records: Array, yarn: String, hook_mm: float, stitch := "sc") -> Dictionary:
	var want := key_of(yarn, hook_mm, stitch)
	for r in records:
		if key_of(String(r["yarn"]), float(r["hook_mm"]), String(r.get("stitch", "sc"))) == want:
			return r
	return {}

# the records with this one added, or replacing the one for the same (yarn, hook, stitch); the hinge entries of the old one are kept
# when the new one has none (re-measuring the fabric does not throw the hinge fits away)
static func upsert(records: Array, rec: Dictionary) -> Array:
	var out: Array = []
	var replaced := false
	var want := key_of(String(rec["yarn"]), float(rec["hook_mm"]), String(rec.get("stitch", "sc")))
	for r in records:
		if key_of(String(r["yarn"]), float(r["hook_mm"]), String(r.get("stitch", "sc"))) == want:
			var merged: Dictionary = rec.duplicate(true)
			if not merged.has("hinge") and r.has("hinge"):
				merged["hinge"] = r["hinge"]
			out.append(merged)
			replaced = true
		else:
			out.append(r)
	if not replaced:
		out.append(rec)
	return out

static func remove(records: Array, yarn: String, hook_mm: float, stitch := "sc") -> Array:
	var want := key_of(yarn, hook_mm, stitch)
	var out: Array = []
	for r in records:
		if key_of(String(r["yarn"]), float(r["hook_mm"]), String(r.get("stitch", "sc"))) != want:
			out.append(r)
	return out
