// Walking pose for the mannequin: a parametric human gait cycle drives the skinned body (FL.Body.pose).
//
//   FL.Gait.pose(M, body, phase, opts) -> { pos, allPos, joints, rot, root }
//
// phase = fraction of the gait cycle of the LEFT leg (0 = left heel strike, 0.5 = right heel strike). The joint
// angle curves are keyframes after typical adult sagittal-plane gait kinematics (hip, knee, ankle over one stride;
// pelvis rotation / obliquity / lateral shift; counter-rotating shoulders; opposite-arm swing; elbow flexion).
// They are plausible, not measured on a person: a mannequin walk, not a motion-capture replay.
// The body stays in place (walk-in-place, as on a treadmill); the lowest point of the feet is put on y = 0 every
// frame, which gives the vertical pelvis bob for free.
// Axes (body.js): y up, +z front, +x the body's left. Flexion is a rotation about x.
(function (FL) {
  'use strict';
  const G = {};
  FL.Gait = G;
  const D = Math.PI / 180;

  // periodic Catmull-Rom through keyframes [[phase, value], ...] (phases ascending in [0, 1], first at 0)
  const curve = (keys) => {
    const n = keys.length, ph = keys.map((k) => k[0]), v = keys.map((k) => k[1]);
    const at = (i) => (i < 0 ? [ph[(i % n + n) % n] - 1 * Math.ceil(-i / n), v[(i % n + n) % n]] : i >= n ? [ph[i % n] + Math.floor(i / n), v[i % n]] : [ph[i], v[i]]);
    return (f) => {
      f -= Math.floor(f);
      let i = 0; while (i + 1 < n && ph[i + 1] <= f) i++;
      const p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
      const h = p2[0] - p1[0], t = (f - p1[0]) / h;
      const m1 = (p2[1] - p0[1]) / (p2[0] - p0[0]) * h, m2 = (p3[1] - p1[1]) / (p3[0] - p1[0]) * h;
      const t2 = t * t, t3 = t2 * t;
      return (2 * t3 - 3 * t2 + 1) * p1[1] + (t3 - 2 * t2 + t) * m1 + (-2 * t3 + 3 * t2) * p2[1] + (t3 - t2) * m2;
    };
  };
  // degrees. Hip flexion, knee flexion, ankle dorsiflexion (+) / plantarflexion (-), toe extension
  const HIP = curve([[0, 28], [0.12, 25], [0.30, 8], [0.50, -10], [0.60, -4], [0.73, 15], [0.87, 30]]);
  const KNEE = curve([[0, 4], [0.14, 17], [0.34, 6], [0.48, 14], [0.60, 42], [0.72, 64], [0.84, 38], [0.94, 9]]);
  const ANKLE = curve([[0, 0], [0.07, -6], [0.20, 2], [0.40, 10], [0.52, 6], [0.60, -17], [0.68, -9], [0.80, 1], [0.92, 3]]);
  const TOE = curve([[0, 0], [0.40, 0], [0.50, 14], [0.58, 36], [0.66, 22], [0.76, 4], [0.86, 0]]);
  const ELBOW = curve([[0, 12], [0.25, 24], [0.5, 40], [0.75, 24]]);   // flexion peaks as the arm swings forward (phase of that arm)

  const idx = (M) => { const o = {}; M.bones.forEach((b, k) => (o[b.joint] = k)); return o; };

  // opts: { stride: 0..1.4 (amplitude scale, 1 = normal walk), arms: swing scale, armAbduct: rad from vertical at rest }
  G.rotations = (M, body, phase, opts) => {
    opts = opts || {};
    const k = idx(M), rot = M.bones.map(() => [0, 0, 0]), s = opts.stride == null ? 1 : opts.stride, sa = opts.arms == null ? 1 : opts.arms;
    const tau = 2 * Math.PI, J = body.joints;
    const leg = (side, ph) => {
      const sg = side === 'l' ? 1 : -1, hip = J[side + '-upper-leg'], ank = J[side + '-ankle'];
      // bring the A-pose legs to a normal stance: rotate about z until the hip-ankle line is nearly vertical
      const a = Math.atan2(ank[0] - hip[0], hip[1] - ank[1]), stance = sg * 2.5 * D;
      rot[k[side + '-upper-leg']] = [-HIP(ph) * s * D, 0, -(a - stance)];
      rot[k[side + '-knee']] = [KNEE(ph) * s * D, 0, 0];
      rot[k[side + '-ankle']] = [-ANKLE(ph) * s * D, 0, (a - stance) * 0.0];
      rot[k[side + '-toe']] = [-TOE(ph) * s * D, 0, 0];
    };
    leg('l', phase); leg('r', phase + 0.5);
    // pelvis: axial rotation (the swinging side comes forward), obliquity (the unsupported side drops)
    const psi = -4.5 * D * s * Math.cos(tau * phase), obl = 3 * D * s * Math.sin(tau * phase);
    rot[k.pelvis] = [3 * D * s, psi, obl];
    // spine: counter-rotation so the shoulders turn against the pelvis (total shoulder angle +5 deg * cos)
    const rel = (5 * D * s * Math.cos(tau * phase) - psi) / 4;
    for (const j of ['spine-4', 'spine-3', 'spine-2', 'spine-1']) rot[k[j]] = [0, rel, -obl * 0.2];
    rot[k.neck] = [0, 0, 0]; rot[k.head] = [-3 * D * s, -(psi + rel * 4) * 0.5, obl * 0.3];
    // arms: opposite to the same-side leg
    const arm = (side, ph) => {
      const sg = side === 'l' ? 1 : -1, sh = J[side + '-shoulder'], hand = J[side + '-hand'];
      const a = Math.atan2(hand[0] - sh[0], sh[1] - hand[1]), want = sg * (opts.armAbduct == null ? ARM_ABDUCT : opts.armAbduct);
      const rx = 24 * D * sa * Math.cos(tau * ph);   // = -flexion: the arm is forward when the same-side leg is back
      rot[k[side + '-shoulder']] = [rx, 0, -(a - want)];
      rot[k[side + '-elbow']] = [-(8 + ELBOW(ph) * 0.6) * sa * D, 0, 0];
    };
    arm('l', phase); arm('r', phase + 0.5);
    return rot;
  };

  G.pose = (M, body, phase, opts) => {
    const rot = G.rotations(M, body, phase, opts), s = opts && opts.stride != null ? opts.stride : 1;
    const tau = 2 * Math.PI;
    let P = FL.Body.pose(M, body, rot, [0.018 * s * Math.sin(tau * phase), 0, 0]);
    // feet on the floor: lowest foot-part vertex at y = 0
    let ymin = Infinity;
    const part = M.part, fl = M.parts.indexOf('foot.L'), fr = M.parts.indexOf('foot.R');
    for (let i = 0; i < M.nb; i++) if (part[i] === fl || part[i] === fr) ymin = Math.min(ymin, P.pos[3 * i + 1]);
    if (isFinite(ymin)) {
      for (let i = 0; i < P.allPos.length; i += 3) P.allPos[i + 1] -= ymin;
      for (const n of Object.keys(P.joints)) P.joints[n][1] -= ymin;
    }
    P.rot = rot; P.root = [0.018 * s * Math.sin(tau * phase), -ymin, 0];
    return P;
  };

  // Arm tubes for sleeves: axis polyline shoulder -> elbow -> wrist and the bounding radius of the arm every 1 cm
  // along it (arm and hand vertices, perpendicular distance to the axis).
  G.arms = (M, P) => {
    const out = {};
    for (const [side, label] of [['L', 'l'], ['R', 'r']]) {
      const pts = [P.joints[label + '-shoulder'], P.joints[label + '-elbow'], P.joints[label + '-hand']];
      const seg = [], len = [];
      for (let i = 0; i < 2; i++) { const d = [pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1], pts[i + 1][2] - pts[i][2]], l = Math.hypot(...d); seg.push(d.map((x) => x / l)); len.push(l); }
      const total = len[0] + len[1], nbin = Math.ceil(total / 0.01) + 1, rad = new Float32Array(nbin);
      const ids = [M.parts.indexOf('arm.' + side)];
      for (let i = 0; i < M.nb; i++) {
        if (!ids.includes(M.part[i])) continue;
        const x = P.pos[3 * i], y = P.pos[3 * i + 1], z = P.pos[3 * i + 2];
        for (let k = 0; k < 2; k++) {
          const d = [x - pts[k][0], y - pts[k][1], z - pts[k][2]], s = d[0] * seg[k][0] + d[1] * seg[k][1] + d[2] * seg[k][2];
          if (s < (k ? 0 : -0.02) || s > len[k]) continue;
          const r = Math.hypot(d[0] - s * seg[k][0], d[1] - s * seg[k][1], d[2] - s * seg[k][2]), b = Math.round((s + (k ? len[0] : 0)) / 0.01);
          if (r > rad[b]) rad[b] = r;
        }
      }
      for (let b = 1; b < nbin; b++) if (!rad[b]) rad[b] = rad[b - 1];
      out[side] = { pts, seg, len, rad, total };
    }
    return out;
  };

  // Arm clearance. The arms hang so that, below the armpit, they stay ARM_GAP clear of the body's section hulls (the surface
  // garments are wrapped on and the cloth rests on). A body with wide hips or a belly (the plus preset: at the default 8 degrees
  // the elbow lies 17 mm inside the hull) needs its arms held further out, else the cloth is pinched between arm and torso and
  // never settles. The angle is never smaller than the default.
  const ARM_ABDUCT = 0.14, ARM_GAP = 0.02, ARM_MAX = 0.5;
  const hullDist = (h, x, z) => {   // signed distance of (x, z) to a convex polygon [x0, z0, x1, z1, ...]; negative inside
    const n = h.length >> 1; if (n < 3) return 1;
    let area = 0; for (let i = 0; i < n; i++) { const j = (i + 1) % n; area += h[2 * i] * h[2 * j + 1] - h[2 * j] * h[2 * i + 1]; }
    const sg = area >= 0 ? 1 : -1; let inside = true, best = 1e9;
    for (let i = 0; i < n; i++) {
      const j = (i + 1) % n, ax = h[2 * i], az = h[2 * i + 1], ex = h[2 * j] - ax, ez = h[2 * j + 1] - az, l = Math.hypot(ex, ez) || 1e-9;
      if ((ex * (z - az) - ez * (x - ax)) * sg < 0) inside = false;
      const t = Math.max(0, Math.min(1, ((x - ax) * ex + (z - az) * ez) / (l * l))); best = Math.min(best, Math.hypot(x - ax - t * ex, z - az - t * ez));
    }
    return inside ? -best : best;
  };
  // smallest distance between the arm and hand vertices (below the waist + 6 cm: elbow, forearm, hand) and the section hulls: { d, y }
  G.armGap = (M, P, S) => {
    const ids = ['arm.L', 'arm.R', 'hand.L', 'hand.R'].map((n) => M.parts.indexOf(n)), yMax = P.allPos[3 * M.lm.waist + 1] + 0.06;
    let d = 1e9, y = 0;
    for (let i = 0; i < M.nb; i++) {
      if (!ids.includes(M.part[i])) continue;
      const vy = P.allPos[3 * i + 1]; if (vy > yMax) continue;
      const h = S.hulls[Math.round(vy / S.dy)]; if (!h || h.length < 6) continue;
      const q = hullDist(h, P.allPos[3 * i], P.allPos[3 * i + 2]); if (q < d) { d = q; y = vy; }
    }
    return { d, y };
  };

  // The standing mannequin: A-pose legs and arms brought down (stride 0). Same structure as a generated body
  // (pos, allPos, joints, landmarks, sections) so the rest of the app can use it as is; .rest is the A-pose body.
  // .armAbduct is the arm angle used (rad from vertical); a walk of this body must pass it to G.pose.
  G.stand = (M, body) => {
    let abd = ARM_ABDUCT, P, S;
    for (let it = 0; it < 4; it++) {
      P = G.pose(M, body, 0, { stride: 0, arms: 0, armAbduct: abd });
      S = FL.Body.sections(M, P.allPos, 0.01);
      const g = G.armGap(M, P, S);
      if (g.d >= ARM_GAP - 0.002 || abd >= ARM_MAX) break;
      abd = Math.min(ARM_MAX, abd + (ARM_GAP - g.d) / Math.max(0.15, P.joints['l-shoulder'][1] - g.y));
    }
    const out = Object.assign({}, body, { pos: P.pos, allPos: P.allPos, joints: P.joints, rest: body, key: body.key + '/stand', landmarks: {}, armAbduct: abd });
    for (const [name, id] of Object.entries(M.lm)) if (typeof id === 'number') out.landmarks[name] = [P.allPos[3 * id], P.allPos[3 * id + 1], P.allPos[3 * id + 2]];
    out.sections = S;
    out.arms = G.arms(M, P);
    return out;
  };

  if (typeof module !== 'undefined') module.exports = G;
})(typeof FL !== 'undefined' ? FL : require('./shared.js'));
