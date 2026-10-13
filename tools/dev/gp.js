// node gp.js <plan prefix> presets... : hull girth below the natural waist vs the chinos band ring length
const fs = require('fs'), path = require('path'), R = (...p) => path.join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js'));
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const pre = process.argv[2];
for (const preset of process.argv.slice(3)) {
  const body = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[preset], null)), S = body.sections, yW = body.landmarks.waist[1];
  const plan = JSON.parse(fs.readFileSync(pre + '_' + preset + '.json', 'utf8'));
  const row = []; for (let d = 0; d <= 14; d += 2) { const k = Math.round((yW - d / 100) / S.dy); row.push(d + ':' + (S.girth[k] * 100).toFixed(1)); }
  console.log(preset.padEnd(9), 'waistY', (yW * 100).toFixed(1), 'girth by cm below waist', row.join(' '));
}
