// Garment drafting: patterns computed from the mannequin's own measurements (not copied from any commercial pattern).
//   FL.draft.pants(body)  -> plan { name, fabric, notes, pieces, seams }   straight-leg trousers, 4 panels
//   FL.draft.measures(body) -> the girths and heights the drafts use (read off the body's section table)
// Units are metres, outlines run counter-clockwise (x right, y up, seen from outside the garment), edge i runs
// outline[i] -> outline[i+1]. Panels are wrapped onto the body by FL.makeWrapRows (wrap.mode).
(function (FL) {
  'use strict';
  const draft = {};
  FL.draft = draft;

  // back arc of a convex section polygon [x, z, ...]: from its widest point on one side to the other, through the back (low z)
  const backArc = (h) => {
    const n = h.length >> 1; if (n < 3) return 0;
    let iMax = 0, iMin = 0; for (let i = 1; i < n; i++) { if (h[2 * i] > h[2 * iMax]) iMax = i; if (h[2 * i] < h[2 * iMin]) iMin = i; }
    const walk = (a, b) => { let len = 0, zs = 0, c = 0, i = a; while (i !== b) { const j = (i + 1) % n; len += Math.hypot(h[2 * i] - h[2 * j], h[2 * i + 1] - h[2 * j + 1]); zs += h[2 * i + 1]; c++; i = j; } return [len, zs / Math.max(1, c)]; };
    const A = walk(iMax, iMin), B = walk(iMin, iMax); return A[1] < B[1] ? A[0] : B[0];
  };
  // where the plane x = 0 cuts a convex section: [front z, back z]
  const midZ = (h) => {
    const n = h.length >> 1; let zf = -9, zb = 9;
    for (let i = 0; i < n; i++) { const j = (i + 1) % n, x0 = h[2 * i], x1 = h[2 * j]; if (x0 * x1 <= 0 && x0 !== x1) { const z = h[2 * i + 1] + (h[2 * j + 1] - h[2 * i + 1]) * (0 - x0) / (x1 - x0); zf = Math.max(zf, z); zb = Math.min(zb, z); } }
    return zf > -9 ? [zf, zb] : null;
  };
  draft.measures = (body) => {
    const S = body.sections, at = (list, y) => list[Math.max(0, Math.min(S.ny - 1, Math.round(y / S.dy)))];
    const per = (h) => (h && h.length >= 6 ? FL.perimeter2(h) : 0);
    const J = body.joints, L = body.landmarks, yc = S.yCrotch;
    const m = {
      yCrotch: yc, yWaist: L.waist[1], yHip: L.hip[1], yKnee: J['l-knee'][1], yAnkle: J['l-ankle'][1],
      yShoulder: L['acromion.L'][1], yNeck: L.cervicale[1], yBust: L['bust.L'][1],
      hip: per(at(S.hulls, L.hip[1])), waist: per(at(S.hulls, L.waist[1])), bust: per(at(S.hulls, L['bust.L'][1])),
      thigh: per(at(S.legs.L, yc - 0.035)), knee: per(at(S.legs.L, J['l-knee'][1])), ankle: per(at(S.legs.L, J['l-ankle'][1] + 0.04)),
    };
    // seat: the fullest girth 3-16 cm above the crotch (the buttocks; at the crotch itself the section is just both thighs)
    let ks = Math.ceil((yc + 0.03) / S.dy); const k1 = Math.floor(Math.min(L.waist[1] - 0.06, yc + 0.16) / S.dy);
    for (let k = ks; k <= k1; k++) if (S.girth[k] > S.girth[ks]) ks = k;
    m.ySeat = ks * S.dy; m.seat = S.girth[ks]; m.seatBack = backArc(S.hulls[ks]); m.waistBack = backArc(at(S.hulls, L.waist[1]));
    // crotch seam: a taut tape from the centre front waist down the front, round the crotch point, up the back to the centre back waist
    const cp = L.crotch, kc = Math.ceil(yc / S.dy), kw = Math.round(L.waist[1] / S.dy); let lf = 0, lb = 0, pf = null, pb = null;
    for (let k = kw; k >= kc; k--) { const r = midZ(S.hulls[k]); if (!r) continue; const y = k * S.dy; if (pf) { lf += Math.hypot(pf[0] - y, pf[1] - r[0]); lb += Math.hypot(pb[0] - y, pb[1] - r[1]); } pf = [y, r[0]]; pb = [y, r[1]]; }
    if (pf) { lf += Math.hypot(pf[0] - cp[1], pf[1] - cp[2]); lb += Math.hypot(pb[0] - cp[1], pb[1] - cp[2]); m.crossSeam = lf + lb; m.crossSeamFront = lf; }
    return m;
  };

  // ---- trousers
  const mirrorPiece = (pts, names) => {   // reflect x and reverse so the outline stays counter-clockwise; edge names follow
    const n = pts.length, X = Math.max(...pts.map((p) => p[0])), out = [], on = [];
    for (let j = 0; j < n; j++) out.push([X - pts[n - 1 - j][0], pts[n - 1 - j][1]]);
    for (let j = 0; j < n; j++) on.push(names[((n - 2 - j) % n + n) % n]);
    return { pts: out, names: on };
  };
  draft.pants = (body, opt) => {
    opt = opt || {};
    const m = draft.measures(body), hemY = opt.hemY == null ? 0.04 : opt.hemY, ease = opt.ease == null ? 0.035 : opt.ease;
    const Li = m.yCrotch - hemY, vk = m.yKnee - hemY, vH = m.yHip - hemY, vT = (opt.topY == null ? m.yWaist - 0.01 : opt.topY) - hemY;
    const Hg = m.hip, Wg = m.waist, S = body.sections, rk = (y) => Math.max(0, Math.min(S.ny - 1, Math.round(y / S.dy)));
    const curve = (y) => FL.sectionCurve(body, rk(y), 0.012);
    const arcF = (y) => { const c = curve(y); return c.cum(Math.PI / 2) - c.cum(0); };          // centre front -> side, at height y
    const arcB = (y) => { const c = curve(y); return c.cum(Math.PI) - c.cum(Math.PI / 2); };    // side -> centre back
    const yTopW = opt.topY == null ? m.yWaist - 0.01 : opt.topY, yTopB = yTopW + 0.03;
    // belly row: the fullest part of the trunk between the top edge and the hip line (kept at least 3 cm from both)
    let yb = (yTopW + m.yHip) / 2, gb = 0;
    for (let y = m.yHip + 0.03; y <= yTopW - 0.03; y += S.dy) { const g = S.girth[rk(y)]; if (g > gb) { gb = g; yb = y; } }
    const wf = arcF(m.yHip) + ease / 4, wb = arcB(m.yHip) + ease / 4;
    const extTotal = Math.max(0.07, (m.thigh + 0.08) - (wf + wb));
    const ef = 0.30 * extTotal, eb = 0.70 * extTotal;
    const kneeG = Math.max(m.knee + 0.06, 0.34), hemG = opt.hemGirth || Math.max(m.ankle + 0.07, 0.40);
    const front = { w: wf, e: ef, top: vT, cfTop: wf, share: 0.46, arc: arcF, yTop: yTopW };
    const back = { w: wb, e: eb, top: yTopB - hemY, cfTop: wb + 0.025, share: 0.54, arc: arcB, yTop: yTopB };
    const make = (f) => {
      const xc = (f.w + f.e) / 2, kw = f.share * kneeG, hw = f.share * hemG;
      const pts = [], names = [];
      const add = (x, y, name) => { pts.push([x, y]); names.push(name); };   // name = edge from this point to the next
      add(xc - hw / 2, 0, 'hem');
      add(xc + hw / 2, 0, 'inseam0');
      add(xc + kw / 2, vk, 'inseam1');
      add(f.w + f.e, Li, 'crotch0');                               // crotch point; curve up to the centre line at hip level
      for (let i = 1; i <= 3; i++) { const s2 = 1 - i / 4; add(f.w + f.e * (1 - Math.cos(Math.PI / 2 * s2)), vH + (Li - vH) * s2, 'crotch' + i); }
      add(f.w, vH, 'rise');
      add(f.cfTop, f.top, 'waist');
      add(f.cfTop - f.arc(f.yTop) - ease / 4, f.top, 'out2a');
      add(f.w - f.arc(yb) - ease / 4, yb - hemY, 'out2b');
      add(0, vH, 'out1');
      add(xc - kw / 2, vk, 'out0');
      return { pts, names };
    };
    const F = make(front), Bk = make(back), Fm = mirrorPiece(F.pts, F.names), Bm = mirrorPiece(Bk.pts, Bk.names);
    const mk = (id, name, P, side, theta, y0) => ({ id, name, outline: P.pts, names: P.names, grain: Math.PI / 2, table: { x: y0, y: 0 }, wrap: { mode: 'leg', side, theta, y: hemY, clear: 0.012 } });
    const pieces = [mk('frontR', 'Front, right leg', F, 'R', 0, 0.0), mk('frontL', 'Front, left leg', Fm, 'L', 0, 0.55),
      mk('backL', 'Back, left leg', Bk, 'L', 180, 1.1), mk('backR', 'Back, right leg', Bm, 'R', 180, 1.65)];
    const idx = (p, nm) => p.names.indexOf(nm);
    const byId = Object.fromEntries(pieces.map((p) => [p.id, p]));
    const pair = (a, b, names) => names.map((nm) => ({ a: [a, idx(byId[a], nm)], b: [b, idx(byId[b], nm)], reverse: true }));
    const seams = [].concat(
      pair('frontL', 'backL', ['out0', 'out1', 'out2a', 'out2b', 'inseam0', 'inseam1']),
      pair('frontR', 'backR', ['out0', 'out1', 'out2a', 'out2b', 'inseam0', 'inseam1']),
      pair('frontL', 'frontR', ['crotch0', 'crotch1', 'crotch2', 'crotch3', 'rise']),
      pair('backL', 'backR', ['crotch0', 'crotch1', 'crotch2', 'crotch3', 'rise']));
    for (const p of pieces) delete p.names;
    return {
      name: 'Straight trousers - 4 panels (drafted to this body)', fabric: opt.fabric || 'denim_12oz',
      notes: 'Front and back panel for each leg, drafted from this mannequin: hip ' + (Hg * 100).toFixed(0) + ' cm, waist ' + (Wg * 100).toFixed(0) +
        ' cm, thigh ' + (m.thigh * 100).toFixed(0) + ' cm, knee ' + (m.knee * 100).toFixed(0) + ' cm. Seams: outseams, inseams, front and back rise. No waistband, pockets or darts.',
      pieces, seams,
    };
  };

  // ---- tank top / sleeveless shell: two panels drafted by unrolling the body's own trunk sections (arc lengths of
  // the hull at every row, plus the clearance), then wrapped back with mode 'arc' (arc length = pattern x).
  draft.shirt = (body, opt) => draft.tank(body, Object.assign({ sleeves: true, armholeScoop: 12, armholeDepth: 0.19, sleeveEase: 1.0, fabric: 'cotton_poplin' }, opt || {}));
  draft.tank = (body, opt) => {
    opt = opt || {};
    const S = body.sections, m = draft.measures(body), clear = opt.clear == null ? 0.02 : opt.clear, D = Math.PI / 180;
    const yTip = m.yShoulder, yTop = yTip + 0.05, yU = yTip - (opt.armholeDepth || 0.17), yHem = opt.hemY == null ? m.yHip : opt.hemY, scoop = (opt.scoop || 0.11) , strapW = 0.032;
    const rowK = (y) => Math.max(0, Math.min(S.ny - 1, Math.round(y / S.dy)));
    const arcAt = (y, theta, dphi) => { const c = FL.sectionCurve(body, rowK(y), clear); return c.cum(theta + dphi) - c.cum(theta); };
    // sample heights (identical for front and back so that seam edges pair one to one)
    const ys = [yHem]; for (let y = yHem + 0.05; y < yU - 0.02; y += 0.05) ys.push(y);
    ys.push(yU); const na = 4; for (let i = 1; i <= na; i++) ys.push(yU + (yTip - yU) * i / na);
    const nsh = 2; for (let i = 1; i <= nsh; i++) ys.push(yTip + (yTop - yTip) * i / nsh);
    const iU = ys.indexOf(yU), iTip = iU + na;
    const make = (theta, depth) => {
      const half = (y) => { // angular half extent of the panel at height y (degrees from the centre line)
        if (y <= yU || y >= yTip) return 90;
        const s = (y - yU) / (yTip - yU); return 90 - (opt.armholeScoop || 24) * Math.sin(Math.PI * s);
      };
      const R = ys.map((y) => arcAt(y, theta, half(y) * D));
      const xTop = R[R.length - 1] - strapW, yN0 = yTop - depth;
      const neck = []; const nn = 6; for (let i = 0; i <= nn; i++) { const sN = i / nn, y = yTop - (yTop - yN0) * sN, q = (y - yN0) / (yTop - yN0); neck.push([xTop * Math.sqrt(Math.max(0, 2 * q - q * q)), y]); }
      const pts = [], names = [], v = (y) => y - yHem;
      // bottom edge, right side up, strap, neckline down and up, strap, left side down
      pts.push([-R[0], 0]); names.push('hem'); pts.push([R[0], 0]);
      for (let i = 0; i < ys.length - 1; i++) { names.push(i < iU ? 'side' + i : i < iTip ? 'arm' + i : 'sh' + i); pts.push([R[i + 1], v(ys[i + 1])]); }
      names.push('strapR'); pts.push([neck[0][0], v(neck[0][1])]);
      for (let i = 0; i < neck.length - 1; i++) { names.push('neck' + i); pts.push([neck[i + 1][0], v(neck[i + 1][1])]); }
      // left mirror of the neckline and the rest
      for (let i = neck.length - 2; i >= 0; i--) { names.push('neck' + (neck.length - 1 + (neck.length - 2 - i))); pts.push([-neck[i][0], v(neck[i][1])]); }
      names.push('strapL'); pts.push([-R[R.length - 1], v(ys[ys.length - 1])]);
      for (let i = ys.length - 2; i >= 0; i--) { names.push((i < iU ? 'sideL' : i < iTip ? 'armL' : 'shL') + i); pts.push([-R[i], v(ys[i])]); }
      // pts now ends back at (-R[0], v(yHem)) = the first point: drop the duplicate
      pts.pop();
      return { pts, names };
    };
    const Fr = make(0, scoop), Bk = make(Math.PI, 0.03);
    // the neckline's mirrored half is named neck6..; right half sides are 'sideN', left 'sideLN': make names unique
    const mk = (id, name, P, theta, x) => ({ id, name, outline: P.pts, names: P.names, grain: Math.PI / 2, table: { x, y: 0 }, wrap: { mode: 'arc', theta, y: yHem, clear } });
    const pieces = [mk('front', 'Front', Fr, 0, 0.0), mk('back', 'Back', Bk, 180, 0.75)];
    const idx = (p, nm) => p.names.indexOf(nm);
    const seams = [];
    // front right (+x, the body's left) side/shoulder <-> back left (pattern -x = world +x); and the mirror
    for (let i = 0; i < ys.length - 1; i++) {
      const kind = i < iU ? 'side' : i < iTip ? null : 'sh', kindL = i < iU ? 'sideL' : i < iTip ? null : 'shL';
      if (!kind) continue;
      seams.push({ a: ['front', idx(pieces[0], kind + i)], b: ['back', idx(pieces[1], kindL + i)], reverse: true });
      seams.push({ a: ['front', idx(pieces[0], kindL + i)], b: ['back', idx(pieces[1], kind + i)], reverse: true });
    }
    if (opt.sleeves) {
      const sl = opt.sleeveLen || 0.20, na_ = na;
      const len = (P, nm) => { const i = idx(P, nm), o = P.outline, q = o[i], r = o[(i + 1) % o.length]; return Math.hypot(r[0] - q[0], r[1] - q[1]); };
      pieces.forEach((P, i) => { P.names = [Fr, Bk][i].names; });
      let tx = 1.5;
      for (const side of ['L', 'R']) {
        const A = side === 'L' ? pieces[0] : pieces[1], B = side === 'L' ? pieces[1] : pieces[0];
        const la = [], lb = [];
        for (let k = 0; k < na_; k++) la.push(len(A, 'arm' + (iU + k)));
        for (let j = 0; j < na_; j++) lb.push(len(B, 'armL' + (iTip - 1 - j)));
        const se = opt.sleeveEase || 0.9, xs = [0]; for (const l of la.concat(lb)) xs.push(xs[xs.length - 1] + l * se);   // x of the top-edge vertices, 0 .. W
        const W = xs[xs.length - 1], Wh = W * (opt.sleeveHem || 0.9), pts = [[0, 0], [Wh, 0], [W, sl]], names = ['hem', 'seamR'];
        for (let i = xs.length - 1; i >= 1; i--) { names.push(i - 1 >= na_ ? 'topB' + (i - 1 - na_) : 'topA' + (i - 1)); pts.push([xs[i - 1], sl]); }
        names.push('seamL');
        const id = 'sleeve' + side;
        pieces.push({ id, name: (side === 'L' ? 'Left' : 'Right') + ' sleeve', outline: pts, names, grain: Math.PI / 2, table: { x: tx, y: 0 }, wrap: { mode: 'arm', side, sTop: 0.06, clear: 0.02, phiSeam: side === 'L' ? -90 : 90 } });
        tx += 0.0;
        const S2 = pieces[pieces.length - 1];
        for (let k = 0; k < na_; k++) seams.push({ a: [id, idx(S2, 'topA' + k)], b: [A.id, idx(A, 'arm' + (iU + k))], reverse: true });
        for (let j = 0; j < na_; j++) seams.push({ a: [id, idx(S2, 'topB' + j)], b: [B.id, idx(B, 'armL' + (iTip - 1 - j))], reverse: true });
        seams.push({ a: [id, idx(S2, 'seamR')], b: [id, idx(S2, 'seamL')], reverse: true });
      }
      pieces[2].table = { x: 1.3, y: 0 }; pieces[3].table = { x: 1.3, y: 0.4 };
    }
    for (const p of pieces) delete p.names;
    return {
      name: opt.sleeves ? 'T-shirt - body + 2 sleeves (drafted to this body)' : 'Tank top - 2 panels (drafted to this body)', fabric: opt.fabric || 'cotton_poplin',
      notes: 'Front and back, unrolled from this mannequin\'s trunk sections (arc length of the body at every height plus ' + (clear * 1000).toFixed(0) +
        ' mm ease). Side seams and shoulder seams; scoop neckline, no darts or hem allowance.',
      pieces, seams,
    };
  };

  if (typeof module !== 'undefined') module.exports = draft;
})(typeof FL !== 'undefined' ? FL : require('./shared.js'));
