// node meas2.js : candidate seat / seatBack / waistBack / waistToSeat / cross seam on every preset
const fs = require('fs'); const R = (...p) => require('path').join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js')); require(R('app/src/draft.js'));
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const polyPer = (h) => { let s = 0; const n = h.length / 2; for (let i = 0; i < n; i++) { const j = (i + 1) % n; s += Math.hypot(h[2 * i] - h[2 * j], h[2 * i + 1] - h[2 * j + 1]); } return s; };
// back arc: from the max-x vertex to the min-x vertex through the lower-z side
const backArc = (h) => { const n = h.length / 2; let iMax = 0, iMin = 0; for (let i = 0; i < n; i++) { if (h[2 * i] > h[2 * iMax]) iMax = i; if (h[2 * i] < h[2 * iMin]) iMin = i; }
  const walk = (a, b) => { let s = 0, i = a; while (i !== b) { const j = (i + 1) % n; s += Math.hypot(h[2 * i] - h[2 * j], h[2 * i + 1] - h[2 * j + 1]); i = j; } return s; };
  const A = walk(iMax, iMin), B = walk(iMin, iMax);
  // which arc is the back? the one whose mean z is lower
  const mz = (a, b) => { let s = 0, c = 0, i = a; while (i !== b) { s += h[2 * i + 1]; c++; i = (i + 1) % n; } return s / Math.max(1, c); };
  return mz(iMax, iMin) < mz(iMin, iMax) ? A : B; };
for (const P of ['petite', 'average', 'tall', 'curvy', 'athletic', 'plus']) {
  const b = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[P], null)), S = b.sections, m = FL.draft.measures(b), yc = S.yCrotch;
  let best = 0, ky = 0; for (let k = Math.round(yc / S.dy); k <= Math.round((m.yWaist - 0.05) / S.dy); k++) { const p = S.girth[k]; if (p > best) { best = p; ky = k; } }
  const hSeat = S.hulls[ky], hWaist = S.hulls[Math.round(m.yWaist / S.dy)], hHip = S.hulls[Math.round(m.yHip / S.dy)];
  console.log(P.padEnd(9), `waist ${(m.waist * 100).toFixed(1)} waistBack ${(backArc(hWaist) * 100).toFixed(1)} | hip(yHip ${(m.yHip * 100).toFixed(0)}) ${(m.hip * 100).toFixed(1)} | seat max ${(best * 100).toFixed(1)} at y ${(ky * S.dy * 100).toFixed(0)} (crotch+${((ky * S.dy - yc) * 100).toFixed(0)}) back ${(backArc(hSeat) * 100).toFixed(1)} | waistToSeat ${((m.yWaist - ky * S.dy) * 100).toFixed(1)} | ratio seat/hip ${(best / m.hip).toFixed(3)}  (ref 1.122)`);
}
