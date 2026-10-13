// node girth.js <rawdir-prefix> : per preset, pattern girth P vs body section perimeter at waist / hip rows (skirt group)
const fs = require('fs'), path = require('path'), R = (...p) => path.join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js')); require(R('app/src/fsimport.js'));
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
for (const preset of process.argv.slice(2)) {
  const body = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[preset], null));
  const raw = JSON.parse(fs.readFileSync(R('app/assets/freesewing/raw_penelope_' + preset + '.json'), 'utf8'));
  const plan = FL.fsimport.plan(raw, body);
  const main = plan.pieces.find((p) => p.id === 'front').wrap, S = body.sections, yW = body.landmarks.waist[1], yH = body.landmarks.hip ? body.landmarks.hip[1] : null;
  const rows = [];
  for (const dy of [0, -0.04, -0.08, -0.12, -0.16, -0.2, -0.3, -0.4]) {
    const y = yW + dy, k = Math.round(y / S.dy), Pi = (y - main.Py0) / main.Pdy, i0 = Math.max(0, Math.min(main.P.length - 1, Math.floor(Pi)));
    const c0 = FL.sectionCurve(body, k, 0.012);
    rows.push(`${(dy * 100).toFixed(0)}cm: P ${(main.P[i0] * 100).toFixed(1)} body ${(c0.per * 100).toFixed(1)} ratio ${(main.P[i0] / c0.per).toFixed(3)}`);
  }
  console.log(preset.padEnd(9), 'waistY', (yW * 100).toFixed(0), 'hipY', yH ? (yH * 100).toFixed(0) : '?', '|', rows.join(' | '));
}
