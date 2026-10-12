// Import of open-source sewing patterns drafted by FreeSewing (https://freesewing.org, MIT licence) onto the mannequin.
//   FL.fsimport.plan(raw, body) -> plan { name, fabric, notes, pieces, seams, info }
// `raw` is the output of tools/fs_export (the real pattern parts for the mannequin's measurements: seam-line outlines in mm,
// cut-on-fold halves already unfolded, cut counts, grain). This module
//   * orients each piece (metres, y up, counter-clockwise), makes the second copy of cut-2 halves,
//   * decides how each piece is laid round the body (recipes below: body 'fit', sleeve 'arm', waistband 'torso'),
//   * finds the seams itself: after the starting wrap, edges of the pieces that lie next to each other are sewn,
//     so no hand-written seam list is needed (and darts close, sleeves join the armholes, the band meets the skirt),
//   * returns the plan in the app's own format plus the information a person needs at the sewing table.
(function (FL) {
  'use strict';
  const I = {};
  FL.fsimport = I;

  // How each FreeSewing part is worn. kind: 'body' (fitted round the trunk by FL.makeWrapRows mode 'fit'), 'sleeve', 'band'.
  // top: world height the piece's highest point is hung from ('hps' high point shoulder, 'waist').
  I.RECIPES = {
    teagan: { title: 'Teagan t-shirt', fabric: 'cotton_jersey', url: 'https://freesewing.org/docs/designs/teagan',
      parts: { 'teagan.front': { kind: 'body', theta: 0, top: 'hps', role: 'Front' }, 'teagan.back': { kind: 'body', theta: 180, top: 'hps', role: 'Back' }, 'teagan.sleeve': { kind: 'sleeve', role: 'Sleeve' } } },
    aaron: { title: 'Aaron a-shirt', fabric: 'cotton_jersey', url: 'https://freesewing.org/docs/designs/aaron',
      parts: { 'aaron.front': { kind: 'body', theta: 0, top: 'hps', role: 'Front' }, 'aaron.back': { kind: 'body', theta: 180, top: 'hps', role: 'Back' } } },
    penelope: { title: 'Penelope pencil skirt', fabric: 'wool_suiting', url: 'https://freesewing.org/docs/designs/penelope',
      parts: { 'penelope.front': { kind: 'body', theta: 0, top: 'waist', role: 'Front' }, 'penelope.back': { kind: 'body', theta: 180, top: 'waist', role: 'Back half' }, 'penelope.waistband': { kind: 'band', role: 'Waistband' } } },
  };

  const area = (p) => { let a = 0; for (let i = 0; i < p.length; i++) { const j = (i + 1) % p.length; a += p[i][0] * p[j][1] - p[j][0] * p[i][1]; } return a / 2; };
  const dist2 = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1]);
  const dist3 = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
  const chainLen = (pts) => { let l = 0; for (let i = 1; i < pts.length; i++) l += dist2(pts[i - 1], pts[i]); return l; };
  // point at fraction f of the arc length of polyline pts, with cumulative table
  const mkCurve = (pts) => {
    const cum = [0]; for (let i = 1; i < pts.length; i++) cum.push(cum[i - 1] + dist2(pts[i - 1], pts[i]));
    const L = cum[cum.length - 1];
    const at = (f) => { const s = Math.max(0, Math.min(1, f)) * L; let lo = 0, hi = pts.length - 1; while (hi - lo > 1) { const m = (lo + hi) >> 1; if (cum[m] <= s) lo = m; else hi = m; }
      const t = cum[hi] > cum[lo] ? (s - cum[lo]) / (cum[hi] - cum[lo]) : 0; return [pts[lo][0] + (pts[hi][0] - pts[lo][0]) * t, pts[lo][1] + (pts[hi][1] - pts[lo][1]) * t]; };
    return { L, at };
  };

  // split a chain at corners (turning angle > 28 degrees between neighbouring samples): dart legs, neck/shoulder joints
  const splitCorners = (ch) => {
    const p = ch.pts, cuts = [0];
    for (let i = 1; i + 1 < p.length; i++) {
      const a = Math.atan2(p[i][1] - p[i - 1][1], p[i][0] - p[i - 1][0]), b = Math.atan2(p[i + 1][1] - p[i][1], p[i + 1][0] - p[i][0]);
      let d = Math.abs(b - a); if (d > Math.PI) d = 2 * Math.PI - d;
      if (d > 28 * Math.PI / 180 && dist2(p[i], p[cuts[cuts.length - 1]]) > 0.004) cuts.push(i);
    }
    cuts.push(p.length - 1);
    const out = [];
    for (let k = 0; k + 1 < cuts.length; k++) { const seg = p.slice(cuts[k], cuts[k + 1] + 1); if (seg.length >= 2 && chainLen(seg) > 0.004) out.push({ label: ch.label, pts: seg }); }
    return out;
  };

  const mirrorChains = (chains) => chains.slice().reverse().map((c) => ({ label: c.label, pts: c.pts.slice().reverse().map((q) => [-q[0], q[1]]) }));

  I.plan = (raw, body, opt) => {
    opt = opt || {};
    const rec = I.RECIPES[raw.design]; if (!rec) throw new Error('no recipe for ' + raw.design);
    const L = body.landmarks, S = body.sections;
    const yWaist = L.waist[1], yHps = L.cervicale[1] - 0.005;
    // ---- 1. instances: metres, y up, counter-clockwise
    const inst = [];
    for (const rp of raw.pieces) {
      const spec = rec.parts[rp.id]; if (!spec) continue;
      let chains = rp.chains.map((c) => ({ label: c.label, pts: c.pts.map(([x, y]) => [x / 1000, -y / 1000]) }));
      if (spec.kind === 'band') chains = chains.map((c) => ({ label: c.label, pts: c.pts.map(([x, y]) => [-y, x]) }));   // long axis horizontal
      const flat = [].concat(...chains.map((c) => c.pts));
      if (area(flat) < 0) chains = chains.slice().reverse().map((c) => ({ label: c.label, pts: c.pts.slice().reverse() }));
      const copies = rp.cut >= 2 && !rp.onFold ? ['L', 'R'] : [null];
      copies.forEach((side, k) => {
        let ch = chains; if (k === 1) ch = mirrorChains(chains);
        const parts = [].concat(...ch.map(splitCorners));
        inst.push({ id: rp.id.replace(/^[a-z]+\./, '') + (side ? '-' + side : ''), base: rp.id, spec, side, chains: parts, raw: rp });
      });
    }
    // ---- 2. wrap descriptions (fit group first: the circumference the pieces cover row by row)
    for (const pc of inst) {
      const flat = [].concat(...pc.chains.map((c) => c.pts));
      pc.bb = FL.bbox(flat);
      const h = pc.bb.y1 - pc.bb.y0;
      if (pc.spec.kind === 'body') pc.wrap = { mode: 'fit', theta: pc.spec.theta, y: (pc.spec.top === 'hps' ? yHps : yWaist) - h, clear: 0.012 };
      else if (pc.spec.kind === 'band') pc.wrap = { mode: 'fit', theta: 0, y: yWaist - 0.002, clear: 0.012, group: 'band' };
      else if (pc.spec.kind === 'sleeve') pc.wrap = { mode: 'arm', side: pc.side === 'R' ? 'R' : 'L', sTop: 0.045, clear: 0.02, phiSeam: pc.side === 'R' ? (+process.env.PR || -90) : (+process.env.PL || -90) };
      pc.flat = flat;
    }
    // sleeves: a cut-2 part; the left copy is the original; wrap sides assigned above (L/R)
    for (const grp of [...new Set(inst.filter((p) => p.wrap.mode === 'fit').map((p) => p.wrap.group || 'main'))]) {
      const fitters = inst.filter((p) => p.wrap.mode === 'fit' && (p.wrap.group || 'main') === grp);
      const y0 = Math.min(...fitters.map((p) => p.wrap.y)), y1 = Math.max(...fitters.map((p) => p.wrap.y + p.bb.y1 - p.bb.y0)), dy = 0.01, n = Math.ceil((y1 - y0) / dy) + 1;
      const rowW = (pc, y) => { let v = y - pc.wrap.y + pc.bb.y0; if (v < pc.bb.y0 - 1e-6 || v > pc.bb.y1 + 1e-6) return 0; v = Math.max(pc.bb.y0 + 1e-5, Math.min(pc.bb.y1 - 1e-5, v)); let lo = 1e9, hi = -1e9;
        for (const ch of pc.chains) for (let i = 0; i + 1 < ch.pts.length; i++) { const a = ch.pts[i], b = ch.pts[i + 1]; if ((a[1] - v) * (b[1] - v) <= 0 && a[1] !== b[1]) { const x = a[0] + (b[0] - a[0]) * (v - a[1]) / (b[1] - a[1]); lo = Math.min(lo, x); hi = Math.max(hi, x); } }
        return hi > lo ? hi - lo : 0; };
      const P = []; for (let k = 0; k < n; k++) { const y = y0 + k * dy; P.push(+fitters.reduce((s, pc) => s + rowW(pc, y), 0).toFixed(4)); }
      for (const pc of fitters) { pc.wrap.P = P; pc.wrap.Py0 = y0; pc.wrap.Pdy = dy; }
    }
    // ---- 3. start geometry of every chain (3D) and seam matching
    const chainList = [];
    for (const pc of inst) {
      const stub = FL.normalizePiece({ outline: pc.flat.slice(), wrap: Object.assign({}, pc.wrap) });
      pc.wrapObj = FL.makeWrap(stub, body);
      pc.chains.forEach((c, k) => { const cv = mkCurve(c.pts); chainList.push({ pc, k, c, cv, M: Math.max(8, Math.min(60, Math.ceil(cv.L / 0.01))) }); });
    }
    const M = 40;
    for (const e of chainList) { e.P3 = []; for (let i = 0; i <= M; i++) { const q = e.cv.at(i / M); e.P3.push(e.pc.wrapObj.point(q[0], q[1])); } }
    for (const e of chainList) { e.T3 = e.P3.map((q, i) => { const a = e.P3[Math.max(0, i - 1)], b = e.P3[Math.min(M, i + 1)], t = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], l = Math.hypot(...t) || 1; return t.map((x) => x / l); }); }
    const sideBy = (a, i, b, j) => { const d = [b.P3[j][0] - a.P3[i][0], b.P3[j][1] - a.P3[i][1], b.P3[j][2] - a.P3[i][2]], l = Math.hypot(...d); return l < 0.006 || Math.abs(d[0] * a.T3[i][0] + d[1] * a.T3[i][1] + d[2] * a.T3[i][2]) / l < 0.6; };
    const tolFor = (a, b) => (a.c.label === 'sleevecap' || b.c.label === 'sleevecap') ? 0.10 : 0.05;
    // For every pair of chains: the samples of a that have a side-by-side neighbour on b (within tolerance), as runs
    // that may skip two samples; a pair is a seam when both chains have such a run. Greedy by length, no overlaps.
    const nearOn = (p, ta, b) => {   // closest point of b's 3D polyline to p: [distance, fractional sample index, side-by-side?]
      let best = Infinity, bj = -1, bd = null;
      for (let j = 0; j < M; j++) {
        const A = b.P3[j], B = b.P3[j + 1], e = [B[0] - A[0], B[1] - A[1], B[2] - A[2]], l2 = e[0] * e[0] + e[1] * e[1] + e[2] * e[2] || 1e-12;
        const t = Math.max(0, Math.min(1, ((p[0] - A[0]) * e[0] + (p[1] - A[1]) * e[1] + (p[2] - A[2]) * e[2]) / l2)), d = [A[0] + t * e[0] - p[0], A[1] + t * e[1] - p[1], A[2] + t * e[2] - p[2]], dl = Math.hypot(...d);
        if (dl < best) { best = dl; bj = j + t; bd = d; }
      }
      const side = best < 0.006 || Math.abs(bd[0] * ta[0] + bd[1] * ta[1] + bd[2] * ta[2]) / best < 0.6;
      return [best, bj, side];
    };
    const runsOn = (a, b) => {
      const tol = tolFor(a, b), ok = [];
      for (let i = 0; i <= M; i++) { const r = nearOn(a.P3[i], a.T3[i], b); ok.push(r[0] < tol && r[2] ? r[1] : -1); }
      const runs = []; let cur = null, gap = 0;
      for (let i = 0; i <= M; i++) {
        if (ok[i] >= 0) { if (cur && gap <= 2) { cur.i1 = i; } else { if (cur) runs.push(cur); cur = { i0: i, i1: i }; } gap = 0; } else gap++;
      }
      if (cur) runs.push(cur);
      return runs.filter((r) => r.i1 - r.i0 >= 3 && (r.i1 - r.i0) / M * a.cv.L >= 0.012).map((r) => ({ f: [r.i0 / M, r.i1 / M], js: ok.slice(r.i0, r.i1 + 1).filter((x) => x >= 0) }));
    };
    const cand = [];
    for (let x = 0; x < chainList.length; x++) for (let y = x + 1; y < chainList.length; y++) {
      const a = chainList[x], b = chainList[y], ra = runsOn(a, b), rb = runsOn(b, a);
      for (const p of ra) for (const q of rb) {
        const ja = [Math.min(...p.js) / M, Math.max(...p.js) / M];   // where on b the a-run points to
        const ov = Math.min(ja[1], q.f[1]) - Math.max(ja[0], q.f[0]);
        const la = (p.f[1] - p.f[0]) * a.cv.L, lb = (q.f[1] - q.f[0]) * b.cv.L;
        if (ov > 0.5 * Math.min(ja[1] - ja[0], q.f[1] - q.f[0]) && Math.max(la, lb) <= 1.7 * Math.min(la, lb)) cand.push({ a, b, fa: p.f, fb: q.f, score: Math.min(la, lb) });
      }
    }
    // Sleeves: the cap is sewn to the armholes by construction (the front half to one armhole, the back half to the other),
    // not by where the start pose happens to put them; the armhole is the run of chains from the side seam's upper end up to
    // the shoulder tip.
    const forced = [];
    for (const sl of inst.filter((p) => p.spec.kind === 'sleeve')) {
      const cap = chainList.find((e) => e.pc === sl && e.c.label === 'sleevecap'); if (!cap) continue;
      const cp = cap.c.pts; let pk = 0; for (let i = 1; i < cp.length; i++) if (cp[i][1] > cp[pk][1]) pk = i;
      const fp = chainLen(cp.slice(0, pk + 1)) / cap.cv.L, sgn = sl.side === 'R' ? -1 : 1, arm = [];
      for (const bp of inst.filter((p) => p.spec.kind === 'body')) {
        const ch = chainList.filter((e) => e.pc === bp), nc = ch.length;
        const ss = ch.find((e) => /^sideSeam/.test(e.c.label || '') && Math.sign(e.P3[M][0] + e.P3[0][0]) === sgn); if (!ss) continue;
        const up = ss.P3[M][1] > ss.P3[0][1], dir = up ? 1 : -1; let k = (ss.k + dir + nc) % nc, acc = [], best = null;
        for (let n = 0; n < 4; n++, k = (k + dir + nc) % nc) { acc.push(ch[k]); const tl = acc.reduce((t, e) => t + e.cv.L, 0), d = Math.abs(tl - cap.cv.L / 2); if (!best || d < best.d) best = { d, n: acc.length }; }
        arm.push({ bp, list: acc.slice(0, best.n), dir, len: acc.slice(0, best.n).reduce((t, e) => t + e.cv.L, 0) });
      }
      if (arm.length !== 2) continue;
      // which armhole takes the first part of the cap (corner A -> peak): the one nearer to the cap's first half
      const mid = (f) => sl.wrapObj.point(...cap.cv.at(f)), amid = (a) => a.list[Math.floor(a.list.length / 2)].P3[M >> 1];
      const dd = (i, j) => dist3(mid(i ? (fp + 1) / 2 : fp / 2), amid(arm[j]));
      const swap = dd(0, 0) + dd(1, 1) > dd(0, 1) + dd(1, 0), halves = [[0, fp], [fp, 1]];
      [0, 1].forEach((h) => {
        const a = arm[swap ? 1 - h : h], f = halves[h], tot = a.len; let acc = 0;
        const ord = h === 0 ? [0, 1] : [1, 0];   // cap order from the corner towards the peak: half 0 forward, half 1 backward
        a.list.forEach((e) => { const l0 = acc / tot, l1 = (acc + e.cv.L) / tot; acc += e.cv.L;
          const g = (t) => (h === 0 ? f[0] + (f[1] - f[0]) * t : f[1] - (f[1] - f[0]) * t), c0 = g(l0), c1 = g(l1);
          forced.push({ a: cap, b: e, fa: [Math.min(c0, c1), Math.max(c0, c1)], fb: [0, 1] }); });
        void ord;
      });
    }
    cand.sort((p, q) => q.score - p.score);
    const used0 = [];
    const used = new Map(), overlaps = (e, f) => (used.get(e) || []).some((g) => Math.min(g[1], f[1]) - Math.max(g[0], f[0]) > 0.05);
    const pairs = [];
    for (const c of forced) { (used.get(c.a) || used.set(c.a, []).get(c.a)).push(c.fa); (used.get(c.b) || used.set(c.b, []).get(c.b)).push(c.fb); pairs.push(c); }
    for (const c of cand) {
      if (overlaps(c.a, c.fa) || overlaps(c.b, c.fb)) continue;
      const ext = (e, f) => (f[1] - f[0] >= 0.85 || (e.cv.L < 0.2 && f[1] - f[0] >= 0.5) ? [0, 1] : f);
      const fa = ext(c.a, c.fa), fb = ext(c.b, c.fb);
      (used.get(c.a) || used.set(c.a, []).get(c.a)).push(fa); (used.get(c.b) || used.set(c.b, []).get(c.b)).push(fb);
      pairs.push({ a: c.a, b: c.b, fa, fb });
    }
    // dedupe self-pairs (a chain pairing with another chain of the same piece is found from both sides: kept once above)
    // ---- 4. intervals per chain, outline per piece
    for (const e of chainList) e.cuts = new Set([0, 1]);
    for (const p of pairs) { for (const f of p.fa) p.a.cuts.add(+f.toFixed(4)); for (const f of p.fb) p.b.cuts.add(+f.toFixed(4)); }
    for (const e of chainList) {
      let c = [...e.cuts].sort((x, y) => x - y); const m = [c[0]];
      for (let i = 1; i < c.length; i++) if (c[i] - m[m.length - 1] > 0.03 || i === c.length - 1) { if (c[i] - m[m.length - 1] <= 0.03 && m.length > 1) m[m.length - 1] = c[i]; else m.push(c[i]); }
      e.iv = []; for (let i = 0; i + 1 < m.length; i++) e.iv.push({ f0: m[i], f1: m[i + 1], n: 0, pair: null, e0: -1 });
    }
    const findIv = (e, f) => e.iv.find((v) => Math.abs(v.f0 - f[0]) < 0.035 && Math.abs(v.f1 - f[1]) < 0.035) || e.iv.reduce((best, v) => { const ov = Math.min(v.f1, f[1]) - Math.max(v.f0, f[0]); return ov > (best.ov || -1) ? { v, ov } : best; }, {}).v;
    const STEP = 0.032;
    const seamPairs = [];
    for (const p of pairs) {
      const A = findIv(p.a, p.fa), B = findIv(p.b, p.fb); if (!A || !B || A === B || A.pair || B.pair) continue;
      A.pair = B; B.pair = A; A.owner = p.a; B.owner = p.b;
      const la = (A.f1 - A.f0) * p.a.cv.L, lb = (B.f1 - B.f0) * p.b.cv.L;
      A.n = B.n = Math.max(1, Math.ceil(Math.max(la, lb) / STEP));
      seamPairs.push({ A, B, a: p.a, b: p.b });
    }
    const outlines = new Map();
    for (const pc of inst) {
      const pts = [], names = []; pc.ivs = [];
      for (const e of chainList.filter((x) => x.pc === pc)) for (const v of e.iv) {
        if (!v.n) v.n = Math.max(1, Math.ceil((v.f1 - v.f0) * e.cv.L / STEP));
        v.owner = e; v.e0 = pts.length;
        for (let i = 0; i < v.n; i++) { pts.push(e.cv.at(v.f0 + (v.f1 - v.f0) * i / v.n)); }
        pc.ivs.push(v);
      }
      pc.outline = pts;
    }
    const seams = [];
    const idOf = (e) => e.pc.id;
    for (const sp of seamPairs) {
      const { A, B, a, b } = sp, n = A.n;
      const A0 = a.pc.wrapObj.point(...a.cv.at(A.f0)), A1 = a.pc.wrapObj.point(...a.cv.at(A.f1)), B0 = b.pc.wrapObj.point(...b.cv.at(B.f0)), B1 = b.pc.wrapObj.point(...b.cv.at(B.f1));
      const rev = dist3(A0, B1) + dist3(A1, B0) < dist3(A0, B0) + dist3(A1, B1);
      for (let j = 0; j < n; j++) seams.push({ a: [idOf(a), A.e0 + j], b: [idOf(b), B.e0 + (rev ? n - 1 - j : j)], reverse: rev });
      sp.rev = rev;
    }
    // ---- 5. pieces + table layout + information
    let tx = 0;
    const pieces = inst.map((pc) => {
      const bb = FL.bbox(pc.outline), horiz = Math.abs(((pc.raw.grain || 270) % 180)) === 90 ? Math.PI / 2 : 0;
      const piece = { id: pc.id, name: (pc.spec.role || pc.id) + (pc.side === 'L' ? ' (left)' : pc.side === 'R' ? ' (right)' : ''), outline: pc.outline, grain: pc.spec.kind === 'band' ? 0 : Math.PI / 2, table: { x: tx - bb.x0, y: -bb.y0 }, wrap: pc.wrap };
      tx += bb.x1 - bb.x0 + 0.06; void horiz; return piece;
    });
    const lab = (e) => e.c.label || '';
    const edgeLabel = (e, k) => { const l = lab(e); return l.replace(/\.mirror$/, '') || ''; };
    const info = {
      design: raw.design, title: rec.title, url: rec.url, license: 'MIT (FreeSewing)', measurements: raw.measurements,
      pieces: inst.map((pc) => ({ id: pc.id, role: pc.spec.role, cut: pc.raw.onFold ? 1 : (pc.side ? 1 : pc.raw.cut), cutNote: pc.raw.onFold ? 'cut 1 on the fold (the fold is already unfolded here)' : pc.side ? 'one of a mirrored pair (cut ' + pc.raw.cut + ')' : 'cut ' + pc.raw.cut, grain: pc.raw.grain, material: pc.raw.material })),
      seams: seamPairs.map((sp) => ({ a: idOf(sp.a), b: idOf(sp.b), la: +(((sp.A.f1 - sp.A.f0) * sp.a.cv.L) * 100).toFixed(1), lb: +(((sp.B.f1 - sp.B.f0) * sp.b.cv.L) * 100).toFixed(1), n: sp.A.n, labels: [edgeLabel(sp.a), edgeLabel(sp.b)], ch: [sp.a.k, sp.b.k] })),
    };
    void S;
    if (opt.debug) info.cov = chainList.map((e) => e.pc.id + '#' + e.k + ' ' + (e.c.label || '-') + ' L=' + (e.cv.L * 100).toFixed(1) + ' iv=' + e.iv.map((v) => (v.pair ? 'S' : '.') + ((v.f1 - v.f0) * e.cv.L * 100).toFixed(0)).join(','));
    if (opt.debug) info.dbg = chainList.map((e) => ({ id: e.pc.id, k: e.k, label: e.c.label || '', P3: e.P3, L: e.cv.L }));
    return { name: rec.title + ' (FreeSewing)', fabric: rec.fabric, notes: rec.title + ' drafted by FreeSewing for this mannequin\'s measurements; seams found automatically.', pieces, seams, info };
  };
  if (typeof module !== 'undefined') module.exports = I;
})(typeof FL !== 'undefined' ? FL : require('./shared.js'));
