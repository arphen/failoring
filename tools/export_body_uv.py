#!/usr/bin/env python3
"""Export the MakeHuman texture coordinates of the mannequin's body for the Godot body view.

    python3 -I tools/export_body_uv.py <makehuman-checkout> godot/assets/body_uv.json

The body in godot/assets/body_data.json keeps MakeHuman's own vertex numbers (0..13379) and its quads in base.obj
order (tools/convert_makehuman.py), so base.obj's UVs ("vt" and the "f v/vt" corners of the "body" group) apply to it
directly. A vertex on a UV seam has more than one UV, so the drawn mesh splits it: every distinct (vertex, uv) pair
is one drawn vertex. Output (JSON):

    src   drawn vertex -> body vertex (positions and normals are gathered from the posed body every frame)
    uv    drawn vertex -> u, v (flat list; v as MakeHuman has it, 0 at the bottom of the image)
    tris  triangles over drawn vertices, quad q as (q0, q1, q2) and (q0, q2, q3), the same order as body_data.json

The UV layout is MakeHuman's (CC0), the one MakeHuman and MPFB skins and region masks are painted for.
"""
import json
import os
import sys


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    root, out = sys.argv[1], sys.argv[2]
    obj = os.path.join(root, 'makehuman', 'data', '3dobjs', 'base.obj')
    vt = []
    faces = []
    group = None
    with open(obj) as f:
        for line in f:
            if line.startswith('vt '):
                p = line.split()
                vt.append((float(p[1]), float(p[2])))
            elif line.startswith('g '):
                group = line.split()[1]
            elif line.startswith('f ') and group == 'body':
                corners = []
                for c in line.split()[1:]:
                    v, t = c.split('/')[:2]
                    corners.append((int(v) - 1, int(t) - 1))
                assert len(corners) == 4, 'body faces are quads'
                faces.append(corners)
    here = os.path.dirname(os.path.abspath(__file__))
    body = json.load(open(os.path.join(here, '..', 'godot', 'assets', 'body_data.json')))
    tris = body['tris']
    assert len(tris) == 6 * len(faces), 'quad count differs from body_data.json'
    pair = {}
    src = []
    uv = []
    out_tris = []
    for q, corners in enumerate(faces):
        # same vertices as body_data.json's two triangles for this quad
        want = [tris[6 * q], tris[6 * q + 1], tris[6 * q + 2], tris[6 * q + 5]]
        assert [c[0] for c in corners] == want, 'quad %d does not match body_data.json' % q
        ids = []
        for v, t in corners:
            key = (v, t)
            if key not in pair:
                pair[key] = len(src)
                src.append(v)
                uv.extend([round(vt[t][0], 5), round(vt[t][1], 5)])
            ids.append(pair[key])
        out_tris += [ids[0], ids[1], ids[2], ids[0], ids[2], ids[3]]
    assert sorted(set(src)) == list(range(body['nb'])), 'every body vertex is drawn'
    json.dump({'format': 'failoring-body-uv', 'version': 1, 'source': 'makehuman/data/3dobjs/base.obj (CC0)',
               'n': len(src), 'nb': body['nb'], 'src': src, 'uv': uv, 'tris': out_tris},
              open(out, 'w'), separators=(',', ':'))
    print('drawn vertices %d for %d body vertices (%d on UV seams), %d triangles' % (
        len(src), body['nb'], len(src) - body['nb'], len(out_tris) // 3))


if __name__ == '__main__':
    main()
