// pattern widths per row of the chinos pieces at heights relative to the crotch; node patw.js <planfile> <preset>
const fs = require('fs'); const R = (...p) => require('path').join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js'));
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const [planfile, P] = process.argv.slice(2);
const b = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[P], null)), plan = JSON.parse(fs.readFileSync(planfile, 'utf8')); plan.pieces.forEach(FL.normalizePiece);
const yc = b.sections.yCrotch;
const rowExt = (o, v) => { let lo = 1e9, hi = -1e9; for (let i = 0; i < o.length; i++) { const a = o[i], c = o[(i + 1) % o.length]; if ((a[1] - v) * (c[1] - v) <= 0 && a[1] !== c[1]) { const x = a[0] + (c[0] - a[0]) * (v - a[1]) / (c[1] - a[1]); lo = Math.min(lo, x); hi = Math.max(hi, x); } } return hi > lo ? [lo, hi] : null; };
console.log('crotch y', (yc * 100).toFixed(1), 'cm; pieces:', plan.pieces.map((p) => `${p.id}(${p.wrap && p.wrap.mode}${p.wrap && p.wrap.side ? p.wrap.side : ''} y=${p.wrap && (p.wrap.y * 100).toFixed(1)})`).join(' '));
for (const dyc of [16, 12, 8, 4, 0, -4, -8, -12, -16, -20, -25, -30]) {
  const y = yc + dyc / 100, row = [String(dyc).padStart(4)];
  let tot = { L: 0, R: 0 };
  for (const p of plan.pieces) { const w = p.wrap, bb = FL.bbox(p.outline); if (!w || w.mode !== 'leg') continue; const v = bb.y0 + (y - w.y); const e = rowExt(p.outline, Math.min(v, w.vcap == null ? 1e9 : bb.y1)); const wd = e ? e[1] - e[0] : 0; row.push(`${p.id} ${(wd * 100).toFixed(1)}`); tot[w.side] += wd; }
  const kk = Math.round(y / b.sections.dy); const per = (h) => { let s = 0; const n = h.length / 2; for (let i = 0; i < n; i++) { const j = (i + 1) % n; s += Math.hypot(h[2 * i] - h[2 * j], h[2 * i + 1] - h[2 * j + 1]); } return s; };
  const lb = b.sections.legs.L[kk], hb = b.sections.hulls[kk];
  row.push('| leg tube pattern L ' + (tot.L * 100).toFixed(1) + ' R ' + (tot.R * 100).toFixed(1) + ' | body leg ' + (lb ? (per(lb) * 100).toFixed(1) : '-') + ' pelvis ' + (hb ? (per(hb) * 100).toFixed(1) : '-'));
  console.log(row.join('  '));
}
