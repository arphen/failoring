// seampos.js <dump> <pieceA> <pieceB> [start] : endpoints (cm) of every seam edge pair between two pieces
const d = JSON.parse(require('fs').readFileSync(process.argv[2], 'utf8')), A = process.argv[3], B = process.argv[4], pos = process.argv[5] ? d.pos0 : d.pos;
const off = d.ranges.map((r) => r.first), idx = Object.fromEntries(d.ids.map((id, i) => [id, i]));
const gl = (k, v) => v + (d.chains[k].flat().length && d.chains[k][0][0] >= off[k] ? 0 : off[k]);
const P = (g) => [pos[3 * g], pos[3 * g + 1], pos[3 * g + 2]].map((v) => (v * 100).toFixed(1)).join(',');
for (const s of d.seams) { const names = [s.a[0], s.b[0]]; if (!(names.includes(A) && names.includes(B))) continue;
  const ka = idx[s.a[0]], kb = idx[s.b[0]], ca = d.chains[ka][s.a[1]], cb = d.chains[kb][s.b[1]];
  console.log(s.a[0] + '#' + s.a[1], '(' + P(gl(ka, ca[0])) + ')->(' + P(gl(ka, ca[1])) + ')   ', s.b[0] + '#' + s.b[1], '(' + P(gl(kb, cb[s.reverse ? 1 : 0])) + ')->(' + P(gl(kb, cb[s.reverse ? 0 : 1])) + ')'); }
