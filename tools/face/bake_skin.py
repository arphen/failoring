#!/usr/bin/env python3
"""Face skin of the likeness, baked from a front photo into MakeHuman's UV layout (tools/face/build_face.sh).

    python -I tools/face/bake_skin.py <body_data.json> <body_uv.json> <work-dir> <photo name> <face-dir> [<hair photo name>]

Inputs: face_shape.json in <face-dir> (fit_shape.py), and in the work directory photo_landmarks.json, fitted_front.json
and fitted_landmarks.json (the fitted head rendered by face_capture.gd and its landmarks), and photos/<photo name>.jpg.
  1. Camera of the photo: the photo's landmarks tied to vertices of the fitted head (as fit_shape.py ties them), then
     OpenCV solvePnP with a phone front camera's focal length (0.9 x the long side).
  2. The photo's broad shading (ceiling light: bright forehead, dark under the brows and the chin) is divided out:
     luminance over a blur of 6 % of the face width, times its mean; brows, lashes, lips and pores stay.
  3. Every head triangle is rasterised in UV space (4096 px); each texel takes the photo's colour where the camera sees
     that point of the fitted head, alpha by how squarely it faces the camera (full up to 45 degrees off the view
     direction, none from 80; at 20-50 the cheeks and the forehead were left uncovered), and only on the face (below the hairline landmark, in front of the ears).
Output in <face-dir>: face_skin.png (RGBA, MakeHuman UV, alpha = cover) and face.json (skin, iris and hair colours,
sRGB 0..1, sampled from the photos). The hair colour comes from <hair photo name> (a front photo with the hair down:
patches beside the face at cheek height, the median of their middle brightness), or from above the forehead.
"""
import json
import os
import sys

import cv2
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # (run with python -I)
from fit_shape import INNER_LIPS  # noqa: E402

SIZE = 4096
MAX_SNAP_PX = 10.0


def main():
    if len(sys.argv) < 6:
        print(__doc__)
        sys.exit(1)
    body_path, uv_path, work, photo, fdir = sys.argv[1:6]
    body = json.load(open(body_path))
    base = np.array(body['base'], dtype=np.float64).reshape(-1, 3)
    nb = int(body['nb'])
    fs = json.load(open(f'{fdir}/face_shape.json'))
    base[np.array(fs['idx'])] += np.array(fs['d']).reshape(-1, 3)
    uvd = json.load(open(uv_path))
    src = np.array(uvd['src'])
    uv = np.array(uvd['uv']).reshape(-1, 2)
    tris = np.array(uvd['tris']).reshape(-1, 3)
    img = cv2.imread(f'{work}/photos/{photo}.jpg')
    h, w = img.shape[:2]
    pl = json.load(open(f'{work}/photo_landmarks.json'))[photo]['landmarks']
    pl = np.array(pl)[:468]
    proj = json.load(open(f'{work}/fitted_front.json'))
    fl = np.array(list(json.load(open(f'{work}/fitted_landmarks.json')).values())[0]['landmarks'])[:468]

    # 1. photo camera
    pv = np.array([[int(k)] + v for k, v in proj['verts'].items()])
    vid, vxy = pv[:, 0].astype(int), pv[:, 1:3]
    obj, imgp = [], []
    for i in range(468):
        if i in INNER_LIPS:
            continue
        d = np.hypot(*(vxy - fl[i, :2]).T)
        j = int(np.argmin(d))
        if d[j] <= MAX_SNAP_PX:
            obj.append(base[vid[j]])
            imgp.append(pl[i, :2])
    obj, imgp = np.array(obj), np.array(imgp)
    f = 0.9 * max(w, h)
    K = np.array([[f, 0, w / 2], [0, f, h / 2], [0, 0, 1]], dtype=np.float64)
    ok, rvec, tvec = cv2.solvePnP(obj, imgp, K, None, flags=cv2.SOLVEPNP_ITERATIVE)
    rep, _ = cv2.projectPoints(obj, rvec, tvec, K, None)
    err = np.linalg.norm(rep[:, 0] - imgp, axis=1)
    R, _ = cv2.Rodrigues(rvec)
    cam = (-R.T @ tvec).ravel()
    face_w = np.linalg.norm(pl[234, :2] - pl[454, :2])
    print(f'camera: {len(obj)} points, reprojection {err.mean():.1f} px mean, {err.max():.1f} max; face {face_w:.0f} px wide')

    # 2. flatten the broad shading
    lab = cv2.cvtColor(img, cv2.COLOR_BGR2LAB).astype(np.float32)
    L = lab[:, :, 0]
    blur = cv2.GaussianBlur(L, (0, 0), 0.06 * face_w)
    hull = cv2.convexHull(pl[:, :2].astype(np.int32))
    fmask = np.zeros((h, w), np.uint8)
    cv2.fillConvexPoly(fmask, hull, 1)
    mean_l = L[fmask > 0].mean()
    lab[:, :, 0] = np.clip(L / np.maximum(blur, 1.0) * mean_l, 0, 255)
    flat = cv2.cvtColor(lab.astype(np.uint8), cv2.COLOR_LAB2BGR)

    # per body vertex: photo pixel, facing weight, face region
    P = base[:nb]
    nrm = np.zeros_like(P)
    btris = np.array(body['tris']).reshape(-1, 3)
    fn = np.cross(P[btris[:, 1]] - P[btris[:, 0]], P[btris[:, 2]] - P[btris[:, 0]])
    for k in range(3):
        np.add.at(nrm, btris[:, k], fn)
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
    pix, _ = cv2.projectPoints(P, rvec, tvec, K, None)
    pix = pix[:, 0]
    view = cam - P
    view /= np.linalg.norm(view, axis=1, keepdims=True)
    facing = np.clip(((nrm * view).sum(1) - np.cos(np.radians(80))) / (np.cos(np.radians(45)) - np.cos(np.radians(80))), 0, 1)
    # the face: below the hairline landmark (10, the top of the forehead), in front of the ears (234/454 depth)
    lm_v = {}
    for i in (10, 234, 454, 152):
        d = np.hypot(*(vxy - fl[i, :2]).T)
        lm_v[i] = int(vid[int(np.argmin(d))])
    top_y = base[lm_v[10], 1] - 0.004
    ear_z = 0.5 * (base[lm_v[234], 2] + base[lm_v[454], 2])
    region = np.clip((top_y - P[:, 1]) / 0.012, 0, 1) * np.clip((P[:, 2] - ear_z) / 0.015, 0, 1)
    region *= np.clip((P[:, 1] - (base[lm_v[152], 1] - 0.05)) / 0.02, 0, 1)   # fade out under the chin
    alpha_v = facing * region
    if os.environ.get('BAKE_DEBUG'):
        fv = vid[vid < nb]
        print('cam', cam.round(3), 'facing on front-visible verts: mean %.2f' % facing[fv].mean(), 'region mean %.2f' % region[fv].mean(), 'alpha>0: %d of %d' % ((alpha_v[fv] > 0).sum(), len(fv)))

    # 3. rasterise the head triangles in UV space
    out = np.zeros((SIZE, SIZE, 4), np.float32)
    bv = src  # drawn vertex -> body vertex
    uvp = np.stack([uv[:, 0] * SIZE, (1.0 - uv[:, 1]) * SIZE], 1)
    used = 0
    for t in tris:
        b = bv[t]
        if (b >= nb).any() or alpha_v[b].max() <= 0.0:
            continue
        used += 1
        q = uvp[t]
        x0, y0 = np.floor(q.min(0)).astype(int)
        x1, y1 = np.ceil(q.max(0)).astype(int)
        x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, SIZE - 1), min(y1, SIZE - 1)
        if x1 < x0 or y1 < y0:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        m = np.array([[q[1, 0] - q[0, 0], q[2, 0] - q[0, 0]], [q[1, 1] - q[0, 1], q[2, 1] - q[0, 1]]])
        if abs(np.linalg.det(m)) < 1e-9:
            continue
        mi = np.linalg.inv(m)
        rel = np.stack([xs - q[0, 0], ys - q[0, 1]], -1) @ mi.T
        l1, l2 = rel[..., 0], rel[..., 1]
        l0 = 1 - l1 - l2
        inside = (l0 >= -1e-3) & (l1 >= -1e-3) & (l2 >= -1e-3)
        if not inside.any():
            continue
        lam = np.stack([l0, l1, l2], -1)[inside]
        px = lam @ pix[b]
        a = lam @ alpha_v[b]
        col = cv2.remap(flat, px[:, 0].astype(np.float32).reshape(-1, 1), px[:, 1].astype(np.float32).reshape(-1, 1),
                        cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE).reshape(-1, 3)
        yy, xx = ys[inside].astype(int), xs[inside].astype(int)
        out[yy, xx, :3] = col[:, ::-1] / 255.0
        out[yy, xx, 3] = a
    # pad the colour a few texels past the covered area (mipmaps and filtering must not pull in black)
    rgb8 = (np.clip(out[:, :, :3], 0, 1) * 255).astype(np.uint8)
    a8 = (np.clip(out[:, :, 3], 0, 1) * 255).astype(np.uint8)
    cov = (out[:, :, 3] > 0).astype(np.uint8)
    for _ in range(8):
        grown = cv2.dilate(rgb8, np.ones((3, 3), np.uint8))
        rgb8 = np.where(cov[:, :, None] > 0, rgb8, grown)
        cov = cv2.dilate(cov, np.ones((3, 3), np.uint8))
    cv2.imwrite(f'{fdir}/face_skin.png', np.dstack([rgb8[:, :, ::-1], a8]))

    # colours: skin (the cheeks, unflattened mean), iris (landmarks.py), hair (above the forehead in the hair-down photo)
    allp = json.load(open(f'{work}/photo_landmarks.json'))
    cheek = np.zeros((h, w), np.uint8)
    for c in (pl[50, :2], pl[280, :2]):
        cv2.circle(cheek, (int(c[0]), int(c[1])), int(0.06 * face_w), 1, -1)
    skin = img[cheek > 0].mean(0)[::-1] / 255.0
    iris = allp[photo].get('iris', {})
    irises = [v for v in (iris.get('l'), iris.get('r')) if v]
    hair = None
    hp = allp.get('IMG_2894')
    if len(sys.argv) > 6 and allp.get(sys.argv[6], {}).get('landmarks'):
        # hair down beside the face: patches just outside the face outline at cheek height, middle half of their brightness
        hq = allp[sys.argv[6]]
        him = cv2.imread(hq['path'])
        hl = np.array(hq['landmarks'])
        fw = np.linalg.norm(hl[234, :2] - hl[454, :2])
        px = []
        for side, out in ((234, -1.0), (454, 1.0)):
            cx, cy = hl[side, 0] + out * 0.14 * fw, hl[side, 1] + 0.05 * fw
            r = int(0.05 * fw)
            px.append(him[int(cy - r):int(cy + r), int(cx - r):int(cx + r)].reshape(-1, 3))
        mid = np.vstack(px).astype(np.float64)
        lum = mid @ np.array([0.114, 0.587, 0.299])
        sel = mid[(lum > np.percentile(lum, 25)) & (lum < np.percentile(lum, 75))]
        hair = (np.median(sel, axis=0)[::-1] / 255.0).round(4).tolist()
        hp = None
    if hp and hp.get('landmarks'):
        him = cv2.imread(hp['path'])
        hl = np.array(hp['landmarks'])
        top = hl[10, :2]
        fw = np.linalg.norm(hl[234, :2] - hl[454, :2])
        patch = him[int(top[1] - 0.45 * fw):int(top[1] - 0.25 * fw), int(top[0] - 0.15 * fw):int(top[0] + 0.15 * fw)]
        if patch.size:
            hair = (patch.reshape(-1, 3).mean(0)[::-1] / 255.0).round(4).tolist()
    info = {'skin': skin.round(4).tolist(), 'iris': np.mean(irises, axis=0).round(4).tolist() if irises else None, 'hair': hair,
            'photo': photo, 'camera_error_px': round(float(err.mean()), 2)}
    json.dump(info, open(f'{fdir}/face.json', 'w'), indent=1)
    print(f'baked {used} triangles into {fdir}/face_skin.png; colours {info}')


if __name__ == '__main__':
    main()
