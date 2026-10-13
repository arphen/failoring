// node clothsym.js <dump.json> [frame: 0|last] : mirror symmetry (x -> -x) of the cloth, per piece
const d = require(process.argv[2]); const which = process.argv[3] === 'last' ? d.pos : d.pos0; const n = d.n;
const cell = 0.01, h = new Map(); const key = (x, y, z) => Math.round(x / cell) + ',' + Math.round(y / cell) + ',' + Math.round(z / cell);
for (let i = 0; i < n; i++) { const k = key(which[3 * i], which[3 * i + 1], which[3 * i + 2]); (h.get(k) || h.set(k, []).get(k)).push(i); }
d.ids.forEach((id, k) => { const r = d.ranges[k]; let sum = 0, cnt = 0, worst = 0, cx = 0;
  for (let i = r.first; i < r.first + r.count; i++) { cx += which[3 * i]; const x = -which[3 * i], y = which[3 * i + 1], z = which[3 * i + 2]; let best = 0.3; const ix = Math.round(x / cell), iy = Math.round(y / cell), iz = Math.round(z / cell);
    for (let dx = -3; dx <= 3; dx++) for (let dy = -3; dy <= 3; dy++) for (let dz = -3; dz <= 3; dz++) { const l = h.get((ix + dx) + ',' + (iy + dy) + ',' + (iz + dz)); if (l) for (const j of l) { const dd = Math.hypot(which[3 * j] - x, which[3 * j + 1] - y, which[3 * j + 2] - z); if (dd < best) best = dd; } }
    sum += best; cnt++; if (best > worst) worst = best; }
  console.log(id.padEnd(10), 'mean mirror distance', (sum / cnt * 1000).toFixed(1), 'mm  worst', (worst * 1000).toFixed(0), 'mm   mean x', (cx / r.count * 1000).toFixed(1), 'mm'); });
