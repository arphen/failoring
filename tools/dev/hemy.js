// hemy.js <dump...> : lowest y (cm above the floor) of each piece in the first and last frame
for (const f of process.argv.slice(2)) {
  const d = require(f); const out = [];
  d.ids.forEach((id, i) => { const r = d.ranges[i]; let m0 = 1e9, m1 = 1e9; for (let v = r.first; v < r.first + r.count; v++) { m0 = Math.min(m0, d.pos0[3 * v + 1]); m1 = Math.min(m1, d.pos[3 * v + 1]); } out.push(id + ' ' + (m0 * 100).toFixed(1) + '/' + (m1 * 100).toFixed(1)); });
  console.log(f.split('/').pop().padEnd(24), out.join('  '));
}
