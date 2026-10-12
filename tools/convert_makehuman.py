#!/usr/bin/env python3
"""Convert the CC0 MakeHuman 1.x assets into Failoring's compact body asset.

    python3 -I tools/convert_makehuman.py <makehuman-checkout> app/assets/body_mh.bin [--obj out/female_base.obj]

<makehuman-checkout> is a clone of https://github.com/makehumancommunity/makehuman (only the files listed in
SOURCES are read; a sparse checkout of those is enough). Everything read here is an *asset* in MakeHuman's licence
terms (base mesh, targets, skin weights), released under CC0 1.0 in September 2020 (LICENSE.md section C of that
repository, and the header of every file). No MakeHuman program code is used or ported: this script, the
blending and the measurement fitting in app/src/body.js are Failoring's own code.

Output (raw DEFLATE of the following, little endian):
    8 bytes  magic "FLBODY01"
    u32      length of the JSON header, then the UTF-8 JSON header, space-padded to a multiple of 4
    blobs    at the byte offsets listed in the header (relative to the start of the blob area)
The header describes vertex/quad counts, quantisation steps, the female base mesh, quads, per-vertex part labels,
sparse targets, joint vertex groups, landmark vertex ids and region masks. Every 16-bit array is stored
"shuffled" (all low bytes, then all high bytes); index arrays are delta coded first (u16 running sum). Base mesh
and target deltas are planar (all x, then all y, then all z) signed 16-bit integers times qBase / qTarget.
A target blob is: indices (n x u16, delta coded) followed by deltas (3n x i16).

Units are metres; y is up, +z is the front of the body, +x is the body's left side. The origin is MakeHuman's
(roughly at the pelvis); app/src/body.js puts the feet on y = 0.
"""
import json
import os
import struct
import subprocess
import sys
import zlib
from collections import OrderedDict, defaultdict

import numpy as np

DM = 0.1          # MakeHuman units are decimetres
Q_BASE = 3e-5     # m per unit for the base mesh (range +-0.98 m)
Q_TARGET = 2.5e-5  # m per unit for target deltas (range +-0.8 m)
EPS_DM = 1e-4     # drop target entries smaller than 0.01 mm

JOINTS = ['ground', 'pelvis', 'spine-4', 'spine-3', 'spine-2', 'spine-1', 'neck', 'head', 'head-2',
          'l-clavicle', 'r-clavicle', 'l-scapula', 'r-scapula', 'l-shoulder', 'r-shoulder', 'l-elbow', 'r-elbow',
          'l-hand', 'r-hand', 'l-upper-leg', 'r-upper-leg', 'l-knee', 'r-knee', 'l-ankle', 'r-ankle',
          'l-eye', 'r-eye']

PARTS = ['trunk', 'neck', 'head', 'arm.L', 'arm.R', 'hand.L', 'hand.R', 'leg.L', 'leg.R', 'foot.L', 'foot.R']


def target_list():
    """name used in body.js -> target file (relative to data/targets)."""
    t = OrderedDict()
    for m in ('min', 'average', 'max'):
        for w in ('min', 'average', 'max'):
            if m == 'average' and w == 'average':
                continue  # (almost) empty: the neutral body
            t['universal-%s-%s' % (m, w)] = 'macrodetails/universal-female-young-%smuscle-%sweight.target' % (m, w)
    hp = 'macrodetails/height/female-young-averagemuscle-averageweight-'
    t['height-max'] = hp + 'maxheight.target'
    t['height-min'] = hp + 'minheight.target'
    bp = 'breast/female-young-averagemuscle-averageweight-'
    t['cup-max'] = bp + 'maxcup-averagefirmness.target'
    t['cup-min'] = bp + 'mincup-averagefirmness.target'
    t['firm-max'] = bp + 'averagecup-maxfirmness.target'
    t['firm-min'] = bp + 'averagecup-minfirmness.target'
    t['nipple-size-decr'] = 'breast/nipple-size-decr.target'
    t['nipple-point-decr'] = 'breast/nipple-point-decr.target'
    t['buttocks-incr'] = 'buttocks/buttocks-volume-incr.target'
    t['buttocks-decr'] = 'buttocks/buttocks-volume-decr.target'
    t['belly-incr'] = 'stomach/stomach-pregnant-incr.target'
    t['belly-decr'] = 'stomach/stomach-pregnant-decr.target'
    for meas in ('neck-circ', 'neck-height', 'upperarm-circ', 'upperarm-length', 'lowerarm-length', 'wrist-circ',
                 'frontchest-dist', 'bust-circ', 'underbust-circ', 'waist-circ', 'napetowaist-dist', 'waisttohip-dist',
                 'shoulder-dist', 'hips-circ', 'upperleg-height', 'thigh-circ', 'lowerleg-height', 'calf-circ',
                 'knee-circ', 'ankle-circ'):
        for d in ('incr', 'decr'):
            t['%s-%s' % (meas, d)] = 'measure/measure-%s-%s.target' % (meas, d)
    return t


ETHNIC = ['macrodetails/caucasian-female-young.target', 'macrodetails/african-female-young.target',
          'macrodetails/asian-female-young.target']

SOURCES = ['3dobjs/base.obj', 'rigs/default_weights.mhw'] + ETHNIC + list(target_list().values())


def part_of_bone(b):
    side = b[-1] if b.endswith(('.L', '.R')) else ''
    if b.startswith(('upperarm', 'lowerarm')):
        return 'arm.' + side
    if b.startswith(('wrist', 'metacarpal', 'finger')):
        return 'hand.' + side
    if b.startswith(('upperleg', 'lowerleg')):
        return 'leg.' + side
    if b.startswith(('foot', 'toe')):
        return 'foot.' + side
    if b.startswith('neck'):
        return 'neck'
    if b in ('root', 'pelvis.L', 'pelvis.R', 'breast.L', 'breast.R', 'clavicle.L', 'clavicle.R',
             'shoulder01.L', 'shoulder01.R') or b.startswith('spine'):
        return 'trunk'
    return 'head'  # head, jaw, eyes, face muscles, tongue


def delta16(a):
    """Successive differences modulo 2^16 (the decoder takes a running sum); small numbers compress well."""
    a = np.asarray(a, dtype=np.int64)
    return np.diff(np.concatenate([[0], a])) & 0xFFFF


def shuffle16(a):
    """16-bit values stored as all low bytes, then all high bytes (helps deflate)."""
    u = (np.asarray(a, dtype=np.int64) & 0xFFFF).astype('<u2')
    return u.astype('u1').tobytes() + (u >> 8).astype('u1').tobytes()


def read_obj(path):
    V, groups, g = [], OrderedDict(), None
    with open(path) as f:
        for line in f:
            if line.startswith('v '):
                V.append([float(x) for x in line.split()[1:4]])
            elif line.startswith('g '):
                g = line.split(None, 1)[1].strip()
                groups.setdefault(g, [])
            elif line.startswith('f '):
                groups[g].append([int(t.split('/')[0]) - 1 for t in line.split()[1:]])
    return np.array(V), groups


def read_target(path, n):
    idx, d = [], []
    with open(path) as f:
        for line in f:
            if not line.strip() or line[0] == '#':
                continue
            p = line.split()
            i = int(p[0])
            if i < n:
                idx.append(i)
                d.append((float(p[1]), float(p[2]), float(p[3])))
    return np.array(idx, dtype=np.int64), np.array(d, dtype=np.float64).reshape(-1, 3)


def dense(path, n):
    i, d = read_target(path, n)
    out = np.zeros((n, 3))
    out[i] = d
    return out


def hull_perimeter(pts):
    """Perimeter of the 2D convex hull (monotone chain)."""
    P = sorted(set(map(tuple, np.round(pts, 7))))
    if len(P) < 3:
        return 0.0
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, hi = [], []
    for p in P:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], p) <= 0:
            lo.pop()
        lo.append(p)
    for p in reversed(P):
        while len(hi) >= 2 and cross(hi[-2], hi[-1], p) <= 0:
            hi.pop()
        hi.append(p)
    H = np.array(lo[:-1] + hi[:-1])
    return float(np.sum(np.linalg.norm(H - np.roll(H, 1, axis=0), axis=1)))


def girth(X, tris, y):
    """Tape-measure girth (convex hull perimeter) of the horizontal slice y through the given triangles."""
    P = X[tris]
    s = P[:, :, 1] - y
    pts = []
    for k in range(3):
        a, b = s[:, k], s[:, (k + 1) % 3]
        m = (a > 0) != (b > 0)
        t = a[m] / (a[m] - b[m])
        pa, pb = P[m, k], P[m, (k + 1) % 3]
        pts.append(pa + (pb - pa) * t[:, None])
    pts = np.concatenate(pts)
    return hull_perimeter(pts[:, [0, 2]]) if len(pts) else 0.0


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    root, out_path = sys.argv[1], sys.argv[2]
    obj_out = sys.argv[sys.argv.index('--obj') + 1] if '--obj' in sys.argv else None
    data = os.path.join(root, 'makehuman', 'data')
    commit = subprocess.run(['git', '-C', root, 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()

    V0, groups = read_obj(os.path.join(data, '3dobjs/base.obj'))
    body = np.array(groups['body'], dtype=np.int64)
    nb = int(body.max()) + 1
    assert nb == 13380 and body.min() == 0, 'unexpected base mesh layout'
    # kept vertices: the body surface, then the joint helper cubes (their centroids are the skeleton joints)
    keep = list(range(nb))
    joint_ids = OrderedDict()
    for j in JOINTS:
        vs = sorted(set(i for f in groups['joint-' + j] for i in f))
        joint_ids[j] = list(range(len(keep), len(keep) + len(vs)))
        keep += vs
    keep = np.array(keep)
    remap = -np.ones(len(V0), dtype=np.int64)
    remap[keep] = np.arange(len(keep))
    nv = len(keep)

    # female young adult: the base mesh plus the mean of the three female-young ethnic targets (MakeHuman's
    # default ethnic mix). This is the neutral body all other targets are added to.
    fem = sum(dense(os.path.join(data, 'targets', p), len(V0)) for p in ETHNIC) / len(ETHNIC)
    Vf = (V0 + fem)[keep] * DM

    # closed, consistently oriented surface? (needed by the signed-distance bake)
    edges = defaultdict(int)
    for q in body:
        for k in range(4):
            edges[(q[k], q[(k + 1) % 4])] += 1
    open_edges = sum(1 for (a, b), c in edges.items() if (b, a) not in edges)
    double_edges = sum(1 for c in edges.values() if c > 1)
    tris = np.concatenate([body[:, [0, 1, 2]], body[:, [0, 2, 3]]])
    P = Vf[tris]
    vol = np.sum(np.einsum('ij,ij->i', P[:, 0], np.cross(P[:, 1], P[:, 2]))) / 6
    assert open_edges == 0 and double_edges == 0, 'body mesh is not a closed, consistently oriented surface'
    assert vol > 0, 'faces are not oriented outwards'

    # part labels from the skin weights (bone with the largest weight)
    W = json.load(open(os.path.join(data, 'rigs/default_weights.mhw')))['weights']
    best = np.zeros(nb)
    part = np.zeros(nv, dtype=np.uint8) + 255
    for bone, lst in W.items():
        pid = PARTS.index(part_of_bone(bone))
        for i, w in lst:
            if i < nb and w > best[i]:
                best[i] = w
                part[i] = pid
    assert (part[:nb] != 255).all()

    # landmarks (vertex ids on the female base; body.js re-reads their positions after deformation)
    X = Vf[:nb]
    J = {j: Vf[ids].mean(0) for j, ids in joint_ids.items()}
    mid = np.where(np.abs(X[:, 0]) < 1e-6)[0]
    lm = {}
    for s, side in ((1, 'L'), (-1, 'R')):
        br = np.where((part[:nb] == PARTS.index('trunk')) & (X[:, 0] * s > 0.02) &
                      (X[:, 1] > J['spine-2'][1]) & (X[:, 1] < J['l-clavicle'][1]))[0]
        lm['bust.' + side] = int(br[np.argmax(X[br, 2])])  # most forward point of the breast = bust point
        # acromion (shoulder point): the corner where the top of the shoulder turns down into the arm, i.e. the
        # extreme point in the direction that bisects the shoulder-top and outer-arm normals (~59 deg above the
        # horizontal for MakeHuman's A-pose), within a coronal band through the shoulder joint
        sh = J[('l' if s > 0 else 'r') + '-shoulder']
        cand = np.where((np.abs(X[:, 2] - sh[2]) < 0.03) & (np.linalg.norm(X - sh, axis=1) < 0.12) & (X[:, 0] * s > 0.05))[0]
        lm['acromion.' + side] = int(cand[np.argmax(0.6 * s * X[cand, 0] + X[cand, 1])])
    c = mid[(X[mid, 1] > J['l-knee'][1]) & (X[mid, 1] < J['pelvis'][1])]
    lm['crotch'] = int(c[np.argmin(X[c, 1])])
    back = mid[X[mid, 2] < J['neck'][2] - 0.01]
    lm['cervicale'] = int(back[np.argmin(np.abs(X[back, 1] - (J['neck'][1] + 0.005)))])
    near = mid[np.abs(X[mid, 1] - (J['l-clavicle'][1] + 0.01)) < 0.006]
    lm['jugular'] = int(near[np.argmax(X[near, 2])])  # front surface just above the sternoclavicular joints
    # back midline: walk along mesh edges between symmetry-plane vertices from the cervicale down to the crotch
    # (the midline is a closed loop round the body, so the walk stays on the back)
    on_mid = np.zeros(nb, bool)
    on_mid[mid] = True
    madj = defaultdict(set)
    for q in body:
        for k in range(4):
            a, b = int(q[k]), int(q[(k + 1) % 4])
            if on_mid[a] and on_mid[b]:
                madj[a].add(b)
                madj[b].add(a)
    chain = [lm['cervicale']]
    prev_v = None
    while True:
        cur = chain[-1]
        nxt = [v for v in madj[cur] if v != prev_v and v not in chain]
        if not nxt:
            break
        if prev_v is None:
            nxt = [min(nxt, key=lambda v: X[v, 1])]   # first step: downwards
        v = nxt[0] if len(nxt) == 1 else min(nxt, key=lambda v: X[v, 1])
        prev_v = cur
        chain.append(int(v))
        if v == lm['crotch'] or X[v, 1] < X[lm['crotch'], 1] + 0.01:
            break
    chain = chain[::-1]  # crotch -> cervicale, increasing height
    lm['backMidline'] = chain
    # waist = narrowest trunk girth between the crotch and the bust. hip = seat level: the most posterior point of
    # the buttocks (the 'fullest girth' is ill-posed in an A-pose stance, where the hull of the splayed thighs keeps
    # growing down to the crotch). Both levels are anchored to vertices so they move with the body when targets
    # are applied (waist: back midline; seat: the buttock's most posterior vertex).
    trunk_tris = tris[np.all(part[tris] == PARTS.index('trunk'), axis=1)]
    hip_parts = [PARTS.index(p) for p in ('trunk', 'leg.L', 'leg.R')]
    hip_tris = tris[np.all(np.isin(part[tris], hip_parts), axis=1)]
    yc, yb = X[lm['crotch'], 1], X[lm['bust.L'], 1]
    ys = np.arange(yc + 0.10, yb - 0.06, 0.002)
    g = np.array([girth(X, trunk_tris, y) for y in ys])
    y_waist = float(ys[np.argmin(g)])
    ch = np.array(chain)
    seat = np.where(np.isin(part[:nb], hip_parts) & (X[:, 1] > yc) & (X[:, 1] < y_waist - 0.05) & (X[:, 0] > 0))[0]
    lm['waist'] = int(ch[np.argmin(np.abs(X[ch, 1] - y_waist))])
    lm['hip'] = int(seat[np.argmin(X[seat, 2])])
    y_hip = float(X[lm['hip'], 1])
    g2 = np.array([girth(X, hip_tris, y_hip)])
    print('neutral female: height %.3f m, waist %.1f cm at y=%.3f, hip %.1f cm at y=%.3f, bust level y=%.3f'
          % (X[:, 1].max() - X[:, 1].min(), g.min() * 100, y_waist, g2.max() * 100, y_hip, yb))

    # region masks for the mannequin finish (feathered weights 0..1)
    def region(center, r_full, r_zero, sel):
        d = np.linalg.norm(X[sel] - center, axis=1)
        w = np.clip((r_zero - d) / (r_zero - r_full), 0, 1)
        k = w > 0
        return [int(i) for i in sel[k]], [round(float(x), 3) for x in w[k]]
    trunk = np.where(part[:nb] == PARTS.index('trunk'))[0]
    masks = {}
    for side in ('L', 'R'):
        masks['nipple.' + side] = region(X[lm['bust.' + side]], 0.018, 0.032, trunk)
    legs_trunk = np.where(np.isin(part[:nb], hip_parts))[0]
    cr = X[lm['crotch']]
    masks['crotch'] = region(cr + np.array([0, 0.035, 0.035]), 0.045, 0.075, legs_trunk)

    # binary layout
    blobs = []
    off = 0

    def add_blob(b):
        nonlocal off
        pad = (-len(b)) % 4
        blobs.append(b + b'\0' * pad)
        o = off
        off += len(b) + pad
        return o

    base_q = np.round(Vf / Q_BASE).astype(np.int64)
    assert np.abs(base_q).max() < 32767
    o_base = add_blob(shuffle16(base_q.T.reshape(-1)))  # planar x[], y[], z[]
    o_quads = add_blob(shuffle16(delta16(remap[body].reshape(-1))))
    o_part = add_blob(part.astype('u1').tobytes())
    theader = []
    for name, rel in target_list().items():
        i, d = read_target(os.path.join(data, 'targets', rel), len(V0))
        k = (remap[i] >= 0) & (np.abs(d).max(axis=1) > EPS_DM)
        i, d = remap[i[k]], d[k] * DM
        order = np.argsort(i)
        i, d = i[order], d[order]
        q = np.round(d / Q_TARGET).astype(np.int64)
        assert np.abs(q).max() < 32767, name
        o = add_blob(shuffle16(delta16(i)) + shuffle16(q.T.reshape(-1)))
        theader.append({'name': name, 'n': int(len(i)), 'off': o, 'src': rel})

    header = {
        'format': 'failoring-body/1', 'units': 'm', 'nv': nv, 'nb': nb, 'nq': int(len(body)),
        'qBase': Q_BASE, 'qTarget': Q_TARGET, 'base': o_base, 'quads': o_quads, 'part': o_part, 'parts': PARTS,
        'targets': theader, 'joints': joint_ids, 'landmarks': lm, 'masks': masks,
        'source': {'project': 'MakeHuman 1.x (github.com/makehumancommunity/makehuman)', 'commit': commit,
                   'licence': 'CC0 1.0 Universal (assets; see LICENSE.md section C and file headers)',
                   'copyright': 'Data Collection AB, Joel Palmius, Jonas Hauquier (originally Manuel Bastioni); '
                                'released to CC0 in September 2020',
                   'files': SOURCES},
    }
    hj = json.dumps(header, separators=(',', ':')).encode()
    hj += b' ' * ((-len(hj)) % 4)
    raw = b'FLBODY01' + struct.pack('<I', len(hj)) + hj + b''.join(blobs)
    comp = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
    packed = comp.compress(raw) + comp.flush()
    os.makedirs(os.path.dirname(out_path) or '.', exist_ok=True)
    with open(out_path, 'wb') as f:
        f.write(packed)
    print('vertices %d (surface %d + %d joint helpers), quads %d, volume %.4f m^3'
          % (nv, nb, nv - nb, len(body), vol))
    print('targets %d, raw %.0f KB, deflated %.0f KB -> %s' % (len(theader), len(raw) / 1e3, len(packed) / 1e3, out_path))
    print('landmarks', {k: (v if isinstance(v, int) else len(v)) for k, v in lm.items()})
    if obj_out:
        with open(obj_out, 'w') as f:
            for p in Vf[:nb]:
                f.write('v %.5f %.5f %.5f\n' % tuple(p))
            for q in body:
                f.write('f %d %d %d %d\n' % tuple(q + 1))


if __name__ == '__main__':
    main()
