// Decodes the mannequin asset once, so the Godot body port needs no DEFLATE code:
//   node tools/export_godot_body.js [--out godot/assets]
// Writes <out>/body_data.json: base mesh, triangles, parts, skin weights, targets, bones,
// joints, landmarks, masks (all as plain arrays, full precision). Plus body_ref.json: per
// preset the generate() measurements, residuals, a few joints/landmarks and section girths,
// read by godot/tests/body_check.tscn. Re-run if tools/convert_makehuman.py changes the asset.
'use strict';
const fs = require('fs'), path = require('path');
const ROOT = path.join(__dirname, '..'), R = (...p) => path.join(ROOT, ...p);
const Body = require(R('app/src/body.js'));
require(R('app/src/gait.js'));

const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : def; };
const OUT = path.resolve(opt('--out', R('godot/assets')));

const J = (a) => Array.from(a || []);
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const targets = {};
for (const [name, t] of Object.entries(M.targets)) targets[name] = { idx: J(t.idx), d: J(t.d), src: t.src };
const data = {
  format: 'failoring-body-data', version: 1, nv: M.nv, nb: M.nb, nq: M.nq,
  base: J(M.base), tris: J(M.tris), parts: M.parts, part: J(M.part),
  skinIdx: J(M.skinIdx), skinW: J(M.skinW), bones: M.bones, joints: M.joints, landmarks: M.lm,
  masks: M.masks || M.header.masks, targets,
};
fs.mkdirSync(OUT, { recursive: true });
fs.writeFileSync(path.join(OUT, 'body_data.json'), JSON.stringify(data));
console.log('body_data.json', (fs.statSync(path.join(OUT, 'body_data.json')).size / 1048576).toFixed(2), 'MB, targets:', Object.keys(targets).length);

const FL = require(R('app/src/shared.js'));
const ref = { presets: {} };
for (const name of Object.keys(FL.PRESETS)) {
  const rest = Body.generate(M, FL.PRESETS[name], null);
  const stand = FL.Gait.stand(M, rest);
  const S = stand.sections;
  const at = (y) => S.girth[Math.max(0, Math.min(S.ny - 1, Math.round(y / S.dy)))];
  const keep = {};
  for (const j of ['l-shoulder', 'l-elbow', 'l-hand', 'l-eye', 'pelvis']) keep['joint:' + j] = stand.joints[j].map((v) => +v.toFixed(6));
  for (const l of ['bust.L', 'waist', 'hip', 'cervicale', 'acromion.L']) keep['landmark:' + l] = stand.landmarks[l].map((v) => +v.toFixed(6));
  ref.presets[name] = {
    m: Object.fromEntries(Object.entries(stand.m).map(([k, v]) => [k, +(v * 1000).toFixed(4)])),
    residual: Object.fromEntries(Object.entries(stand.residual).map(([k, v]) => [k, +(v * 1000).toFixed(4)])),
    iters: stand.iters, secGirthMm: { bust: +(at(stand.landmarks['bust.L'][1]) * 1000).toFixed(2), waist: +(at(stand.landmarks.waist[1]) * 1000).toFixed(2), hip: +(at(stand.landmarks.hip[1]) * 1000).toFixed(2) },
    keep,
  };
}
fs.writeFileSync(path.join(OUT, 'body_ref.json'), JSON.stringify(ref));
console.log('body_ref.json presets:', Object.keys(ref.presets).join(','));
