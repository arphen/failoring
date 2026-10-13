// stretch.js <dump> [piece] : current / rest length of the lower and upper long edge of a (band) piece, first and last frame
const d = require(process.argv[2]), pi = +(process.argv[3] || 4);
const ch = d.chains[pi];
const U = (i, k) => d.uv[2 * i + k];
let y0 = 1e9, y1 = -1e9; for (const [a, b] of ch) for (const v of [a, b]) { y0 = Math.min(y0, U(v, 1)); y1 = Math.max(y1, U(v, 1)); }
const sel = (f) => ch.filter(([a, b]) => f(U(a, 1)) && f(U(b, 1)));
const lo = sel((y) => y < y0 + 0.004), hi = sel((y) => y > y1 - 0.004);
const L2 = (E) => E.reduce((s, [a, b]) => s + Math.hypot(U(a, 0) - U(b, 0), U(a, 1) - U(b, 1)), 0);
const L3 = (E, P) => E.reduce((s, [a, b]) => s + Math.hypot(P[3*a] - P[3*b], P[3*a+1] - P[3*b+1], P[3*a+2] - P[3*b+2]), 0);
for (const [nm, E] of [['lower', lo], ['upper', hi]]) console.log(nm, 'edges', E.length, 'rest', (L2(E) * 100).toFixed(1), 'cm; frame0', (L3(E, d.pos0) * 100).toFixed(1), 'last', (L3(E, d.pos) * 100).toFixed(1), 'ratio', (L3(E, d.pos) / L2(E)).toFixed(3));
// mean height of the band vs the waist
let ys = 0; const seen = new Set(); for (const [a, b] of ch) for (const v of [a, b]) if (!seen.has(v)) { seen.add(v); ys += d.pos[3 * v + 1]; } console.log('mean band height last frame', (ys / seen.size * 100).toFixed(1), 'cm');
