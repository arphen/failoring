// Exports the mannequin the browser app uses (app/assets/body_mh.bin, presets in FL.PRESETS) for the native Godot app.
//   node tools/export_godot_assets.js [--frames 24] [--out godot/assets] [preset ...]
// Per preset, into <out>/<preset>/:
//   body.obj            standing mesh (FL.Gait.stand, the collider's mesh): nb vertices, triangles, metres
//   skeleton.json       bones (name, parent, rest and standing joint positions), all joints, top-4 skin weights per vertex
//   walk.bin            one gait cycle baked per frame (FL.Gait.pose, stride 1): bind pose f32 + frames as f16 offsets
//   plan_<garment>.json FreeSewing plan per garment (FL.fsimport.plan on app/assets/freesewing/raw_<garment>_<preset>.json)
// Formats: docs/GODOT_ASSETS.md. The run ends by reading every file back and checking it (see verify()).
'use strict';
const fs = require('fs'), path = require('path');
const ROOT = path.join(__dirname, '..'), R = (...p) => path.join(ROOT, ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js')); require(R('app/src/fsimport.js'));

const GARMENTS = ['teagan', 'penelope', 'charlie'];   // tee, pencil skirt, chinos (app/src/fsimport.js RECIPES)
const CYCLE = 64 / 60;                                // seconds per gait cycle in the app (main.js WALK.cycle = 64 frames at 60 fps)
const WALK_MAGIC = 'FLWALK01';
const SKEL_FOOT = ['foot.L', 'foot.R'];

// ---------------------------------------------------------------- CLI
const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : def; };
const FRAMES = +opt('--frames', 24);
const OUT = path.resolve(opt('--out', R('godot/assets')));
const positional = args.filter((a, i) => !a.startsWith('--') && !['--frames', '--out'].includes(args[i - 1]));
const presets = positional.length ? positional : Object.keys(FL.PRESETS);

// ---------------------------------------------------------------- float16 (IEEE 754 binary16, round to nearest)
const F32 = new Float32Array(1), U32 = new Uint32Array(F32.buffer);
function toHalf(x) {
  F32[0] = x; const f = U32[0], bexp = (f >>> 23) & 0xff;
  const sign = (f >>> 16) & 0x8000, m = f & 0x7fffff;
  if (bexp === 0xff) return sign | 0x7c00 | (m ? 0x200 : 0);
  const e = bexp - 127 + 15;
  if (e >= 31) return sign | 0x7c00;
  if (e <= 0) {
    if (e < -10) return sign;
    const sh = 14 - e;
    return sign | ((((m | 0x800000) + (1 << (sh - 1))) >>> sh) & 0xffff);
  }
  let h = sign | (e << 10) | (m >>> 13);
  if (m & 0x1000) h += 1;
  return h;
}
function fromHalf(h) {
  const s = h & 0x8000 ? -1 : 1, e = (h >> 10) & 31, m = h & 1023;
  if (e === 0) return s * m * 2 ** -24;
  if (e === 31) return m ? NaN : s * Infinity;
  return s * (1 + m / 1024) * 2 ** (e - 15);
}

// ---------------------------------------------------------------- writers
const r4 = (v) => v.toFixed(4);
const v3 = (a) => [+a[0].toFixed(4), +a[1].toFixed(4), +a[2].toFixed(4)];

function writeObj(file, P, tris, nb) {
  const lines = ['# Failoring mannequin, standing pose (FL.Gait.stand). Metres, y up, +z front, +x the body\'s left.',
    `# vertices ${nb}  triangles ${tris.length / 3}  (1-based indices)`, 'o body'];
  for (let i = 0; i < nb; i++) lines.push(`v ${r4(P[3 * i])} ${r4(P[3 * i + 1])} ${r4(P[3 * i + 2])}`);
  for (let t = 0; t < tris.length; t += 3) lines.push(`f ${tris[t] + 1} ${tris[t + 1] + 1} ${tris[t + 2] + 1}`);
  fs.writeFileSync(file, lines.join('\n') + '\n');
}

function skeletonJson(M, rest, stand) {
  const bones = M.bones.map((b, k) => ({ index: k, name: b.joint, parent: b.parent, rest: v3(rest.joints[b.joint]), stand: v3(stand.joints[b.joint]) }));
  const joints = {}; for (const n of Object.keys(rest.joints)) joints[n] = v3(rest.joints[n]);
  const bone_index = new Array(4 * M.nb), weight = new Array(4 * M.nb);
  for (let i = 0; i < 4 * M.nb; i++) { bone_index[i] = M.skinIdx[i]; weight[i] = +M.skinW[i].toFixed(3); }
  return {
    format: 'failoring-skeleton', version: 1, units: 'm', axes: 'y up, +z front, +x body left',
    bind: 'rest: the generated A-pose body (skinning is applied to the bind vertices in walk.bin, section bind)',
    bones, joints,
    skin: { vertices: M.nb, influences: 4, bone_names: bones.map((b) => b.name), bone_index, weight,
      note: 'vertex i takes bone_index[4i..4i+3] (indices into bones[]) with weight[4i..4i+3]; weights are rounded to 3 decimals, renormalise on load' },
  };
}

// walk.bin: 'FLWALK01' | u32 LE header length | header JSON (utf8) | data
//   data: bind = nb*3 f32 absolute positions of the A-pose (skinning rest, skeleton.json); then `frames` blocks of nb*3
//   f16 offsets from the standing mesh (the 4-decimal values in body.obj), frame k at phase k/frames. The largest offset
//   is ~0.49 m (hands, feet), so float16 error is up to ~0.24 mm; the reference is body.obj, so the loader needs no bind.
function walkBin(file, M, rest, stand, frames) {
  const nb = M.nb, n3 = 3 * nb, bind = rest.pos;
  const ref = Float64Array.from(stand.pos.subarray(0, n3), (v) => +v.toFixed(4));
  const fdata = new Uint16Array(frames * n3);
  for (let k = 0; k < frames; k++) {
    const P = FL.Gait.pose(M, rest, k / frames, { stride: 1, arms: 1, armAbduct: stand.armAbduct }).pos;
    for (let i = 0; i < n3; i++) fdata[k * n3 + i] = toHalf(P[i] - ref[i]);
  }
  const header = {
    format: 'failoring-walk', version: 1, units: 'm', axes: 'y up, +z front, +x body left (same as the browser app)',
    vertices: nb, frames, cycle_seconds: +CYCLE.toFixed(6), fps: +(frames / CYCLE).toFixed(3),
    frame_phase: 'frame k is the pose at phase k/frames of the left-leg gait cycle (0 = left heel strike); loop it',
    gait: 'FL.Gait.pose(M, rest, phase, {stride: 1, arms: 1, armAbduct: stand.armAbduct}) with the default walk (app/src/gait.js; armAbduct is the arm angle FL.Gait.stand chose for this body)',
    sections: {
      bind: { dtype: 'f32', offset: 0, count: n3, layout: 'x0 y0 z0 x1 y1 z1 ... (vertex order = body.obj order)' },
      frames: { dtype: 'f16', offset: 4 * n3, count: frames * n3, frame_bytes: 2 * n3, relative_to: 'body.obj vertex positions',
        layout: 'frame k starts at data offset + k*frame_bytes (x0 y0 z0 x1 ...); position = body.obj vertex + offset (metres)' },
    },
  };
  const hjson = Buffer.from(JSON.stringify(header), 'utf8');
  const bytes = 12 + hjson.length + 4 * n3 + 2 * frames * n3;
  const buf = Buffer.alloc(bytes);
  buf.write(WALK_MAGIC, 0, 'ascii'); buf.writeUInt32LE(hjson.length, 8); hjson.copy(buf, 12);
  let o = 12 + hjson.length;
  for (let i = 0; i < n3; i++, o += 4) buf.writeFloatLE(bind[i], o);
  for (let i = 0; i < fdata.length; i++, o += 2) buf.writeUInt16LE(fdata[i], o);
  fs.writeFileSync(file, buf);
  return header;
}

// ---------------------------------------------------------------- export
function exportPreset(name) {
  const dir = path.join(OUT, name); fs.mkdirSync(dir, { recursive: true });
  const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
  const rest = Body.generate(M, FL.PRESETS[name], null);
  const stand = FL.Gait.stand(M, rest);
  writeObj(path.join(dir, 'body.obj'), stand.pos, M.tris, M.nb);
  fs.writeFileSync(path.join(dir, 'skeleton.json'), JSON.stringify(skeletonJson(M, rest, stand)));
  walkBin(path.join(dir, 'walk.bin'), M, rest, stand, FRAMES);
  for (const g of GARMENTS) {
    const raw = JSON.parse(fs.readFileSync(R('app/assets/freesewing', `raw_${g}_${name}.json`), 'utf8'));
    const plan = FL.fsimport.plan(raw, stand, {});
    fs.writeFileSync(path.join(dir, `plan_${g}.json`), JSON.stringify(plan));
  }
  return { M, rest, stand };
}

// ---------------------------------------------------------------- verification: read the files back from disk
function verify(name, M, rest, stand) {
  const dir = path.join(OUT, name), fails = [], rep = {};
  const check = (ok, msg) => { if (!ok) fails.push(msg); };
  const nb = M.nb;
  // OBJ
  const lines = fs.readFileSync(path.join(dir, 'body.obj'), 'utf8').split('\n');
  const V = [], T = [];
  for (const l of lines) {
    if (l.startsWith('v ')) { const p = l.split(' ').slice(1).map(Number); V.push(p); }
    else if (l.startsWith('f ')) T.push(l.split(' ').slice(1).map((s) => +s - 1));
  }
  check(V.length === nb, `obj vertices ${V.length} != ${nb}`);
  check(T.length === M.tris.length / 3, `obj faces ${T.length} != ${M.tris.length / 3}`);
  check(T.every((t) => t.every((i) => i >= 0 && i < nb)), 'obj face index out of range');
  let err = 0; const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < nb; i++) for (let c = 0; c < 3; c++) {
    err = Math.max(err, Math.abs(V[i][c] - stand.pos[3 * i + c]));
    lo[c] = Math.min(lo[c], V[i][c]); hi[c] = Math.max(hi[c], V[i][c]);
  }
  check(err <= 5e-5 + 1e-9, `obj round-trip error ${err}`);
  rep.obj = { vertices: V.length, faces: T.length, roundTripMaxErrM: +err.toExponential(2), boundsMin: lo.map((v) => +v.toFixed(3)), boundsMax: hi.map((v) => +v.toFixed(3)) };
  check(lo[1] > -1e-3 && lo[1] < 1e-3, `standing feet not on y=0 (${lo[1]})`);
  // skeleton
  const S = JSON.parse(fs.readFileSync(path.join(dir, 'skeleton.json'), 'utf8'));
  check(S.bones.length === M.bones.length, 'bone count');
  check(S.bones.every((b, k) => b.parent === M.bones[k].parent && b.name === M.bones[k].joint), 'bone names/parents differ from asset');
  check(S.bones.every((b) => b.parent < 0 || b.parent < b.index), 'bone parents not topologically ordered');
  check(S.skin.bone_index.length === 4 * nb && S.skin.weight.length === 4 * nb, 'skin array length');
  check(S.skin.bone_index.every((b) => b >= 0 && b < S.bones.length), 'skin bone index out of range');
  let wmax = 0; for (let i = 0; i < nb; i++) { const s = S.skin.weight[4 * i] + S.skin.weight[4 * i + 1] + S.skin.weight[4 * i + 2] + S.skin.weight[4 * i + 3]; wmax = Math.max(wmax, Math.abs(s - 1)); }
  check(wmax < 0.003, `skin weight sum off by ${wmax}`);
  rep.skeleton = { bones: S.bones.length, skinVertices: nb, maxWeightSumErr: +wmax.toFixed(4) };
  // walk
  const B = fs.readFileSync(path.join(dir, 'walk.bin'));
  check(B.toString('ascii', 0, 8) === WALK_MAGIC, 'walk magic');
  const hl = B.readUInt32LE(8), H = JSON.parse(B.toString('utf8', 12, 12 + hl)), d0 = 12 + hl, n3 = 3 * H.vertices;
  check(H.vertices === nb, 'walk vertex count');
  check(B.length === d0 + 4 * n3 + 2 * H.frames * n3, `walk size ${B.length}`);
  const bind = new Float32Array(n3); for (let i = 0; i < n3; i++) bind[i] = B.readFloatLE(d0 + 4 * i);
  let bindErr = 0; for (let i = 0; i < n3; i++) bindErr = Math.max(bindErr, Math.abs(bind[i] - rest.pos[i]));
  check(bindErr === 0, `bind section differs from rest (${bindErr})`);
  const footIdx = SKEL_FOOT.map((p) => M.parts.indexOf(p));
  let worst = 0, footWorst = 0, boundsWalk = [Infinity, Infinity, Infinity, -Infinity, -Infinity, -Infinity];
  const fo = d0 + 4 * n3;
  for (let k = 0; k < H.frames; k++) {
    const P = FL.Gait.pose(M, rest, k / H.frames, { stride: 1, arms: 1, armAbduct: stand.armAbduct }).pos;
    let fmin = Infinity;
    for (let i = 0; i < nb; i++) {
      for (let c = 0; c < 3; c++) {
        const x = V[i][c] + fromHalf(B.readUInt16LE(fo + 2 * (k * n3 + 3 * i + c)));
        worst = Math.max(worst, Math.abs(x - P[3 * i + c]));
        if (c === 1 && footIdx.includes(M.part[i])) fmin = Math.min(fmin, x);
        boundsWalk[c] = Math.min(boundsWalk[c], x); boundsWalk[c + 3] = Math.max(boundsWalk[c + 3], x);
      }
    }
    footWorst = Math.max(footWorst, Math.abs(fmin));
  }
  // float16 has an ulp of 0.5 mm for offsets in [0.5, 1) m; the largest offsets (hands, feet) give up to ~0.25 mm error
  check(worst < 3e-4, `walk frame error ${worst}`);
  check(footWorst < 1e-3, `walk feet off the floor by ${footWorst}`);
  rep.walk = { bytes: B.length, frames: H.frames, fps: H.fps, maxFrameErrM: +worst.toExponential(2), maxFootHeightM: +footWorst.toExponential(2) };
  // plans
  rep.plans = {};
  for (const g of GARMENTS) {
    const p = JSON.parse(fs.readFileSync(path.join(dir, `plan_${g}.json`), 'utf8'));
    check(Array.isArray(p.pieces) && p.pieces.length > 0, `plan ${g} has no pieces`);
    rep.plans[g] = { pieces: p.pieces.length, seams: p.seams.length };
  }
  const sizes = {}; let total = 0;
  for (const f of fs.readdirSync(dir)) { const s = fs.statSync(path.join(dir, f)).size; sizes[f] = s; total += s; }
  rep.sizesKB = Object.fromEntries(Object.entries(sizes).map(([k, v]) => [k, Math.round(v / 1024)])); rep.totalMB = +(total / 1048576).toFixed(2);
  return { fails, rep };
}

// ---------------------------------------------------------------- main
let failed = false;
for (const name of presets) {
  if (!FL.PRESETS[name]) throw new Error('unknown preset ' + name);
  const t0 = Date.now();
  const { M, rest, stand } = exportPreset(name);
  const { fails, rep } = verify(name, M, rest, stand);
  console.log(name, `(${((Date.now() - t0) / 1000).toFixed(1)} s)`, JSON.stringify(rep));
  if (fails.length) { failed = true; console.error(' FAIL', fails.join('; ')); }
}
process.exit(failed ? 1 : 0);
