#!/usr/bin/env python3
"""Face landmarks of photos (and of mannequin renders), for the face likeness (tools/face/build_face.sh).

    python -I tools/face/landmarks.py <out.json> <overlay-dir> <image>...

MediaPipe FaceLandmarker (478 points: 468 face mesh + 10 iris) on each image. Per image the JSON holds the image size,
the landmarks in pixels (x, y; z in the same pixel scale, larger = farther from the camera, relative to the face
centre), the 4x4 facial transformation matrix (canonical face model -> camera, centimetres) and the iris colour (mean
of the iris disc, sRGB 0..1). An image with no face gets "landmarks": null. The overlay directory gets each image with
its landmarks drawn, to check by eye. The model file is ~/.cache/failoring-face/models/face_landmarker.task (or
$FACE_MODEL). Needs mediapipe, opencv-python-headless, numpy (build_face.sh makes the environment).
"""
import json
import os
import sys

import cv2
import numpy as np
import mediapipe as mp
from mediapipe.tasks.python import BaseOptions, vision

MODEL = os.environ.get('FACE_MODEL', os.path.expanduser('~/.cache/failoring-face/models/face_landmarker.task'))
# MediaPipe face mesh: iris centres and rings (refined landmarks 468..477)
IRIS = {'l': (473, [474, 475, 476, 477]), 'r': (468, [469, 470, 471, 472])}


def iris_colour(img, pts, side):
    c, ring = IRIS[side]
    cx, cy = pts[c][:2]
    r = np.mean([np.hypot(pts[k][0] - cx, pts[k][1] - cy) for k in ring])
    h, w = img.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    d = np.hypot(xx - cx, yy - cy)
    m = (d > 0.35 * r) & (d < 0.85 * r)   # the iris ring: not the pupil, not the limbus
    if m.sum() < 10:
        return None
    rgb = img[m][:, ::-1].astype(np.float64) / 255.0   # BGR -> RGB
    # the brighter half: lids and lashes shade the top of the iris
    lum = rgb @ np.array([0.2126, 0.7152, 0.0722])
    sel = rgb[lum >= np.median(lum)]
    return [round(float(v), 4) for v in sel.mean(axis=0)]


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    out, odir, images = sys.argv[1], sys.argv[2], sys.argv[3:]
    os.makedirs(odir, exist_ok=True)
    opts = vision.FaceLandmarkerOptions(base_options=BaseOptions(model_asset_path=MODEL), num_faces=1,
                                        output_facial_transformation_matrixes=True, min_face_detection_confidence=0.3,
                                        min_face_presence_confidence=0.3)
    res = {}
    with vision.FaceLandmarker.create_from_options(opts) as det:
        for path in images:
            img = cv2.imread(path)
            if img is None:
                print(f'{path}: cannot read', file=sys.stderr)
                continue
            h, w = img.shape[:2]
            mpi = mp.Image(image_format=mp.ImageFormat.SRGB, data=cv2.cvtColor(img, cv2.COLOR_BGR2RGB))
            r = det.detect(mpi)
            name = os.path.splitext(os.path.basename(path))[0]
            if not r.face_landmarks:
                res[name] = {'path': path, 'w': w, 'h': h, 'landmarks': None}
                print(f'{name}: no face')
                continue
            pts = [[p.x * w, p.y * h, p.z * w] for p in r.face_landmarks[0]]
            mat = np.array(r.facial_transformation_matrixes[0]).tolist() if r.facial_transformation_matrixes else None
            res[name] = {'path': path, 'w': w, 'h': h, 'landmarks': [[round(v, 2) for v in p] for p in pts], 'matrix': mat,
                         'iris': {'l': iris_colour(img, pts, 'l'), 'r': iris_colour(img, pts, 'r')}}
            ov = img.copy()
            for i, p in enumerate(pts):
                cv2.circle(ov, (int(p[0]), int(p[1])), 2 if i < 468 else 3, (0, 255, 0) if i < 468 else (0, 0, 255), -1)
            cv2.imwrite(os.path.join(odir, name + '_landmarks.jpg'), ov)
            print(f'{name}: {len(pts)} landmarks')
    with open(out, 'w') as f:
        json.dump(res, f)


if __name__ == '__main__':
    main()
