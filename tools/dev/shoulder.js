// node shoulder.js <plan.json> <preset> : where does the shoulder seam start relative to the body's shoulder surface?
const fs = require('fs'), path = require('path'); const R = (...p) => path.join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js'));
const [planFile, preset] = process.argv.slice(2);
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const body = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[preset], null));
const plan = JSON.parse(fs.readFileSync(planFile, 'utf8')); plan.pieces.forEach(FL.normalizePiece);
const W = {}; for (const p of plan.pieces) W[p.id] = FL.makeWrapRows(p, body);
const P = body.pos, nv = P.length / 3;
const top = (x, z, r) => { let m = -9, c = 0; for (let i = 0; i < nv; i++) { const dx = P[3 * i] - x, dz = P[3 * i + 2] - z; if (dx * dx + dz * dz < r * r) { c++; if (P[3 * i + 1] > m) m = P[3 * i + 1]; } } return c ? m : null; };
const out = [];
for (const s of plan.seams) {
  if (!((s.a[0] === 'front' && s.b[0] === 'back') || (s.a[0] === 'back' && s.b[0] === 'front'))) continue;
  const pc = plan.pieces.find((p) => p.id === s.a[0]), o = pc.outline, e = s.a[1];
  const q = [o[e], o[(e + 1) % o.length]].map(([u, v]) => W[pc.id].point(u, v));
  const ym = (q[0][1] + q[1][1]) / 2; if (ym < 1.2) continue;   // shoulder seams only
  for (const pt of q) { const t = top(pt[0], pt[2], 0.02); out.push({ x: +pt[0].toFixed(3), y: +pt[1].toFixed(3), z: +pt[2].toFixed(3), bodyTop: t == null ? null : +t.toFixed(3), above: t == null ? null : +((pt[1] - t) * 100).toFixed(1) }); }
}
console.log(preset, 'shoulder seam samples (x,y,z, body surface y beneath, cloth minus body in cm):');
out.sort((a, b) => a.x - b.x); console.log(out.map((o) => `${o.x},${o.y},${o.z} body ${o.bodyTop} d=${o.above}`).join('\n'));
