// node asym.js <dumpprefix e.g. dump_teagan_r60> : shoulder tilt and drop of the front piece at the last frame, per preset
const S = process.argv[2]; const out = [];
for (const P of ['petite', 'average', 'tall', 'curvy', 'athletic', 'plus']) {
  const d = require(S + '_' + P + '.json'); const k = d.ids.indexOf('front'), r = d.ranges[k];
  const top = (pos, sgn) => { let m = -9; for (let i = r.first; i < r.first + r.count; i++) { if (sgn * pos[3 * i] > 0.08 && pos[3 * i + 1] > m) m = pos[3 * i + 1]; } return m; };
  const l0 = top(d.pos0, 1), r0 = top(d.pos0, -1), l1 = top(d.pos, 1), r1 = top(d.pos, -1);
  const sl = d.ids.map((id, j) => { const q = d.ranges[j]; let a = 0, b = 0; for (let i = q.first; i < q.first + q.count; i++) { a += d.pos0[3 * i + 1]; b += d.pos[3 * i + 1]; } return id[0] + id.slice(-1) + ' dy ' + ((b - a) / q.count * 100).toFixed(1); });
  out.push(`${P.padEnd(9)} tilt(L-R top) ${((l1 - r1) * 100).toFixed(1).padStart(6)} cm  drop L ${((l1 - l0) * 100).toFixed(1).padStart(6)} R ${((r1 - r0) * 100).toFixed(1).padStart(6)} cm | mean dy cm: ${sl.join(' ')}`);
}
console.log(out.join('\n'));
