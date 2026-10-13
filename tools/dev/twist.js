// node twist.js <dump.json>... : rotation (deg about vertical axis) of each piece's centroid between frame 0 and the last frame, and waist-height ring check
const fs = require('fs');
for (const f of process.argv.slice(2)) {
  const d = JSON.parse(fs.readFileSync(f, 'utf8'));
  const cen = (P, r) => { let x = 0, y = 0, z = 0; for (let i = r.first; i < r.first + r.count; i++) { x += P[3*i]; y += P[3*i+1]; z += P[3*i+2]; } return [x/r.count, y/r.count, z/r.count]; };
  // body axis: mean of all piece centroids at frame 0 (x,z)
  const c0 = d.ranges.map((r) => cen(d.pos0, r)), c1 = d.ranges.map((r) => cen(d.pos, r));
  const ax = c0.reduce((a, c) => a + c[0], 0) / c0.length, az = c0.reduce((a, c) => a + c[2], 0) / c0.length;
  const ang = (c) => Math.atan2(c[0] - ax, c[2] - az) * 180 / Math.PI;
  const out = d.ids.map((id, i) => {
    let da = ang(c1[i]) - ang(c0[i]); while (da > 180) da -= 360; while (da < -180) da += 360;
    return id + ' ' + da.toFixed(1) + 'deg dy=' + ((c1[i][1] - c0[i][1]) * 100).toFixed(1) + 'cm';
  });
  console.log(f.split('/').pop().replace('dump_', '').replace('.json', '').padEnd(14), out.join(' | '));
}
