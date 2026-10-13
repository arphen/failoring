// node ease.js <dumpprefix> <preset...> : cloth vs body half-width (|x|) and depth (z extent) at hip/thigh/waist heights, last frame
const fs = require('fs'); const R = (...p) => require('path').join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js'));
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const pref = process.argv[2], presets = process.argv.slice(3);
for (const P of presets) {
  const b = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[P], null)), d = JSON.parse(fs.readFileSync(pref + '_' + P + '.json', 'utf8'));
  const meas = FL.PRESETS[P]; const bm = Body.measurements ? Body.measurements(b) : null;
  const ext = (pos, n, y0, y1, ids) => { let xmax = -9, zmin = 9, zmax = -9; for (let i = 0; i < n; i++) { if (ids && !ids(i)) continue; const y = pos[3 * i + 1]; if (y < y0 || y > y1) continue; xmax = Math.max(xmax, Math.abs(pos[3 * i])); zmin = Math.min(zmin, pos[3 * i + 2]); zmax = Math.max(zmax, pos[3 * i + 2]); } return [xmax, zmin, zmax]; };
  const out = [P.padEnd(9)];
  const S = b.sections; const yc = S.yCrotch;
  for (const [name, y] of [['hip', yc + 0.10], ['upper thigh', yc - 0.04], ['mid thigh', yc - 0.15]]) {
    const bo = ext(b.pos, b.pos.length / 3, y - 0.01, y + 0.01, (i) => b.pos[3 * i + 1] > 0 && Math.abs(b.pos[3 * i]) < 0.35 && !(Math.abs(b.pos[3 * i]) > 0.3));
    const cl = ext(d.pos, d.n, y - 0.01, y + 0.01);
    out.push(`${name}: x body ${(bo[0] * 100).toFixed(1)} cloth ${(cl[0] * 100).toFixed(1)} (+${((cl[0] - bo[0]) * 100).toFixed(1)}) depth body ${((bo[2] - bo[1]) * 100).toFixed(1)} cloth ${((cl[2] - cl[1]) * 100).toFixed(1)}`);
  }
  console.log(out.join(' | '));
}
