const root = '/home/claude/failoring';
const FL = require(root + '/app/src/shared.js'), Body = require(root + '/app/src/body.js'); require(root + '/app/src/gait.js');
const fs = require('fs');
const M = Body.load(new Uint8Array(fs.readFileSync(root + '/app/assets/body_mh.bin')));
const hullDist = (h, x, z) => {
  const n = h.length >> 1; if (n < 3) return 1;
  let area = 0; for (let i = 0; i < n; i++) { const j = (i + 1) % n; area += h[2*i]*h[2*j+1] - h[2*j]*h[2*i+1]; }
  const s = area >= 0 ? 1 : -1; let inside = true, best = 1e9;
  for (let i = 0; i < n; i++) { const j = (i + 1) % n, ax = h[2*i], az = h[2*i+1], ex = h[2*j]-ax, ez = h[2*j+1]-az, l = Math.hypot(ex, ez) || 1e-9;
    if ((ex*(z-az) - ez*(x-ax)) * s < 0) inside = false;
    const t = Math.max(0, Math.min(1, ((x-ax)*ex + (z-az)*ez) / (l*l))); best = Math.min(best, Math.hypot(x-ax-t*ex, z-az-t*ez)); }
  return inside ? -best : best;
};
const armIds = ['arm.L', 'arm.R', 'hand.L', 'hand.R'].map((n) => M.parts.indexOf(n));
function gap(P, S, yMax) {   // smallest distance of the arm/hand vertices below yMax to the section hulls (negative = inside)
  let m = 1e9, at = null;
  for (let i = 0; i < M.nb; i++) {
    if (!armIds.includes(M.part[i])) continue;
    const x = P.allPos[3*i], y = P.allPos[3*i+1], z = P.allPos[3*i+2]; if (y > yMax) continue;
    const k = Math.round(y / S.dy), h = S.hulls[k]; if (!h || h.length < 6) continue;
    const d = hullDist(h, x, z); if (d < m) { m = d; at = [x, y, z, M.parts[M.part[i]]]; }
  }
  return { m, at };
}
const WANT = +(process.argv[2] || 0.022);
for (const name of Object.keys(FL.PRESETS)) {
  const rest = Body.generate(M, FL.PRESETS[name], null);
  let abd = 0.14, out = [];
  for (let it = 0; it < 5; it++) {
    const P = FL.Gait.pose(M, rest, 0, { stride: 0, arms: 0, armAbduct: abd }), S = Body.sections(M, P.allPos, 0.01);
    const yW = P.allPos[3 * M.lm.waist + 1], g = gap(P, S, yW + 0.06);
    out.push(abd.toFixed(3) + ':' + (g.m * 1000).toFixed(1) + 'mm@' + (g.at ? g.at.map((v, i) => i < 3 ? (v * 100).toFixed(0) : v).join(',') : '-'));
    if (g.m >= WANT - 0.002) break;
    const sh = P.joints['l-shoulder'], r = Math.max(0.15, sh[1] - g.at[1]);
    abd = Math.min(0.5, abd + (WANT - g.m) / r);
  }
  console.log(name.padEnd(9), out.join('  ->  '));
}
