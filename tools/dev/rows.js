// node rows.js <plan.json> <preset> [piece] : per-row girth target P vs the body section perimeter near the top of the garment
const fs = require('fs'); const R = (...p) => require('path').join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js'));
const [planFile, preset, pid = 'front'] = process.argv.slice(2);
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const body = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[preset], null));
const plan = JSON.parse(fs.readFileSync(planFile, 'utf8')); const pc = plan.pieces.find((p) => p.id === pid), w = pc.wrap;
console.log(preset, pid, 'wrap', JSON.stringify({ ...w, P: undefined }), 'rows', w.P.length);
const S = body.sections, clear = w.clear;
const bb = FL.bbox(pc.outline);
for (let i = w.P.length - 1; i >= Math.max(0, w.P.length - 16); i--) { const y = w.Py0 + i * w.Pdy, k = Math.max(0, Math.min(S.ny - 1, Math.round(y / S.dy))); const c0 = FL.sectionCurve(body, k, clear); console.log('y', y.toFixed(3), 'P', (w.P[i] * 100).toFixed(1), 'cm  section per', (c0.per * 100).toFixed(1), 'cm  kk', (c0.per / w.P[i]).toFixed(2)); }
