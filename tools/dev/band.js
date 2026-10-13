const fs = require('fs'), path = require('path'), R = (...p) => path.join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js')); require(R('app/src/fsimport.js'));
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
for (const preset of process.argv.slice(2)) {
  const body = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[preset], null));
  const raw = JSON.parse(fs.readFileSync(R('app/assets/freesewing/raw_penelope_' + preset + '.json'), 'utf8'));
  const plan = FL.fsimport.plan(raw, body, { debug: true });
  const wb = plan.pieces.find((p) => p.id === 'waistband'), bb = FL.bbox(wb.outline), w = wb.wrap, S = body.sections;
  const yW = body.landmarks.waist[1], k = Math.round((yW - 0.002) / S.dy), c0 = FL.sectionCurve(body, k, 0.012);
  const Pi = Math.round((yW - 0.002 + 0.01 - w.Py0) / w.Pdy);
  console.log(preset.padEnd(9), 'band bbox w', ((bb.x1 - bb.x0) * 100).toFixed(1), 'h', ((bb.y1 - bb.y0) * 100).toFixed(1), 'P(band rows)', w.P.map((v) => (v * 100).toFixed(0)).join(','), 'body per@waist', (c0.per * 100).toFixed(1), 'wrap.y', w.y.toFixed(3));
  console.log(plan.info.cov.filter((s) => s.startsWith('waistband')).join('\n'));
  console.log(' measurements:', JSON.stringify(plan.info.measurements).slice(0, 300));
}
