const fs = require('fs');
for (const f of process.argv.slice(2)) {
  const d = JSON.parse(fs.readFileSync(f, 'utf8'));
  const cen = (P, r) => { let x = 0, z = 0, y = 0; for (let i = r.first; i < r.first + r.count; i++) { x += P[3*i]; y += P[3*i+1]; z += P[3*i+2]; } return [x/r.count, y/r.count, z/r.count]; };
  const c0 = d.ranges.map((r) => cen(d.pos0, r)), c1 = d.ranges.map((r) => cen(d.pos, r));
  const ax = c0.reduce((a, c) => a + c[0], 0) / c0.length, az = c0.reduce((a, c) => a + c[2], 0) / c0.length;
  const A = (c) => (Math.atan2(c[0] - ax, c[2] - az) * 180 / Math.PI).toFixed(0);
  console.log(f.split('/').pop().replace('dump_base_', '').replace('.json', '').padEnd9 ? '' : '', f.split('/').pop().replace('dump_base_','').replace('.json','').padEnd(9),
    d.ids.map((id, i) => id + ' ' + A(c0[i]) + '->' + A(c1[i]) + ' y ' + (c0[i][1]*100).toFixed(0) + '->' + (c1[i][1]*100).toFixed(0)).join(' | '));
}
