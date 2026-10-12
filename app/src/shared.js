// Shared by the UI thread and the simulation worker: body model, pattern geometry, wrapping onto the body.
'use strict';
const FL = {};
FL.PI = Math.PI;

// ---------------------------------------------------------------- polygons
FL.signedArea = (p) => { let a = 0; for (let i = 0; i < p.length; i++) { const q = p[(i + 1) % p.length]; a += p[i][0] * q[1] - q[0] * p[i][1]; } return a / 2; };
FL.bbox = (p) => { let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9; for (const [x, y] of p) { x0 = Math.min(x0, x); x1 = Math.max(x1, x); y0 = Math.min(y0, y); y1 = Math.max(y1, y); } return { x0, y0, x1, y1 }; };
FL.edgeLen = (p, i) => { const a = p[i], b = p[(i + 1) % p.length]; return Math.hypot(b[0] - a[0], b[1] - a[1]); };
FL.centroid = (p) => { let x = 0, y = 0; for (const q of p) { x += q[0]; y += q[1]; } return [x / p.length, y / p.length]; };
// proper (non-adjacent) edge intersections => invalid polygon
FL.selfIntersects = (p) => {
  const n = p.length;
  const ccw = (a, b, c) => (c[1] - a[1]) * (b[0] - a[0]) - (b[1] - a[1]) * (c[0] - a[0]);
  for (let i = 0; i < n; i++) for (let j = i + 2; j < n; j++) {
    if (i === 0 && j === n - 1) continue;
    const a = p[i], b = p[(i + 1) % n], c = p[j], d = p[(j + 1) % n];
    if (ccw(a, c, d) * ccw(b, c, d) < 0 && ccw(a, b, c) * ccw(a, b, d) < 0) return true;
  }
  return false;
};

// Andrew's monotone chain over flat [x0,y0,x1,y1,...] points (any array type). Returns a flat counter-clockwise hull.
FL.hull2 = (pts, n) => {
  if (n === undefined) n = pts.length >> 1;
  if (n < 3) return Float64Array.from(Array.prototype.slice.call(pts, 0, 2 * n));
  const idx = new Uint32Array(n); for (let i = 0; i < n; i++) idx[i] = i;
  idx.sort((a, b) => (pts[2 * a] - pts[2 * b]) || (pts[2 * a + 1] - pts[2 * b + 1]));
  const H = new Uint32Array(2 * n + 2); let k = 0;
  const cr = (o, a, b) => (pts[2 * a] - pts[2 * o]) * (pts[2 * b + 1] - pts[2 * o + 1]) - (pts[2 * a + 1] - pts[2 * o + 1]) * (pts[2 * b] - pts[2 * o]);
  for (let i = 0; i < n; i++) { while (k >= 2 && cr(H[k - 2], H[k - 1], idx[i]) <= 0) k--; H[k++] = idx[i]; }
  for (let i = n - 2, t = k + 1; i >= 0; i--) { while (k >= t && cr(H[k - 2], H[k - 1], idx[i]) <= 0) k--; H[k++] = idx[i]; }
  const out = new Float64Array(2 * (k - 1));
  for (let i = 0; i < k - 1; i++) { out[2 * i] = pts[2 * H[i]]; out[2 * i + 1] = pts[2 * H[i] + 1]; }
  return out;
};
FL.perimeter2 = (h) => { let L = 0; const n = h.length >> 1; for (let i = 0; i < n; i++) { const j = (i + 1) % n; L += Math.hypot(h[2 * j] - h[2 * i], h[2 * j + 1] - h[2 * i + 1]); } return L; };

// ---------------------------------------------------------------- body
// Body presets: tape measurements in metres (bust/waist/hip girths, shoulder = straight distance between the two
// shoulder points), and body type: muscle 0..1 (0.5 average), cup -1..1, belly -1..1. The secondary measurements
// (arm, inseam, neck, thigh, upperarm, back) are 'auto' (whatever the body type gives) unless set.
// See app/src/body.js and docs/BODY_DESIGN.md.
FL.PRESETS = {
  average:  { height: 1.68, bust: 0.92, waist: 0.74, hip: 0.98, shoulder: 0.37, muscle: 0.5, cup: 0 },
  petite:   { height: 1.55, bust: 0.84, waist: 0.66, hip: 0.90, shoulder: 0.35, muscle: 0.45, cup: -0.1 },
  tall:     { height: 1.80, bust: 0.95, waist: 0.78, hip: 1.00, shoulder: 0.39, muscle: 0.5, cup: 0 },
  curvy:    { height: 1.66, bust: 1.04, waist: 0.80, hip: 1.12, shoulder: 0.37, muscle: 0.45, cup: 0.5 },
  athletic: { height: 1.75, bust: 0.96, waist: 0.78, hip: 0.96, shoulder: 0.40, muscle: 0.85, cup: -0.3, belly: -0.4 },
  plus:     { height: 1.68, bust: 1.18, waist: 1.02, hip: 1.24, shoulder: 0.39, muscle: 0.4, cup: 0.3 },
};
// semi-axis a (x) of an ellipse with depth ratio k = b/a and perimeter C (Ramanujan)
FL.ellipseAxes = (C, k) => { const per = Math.PI * (3 * (1 + k) - Math.sqrt((3 + k) * (1 + 3 * k))); const a = C / per; return [a, a * k]; };
FL.lerp = (a, b, t) => a + (b - a) * t;

// ---- legacy primitive mannequin (about 45 ellipsoids and capsules). Kept as a fallback (no body asset) and for the
// walk-cycle experiment, which needs a moving collider; the default body is the mesh from FL.Body.
// Torso rings (ellipsoids), shared by physics and display.
FL.torsoRings = (m) => {
  const H = m.height, lp = FL.lerp;
  const rings = [
    [0.470, m.hip * 0.95, 0.80], [0.520, m.hip, 0.78], [0.570, lp(m.hip, m.waist, 0.5), 0.74], [0.620, m.waist, 0.70],
    [0.670, lp(m.waist, m.bust, 0.55), 0.71], [0.715, m.bust, 0.74], [0.765, m.bust * 0.97, 0.72],
  ];
  // five interpolated rings between each pair of specified rings keep the silhouette smooth (no ridges between ellipsoids)
  const dense = [];
  for (let i = 0; i < rings.length - 1; i++) for (let j = 0; j < 6; j++) { const t = j / 6; dense.push([lp(rings[i][0], rings[i + 1][0], t), lp(rings[i][1], rings[i + 1][1], t), lp(rings[i][2], rings[i + 1][2], t)]); }
  dense.push(rings[rings.length - 1]);
  const out = dense.map(([f, g, k]) => { const [a, b] = FL.ellipseAxes(g, k); return { c: [0, f * H, 0], r: [a, 0.078 * H, b] }; });
  out.push({ c: [0, 0.815 * H, 0], r: [m.shoulder / 2 + 0.01, 0.045 * H, 0.075 * H] });  // shoulder line
  return out;
};

// Primitives for a pose. phase in [0,1) is the walk cycle; phase = null gives the standing A-pose.
FL.bodyPrims = (m, phase) => {
  const H = m.height, s = H / 1.68, P = [];
  const walk = phase !== null && phase !== undefined;
  const ph = walk ? 2 * Math.PI * phase : 0;
  const bob = walk ? 0.012 * Math.cos(2 * ph) : 0, sway = walk ? 0.012 * Math.sin(ph) : 0;
  for (const e of FL.torsoRings(m)) P.push({ t: 'e', c: [e.c[0] + sway, e.c[1] + bob, e.c[2]], r: e.r });
  P.push({ t: 'e', c: [sway, 0.925 * H + bob, 0.01 * s], r: [0.05 * H, 0.075 * H, 0.06 * H] });             // head
  P.push({ t: 'c', a: [sway, 0.835 * H + bob, 0], b: [sway, 0.885 * H + bob, 0.005], r: 0.052 * s });         // neck
  const hipR = 0.58 * m.hip / (2 * Math.PI), calfR = 0.05 * s;
  for (const side of [-1, 1]) {
    const q = walk ? ph + (side > 0 ? 0 : Math.PI) : 0;
    const swing = walk ? 0.40 * Math.sin(q) : 0.0;                   // thigh angle, forward positive
    const knee = walk ? 0.75 * Math.max(0, -Math.sin(q + 0.6)) + 0.05 : 0.0;
    const hx = side * 0.095 * s + sway, hy = 0.50 * H + bob;
    const L1 = 0.215 * H, L2 = 0.235 * H;
    const k = [hx - side * 0.005 * s, hy - L1 * Math.cos(swing), Math.sin(swing) * L1];
    const a2 = swing - knee;
    const an = [k[0], k[1] - L2 * Math.cos(a2), k[2] + Math.sin(a2) * L2];
    P.push({ t: 'c', a: [hx, hy, 0], b: k, r: hipR });
    P.push({ t: 'c', a: k, b: an, r: calfR });
    const foot = walk ? Math.max(-0.3, Math.min(0.5, -a2 * 0.5)) : 0;
    P.push({ t: 'c', a: [an[0], an[1] + 0.005, an[2]], b: [an[0], Math.max(0.025 * s, an[1] - 0.012 * Math.cos(foot)), an[2] + 0.14 * s * Math.cos(foot)], r: 0.04 * s });
    // arm: opposite phase to the same-side leg
    const sh = [side * (m.shoulder / 2 - 0.01) + sway, 0.80 * H + bob, 0];
    const arm = walk ? -0.35 * Math.sin(q) : 0, ab = 0.24;           // swing + outward A-pose
    const U = 0.19 * H, F = 0.15 * H, elb = walk ? 0.25 + 0.25 * Math.max(0, Math.sin(q)) : 0.12;
    const e = [sh[0] + side * Math.sin(ab) * U, sh[1] - Math.cos(ab) * Math.cos(arm) * U, Math.sin(arm) * U];
    const w = [e[0] + side * Math.sin(ab) * F * 0.8, e[1] - Math.cos(ab) * Math.cos(arm + elb) * F, e[2] + Math.sin(arm + elb) * F];
    P.push({ t: 'c', a: sh, b: e, r: 0.04 * s });
    P.push({ t: 'c', a: e, b: w, r: 0.031 * s });
  }
  return P;
};

// Flat records for fl_set_colliders: [type, 7 numbers] (0 sphere, 1 capsule, 2 ellipsoid)
FL.collidersFlat = (prims) => {
  const out = new Float64Array(prims.length * 8);
  prims.forEach((p, i) => {
    const o = i * 8;
    if (p.t === 'e') { out[o] = 2; out.set(p.c, o + 1); out.set(p.r, o + 4); }
    else if (p.t === 'p') { out[o] = 3; out.set(p.p, o + 1); out.set(p.n, o + 4); }
    else if (p.t === 'c') { out[o] = 1; out.set(p.a, o + 1); out.set(p.b, o + 4); out[o + 7] = p.r; }
    else { out[o] = 0; out.set(p.c, o + 1); out[o + 4] = p.r; }
  });
  return out;
};

// Half-widths (x, z) of the standing body at height y, arms excluded (garments wrap the trunk). `body` is a generated
// body (uses its section table) or, for the legacy primitive mannequin, the measurements.
FL.profile = (body, y) => {
  if (body && body.sections) {
    const S = body.sections, h = S.hulls[Math.max(0, Math.min(S.ny - 1, Math.round(y / S.dy)))];
    let ax = 0.06, z0 = 1e9, z1 = -1e9;
    for (let i = 0; i < h.length; i += 2) { ax = Math.max(ax, Math.abs(h[i])); z0 = Math.min(z0, h[i + 1]); z1 = Math.max(z1, h[i + 1]); }
    return [ax, h.length ? Math.max(0.05, (z1 - z0) / 2) : 0.05];
  }
  const m = body;
  let ax = 0.06, az = 0.05;
  for (const e of FL.torsoRings(m)) {
    const t = (y - e.c[1]) / e.r[1];
    if (Math.abs(t) < 1) { const sc = Math.sqrt(1 - t * t); ax = Math.max(ax, e.r[0] * sc); az = Math.max(az, e.r[2] * sc); }
  }
  if (y < 0.52 * m.height) {                       // legs: they widen the section below the crotch
    const s = m.height / 1.68, lx = 0.095 * s + 0.58 * m.hip / (2 * Math.PI);
    ax = Math.max(ax, lx); az = Math.max(az, 0.58 * m.hip / (2 * Math.PI));
  }
  return [ax, az];
};

// ---------------------------------------------------------------- wrapping a flat piece around the body
// Starting pose of a piece: wrapped round the convex envelope of the body sections it spans (arms and hands
// excluded), plus wrap.clear, centred at wrap.theta (0 = front, 90 = the body's left side). Arc length along the
// envelope equals u, so the piece is not stretched. `body` is a generated body (FL.Body.generate: uses
// body.sections) or, for the legacy primitive mannequin, the measurements (ellipse wrap).
FL.makeWrap = (piece, body) => {
  if (!body || !body.sections) return FL.makeWrapEllipse(piece, body);
  const S = body.sections, w = piece.wrap, bb = FL.bbox(piece.outline), uc = (bb.x0 + bb.x1) / 2;
  const h0 = w.y, h1 = w.y + (bb.y1 - bb.y0);
  const k0 = Math.max(0, Math.floor(h0 / S.dy)), k1 = Math.min(S.ny - 1, Math.ceil(h1 / S.dy));
  const pts = [];
  for (let k = k0; k <= k1; k++) { const h = S.hulls[k]; for (let i = 0; i < h.length; i++) pts.push(h[i]); }
  if (pts.length < 6) for (let a = 0; a < 16; a++) pts.push(0.05 * Math.sin(a * Math.PI / 8), 0.05 * Math.cos(a * Math.PI / 8));
  const hull = FL.hull2(pts), n = hull.length >> 1;
  // centre on the body's symmetry plane, front-back at the hull's area centroid
  let A = 0, cz = 0;
  for (let i = 0; i < n; i++) { const j = (i + 1) % n, cr = hull[2 * i] * hull[2 * j + 1] - hull[2 * j] * hull[2 * i + 1]; A += cr; cz += (hull[2 * i + 1] + hull[2 * j + 1]) * cr; }
  cz = Math.abs(A) > 1e-12 ? cz / (3 * A) : 0;
  // polar radius of the hull around (0, cz), phi measured from +z (front) towards +x, plus the clearance
  const NA = 360, R = new Float64Array(NA + 1);
  for (let a = 0; a < NA; a++) {
    const p = 2 * Math.PI * a / NA, dx = Math.sin(p), dz = Math.cos(p); let best = 0.02;
    for (let i = 0; i < n; i++) {
      const j = (i + 1) % n, px = hull[2 * i], pz = hull[2 * i + 1] - cz, ex = hull[2 * j] - hull[2 * i], ez = hull[2 * j + 1] - hull[2 * i + 1];
      const den = dx * ez - dz * ex; if (Math.abs(den) < 1e-12) continue;
      const t = (px * ez - pz * ex) / den, s = (px * dz - pz * dx) / den;
      if (t > best && s >= -1e-9 && s <= 1 + 1e-9) best = t;
    }
    R[a] = best + w.clear;
  }
  R[NA] = R[0];
  const rad = (phi) => { const f = (((phi / (2 * Math.PI)) % 1) + 1) % 1 * NA, i = Math.min(NA - 1, Math.floor(f)); return R[i] + (R[i + 1] - R[i]) * (f - i); };
  const curve = (phi) => { const r = rad(phi); return [r * Math.sin(phi), cz + r * Math.cos(phi)]; };
  // arc-length table over phi in [0, 2pi]
  const N = 720, ST = new Float64Array(N + 1);
  let prev = curve(0);
  for (let i = 1; i <= N; i++) { const c = curve(2 * Math.PI * i / N); ST[i] = ST[i - 1] + Math.hypot(c[0] - prev[0], c[1] - prev[1]); prev = c; }
  const per = ST[N], phi0 = w.theta * Math.PI / 180;
  const sOf = (phi) => { const f = (((phi / (2 * Math.PI)) % 1) + 1) % 1 * N, i = Math.min(N - 1, Math.floor(f)); return ST[i] + (ST[i + 1] - ST[i]) * (f - i); };
  const phiOf = (s) => { s = ((s % per) + per) % per; let lo = 0, hi = N; while (hi - lo > 1) { const mid = (lo + hi) >> 1; if (ST[mid] <= s) lo = mid; else hi = mid; } return (lo + (s - ST[lo]) / (ST[lo + 1] - ST[lo])) / N * 2 * Math.PI; };
  const sc = sOf(phi0);
  const rowWidth = (v) => { let lo = 1e9, hi = -1e9; const o = piece.outline;
    for (let i = 0; i < o.length; i++) { const a = o[i], b = o[(i + 1) % o.length]; if ((a[1] - v) * (b[1] - v) <= 0 && a[1] !== b[1]) { const x = a[0] + (b[0] - a[0]) * (v - a[1]) / (b[1] - a[1]); lo = Math.min(lo, x); hi = Math.max(hi, x); } }
    return hi > lo ? hi - lo : 0; };
  const ring = w.ring || 0;
  const flare = (v) => (ring > 0 ? Math.max(1, ring * rowWidth(v) / per) : 1);
  const c0 = curve(phi0), ca = curve(phi0 - 1e-3), cb = curve(phi0 + 1e-3);
  let tx = cb[0] - ca[0], tz = cb[1] - ca[1]; const tl = Math.hypot(tx, tz) || 1; tx /= tl; tz /= tl;
  let ax = 0, az = 0; for (let a = 0; a < NA; a++) { const c = curve(2 * Math.PI * a / NA); ax = Math.max(ax, Math.abs(c[0])); az = Math.max(az, Math.abs(c[1] - cz)); }
  return {
    ax, az, cz, per, uc, vmin: bb.y0,
    point(u, v) { const f = flare(v), p = phiOf(sc + (u - uc) / f), r = rad(p) * f; return [r * Math.sin(p), w.y + (v - bb.y0), cz + r * Math.cos(p)]; },
    // exploded pose: upright and flat, tangent to the envelope at the piece centre, pushed outward by `d`
    flatPoint(u, v, d) { return [c0[0] - tz * d + tx * (u - uc), w.y + (v - bb.y0), c0[1] + tx * d + tz * (u - uc)]; },
  };
};

// Legacy wrap around the primitive mannequin: an ellipse sized from the widest section the piece spans.
FL.makeWrapEllipse = (piece, m) => {
  const w = piece.wrap, bb = FL.bbox(piece.outline), uc = (bb.x0 + bb.x1) / 2;
  const h0 = w.y, h1 = w.y + (bb.y1 - bb.y0);
  let ax = 0.05, az = 0.04;
  for (let k = 0; k <= 12; k++) { const [a, b] = FL.profile(m, h0 + (h1 - h0) * k / 12); ax = Math.max(ax, a); az = Math.max(az, b); }
  ax += w.clear; az += w.clear;
  // arc-length table over phi in [0, 2pi]  (x = ax sin phi, z = az cos phi)
  const N = 720, S = new Float64Array(N + 1);
  for (let i = 1; i <= N; i++) { const p = (i - 0.5) / N * 2 * Math.PI; S[i] = S[i - 1] + 2 * Math.PI / N * Math.hypot(ax * Math.cos(p), az * Math.sin(p)); }
  const per = S[N], phi0 = w.theta * Math.PI / 180;
  const sOf = (phi) => { const f = ((phi % (2 * Math.PI)) + 2 * Math.PI) % (2 * Math.PI) / (2 * Math.PI) * N, i = Math.min(N - 1, Math.floor(f)); return S[i] + (S[i + 1] - S[i]) * (f - i); };
  const phiOf = (s) => { s = ((s % per) + per) % per; let lo = 0, hi = N; while (hi - lo > 1) { const mid = (lo + hi) >> 1; if (S[mid] <= s) lo = mid; else hi = mid; } return (lo + (s - S[lo]) / (S[lo + 1] - S[lo])) / N * 2 * Math.PI; };
  const sc = sOf(phi0);
  // width of the piece at row v (scanline), used to flare the ring for pieces that are wider than the body (A-line gores)
  const rowWidth = (v) => { let lo = 1e9, hi = -1e9; const o = piece.outline;
    for (let i = 0; i < o.length; i++) { const a = o[i], b = o[(i + 1) % o.length]; if ((a[1] - v) * (b[1] - v) <= 0 && a[1] !== b[1]) { const x = a[0] + (b[0] - a[0]) * (v - a[1]) / (b[1] - a[1]); lo = Math.min(lo, x); hi = Math.max(hi, x); } }
    return hi > lo ? hi - lo : 0; };
  const ring = w.ring || 0;
  const flare = (v) => (ring > 0 ? Math.max(1, ring * rowWidth(v) / per) : 1);
  return {
    ax, az, cz: 0, per, uc, vmin: bb.y0,
    point(u, v) { const f = flare(v), p = phiOf(sc + (u - uc) / f); return [ax * f * Math.sin(p), w.y + (v - bb.y0), az * f * Math.cos(p)]; },
    // exploded pose: upright and flat, tangent to the ellipse at the piece centre, pushed outward by `d`
    flatPoint(u, v, d) {
      const x = ax * Math.sin(phi0), z = az * Math.cos(phi0);
      let nx = Math.sin(phi0) / ax, nz = Math.cos(phi0) / az; const nl = Math.hypot(nx, nz); nx /= nl; nz /= nl;
      return [x + nx * d + nz * (u - uc), w.y + (v - bb.y0), z + nz * d - nx * (u - uc)];
    },
  };
};

// ---------------------------------------------------------------- cloth display mesh
// One level of subdivision for display: every edge gets a midpoint vertex (n + e), every triangle becomes four.
// The worker places the midpoints so that cloth resting on the body follows it (fl_display_positions).
FL.subdivide = (tris, n) => {
  const key = new Map(), edges = [], out = new Int32Array(tris.length * 4);
  const mid = (a, b) => { const k = a < b ? a * n + b : b * n + a; let e = key.get(k); if (e === undefined) { e = edges.length / 2; key.set(k, e); edges.push(a, b); } return n + e; };
  for (let t = 0, o = 0; t < tris.length; t += 3) {
    const a = tris[t], b = tris[t + 1], c = tris[t + 2], ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
    out.set([a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca], o); o += 12;
  }
  return { edges: Int32Array.from(edges), tris: out };
};
// Display-mesh positions from the positions of the n cloth vertices (pos, 3 floats each): the vertices, then the
// plain midpoint of every edge. Writes into `out` (length 3 * (n + edges/2)), which may be `pos` itself.
FL.withMidpoints = (pos, n, edges, out) => {
  const ne = edges.length / 2;
  out = out || new Float32Array((n + ne) * 3);
  if (out !== pos) for (let i = 0; i < 3 * n; i++) out[i] = pos[i];
  for (let e = 0; e < ne; e++) { const i = 3 * edges[2 * e], j = 3 * edges[2 * e + 1], o = 3 * (n + e); out[o] = 0.5 * (out[i] + out[j]); out[o + 1] = 0.5 * (out[i + 1] + out[j + 1]); out[o + 2] = 0.5 * (out[i + 2] + out[j + 2]); }
  return out;
};

// ---------------------------------------------------------------- pattern helpers
FL.normalizePiece = (p) => {
  if (FL.signedArea(p.outline) < 0) p.outline = p.outline.slice().reverse();   // outlines are counter-clockwise
  p.grain = p.grain || 0;
  p.table = p.table || { x: 0, y: 0 };
  p.wrap = Object.assign({ theta: 0, y: 0.5, clear: 0.012 }, p.wrap || {});
  return p;
};
FL.fabricById = (id) => FL.FABRICS.find((f) => f.id === id) || FL.FABRICS[0];
if (typeof module !== 'undefined') module.exports = FL;
