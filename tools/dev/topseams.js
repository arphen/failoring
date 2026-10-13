// which edges of each piece's top edge (pattern y within 1.5 cm of its own top) are sewn, and to what; node topseams.js plan.json
const fs = require('fs'); const R = (...p) => require('path').join('/home/claude/failoring', ...p);
const FL = require(R('app/src/shared.js'));
const plan = JSON.parse(fs.readFileSync(process.argv[2], 'utf8')); plan.pieces.forEach(FL.normalizePiece);
const sew = {}; for (const s of plan.seams) { (sew[s.a[0]] || (sew[s.a[0]] = {}))[s.a[1]] = s.b[0]; (sew[s.b[0]] || (sew[s.b[0]] = {}))[s.b[1]] = s.a[0]; }
for (const p of plan.pieces) { const o = p.outline, n = o.length, bb = FL.bbox(o); if (p.id === 'waistband') continue;
  const row = []; for (let i = 0; i < n; i++) { const a = o[i], b = o[(i + 1) % n]; const y = Math.max(a[1], b[1]); if (y < bb.y1 - 0.12) continue; const near = Math.min(a[1], b[1]) > bb.y1 - 0.12; if (!near) continue;
    row.push(`${i}:${((a[0] + b[0]) / 2 * 100).toFixed(0)},${((a[1] + b[1]) / 2 * 100).toFixed(0)}${sew[p.id] && sew[p.id][i] ? '→' + sew[p.id][i] : ' ✗'}`); }
  console.log(p.id.padEnd(8), row.join('  ')); }
