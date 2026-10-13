// strain.js <dump> [thr] : stretched runs along every piece's outline in the last frame (3D length / uv rest length of each boundary edge)
const d = require(process.argv[2]), thr = +(process.argv[3] || 1.03);
const U = (i, k) => d.uv[2 * i + k], P = d.pos;
d.ids.forEach((id, pi) => {
  const ch = d.chains[pi]; const runs = []; let cur = null;
  ch.forEach(([a, b], k) => {
    const r = Math.hypot(U(a, 0) - U(b, 0), U(a, 1) - U(b, 1)), l = Math.hypot(P[3*a] - P[3*b], P[3*a+1] - P[3*b+1], P[3*a+2] - P[3*b+2]), s = l / r;
    if (s > thr) { if (!cur || k - cur.k1 > 1) { cur = { k0: k, k1: k, s: [], u: [], y: [] }; runs.push(cur); } cur.k1 = k; cur.s.push(s); cur.u.push([(U(a,0)+U(b,0))/2, (U(a,1)+U(b,1))/2]); cur.y.push((P[3*a+1]+P[3*b+1])/2); }
  });
  console.log(id.padEnd(10), 'outline edges', ch.length, 'stretched runs >' + thr + ':', runs.length);
  for (const r of runs) { const n = r.s.length, mx = Math.max(...r.s), m = r.s.reduce((a, b) => a + b, 0) / n; const u0 = r.u[0], u1 = r.u[n - 1];
    console.log('   edges', r.k0 + '-' + r.k1, 'n', n, 'mean', m.toFixed(3), 'max', mx.toFixed(3), 'uv', u0.map((v) => (v * 100).toFixed(0)).join(','), '->', u1.map((v) => (v * 100).toFixed(0)).join(','), 'y', (r.y.reduce((a, b) => a + b, 0) / n * 100).toFixed(0) + 'cm'); }
});
