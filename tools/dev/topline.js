// node topline.js <plan.json> <preset> : 3D start positions of the highest outline vertices of front and back (x>0 half)
const fs = require('fs'); const R = (...p) => require('path').join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js'));
const [planFile, preset] = process.argv.slice(2);
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const body = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[preset], null));
const plan = JSON.parse(fs.readFileSync(planFile, 'utf8')); plan.pieces.forEach(FL.normalizePiece);
for (const id of ['front', 'back']) { const pc = plan.pieces.find((p) => p.id === id), w = FL.makeWrapRows(pc, body), bb = FL.bbox(pc.outline);
  console.log(id, 'pattern top', bb.y1.toFixed(3));
  pc.outline.forEach(([u, v], i) => { if (v > bb.y1 - 0.075 && (id === 'front' ? u > (bb.x0 + bb.x1) / 2 : u < (bb.x0 + bb.x1) / 2)) { const q = w.point(u, v); console.log('  #' + i, 'u', (u - (bb.x0 + bb.x1) / 2).toFixed(3), 'v', (v - bb.y1).toFixed(3), '->', q.map((x) => x.toFixed(3)).join(',')); } });
}
