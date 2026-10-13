// shl2.js <dump> <preset> : shoulder seam (front~back, top chains) and armhole heights vs the body's acromion/neck landmarks, start and end
const root = '/home/claude/failoring', fs = require('fs');
const FL = require(root + '/app/src/shared.js'), Body = require(root + '/app/src/body.js'); require(root + '/app/src/gait.js');
const [f, preset] = process.argv.slice(2);
const M = Body.load(new Uint8Array(fs.readFileSync(root + '/app/assets/body_mh.bin'))), st = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[preset], null)), L = st.landmarks;
const d = JSON.parse(fs.readFileSync(f, 'utf8')), off = d.ranges.map((r) => r.first), idx = Object.fromEntries(d.ids.map((id, i) => [id, i]));
const gl = (k, v) => v + (d.chains[k].flat().length && d.chains[k][0][0] >= off[k] ? 0 : off[k]);
const P = (pos, g) => [pos[3 * g], pos[3 * g + 1], pos[3 * g + 2]];
console.log('landmarks cm: acromion.L', L['acromion.L'].map((v) => (v * 100).toFixed(1)).join(','), ' acromion.R', L['acromion.R'].map((v) => (v * 100).toFixed(1)).join(','), ' cervicale', L.cervicale.map((v) => (v * 100).toFixed(1)).join(','));
for (const s of d.seams) { const ka = idx[s.a[0]], kb = idx[s.b[0]]; if (d.ids[ka] !== 'front' && d.ids[kb] !== 'front') continue; if (!(d.ids[ka] === 'back' || d.ids[kb] === 'back')) continue;
  const ca = d.chains[ka][s.a[1]]; const g0 = gl(ka, ca[0]), g1 = gl(ka, ca[1]);
  const m0 = P(d.pos0, g0), m1 = P(d.pos, g0), e0 = P(d.pos0, g1), e1 = P(d.pos, g1);
  if (m1[1] < 1.2) continue;
  console.log(s.a.join(':'), s.b.join(':'), 'pt0 start', m0.map((v) => (v * 100).toFixed(1)).join(','), 'end', m1.map((v) => (v * 100).toFixed(1)).join(','), '| pt1 start', e0.map((v) => (v * 100).toFixed(1)).join(','), 'end', e1.map((v) => (v * 100).toFixed(1)).join(','));
}
