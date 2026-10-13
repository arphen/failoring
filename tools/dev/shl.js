const fs = require('fs'); const R = (...p) => require('path').join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js'));
const [planFile, preset] = process.argv.slice(2);
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const body = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[preset], null));
const plan = JSON.parse(fs.readFileSync(planFile, 'utf8')); plan.pieces.forEach(FL.normalizePiece);
const f = plan.pieces.find((p) => p.id === 'front'), b = plan.pieces.find((p) => p.id === 'back');
const wf = FL.makeWrapRows(f, body), wb = FL.makeWrapRows(b, body);
const bbf = FL.bbox(f.outline), uc = (bbf.x0 + bbf.x1) / 2, bbb = FL.bbox(b.outline), ub = (bbb.x0 + bbb.x1) / 2;
// shoulder line in pattern coordinates relative to centre line: from (0.115, -0.009) to (0.217, -0.032)
const A = [[0.115, -0.009], [0.217, -0.032]];
const pts = (w, c, sgn) => { const o = []; for (let i = 0; i <= 20; i++) { const t = i / 20, u = c + sgn * (A[0][0] + (A[1][0] - A[0][0]) * t), v = bbf.y1 + (A[0][1] + (A[1][1] - A[0][1]) * t) + 0.0; o.push(w.point(u, v)); } return o; };
const pf = pts(wf, uc, +1), pb = pts(wb, ub, -1);
for (let i = 0; i <= 20; i += 2) { let m = 9, bj = 0; for (let j = 0; j <= 20; j++) { const d = Math.hypot(pf[i][0] - pb[j][0], pf[i][1] - pb[j][1], pf[i][2] - pb[j][2]); if (d < m) { m = d; bj = j; } } console.log(i, 'front', pf[i].map((x) => x.toFixed(3)).join(','), 'nearest back j', bj, 'd', (m * 1000).toFixed(0), 'mm  back', pb[bj].map((x) => x.toFixed(3)).join(',')); }
console.log('piece top v', bbf.y1.toFixed(4), 'wrap.y', f.wrap.y.toFixed(4), 'vcap', f.wrap.vcap);
