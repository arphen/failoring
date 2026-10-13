// crotch seam length from the section hulls at x=0 (taut tape): waist front -> crotch point -> waist back
const fs = require('fs'); const R = (...p) => require('path').join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js')); require(R('app/src/draft.js'));
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const mid = (h) => { let zf = -9, zb = 9; const n = h.length / 2; for (let i = 0; i < n; i++) { const j = (i + 1) % n, x0 = h[2 * i], x1 = h[2 * j]; if (x0 * x1 <= 0 && x0 !== x1) { const z = h[2 * i + 1] + (h[2 * j + 1] - h[2 * i + 1]) * (0 - x0) / (x1 - x0); zf = Math.max(zf, z); zb = Math.min(zb, z); } } return zf > -9 ? [zf, zb] : null; };
for (const P of ['petite', 'average', 'tall', 'curvy', 'athletic', 'plus']) {
  const b = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[P], null)), S = b.sections, yc = S.yCrotch, m = FL.draft.measures(b);
  const cp = [b.pos[3 * M.lm.crotch], b.pos[3 * M.lm.crotch + 1], b.pos[3 * M.lm.crotch + 2]];
  const kc = Math.ceil(yc / S.dy), kw = Math.round(m.yWaist / S.dy); let Lf = 0, Lb = 0, pf = null, pb = null;
  for (let k = kw; k >= kc; k--) { const r = mid(S.hulls[k]); if (!r) continue; const y = k * S.dy; if (pf) { Lf += Math.hypot(pf[0] - y, pf[1] - r[0]); Lb += Math.hypot(pb[0] - y, pb[1] - r[1]); } pf = [y, r[0]]; pb = [y, r[1]]; }
  const toF = Math.hypot(pf[0] - cp[1], pf[1] - cp[2]), toB = Math.hypot(pb[0] - cp[1], pb[1] - cp[2]);
  console.log(P.padEnd(9), `crotch pt y ${(cp[1] * 100).toFixed(1)} z ${(cp[2] * 100).toFixed(1)} x ${(cp[0] * 100).toFixed(1)} | hull front ${(Lf * 100).toFixed(1)} +${(toF * 100).toFixed(1)}  back ${(Lb * 100).toFixed(1)} +${(toB * 100).toFixed(1)} | crossSeam ${((Lf + Lb + toF + toB) * 100).toFixed(1)} front ${((Lf + toF) * 100).toFixed(1)} | waistToUpperLeg ${((m.yWaist - yc) * 100).toFixed(1)}  ratio ${((Lf + Lb + toF + toB) / (m.yWaist - yc)).toFixed(2)} (ref 2.60)`);
}
