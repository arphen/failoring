// Simulation worker: owns the WebAssembly cloth core, builds a garment from a pattern spec and streams frames.
'use strict';
let wasmBytes = null, X = null, mem = null, curJob = 0;
let bodyKey = null;            // key of the body baked into the wasm collider grid (null = none)
const DISPLAY_MARGIN = 0.0008; // m: cloth triangles are drawn at least this far outside the body (fl_display_positions)

function loadWasm() {
  const wasi = new Proxy({}, { get: (_, name) => {
    if (name === 'fd_write') return (fd, iov, n, out) => { let t = 0; const dv = new DataView(mem.buffer); for (let i = 0; i < n; i++) t += dv.getUint32(iov + i * 8 + 4, true); dv.setUint32(out, t, true); return 0; };
    if (name === 'proc_exit') return (c) => { throw new Error('exit ' + c); };
    if (name === 'clock_time_get') return (id, prec, out) => { new DataView(mem.buffer).setBigUint64(out, BigInt(Date.now()) * 1000000n, true); return 0; };
    if (name === 'environ_sizes_get' || name === 'args_sizes_get') return (a, b) => { const dv = new DataView(mem.buffer); dv.setUint32(a, 0, true); dv.setUint32(b, 0, true); return 0; };
    return () => 0;
  } });
  return WebAssembly.instantiate(wasmBytes, { wasi_snapshot_preview1: wasi }).then(({ instance }) => {
    X = instance.exports; mem = X.memory; bodyKey = null;
    if (X._initialize) X._initialize();
  });
}
const put = (arr) => { const p = X.fl_alloc(arr.byteLength); new arr.constructor(mem.buffer, p, arr.length).set(arr); return p; };
const f32 = (ptr, n) => new Float32Array(mem.buffer, ptr, n).slice();
const tick = () => new Promise((r) => setTimeout(r, 0));

let sim = null, runToken = 0, active = false;   // { spec, n, nframes, pieces, ... }

async function build(spec) {
  curJob = spec.job;
  if (!X) await loadWasm();
  const m = spec.meas;
  X.fl_init();
  const f = spec.fabric, pr = f.params;
  const mat = X.fl_add_material(pr.density_kg_m2, pr.thickness_m, pr.tensile_warp_N_m, pr.tensile_weft_N_m, pr.shear_N_m_rad,
    pr.bending_warp_Nm, pr.bending_weft_Nm, pr.compression_ratio, pr.friction_static, pr.friction_kinetic, pr.damping_1_s);
  X.fl_set_param(0, spec.quality.sub); X.fl_set_param(1, spec.quality.newton); X.fl_set_param(3, spec.rampFrames);
  X.fl_set_param(2, spec.seamStiffness); X.fl_set_param(4, spec.selfCollision ? 1 : 0);
  const FLOOR = { t: 'p', p: [0, 0, 0], n: [0, 1, 0] };
  let bake = null;
  if (spec.body) {
    // mesh body: bake its signed-distance grid once per body shape (the grid survives fl_init), collide with it and the floor
    if (spec.body.key !== bodyKey) {
      const pp = put(spec.body.pos), tp = put(spec.body.tris);
      const ms = X.fl_set_body_mesh(pp, spec.body.pos.length / 3, tp, spec.body.tris.length / 3, spec.body.h || 0);
      X.fl_free(pp); X.fl_free(tp);
      if (ms < 0) throw new Error('body collider: the mesh could not be baked');
      bodyKey = spec.body.key;
      const st = new Float64Array(mem.buffer, X.fl_body_stats(), 8);
      bake = { ms: st[0], bricks: st[1], fine: st[2], coarse: st[3], bytes: st[4], evals: st[5], h: st[6] };
    } else X.fl_body_enable(1);
    const p = put(FL.collidersFlat([FLOOR])); X.fl_set_colliders(p, 1); X.fl_free(p);
  } else {
    X.fl_body_enable(0);
    const prims0 = FL.bodyPrims(m, null).concat([FLOOR]);
    const p = put(FL.collidersFlat(prims0)); X.fl_set_colliders(p, prims0.length); X.fl_free(p);
  }
  X.fl_begin();
  const idx = [], pieces = spec.pieces;
  for (const pc of pieces) {
    const o = new Float64Array(pc.outline.length * 2);
    pc.outline.forEach((q, i) => { o[2 * i] = q[0]; o[2 * i + 1] = q[1]; });
    const ptr = put(o);
    idx.push(X.fl_add_panel(ptr, pc.outline.length, spec.quality.edge, mat, pc.grain));
    X.fl_free(ptr);
  }
  const byId = {}; pieces.forEach((pc, i) => (byId[pc.id] = i));
  for (const s of spec.seams) {
    const a = byId[s.a[0]], b = byId[s.b[0]];
    if (a === undefined || b === undefined || a === b) continue;
    X.fl_add_seam(idx[a], s.a[1], idx[b], s.b[1], s.reverse ? 1 : 0);
  }
  const n = X.fl_vertex_count(), nt = X.fl_triangle_count();
  const uv = f32(X.fl_rest_uv(), n * 2);
  const tris = new Int32Array(mem.buffer, X.fl_triangles(), nt * 3).slice();
  const ranges = pieces.map((pc, i) => ({ first: X.fl_panel_first_vertex(idx[i]), count: X.fl_panel_vertex_count(idx[i]),
    triFirst: X.fl_panel_first_triangle(idx[i]), triCount: X.fl_panel_triangle_count(idx[i]) }));
  const chains = pieces.map((pc, i) => pc.outline.map((_, e) => { const q = X.fl_alloc(4 * 4096); const k = X.fl_edge_chain(idx[i], e, q, 4096); const a = new Int32Array(mem.buffer, q, k).slice(); X.fl_free(q); return Array.from(a); }));
  // display mesh: one midpoint per edge, four display triangles per cloth triangle (see fl_display_positions)
  const disp = FL.subdivide(tris, n);
  { const ep = put(disp.edges), tp = put(disp.tris); X.fl_display_setup(ep, disp.edges.length / 2, tp, disp.tris.length / 3); X.fl_free(ep); X.fl_free(tp); }
  // keyframes: exploded (upright, around the body) and wrapped on the body
  const P1 = new Float32Array(n * 3), P2 = new Float32Array(n * 3);
  pieces.forEach((pc, i) => {
    const w = FL.makeWrap(pc, spec.body || m), r = ranges[i];
    for (let k = 0; k < r.count; k++) {
      const vi = r.first + k, u = uv[2 * vi], v = uv[2 * vi + 1];
      const a = w.point(u, v), b = w.flatPoint(u, v, 0.30);
      P2.set(a, 3 * vi); P1.set(b, 3 * vi);
    }
  });
  const q = put(P2); X.fl_set_positions(q); X.fl_free(q);
  const nd = n + disp.edges.length / 2;
  sim = { spec, n, nd, frame: 0, target: spec.frames, cycle: spec.walk ? spec.walk.cycle : 0, t0: Date.now() };
  postMessage({ type: 'meta', job: spec.job, n, nd, tris, dtris: disp.tris, edges: disp.edges, uv, ranges, chains, P1, P2, warn: X.fl_warnings(), bake }, [tris.buffer]);
  postMessage({ type: 'frame', job: spec.job, i: 0, pos: FL.withMidpoints(P2, n, disp.edges), ke: 0, gap: 0 }, []);
  await run(spec.job);
}

async function run(job) {
  const tok = ++runToken; active = true;
  while (sim && sim.frame < sim.target) {
    if (job !== curJob || tok !== runToken) return;
    const s = sim, w = s.spec.body ? null : s.spec.walk, f = s.frame + 1;
    if (w && f > w.settle) {      // legacy primitive body only: it starts walking after the garment has settled
      const prims = FL.bodyPrims(s.spec.meas, ((f - w.settle) / w.cycle) % 1).concat([{ t: 'p', p: [0, 0, 0], n: [0, 1, 0] }]);
      const p = put(FL.collidersFlat(prims)); X.fl_set_colliders(p, prims.length); X.fl_free(p);
    }
    // Gravity is faded in while the seams pull the pieces together (seam ramp): pieces held in the air are sewn
    // first and then settle on the body, instead of sliding down past the hips before the garment is closed.
    const ramp = s.spec.rampFrames || 0;
    if (ramp > 0 && f <= ramp + 1) X.fl_set_param(5, -9.81 * Math.min(1, Math.pow(f / ramp, 2)));
    const t = performance.now();
    try { X.fl_step(1); } catch (e) { postMessage({ type: 'error', job, msg: String(e) }); X = null; sim = null; active = false; bodyKey = null; return; }
    s.frame = f;
    const pos = f32(X.fl_display_positions(DISPLAY_MARGIN), s.nd * 3);   // display mesh: subdivided, following the body
    postMessage({ type: 'frame', job, i: f, pos, ke: X.fl_kinetic(), gap: X.fl_seam_gap(), ms: performance.now() - t, clear: X.fl_clearance() }, [pos.buffer]);
    await tick();
  }
  if (tok === runToken) active = false;
  if (job === curJob && sim) {
    const st = new Float64Array(mem.buffer, X.fl_solver_stats(), 4);
    postMessage({ type: 'done', job, frames: sim.frame, lineSearchFailures: st[0], newtonPerSubstep: st[2] ? st[1] / st[2] : 0 });
  }
}

onmessage = (ev) => {
  const d = ev.data;
  if (d.type === 'init') { wasmBytes = d.wasm; FL.FABRICS = d.fabrics; }
  else if (d.type === 'build') { build(d.spec).catch((e) => postMessage({ type: 'error', job: d.spec.job, msg: String(e && e.stack || e) })); }
  else if (d.type === 'more') { if (sim && d.job === curJob) { sim.target = sim.frame + d.count; if (!active) run(d.job); } }
  else if (d.type === 'cancel') { curJob = -1; }
};
