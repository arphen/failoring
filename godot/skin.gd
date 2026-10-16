# Skin data for the drawn mannequin: per-vertex attributes for shaders/skin.gdshader and the anchors that keep the
# eyeballs (eyes.gd) in their sockets. Look only: nothing here reaches the solver or the collision body.
#
# Everything is computed once from the MakeHuman base mesh (body_data.json "base", the rest shape before any morph),
# so the pores, freckles and face marks stick to the same spot of skin on every body preset and in every pose.
#
#   UV      = MakeHuman's texture coordinates (assets/body_uv.json, tools/export_body_uv.py): region masks and photo skins
#   CUSTOM0 = rest position x, y, z (metres; +x is the body's left, +y up, +z the front) and the cavity (-1 ridge ..
#             1 crease; per-vertex concavity of the rest mesh)
#   COLOR   = r redness (knees, elbows, hands, feet, ears), g thin (ears, fingers, toes: light passes through),
#             b areola (the body's nipple masks), a nails (dorsal fingertips and toe tips)
#   CUSTOM1 = the four bones that skin the vertex (indices into the rig's bone list), CUSTOM2 = their weights: the idle
#             layer (idle.gd) moves the body in the vertex shader, as a skin on top of whatever pose is drawn
#   CUSTOM3 = how much the vertex breathes: x ribcage, y belly (rest-space windows on the trunk), z, w unused
# MakeHuman's UV map has seams, so the drawn mesh has more vertices than the body: "drawn" maps them back (src).
#
# Face features (lips, brows, scalp hair, cheeks, nose) are drawn per pixel in the shader from the rest position,
# with the landmarks below (measured on the base mesh; see docs/LOOK_NOTES.md).
extends RefCounted

# Rest-space landmarks of the base mesh (metres). The eye centres are the l-eye / r-eye joint helpers.
const MOUTH := Vector3(0.0, 0.6058, 0.1375)     # centre of the closed mouth line, on the midline
const MOUTH_HALF_W := 0.0225                    # mouth corner, |x|
const NOSE_TIP := Vector3(0.0, 0.6370, 0.1518)
const SCALP_C := Vector3(0.0, 0.672, 0.030)     # centre of the skull sphere used for the hairline

# eye sockets: vertices of the lid margin are within this distance of the eye centre and in front of it
const LID_R := 0.0135
const LID_FRONT := 0.003
const EYE_R := 0.0106                           # eyeball radius (the lid margin is at 11.0 to 12.7 mm)


static func _v(p: PackedFloat32Array, i: int) -> Vector3:
	return Vector3(p[3 * i], p[3 * i + 1], p[3 * i + 2])


static func joint_rest(m: Dictionary, name: String) -> Vector3:
	var base: PackedFloat32Array = m["base"]
	var ids: Array = (m["joints"] as Dictionary).get(name, [])
	var c := Vector3.ZERO
	if ids.is_empty():
		return c
	for i in ids:
		c += _v(base, int(i))
	return c / float(ids.size())


# Returns {uv, uv2, color, eyes, n}. Cached in m["skin"] (it depends only on the base mesh).
static func build(m: Dictionary) -> Dictionary:
	if m.has("skin"):
		return m["skin"]
	var nb: int = int(m["nb"])
	var base: PackedFloat32Array = m["base"]
	var tris: PackedInt32Array = m["tris"]
	var part: PackedInt32Array = m["part"]
	var parts: Array = m["parts"]
	var pid := {}
	for k in parts.size():
		pid[parts[k]] = k

	# rest positions, area-weighted normals, one-ring neighbours
	var P := PackedVector3Array()
	P.resize(nb)
	for i in nb:
		P[i] = _v(base, i)
	var N := PackedVector3Array()
	N.resize(nb)
	var nbr: Array = []
	nbr.resize(nb)
	for i in nb:
		nbr[i] = []
	for t in range(0, tris.size(), 3):
		var a := tris[t]
		var b := tris[t + 1]
		var c := tris[t + 2]
		if a >= nb or b >= nb or c >= nb:
			continue
		var fn := (P[b] - P[a]).cross(P[c] - P[a])
		N[a] += fn
		N[b] += fn
		N[c] += fn
		for e in [[a, b], [b, c], [c, a]]:
			var x: int = e[0]
			var y: int = e[1]
			if not (nbr[x] as Array).has(y):
				(nbr[x] as Array).append(y)
			if not (nbr[y] as Array).has(x):
				(nbr[y] as Array).append(x)
	for i in nb:
		N[i] = N[i].normalized()

	# cavity: how far the neighbourhood's mean lies along the normal, at two scales (one ring, and the shape smoothed
	# six times for folds such as the ear, the armpit and between the fingers). Positive in creases.
	var S := P.duplicate()
	var cav_small := PackedFloat32Array()
	cav_small.resize(nb)
	var edge := PackedFloat32Array()
	edge.resize(nb)
	for i in nb:
		var ring: Array = nbr[i]
		if ring.is_empty():
			continue
		var mean := Vector3.ZERO
		var el := 0.0
		for j in ring:
			mean += P[j]
			el += P[j].distance_to(P[i])
		mean /= float(ring.size())
		el /= float(ring.size())
		edge[i] = maxf(el, 1e-5)
		cav_small[i] = (mean - P[i]).dot(N[i]) / edge[i]
	for it in 6:
		var S2 := S.duplicate()
		for i in nb:
			var ring: Array = nbr[i]
			if ring.is_empty():
				continue
			var mean := Vector3.ZERO
			for j in ring:
				mean += S[j]
			S2[i] = S[i].lerp(mean / float(ring.size()), 0.7)
		S = S2

	# joints used by the colour masks
	var J := {}
	for jn in ["l-knee", "r-knee", "l-elbow", "r-elbow", "l-hand", "r-hand", "l-ankle", "r-ankle", "l-toe", "r-toe", "l-eye", "r-eye"]:
		J[jn] = joint_rest(m, jn)
	var areola := PackedFloat32Array()
	areola.resize(nb)
	var masks: Dictionary = m.get("masks", {})
	for mk in ["nipple.L", "nipple.R"]:
		if not masks.has(mk):
			continue
		var pair: Array = masks[mk]
		var ids: Array = pair[0]
		var ws: Array = pair[1]
		for k in ids.size():
			var vi := int(ids[k])
			if vi < nb:
				areola[vi] = maxf(areola[vi], float(ws[k]))

	# cavity per vertex, then smoothed over the one ring three times: on the coarse limbs the raw measure follows the
	# mesh's edge loops and drew lines down the legs
	var cavs := PackedFloat32Array()
	cavs.resize(nb)
	for i in nb:
		var cl := cav_small[i] * 2.5
		var cb := (S[i] - P[i]).dot(N[i]) / edge[i] * 0.5
		cavs[i] = clampf(0.6 * cl + 0.6 * cb, -1.0, 1.0)
	for it in 3:
		var c2 := cavs.duplicate()
		for i in nb:
			var ring: Array = nbr[i]
			if ring.is_empty():
				continue
			var acc := 0.0
			for j in ring:
				acc += cavs[j]
			c2[i] = 0.5 * cavs[i] + 0.5 * acc / float(ring.size())
		cavs = c2

	var uv := PackedVector2Array()
	var uv2 := PackedVector2Array()
	var col := PackedColorArray()
	uv.resize(nb)
	uv2.resize(nb)
	col.resize(nb)
	var p_head: int = pid.get("head", -1)
	var hands := [pid.get("hand.L", -1), pid.get("hand.R", -1)]
	var feet := [pid.get("foot.L", -1), pid.get("foot.R", -1)]
	for i in nb:
		var p := P[i]
		var n := N[i]
		# both measures are relative to the local edge length: the base mesh is fine where it has small folds (ears,
		# eyelids, fingers) and coarse on the limbs, so a crease shows at the scale it is modelled at
		var cav := cavs[i]
		uv[i] = Vector2(p.x, p.y)
		uv2[i] = Vector2(p.z, cav)

		var pr := part[i]
		var red := 0.0
		var thin := 0.0
		var nail := 0.0
		# knees: the kneecap, in front of the joint
		for s in ["l", "r"]:
			var kn: Vector3 = J[s + "-knee"]
			var dk := p.distance_to(kn + Vector3(0.0, 0.0, 0.03))
			red = maxf(red, 0.55 * exp(-pow(dk / 0.045, 2.0)) * smoothstep(-0.2, 0.6, n.z))
			var el: Vector3 = J[s + "-elbow"]
			var de := p.distance_to(el)
			red = maxf(red, 0.5 * exp(-pow(de / 0.04, 2.0)) * smoothstep(-0.1, 0.6, -n.z))
		if hands.has(pr):
			var hj: Vector3 = J["l-hand" if p.x > 0.0 else "r-hand"]
			var t := clampf(p.distance_to(hj) / 0.19, 0.0, 1.0)
			red = maxf(red, 0.32 + 0.35 * smoothstep(0.55, 1.0, t))
			thin = smoothstep(0.45, 0.9, t)
		elif feet.has(pr):
			var tj: Vector3 = J["l-toe" if p.x > 0.0 else "r-toe"]
			var aj: Vector3 = J["l-ankle" if p.x > 0.0 else "r-ankle"]
			var dt := p.distance_to(tj)
			red = maxf(red, 0.3 + 0.3 * exp(-pow(dt / 0.04, 2.0)) + 0.25 * smoothstep(0.02, -0.05, p.z - aj.z))
			thin = 0.4 * exp(-pow(dt / 0.04, 2.0))
		elif pr == p_head and absf(p.x) > 0.06:
			# ears
			var e := smoothstep(0.06, 0.07, absf(p.x))
			red = maxf(red, 0.55 * e)
			thin = maxf(thin, e)
		nail = _nail(p, n, pr, hands, feet, J)
		col[i] = Color(clampf(red, 0.0, 1.0), clampf(thin, 0.0, 1.0), clampf(areola[i], 0.0, 1.0), nail)

	var bones4 := PackedFloat32Array()
	var w4 := PackedFloat32Array()
	var br := PackedFloat32Array()
	bones4.resize(4 * nb)
	w4.resize(4 * nb)
	br.resize(4 * nb)
	var si: PackedInt32Array = m.get("skinIdx", PackedInt32Array())
	var sw: PackedFloat32Array = m.get("skinW", PackedFloat32Array())
	var p_trunk: int = pid.get("trunk", -1)
	for i in nb:
		var wsum := 0.0
		for k in 4:
			if si.size() >= 4 * nb:
				bones4[4 * i + k] = float(si[4 * i + k])
				w4[4 * i + k] = sw[4 * i + k]
				wsum += sw[4 * i + k]
		if wsum > 1.0e-6:
			for k in 4:
				w4[4 * i + k] /= wsum
		else:
			w4[4 * i] = 1.0
		# breathing: the trunk only (the arms hang beside it), from the ribcage to the belly, fading out towards the
		# armpits; the ribs move most at the sides and front, the belly at the front
		var q := P[i]
		if part[i] == p_trunk:
			var side := 1.0 - smoothstep(0.11, 0.17, absf(q.x))
			var chest := smoothstep(0.24, 0.31, q.y) * (1.0 - smoothstep(0.46, 0.52, q.y))
			var belly := smoothstep(0.02, 0.08, q.y) * (1.0 - smoothstep(0.19, 0.26, q.y))
			var front := 0.35 + 0.65 * smoothstep(-0.02, 0.05, q.z)
			br[4 * i] = chest * (0.5 + 0.5 * side) * front
			br[4 * i + 1] = belly * side * smoothstep(-0.01, 0.06, q.z)
	var out := {"rest_xy": uv, "rest_z_cav": uv2, "color": col, "n": nb, "eyes": eye_anchors(m, P, J),
		"drawn": drawn(nb, uv, uv2, col, m["tris"], bones4, w4, br),
		"bones": m.get("bones", [])}
	m["skin"] = out
	return out


# Nails: the back of the last 1.4 cm of each finger and the top of the toe tips. A fingertip is a hand vertex that is
# nearly as far from the wrist as anything within 2 cm of it; the back of the hand faces away from the body (A-pose).
static func _nail(p: Vector3, n: Vector3, pr: int, hands: Array, feet: Array, J: Dictionary) -> float:
	if hands.has(pr):
		var hj: Vector3 = J["l-hand" if p.x > 0.0 else "r-hand"]
		var d := p.distance_to(hj)
		if d < 0.12:
			return 0.0
		var out_dir := Vector3(signf(p.x), 0.0, 0.0)
		var axis := (p - hj).normalized()
		# fingers point down and out in the rest pose; the nail faces the back of the hand (lateral) or, for the thumb,
		# forwards. Use the part of the normal across the finger axis.
		var nn := (n - axis * n.dot(axis)).normalized()
		var dorsal := maxf(nn.dot(out_dir), 0.0)
		var thumb := maxf(nn.z, 0.0) * smoothstep(0.0, 0.03, p.z - hj.z)
		var tip := smoothstep(0.155, 0.185, d) if p.z - hj.z < 0.03 else smoothstep(0.12, 0.14, d)
		return clampf(tip * smoothstep(0.35, 0.8, maxf(dorsal, thumb)), 0.0, 1.0)
	if feet.has(pr):
		var tj: Vector3 = J["l-toe" if p.x > 0.0 else "r-toe"]
		var ahead := p.z - tj.z
		return clampf(smoothstep(0.035, 0.05, ahead) * smoothstep(0.3, 0.8, n.y), 0.0, 1.0)
	return 0.0


# Eye anchors: for each eye, the lid-margin vertices (their centroid follows the socket), a forehead group and the
# opposite socket (they fix the head's rotation), and the eye centre's offset from the lid centroid in that head frame.
static func eye_anchors(m: Dictionary, P: PackedVector3Array, J: Dictionary) -> Dictionary:
	var out := {}
	var fore := PackedInt32Array()
	var fc := Vector3(0.0, 0.705, 0.125)
	for i in P.size():
		if P[i].distance_to(fc) < 0.015 and P[i].z > fc.z - 0.01:
			fore.append(i)
	var rings := {}
	for s in ["l", "r"]:
		var e: Vector3 = J[s + "-eye"]
		var ring := PackedInt32Array()
		for i in P.size():
			var d := P[i] - e
			if d.length() < LID_R and d.z > LID_FRONT:
				ring.append(i)
		rings[s] = ring
	out["fore"] = fore
	for s in ["l", "r"]:
		var e: Vector3 = J[s + "-eye"]
		out[s] = {"ring": rings[s], "center": e}
	out["radius"] = EYE_R
	# rest frame
	var fr := head_frame(P, rings["l"], rings["r"], fore)
	for s in ["l", "r"]:
		var lc := centroid(P, rings[s])
		var e: Vector3 = J[s + "-eye"]
		out[s]["off"] = (fr["basis"] as Basis).inverse() * (e - lc) / float(fr["scale"])
	out["rest_scale"] = fr["scale"]
	out["rest_basis"] = fr["basis"]
	return out


# The drawn mesh: one vertex per (body vertex, MakeHuman UV) pair, from assets/body_uv.json; without that file, the
# body's own vertices with no texture coordinates.
const UV_PATH := "res://assets/body_uv.json"

static func drawn(nb: int, rest_xy: PackedVector2Array, rest_z_cav: PackedVector2Array, col: PackedColorArray, tris: PackedInt32Array,
		bones4: PackedFloat32Array = PackedFloat32Array(), w4: PackedFloat32Array = PackedFloat32Array(), br: PackedFloat32Array = PackedFloat32Array()) -> Dictionary:
	var src := PackedInt32Array()
	var uv := PackedVector2Array()
	var dt := PackedInt32Array()
	var data = null
	if FileAccess.file_exists(UV_PATH):
		data = JSON.parse_string(FileAccess.get_file_as_string(UV_PATH))
	if data is Dictionary and int(data.get("nb", -1)) == nb and (data["tris"] as Array).size() == tris.size():
		src = PackedInt32Array(data["src"])
		var flat := PackedFloat32Array(data["uv"])
		uv.resize(src.size())
		for i in src.size():
			uv[i] = Vector2(flat[2 * i], 1.0 - flat[2 * i + 1])   # OBJ v runs up the image, Godot's down
		dt = PackedInt32Array(data["tris"])
	else:
		src.resize(nb)
		for i in nb:
			src[i] = i
		uv.resize(nb)
		dt = tris.duplicate()
	var c0 := PackedFloat32Array()
	c0.resize(4 * src.size())
	var dc := PackedColorArray()
	dc.resize(src.size())
	var has_rig := bones4.size() >= 4 * nb
	var c1 := PackedFloat32Array()
	var c2 := PackedFloat32Array()
	var c3 := PackedFloat32Array()
	if has_rig:
		c1.resize(4 * src.size())
		c2.resize(4 * src.size())
		c3.resize(4 * src.size())
	for i in src.size():
		var s := src[i]
		c0[4 * i] = rest_xy[s].x
		c0[4 * i + 1] = rest_xy[s].y
		c0[4 * i + 2] = rest_z_cav[s].x
		c0[4 * i + 3] = rest_z_cav[s].y
		dc[i] = col[s]
		if has_rig:
			for k in 4:
				c1[4 * i + k] = bones4[4 * s + k]
				c2[4 * i + k] = w4[4 * s + k]
				c3[4 * i + k] = br[4 * s + k]
	return {"src": src, "uv": uv, "custom0": c0, "custom1": c1, "custom2": c2, "custom3": c3, "color": dc, "tris": dt, "textured": data is Dictionary}


static func centroid(P: PackedVector3Array, ids: PackedInt32Array) -> Vector3:
	var c := Vector3.ZERO
	for i in ids:
		c += P[i]
	return c / maxf(1.0, float(ids.size()))


# Head frame from the two lid rings and the forehead: x from the right to the left socket, y up through the forehead,
# z forward. scale is the distance between the sockets (the eyes grow with the head).
static func head_frame(P: PackedVector3Array, ring_l: PackedInt32Array, ring_r: PackedInt32Array, fore: PackedInt32Array) -> Dictionary:
	var a := centroid(P, ring_l)
	var b := centroid(P, ring_r)
	var f := centroid(P, fore)
	var ex := (a - b)
	var sc := ex.length()
	ex = ex / maxf(sc, 1e-6)
	var mid := (a + b) * 0.5
	var t := f - mid
	var ey := (t - ex * t.dot(ex)).normalized()
	var ez := ex.cross(ey)
	return {"basis": Basis(ex, ey, ez), "scale": sc}
