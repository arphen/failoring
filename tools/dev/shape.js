// node shape.js [preset] : torso hull half-widths vs arm extents by height; landmarks
'use strict';
const fs = require('fs'), path = require('path'), R = (...p) => path.join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js'));
const preset = process.argv[2] || 'petite';
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const body = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[preset], null));
const L = body.landmarks, J = body.joints, S = body.sections;
console.log('landmarks', Object.keys(L).join(' '));
console.log('joints', Object.keys(J).join(' '));
const f = (a) => a.map((v) => (v * 100).toFixed(1)).join(',');
for (const k of ['acromion.L', 'cervicale', 'bust.L', 'waist', 'axilla.L', 'armpit.L', 'neck.L', 'shoulder.L']) if (L[k]) console.log(k, f(L[k]));
for (const k of Object.keys(J)) if (/shoulder|elbow|clav/.test(k)) console.log('joint', k, f(J[k]));
console.log('armAbduct', body.armAbduct, 'arm L pts', body.arms && body.arms.L.pts.map(f).join(' | '), 'rad0', body.arms.L.rad.slice(0, 40).filter((_, i) => i % 5 === 0).map((v) => (v * 100).toFixed(1)).join(','));
// arm vertex extents by height
const P = body.pos, n = P.length / 3;
const sets = M.sets; console.log('sets', Object.keys(sets).join(' '), 'nb', M.nb);
const arm = sets.arm || sets.arms, secs = new Set(sets.section || []);
// which vertices belong to arms? use triangles not in section set
const inSec = new Uint8Array(M.tris.length / 3); for (const t of sets.section) inSec[t] = 1;
const isTorso = new Uint8Array(M.nb), isOther = new Uint8Array(M.nb);
for (let t = 0; t < inSec.length; t++) for (let q = 0; q < 3; q++) { const v = M.tris[3 * t + q]; if (v < M.nb) { if (inSec[t]) isTorso[v] = 1; else isOther[v] = 1; } }
for (let y = 1.0; y <= 1.40; y += 0.02) {
  const k = Math.round(y / S.dy), h = S.hulls[k]; let hx = 0, hz0 = 9, hz1 = -9; for (let i = 0; i < h.length; i += 2) { hx = Math.max(hx, Math.abs(h[i])); hz0 = Math.min(hz0, h[i + 1]); hz1 = Math.max(hz1, h[i + 1]); }
  let ax0 = 9, ax1 = -9, az0 = 9, az1 = -9, c = 0;
  for (let v = 0; v < M.nb; v++) if (isOther[v] && !isTorso[v] && P[3 * v] > 0 && Math.abs(P[3 * v + 1] - y) < 0.01) { ax0 = Math.min(ax0, P[3 * v]); ax1 = Math.max(ax1, P[3 * v]); az0 = Math.min(az0, P[3 * v + 2]); az1 = Math.max(az1, P[3 * v + 2]); c++; }
  console.log('y', y.toFixed(2), 'torso hull |x|max', (hx * 100).toFixed(1), 'z', (hz0 * 100).toFixed(1), (hz1 * 100).toFixed(1), '| arm(L) verts', c, c ? 'x ' + (ax0 * 100).toFixed(1) + '..' + (ax1 * 100).toFixed(1) + ' z ' + (az0 * 100).toFixed(1) + '..' + (az1 * 100).toFixed(1) : '');
}
