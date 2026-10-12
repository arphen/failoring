// Parametric mannequin: the CC0 MakeHuman base mesh (female young adult) plus blend shapes ("targets"), solved
// so that tape measurements match the Body tab. Runs in the UI thread and in node (tools/verify_body.js).
// Asset: app/assets/body_mh.bin, written by tools/convert_makehuman.py. Design: docs/BODY_DESIGN.md.
// All of this file is Failoring's own code; only the mesh and target data come from MakeHuman (CC0).
'use strict';
(function (FL) {
  const B = {};
  FL.Body = B;
  const now = () => (typeof performance !== 'undefined' ? performance.now() : Date.now());

  // ---------------------------------------------------------------- raw DEFLATE decoder (RFC 1951)
  const LBASE = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258];
  const LEXT = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0];
  const DBASE = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577];
  const DEXT = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13];
  const ORDER = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15];
  B.inflateRaw = (src) => {
    let out = new Uint8Array(Math.max(1 << 16, src.length * 6)), op = 0, pos = 0, bb = 0, bc = 0;
    const fail = (m) => { throw new Error('inflate: ' + m); };
    const need = (n) => { if (op + n > out.length) { let L = out.length * 2; while (L < op + n) L *= 2; const o = new Uint8Array(L); o.set(out.subarray(0, op)); out = o; } };
    const bits = (n) => {
      while (bc < n) { if (pos >= src.length) fail('truncated input'); bb |= src[pos++] << bc; bc += 8; }
      const v = bb & ((1 << n) - 1); bb >>>= n; bc -= n; return v;
    };
    const build = (lens, n) => {   // canonical Huffman code: symbols sorted by code length
      const count = new Uint16Array(16), offs = new Uint16Array(16), sym = new Uint16Array(n);
      for (let i = 0; i < n; i++) count[lens[i]]++;
      count[0] = 0;
      for (let l = 1; l < 15; l++) offs[l + 1] = offs[l] + count[l];
      for (let i = 0; i < n; i++) if (lens[i]) sym[offs[lens[i]]++] = i;
      return { count, sym };
    };
    const decode = (h) => {        // codes are read most significant bit first, one bit at a time
      let code = 0, first = 0, index = 0;
      for (let l = 1; l < 16; l++) {
        if (bc === 0) { if (pos >= src.length) fail('truncated input'); bb = src[pos++]; bc = 8; }
        code |= bb & 1; bb >>>= 1; bc--;
        const c = h.count[l];
        if (code - c < first) return h.sym[index + (code - first)];
        index += c; first += c; first <<= 1; code <<= 1;
      }
      return fail('invalid code');
    };
    const codes = (lc, dc) => {
      for (;;) {
        let s = decode(lc);
        if (s < 256) { need(1); out[op++] = s; continue; }
        if (s === 256) return;
        s -= 257; if (s >= 29) fail('invalid length symbol');
        const len = LBASE[s] + bits(LEXT[s]);
        const ds = decode(dc); if (ds >= 30) fail('invalid distance symbol');
        const dist = DBASE[ds] + bits(DEXT[ds]);
        if (dist > op) fail('distance too far back');
        need(len);
        for (let k = 0; k < len; k++, op++) out[op] = out[op - dist];
      }
    };
    let fixedL = null, fixedD = null, last = 0;
    do {
      last = bits(1);
      const type = bits(2);
      if (type === 0) {                       // stored block
        bb = 0; bc = 0;
        if (pos + 4 > src.length) fail('truncated input');
        const len = src[pos] | (src[pos + 1] << 8), nlen = src[pos + 2] | (src[pos + 3] << 8); pos += 4;
        if (len !== (~nlen & 0xffff)) fail('stored length mismatch');
        if (pos + len > src.length) fail('truncated input');
        need(len); out.set(src.subarray(pos, pos + len), op); op += len; pos += len;
      } else if (type === 1) {                // fixed Huffman codes
        if (!fixedL) { const l = new Uint8Array(288); l.fill(8, 0, 144); l.fill(9, 144, 256); l.fill(7, 256, 280); l.fill(8, 280, 288); fixedL = build(l, 288); fixedD = build(new Uint8Array(30).fill(5), 30); }
        codes(fixedL, fixedD);
      } else if (type === 2) {                // dynamic Huffman codes
        const nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
        if (nlen > 286 || ndist > 30) fail('bad counts');
        const cl = new Uint8Array(19);
        for (let i = 0; i < ncode; i++) cl[ORDER[i]] = bits(3);
        const lc = build(cl, 19), all = new Uint8Array(nlen + ndist);
        for (let i = 0; i < nlen + ndist;) {
          const s = decode(lc);
          if (s < 16) { all[i++] = s; continue; }
          let v = 0, rep;
          if (s === 16) { if (!i) fail('repeat without length'); v = all[i - 1]; rep = 3 + bits(2); } else if (s === 17) rep = 3 + bits(3); else rep = 11 + bits(7);
          if (i + rep > nlen + ndist) fail('too many lengths');
          all.fill(v, i, i + rep); i += rep;
        }
        codes(build(all.subarray(0, nlen), nlen), build(all.subarray(nlen), ndist));
      } else fail('invalid block type');
    } while (!last);
    return out.slice(0, op);
  };

  // ---------------------------------------------------------------- small geometry helpers
  B.hull2 = FL.hull2;           // convex hull and perimeter of flat 2D point lists (shared.js)
  B.perimeter2 = FL.perimeter2;

  // Points where plane n.p = d cuts the triangles `list` of mesh (P, tris), in the plane basis (e1, e2).
  // Optional filter: keep points within `rmax` of point c. Returns the number of points written into B._pts.
  let PTS = new Float64Array(8192);
  const slice = (P, tris, list, n, d, e1, e2, c, rmax) => {
    let k = 0;
    const nx = n[0], ny = n[1], nz = n[2], r2 = rmax ? rmax * rmax : 0;
    const emit = (a, b, sa, sb) => {
      const t = sa / (sa - sb), x = P[a] + (P[b] - P[a]) * t, y = P[a + 1] + (P[b + 1] - P[a + 1]) * t, z = P[a + 2] + (P[b + 2] - P[a + 2]) * t;
      if (r2 && (x - c[0]) ** 2 + (y - c[1]) ** 2 + (z - c[2]) ** 2 > r2) return;
      if (2 * k + 2 > PTS.length) { const o = new Float64Array(PTS.length * 2); o.set(PTS); PTS = o; }
      PTS[2 * k] = x * e1[0] + y * e1[1] + z * e1[2]; PTS[2 * k + 1] = x * e2[0] + y * e2[1] + z * e2[2]; k++;
    };
    for (let q = 0; q < list.length; q++) {
      const t = 3 * list[q], a = 3 * tris[t], b = 3 * tris[t + 1], cc = 3 * tris[t + 2];
      const sa = P[a] * nx + P[a + 1] * ny + P[a + 2] * nz - d, sb = P[b] * nx + P[b + 1] * ny + P[b + 2] * nz - d, sc = P[cc] * nx + P[cc + 1] * ny + P[cc + 2] * nz - d;
      if ((sa > 0) === (sb > 0) && (sb > 0) === (sc > 0)) continue;
      if ((sa > 0) !== (sb > 0)) emit(a, b, sa, sb);
      if ((sb > 0) !== (sc > 0)) emit(b, cc, sb, sc);
      if ((sc > 0) !== (sa > 0)) emit(cc, a, sc, sa);
    }
    return k;
  };
  const X = [1, 0, 0], Y = [0, 1, 0], Z = [0, 0, 1];
  const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  const crs = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const nrm = (a) => { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  const dist = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
  // tape-measure girth: convex hull perimeter of a plane section
  const girthPlane = (P, tris, list, n, d, e1, e2, c, rmax) => { const k = slice(P, tris, list, n, d, e1, e2, c, rmax); return k < 3 ? 0 : B.perimeter2(B.hull2(PTS, k)); };
  const girthY = (P, tris, list, y, keep) => {
    const k = slice(P, tris, list, Y, y, X, Z);
    if (!keep) return k < 3 ? 0 : B.perimeter2(B.hull2(PTS, k));
    let m = 0; for (let i = 0; i < k; i++) if (keep(PTS[2 * i], PTS[2 * i + 1])) { PTS[2 * m] = PTS[2 * i]; PTS[2 * m + 1] = PTS[2 * i + 1]; m++; }
    return m < 3 ? 0 : B.perimeter2(B.hull2(PTS, m));
  };

  // ---------------------------------------------------------------- asset
  B.load = (packed) => {
    const t0 = now();
    const raw = B.inflateRaw(packed instanceof Uint8Array ? packed : new Uint8Array(packed));
    let magic = ''; for (let i = 0; i < 8; i++) magic += String.fromCharCode(raw[i]);
    if (magic !== 'FLBODY01') throw new Error('body asset: bad magic ' + magic);
    const hlen = raw[8] | (raw[9] << 8) | (raw[10] << 16) | (raw[11] << 24);
    const H = JSON.parse(new TextDecoder().decode(raw.subarray(12, 12 + hlen)));
    const blob = raw.subarray(12 + hlen);
    const u16 = (off, n) => { const a = new Uint16Array(n); for (let i = 0; i < n; i++) a[i] = blob[off + i] | (blob[off + n + i] << 8); return a; };
    const undelta = (a) => { const r = new Uint32Array(a.length); let s = 0; for (let i = 0; i < a.length; i++) { s = (s + a[i]) & 0xffff; r[i] = s; } return r; };
    const nv = H.nv, nb = H.nb, nq = H.nq;
    const bq = new Int16Array(u16(H.base, 3 * nv).buffer), base = new Float32Array(3 * nv);
    for (let i = 0; i < nv; i++) for (let c = 0; c < 3; c++) base[3 * i + c] = bq[c * nv + i] * H.qBase;
    const qd = undelta(u16(H.quads, 4 * nq)), tris = new Uint32Array(6 * nq);
    for (let q = 0; q < nq; q++) {
      const a = qd[4 * q], b = qd[4 * q + 1], c = qd[4 * q + 2], d = qd[4 * q + 3], o = 6 * q;
      tris[o] = a; tris[o + 1] = b; tris[o + 2] = c; tris[o + 3] = a; tris[o + 4] = c; tris[o + 5] = d;
    }
    const part = blob.slice(H.part, H.part + nv);
    const targets = {};
    for (const t of H.targets) {
      const idx = undelta(u16(t.off, t.n)), dq = new Int16Array(u16(t.off + 2 * t.n, 3 * t.n).buffer), d = new Float32Array(3 * t.n);
      for (let i = 0; i < t.n; i++) for (let c = 0; c < 3; c++) d[3 * i + c] = dq[c * t.n + i] * H.qTarget;
      targets[t.name] = { idx, d, src: t.src };
    }
    const M = { header: H, nv, nb, nq, base, tris, part, parts: H.parts, targets, joints: H.joints, lm: H.landmarks, masks: H.masks, bytes: packed.length };
    // triangle subsets used by the tape measures and the wrap sections
    const triSet = (names) => {
      const ok = new Uint8Array(32); for (const s of names) ok[H.parts.indexOf(s)] = 1;
      const out = []; for (let t = 0; t < 2 * nq; t++) if (ok[part[tris[3 * t]]] && ok[part[tris[3 * t + 1]]] && ok[part[tris[3 * t + 2]]]) out.push(t);
      return Uint32Array.from(out);
    };
    M.sets = {
      trunk: triSet(['trunk']), hip: triSet(['trunk', 'leg.L', 'leg.R']), neck: triSet(['neck', 'trunk', 'head']), armL: triSet(['arm.L']),
      section: triSet(['trunk', 'neck', 'head', 'leg.L', 'leg.R', 'foot.L', 'foot.R']),
    };
    // finish (smoothing) masks with their vertex neighbourhoods
    const mv = new Map();
    for (const k of Object.keys(H.masks)) { const [ids, ws] = H.masks[k]; ids.forEach((v, i) => mv.set(v, Math.max(mv.get(v) || 0, ws[i]))); }
    const nbr = new Map(); for (const v of mv.keys()) nbr.set(v, new Set());
    for (let q = 0; q < nq; q++) for (let e = 0; e < 4; e++) {
      const a = qd[4 * q + e], b = qd[4 * q + ((e + 1) & 3)];
      if (nbr.has(a)) nbr.get(a).add(b); if (nbr.has(b)) nbr.get(b).add(a);
    }
    M.smooth = { ids: Uint32Array.from(mv.keys()), w: Float32Array.from(mv.values()) };
    M.smooth.nbr = Array.from(M.smooth.ids, (v) => Uint32Array.from(nbr.get(v)));
    M.work = { Pm: new Float32Array(3 * nv), P: new Float32Array(3 * nv), tmp: new Float32Array(3 * M.smooth.ids.length) };
    M.jcache = new Map();
    B.calibrate(M);
    M.loadMs = now() - t0;
    return M;
  };

  // ---------------------------------------------------------------- blending
  const addT = (M, P, name, w) => {
    if (!w) return; const t = M.targets[name]; if (!t) return;
    const idx = t.idx, d = t.d;
    for (let i = 0, n = idx.length; i < n; i++) { const o = 3 * idx[i], j = 3 * i; P[o] += w * d[j]; P[o + 1] += w * d[j + 1]; P[o + 2] += w * d[j + 2]; }
  };
  // interpolation weights of the min / average / max targets for a macro value v (0.5 = average); beyond 1 the
  // average->max segment is extrapolated linearly (MakeHuman's own 'max weight' is a modest +9 % girth)
  const tri3 = (v) => (v < 0.5 ? { min: 1 - 2 * Math.max(0, v), average: 2 * Math.max(0, v), max: 0 } : { min: 0, average: 2 - 2 * v, max: 2 * v - 1 });
  // macros: muscle and weight in [0,1] (0.5 = average), th (height target) and cup in [-1,1]
  const macroBody = (M, mac, P) => {
    P.set(M.base);
    const mw = tri3(mac.muscle), ww = tri3(mac.weight);
    for (const m of ['min', 'average', 'max']) for (const w of ['min', 'average', 'max']) if (m !== 'average' || w !== 'average') addT(M, P, `universal-${m}-${w}`, mw[m] * ww[w]);
    addT(M, P, mac.th > 0 ? 'height-max' : 'height-min', Math.abs(mac.th));
    addT(M, P, mac.cup > 0 ? 'cup-max' : 'cup-min', Math.abs(mac.cup));
    addT(M, P, mac.belly > 0 ? 'belly-incr' : 'belly-decr', Math.abs(mac.belly || 0));
    addT(M, P, mac.seat > 0 ? 'buttocks-incr' : 'buttocks-decr', Math.abs(mac.seat || 0));
    addT(M, P, 'nipple-size-decr', 1); addT(M, P, 'nipple-point-decr', 1);   // mannequin finish
    return P;
  };
  const finish = (M, P) => {   // feathered Laplacian smoothing of the nipple and crotch regions
    const S = M.smooth, T = M.work.tmp, n = S.ids.length;
    for (let it = 0; it < 4; it++) {
      for (let i = 0; i < n; i++) {
        const v = 3 * S.ids[i], nb = S.nbr[i]; let x = 0, y = 0, z = 0;
        for (let k = 0; k < nb.length; k++) { const o = 3 * nb[k]; x += P[o]; y += P[o + 1]; z += P[o + 2]; }
        const l = 0.6 * S.w[i] / nb.length;
        T[3 * i] = P[v] + l * (x - nb.length * P[v]); T[3 * i + 1] = P[v + 1] + l * (y - nb.length * P[v + 1]); T[3 * i + 2] = P[v + 2] + l * (z - nb.length * P[v + 2]);
      }
      for (let i = 0; i < n; i++) { const v = 3 * S.ids[i]; P[v] = T[3 * i]; P[v + 1] = T[3 * i + 1]; P[v + 2] = T[3 * i + 2]; }
    }
  };
  // Measurement targets driven by the fit. Each slider key moves one MakeHuman measure modifier (or two, together).
  B.CONTROLS = {
    bust: ['bust-circ'], waist: ['waist-circ'], hip: ['hips-circ'], shoulder: ['shoulder-dist'], neck: ['neck-circ'], thigh: ['thigh-circ'],
    upperarm: ['upperarm-circ'], arm: ['upperarm-length', 'lowerarm-length'], inseam: ['upperleg-height', 'lowerleg-height'], back: ['napetowaist-dist'],
  };
  B.PRIMARY = ['height', 'bust', 'waist', 'hip', 'shoulder'];
  B.SECONDARY = ['arm', 'inseam', 'neck', 'thigh', 'upperarm', 'back'];
  B.LABELS = { height: 'Height', bust: 'Bust', waist: 'Waist', hip: 'Hip', shoulder: 'Shoulder', arm: 'Arm length', inseam: 'Inseam', neck: 'Neck', thigh: 'Thigh', upperarm: 'Upper arm', back: 'Back length' };
  const applyControls = (M, P, keys, x) => {
    for (let j = 0; j < keys.length; j++) {
      const v = x[j]; if (!v) continue;
      for (const t of B.CONTROLS[keys[j]]) addT(M, P, t + (v > 0 ? '-incr' : '-decr'), Math.abs(v));
    }
  };

  // ---------------------------------------------------------------- tape measures
  // Measures the (unscaled) blended mesh P; lengths are multiplied by s = Hreq / height so they refer to the final
  // body (uniformly scaled to the requested height, feet on the floor).
  const vtx = (P, i) => [P[3 * i], P[3 * i + 1], P[3 * i + 2]];
  const joint = (M, P, name) => { const ids = M.joints[name]; let x = 0, y = 0, z = 0; for (const i of ids) { x += P[3 * i]; y += P[3 * i + 1]; z += P[3 * i + 2]; } return [x / ids.length, y / ids.length, z / ids.length]; };
  B.measure = (M, P, Hreq) => {
    const nb = M.nb, L = M.lm, T = M.tris, S = M.sets;
    let y0 = Infinity, y1 = -Infinity;
    for (let i = 0; i < nb; i++) { const y = P[3 * i + 1]; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    const H0 = y1 - y0, s = Hreq ? Hreq / H0 : 1;
    const r = { s, y0, height: H0 * s };
    const yb = 0.5 * (P[3 * L['bust.L'] + 1] + P[3 * L['bust.R'] + 1]);
    r.bust = girthY(P, T, S.trunk, yb) * s;
    const yw = P[3 * L.waist + 1], yh = P[3 * L.hip + 1];
    r.waist = girthY(P, T, S.trunk, yw) * s;
    r.hip = girthY(P, T, S.hip, yh) * s;
    r.shoulder = dist(vtx(P, L['acromion.L']), vtx(P, L['acromion.R'])) * s;
    // neck: plane parallel to the neck-base plane (cervicale - jugular notch, containing the left-right axis), 1.5 cm
    // up so that it cuts the neck itself, not the trapezius
    const C = vtx(P, L.cervicale), Jg = vtx(P, L.jugular);
    let nn = nrm(crs(sub(Jg, C), X)); if (nn[1] < 0) nn = [-nn[0], -nn[1], -nn[2]];
    const up = 0.015 / s, mid = [(C[0] + Jg[0]) / 2 + nn[0] * up, (C[1] + Jg[1]) / 2 + nn[1] * up, (C[2] + Jg[2]) / 2 + nn[2] * up];
    r.neck = girthPlane(P, T, S.neck, nn, dot(nn, mid), X, crs(nn, X), mid, 0.08 / s) * s;
    // thigh: just below the crotch, left leg only
    const yc = P[3 * L.crotch + 1];
    r.thigh = girthY(P, T, S.hip, yc - 0.025 / s, (x) => x > 0.004 / s) * s;
    // upper arm: halfway between shoulder and elbow joints, perpendicular to the upper arm
    const js = joint(M, P, 'l-shoulder'), je = joint(M, P, 'l-elbow'), jw = joint(M, P, 'l-hand'), ax = nrm(sub(je, js)), q = [(js[0] + je[0]) / 2, (js[1] + je[1]) / 2, (js[2] + je[2]) / 2];
    const e1 = nrm(crs(ax, Z)), e2 = crs(ax, e1);
    r.upperarm = girthPlane(P, T, S.armL, ax, dot(ax, q), e1, e2, q, 0.09 / s) * s;
    r.arm = (dist(vtx(P, L['acromion.L']), je) + dist(je, jw)) * s;
    r.inseam = (yc - y0) * s;
    // back length: cervicale down to the waist level along the back midline
    const ch = L.backMidline, yC = C[1]; let bl = 0;
    for (let i = 0; i + 1 < ch.length; i++) {
      const a = vtx(P, ch[i]), b = vtx(P, ch[i + 1]); let lo = Math.min(a[1], b[1]), hi = Math.max(a[1], b[1]);
      if (hi <= yw || lo >= yC) continue;
      const f = hi - lo > 1e-9 ? (Math.min(hi, yC) - Math.max(lo, yw)) / (hi - lo) : 1;
      bl += f * dist(a, b);
    }
    r.back = bl * s;
    r._yw = yw; r._yh = yh;
    return r;
  };

  // ---------------------------------------------------------------- calibration (once per asset)
  B.calibrate = (M) => {
    const P = M.work.Pm, mac = { muscle: 0.5, weight: 0.5, th: 0, cup: 0 };
    const h = [];
    for (const th of [-1, -0.5, 0, 0.5, 1]) { mac.th = th; macroBody(M, mac, P); finish(M, P); h.push([B.measure(M, P).height, th]); }
    mac.th = 0;
    const g = {};
    for (const w of [0, 0.5, 1]) { mac.weight = w; macroBody(M, mac, P); finish(M, P); g[w] = B.measure(M, P); }
    M.cal = { heights: h, neutral: g[0.5], min: g[0], max: g[1] };
  };
  // height-target weight that gives height H before the final uniform scale (piecewise linear inverse)
  const heightTarget = (M, H) => {
    const t = M.cal.heights;
    if (H <= t[0][0]) return -1; if (H >= t[t.length - 1][0]) return 1;
    for (let i = 0; i + 1 < t.length; i++) if (H <= t[i + 1][0]) return t[i][1] + (t[i + 1][1] - t[i][1]) * (H - t[i][0]) / (t[i + 1][0] - t[i][0]);
    return 0;
  };
  // weight macro from the requested girths relative to the neutral body of the same height (keeps limbs, face and
  // neck in proportion when the trunk girths change a lot); the measurement targets then fit the exact values.
  const GIRTH_KEYS = ['bust', 'waist', 'hip'];
  const meanRatio = (a, b, sc) => GIRTH_KEYS.reduce((acc, k) => acc + a[k] / (b[k] * sc), 0) / GIRTH_KEYS.length;
  // Weight macro from how full the requested girths are relative to the neutral body of the same height: keeps
  // arms, legs, neck and face in proportion when the trunk girths change a lot (a plus-size trunk on slim limbs
  // looks wrong). The slope is chosen so that the presets land on plausible builds (average 0.58, curvy 0.9,
  // plus 1.4, where 1 is MakeHuman's 'max weight', which by itself only adds ~9 % girth).
  const weightMacro = (M, req) => {
    const c = M.cal, r = meanRatio(req, c.neutral, req.height / c.neutral.height);
    return Math.max(0, Math.min(B.WMAX, 0.5 + 2.5 * (r - 1)));
  };

  // ---------------------------------------------------------------- generation
  B.DEFAULT = { height: 1.68, bust: 0.92, waist: 0.74, hip: 0.98, shoulder: 0.40, muscle: 0.5, cup: 0 };
  const solve = (A, b) => {   // Gaussian elimination with partial pivoting, A n x n (array of rows)
    const n = b.length, a = A.map((r, i) => r.concat([b[i]]));
    for (let c = 0; c < n; c++) {
      let p = c; for (let r = c + 1; r < n; r++) if (Math.abs(a[r][c]) > Math.abs(a[p][c])) p = r;
      [a[c], a[p]] = [a[p], a[c]];
      if (Math.abs(a[c][c]) < 1e-12) continue;
      for (let r = 0; r < n; r++) if (r !== c) { const f = a[r][c] / a[c][c]; for (let k = c; k <= n; k++) a[r][k] -= f * a[c][k]; }
    }
    return a.map((r, i) => (Math.abs(r[i]) < 1e-12 ? 0 : r[n] / r[i]));
  };
  B.XMAX = 1.6;    // largest measurement-target weight (MakeHuman's sliders stop at 1; moderate extrapolation looks fine)
  B.WMAX = 1.5;    // largest weight macro (extrapolated beyond MakeHuman's max weight, see tri3)

  // meas: { height, bust, waist, hip, shoulder (m), optional arm, inseam, neck, thigh, upperarm, back (m, null = auto),
  //         muscle (0..1), cup (-1..1) }. prev: the previous result (warm start).
  B.generate = (M, meas, prev) => {
    const t0 = now();
    const req = Object.assign({}, B.DEFAULT, meas || {});
    for (const k of B.PRIMARY) if (!(req[k] > 0)) req[k] = B.DEFAULT[k];
    const mac = { muscle: clamp01(req.muscle == null ? 0.5 : req.muscle), cup: Math.max(-1, Math.min(1, req.cup || 0)), th: 0, weight: 0.5 };
    mac.th = heightTarget(M, req.height);
    mac.weight = req.weight != null ? req.weight : weightMacro(M, req);
    mac.belly = Math.max(-1, Math.min(1, req.belly || 0)); mac.seat = Math.max(-1, Math.min(1, req.seat || 0));
    const keys = Object.keys(B.CONTROLS).filter((k) => B.PRIMARY.includes(k) || (req[k] != null && req[k] > 0));
    const Pm = macroBody(M, mac, M.work.Pm), P = M.work.P;
    const evalX = (x) => { P.set(Pm); applyControls(M, P, keys, x); finish(M, P); return B.measure(M, P, req.height); };
    // warm start from the previous solution
    let x = keys.map((k) => (prev && prev.x && prev.keys ? (prev.x[prev.keys.indexOf(k)] || 0) : 0));
    // Jacobian d(measurement)/d(target weight), cached per body type (macros) and control set
    const jkey = keys.join(',') + '|' + [mac.muscle, mac.weight, mac.th, mac.cup, mac.belly, mac.seat].map((v) => (Math.round(v * 10) / 10).toFixed(1)).join(',');
    let J = M.jcache.get(jkey), evals = 0, r = null, m;
    if (!J) {
      m = evalX(x); evals++;
      J = keys.map(() => new Array(keys.length).fill(0));
      for (let j = 0; j < keys.length; j++) {
        const h = x[j] > 0.8 ? -0.25 : 0.25, xj = x.slice(); xj[j] += h;
        const mj = evalX(xj); evals++;
        for (let i = 0; i < keys.length; i++) J[i][j] = (mj[keys[i]] - m[keys[i]]) / h;
      }
      if (M.jcache.size > 64) M.jcache.clear();
      M.jcache.set(jkey, J);
    }
    m = evalX(x); evals++;   // P now holds the body for x
    let it = 0;
    for (; it < 8; it++) {
      r = keys.map((k) => req[k] - m[k]);
      if (Math.max(...r.map(Math.abs)) < 0.0004) break;
      // active set: a weight sitting on its bound whose Newton step points further out is frozen, and the step
      // is re-solved for the others (so one unreachable measurement does not drag the rest off target)
      let free = keys.map(() => true), dx = null;
      for (let pass = 0; pass < keys.length; pass++) {
        const fi = free.map((f, j) => (f ? j : -1)).filter((j) => j >= 0);
        const d = solve(fi.map((i) => fi.map((j) => J[i][j])), fi.map((i) => r[i]));
        dx = keys.map(() => 0); fi.forEach((j, q) => (dx[j] = d[q]));
        let changed = false;
        for (const j of fi) if ((x[j] >= B.XMAX - 1e-9 && dx[j] > 0) || (x[j] <= -B.XMAX + 1e-9 && dx[j] < 0)) { free[j] = false; changed = true; }
        if (!changed) break;
      }
      x = x.map((v, j) => Math.max(-B.XMAX, Math.min(B.XMAX, v + dx[j])));
      m = evalX(x); evals++;
    }
    r = keys.map((k) => req[k] - m[k]);
    // final placement: uniform scale to the requested height, feet on y = 0, hip section centred on z = 0
    const s = m.s, y0 = m.y0, nb = M.nb;
    const k = slice(P, M.tris, M.sets.hip, Y, m._yh, X, Z); let zmin = Infinity, zmax = -Infinity;
    for (let i = 0; i < k; i++) { const z = PTS[2 * i + 1]; if (z < zmin) zmin = z; if (z > zmax) zmax = z; }
    const zc = 0.5 * (zmin + zmax);
    const out = new Float32Array(3 * M.nv);
    for (let i = 0; i < M.nv; i++) { out[3 * i] = P[3 * i] * s; out[3 * i + 1] = (P[3 * i + 1] - y0) * s; out[3 * i + 2] = (P[3 * i + 2] - zc) * s; }
    const meas_ = {}; for (const kk of B.PRIMARY.concat(B.SECONDARY)) meas_[kk] = m[kk];
    const body = {
      pos: out.subarray(0, 3 * nb), allPos: out, tris: M.tris, nb, keys, x, req, mac, m: meas_, residual: Object.fromEntries(keys.map((kk, i) => [kk, r[i]])),
      iters: it, evals, key: B.key(req), landmarks: {}, joints: {},
    };
    for (const [name, id] of Object.entries(M.lm)) if (typeof id === 'number') body.landmarks[name] = vtx(out, id);
    for (const name of Object.keys(M.joints)) body.joints[name] = joint(M, out, name);
    body.sections = B.sections(M, out, 0.01);
    body.ms = now() - t0;
    return body;
  };
  const clamp01 = (v) => Math.max(0, Math.min(1, v));
  B.key = (req) => [...B.PRIMARY, ...B.SECONDARY, 'muscle', 'cup', 'belly', 'seat', 'weight'].map((k) => (req[k] == null ? '-' : (+req[k]).toFixed(4))).join('/');

  // ---------------------------------------------------------------- wrap sections
  // Convex hull (x, z) of the body's horizontal section every dy metres, arms and hands excluded. FL.makeWrap
  // wraps pattern pieces around the envelope of the sections they span.
  B.sections = (M, P, dy) => {
    const T = M.tris, set = M.sets.section;
    let ymax = 0; for (let i = 0; i < M.nb; i++) ymax = Math.max(ymax, P[3 * i + 1]);
    const ny = Math.floor(ymax / dy) + 1, cnt = new Uint32Array(ny + 1), k0 = new Int32Array(set.length), k1 = new Int32Array(set.length);
    for (let q = 0; q < set.length; q++) {
      const t = 3 * set[q], a = P[3 * T[t] + 1], b = P[3 * T[t + 1] + 1], c = P[3 * T[t + 2] + 1];
      const lo = Math.max(0, Math.ceil(Math.min(a, b, c) / dy)), hi = Math.min(ny - 1, Math.floor(Math.max(a, b, c) / dy));
      k0[q] = lo; k1[q] = hi; for (let k = lo; k <= hi; k++) cnt[k + 1]++;
    }
    for (let k = 0; k < ny; k++) cnt[k + 1] += cnt[k];
    const fill = cnt.slice(), bins = new Uint32Array(cnt[ny]);
    for (let q = 0; q < set.length; q++) for (let k = k0[q]; k <= k1[q]; k++) bins[fill[k]++] = set[q];
    const hulls = [], girth = new Float32Array(ny);
    for (let k = 0; k < ny; k++) {
      const n = slice(P, T, bins.subarray(cnt[k], cnt[k + 1]), Y, k * dy, X, Z);
      const h = n >= 3 ? B.hull2(PTS, n) : new Float64Array(0);
      hulls.push(Float32Array.from(h)); girth[k] = h.length ? B.perimeter2(h) : 0;
    }
    return { dy, ny, hulls, girth };
  };

  if (typeof module !== 'undefined') module.exports = B;
})(typeof FL !== 'undefined' ? FL : require('./shared.js'));
