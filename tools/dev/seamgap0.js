// node seamgap0.js <plan.json> <preset> : initial distance between the two sides of every seam edge pair (start pose), grouped by piece pair
const fs = require('fs'); const R = (...p) => require('path').join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js'));
const [planFile, preset] = process.argv.slice(2);
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const body = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[preset], null));
const plan = JSON.parse(fs.readFileSync(planFile, 'utf8')); plan.pieces.forEach(FL.normalizePiece);
const W = {}, O = {}; for (const p of plan.pieces) { W[p.id] = FL.makeWrapRows(p, body); O[p.id] = p.outline; }
const mid = (id, e) => { const o = O[id], a = o[e], b = o[(e + 1) % o.length], m = [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2]; return W[id].point(m[0], m[1]); };
const groups = {};
for (const s of plan.seams) { const pa = mid(s.a[0], s.a[1]), pb = mid(s.b[0], s.b[1]); const d = Math.hypot(pa[0] - pb[0], pa[1] - pb[1], pa[2] - pb[2]); const k = s.a[0] + '|' + s.b[0]; (groups[k] = groups[k] || []).push(d); }
for (const [k, v] of Object.entries(groups)) console.log(preset, k.padEnd(20), 'n', String(v.length).padStart(3), 'mean', (v.reduce((a, b) => a + b, 0) / v.length * 1000).toFixed(1), 'max', (Math.max(...v) * 1000).toFixed(1), 'mm');
if (process.env.DETAIL) { const rows = []; for (const s of plan.seams) { if (!(s.a[0] === 'front' && s.b[0] === 'back')) continue; const pa = mid(s.a[0], s.a[1]), pb = mid(s.b[0], s.b[1]); rows.push([Math.hypot(pa[0] - pb[0], pa[1] - pb[1], pa[2] - pb[2]), s.a[1], s.b[1], pa.map((v) => v.toFixed(3)).join(','), pb.map((v) => v.toFixed(3)).join(',')]); } rows.sort((a, b) => b[0] - a[0]); for (const r of rows.slice(0, 8)) console.log((r[0] * 1000).toFixed(1), 'mm  front edge', r[1], 'back edge', r[2], 'A', r[3], 'B', r[4]); }
