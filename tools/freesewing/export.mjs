// node --import ./reg.mjs export.mjs <design> <measures.json|-> <out.json> [only,parts]
// Drafts a FreeSewing design for the mannequin's measurements and writes the seam-line outline of every visible part.
import fs from 'node:fs';
import * as models from '@freesewing/models';
import { Teagan } from '@freesewing/teagan';
import { Penelope } from '@freesewing/penelope';
import { Charlie } from '@freesewing/charlie';
import { Aaron } from '@freesewing/aaron';
const DESIGNS = { teagan: Teagan, penelope: Penelope, charlie: Charlie, aaron: Aaron };
const [design, mfile, outFile, onlyArg] = process.argv.slice(2);
const ref = { ...models.cisFemaleAdult34 };
const mm = mfile && mfile !== '-' ? JSON.parse(fs.readFileSync(mfile, 'utf8')) : null;
let meas = { ...ref };
if (mm) {   // scale the reference set with the mannequin: girths by their own ratio, lengths by height, a few set directly
  const H = mm.height * 1000, rh = ref.waistToFloor + ref.hpsToWaistBack + 150, sl = (mm.yWaist + mm.yNeck - mm.yWaist + 0.0) * 1000 / rh;
  const gU = (mm.bust * 1000) / ref.chest, gW = (mm.waist * 1000) / ref.waist, gH = (mm.hip * 1000) / ref.hips;
  for (const k of Object.keys(ref)) {
    const gl = /^(chest|bust|underbust|highBust|biceps|neck|shoulderToShoulder|wrist|head)$/i.test(k) ? gU : /^(waist|waistBack)$/.test(k) ? gW : /^(hips|seat|seatBack|upperLeg|knee|ankle|heel)$/.test(k) ? gH : null;
    meas[k] = ref[k] * (k === 'waist' ? gW : k === 'hips' ? gH : (k === 'chest' || k === 'bust') ? gU : gl && k !== 'shoulderToShoulder' ? Math.sqrt(gl) : sl);
  }
  Object.assign(meas, { seatBack: ref.seatBack * gH, waistBack: ref.waistBack * gW, upperLeg: mm.thigh * 1000, knee: mm.knee * 1000, ankle: mm.ankle * 1000 * 1.0,
    crossSeam: ref.crossSeam * Math.pow(gH, 0.85) * sl / sl, crossSeamFront: ref.crossSeamFront * Math.pow(gH, 0.85) });
  Object.assign(meas, { height: H, chest: mm.bust * 1000, bust: mm.bust * 1000, waist: mm.waist * 1000, hips: mm.hip * 1000, seat: mm.hip * 1000 * ref.seat / ref.hips,
    waistToFloor: mm.yWaist * 1000, waistToHips: (mm.yWaist - mm.yHip) * 1000, waistToKnee: (mm.yWaist - mm.yKnee) * 1000, inseam: mm.yCrotch * 1000,
    waistToUpperLeg: (mm.yWaist - mm.yCrotch) * 1000, shoulderToShoulder: mm.shoulderToShoulder * 1000,
    hpsToWaistBack: (mm.yNeck + 0.02 - mm.yWaist) * 1000 * 1.03, shoulderToElbow: mm.upperArmLen * 1000 * 1.03, shoulderToWrist: (mm.upperArmLen + mm.foreArmLen) * 1000 * 1.03 });
}
const settings = {};
if (onlyArg) settings.only = onlyArg.split(',');
const D = DESIGNS[design];
const pat = new D({ measurements: meas, ...settings }); pat.draft();
const S = pat.setStores[0];
const sample = (op, from, n) => {   // polyline of one path op (mm)
  if (op.type === 'line') return [[op.to.x, op.to.y]];
  if (op.type === 'curve') { const pts = []; const p0 = from, c1 = op.cp1, c2 = op.cp2, p1 = op.to;
    for (let i = 1; i <= n; i++) { const t = i / n, u = 1 - t;
      pts.push([u*u*u*p0.x + 3*u*u*t*c1.x + 3*u*t*t*c2.x + t*t*t*p1.x, u*u*u*p0.y + 3*u*u*t*c1.y + 3*u*t*t*c2.y + t*t*t*p1.y]); }
    return pts; }
  return [];
};
const errs = (S.logs && S.logs.error) || []; if (errs.length) console.log('ERRORS', JSON.stringify(errs).slice(0, 500));
const out = { design, measurements: meas, pieces: [] };
const polyOf = (path) => { const pts = []; let cur = null;
  for (const op of path.ops) { if (op.type === 'move') { cur = { x: op.to.x, y: op.to.y }; pts.push([cur.x, cur.y]); } else if (op.type === 'line') { cur = { x: op.to.x, y: op.to.y }; pts.push([cur.x, cur.y]); } else if (op.type === 'curve') { for (const q of sample(op, cur, 40)) pts.push(q); cur = { x: op.to.x, y: op.to.y }; } }
  return pts; };
const distTo = (poly, p) => { let best = 1e9; for (let i = 0; i + 1 < poly.length; i++) { const [ax, ay] = poly[i], [bx, by] = poly[i + 1], dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy || 1e-9; const t = Math.max(0, Math.min(1, ((p[0] - ax) * dx + (p[1] - ay) * dy) / l2)); best = Math.min(best, Math.hypot(p[0] - ax - t * dx, p[1] - ay - t * dy)); } return best; };
const cl = S.cutlist || {};
for (const [name, part] of Object.entries(pat.parts[0])) {
  const sp = part.paths.seam || part.paths.outline; if (!sp || part.hidden) continue;
  const ops = sp.ops; let chains = [], cur = null, start = null;
  for (const op of ops) {
    if (op.type === 'move') { cur = { x: op.to.x, y: op.to.y }; start = cur; continue; }
    if (op.type === 'close') { if (Math.hypot(cur.x - start.x, cur.y - start.y) > 1e-6) chains.push({ type: 'line', pts: [[cur.x, cur.y], [start.x, start.y]] }); continue; }
    const from = cur, pts = [[from.x, from.y]].concat(sample(op, from, 24));
    chains.push({ type: op.type, pts }); cur = { x: op.to.x, y: op.to.y };
  }
  const c = cl[name] || {}, mat = (c.materials && c.materials.fabric && c.materials.fabric[0]) || {};
  // label chains with the designer's named helper paths (sideSeam, hem, ...)
  const named = Object.entries(part.paths).filter(([k]) => !/^(seam|outline|sa|saBase|waistSA|__.*)$/.test(k)).map(([k, p]) => [k, polyOf(p)]);
  for (const ch of chains) { const mid = ch.pts[Math.floor(ch.pts.length / 2)]; const mp = ch.pts.length > 2 ? mid : [(ch.pts[0][0] + ch.pts[1][0]) / 2, (ch.pts[0][1] + ch.pts[1][1]) / 2];
    let best = null, bd = 0.6; for (const [k, poly] of named) { const d = distTo(poly, mp); if (d < bd) { bd = d; best = k; } } if (best) ch.label = best; }
  // unfold a cut-on-fold half: drop the fold chain, mirror the rest across the fold line
  let fold = null;
  if (c.cutOnFold && c.cutOnFold.length === 2) {
    const [a, b] = c.cutOnFold, dx = b.x - a.x, dy = b.y - a.y, L = Math.hypot(dx, dy);
    const onLine = (p) => Math.abs((p[0] - a.x) * dy - (p[1] - a.y) * dx) / L < 0.5;
    const fi = chains.findIndex((ch) => ch.pts.every(onLine) && ch.pts.length >= 2 && Math.hypot(ch.pts[0][0] - ch.pts[ch.pts.length - 1][0], ch.pts[0][1] - ch.pts[ch.pts.length - 1][1]) > 5);
    if (fi >= 0) {
      fold = { a: [a.x, a.y], b: [b.x, b.y] };
      const rest = chains.slice(fi + 1).concat(chains.slice(0, fi));
      const refl = (p) => { const t = ((p[0] - a.x) * dx + (p[1] - a.y) * dy) / (L * L), fx = a.x + t * dx, fy = a.y + t * dy; return [2 * fx - p[0], 2 * fy - p[1]]; };
      const mir = rest.slice().reverse().map((ch) => ({ type: ch.type, label: ch.label ? ch.label + '.mirror' : undefined, pts: ch.pts.slice().reverse().map(refl), mirrored: true }));
      chains = rest.concat(mir);
    }
  }
  { const m = []; for (const ch of chains) { const last = m[m.length - 1]; if (last && ch.label && last.label === ch.label) { last.pts = last.pts.concat(ch.pts.slice(1)); last.type = 'curve'; } else m.push({ type: ch.type, label: ch.label, pts: ch.pts.slice() }); } chains = m; }
  out.pieces.push({ id: name, chains, fold, cut: mat.cut || 1, onFold: !!fold, identical: !!mat.identical, grain: c.grain, grainOrigin: c.grainOrigin, material: Object.keys(c.materials || { fabric: 1 })[0] });
}
fs.writeFileSync(outFile, JSON.stringify(out));
console.log(design, out.pieces.map((p) => p.id + '(' + p.chains.length + (p.fold ? ',unfolded' : '') + ',cut ' + p.cut + ')').join(' '));
