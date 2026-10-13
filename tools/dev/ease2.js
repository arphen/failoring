// node ease2.js <dumpprefix> <preset...> : cloth vs body hull (x extent, z extent, perimeter) per height, last frame, particles only
const fs = require('fs'); const R = (...p) => require('path').join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js'));
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const pref = process.argv[2], presets = process.argv.slice(3);
const hull = (pts) => { pts = pts.slice().sort((a, b) => a[0] - b[0] || a[1] - b[1]); if (pts.length < 3) return pts; const cr = (o, a, b) => (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]); const lo = [], up = []; for (const p of pts) { while (lo.length >= 2 && cr(lo[lo.length - 2], lo[lo.length - 1], p) <= 0) lo.pop(); lo.push(p); } for (let i = pts.length - 1; i >= 0; i--) { const p = pts[i]; while (up.length >= 2 && cr(up[up.length - 2], up[up.length - 1], p) <= 0) up.pop(); up.push(p); } lo.pop(); up.pop(); return lo.concat(up); };
const per = (h) => { let s = 0; for (let i = 0; i < h.length; i++) { const a = h[i], b = h[(i + 1) % h.length]; s += Math.hypot(a[0] - b[0], a[1] - b[1]); } return s; };
const ext = (h) => { let x0 = 9, x1 = -9, z0 = 9, z1 = -9; for (const p of h) { x0 = Math.min(x0, p[0]); x1 = Math.max(x1, p[0]); z0 = Math.min(z0, p[1]); z1 = Math.max(z1, p[1]); } return [x0, x1, z0, z1]; };
for (const P of presets) {
  const b = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[P], null)), d = JSON.parse(fs.readFileSync(pref + '_' + P + '.json', 'utf8'));
  const S = b.sections, yc = S.yCrotch; console.log('== ' + P + ' crotch y ' + (yc * 100).toFixed(1));
  for (const dyc of [20, 16, 12, 8, 4, 0, -4, -8, -12, -16, -20, -30]) {
    const y = yc + dyc / 100, k = Math.round(y / S.dy); const hb = [...S.hulls[k]]; const bp = []; for (let i = 0; i < hb.length; i += 2) bp.push([hb[i], hb[i + 1]]);
    const cp = []; for (let i = 0; i < d.n; i++) if (Math.abs(d.pos[3 * i + 1] - y) < 0.006) cp.push([d.pos[3 * i], d.pos[3 * i + 2]]);
    if (!bp.length || cp.length < 3) { console.log(String(dyc).padStart(4), 'n/a', cp.length); continue; }
    const bh = hull(bp), ch = hull(cp), be = ext(bh), ce = ext(ch);
    console.log(String(dyc).padStart(4), `body x ${(be[0] * 100).toFixed(1)}..${(be[1] * 100).toFixed(1)} z ${(be[2] * 100).toFixed(1)}..${(be[3] * 100).toFixed(1)} per ${(per(bh) * 100).toFixed(1)} | cloth x ${(ce[0] * 100).toFixed(1)}..${(ce[1] * 100).toFixed(1)} z ${(ce[2] * 100).toFixed(1)}..${(ce[3] * 100).toFixed(1)} per ${(per(ch) * 100).toFixed(1)} | +per ${((per(ch) - per(bh)) * 100).toFixed(1)} (${(100 * (per(ch) / per(bh) - 1)).toFixed(0)}%)`);
  }
}
