#!/usr/bin/env python3
"""Pack MPFB2's region masks (CC0, painted for MakeHuman's UV layout) into one RGBA texture for the skin shader.

    python3 -I tools/pack_skin_masks.py <mpfb2-checkout> godot/assets/skin/regions.png

Sources: src/mpfb/data/textures/mpfb_<name>.jpg of https://github.com/makehumancommunity/mpfb2 (assets under CC0 1.0,
LICENSE.ASSETS.md there). Channels: R lips, G nails (finger and toe), B eyelids, A inside of the mouth.
Kept at 2048 px: the head has a small share of MakeHuman's UV map, and the lips are about 50 px wide at this size.
"""
import os
import sys

from PIL import Image


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    tex = os.path.join(sys.argv[1], 'src', 'mpfb', 'data', 'textures')

    def m(name):
        return Image.open(os.path.join(tex, 'mpfb_%s.jpg' % name)).convert('L')

    from PIL import ImageChops
    nails = ImageChops.lighter(m('fingernails'), m('toenails'))
    out = Image.merge('RGBA', (m('lips'), nails, m('eyelids'), m('inside-mouth')))
    out.save(sys.argv[2], optimize=True)
    print('wrote', sys.argv[2], out.size)


if __name__ == '__main__':
    main()
