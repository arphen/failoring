// band_h.js <dump> : mean height of the band piece in the last frame vs the preset's natural waist (needs preset name)
const root = '/home/claude/failoring', fs = require('fs');
const FL = require(root + '/app/src/shared.js'), Body = require(root + '/app/src/body.js'); require(root + '/app/src/gait.js');
const [f, preset] = process.argv.slice(2);
const M = Body.load(new Uint8Array(fs.readFileSync(root + '/app/assets/body_mh.bin'))), st = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[preset], null));
const d = JSON.parse(fs.readFileSync(f, 'utf8')), bi = d.ids.indexOf('waistband'), r = d.ranges[bi];
let ys = 0; for (let i = r.first; i < r.first + r.count; i++) ys += d.pos[3 * i + 1];
console.log(preset.padEnd(9), 'band mean y', (ys / r.count * 100).toFixed(1), 'cm, natural waist', (st.landmarks.waist[1] * 100).toFixed(1), 'cm');
