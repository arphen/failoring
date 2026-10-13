// node sec.js <dumpprefix> <preset...> : cloth cross-sections (display mesh) vs body section hulls. cm.
const fs = require('fs'); const R = (...p) => require('path').join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js'));
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const pref = process.argv[2], presets = process.argv.slice(3);
const hull = (pts) => { pts = pts.slice().sort((a, b) => a[0] - b[0] || a[1] - b[1]); if (pts.length < 3) return pts; const cr = (o, a, b) => (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]); const lo = [], up = []; for (const p of pts) { while (lo.length >= 2 && cr(lo[lo.length - 2], lo[lo.length - 1], p) <= 0) lo.pop(); lo.push(p); } for (let i = pts.length - 1; i >= 0; i--) { const p = pts[i]; while (up.length >= 2 && cr(up[up.length - 2], up[up.length - 1], p) <= 0) up.pop(); up.push(p); } lo.pop(); up.pop(); return lo.concat(up); };
const per = (h) => { let s = 0; for (let i = 0; i < h.length; i++) { const a = h[i], b = h[(i + 1) % h.length]; s += Math.hypot(a[0] - b[0], a[1] - b[1]); } return s; };
const ext = (h) => { let x0 = 9, x1 = -9, z0 = 9, z1 = -9; for (const p of h) { x0 = Math.min(x0, p[0]); x1 = Math.max(x1, p[0]); z0 = Math.min(z0, p[1]); z1 = Math.max(z1, p[1]); } return [x0, x1, z0, z1]; };
const f1 = (v) => (v * 100).toFixed(1);
const heights = (process.env.H || '16,12,8,4,0,-4,-8,-12,-16,-20,-25,-30').split(',').map(Number);
for (const P of presets) {
  const b = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[P], null)), d = JSON.parse(fs.readFileSync(pref + '_' + P + '.json', 'utf8'));
  const S = b.sections, yc = S.yCrotch, T = d.dtris, pos = d.pos; console.log('== ' + P + ' crotch ' + f1(yc) + ' cm');
  for (const dyc of heights) {
    const y = yc + dyc / 100, k = Math.round(y / S.dy);
    const segs = [];
    for (let t = 0; t < T.length; t += 3) { const v = [T[t], T[t + 1], T[t + 2]], ys = v.map((i) => pos[3 * i + 1]); if (Math.min(...ys) >= y || Math.max(...ys) <= y) continue;
      const pts = []; for (let e = 0; e < 3; e++) { const a = v[e], c = v[(e + 1) % 3], ya = pos[3 * a + 1], yb = pos[3 * c + 1]; if ((ya - y) * (yb - y) < 0) { const s = (y - ya) / (yb - ya); pts.push([pos[3 * a] + s * (pos[3 * c] - pos[3 * a]), pos[3 * a + 2] + s * (pos[3 * c + 2] - pos[3 * a + 2])]); } }
      if (pts.length === 2) segs.push(pts); }
    const groups = dyc < -0.4 ? [['L', (p) => p[0] > 0], ['R', (p) => p[0] < 0]] : [['both', () => true]];
    const row = [String(dyc).padStart(4)];
    for (const [side, sel] of groups) {
      const pts = []; let len = 0; for (const s of segs) if (sel(s[0])) { pts.push(s[0], s[1]); len += Math.hypot(s[0][0] - s[1][0], s[0][1] - s[1][1]); }
      const bl = side === 'both' ? S.hulls[k] : S.legs[side][k]; if (!bl || !pts.length) { row.push(side + ': n/a'); continue; }
      const bp = []; for (let i = 0; i < bl.length; i += 2) bp.push([bl[i], bl[i + 1]]);
      const bh = hull(bp), ch = hull(pts), be = ext(bh), ce = ext(ch);
      row.push(`${side}: body x ${f1(be[0])}..${f1(be[1])} z ${f1(be[2])}..${f1(be[3])} per ${f1(per(bh))} | cloth x ${f1(ce[0])}..${f1(ce[1])} z ${f1(ce[2])}..${f1(ce[3])} hull ${f1(per(ch))} fabric ${f1(len)} ease ${(100 * (per(ch) / per(bh) - 1)).toFixed(0)}%`);
    }
    console.log(row.join('\n     '));
  }
}
