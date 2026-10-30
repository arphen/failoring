#!/usr/bin/env python3
"""A feminised variant of a face likeness, roughly as after a few years of feminising HRT, with a friendlier resting
expression (tools/face/build_face.sh builds the face it starts from).

    python -I tools/face/soften.py <body_data.json> <body_uv.json> <face-dir> <out face-dir> [strength] [label] [smile] [delight]

HRT changes soft tissue, not bone: fat moves into the upper cheeks, the jaw softens (smaller masseters), the lips fill a
little, the skin gets smoother and the beard shadow fades. The brow ridge, nose and chin bone stay, so this leaves them.

Reads a built face (face_shape.json, face_skin.png, face.json, and work/fitted_front.json + work/fitted_landmarks.json,
the fitted head's render and its landmarks) and writes a second face folder that face.gd lists next to it.
Shape (offsets added to the fitted ones, Gaussian falloff round landmark vertices, mirrored): fuller upper cheeks, the
jaw's angle softened inwards, lips a little fuller, mouth corners up and brows lifted and apart (a resting frown pulls
them down and together). Skin (the baked texture, per texel by its point on the head): smoother, the beard shadow
evened out to the cheeks' colour, lips rosier, a blush on the cheeks, the shadow under the eyes and between the brows
lifted, the brows a little lighter.
The skin is in UV space, so it follows the shape. strength (default 1) scales everything.

delight (default 0, 1 full) takes the photo's own lighting out of the skin first: selfies under ceiling lamps bake dark
eye sockets, shadows under the eyes and the nose and a lit nose ridge into the texture, and the studio lamps then shade
the head a second time (it read as hollow-eyed and harsh). The shading is the skin's lightness at the scale of a few
millimetres to a couple of centimetres (a blur of 4 mm less one of 40 mm, in the head's own millimetres); the brows,
eyes, lips and nostrils are left out of that estimate (their darkness is the feature, not a shadow), so the shading
under them is the surroundings'. The photo's eye, larger than the lid opening, is painted out of the lids, and a soft line runs along the
upper edge of the opening (lashes are not modelled).
"""
import json
import os
import shutil
import sys

import cv2
import numpy as np

SIZE = 4096
MAX_SNAP_PX = 10.0
MM = 0.001

# (landmarks of one side; the mirrored one is found by x), (out, up, forward) in mm, sigma in mm
SHAPE = [
    ((61,), (0.4, 3.5, -0.8), 6.0),                 # mouth corners up
    ((0, 37), (0.0, 0.4, 0.8), 6.0),                # upper lip fuller
    ((17, 84), (0.0, -0.3, 0.6), 6.0),              # lower lip fuller
    ((107, 55), (1.0, 2.8, 0.0), 7.0),              # inner brows up and apart: no frown
    ((105, 52), (0.0, 3.2, 0.0), 7.0),              # brow arch up
    ((70, 46), (0.0, 1.2, 0.0), 7.0),               # outer brows
    ((50,), (0.3, 0.8, 2.4), 14.0),                 # fuller upper cheeks (fat redistribution)
    ((205,), (0.0, 0.3, 1.2), 12.0),
    ((172,), (-1.8, 0.6, 0.0), 14.0),               # softer jaw angle (smaller masseter)
    ((136,), (-1.2, 0.4, 0.0), 12.0),
]
# the resting expression, scaled by `smile` (argument 7, default 1): a closed-mouth smile (corners up, back and wider, the
# cheeks lifted with them), and the lids of a smiling look (face.json "lids", skin.gdshader lid_rest: the lower lids
# raised; the upper ones stay)
SMILE = [
    ((61,), (1.4, 3.5, -1.8), 6.5),
    ((205,), (0.3, 1.8, 0.4), 11.0),
    ((50,), (0.0, 1.2, 0.0), 12.0),
]
LIDS = (0.0, 0.08)   # the upper lid: even 0.02 hid the top of the iris (the eye centre is the mannequin's, skin.gdshader eye_c)
LIDS_OPEN = (-0.04, -0.03)   # with delight: the lids a little more open than the photo's (it was taken with tired, narrowed eyes)
EYE_R = [33, 7, 163, 144, 145, 153, 154, 155, 133, 173, 157, 158, 159, 160, 161, 246]
EYE_L = [263, 249, 390, 373, 374, 380, 381, 382, 362, 398, 384, 385, 386, 387, 388, 466]
LIPS_OUTER = [61, 185, 40, 39, 37, 0, 267, 269, 270, 409, 291, 375, 321, 405, 314, 17, 84, 181, 91, 146]
BROW_R = [70, 63, 105, 66, 107, 55, 65, 52, 53, 46]
BROW_L = [300, 293, 334, 296, 336, 285, 295, 282, 283, 276]


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        sys.exit(1)
    body_path, uv_path, fdir, out = sys.argv[1:5]
    k = float(sys.argv[5]) if len(sys.argv) > 5 else 1.0
    label = sys.argv[6] if len(sys.argv) > 6 else 'Mine (HRT, 3 years)'
    smile = float(sys.argv[7]) if len(sys.argv) > 7 else 1.0
    delight = float(sys.argv[8]) if len(sys.argv) > 8 else 0.0
    body = json.load(open(body_path))
    base = np.array(body['base'], dtype=np.float64).reshape(-1, 3)
    nb = int(body['nb'])
    fs = json.load(open(f'{fdir}/face_shape.json'))
    off = np.zeros_like(base)
    off[np.array(fs['idx'])] = np.array(fs['d']).reshape(-1, 3)
    P = (base + off)[:nb]   # the fitted head
    proj = json.load(open(f'{fdir}/work/fitted_front.json'))
    fl = np.array(list(json.load(open(f'{fdir}/work/fitted_landmarks.json')).values())[0]['landmarks'])[:468]
    pv = np.array([[int(kk)] + v for kk, v in proj['verts'].items()])
    vid, vxy = pv[:, 0].astype(int), pv[:, 1:3]
    lm = {}
    for i in range(468):
        d = np.hypot(*(vxy - fl[i, :2]).T)
        j = int(np.argmin(d))
        if d[j] <= MAX_SNAP_PX:
            lm[i] = P[vid[j]]
    # the mirror of a landmark: the tied point nearest its mirrored position
    pts = np.array(list(lm.values()))
    keys = list(lm.keys())

    def mirror(i):
        q = lm[i] * np.array([-1.0, 1.0, 1.0])
        return keys[int(np.argmin(np.linalg.norm(pts - q, axis=1)))]

    # 1. shape
    disp = np.zeros_like(P)
    for ids, (o, u, f), sig, kk in [e + (k,) for e in SHAPE] + [e + (smile,) for e in SMILE]:
        for i in ids:
            if i not in lm:
                continue
            for c in {i, mirror(i)}:
                p = lm[c]
                side = 1.0 if p[0] >= 0 else -1.0
                if abs(p[0]) < 0.003:
                    side = 0.0          # on the midline: no sideways push
                dv = np.array([o * side, u, f]) * MM * kk
                w = np.exp(-((P - p) ** 2).sum(1) / (2 * (sig * MM) ** 2))
                disp += w[:, None] * dv
    # the eyelids keep their shape: eyes.gd places the eyeballs from the lid margins, so lifting them (the cheeks, the
    # brows) pushed the eyes up under the upper lids; the lids' expression is the shader's (LIDS)
    for a_, b_ in ((33, 133), (263, 362)):
        if a_ in lm and b_ in lm:
            dist = np.linalg.norm(P - 0.5 * (lm[a_] + lm[b_]), axis=1)
            t = np.clip((dist - 0.010) / 0.008, 0, 1)
            disp *= (t * t * (3 - 2 * t))[:, None]
    moved = np.linalg.norm(disp, axis=1) > 1e-6
    off[:nb][moved] += disp[moved]
    idx = sorted(set(fs['idx']) | set(np.nonzero(moved)[0].tolist()))
    os.makedirs(out, exist_ok=True)
    shape = dict(fs)
    shape['idx'] = idx
    shape['d'] = off[idx].round(6).ravel().tolist()
    shape['report'] = dict(fs.get('report', {}), soften=k, soften_max_mm=round(float(np.linalg.norm(disp, axis=1).max() / MM), 2))
    json.dump(shape, open(f'{out}/face_shape.json', 'w'))

    # 2. skin: each covered texel's point on the fitted head
    img = cv2.imread(f'{fdir}/face_skin.png', cv2.IMREAD_UNCHANGED).astype(np.float32)
    uvd = json.load(open(uv_path))
    src = np.array(uvd['src'])
    uv = np.array(uvd['uv']).reshape(-1, 2)
    tris = np.array(uvd['tris']).reshape(-1, 3)
    uvp = np.stack([uv[:, 0] * SIZE, (1.0 - uv[:, 1]) * SIZE], 1)
    pos = np.full((SIZE, SIZE, 3), np.nan, np.float32)
    alpha = img[:, :, 3]
    for t in tris:
        b = src[t]
        if (b >= nb).any():
            continue
        q = uvp[t]
        x0, y0 = np.floor(q.min(0)).astype(int)
        x1, y1 = np.ceil(q.max(0)).astype(int)
        x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, SIZE - 1), min(y1, SIZE - 1)
        if x1 < x0 or y1 < y0 or alpha[y0:y1 + 1, x0:x1 + 1].max() <= 0:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        m = np.array([[q[1, 0] - q[0, 0], q[2, 0] - q[0, 0]], [q[1, 1] - q[0, 1], q[2, 1] - q[0, 1]]])
        if abs(np.linalg.det(m)) < 1e-9:
            continue
        rel = np.stack([xs - q[0, 0], ys - q[0, 1]], -1) @ np.linalg.inv(m).T
        lam = np.stack([1 - rel[..., 0] - rel[..., 1], rel[..., 0], rel[..., 1]], -1)
        inside = (lam >= -1e-3).all(-1)
        if inside.any():
            pos[ys[inside].astype(int), xs[inside].astype(int)] = lam[inside] @ P[b]
    have = ~np.isnan(pos[:, :, 0])
    X = pos[have].astype(np.float64)

    def blob(ids, sx, sy, dy=0.0):
        w = np.zeros(len(X))
        for i in ids:
            if i not in lm:
                continue
            for c in {i, mirror(i)}:
                p = lm[c] + np.array([0.0, dy * MM, 0.0])
                r = ((X[:, 0] - p[0]) / (sx * MM)) ** 2 + ((X[:, 1] - p[1]) / (sy * MM)) ** 2 + ((X[:, 2] - p[2]) / (sx * MM)) ** 2
                w = np.maximum(w, np.exp(-0.5 * r))
        return w

    # polygons in the front view (x, y), 0.2 mm pixels, feathered
    lo = X[:, :2].min(0)
    res = 0.0002
    gw, gh = (np.ceil((X[:, :2].max(0) - lo) / res) + 2).astype(int)

    def poly(ids, feather_mm, z_front):
        g = np.zeros((gh, gw), np.float32)
        p = np.array([lm[i][:2] for i in ids if i in lm])
        cv2.fillPoly(g, [np.round((p - lo) / res).astype(np.int32)], 1.0)
        g = cv2.GaussianBlur(g, (0, 0), feather_mm * MM / res)
        gx = np.clip(((X[:, 0] - lo[0]) / res).astype(int), 0, gw - 1)
        gy = np.clip(((X[:, 1] - lo[1]) / res).astype(int), 0, gh - 1)
        return g[gy, gx] * (X[:, 2] > z_front)

    lip_z = min(lm[i][2] for i in LIPS_OUTER if i in lm) - 0.008
    brow_z = min(lm[i][2] for i in BROW_R + BROW_L if i in lm) - 0.008
    lips = poly(LIPS_OUTER, 0.8, lip_z)
    brows = np.maximum(poly(BROW_R, 1.2, brow_z), poly(BROW_L, 1.2, brow_z))
    blush = blob((205,), 13.0, 10.0)
    under = blob((145,), 14.0, 7.0, dy=-9.0)
    glab = blob((168,), 6.0, 7.0, dy=4.0)
    beard = blob((164, 152, 148, 176, 150, 136), 11.0, 11.0) * (1 - lips)

    if delight > 0:
        img = _delight(img, have, X, lm, poly, blob, lo, res, gw, gh, delight)

    # smoother skin: a fraction of an edge-keeping blur (pores and stubble fade, brows and lips keep their edges)
    rgb8 = img[:, :, :3].clip(0, 255).astype(np.uint8)
    smooth = cv2.bilateralFilter(rgb8, 9, 18, 6).astype(np.float32)
    img[:, :, :3] = img[:, :, :3] + 0.45 * k * (smooth - img[:, :, :3])
    bgr = img[:, :, :3][have].reshape(-1, 1, 3).clip(0, 255).astype(np.uint8)
    lab = cv2.cvtColor(bgr, cv2.COLOR_BGR2LAB).reshape(-1, 3).astype(np.float32)
    L, A, B = lab[:, 0], lab[:, 1], lab[:, 2]
    cheek = blush > 0.5
    ref = np.median(L[cheek]), np.median(A[cheek]), np.median(B[cheek])
    bw = np.clip(0.5 * k * beard, 0, 1)      # the beard shadow half way to the cheeks' colour
    L += bw * (ref[0] - L)
    A += bw * (ref[1] - A)
    B += bw * (ref[2] - B)
    A += k * (14.0 * lips + 6.0 * blush * (1 - lips))
    B += k * (2.0 * lips - 1.0 * blush)
    L += k * (-3.0 * lips + 15.0 * under + 7.0 * glab)
    L += k * 0.35 * brows * np.maximum(0.0, ref[0] - L)   # brows a third of the way to the skin
    lab = np.stack([L, A, B], 1).clip(0, 255).astype(np.uint8).reshape(-1, 1, 3)
    img[:, :, :3][have] = cv2.cvtColor(lab, cv2.COLOR_LAB2BGR).reshape(-1, 3)
    cv2.imwrite(f'{out}/face_skin.png', img.clip(0, 255).astype(np.uint8))

    info = json.load(open(f'{fdir}/face.json'))
    info['label'] = label
    lo = LIDS_OPEN if delight > 0 else (0.0, 0.0)
    info['lids'] = [round(LIDS[0] * smile + lo[0], 3), round(LIDS[1] * smile + lo[1], 3)]
    info['from'] = os.path.basename(os.path.normpath(fdir))
    json.dump(info, open(f'{out}/face.json', 'w'), indent=1)
    if os.path.isfile(f'{fdir}/hair.glb'):
        shutil.copy(f'{fdir}/hair.glb', out)
    print(f'{out}: {len(idx)} vertices, shape change up to {shape["report"]["soften_max_mm"]} mm; '
          f'skin: lips {int((lips > 0.5).sum())}, blush {int((blush > 0.5).sum())} texels')


def _band(L, w, sig_px):
    """Weighted blur: the mean of L under weights w (normalised convolution), so excluded texels do not count."""
    num = cv2.GaussianBlur(L * w, (0, 0), sig_px)
    den = cv2.GaussianBlur(w, (0, 0), sig_px)
    return num / np.maximum(den, 1e-4)


def _delight(img, have, X, lm, poly, blob, lo, res, gw, gh, amount):
    # texel size: the median distance on the head between neighbouring covered texels
    ys, xs = np.nonzero(have)
    pos = np.full(have.shape + (3,), np.nan, np.float32)
    pos[have] = X
    step = np.linalg.norm(pos[:, 1:] - pos[:, :-1], axis=2)
    mm_px = float(np.nanmedian(step)) / MM
    # features whose darkness is their own
    eye_z = min(lm[i][2] for i in EYE_R + EYE_L if i in lm) - 0.012
    feat = np.maximum.reduce([
        poly(BROW_R, 2.0, eye_z), poly(BROW_L, 2.0, eye_z),
        poly(EYE_R, 2.5, eye_z), poly(EYE_L, 2.5, eye_z),
        poly(LIPS_OUTER, 1.5, eye_z - 0.03),
        blob((98,), 4.5, 3.5, dy=-1.5)])
    H, W = have.shape
    w = np.zeros((H, W), np.float32)
    f = np.zeros((H, W), np.float32)
    f[have] = feat
    w[have] = 1.0 - np.clip(feat * 1.5, 0, 1)
    rgb8 = img[:, :, :3].clip(0, 255).astype(np.uint8)
    lab = cv2.cvtColor(rgb8, cv2.COLOR_BGR2LAB).astype(np.float32)
    L = lab[:, :, 0]
    near = _band(L, w, 4.0 / mm_px)
    far = _band(L, w, 40.0 / mm_px)
    shade = near - far
    # shadows go fully, highlights (the photo's lit ridge) most of the way; a little under the features too
    corr = np.where(shade < 0, shade, 0.8 * shade) * amount
    L2 = L - corr
    lab[:, :, 0] = np.where(have, L2, L).clip(0, 255)
    out = img.copy()
    out[:, :, :3] = np.where(have[:, :, None], cv2.cvtColor(lab.astype(np.uint8), cv2.COLOR_LAB2BGR).astype(np.float32), img[:, :, :3])
    # the eyes: the photo's eye (white and iris) is bigger than the head's lid opening, so it lay on the lids round the real
    # eyeball as a pale rim. Inside the photo's eye outline the lids get the skin round them (a weighted blur that leaves
    # the eye out), and a soft lash line runs along the upper edge of the opening.
    eyes = np.zeros((H, W), np.float32)
    eyes[have] = np.maximum(poly(EYE_R, 0.6, eye_z), poly(EYE_L, 0.6, eye_z))
    ring = have.astype(np.float32) * (1.0 - np.clip(eyes * 3.0, 0, 1))
    fill = np.stack([_band(out[:, :, c], ring, 2.5 / mm_px) for c in range(3)], 2)
    e = np.clip(eyes * 1.4, 0, 1)[:, :, None] * amount
    out[:, :, :3] = np.where(have[:, :, None], out[:, :, :3] * (1 - e) + fill * e, out[:, :, :3])
    hole_d = cv2.distanceTransform(have.astype(np.uint8), cv2.DIST_L2, 5) * mm_px      # mm to the nearest uncovered texel
    cy = {}
    for name, ids in (('r', EYE_R), ('l', EYE_L)):
        q = np.array([lm[i] for i in ids if i in lm])
        cy[name] = q.mean(0)
    up = np.zeros((H, W), np.float32)
    above = np.zeros(len(X), np.float32)
    for c in cy.values():
        dd = np.hypot(X[:, 0] - c[0], X[:, 1] - c[1])
        sel = (dd < 0.022) & (X[:, 2] > eye_z)
        above = np.maximum(above, sel * np.clip((X[:, 1] - c[1] + 0.0005) / 0.0015, 0, 1))
    up[have] = above
    lash = up * np.exp(-0.5 * (hole_d / 0.6) ** 2) * (hole_d > 0)
    o8 = out[:, :, :3].clip(0, 255).astype(np.uint8)
    lab2 = cv2.cvtColor(o8, cv2.COLOR_BGR2LAB).astype(np.float32)
    lab2[:, :, 0] -= 60.0 * np.clip(lash, 0, 1) * amount
    out[:, :, :3] = np.where(have[:, :, None], cv2.cvtColor(lab2.clip(0, 255).astype(np.uint8), cv2.COLOR_LAB2BGR).astype(np.float32), out[:, :, :3])
    print(f'delight: {mm_px:.3f} mm per texel, shading {float(np.percentile(shade[have], 2)):.1f} .. '
          f'{float(np.percentile(shade[have], 98)):.1f} L')
    return out


if __name__ == '__main__':
    main()
