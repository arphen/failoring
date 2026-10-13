// node tools/render_dump.js <dump.json> <preset> <out.png> [views] [yr0,yr1] [cellWidth]
// Renders a cloth state saved by walk_sim.js (DUMP=file) on the standing mannequin, coloured by piece, in any views
// (front, side, back, a45 ...); POS0=1 shows the start pose instead of the last frame. Several dumps can be shown side by side: <dump1.json,dump2.json,...> <preset1,preset2,...>.
'use strict';
const fs = require('fs'), path = require('path');
const root = path.join(__dirname, '..'), R = (...p) => path.join(root, ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js'));
const lib = require('./render_lib.js');
const [dumps, presets, out, viewArg = 'front,side', yrArg = '0.45,1.3', fwArg = '300'] = process.argv.slice(2);
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const dl = dumps.split(','), pl = presets.split(','), views = viewArg.split(','), yr = yrArg.split(',').map(Number), W = +fwArg, H = 640;
const F = dl.map((f, k) => {
  const d = JSON.parse(fs.readFileSync(f, 'utf8')), P = pl[k] || pl[0], body = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[P], null));
  const PC = new Int8Array(d.dtris.length / 3); d.ranges.forEach((r, i) => { for (let s = r.triFirst; s < r.triFirst + r.triCount; s++) for (let q = 0; q < 4; q++) PC[4 * s + q] = i; });
  return { i: k, label: P, cloth: Array.from(process.env.POS0 ? d.pos0 : d.pos, (v) => +v.toFixed(4)), body: Array.from(body.pos, (v) => +v.toFixed(4)), dtris: d.dtris, PC };
});
const html = lib.page({ BT: Array.from(M.tris), CT: null, F: F.map((f) => ({ i: f.i, label: f.label, cloth: f.cloth, body: f.body, CT: f.dtris, PC: Array.from(f.PC) })), views, PC: null, sc: (H - 40) / (yr[1] - yr[0]), y0: yr[0], ss: 2, W, H });
const hp = out.replace(/\.png$/, '') + '.html'; fs.writeFileSync(hp, html);
lib.shoot(hp, out, W * F.length, H * views.length).then(() => { console.log('wrote', out); process.exit(0); });
