// Application state, undo, pattern load/save. Globals shared by the other app files.
'use strict';
const App = {
  S: null, undo: [], redo: [], listeners: [],
  DEFAULT_MEAS: Object.assign({}, FL.PRESETS.average),
  QUALITY: {
    draft:    { label: 'Draft',    edge: 0.060, sub: 1, newton: 6,  frames: 36 },
    standard: { label: 'Standard', edge: 0.040, sub: 2, newton: 8,  frames: 48 },
    fine:     { label: 'Fine',     edge: 0.028, sub: 2, newton: 10, frames: 60 },
    photo:    { label: 'Photo',    edge: 0.025, sub: 3, newton: 12, frames: 60, self: true },   // slow: finer mesh, more substeps, self-collision
  },
};
const $ = (s, el) => (el || document).querySelector(s);
const $$ = (s, el) => Array.from((el || document).querySelectorAll(s));
const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
const smooth = (t) => { t = clamp(t, 0, 1); return t * t * (3 - 2 * t); };
const SEAM_COLORS = ['#3fd3c0', '#ff9d4d', '#c792ea', '#82aaff', '#f07178', '#c3e88d', '#ffcb6b', '#89ddff'];
const FABRIC_COLORS = { cotton_poplin: '#6f93c4', denim_12oz: '#3d5c8c', linen_medium: '#c2b184', wool_suiting: '#566175', silk_charmeuse: '#a8405c',
  polyester_chiffon: '#dc94ac', canvas_heavy: '#a89a76', cotton_jersey: '#6f9a88', lambskin_leather: '#7a4630' };

App.snapshot = () => JSON.stringify({ pieces: App.S.pieces, seams: App.S.seams });
App.pushUndo = () => { App.undo.push(App.snapshot()); if (App.undo.length > 60) App.undo.shift(); App.redo.length = 0; };
App.doUndo = () => { if (!App.undo.length) return; App.redo.push(App.snapshot()); const o = JSON.parse(App.undo.pop()); App.S.pieces = o.pieces; App.S.seams = o.seams; App.fixSelection(); App.changed('structure'); };
App.doRedo = () => { if (!App.redo.length) return; App.undo.push(App.snapshot()); const o = JSON.parse(App.redo.pop()); App.S.pieces = o.pieces; App.S.seams = o.seams; App.fixSelection(); App.changed('structure'); };
App.fixSelection = () => { const S = App.S; if (!S.pieces.find((p) => p.id === S.sel)) S.sel = null; S.selV = -1; S.sewA = null; };
// kind: 'structure' (resimulate + rebuild panels), 'table' (only table positions), 'ui' (redraw), 'body', 'fabric'
App.changed = (kind) => { for (const f of App.listeners) f(kind); };
App.onChange = (f) => App.listeners.push(f);
App.toast = (msg) => { const t = $('#toast'); t.textContent = msg; t.classList.add('show'); clearTimeout(App._tt); App._tt = setTimeout(() => t.classList.remove('show'), 2600); };

App.piece = (id) => App.S.pieces.find((p) => p.id === id);
App.newId = (base) => { let i = 1; while (App.S.pieces.some((p) => p.id === base + i)) i++; return base + i; };
App.pieceIndex = (id) => App.S.pieces.findIndex((p) => p.id === id);

// Load a pattern object. Outlines are made counter-clockwise; seam edge indices are remapped accordingly.
App.loadPattern = (src, keepView) => {
  const o = JSON.parse(JSON.stringify(src)), S = App.S;
  const seams = o.seams || [], rev = {};
  for (const p of o.pieces) {
    const n = p.outline.length;
    if (FL.signedArea(p.outline) < 0) {
      p.outline = p.outline.slice().reverse(); rev[p.id] = true;
      for (const s of seams) for (const side of ['a', 'b']) if (s[side][0] === p.id) { s[side][1] = ((n - 2 - s[side][1]) % n + n) % n; s.reverse = !s.reverse; }
    }
    FL.normalizePiece(p);
    p.name = p.name || p.id;
  }
  S.pieces = o.pieces; S.seams = seams.filter((s) => s.a && s.b);
  if (o.fabric) S.fabric = o.fabric;
  if (o.meas) S.meas = Object.assign({}, App.DEFAULT_MEAS, o.meas);
  S.planName = o.name || 'Untitled'; S.planNotes = o.notes || '';
  S.sel = S.pieces.length ? S.pieces[0].id : null; S.selV = -1; S.sewA = null; S.hotSeam = -1;
  App.undo = []; App.redo = [];
  App.changed('load');
};
App.exportPattern = () => {
  const S = App.S;
  return JSON.stringify({ format: 'failoring-pattern/1', name: S.planName, notes: S.planNotes, fabric: S.fabric, meas: S.meas,
    pieces: S.pieces, seams: S.seams }, (k, v) => (typeof v === 'number' ? Math.round(v * 1e5) / 1e5 : v), 1);
};

// seam geometry readout
App.seamInfo = (s) => {
  const A = App.piece(s.a[0]), B = App.piece(s.b[0]);
  if (!A || !B) return null;
  const la = FL.edgeLen(A.outline, s.a[1]), lb = FL.edgeLen(B.outline, s.b[1]);
  return { la, lb, ease: (lb - la) / la };
};

// geometry edits that must keep seam edge indices valid
App.insertVertex = (p, e, pt) => {
  p.outline.splice(e + 1, 0, pt);
  let dropped = 0;
  App.S.seams = App.S.seams.filter((s) => {
    for (const side of ['a', 'b']) if (s[side][0] === p.id) { if (s[side][1] === e) { dropped++; return false; } if (s[side][1] > e) s[side][1]++; }
    return true;
  });
  if (dropped) App.toast('Seam on the split edge was removed - sew the two new edges again');
};
App.removeVertex = (p, i) => {
  const n = p.outline.length; if (n <= 3) return false;
  // edges i-1 and i merge into edge (i-1)
  const prev = (i - 1 + n) % n; let dropped = 0;
  App.S.seams = App.S.seams.filter((s) => {
    for (const side of ['a', 'b']) if (s[side][0] === p.id) {
      const e = s[side][1];
      if (e === prev || e === i) { dropped++; return false; }
    }
    return true;
  });
  for (const s of App.S.seams) for (const side of ['a', 'b']) if (s[side][0] === p.id && s[side][1] > i) s[side][1]--;
  // when i == 0 the surviving edges shift by one more (edge n-1 becomes n-2 after removal; handled by > i decrement)
  p.outline.splice(i, 1);
  if (dropped) App.toast('Seams on the merged edges were removed');
  return true;
};
App.removePiece = (id) => { const S = App.S; S.pieces = S.pieces.filter((p) => p.id !== id); S.seams = S.seams.filter((s) => s.a[0] !== id && s.b[0] !== id); App.fixSelection(); };
App.addPiece = (kind) => {
  const S = App.S, id = App.newId('p');
  const outlines = { rect: [[0, 0], [0.4, 0], [0.4, 0.5], [0, 0.5]], trap: [[-0.2, 0], [0.2, 0], [0.1, 0.5], [-0.1, 0.5]], tri: [[0, 0], [0.4, 0], [0.2, 0.45]] };
  let maxx = -1e9; for (const p of S.pieces) { const b = FL.bbox(p.outline); maxx = Math.max(maxx, b.x1 + p.table.x); }
  const p = FL.normalizePiece({ id, name: 'Piece ' + (S.pieces.length + 1), outline: outlines[kind].map((q) => q.slice()), grain: 1.5708,
    table: { x: (S.pieces.length ? maxx + 0.12 : 0) + (kind === 'trap' ? 0.2 : 0), y: 0 }, wrap: { theta: 0, y: 0.5, clear: 0.012, ring: 0 } });
  S.pieces.push(p); S.sel = id; return p;
};
App.mirrorPiece = (p) => {
  const n = p.outline.length;
  const bb = FL.bbox(p.outline), cx = (bb.x0 + bb.x1) / 2;
  // mirror x, then restore CCW by reversing; edge e (i->i+1) maps to the new index n-2-e
  p.outline = p.outline.map(([x, y]) => [2 * cx - x, y]).reverse();
  for (const s of App.S.seams) for (const side of ['a', 'b']) if (s[side][0] === p.id) { s[side][1] = ((n - 2 - s[side][1]) % n + n) % n; s.reverse = !s.reverse; }
  p.grain = Math.PI - p.grain; p.wrap.theta = (360 - p.wrap.theta) % 360;
};
App.duplicatePiece = (p) => {
  const q = JSON.parse(JSON.stringify(p)); q.id = App.newId('p'); q.name = p.name + ' copy';
  q.table.x += FL.bbox(p.outline).x1 - FL.bbox(p.outline).x0 + 0.1; App.S.pieces.push(q); App.S.sel = q.id; return q;
};

// validity before sending to the simulator
App.validPieces = () => {
  const out = [], bad = {};
  for (const p of App.S.pieces) {
    const o = p.outline; let why = null;
    if (o.length < 3) why = 'needs 3+ corners';
    else if (FL.signedArea(o) < 6e-4) why = 'too small or inverted';
    else if (FL.selfIntersects(o)) why = 'outline crosses itself';
    else { for (let i = 0; i < o.length; i++) if (FL.edgeLen(o, i) < 0.008) why = 'edge shorter than 8 mm'; }
    if (why) bad[p.id] = why; else out.push(p);
  }
  return { pieces: out, bad };
};
