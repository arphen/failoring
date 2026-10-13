const fs = require('fs'); const R = (...p) => require('path').join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js'));
for (const f of process.argv.slice(2)) { const plan = JSON.parse(fs.readFileSync(f, 'utf8')); plan.pieces.forEach(FL.normalizePiece);
  console.log(f.split('/').pop(), plan.pieces.map((p) => { const bb = FL.bbox(p.outline), w = p.wrap; return `${p.id}: y ${(w.y * 100).toFixed(1)}..${((w.y + bb.y1 - bb.y0) * 100).toFixed(1)} h ${((bb.y1 - bb.y0) * 100).toFixed(1)}`; }).join(' | ')); }
