const fs = require('fs'); const R = (...p) => require('path').join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js'));
const [planFile, preset] = process.argv.slice(2);
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const body = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[preset], null));
const plan = JSON.parse(fs.readFileSync(planFile, 'utf8')); plan.pieces.forEach(FL.normalizePiece);
const W = {}, O = {}; for (const p of plan.pieces) { W[p.id] = FL.makeWrapRows(p, body); O[p.id] = p.outline; }
const pt = (id, e, f) => { const o = O[id], a = o[e], b = o[(e + 1) % o.length]; return W[id].point(a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f); };
for (const sl of ['sleeve-L', 'sleeve-R']) for (const bp of ['front', 'back']) {
  const rows = plan.seams.filter((s) => s.a[0] === sl && s.b[0] === bp).sort((a, b) => a.a[1] - b.a[1]);
  console.log(sl, bp, rows.map((s) => { const p0 = pt(s.a[0], s.a[1], 0), p1 = pt(s.a[0], s.a[1], 1), q0 = pt(s.b[0], s.b[1], s.reverse ? 1 : 0), q1 = pt(s.b[0], s.b[1], s.reverse ? 0 : 1); const d = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]) * 100; return `${s.a[1]}>${s.b[1]}${s.reverse ? 'r' : ''}:${d(p0, q0).toFixed(0)}/${d(p1, q1).toFixed(0)}`; }).join(' '));
}
