const { load } = require('./wasm_host.js');
(async () => {
  const x = await load(process.argv[2] || 'wasm/failoring.wasm');
  x.fl_init();
  // cotton_poplin
  const mat = x.fl_add_material(0.12, 0.0003, 6000, 4000, 45, 1.0e-5, 8.0e-6, 0.3, 0.5, 0.35, 0.6);
  x.fl_set_param(0, 4); x.fl_set_param(3, 0);
  const W = 0.40, H = 0.30, h = 0.02, y0 = 0.25;
  const Rad = [0.17, 0.35, 0.11];
  const col = new Float64Array([2, 0, 0, 0, Rad[0], Rad[1], Rad[2], 0]);
  let p = x.fl_alloc(col.length * 8); new Float64Array(x.memory.buffer, p, col.length).set(col); x.fl_set_colliders(p, 1);
  x.fl_begin();
  const poly = new Float64Array([0, 0, W, 0, W, H, 0, H]);
  const pp = x.fl_alloc(64); new Float64Array(x.memory.buffer, pp, 8).set(poly);
  const f = x.fl_add_panel(pp, 4, h, mat, Math.PI / 2), b = x.fl_add_panel(pp, 4, h, mat, Math.PI / 2);
  x.fl_add_seam(f, 1, b, 3, 1); x.fl_add_seam(f, 3, b, 1, 1);
  const n = x.fl_vertex_count();
  const uv = new Float32Array(x.memory.buffer, x.fl_rest_uv(), n * 2).slice();
  const pos = new Float32Array(n * 3), Rc = W / Math.PI;
  for (const [pi, sgn] of [[f, 1], [b, -1]]) {
    const first = x.fl_panel_first_vertex(pi), cnt = x.fl_panel_vertex_count(pi);
    for (let i = 0; i < cnt; i++) { const u = uv[2 * (first + i)], v = uv[2 * (first + i) + 1], th = Math.PI * u / W;
      pos[3 * (first + i)] = -sgn * Rc * Math.cos(th); pos[3 * (first + i) + 1] = y0 + v; pos[3 * (first + i) + 2] = sgn * Rc * Math.sin(th); }
  }
  const pb = x.fl_alloc(pos.byteLength); new Float32Array(x.memory.buffer, pb, pos.length).set(pos); x.fl_set_positions(pb);
  console.log('verts', n, 'tris', x.fl_triangle_count(), 'warn', x.fl_warnings());
  const t0 = Date.now();
  for (let fr = 0; fr < (+process.env.FR||150); fr++) { x.fl_step(1); if (fr % 50 === 0) console.log(fr, 'KE', x.fl_kinetic().toExponential(2), 'gap', (x.fl_seam_gap() * 1000).toFixed(2), 'mm'); }
  const P = new Float32Array(x.memory.buffer, x.fl_positions(), n * 3);
  let hem = 0, hc = 0;
  for (const pi of [f, b]) { const ch = new Int32Array(64); const q = x.fl_alloc(256); const k = x.fl_edge_chain(pi, 0, q, 64); const a = new Int32Array(x.memory.buffer, q, k); for (const v of a) { hem += P[3 * v + 1]; hc++; } }
  console.log('hem y =', (hem / hc * 1000).toFixed(1), 'mm (native ~158, predicted 161); time', (Date.now() - t0) / 1000, 's for 150 frames');
})();
