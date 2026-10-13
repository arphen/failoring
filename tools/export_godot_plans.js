// Exports Godot plan data for every preset and FreeSewing garment:
//   node tools/export_godot_plans.js [--out godot/assets] [preset ...]
// Per preset, into <out>/<preset>/:
//   plan_teagan.json, plan_penelope.json, plan_charlie.json
//     what FL.fsimport.plan produces (pieces, seams, info), exactly like tools/fs_plan.js:
//     the raw draft app/assets/freesewing/raw_<garment>_<preset>.json planned on the standing
//     body (FL.Gait.stand). Re-run after any app/src/fsimport.js change (e.g. the holdTop fix).
//   sections.json   {preset, sections, arms}: body sections + arm tubes for the wrap port
//     (godot/wrap.gd); hulls/legs as plain arrays, null where a row has no section.
//   wrap_ref.json   spot values [{piece, u, v, p}] from the real FL.makeWrap, one 3x3 grid per
//     piece; rows narrower than 1e-9 m are skipped (the wrap angle is trig garbage there in any
//     implementation, so they cannot verify a port). Read by godot/tests/wrap_check.tscn.
// Bodies (body.obj, walk.bin) come from tools/export_godot_assets.js; this tool only adds what
// the editor needs on top. Ends by reading everything back and checking it.
'use strict';
const fs = require('fs'), path = require('path');
const ROOT = path.join(__dirname, '..'), R = (...p) => path.join(ROOT, ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js'));
require(R('app/src/gait.js')); require(R('app/src/fsimport.js')); require(R('app/src/draft.js'));

const GARMENTS = ['teagan', 'penelope', 'charlie'];   // tee, pencil skirt, chinos
const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : def; };
const OUT = path.resolve(opt('--out', R('godot/assets')));
const positional = args.filter((a, i) => !a.startsWith('--') && args[i - 1] !== '--out');
const presets = positional.length ? positional : Object.keys(FL.PRESETS);

const J = (a) => Array.from(a || []);
const rowW = (o, v) => {
  let lo = 1e9, hi = -1e9;
  for (let i = 0; i < o.length; i++) {
    const a = o[i], b = o[(i + 1) % o.length];
    if ((a[1] - v) * (b[1] - v) <= 0 && a[1] !== b[1]) { const x = a[0] + (b[0] - a[0]) * (v - a[1]) / (b[1] - a[1]); lo = Math.min(lo, x); hi = Math.max(hi, x); }
  }
  return hi - lo;
};

function exportPreset(name) {
  const dir = path.join(OUT, name); fs.mkdirSync(dir, { recursive: true });
  const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
  const rest = Body.generate(M, FL.PRESETS[name], null);
  const stand = FL.Gait.stand(M, rest);
  const S = stand.sections;
  const sections = {
    dy: S.dy, ny: S.ny, yCrotch: S.yCrotch,
    hulls: S.hulls.map((h) => J(h)), girth: J(S.girth),
    legs: { L: S.legs.L.map((h) => (h ? J(h) : null)), R: S.legs.R.map((h) => (h ? J(h) : null)) },
  };
  const arms = {};
  for (const side of ['L', 'R']) { const A = stand.arms[side]; arms[side] = { pts: A.pts, seg: A.seg, len: A.len, total: A.total, rad: J(A.rad) }; }
  fs.writeFileSync(path.join(dir, 'sections.json'), JSON.stringify({ preset: name, sections, arms }));
  const refs = [];
  for (const g of GARMENTS) {
    // the plan step, exactly like tools/fs_plan.js: raw draft planned on the standing body
    const raw = JSON.parse(fs.readFileSync(R('app/assets/freesewing', `raw_${g}_${name}.json`), 'utf8'));
    const plan = FL.fsimport.plan(raw, stand, {});
    for (const pc of plan.pieces) if (!pc.wrap || !pc.wrap.mode) throw new Error(`plan ${g}/${name}: piece ${pc.id} has no wrap.mode`);
    fs.writeFileSync(path.join(dir, `plan_${g}.json`), JSON.stringify(plan));
    for (const pc of plan.pieces) {
      const w = FL.makeWrap(pc, stand), bb = FL.bbox(pc.outline);
      for (const u of [bb.x0, (bb.x0 + bb.x1) / 2, bb.x1]) for (const v of [bb.y0, (bb.y0 + bb.y1) / 2, bb.y1]) {
        if (rowW(pc.outline, v) < 1e-9) continue;
        refs.push({ piece: pc.id, garment: g, u, v, p: w.point(u, v) });
      }
    }
  }
  fs.writeFileSync(path.join(dir, 'wrap_ref.json'), JSON.stringify({ preset: name, refs }));
  // draft reference outlines for godot/tests/draft_check.tscn (pants/tank/shirt on this body)
  const drafts = {};
  for (const [dn, df] of [['pants', (b) => FL.draft.pants(b, {})], ['tank', (b) => FL.draft.tank(b, {})], ['shirt', (b) => FL.draft.shirt(b, {})]]) {
    const p = df(stand);
    drafts[dn] = { pieces: p.pieces.map((pc) => ({ id: pc.id, outline: pc.outline })), seams: p.seams.length };
  }
  fs.writeFileSync(path.join(dir, 'draft_ref.json'), JSON.stringify({ preset: name, drafts }));
  return { stand };
}

// ---------------------------------------------------------------- verification: read back from disk
function verify(name) {
  const dir = path.join(OUT, name), fails = [], rep = {};
  const check = (ok, msg) => { if (!ok) fails.push(msg); };
  const sec = JSON.parse(fs.readFileSync(path.join(dir, 'sections.json'), 'utf8'));
  check(sec.preset === name, 'sections preset tag');
  check(sec.sections.ny > 100 && sec.sections.hulls.length === sec.sections.ny, 'sections hull rows');
  check(sec.arms.L && sec.arms.R && sec.arms.L.total > 0, 'sections arms');
  rep.sectionsKB = Math.round(fs.statSync(path.join(dir, 'sections.json')).size / 1024);
  rep.plans = {};
  for (const g of GARMENTS) {
    const p = JSON.parse(fs.readFileSync(path.join(dir, `plan_${g}.json`), 'utf8'));
    check(Array.isArray(p.pieces) && p.pieces.length > 0, `plan ${g} has no pieces`);
    check(Array.isArray(p.seams) && p.seams.length > 0, `plan ${g} has no seams`);
    check(p.pieces.every((pc) => pc.wrap && pc.wrap.mode), `plan ${g} piece without wrap.mode`);
    check(p.info && Array.isArray(p.info.pieces) && Array.isArray(p.info.seams), `plan ${g} has no info`);
    rep.plans[g] = { pieces: p.pieces.length, seams: p.seams.length };
  }
  const refs = JSON.parse(fs.readFileSync(path.join(dir, 'wrap_ref.json'), 'utf8'));
  check(refs.refs.length > 20, `only ${refs.refs.length} wrap refs`);
  const dref = JSON.parse(fs.readFileSync(path.join(dir, 'draft_ref.json'), 'utf8'));
  check(Object.keys(dref.drafts).length === 3, 'draft refs');
  // every ref recomputes from the written files (catches serialization loss)
  const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
  const stand = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[name], null));
  let worst = 0;
  for (const g of GARMENTS) {
    const p = JSON.parse(fs.readFileSync(path.join(dir, `plan_${g}.json`), 'utf8'));
    const byId = {};
    for (const pc of p.pieces) byId[pc.id] = pc;
    for (const r of refs.refs.filter((x) => x.garment === g)) {
      const w = FL.makeWrap(byId[r.piece], stand);
      const q = w.point(r.u, r.v);
      worst = Math.max(worst, Math.hypot(q[0] - r.p[0], q[1] - r.p[1], q[2] - r.p[2]));
    }
  }
  check(worst < 1e-12, `ref recompute drift ${worst}`);
  rep.refs = refs.refs.length;
  return { fails, rep };
}

let failed = false;
for (const name of presets) {
  if (!FL.PRESETS[name]) throw new Error('unknown preset ' + name);
  const t0 = Date.now();
  exportPreset(name);
  const { fails, rep } = verify(name);
  console.log(name, `(${((Date.now() - t0) / 1000).toFixed(1)} s)`, JSON.stringify(rep));
  if (fails.length) { failed = true; console.error(' FAIL', fails.join('; ')); }
}
process.exit(failed ? 1 : 0);
