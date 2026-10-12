// 3D preview: mannequin, cutting-table mat, cloth mesh, sew-lines, assembly timeline poses, own orbit camera.
'use strict';
const V = { dirty: true, yaw: 0.55, pitch: 0.42, zoom: 1, pan: [0, 0, 0], T: 0, playing: false, speed: 1, sim: null, tableZ: 1.75, shoot: null };
const STAGE_NAMES = ['1 - cut pieces on the table', '2 - move pieces to the body', '3 - sew the seams', '4 - drape'];

V.init = () => {
  if (THREE.ColorManagement) THREE.ColorManagement.legacyMode = false;
  const host = $('#view3d');
  const r = V.renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
  r.setPixelRatio(Math.min(2, devicePixelRatio || 1)); r.shadowMap.enabled = true; r.shadowMap.type = THREE.PCFSoftShadowMap;
  r.toneMapping = THREE.ACESFilmicToneMapping; r.toneMappingExposure = 0.95; r.outputEncoding = THREE.sRGBEncoding;
  host.appendChild(r.domElement);
  const sc = V.scene = new THREE.Scene(); sc.background = new THREE.Color('#12151a'); sc.fog = new THREE.Fog('#12151a', 7, 16);
  V.cam = new THREE.PerspectiveCamera(36, 1, 0.05, 40);
  sc.add(new THREE.HemisphereLight('#cfd8ea', '#3a342c', 0.55));
  const key = V.key = new THREE.DirectionalLight('#fff1dc', 1.9); key.position.set(2.2, 4.2, 3.2); key.castShadow = true;
  key.shadow.mapSize.set(2048, 2048); const sh = key.shadow.camera; sh.left = -3; sh.right = 3; sh.top = 3; sh.bottom = -3; sh.near = 0.5; sh.far = 12;
  key.shadow.bias = -0.0004; key.shadow.normalBias = 0.012; sc.add(key); sc.add(key.target);
  const rim = V.rim = new THREE.DirectionalLight('#9ec3ff', 0.9); rim.position.set(-3, 2.5, -3); sc.add(rim);
  const floor = new THREE.Mesh(new THREE.CircleGeometry(9, 64), new THREE.MeshStandardMaterial({ color: '#232830', roughness: 0.95 }));
  floor.rotation.x = -Math.PI / 2; floor.position.y = -0.001; floor.receiveShadow = true; sc.add(floor); V.floor = floor;
  V.matGroup = new THREE.Group(); sc.add(V.matGroup);
  V.modelGroup = new THREE.Group(); sc.add(V.modelGroup);
  V.bodyGroup = new THREE.Group(); V.modelGroup.add(V.bodyGroup);
  V.cloth = null; V.stitches = []; V.seamLines = [];
  V.bindOrbit(r.domElement);
  new ResizeObserver(() => V.resize()).observe(host); V.resize();
};
V.resize = () => { const h = $('#view3d'), w = h.clientWidth, ht = h.clientHeight; if (!w || !ht) return; V.renderer.setSize(w, ht); V.cam.aspect = w / ht; V.cam.updateProjectionMatrix(); V.dirty = true; };

// ---------------------------------------------------------------- orbit
V.bindOrbit = (el) => {
  let d = null;
  el.addEventListener('contextmenu', (e) => e.preventDefault());
  el.addEventListener('pointerdown', (e) => { d = { x: e.clientX, y: e.clientY, pan: e.button === 2 || e.shiftKey }; el.setPointerCapture(e.pointerId); V.userMoved = true; });
  el.addEventListener('pointermove', (e) => {
    if (!d) return; const dx = e.clientX - d.x, dy = e.clientY - d.y; d.x = e.clientX; d.y = e.clientY;
    if (d.pan) { const k = 0.0015 * V.dist; V.pan[0] -= Math.cos(V.yaw) * dx * k; V.pan[2] += Math.sin(V.yaw) * dx * k; V.pan[1] += dy * k; }
    else { V.yaw -= dx * 0.008; V.pitch = clamp(V.pitch + dy * 0.006, -0.15, 1.5); }
    V.dirty = true;
  });
  el.addEventListener('pointerup', () => { d = null; });
  el.addEventListener('wheel', (e) => { e.preventDefault(); V.zoom = clamp(V.zoom * Math.exp(e.deltaY * 0.0012), 0.25, 3); V.dirty = true; }, { passive: false });
};
V.focus = (T) => {   // auto framing: table early, body later
  const s = smooth(T - 0.9);   // 0 at table, 1 at body
  const tgt = [0, FL.lerp(0.02, App.S.meas.height * 0.52, s), FL.lerp(V.tableZ, 0, s)];
  const dist = FL.lerp(2.9, 3.3 * App.S.meas.height / 1.68, s);
  return { tgt, dist };
};
V.updateCamera = () => {
  const f = V.focus(V.T), c = V.cam; V.dist = f.dist * V.zoom;
  const t = [f.tgt[0] + V.pan[0], f.tgt[1] + V.pan[1], f.tgt[2] + V.pan[2]];
  const pit = V.pitch + 0.45 * (1 - smooth(V.T - 0.9));
  c.position.set(t[0] + V.dist * Math.sin(V.yaw) * Math.cos(pit), t[1] + V.dist * Math.sin(pit), t[2] + V.dist * Math.cos(V.yaw) * Math.cos(pit));
  c.lookAt(t[0], t[1], t[2]);
};

// ---------------------------------------------------------------- body
// The mannequin is the generated body mesh (App.body, see body.js): exactly the surface the cloth collides with
// (the worker bakes the same triangles into its distance grid), so it is drawn unshrunk.
V.mannequinMat = () => V.bodyMat || (V.bodyMat = new THREE.MeshStandardMaterial({ color: '#b3a699', roughness: 0.52, metalness: 0 }));
V.buildBody = () => {
  const body = App.body, g = V.bodyGroup;
  if (!body) { V.buildBodyPrims(); return; }
  if (!V.bodyMesh) {
    while (g.children.length) { const o = g.children.pop(); o.geometry && o.geometry.dispose(); }
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(body.pos.length), 3).setUsage(THREE.DynamicDrawUsage));
    geo.setIndex(new THREE.BufferAttribute(body.tris, 1));
    const m = V.bodyMesh = new THREE.Mesh(geo, V.mannequinMat()); m.castShadow = m.receiveShadow = true; m.frustumCulled = false; g.add(m);
    // display-only eyeballs fill the open sockets (sculpted-mannequin look); the cloth never reaches them
    const eye = new THREE.SphereGeometry(1, 24, 16);
    V.eyes = [0, 1].map(() => { const e = new THREE.Mesh(eye, V.mannequinMat()); g.add(e); return e; });
  }
  const geo = V.bodyMesh.geometry;
  geo.attributes.position.array.set(body.pos); geo.attributes.position.needsUpdate = true;
  geo.computeVertexNormals(); geo.computeBoundingSphere();
  const r = 0.0118 * body.m.height / 1.68;
  V.eyes[0].position.set(...body.joints['l-eye']); V.eyes[1].position.set(...body.joints['r-eye']);
  for (const e of V.eyes) e.scale.setScalar(r);
  V.dirty = true;
};
// Legacy primitive mannequin (only when the body asset is missing): primitives shrunk 4 mm so cloth resting on
// the analytic surfaces does not z-fight with their coarse display meshes.
V.buildBodyPrims = () => {
  const g = V.bodyGroup; V.bodyMesh = null; while (g.children.length) { const o = g.children.pop(); o.geometry && o.geometry.dispose(); }
  const mat = V.mannequinMat();
  const sphere = new THREE.SphereGeometry(1, 28, 20), cyl = new THREE.CylinderGeometry(1, 1, 1, 20, 1, true);
  for (const p of FL.bodyPrims(App.S.meas, null)) {
    if (p.t === 'e') { const m = new THREE.Mesh(sphere, mat); m.position.set(...p.c); m.scale.set(p.r[0] - 0.004, p.r[1] - 0.004, p.r[2] - 0.004); m.castShadow = m.receiveShadow = true; g.add(m); }
    else if (p.t === 'c') {
      const a = new THREE.Vector3(...p.a), b = new THREE.Vector3(...p.b), dir = b.clone().sub(a), L = dir.length() || 1e-6;
      const q = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.clone().normalize());
      const c = new THREE.Mesh(cyl, mat); c.position.copy(a).add(b).multiplyScalar(0.5); c.quaternion.copy(q); c.scale.set(p.r - 0.004, L, p.r - 0.004); c.castShadow = c.receiveShadow = true; g.add(c);
      for (const e of [a, b]) { const s = new THREE.Mesh(sphere, mat); s.position.copy(e); s.scale.setScalar(p.r - 0.004); s.castShadow = s.receiveShadow = true; g.add(s); }
    }
  }
  V.dirty = true;
};

// ---------------------------------------------------------------- cutting-table mat
V.layout = () => {   // bbox of the laid-out pieces (valid ones), used to centre them on the mat
  let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
  for (const p of App.S.pieces) { const b = FL.bbox(p.outline); x0 = Math.min(x0, b.x0 + p.table.x); x1 = Math.max(x1, b.x1 + p.table.x); y0 = Math.min(y0, b.y0 + p.table.y); y1 = Math.max(y1, b.y1 + p.table.y); }
  if (x0 > x1) { x0 = y0 = 0; x1 = y1 = 1; }
  return { cx: (x0 + x1) / 2, cy: (y0 + y1) / 2, w: x1 - x0, h: y1 - y0 };
};
V.buildMat = () => {
  const g = V.matGroup; while (g.children.length) { const o = g.children.pop(); o.geometry && o.geometry.dispose(); }
  const L = V.layout(), w = Math.max(1.6, L.w + 0.6), h = Math.max(1.0, L.h + 0.6);
  V.layoutKey = JSON.stringify([L.cx.toFixed(3), L.cy.toFixed(3)]);
  const slab = new THREE.Mesh(new THREE.BoxGeometry(w, 0.02, h), new THREE.MeshStandardMaterial({ color: '#2a4640', roughness: 0.9 }));
  slab.position.set(0, 0.01, V.tableZ); slab.receiveShadow = true; g.add(slab);
  const div = Math.round(w / 0.1), grid = new THREE.GridHelper(w, div, '#8fb7ad', '#476a62'); grid.scale.z = h / w; grid.position.set(0, 0.026, V.tableZ);
  grid.material.transparent = true; grid.material.opacity = 0.5; g.add(grid);
  V.dirty = true;
};
V.tableXZ = (x, y) => { const L = V.layout(); return [x - L.cx, V.tableZ - (y - L.cy)]; };

// ---------------------------------------------------------------- cloth
const tbl = (p) => { const q = App.piece(p.id); return (q && q.table) || p.table || { x: 0, y: 0 }; };   // pieces of a sim from a previous plan have no table entry
V.setMesh = (sim) => {
  const sc = V.scene, meta = sim.meta;
  if (V.cloth) { V.modelGroup.remove(V.cloth); V.cloth.geometry.dispose(); }
  const geo = new THREE.BufferGeometry();
  // display mesh: the cloth vertices plus one midpoint per edge (worker: FL.subdivide / fl_display_positions)
  V.pos = new Float32Array(meta.nd * 3); V.col = new Float32Array(meta.nd * 3);
  geo.setAttribute('position', new THREE.BufferAttribute(V.pos, 3).setUsage(THREE.DynamicDrawUsage));
  geo.setAttribute('color', new THREE.BufferAttribute(V.col, 3));
  geo.setIndex(new THREE.BufferAttribute(new Uint32Array(meta.dtris), 1));
  const mat = V.clothMat || (V.clothMat = new THREE.MeshPhysicalMaterial({ vertexColors: true, side: THREE.DoubleSide, roughness: 0.78, metalness: 0, sheen: 0.3, sheenRoughness: 0.6, sheenColor: new THREE.Color('#ffffff'), polygonOffset: true, polygonOffsetFactor: 0.5, polygonOffsetUnits: 0.5 }));
  V.cloth = new THREE.Mesh(geo, mat); V.cloth.castShadow = V.cloth.receiveShadow = true; V.cloth.frustumCulled = false; V.modelGroup.add(V.cloth);
  // per-piece wrap data
  V.wraps = sim.pieces.map((p) => FL.makeWrap(p, sim.body || App.S.meas));   // the same body the worker wrapped onto
  V.rebuildSeamObjects(sim);
  V.recolor(); V.dirty = true;
};
V.recolor = () => {
  const sim = V.sim; if (!sim || !V.cloth) return;
  const base = new THREE.Color(FABRIC_COLORS[App.S.fabric] || '#9fb7d6'), hi = new THREE.Color('#ff9d4d'), c = new THREE.Color();
  sim.pieces.forEach((p, i) => {
    const r = sim.meta.ranges[i], sel = p.id === App.S.sel, k = 0.92 + 0.06 * (i % 3);
    c.copy(base).multiplyScalar(k); if (sel && !V.noSel) c.multiplyScalar(1.18);
    for (let j = 0; j < r.count; j++) { V.col[3 * (r.first + j)] = c.r; V.col[3 * (r.first + j) + 1] = c.g; V.col[3 * (r.first + j) + 2] = c.b; }
  });
  const E = sim.meta.edges, n = sim.meta.n;   // edge midpoints take the colour of their piece
  for (let e = 0; e < E.length / 2; e++) { const a = 3 * E[2 * e], o = 3 * (n + e); V.col[o] = V.col[a]; V.col[o + 1] = V.col[a + 1]; V.col[o + 2] = V.col[a + 2]; }
  V.cloth.geometry.attributes.color.needsUpdate = true;
  V.seamLines.forEach((s) => { const hot = App.S.hotSeam === s.si; s.line.material.opacity = hot ? 1 : 0.85; s.line.material.color.set(hot ? '#ffffff' : SEAM_COLORS[s.si % SEAM_COLORS.length]); });
  V.dirty = true;
};
V.rebuildSeamObjects = (sim) => {
  for (const o of V.stitches.concat(V.seamLines)) { V.scene.remove(o.line); o.line.geometry.dispose(); o.line.material.dispose(); }
  V.stitches = []; V.seamLines = [];
  const idx = {}; sim.pieces.forEach((p, i) => (idx[p.id] = i));
  App.S.seams.forEach((s, si) => {
    const a = idx[s.a[0]], b = idx[s.b[0]]; if (a === undefined || b === undefined || a === b) return;
    const A = sim.meta.chains[a][s.a[1]], B = sim.meta.chains[b][s.b[1]]; if (!A || !B || A.length < 2 || B.length < 2) return;
    const col = SEAM_COLORS[si % SEAM_COLORS.length], m = Math.max(A.length, B.length), pairs = [];
    for (let k = 0; k < m; k++) { const t = k / (m - 1); pairs.push([A[Math.round(t * (A.length - 1))], B[Math.round((s.reverse ? 1 - t : t) * (B.length - 1))]]); }
    const sg = new THREE.BufferGeometry(); sg.setAttribute('position', new THREE.BufferAttribute(new Float32Array(pairs.length * 6), 3).setUsage(THREE.DynamicDrawUsage));
    const sl = new THREE.LineSegments(sg, new THREE.LineBasicMaterial({ color: col, transparent: true, opacity: 0.6 })); sl.frustumCulled = false; V.scene.add(sl);
    V.stitches.push({ line: sl, pairs, si });
    // seam edge polylines
    const seg = []; for (const ch of [A, B]) for (let k = 0; k + 1 < ch.length; k++) seg.push(ch[k], ch[k + 1]);
    const lg = new THREE.BufferGeometry(); lg.setAttribute('position', new THREE.BufferAttribute(new Float32Array(seg.length * 3), 3).setUsage(THREE.DynamicDrawUsage));
    const ll = new THREE.LineSegments(lg, new THREE.LineBasicMaterial({ color: col, transparent: true, opacity: 0.85 })); ll.frustumCulled = false; V.scene.add(ll);
    V.seamLines.push({ line: ll, seg, si });
  });
};

// ---------------------------------------------------------------- timeline pose
const _m = new THREE.Matrix4(), _q0 = new THREE.Quaternion(), _q1 = new THREE.Quaternion(), _q = new THREE.Quaternion(), _v = new THREE.Vector3();
V.currentFrame = () => {
  const sim = V.sim; if (!sim || !sim.frames.length) return null;
  const N = Math.max(2, sim.target || sim.frames.length);
  const idx = V.T >= 4 ? sim.frames.length - 1 : Math.round(clamp(V.T - 3, 0, 1) * (N - 1));
  return sim.frames[Math.min(idx, sim.frames.length - 1)];
};
V.pose = (T) => {
  const sim = V.sim; if (!sim || !V.cloth) return;
  const meta = sim.meta, n = sim.pieces.length, uv = meta.uv, P2 = meta.P2, pos = V.pos;
  if (T >= 2) {
    const f = T >= 3 ? V.currentFrame() : null;
    if (f && f.length === pos.length) pos.set(f); else FL.withMidpoints(P2, meta.n, meta.edges, pos);
  }
  else {
    sim.pieces.forEach((p, i) => {
      const r = meta.ranges[i], w = V.wraps[i], bb = FL.bbox(p.outline), vm = (bb.y0 + bb.y1) / 2, uc = w.uc;
      const [c0x, c0z] = V.tableXZ(uc + tbl(p).x, vm + tbl(p).y), C0 = [c0x, 0.032, c0z];
      const C1 = w.flatPoint(uc, vm, 0.30);
      // end-of-phase-A basis (upright, tangent to the ellipse at the piece centre)
      const ph = w.flatPoint(uc, vm, 0), tx = w.flatPoint(uc + 0.1, vm, 0), nx0 = w.flatPoint(uc, vm, 1);
      let ex1 = [tx[0] - ph[0], tx[1] - ph[1], tx[2] - ph[2]], nn = [nx0[0] - ph[0], nx0[1] - ph[1], nx0[2] - ph[2]];
      const l1 = Math.hypot(...ex1); ex1 = ex1.map((x) => x / l1);
      const ey1 = [0, 1, 0];
      _m.makeBasis(new THREE.Vector3(1, 0, 0), new THREE.Vector3(0, 0, -1), new THREE.Vector3(0, 1, 0)); _q0.setFromRotationMatrix(_m);
      _m.makeBasis(new THREE.Vector3(...ex1), new THREE.Vector3(...ey1), new THREE.Vector3(nn[0], nn[1], nn[2]).normalize()); _q1.setFromRotationMatrix(_m);
      const delay = n > 1 ? 0.3 * i / (n - 1) : 0, pr = clamp((T - 1 - delay) / 0.7, 0, 1);
      const sA = smooth(pr / 0.6), sB = smooth((pr - 0.6) / 0.4);
      const lift = 0.35 * Math.sin(Math.PI * clamp(pr / 0.6, 0, 1));
      _q.copy(_q0).slerp(_q1, sA); _m.makeRotationFromQuaternion(_q); const e = _m.elements;
      const Cx = C0[0] + (C1[0] - C0[0]) * sA, Cy = C0[1] + (C1[1] - C0[1]) * sA + lift, Cz = C0[2] + (C1[2] - C0[2]) * sA;
      for (let k = 0; k < r.count; k++) {
        const vi = r.first + k, a = uv[2 * vi] - uc, b = uv[2 * vi + 1] - vm;
        let x = Cx + e[0] * a + e[4] * b, y = Cy + e[1] * a + e[5] * b, z = Cz + e[2] * a + e[6] * b;
        if (sB > 0) { x += (P2[3 * vi] - x) * sB; y += (P2[3 * vi + 1] - y) * sB; z += (P2[3 * vi + 2] - z) * sB; }
        pos[3 * vi] = x; pos[3 * vi + 1] = y; pos[3 * vi + 2] = z;
      }
    });
    FL.withMidpoints(pos, meta.n, meta.edges, pos);
  }
  V.cloth.geometry.attributes.position.needsUpdate = true; V.cloth.geometry.computeVertexNormals();
  // stitches: hidden before stage 2, faint guides while pieces move, drawn progressively in stage 3
  const reveal = T < 2 ? 1 : clamp(T - 2, 0, 1) * 1.0;
  for (const s of V.stitches) {
    const a = s.line.geometry.attributes.position.array, m = s.pairs.length;
    for (let k = 0; k < m; k++) { const [i, j] = s.pairs[k]; a[6 * k] = pos[3 * i]; a[6 * k + 1] = pos[3 * i + 1]; a[6 * k + 2] = pos[3 * i + 2]; a[6 * k + 3] = pos[3 * j]; a[6 * k + 4] = pos[3 * j + 1]; a[6 * k + 5] = pos[3 * j + 2]; }
    s.line.geometry.attributes.position.needsUpdate = true;
    s.line.geometry.setDrawRange(0, T < 1.2 ? 0 : (T < 2 ? m * 2 : Math.max(2, Math.ceil(m * reveal) * 2)));
    s.line.material.opacity = T < 2 ? 0.35 : (T < 3 ? 0.35 + 0.65 * (T - 2) : 0.9);
  }
  for (const s of V.seamLines) {
    const a = s.line.geometry.attributes.position.array;
    s.seg.forEach((vi, k) => { a[3 * k] = pos[3 * vi]; a[3 * k + 1] = pos[3 * vi + 1]; a[3 * k + 2] = pos[3 * vi + 2]; });
    s.line.geometry.attributes.position.needsUpdate = true; s.line.visible = T >= 0.05;
  }
};

V.stageLabel = (T) => STAGE_NAMES[Math.min(3, Math.floor(T))] + (T >= 3 && V.sim && !V.sim.done ? '  (simulating ' + V.sim.frames.length + '/' + V.sim.target + ')' : '');
V.render = () => {
  if (V.shoot) return;
  if (!V.dirty && !V.playing) return; V.dirty = false;
  V.pose(V.T); V.updateCamera(); V.renderer.render(V.scene, V.cam);
};
