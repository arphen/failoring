#!/usr/bin/env python3
"""Face shape of the likeness: per-vertex offsets of the mannequin's head (tools/face/build_face.sh).

    python -I tools/face/fit_shape.py <body_data.json> <work-dir> <photo name> <out face_shape.json> [<prev face_shape.json> <render name>]

With a previous shape: a refinement step. <render name> is a render of the head with that shape (face_capture.gd with
FL_FACE, landmarks.py on it: <render name>.json and <render name> in a landmarks file of that name); the offsets still
missing are fitted on top of the previous ones. The detector reads a smooth mannequin face a little differently from a
photographed one, so the first fit leaves part of the difference. Measured (eval_fit.py, IMG_2898): mannequin 2.6 % of
the eye span, first fit 2.0 %, one refinement 1.1 %; further steps stay at the detector's noise (0.9 to 1.4 % with full
steps) or drift (half steps grew from 1.5 to 3.0 %), so build_face.sh does exactly one.

Inputs in the work directory: photo_landmarks.json and mannequin_landmarks.json (landmarks.py), mannequin_front.json
(godot/tests/face_capture.gd: where each front-facing head vertex of the base mesh lands in the mannequin render).

The photo and the mannequin render go through the same landmark detector, and the shape comes from the difference of
the two results, so the detector's own habits (where it puts the jaw line on a smooth face, its depth guess) cancel:
  1. each mannequin landmark is tied to the nearest projected head vertex: its rest position on the base mesh;
  2. the photo's landmarks are aligned to the mannequin's (similarity, least squares over the stable landmarks: the face
     keeps the mannequin's size, which the body measurements decide);
  3. the per-landmark difference, in the render's pixels, becomes metres on the base mesh (the render's scale, from the
     vertex projections), y and z flipped to the body's axes (+y up, +z front);
  4. a thin-plate spline spreads the offsets over the head; vertices of the back of the skull, the ears' backs and the
     neck below the jaw are pinned at zero, and the offsets fade out over the neck.
The irises are left out. The inner lip contour is fitted (FIT_INNER_LIPS): it closes the base mesh's parted lips.
Output: {"version", "photo", "idx": [base vertex], "d": [dx, dy, dz per vertex] (metres on the base mesh), "ref": [two
vertex indices whose distance scales the offsets to another body], "report": {...}}.
"""
import json
import sys

import numpy as np
from scipy.interpolate import RBFInterpolator

# MediaPipe face mesh indices
INNER_LIPS = [78, 191, 80, 81, 82, 13, 312, 311, 310, 415, 308, 324, 318, 402, 317, 14, 87, 178, 88, 95]
FACE_OVAL = [10, 338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288, 397, 365, 379, 378, 400, 377, 152, 148, 176, 149, 150,
             136, 172, 58, 132, 93, 234, 127, 162, 21, 54, 103, 67, 109]
# for the similarity: eye corners, nose bridge and tip, mouth corners, chin, temples
STABLE = [33, 133, 362, 263, 168, 6, 197, 1, 61, 291, 152, 234, 454, 10]
FIT_INNER_LIPS = True
LIP_TUCK = 0.003        # m
MAX_SNAP_PX = 10.0      # a mannequin landmark further than this from any projected vertex is not used
MAX_OFFSET = 0.025      # m: offsets are clamped to this (a bad landmark must not tear the head)


def umeyama(src, dst):
    """Similarity (s, R, t) with s R src + t ~ dst (least squares)."""
    mu_s, mu_d = src.mean(0), dst.mean(0)
    a, b = src - mu_s, dst - mu_d
    cov = b.T @ a / len(src)
    u, sig, vt = np.linalg.svd(cov)
    d = np.eye(3)
    if np.linalg.det(u @ vt) < 0:
        d[2, 2] = -1
    r = u @ d @ vt
    s = np.trace(np.diag(sig) @ d) / (a ** 2).sum(1).mean()
    return s, r, mu_d - s * r @ mu_s


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        sys.exit(1)
    body_path, work, photo, out = sys.argv[1:5]
    prev_path = sys.argv[5] if len(sys.argv) > 6 else None
    render = sys.argv[6] if len(sys.argv) > 6 else 'mannequin_front'
    body = json.load(open(body_path))
    base0 = np.array(body['base'], dtype=np.float64).reshape(-1, 3)
    nb = int(body['nb'])
    prev = np.zeros_like(base0)
    if prev_path:
        pj = json.load(open(prev_path))
        prev[np.array(pj['idx'])] = np.array(pj['d']).reshape(-1, 3)
    base = base0 + prev   # the head as the render shows it
    proj = json.load(open(f'{work}/{render}.json'))
    man = list(json.load(open(f'{work}/{render.replace("_front", "")}_landmarks.json')).values())[0]['landmarks']
    pho = json.load(open(f'{work}/photo_landmarks.json'))[photo]['landmarks']
    if man is None or pho is None:
        sys.exit('no landmarks on the mannequin render or the photo')
    man = np.array(man)[:468]
    pho = np.array(pho)[:468]
    # detector space -> body axes: the cameras look at the face from the front, so the image's right is the body's
    # left, +x as in the body; y down -> up; z away -> front
    flip = np.array([1.0, -1.0, -1.0])
    man3, pho3 = man * flip, pho * flip

    # 1. landmark -> vertex
    pv = np.array([[int(k)] + v for k, v in proj['verts'].items()])
    vid, vxy = pv[:, 0].astype(int), pv[:, 1:3]
    tie = {}
    for i in range(468):
        d = np.hypot(*(vxy - man[i, :2]).T)
        j = int(np.argmin(d))
        if d[j] <= MAX_SNAP_PX:
            tie[i] = int(vid[j])
    # render pixels -> base metres: the scale between the projected vertices and their base positions (front view)
    ids = list(tie.values())
    px = np.array([vxy[list(vid).index(v)] for v in ids])
    bxy = base[ids][:, :2]
    pxc, bc = px - px.mean(0), bxy - bxy.mean(0)
    m_per_px = np.sqrt((bc ** 2).sum() / (pxc ** 2).sum())

    # 2. align the photo to the mannequin (both in detector space, body axes)
    s, r, t = umeyama(pho3[STABLE], man3[STABLE])
    pa = (s * (r @ pho3.T)).T + t
    # 3. offsets
    # the inner lip contour too: the base mesh's lips are parted, and with these the mouth closes as in the photo (left
    # out, the photo's lips were painted round a dark slit)
    use = [i for i in tie if i not in INNER_LIPS or FIT_INNER_LIPS]
    off = (pa[use] - man3[use]) * m_per_px
    # closed lips meet behind their fronts: the inner contour is tucked 3 mm back (pulled to the photo's mouth line at the
    # lips' front, the parted base mesh folded into a zig-zag band)
    for k, i in enumerate(use):
        if i in INNER_LIPS:
            off[k, 2] -= LIP_TUCK
    norm = np.linalg.norm(off, axis=1)
    clamped = int((norm > MAX_OFFSET).sum())
    off *= np.minimum(1.0, MAX_OFFSET / np.maximum(norm, 1e-9))[:, None]
    ctrl = base[[tie[i] for i in use]]

    # 4. spread over the head; pinned: the back of the skull, behind the ears, and the neck below the jaw
    head_top = base[:nb, 1].max()
    # the chin: landmark 152, or the lowest tied landmark of the face outline when that one did not tie to a vertex
    chin_v = tie.get(152, min((tie[i] for i in FACE_OVAL if i in tie), key=lambda v: base[v, 1]))
    chin_y = base[chin_v, 1]
    jaw_z = base[chin_v, 2]
    head = np.where(base[:nb, 1] > chin_y - 0.10)[0]
    face_front = base[[tie[i] for i in use], 2].min()
    pins = [v for v in head if base[v, 2] < face_front - 0.045 or base[v, 1] < chin_y - 0.06]
    pins = pins[::3]
    pts = np.vstack([ctrl, base[pins]])
    vals = np.vstack([off, np.zeros((len(pins), 3))])
    # duplicate control vertices (two landmarks on one vertex): average
    key = {}
    for p, v in zip(map(tuple, pts), vals):
        key.setdefault(p, []).append(v)
    pts = np.array(list(key.keys()))
    vals = np.array([np.mean(v, axis=0) for v in key.values()])
    rbf = RBFInterpolator(pts, vals, kernel='thin_plate_spline', smoothing=1e-6)
    d = rbf(base[head])
    # fade out over the neck: full at the jaw line, none 8 cm below the chin
    w = np.clip((base[head, 1] - (chin_y - 0.08)) / 0.06, 0.0, 1.0)
    d *= w[:, None]
    total = prev.copy()
    total[head] += d
    keep = np.where(np.linalg.norm(total[:nb], axis=1) > 1e-5)[0]
    idx, d = keep, total[keep]
    # reference distance for other bodies: the outer eye corners
    ref = [tie[33], tie[263]] if 33 in tie and 263 in tie else [tie[STABLE[0]], tie[STABLE[3]]]
    res = {'version': 1, 'photo': photo, 'idx': idx.tolist(), 'd': [round(float(x), 6) for x in d.ravel()], 'ref': ref,
           'report': {'landmarks_tied': len(tie), 'used': len(use), 'clamped': clamped, 'pins': len(pins),
                      'm_per_px': m_per_px, 'scale_photo_to_mannequin': s,
                      'offset_mm': {'mean': float(np.linalg.norm(off, axis=1).mean() * 1000),
                                    'max': float(np.linalg.norm(off, axis=1).max() * 1000)},
                      'vertices': int(len(idx)), 'head_top': float(head_top), 'jaw_z': float(jaw_z)}}
    json.dump(res, open(out, 'w'))
    print(json.dumps(res['report'], indent=1))


if __name__ == '__main__':
    main()
