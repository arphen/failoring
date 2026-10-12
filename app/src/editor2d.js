// 2D cutting table: pan/zoom canvas, select/move pieces, drag corners, add/delete corners, sew edges.
'use strict';
const Ed = {
  cv: null, ctx: null, Z: 260, ox: 0, oy: 0, drag: null, hover: null, spaceDown: false, dirty: true,
};
Ed.init = () => {
  const cv = Ed.cv = $('#c2d'); Ed.ctx = cv.getContext('2d');
  new ResizeObserver(() => { Ed.resize(); }).observe(cv);
  cv.addEventListener('mousedown', Ed.down); window.addEventListener('mousemove', Ed.move); window.addEventListener('mouseup', Ed.up);
  cv.addEventListener('wheel', Ed.wheel, { passive: false }); cv.addEventListener('dblclick', Ed.dbl);
  cv.addEventListener('contextmenu', (e) => e.preventDefault());
  window.addEventListener('keydown', Ed.key); window.addEventListener('keyup', (e) => { if (e.code === 'Space') Ed.spaceDown = false; });
  $('#toolSel').onclick = () => Ed.setTool('select'); $('#toolSew').onclick = () => Ed.setTool('sew'); $('#btnFit').onclick = Ed.fit;
  Ed.setTool('select'); Ed.resize();
};
Ed.setTool = (t) => { App.S.tool = t; App.S.sewA = null; $('#toolSel').classList.toggle('on', t === 'select'); $('#toolSew').classList.toggle('on', t === 'sew'); Ed.updateHint(); Ed.dirty = true; };
Ed.updateHint = () => {
  const S = App.S;
  $('#hint2d').textContent = S.tool === 'sew'
    ? (S.sewA ? 'Now click the edge to sew it to (Esc cancels)' : 'Sew: click the first edge, then the edge to join it to')
    : 'Drag a piece to move it, drag corners to reshape, double-click an edge to add a corner, Del removes, wheel zooms, drag empty space pans';
};
Ed.resize = () => { const r = Ed.cv.getBoundingClientRect(), d = devicePixelRatio || 1; Ed.cv.width = Math.max(10, r.width * d); Ed.cv.height = Math.max(10, r.height * d); Ed.W = r.width; Ed.H = r.height; Ed.dirty = true; if (!Ed.fitted && r.width > 50) { Ed.fit(); Ed.fitted = true; } };
Ed.toScreen = (x, y) => [Ed.ox + x * Ed.Z, Ed.oy - y * Ed.Z];
Ed.toWorld = (sx, sy) => [(sx - Ed.ox) / Ed.Z, (Ed.oy - sy) / Ed.Z];
Ed.fit = () => {
  const S = App.S; if (!S || !S.pieces.length) return;
  let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
  for (const p of S.pieces) { const b = FL.bbox(p.outline); x0 = Math.min(x0, b.x0 + p.table.x); x1 = Math.max(x1, b.x1 + p.table.x); y0 = Math.min(y0, b.y0 + p.table.y); y1 = Math.max(y1, b.y1 + p.table.y); }
  const w = Math.max(0.3, x1 - x0), h = Math.max(0.3, y1 - y0);
  Ed.Z = Math.min((Ed.W - 150) / w, (Ed.H - 150) / h);
  Ed.ox = (Ed.W - w * Ed.Z) / 2 - x0 * Ed.Z; Ed.oy = (Ed.H + h * Ed.Z) / 2 + y0 * Ed.Z; Ed.dirty = true;
};
const snapv = (v) => +(App.S.snap ? Math.round(v / 0.005) * 0.005 : v).toFixed(4);
Ed.pos = (e) => { const r = Ed.cv.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; };

// hit tests -------------------------------------------------------------
Ed.pointIn = (poly, x, y) => { let c = false; for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) { const a = poly[i], b = poly[j]; if ((a[1] > y) !== (b[1] > y) && x < (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]) + a[0]) c = !c; } return c; };
Ed.hitVertex = (p, sx, sy) => { for (let i = 0; i < p.outline.length; i++) { const [x, y] = Ed.toScreen(p.outline[i][0] + p.table.x, p.outline[i][1] + p.table.y); if (Math.hypot(x - sx, y - sy) < 9) return i; } return -1; };
Ed.hitEdge = (p, sx, sy, tol) => {
  let best = -1, bd = tol || 9, bt = 0; const o = p.outline;
  for (let i = 0; i < o.length; i++) {
    const [ax, ay] = Ed.toScreen(o[i][0] + p.table.x, o[i][1] + p.table.y), [bx, by] = Ed.toScreen(o[(i + 1) % o.length][0] + p.table.x, o[(i + 1) % o.length][1] + p.table.y);
    const dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy || 1, t = clamp(((sx - ax) * dx + (sy - ay) * dy) / L2, 0, 1), d = Math.hypot(ax + t * dx - sx, ay + t * dy - sy);
    if (d < bd) { bd = d; best = i; bt = t; }
  }
  return best < 0 ? null : { edge: best, t: bt };
};
Ed.hitPiece = (sx, sy) => {
  const [wx, wy] = Ed.toWorld(sx, sy), P = App.S.pieces;
  for (let i = P.length - 1; i >= 0; i--) if (Ed.pointIn(P[i].outline, wx - P[i].table.x, wy - P[i].table.y)) return P[i];
  return null;
};

// events ----------------------------------------------------------------
Ed.down = (e) => {
  const S = App.S, [sx, sy] = Ed.pos(e); Ed.cv.focus && Ed.cv.focus();
  if (e.button === 1 || e.button === 2 || Ed.spaceDown) { Ed.drag = { kind: 'pan', sx, sy, ox: Ed.ox, oy: Ed.oy }; return; }
  if (S.tool === 'sew') {
    let best = null;
    for (const p of S.pieces) { const h = Ed.hitEdge(p, sx, sy, 12); if (h) best = { p, e: h.edge }; }
    if (!best) { S.sewA = null; Ed.updateHint(); Ed.dirty = true; return; }
    if (!S.sewA) { S.sewA = [best.p.id, best.e]; S.sel = best.p.id; Ed.updateHint(); App.changed('ui'); return; }
    if (S.sewA[0] === best.p.id && S.sewA[1] === best.e) { S.sewA = null; Ed.updateHint(); App.changed('ui'); return; }
    App.pushUndo();
    const same = (a, b) => a[0] === b[0] && a[1] === b[1], B = [best.p.id, best.e];
    S.seams = S.seams.filter((s) => !(same(s.a, S.sewA) || same(s.b, S.sewA) || same(s.a, B) || same(s.b, B)));
    S.seams.push({ a: S.sewA, b: B, reverse: true }); S.sewA = null; S.sewFlash = S.seams.length - 1;
    Ed.updateHint(); App.changed('structure'); return;
  }
  // select tool: vertex handle of the selected piece first
  const sel = App.piece(S.sel);
  if (sel) { const vi = Ed.hitVertex(sel, sx, sy); if (vi >= 0) { App.pushUndo(); S.selV = vi; Ed.drag = { kind: 'vertex', p: sel, vi }; App.changed('ui'); return; } }
  const hp = Ed.hitPiece(sx, sy);
  if (hp) {
    App.pushUndo(); S.sel = hp.id; S.selV = -1;
    const [wx, wy] = Ed.toWorld(sx, sy); Ed.drag = { kind: 'piece', p: hp, dx: hp.table.x - wx, dy: hp.table.y - wy, moved: false }; App.changed('ui');
  } else { S.sel = null; S.selV = -1; Ed.drag = { kind: 'pan', sx, sy, ox: Ed.ox, oy: Ed.oy }; App.changed('ui'); }
};
Ed.move = (e) => {
  const [sx, sy] = Ed.pos(e), d = Ed.drag, S = App.S;
  if (!d) {   // hover
    const h = Ed.hover; let nh = null;
    if (S.tool === 'sew') { for (const p of S.pieces) { const he = Ed.hitEdge(p, sx, sy, 12); if (he) nh = { p: p.id, e: he.edge }; } }
    else { const p = Ed.hitPiece(sx, sy); nh = p ? { p: p.id } : null; }
    if (JSON.stringify(h) !== JSON.stringify(nh)) { Ed.hover = nh; Ed.dirty = true; }
    return;
  }
  if (d.kind === 'pan') { Ed.ox = d.ox + sx - d.sx; Ed.oy = d.oy + sy - d.sy; Ed.dirty = true; }
  else if (d.kind === 'piece') { const [wx, wy] = Ed.toWorld(sx, sy); d.p.table.x = snapv(wx + d.dx); d.p.table.y = snapv(wy + d.dy); d.moved = true; Ed.dirty = true; App.changed('table'); }
  else if (d.kind === 'vertex') {
    const [wx, wy] = Ed.toWorld(sx, sy), p = d.p; p.outline[d.vi] = [snapv(wx - p.table.x), snapv(wy - p.table.y)];
    Ed.dirty = true; App.changed('structure-drag');
  }
};
Ed.up = () => { const d = Ed.drag; Ed.drag = null; if (d && d.kind === 'vertex') App.changed('structure'); if (d && d.kind === 'piece' && d.moved) App.changed('table-done'); };
Ed.wheel = (e) => {
  e.preventDefault(); const [sx, sy] = Ed.pos(e), [wx, wy] = Ed.toWorld(sx, sy), f = Math.exp(-e.deltaY * 0.0015);
  Ed.Z = clamp(Ed.Z * f, 40, 3000); Ed.ox = sx - wx * Ed.Z; Ed.oy = sy + wy * Ed.Z; Ed.dirty = true;
};
Ed.dbl = (e) => {
  const S = App.S, [sx, sy] = Ed.pos(e), p = App.piece(S.sel); if (!p || S.tool !== 'select') return;
  const h = Ed.hitEdge(p, sx, sy, 10); if (!h) return;
  const a = p.outline[h.edge], b = p.outline[(h.edge + 1) % p.outline.length];
  App.pushUndo(); App.insertVertex(p, h.edge, [snapv(a[0] + (b[0] - a[0]) * h.t), snapv(a[1] + (b[1] - a[1]) * h.t)]);
  S.selV = h.edge + 1; App.changed('structure');
};
Ed.key = (e) => {
  if (/INPUT|SELECT|TEXTAREA/.test(document.activeElement.tagName) && document.activeElement.type !== 'range') return;
  const S = App.S;
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z') { e.preventDefault(); e.shiftKey ? App.doRedo() : App.doUndo(); return; }
  if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'y') { e.preventDefault(); App.doRedo(); return; }
  if (e.key === 'Escape') { S.sewA = null; Ed.updateHint(); App.changed('ui'); }
  else if (e.key === 'v') Ed.setTool('select'); else if (e.key === 's' && !e.ctrlKey && !e.metaKey) Ed.setTool('sew'); else if (e.key === 'f') Ed.fit();
  else if (e.key === 'Delete' || e.key === 'Backspace') {
    const p = App.piece(S.sel); if (!p) return; App.pushUndo();
    if (S.selV >= 0 && p.outline.length > 3) { App.removeVertex(p, S.selV); S.selV = -1; }
    else App.removePiece(p.id);
    App.changed('structure');
  }
};

// drawing ---------------------------------------------------------------
Ed.draw = () => {
  if (!Ed.dirty) return; Ed.dirty = false;
  const S = App.S, c = Ed.ctx, d = devicePixelRatio || 1; if (!S) return;
  c.setTransform(d, 0, 0, d, 0, 0); c.clearRect(0, 0, Ed.W, Ed.H);
  // grid: 10 cm lines, 50 cm heavier
  c.lineWidth = 1;
  const [wx0, wy1] = Ed.toWorld(0, 0), [wx1, wy0] = Ed.toWorld(Ed.W, Ed.H), step = Ed.Z > 120 ? 0.1 : 0.5;
  for (let x = Math.floor(wx0 / step) * step; x <= wx1; x += step) { const sx = Ed.toScreen(x, 0)[0]; c.strokeStyle = Math.abs(x % 0.5) < 1e-6 || Math.abs(x % 0.5 - 0.5) < 1e-6 ? '#232a35' : '#1a1f27'; c.beginPath(); c.moveTo(sx, 0); c.lineTo(sx, Ed.H); c.stroke(); }
  for (let y = Math.floor(wy0 / step) * step; y <= wy1; y += step) { const sy = Ed.toScreen(0, y)[1]; c.strokeStyle = Math.abs(y % 0.5) < 1e-6 || Math.abs(y % 0.5 - 0.5) < 1e-6 ? '#232a35' : '#1a1f27'; c.beginPath(); c.moveTo(0, sy); c.lineTo(Ed.W, sy); c.stroke(); }
  const val = App.validPieces();
  const fcol = FABRIC_COLORS[S.fabric] || '#9fb7d6';
  c.font = '11px ui-monospace,Menlo,monospace'; c.textAlign = 'center'; c.textBaseline = 'middle';
  // seams (behind pieces' labels): link lines between matched edge midpoints
  S.seams.forEach((s, si) => {
    const A = App.piece(s.a[0]), B = App.piece(s.b[0]); if (!A || !B) return;
    const col = SEAM_COLORS[si % SEAM_COLORS.length], hot = S.hotSeam === si || S.sewFlash === si;
    const ends = (P, e) => { const o = P.outline, a = o[e], b = o[(e + 1) % o.length]; return [[a[0] + P.table.x, a[1] + P.table.y], [b[0] + P.table.x, b[1] + P.table.y]]; };
    const ea = ends(A, s.a[1]), eb = ends(B, s.b[1]);
    c.lineWidth = hot ? 5 : 3.5; c.strokeStyle = col; c.globalAlpha = hot ? 1 : 0.9;
    for (const e of [ea, eb]) { const p0 = Ed.toScreen(...e[0]), p1 = Ed.toScreen(...e[1]); c.beginPath(); c.moveTo(...p0); c.lineTo(...p1); c.stroke(); }
    c.globalAlpha = 1; c.lineWidth = 1.5; c.setLineDash([5, 5]); c.strokeStyle = col;
    // matched ticks: point k on A <-> point on B (reverse flips direction)
    const K = 5;
    for (let k = 0; k <= K; k++) {
      const t = k / K, pa = [ea[0][0] + (ea[1][0] - ea[0][0]) * t, ea[0][1] + (ea[1][1] - ea[0][1]) * t], tb = s.reverse ? 1 - t : t;
      const pb = [eb[0][0] + (eb[1][0] - eb[0][0]) * tb, eb[0][1] + (eb[1][1] - eb[0][1]) * tb];
      c.globalAlpha = hot ? 0.9 : 0.4; c.beginPath(); c.moveTo(...Ed.toScreen(...pa)); c.lineTo(...Ed.toScreen(...pb)); c.stroke();
    }
    c.setLineDash([]); c.globalAlpha = 1;
  });
  for (const p of S.pieces) {
    const o = p.outline, isSel = S.sel === p.id, bad = val.bad[p.id], hov = Ed.hover && Ed.hover.p === p.id;
    c.beginPath(); o.forEach((q, i) => { const [x, y] = Ed.toScreen(q[0] + p.table.x, q[1] + p.table.y); i ? c.lineTo(x, y) : c.moveTo(x, y); }); c.closePath();
    c.fillStyle = bad ? '#5a2328cc' : (isSel ? '#ece6d6' : '#d9d1bd'); c.globalAlpha = 0.93; c.fill(); c.globalAlpha = 1;
    // fabric tint + grain hatch
    c.save(); c.clip(); c.globalAlpha = 0.22; c.fillStyle = fcol; c.fill();
    c.globalAlpha = 0.13; c.strokeStyle = '#000'; c.lineWidth = 1;
    const bb = FL.bbox(o), g = p.grain, gx = Math.cos(g), gy = Math.sin(g), nx = -gy, ny = gx, sp = 0.02;
    const cx = (bb.x0 + bb.x1) / 2 + p.table.x, cy = (bb.y0 + bb.y1) / 2 + p.table.y, R = Math.hypot(bb.x1 - bb.x0, bb.y1 - bb.y0);
    for (let k = -R / sp; k <= R / sp; k++) { const ox = cx + nx * k * sp, oy = cy + ny * k * sp; c.beginPath(); c.moveTo(...Ed.toScreen(ox - gx * R, oy - gy * R)); c.lineTo(...Ed.toScreen(ox + gx * R, oy + gy * R)); c.stroke(); }
    c.restore();
    c.lineWidth = isSel ? 2.5 : 1.6; c.strokeStyle = bad ? '#ff6b6b' : (isSel ? '#ff9d4d' : (hov ? '#8a8268' : '#6e674f')); c.stroke();
    // grain arrow
    const gc = FL.centroid(o), a0 = Ed.toScreen(gc[0] + p.table.x - gx * 0.07, gc[1] + p.table.y - gy * 0.07), a1 = Ed.toScreen(gc[0] + p.table.x + gx * 0.07, gc[1] + p.table.y + gy * 0.07);
    c.strokeStyle = '#2b2b2b'; c.lineWidth = 1.5; c.beginPath(); c.moveTo(...a0); c.lineTo(...a1); c.stroke();
    const ang = Math.atan2(a1[1] - a0[1], a1[0] - a0[0]); c.fillStyle = '#2b2b2b'; c.beginPath(); c.moveTo(...a1); c.lineTo(a1[0] - 8 * Math.cos(ang - 0.4), a1[1] - 8 * Math.sin(ang - 0.4)); c.lineTo(a1[0] - 8 * Math.cos(ang + 0.4), a1[1] - 8 * Math.sin(ang + 0.4)); c.fill();
    c.fillStyle = '#3a3426'; c.fillText(p.name, ...Ed.toScreen(gc[0] + p.table.x, gc[1] + p.table.y - 0.04 - 0.0));
    if (bad) { c.fillStyle = '#ffd0d0'; c.fillText(bad, ...Ed.toScreen(gc[0] + p.table.x, gc[1] + p.table.y + 0.06)); }
    // edge labels: index + length (cm) outside the edge
    for (let i = 0; i < o.length; i++) {
      const a = o[i], b = o[(i + 1) % o.length], mx = (a[0] + b[0]) / 2 + p.table.x, my = (a[1] + b[1]) / 2 + p.table.y;
      const L = FL.edgeLen(o, i), [px, py] = Ed.toScreen(mx, my), ex = (b[0] - a[0]) / L, ey = (b[1] - a[1]) / L;
      const hotEdge = S.tool === 'sew' && ((Ed.hover && Ed.hover.p === p.id && Ed.hover.e === i) || (S.sewA && S.sewA[0] === p.id && S.sewA[1] === i));
      if (hotEdge) { c.strokeStyle = '#ff9d4d'; c.lineWidth = 5; c.beginPath(); c.moveTo(...Ed.toScreen(a[0] + p.table.x, a[1] + p.table.y)); c.lineTo(...Ed.toScreen(b[0] + p.table.x, b[1] + p.table.y)); c.stroke(); }
      if (isSel || S.tool === 'sew' || Ed.Z > 200) { c.fillStyle = isSel ? '#ffb877' : '#8f98a6'; const off = 15; c.fillText(`${i}: ${(L * 100).toFixed(1)}`, px + ey * off, py + ex * off); }
    }
    if (isSel) for (let i = 0; i < o.length; i++) { const [x, y] = Ed.toScreen(o[i][0] + p.table.x, o[i][1] + p.table.y); c.fillStyle = S.selV === i ? '#ff9d4d' : '#14171c'; c.strokeStyle = '#ff9d4d'; c.lineWidth = 2; c.beginPath(); c.rect(x - 5, y - 5, 10, 10); c.fill(); c.stroke(); }
  }
  // scale bar
  const bx = 14, by = Ed.H - 18, len = 0.1 * Ed.Z; c.strokeStyle = '#8f98a6'; c.lineWidth = 2; c.beginPath(); c.moveTo(bx, by); c.lineTo(bx + len, by); c.stroke(); c.fillStyle = '#8f98a6'; c.textAlign = 'left'; c.fillText('10 cm', bx + len + 6, by);
};
