// seam gaps by piece pair, last frame (dump from walk_sim): node gaps.js <dump>
const d = JSON.parse(require('fs').readFileSync(process.argv[2], 'utf8')); const pos = d.pos, off = d.ranges.map((r) => r.first), idx = Object.fromEntries(d.ids.map((id, i) => [id, i]));
const gl = (k, v) => v + (d.chains[k].flat().length && d.chains[k][0][0] >= off[k] ? 0 : off[k]);   // local->global if needed
const P = (g) => [pos[3 * g], pos[3 * g + 1], pos[3 * g + 2]];
const by = {};
for (const s of d.seams) { const ka = idx[s.a[0]], kb = idx[s.b[0]], ca = d.chains[ka][s.a[1]], cb = d.chains[kb][s.b[1]]; if (!ca || !cb) continue;
  const a0 = P(gl(ka, ca[0])), a1 = P(gl(ka, ca[1])), b0 = P(gl(kb, cb[s.reverse ? 1 : 0])), b1 = P(gl(kb, cb[s.reverse ? 0 : 1]));
  const g = (Math.hypot(a0[0] - b0[0], a0[1] - b0[1], a0[2] - b0[2]) + Math.hypot(a1[0] - b1[0], a1[1] - b1[1], a1[2] - b1[2])) / 2;
  const key = [s.a[0], s.b[0]].sort().join('~'); const r = by[key] || (by[key] = { n: 0, sum: 0, max: 0, at: null }); r.n++; r.sum += g; if (g > r.max) { r.max = g; r.at = [s.a[1], s.b[1], ((a0[0] + a1[0]) / 2 * 100).toFixed(0), ((a0[1] + a1[1]) / 2 * 100).toFixed(0), ((a0[2] + a1[2]) / 2 * 100).toFixed(0)]; } }
for (const [k, r] of Object.entries(by)) console.log(k.padEnd(26), 'n', String(r.n).padStart(3), 'mean', (r.sum / r.n * 1000).toFixed(1), 'mm max', (r.max * 1000).toFixed(1), 'mm at edge', r.at.slice(0, 2).join('/'), 'xyz cm', r.at.slice(2).join(','));
