// Headless check of the mannequin, its collider and the drapes:   node tools/verify_body.js [--quick] [--legacy]
//
// Runs the app's own code: app/src/body.js generates every preset, app/src/worker.js (with shared.js, in a VM
// context, on wasm/failoring.wasm) bakes the collider and simulates the plans of app/src/patterns.js exactly as
// the browser does. Reports and writes to out/verify_body.json:
//   body      fit residuals, regeneration time, measurements
//   collider  bake time, grid size, collider surface vs the rendered polygons (by body region)
//   drape     frames, ms/frame, kinetic energy, seam gap, penetration of the rendered cloth into the rendered body,
//             and how far cloth that rests on the body is drawn above the skin
// --legacy also runs the average body with the old primitive mannequin (for the before/after ms/frame).
'use strict';
const vm = require('vm'), fs = require('fs'), path = require('path');
const root = path.join(__dirname, '..');
const R = (...p) => path.join(root, ...p);
const FL = require(R('app/src/shared.js'));
const Body = require(R('app/src/body.js'));
const args = process.argv.slice(2), QUICK = args.includes('--quick'), LEGACY = args.includes('--legacy');
const FRAMES = args.includes('--frames') ? +args[args.indexOf('--frames') + 1] : 48;
const PLANS = vm.runInNewContext(fs.readFileSync(R('app/src/patterns.js'), 'utf8') + ';PLANS', {});
const FABRICS = JSON.parse(fs.readFileSync(R('materials/fabrics.json'), 'utf8')).fabrics;
FL.FABRICS = FABRICS;
const QUALITY = { standard: { edge: 0.040, sub: 2, newton: 8, frames: FRAMES } };   // as App.QUALITY.standard (48 frames in the app)

// ---------------------------------------------------------------- the worker, in a VM context
function makeWorker() {
  const posted = [];
  const ctx = vm.createContext({ console, performance, setTimeout, clearTimeout, Date, Math, JSON, WebAssembly, BigInt, Proxy, Promise, Error, String,
    Float32Array, Float64Array, Int32Array, Uint32Array, Uint8Array, Uint16Array, Int16Array, DataView, ArrayBuffer, Array, Object, Number, Infinity, NaN, isFinite,
    postMessage: (m) => posted.push(m) });
  const src = fs.readFileSync(R('app/src/shared.js'), 'utf8') + '\n' + fs.readFileSync(R('app/src/worker.js'), 'utf8') + '\nglobalThis.__X = () => X;';
  vm.runInContext(src, ctx, { filename: 'worker.js' });
  ctx.onmessage({ data: { type: 'init', wasm: new Uint8Array(fs.readFileSync(R('wasm/failoring.wasm'))).buffer, fabrics: FABRICS } });
  return { ctx, posted, X: () => ctx.__X() };
}
const W = makeWorker();
function simulate(spec) {
  W.posted.length = 0;
  W.ctx.onmessage({ data: { type: 'build', spec } });
  return new Promise((resolve, reject) => {
    const poll = () => {
      const err = W.posted.find((m) => m.type === 'error');
      if (err) return reject(new Error(err.msg));
      if (W.posted.some((m) => m.type === 'done')) return resolve(W.posted.slice());
      setTimeout(poll, 5);
    };
    poll();
  });
}

// ---------------------------------------------------------------- exact distance to the rendered body polygons
function polyIndex(pos, tris) {
  const cell = 0.02, map = new Map(), key = (i, j, k) => ((i + 1000) * 4096 + (j + 1000)) * 4096 + (k + 1000);   // exact, no collisions
  for (let t = 0; t < tris.length; t += 3) {
    let lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9];
    for (let c = 0; c < 3; c++) for (let a = 0; a < 3; a++) { const v = pos[3 * tris[t + c] + a]; lo[a] = Math.min(lo[a], v); hi[a] = Math.max(hi[a], v); }
    for (let i = Math.floor(lo[0] / cell); i <= Math.floor(hi[0] / cell); i++) for (let j = Math.floor(lo[1] / cell); j <= Math.floor(hi[1] / cell); j++)
      for (let k = Math.floor(lo[2] / cell); k <= Math.floor(hi[2] / cell); k++) { const kk = key(i, j, k); if (!map.has(kk)) map.set(kk, []); map.get(kk).push(t); }
  }
  // signed distance (sign from the closest triangle's face normal; reliable within a few mm of a smooth surface)
  return (p) => {
    const i0 = Math.floor(p[0] / cell), j0 = Math.floor(p[1] / cell), k0 = Math.floor(p[2] / cell); let best = 1e9, sgn = 1; const seen = new Set();
    for (let i = i0 - 1; i <= i0 + 1; i++) for (let j = j0 - 1; j <= j0 + 1; j++) for (let k = k0 - 1; k <= k0 + 1; k++) {
      const L = map.get(key(i, j, k)); if (!L) continue;
      for (const t of L) {
        if (seen.has(t)) continue; seen.add(t);
        const a = tris[t] * 3, b = tris[t + 1] * 3, c = tris[t + 2] * 3;
        const A = [pos[a], pos[a + 1], pos[a + 2]], Bv = [pos[b], pos[b + 1], pos[b + 2]], C = [pos[c], pos[c + 1], pos[c + 2]];
        const q = closest(p, A, Bv, C), d = Math.hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]);
        if (d < best) { best = d; const n = cross(sub(Bv, A), sub(C, A)); sgn = dot(sub(p, q), n) >= 0 ? 1 : -1; }
      }
    }
    return best > cell ? Math.max(best, cell) : best * sgn;   // farther than one cell: outside (the body is closed)
  };
}
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]], dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const add = (a, b, s) => [a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s];
function closest(p, a, b, c) {   // Ericson 5.1.5
  const ab = sub(b, a), ac = sub(c, a), ap = sub(p, a), d1 = dot(ab, ap), d2 = dot(ac, ap);
  if (d1 <= 0 && d2 <= 0) return a;
  const bp = sub(p, b), d3 = dot(ab, bp), d4 = dot(ac, bp);
  if (d3 >= 0 && d4 <= d3) return b;
  const vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) return add(a, ab, d1 / (d1 - d3));
  const cp = sub(p, c), d5 = dot(ab, cp), d6 = dot(ac, cp);
  if (d6 >= 0 && d5 <= d6) return c;
  const vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) return add(a, ac, d2 / (d2 - d6));
  const va = d3 * d6 - d5 * d4;
  if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) return add(b, sub(c, b), (d4 - d3) / (d4 - d3 + d5 - d6));
  const den = 1 / (va + vb + vc); return add(add(a, ab, vb * den), ac, vc * den);
}
const pct = (arr, q) => { const a = Float64Array.from(arr).sort(); return a.length ? a[Math.min(a.length - 1, Math.floor(q * (a.length - 1)))] : NaN; };
const mm = (v) => +(v * 1000).toFixed(2);

// grid query through the wasm API
function gridEval(pts) {
  const X = W.X(), n = pts.length / 3, mem = X.memory;
  const p = X.fl_alloc(pts.length * 4), o = X.fl_alloc(n * 16);
  new Float32Array(mem.buffer, p, pts.length).set(pts);
  X.fl_body_eval(p, n, o);
  const out = new Float32Array(mem.buffer, o, n * 4).slice();
  X.fl_free(p); X.fl_free(o);
  return out;
}

(async () => {
  const report = { date: new Date().toISOString(), body: {}, collider: {}, drape: [], legacy: [] };
  await W.ctx.loadWasm();
  const t0 = Date.now();
  const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
  report.asset = { bytes: M.bytes, loadMs: +M.loadMs.toFixed(1) };
  console.log(`asset ${(M.bytes / 1024).toFixed(0)} KB, load + calibration ${M.loadMs.toFixed(0)} ms`);
  const presets = args.includes('--preset') ? [args[args.indexOf('--preset') + 1]] : QUICK ? ['average', 'curvy', 'plus'] : Object.keys(FL.PRESETS);
  const plans = args.includes('--plan') ? [args[args.indexOf('--plan') + 1]] : QUICK ? ['pencil', 'aline'] : Object.keys(PLANS);
  for (const name of presets) {
    const meas = FL.PRESETS[name];
    const body = Body.generate(M, meas, null);
    // regeneration while dragging: 10 small waist steps with warm start (what the UI does)
    let prev = body; const drag = [];
    for (let k = 1; k <= 10; k++) { const b = Body.generate(M, Object.assign({}, meas, { waist: meas.waist + 0.002 * k }), prev); drag.push(b.ms); prev = b; }
    const res = Math.max(...Object.values(body.residual).map(Math.abs));
    report.body[name] = { ms: +body.ms.toFixed(1), dragMs: +pct(drag, 0.5).toFixed(1), dragMaxMs: +Math.max(...drag).toFixed(1), maxResidualMm: mm(res), weight: +body.mac.weight.toFixed(2),
      m: Object.fromEntries(Object.entries(body.m).map(([k, v]) => [k, +(v * 100).toFixed(1)])) };
    console.log(`\n== ${name}: generated in ${body.ms.toFixed(0)} ms (drag ${pct(drag, 0.5).toFixed(1)} ms median), fit residual max ${mm(res)} mm, weight ${body.mac.weight.toFixed(2)}`);
    // collider: bake through a tiny build (no pieces simulated yet) -> use the first plan's build below; bake explicitly here
    const X = W.X();
    const tris = Int32Array.from(body.tris);
    const pp = X.fl_alloc(body.pos.length * 4), tp = X.fl_alloc(tris.length * 4);
    new Float32Array(X.memory.buffer, pp, body.pos.length).set(body.pos); new Int32Array(X.memory.buffer, tp, tris.length).set(tris);
    const bakeMs = X.fl_set_body_mesh(pp, body.pos.length / 3, tp, tris.length / 3, 0);
    X.fl_free(pp); X.fl_free(tp);
    const st = new Float64Array(X.memory.buffer, X.fl_body_stats(), 8).slice();
    // collider surface vs rendered polygons: vertices, edge midpoints and centroids, by region
    const regions = { 'trunk+legs+arms': ['trunk', 'leg.L', 'leg.R', 'arm.L', 'arm.R', 'neck'], 'hands+feet+head': ['hand.L', 'hand.R', 'foot.L', 'foot.R', 'head'] };
    const dev = {};
    for (const [rn, parts] of Object.entries(regions)) {
      const ok = new Set(parts.map((p) => M.parts.indexOf(p))), pts = [];
      for (let t = 0; t < tris.length; t += 3) {
        if (!ok.has(M.part[tris[t]]) || !ok.has(M.part[tris[t + 1]]) || !ok.has(M.part[tris[t + 2]])) continue;
        const v = [0, 1, 2].map((c) => [body.pos[3 * tris[t + c]], body.pos[3 * tris[t + c] + 1], body.pos[3 * tris[t + c] + 2]]);
        const cen = [0, 1, 2].map((a) => (v[0][a] + v[1][a] + v[2][a]) / 3);
        pts.push(...v[0], ...cen, ...v[0].map((x, a) => (x + v[1][a]) / 2));
      }
      const g = gridEval(Float32Array.from(pts)), d = []; for (let i = 0; i < g.length; i += 4) d.push(g[i]);
      const ad = d.map(Math.abs);
      dev[rn] = { medianMm: mm(pct(ad, 0.5)), p95Mm: mm(pct(ad, 0.95)), minMm: mm(Math.min(...d)), maxMm: mm(Math.max(...d)) };
    }
    report.collider[name] = { bakeMs: +bakeMs.toFixed(0), bricks: st[1], fineSamples: st[2], coarseSamples: st[3], MB: +(st[4] / 1e6).toFixed(1), hMm: st[6] * 1000, deviation: dev };
    console.log(`   collider: bake ${bakeMs.toFixed(0)} ms in wasm, ${st[2]} fine + ${st[3]} coarse samples, ${(st[4] / 1e6).toFixed(1)} MB; surface vs polygons ` +
      Object.entries(dev).map(([k, v]) => `${k}: median ${v.medianMm} / 95% ${v.p95Mm} mm`).join('; '));
    const dist = polyIndex(body.pos, body.tris);
    for (const plan of plans) {
      const P = JSON.parse(JSON.stringify(PLANS[plan]));
      P.pieces.forEach(FL.normalizePiece);
      const fab = FL.fabricById(P.fabric), q = QUALITY.standard;
      const spec = { job: Math.floor(Math.random() * 1e9), meas, fabric: fab, quality: q, rampFrames: 20, seamStiffness: 20000, selfCollision: false, frames: q.frames,
        pieces: P.pieces.map((p) => ({ id: p.id, outline: p.outline, grain: p.grain, wrap: p.wrap })), seams: P.seams,
        body: { key: body.key, pos: body.pos.slice(), tris, sections: body.sections } };
      const tt = Date.now();
      const msgs = await simulate(spec);
      const meta = msgs.find((m) => m.type === 'meta'), frames = msgs.filter((m) => m.type === 'frame' && m.i > 0), last = frames[frames.length - 1];
      const msF = frames.map((f) => f.ms);
      // rendered cloth: vertices, edge midpoints and triangle centroids of the displayed (lifted) positions
      const D = last.pos, T = meta.dtris, Xw = W.X();   // the rendered (display) cloth mesh
      const raw = new Float32Array(Xw.memory.buffer, Xw.fl_positions(), meta.n * 3).slice();
      const rawPhi = gridEval(raw);
      let pen = 1e9, nIn = 0, nPts = 0, worst = null; const restGap = [];
      for (let t = 0; t < T.length; t += 3) {
        const v = [0, 1, 2].map((c) => [D[3 * T[t + c]], D[3 * T[t + c] + 1], D[3 * T[t + c] + 2]]);
        for (const p of [[(v[0][0] + v[1][0] + v[2][0]) / 3, (v[0][1] + v[1][1] + v[2][1]) / 3, (v[0][2] + v[1][2] + v[2][2]) / 3], v[0].map((x, a) => (x + v[1][a]) / 2)]) {
          const d = dist(p); nPts++; if (d < pen) { pen = d; worst = { p: p.map((x) => +x.toFixed(3)), tri: [T[t], T[t + 1], T[t + 2]], phi: [T[t], T[t + 1], T[t + 2]].map((i) => mm(rawPhi[4 * i])) }; } if (d < 0) nIn++;
        }
      }
      for (let i = 0; i < meta.nd; i++) {
        const d = dist([D[3 * i], D[3 * i + 1], D[3 * i + 2]]); if (d < pen) pen = d; if (d < 0) nIn++; nPts++;
        if (i >= meta.n) continue;
        if (rawPhi[4 * i] < 0.0025 && D[3 * i + 1] > 0.01) restGap.push(d);   // resting on the body (not the floor)
      }
      const r = { preset: name, plan, n: meta.n, frames: frames.length, wallS: +((Date.now() - tt) / 1000).toFixed(1), msPerFrame: +pct(msF, 0.5).toFixed(0),
        bakeMs: meta.bake ? +meta.bake.ms.toFixed(0) : 0, ke: +last.ke.toExponential(1), seamGapMm: mm(last.gap), minClearanceMm: mm(last.clear),
        renderedPenetrationMm: mm(Math.max(0, -pen)), pointsInside: nIn, points: nPts, resting: restGap.length,
        restingGapMm: restGap.length ? { median: mm(pct(restGap, 0.5)), p95: mm(pct(restGap, 0.95)), max: mm(Math.max(...restGap)) } : null };
      if (pen < 0) r.worst = worst;
      report.drape.push(r);
      console.log(`   ${plan.padEnd(6)} ${r.n} verts, ${r.frames} frames, ${r.msPerFrame} ms/frame, KE ${r.ke} J, seam gap ${r.seamGapMm} mm, clearance ${r.minClearanceMm} mm, ` +
        `rendered cloth inside the body: ${r.pointsInside}/${r.points} points (deepest ${r.renderedPenetrationMm} mm); resting on the body: ${r.resting} verts drawn ` +
        (r.restingGapMm ? `${r.restingGapMm.median} mm (95% ${r.restingGapMm.p95}, max ${r.restingGapMm.max}) above the skin` : '-') + (r.worst ? `\n          deepest at ${JSON.stringify(r.worst)}` : ''));
    }
  }
  if (LEGACY) {
    console.log('\n== legacy primitive mannequin (average), for the before/after comparison');
    for (const plan of plans) {
      const P = JSON.parse(JSON.stringify(PLANS[plan])); P.pieces.forEach(FL.normalizePiece);
      const fab = FL.fabricById(P.fabric), q = QUALITY.standard, meas = Object.assign({}, FL.PRESETS.average, { shoulder: 0.40 });
      const spec = { job: Math.floor(Math.random() * 1e9), meas, fabric: fab, quality: q, rampFrames: 20, seamStiffness: 20000, selfCollision: false, frames: q.frames,
        pieces: P.pieces.map((p) => ({ id: p.id, outline: p.outline, grain: p.grain, wrap: p.wrap })), seams: P.seams, body: null };
      const msgs = await simulate(spec), meta = msgs.find((m) => m.type === 'meta'), frames = msgs.filter((m) => m.type === 'frame' && m.i > 0), last = frames[frames.length - 1];
      const r = { plan, n: meta.n, frames: frames.length, msPerFrame: +pct(frames.map((f) => f.ms), 0.5).toFixed(0), ke: +last.ke.toExponential(1), seamGapMm: mm(last.gap) };
      report.legacy.push(r);
      console.log(`   ${plan.padEnd(6)} ${r.n} verts, ${r.msPerFrame} ms/frame, KE ${r.ke} J, seam gap ${r.seamGapMm} mm`);
    }
  }
  report.totalS = (Date.now() - t0) / 1000;
  fs.mkdirSync(R('out'), { recursive: true });
  fs.writeFileSync(R('out/verify_body.json'), JSON.stringify(report, null, 1));
  console.log(`\nwrote out/verify_body.json (${report.totalS.toFixed(0)} s)`);
})().catch((e) => { console.error(e); process.exit(1); });
