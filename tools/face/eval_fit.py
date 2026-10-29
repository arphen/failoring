#!/usr/bin/env python3
"""How close a rendered head's landmarks are to the photo's (tools/face/build_face.sh prints it after each fit step).

    python -I tools/face/eval_fit.py <photo_landmarks.json> <photo name> <render landmarks.json>...

The photo's landmarks are aligned to each render's (similarity over all but the inner lips) and the remaining 2D
distance is reported as a share of the render's eye span (outer corner to outer corner): mean and 90th percentile.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # (run with python -I: the script's folder is not on the path)
from fit_shape import INNER_LIPS, umeyama  # noqa: E402


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    p = np.array(json.load(open(sys.argv[1]))[sys.argv[2]]['landmarks'])[:468] * [1, -1, -1]
    use = [i for i in range(468) if i not in INNER_LIPS]
    for path in sys.argv[3:]:
        m = np.array(list(json.load(open(path)).values())[0]['landmarks'])[:468] * [1, -1, -1]
        s, r, t = umeyama(p[use], m[use])
        pa = (s * (r @ p.T)).T + t
        e = np.linalg.norm((pa - m)[use][:, :2], axis=1)
        iod = np.linalg.norm(m[33, :2] - m[263, :2])
        print('%s: landmark error %.2f%% of the eye span (mean), %.2f%% (90th percentile)' % (path.split('/')[-1], 100 * e.mean() / iod,
                                                                                            100 * np.percentile(e, 90) / iod))


if __name__ == '__main__':
    main()
