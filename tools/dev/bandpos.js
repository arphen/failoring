// where does the band's top edge end up relative to the natural waist? node bandpos.js tag [presets]
const fs = require('fs'); const FL = require('/home/claude/failoring/app/src/shared.js'); const Body = require('/home/claude/failoring/app/src/body.js'); require('/home/claude/failoring/app/src/gait.js');
const M = Body.load(new Uint8Array(fs.readFileSync('/home/claude/failoring/app/assets/body_mh.bin')));
const tag = process.argv[2];
for (const P of (process.argv[3] || 'petite,average,tall,curvy,athletic,plus').split(',')) {
  const f = `${process.argv[4] || (process.env.SK || '/tmp/failoring-sk')}/dump_${tag}_${P}.json`; if (!fs.existsSync(f)) continue;
  const d = JSON.parse(fs.readFileSync(f, 'utf8')), body = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[P], null)), yw = body.landmarks.waist[1], yh = body.landmarks.hip ? body.landmarks.hip[1] : null;
  const r = d.ranges[d.ranges.length - 1], pos = d.pos;   // band is the last piece
  const bins = { CF: [-1e9, 1e9, 0], side: [0, 0, 0], CB: [0, 0, 0] }; const sec = (x, z) => { const a = Math.atan2(Math.abs(x), z) * 180 / Math.PI; return a < 35 ? 'CF' : a > 145 ? 'CB' : 'side'; };
  const top = { CF: -1e9, side: -1e9, CB: -1e9 }, bot = { CF: 1e9, side: 1e9, CB: 1e9 };
  for (let v = r.first; v < r.first + r.count; v++) { const x = pos[3 * v], y = pos[3 * v + 1], z = pos[3 * v + 2], s = sec(x, z); top[s] = Math.max(top[s], y); bot[s] = Math.min(bot[s], y); }
  const cm = (v) => ((v - yw) * 100).toFixed(1).padStart(6);
  console.log(P.padEnd(9), 'waist y', (yw * 100).toFixed(1), '| band top vs waist (cm): CF', cm(top.CF), 'side', cm(top.side), 'CB', cm(top.CB), '| bottom: CF', cm(bot.CF), 'side', cm(bot.side), 'CB', cm(bot.CB), '| height', ((top.CF - bot.CF) * 100).toFixed(1));
}
