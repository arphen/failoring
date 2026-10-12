// Measures the numbers in docs/NUMBERS.md and writes that file:   node tools/measure_numbers.js [options]
//   --walk-reps N    repeats of each walk_sim.js run (default 3)
//   --regen-reps N   repeats per (preset, slider) for body regeneration (default 20)
//   --out DIR        scratch directory (default out/numbers, git-ignored)
//   --md FILE        markdown to write (default docs/NUMBERS.md)
//   --skip-walk      skip the walk_sim.js runs (they take minutes)
//   --render-only    rewrite --md from the numbers.json of an earlier run in --out, without measuring
//
// 1. body regeneration: app/src/body.js (Body.load + Body.generate on app/assets/body_mh.bin), every preset of
//    FL.PRESETS, each of the five primary sliders moved by 2 mm, regen-reps times. "cold" = no warm start and the
//    Jacobian cache cleared; "warm" = the previous result is the warm start, as while dragging a slider.
// 2. collider bake and step time: tools/walk_sim.js (pencil, average) run as a child process, STAND=1 with 40
//    frames and walking with 80 frames, walk-reps times each. Numbers are the tool's own "bake ms avg" and
//    "step ms avg" lines.
// 3. download size: node app/build.js into DIR (not the committed failoring.html), plus the embedded assets and
//    the wasm, and the committed reference build d7e62e0.
// 4. mesh sizes: the body (vertices and triangles from Body.generate and the mesh) and each garment built by the
//    worker (the same worker code as the app, in a VM context on wasm/failoring.wasm), one frame, standing body.
// Every section records os.loadavg() before and after, because the machine is shared.
'use strict';
const vm = require('vm'), fs = require('fs'), path = require('path'), os = require('os'), zlib = require('zlib');
const { spawnSync } = require('child_process');

const root = path.join(__dirname, '..'), R = (...p) => path.join(root, ...p);
const args = process.argv.slice(2);
const opt = (name, def) => (args.includes(name) ? args[args.indexOf(name) + 1] : def);
const WALK_REPS = +opt('--walk-reps', 3), REGEN_REPS = +opt('--regen-reps', 20);
const OUT = path.resolve(opt('--out', R('out/numbers'))), MD = path.resolve(opt('--md', R('docs/NUMBERS.md')));
const SKIP_WALK = args.includes('--skip-walk'), BAKE_REPS = 5;
fs.mkdirSync(OUT, { recursive: true });

const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js'));
require(R('app/src/gait.js')); require(R('app/src/draft.js'));
const PLANS = vm.runInNewContext(fs.readFileSync(R('app/src/patterns.js'), 'utf8') + ';PLANS', {});
const FABRICS = JSON.parse(fs.readFileSync(R('materials/fabrics.json'), 'utf8')).fabrics; FL.FABRICS = FABRICS;
const ASSET_PATH = R('app/assets/body_mh.bin'), WASM_PATH = R('wasm/failoring.wasm');
const asset = new Uint8Array(fs.readFileSync(ASSET_PATH));

const load = () => os.loadavg().map((x) => +x.toFixed(2));
const stats = (xs) => {
  const s = xs.slice().sort((a, b) => a - b), n = s.length;
  const med = n % 2 ? s[(n - 1) / 2] : (s[n / 2 - 1] + s[n / 2]) / 2;
  return { n, median: med, min: s[0], max: s[n - 1] };
};
const r1 = (x) => +x.toFixed(1), r2 = (x) => +x.toFixed(2);
const kB = (b) => (b / 1024).toFixed(1) + ' KB', MB = (b) => (b / 1e6).toFixed(3) + ' MB';
const report = { node: process.version, cpus: os.cpus().length, startedAt: new Date().toISOString(), loadStart: load() };

// ------------------------------------------------------------------ 1. body regeneration
function regenSection() {
  const loadBefore = load();
  const M = Body.load(asset);
  const out = { loadMs: r1(M.loadMs), loadBefore, presets: {} };
  for (const [name, preset] of Object.entries(FL.PRESETS)) {
    out.presets[name] = { sliders: {} };
    for (const slider of Body.PRIMARY) {
      const base = preset[slider];
      const meas = (k) => Object.assign({}, preset, { [slider]: base + (k % 2 ? 0.002 : 0) });
      const cold = [], warm = [];
      for (let k = 1; k <= REGEN_REPS; k++) {
        M.jcache.clear();
        let t = performance.now(); Body.generate(M, meas(k), null); cold.push(performance.now() - t);
      }
      let prev = Body.generate(M, preset, null);            // warm chain starts from the preset's own body
      for (let k = 1; k <= REGEN_REPS; k++) {
        const t = performance.now(); prev = Body.generate(M, meas(k), prev); warm.push(performance.now() - t);
      }
      out.presets[name].sliders[slider] = { cold: stats(cold), warm: stats(warm) };
    }
    // per-preset vertex count of the generated body (constant topology, checked here)
    out.presets[name].vertices = Body.generate(M, preset, null).pos.length / 3;
  }
  out.triangles = M.tris.length / 3;
  out.loadAfter = load();
  console.log('regen done', JSON.stringify(out.loadAfter));
  return out;
}

// ------------------------------------------------------------------ 2. collider bake and step time (walk_sim.js)
// walk_sim.js prints "bake ms avg" and "step ms avg", but its step average is NaN (frame 0 carries no ms) and its
// STAND bake average is 0 (only walking frames carry a bake). So the numbers here are recomputed from the per-frame
// arrays walk_sim.js writes next to its PNG (walk_pencil_average.json); the printed line is kept verbatim.
const mean = (xs) => (xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : null);
function walkSection(stand, frames) {
  const runs = [];
  const dir = path.join(OUT, stand ? 'stand' : 'walk'), jsonPath = path.join(dir, 'walk_pencil_average.json');
  for (let i = 0; i < WALK_REPS; i++) {
    const env = Object.assign({}, process.env); delete env.STAND; if (stand) env.STAND = '1';
    try { fs.unlinkSync(jsonPath); } catch (e) { /* first run */ }
    const loadBefore = load(), t = Date.now();
    const r = spawnSync(process.execPath, [R('tools/walk_sim.js'), 'pencil', 'average', String(frames), dir],
      { cwd: root, env, encoding: 'utf8', timeout: 1200000, maxBuffer: 64e6 });
    const printed = /bake ms avg .*/.exec(r.stdout || '');
    let d = null; try { d = JSON.parse(fs.readFileSync(jsonPath, 'utf8')); } catch (e) { /* no output */ }
    const bakeMs = d ? mean(d.bakes.filter((x) => x != null)) : null, stepMs = d ? mean(d.ms.filter((x) => x != null)) : null;
    runs.push({ loadBefore, loadAfter: load(), wallS: r2((Date.now() - t) / 1000), exit: r.status, printed: printed ? printed[0] : null,
      bakeMs, stepMs, bakeFrames: d ? d.bakes.length : 0, stepFrames: d ? d.ms.filter((x) => x != null).length : 0,
      error: d ? null : (r.stderr || r.stdout || '').trim().split('\n').slice(-2).join(' | ') });
    console.log(`walk_sim ${stand ? 'STAND' : 'walk'} run ${i + 1}: printed "${printed ? printed[0] : '?'}"; recomputed bake ${r1(bakeMs || 0)} ms (${bakeMs == null ? 'n/a' : 'walking frames'}), step ${r1(stepMs || 0)} ms, exit ${r.status}`);
  }
  const ok = runs.filter((x) => x.stepMs != null);
  return { frames, runs, bake: stats(runs.filter((x) => x.bakeMs != null).map((x) => x.bakeMs)), step: stats(ok.map((x) => x.stepMs)) };
}

// ------------------------------------------------------------------ 3. download size
const blobLen = (buf, id) => { const m = new RegExp('id="' + id + '"[^>]*>([^<]*)<').exec(Buffer.from(buf).toString('latin1')); return m ? m[1].length : 0; };
function sizeSection() {
  const htmlOut = path.join(OUT, 'failoring.html');
  const t = Date.now();
  const b = spawnSync(process.execPath, [R('app/build.js'), htmlOut], { cwd: root, encoding: 'utf8' });
  const html = fs.readFileSync(htmlOut);
  const bodyBuf = fs.readFileSync(ASSET_PATH), wasmBuf = fs.readFileSync(WASM_PATH);
  const ref = spawnSync('git', ['show', 'd7e62e0:failoring.html'], { cwd: root, maxBuffer: 64e6 });
  const headRef = spawnSync('git', ['show', 'HEAD:failoring.html'], { cwd: root, maxBuffer: 64e6 });
  return {
    buildExit: b.status, buildS: r2((Date.now() - t) / 1000), buildOut: (b.stdout || '').trim(),
    html: html.length, htmlGzip: zlib.gzipSync(html).length,
    bodyAsset: bodyBuf.length, bodyAssetBase64: Buffer.from(bodyBuf).toString('base64').length,
    wasm: wasmBuf.length, wasmBase64: Buffer.from(wasmBuf).toString('base64').length,
    refD7e62e0: ref.status === 0 ? ref.stdout.length : null,
    refBlobs: ref.status === 0 ? { body: blobLen(ref.stdout, 'bodyB64'), wasm: blobLen(ref.stdout, 'wasmB64') } : null,
    blobs: { body: blobLen(html, 'bodyB64'), wasm: blobLen(html, 'wasmB64') },
    refHeadCommitted: headRef.status === 0 ? headRef.stdout.length : null,
  };
}

// ------------------------------------------------------------------ 4. mesh sizes (worker code, one frame)
const VM_GLOBALS = ['console', 'performance', 'setTimeout', 'clearTimeout', 'Date', 'Math', 'JSON', 'WebAssembly', 'BigInt', 'Proxy', 'Promise', 'Error', 'String',
  'Map', 'Set', 'Float32Array', 'Float64Array', 'Int32Array', 'Uint32Array', 'Uint8Array', 'Uint16Array', 'Int16Array', 'DataView', 'ArrayBuffer', 'Array', 'Object',
  'Number', 'Infinity', 'NaN', 'isFinite', 'TextDecoder'];
function makeWorker() {
  const posted = [];
  const sandbox = { postMessage: (m) => posted.push(m) };
  VM_GLOBALS.forEach((g) => (sandbox[g] = globalThis[g]));
  const ctx = vm.createContext(sandbox);
  const src = ['shared.js', 'body.js', 'gait.js', 'worker.js'].map((f) => fs.readFileSync(R('app/src', f), 'utf8')).join('\n');
  vm.runInContext(src, ctx, { filename: 'worker.js' });
  ctx.onmessage({ data: { type: 'init', wasm: new Uint8Array(fs.readFileSync(WASM_PATH)).buffer, fabrics: FABRICS, bodyAsset: asset } });
  return { ctx, posted };
}
function build(W, P, spec) {
  W.posted.length = 0;
  W.ctx.onmessage({ data: { type: 'build', spec } });
  return new Promise((resolve, reject) => {
    const poll = () => {
      const err = W.posted.find((m) => m.type === 'error'); if (err) return reject(new Error(err.msg));
      if (W.posted.some((m) => m.type === 'done')) return resolve(W.posted.find((m) => m.type === 'meta'));
      setTimeout(poll, 5);
    };
    poll();
  });
}
function garmentSpec(P, M, body, frames) {
  const fab = FL.fabricById(P.fabric);
  return { job: 1, meas: FL.PRESETS.average, fabric: fab, quality: { edge: 0.04, sub: 2, newton: 8, frames }, rampFrames: 20,
    seamStiffness: 20000, selfCollision: false, frames,
    pieces: P.pieces.map((p) => ({ id: p.id, outline: p.outline, grain: p.grain, wrap: p.wrap })), seams: P.seams,
    body: { key: body.key, pos: body.pos.slice(), tris: Int32Array.from(M.tris), sections: body.sections, arms: body.arms }, walk: null };
}
async function meshSection() {
  const loadBefore = load();
  const M = Body.load(asset);
  const rest = Body.generate(M, FL.PRESETS.average, null), body = FL.Gait.stand(M, rest);
  const plans = {
    pencil: JSON.parse(JSON.stringify(PLANS.pencil)), aline: JSON.parse(JSON.stringify(PLANS.aline)), blank: JSON.parse(JSON.stringify(PLANS.blank)),
    pants: FL.draft.pants(body), shirt: FL.draft.shirt(body), tank: FL.draft.tank(body),
  };
  Object.values(plans).forEach((P) => P.pieces.forEach(FL.normalizePiece));
  // collider bake of the standing average body: a fresh worker per sample, because the worker keeps the grid per body key
  const bakeSamples = [];
  for (let i = 0; i < BAKE_REPS; i++) {
    const meta = await build(makeWorker(), plans.pencil, garmentSpec(plans.pencil, M, body, 1));
    bakeSamples.push(meta.bake.ms);
  }
  console.log('collider bake samples (ms):', bakeSamples.map((x) => x.toFixed(0)).join(' '));
  const W = makeWorker(), garments = {};
  for (const [name, P] of Object.entries(plans)) {
    const meta = await build(W, P, garmentSpec(P, M, body, 1));
    garments[name] = { pieces: P.pieces.length, seams: P.seams.length, simVertices: meta.n, simTriangles: meta.tris.length / 3,
      displayVertices: meta.nd, displayTriangles: meta.dtris.length / 3, displayEdges: meta.edges.length / 2 };
    console.log(`garment ${name}: ${meta.n} sim vertices, ${meta.tris.length / 3} triangles, ${meta.nd} display vertices, ${meta.dtris.length / 3} display triangles`);
  }
  return { body: { vertices: body.pos.length / 3, triangles: M.tris.length / 3 }, garments, bakeSamples, loadBefore, loadAfter: load() };
}

// ------------------------------------------------------------------ markdown
function md(rep) {
  const L = [];
  const row = (...c) => L.push('| ' + c.join(' | ') + ' |');
  const ms = (s) => `${r1(s.median)} (${r1(s.min)}–${r1(s.max)})`;
  const ld = (a) => a.join(' / ');
  L.push('# Numbers', '',
    'Generated by `tools/measure_numbers.js` (run it to refresh this file; raw data in `out/numbers/numbers.json`, git-ignored).', '',
    `Measured ${rep.startedAt.slice(0, 10)} on Node ${rep.node}, ${rep.cpus} logical CPUs reported by the OS. ` +
    '**The machine was loaded while these ran**: it is a 2-core box shared with about 11 other agents. Every timing is a median over repeats with the min–max range in brackets, and the 1/5/15-minute load averages are given before and after each section. Treat the spreads as the real uncertainty.', '');

  L.push('## 1. Body regeneration per slider change', '',
    `Body.generate on the MakeHuman asset (\`app/assets/body_mh.bin\`, ${kB(fs.statSync(ASSET_PATH).size)}); Body.load took ${r1(rep.regen.loadMs)} ms. ` +
    `Each row is one slider moved by 2 mm, repeated ${REGEN_REPS} times. "cold" = no warm start, Jacobian cache cleared. "warm" = previous result as warm start (the drag case). ` +
    `Times are wall-clock ms per generate, median (min–max over the repeats). The last table column is the five primary sliders (height, bust, waist, hip, shoulder), warm, summarised as the median of their per-slider medians.`, '');
  row('Preset', 'waist cold', 'waist warm', 'five sliders warm: median of the per-slider medians (min–max)', 'vertices');
  row('---', '---:', '---:', '---:', '---:');
  for (const [name, p] of Object.entries(rep.regen.presets)) {
    const perSlider = Body.PRIMARY.map((s) => p.sliders[s].warm.median);
    row(name, ms(p.sliders.waist.cold), ms(p.sliders.waist.warm), ms(stats(perSlider)), p.vertices);
  }
  L.push('', 'Per-slider warm medians (ms), all presets:', '');
  row('Preset', ...Body.PRIMARY);
  row('---', ...Body.PRIMARY.map(() => '---:'));
  for (const [name, p] of Object.entries(rep.regen.presets)) row(name, ...Body.PRIMARY.map((s) => r1(p.sliders[s].warm.median)));
  L.push('');

  L.push('## 2. Collider bake and step time (tools/walk_sim.js, pencil, average)', '',
    `Medians of ${WALK_REPS} runs of \`tools/walk_sim.js pencil average <frames>\`, min–max in brackets. ` +
    'The collider bake of the STAND body is measured separately (worker `meta.bake.ms`, ' + BAKE_REPS + ' fresh workers) because walk_sim.js does not report it for STAND runs. ' +
    'The walking body is re-baked every frame, so its bake is a per-frame average.', '');
  row('Run', 'frames', 'collider bake ms', 'step ms per frame', 'wall s per run', 'load avg before → after (median run)');
  row('---', '---:', '---:', '---:', '---:', '---');
  const stand = rep.walk.stand, walk = rep.walk.walk;
  if (stand) {
    const mid = stand.runs.slice().sort((x, y) => x.wallS - y.wallS)[Math.floor(stand.runs.length / 2)];
    row('STAND=1 (standing body)', stand.frames, ms(stats(rep.mesh.bakeSamples)) + ' (' + rep.mesh.bakeSamples.length + ' builds)',
      stand.step.n ? ms(stand.step) : 'n/a', r1(stats(stand.runs.map((x) => x.wallS)).median), `${ld(mid.loadBefore)} → ${ld(mid.loadAfter)}`);
  } else row('STAND=1 (standing body)', '-', 'skipped', 'skipped', '-', '-');
  if (walk) {
    const mid = walk.runs.slice().sort((x, y) => x.wallS - y.wallS)[Math.floor(walk.runs.length / 2)];
    row('walking body', walk.frames, walk.bake.n ? ms(walk.bake) + ' (per frame)' : 'n/a', walk.step.n ? ms(walk.step) : 'n/a',
      r1(stats(walk.runs.map((x) => x.wallS)).median), `${ld(mid.loadBefore)} → ${ld(mid.loadAfter)}`);
  } else row('walking body', '-', 'skipped', 'skipped', '-', '-');
  L.push('');
  L.push('walk_sim.js prints these lines (the first run of each variant, verbatim):', '');
  L.push('```');
  if (stand) L.push('STAND=1: ' + (stand.runs[0].printed || stand.runs[0].error));
  if (walk) L.push('walking: ' + (walk.runs[0].printed || walk.runs[0].error));
  L.push('```', '');
  L.push('The printed step average is NaN and the STAND bake average is 0 for the reasons given in `tools/measure_numbers.js` section 2; the table above is recomputed from the per-frame arrays that walk_sim.js writes. ' +
    `Frames per run: ${walk ? walk.runs[0].stepFrames : '-'} walking, ${stand ? stand.runs[0].stepFrames : '-'} STAND (frame 0 has no step time).`, '');

  const b = rep.size;
  L.push('## 3. Download size', '',
    `\`node app/build.js\` writes \`failoring.html\` (the committed copy in the repo root is not touched; this run wrote to \`out/numbers/\`). Build exit ${b.buildExit}.`, '');
  row('Item', 'bytes', 'size', 'gzip');
  row('---', '---:', '---:', '---:');
  row('failoring.html (this build)', b.html, kB(b.html), kB(b.htmlGzip));
  row('embedded body asset `app/assets/body_mh.bin`', b.bodyAsset, kB(b.bodyAsset), '-');
  row('  as base64 in the html', b.bodyAssetBase64, kB(b.bodyAssetBase64), '-');
  row('wasm `wasm/failoring.wasm`', b.wasm, kB(b.wasm), '-');
  row('  as base64 in the html', b.wasmBase64, kB(b.wasmBase64), '-');
  L.push('');
  if (b.refD7e62e0 != null) {
    const d = b.html - b.refD7e62e0;
    L.push(`Reference: the html committed at \`d7e62e0\` (commit d7e62e0, the 1.58 MB figure in the request; it has no body asset blob) is ${b.refD7e62e0} bytes (${MB(b.refD7e62e0)}). ` +
      `This build is ${d >= 0 ? '+' : ''}${d} bytes (${(d / 1e6).toFixed(3)} MB, ${(100 * (b.html / b.refD7e62e0 - 1)).toFixed(1)} %) against it.`, '');
    row('Embedded base64 blob', 'reference d7e62e0 (chars)', 'this build (chars)', 'difference');
    row('---', '---:', '---:', '---:');
    row('`bodyB64` (body asset)', b.refBlobs.body, b.blobs.body, b.blobs.body - b.refBlobs.body);
    row('`wasmB64` (wasm)', b.refBlobs.wasm, b.blobs.wasm, b.blobs.wasm - b.refBlobs.wasm);
    const rest = d - (b.blobs.body - b.refBlobs.body) - (b.blobs.wasm - b.refBlobs.wasm);
    L.push('', `Everything outside those two blobs changes by ${rest >= 0 ? '+' : ''}${rest} bytes.`, '');
  }
  if (b.refHeadCommitted != null) {
    L.push(`The html currently committed at HEAD is ${b.refHeadCommitted} bytes (${MB(b.refHeadCommitted)}), so the committed copy is stale relative to the sources.`);
  }
  L.push('');
  L.push('## 4. Mesh sizes', '',
    `Body: the MakeHuman base mesh as used by the app (\`M.tris\`) and the posed body the worker collides with (\`Body.generate(...).pos\`, average preset). ` +
    `Topology is fixed, so the counts are the same for every preset. Garments: the worker\'s own mesh for one frame on a standing body, average preset, ` +
    '"sim" = cloth particles and triangles, "display" = the subdivided mesh the viewer draws (one midpoint per edge, four triangles per cloth triangle).', '');
  row('Mesh', 'vertices', 'triangles');
  row('---', '---:', '---:');
  row('body (rendered and collided)', rep.mesh.body.vertices, rep.mesh.body.triangles);
  L.push('');
  row('Garment (plan)', 'pieces', 'seams', 'sim vertices', 'sim triangles', 'display vertices', 'display triangles');
  row('---', '---:', '---:', '---:', '---:', '---:', '---:');
  for (const [name, g] of Object.entries(rep.mesh.garments)) row(name, g.pieces, g.seams, g.simVertices, g.simTriangles, g.displayVertices, g.displayTriangles);
  L.push('', 'Plans `pencil`, `aline` and `blank` are from `app/src/patterns.js`; `pants`, `shirt` and `tank` are drafted from the average body by `app/src/draft.js`.', '');

  L.push('## Reproduce', '', '```', 'node tools/measure_numbers.js' + (args.filter((a) => a !== '--render-only').length ? ' ' + args.filter((a) => a !== '--render-only').join(' ') : ''), '```', '',
    `Settings used: \`--walk-reps ${WALK_REPS}\`, \`--regen-reps ${REGEN_REPS}\`. The walk runs use \`tools/walk_sim.js pencil average 40\` (STAND=1) and \`tools/walk_sim.js pencil average 80\` (walking).`, '',
    'Load average at start: ' + ld(rep.loadStart) + '; at end: ' + ld(rep.loadEnd || load()) + '.', '');
  return L.join('\n');
}

// ------------------------------------------------------------------ main
(async () => {
  if (args.includes('--render-only')) {
    const saved = JSON.parse(fs.readFileSync(path.join(OUT, 'numbers.json'), 'utf8'));
    fs.writeFileSync(MD, md(saved));
    console.log('rewrote', MD, 'from', path.join(OUT, 'numbers.json'));
    return;
  }
  report.regen = regenSection();
  report.walk = { stand: null, walk: null };
  if (!SKIP_WALK) {
    report.walk.stand = walkSection(true, 40);
    report.walk.walk = walkSection(false, 80);
  }
  report.size = sizeSection();
  report.mesh = await meshSection();
  report.loadEnd = load();
  fs.writeFileSync(path.join(OUT, 'numbers.json'), JSON.stringify(report, null, 1));
  fs.writeFileSync(MD, md(report));
  console.log('wrote', MD, 'and', path.join(OUT, 'numbers.json'));
})().catch((e) => { console.error(e); process.exit(1); });
