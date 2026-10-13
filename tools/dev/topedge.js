// world heights along the top edge of each piece (rows 'x' fraction across), plan file
const fs = require('fs'); const R = (...p) => require('path').join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js'));
const plan = JSON.parse(fs.readFileSync(process.argv[2], 'utf8')); plan.pieces.forEach(FL.normalizePiece);
for (const p of plan.pieces) { const bb = FL.bbox(p.outline), w = p.wrap, o = p.outline; const n = o.length;
  // top vertices: those with y > bb.y1 - 0.15
  const tops = o.map((q, i) => [q[0], q[1], i]).filter((q) => q[1] > bb.y1 - 0.12);
  console.log(p.id.padEnd(10), w.mode, 'x0', (bb.x0 * 100).toFixed(1), 'x1', (bb.x1 * 100).toFixed(1), tops.map((q) => `(${(q[0] * 100).toFixed(1)}, ${((w.y + q[1] - bb.y0) * 100).toFixed(1)})`).join(' '));
}
