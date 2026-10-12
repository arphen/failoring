// Controller: worker, rebuilds, panels, timeline, save/load.
'use strict';
App.jobId = 0; App.sim = null; App.worker = null;

App.initWorker = () => {
  const b64 = $('#wasmB64').textContent.trim(), bin = atob(b64), bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  const url = URL.createObjectURL(new Blob([$('#workerSrc').textContent], { type: 'text/javascript' }));
  const w = App.worker = new Worker(url);
  w.onmessage = (ev) => App.onWorker(ev.data);
  w.onerror = (e) => { App.setStatus('worker error: ' + e.message); console.error(e); };
  w.postMessage({ type: 'init', wasm: bytes.buffer, fabrics: FL.FABRICS }, [bytes.buffer]);
};
App.setStatus = (t) => { $('#status').textContent = t; };

App.onWorker = (d) => {
  const P = App.pending;
  if (d.type === 'meta' && P && d.job === P.job) {
    P.meta = d; App.sim = P; App.pending = null; V.sim = P; V.setMesh(P); V.cloth.visible = true;
    if (d.warn) App.toast('Mesher warning: a piece boundary could not be fully meshed - try a larger Mesh quality or simpler outline');
    if (d.bake) App.lastBake = d.bake;
    App.setStatus(`${d.n} vertices, ${d.tris.length / 3 | 0} triangles - simulating`); App.refreshBodyNote && App.refreshBodyNote();
  } else if (d.type === 'frame' && App.sim && d.job === App.sim.job) {
    const s = App.sim; s.frames.push(d.pos); s.ke = d.ke; s.gap = d.gap; s.ms = d.ms; s.clear = d.clear;
    V.dirty = true; App.updateSimUi();
  } else if (d.type === 'done' && App.sim && d.job === App.sim.job) { App.sim.done = true; V.dirty = true; App.updateSimUi(); }
  else if (d.type === 'error') { App.setStatus('simulation error: ' + d.msg.split('\n')[0]); console.error(d.msg); }
};
App.updateSimUi = () => {
  const s = App.sim; if (!s) return;
  $('#simFill').style.width = (100 * s.frames.length / s.target) + '%';
  $('#simInfo').textContent = s.done ? `finished after ${s.frames.length} frames - KE ${s.ke.toExponential(1)} J - seam gap ${(s.gap * 1000).toFixed(1)} mm` :
    `frame ${s.frames.length}/${s.target} - ${s.ms ? s.ms.toFixed(0) : '?'} ms/frame`;
  App.setStatus(s.done ? `${s.meta.n} vertices - ready` : `${s.meta.n} vertices - simulating ${s.frames.length}/${s.target}`);
};

App.rebuildTimer = null;
App.scheduleRebuild = (delay) => { clearTimeout(App.rebuildTimer); App.rebuildTimer = setTimeout(App.rebuild, delay); };
App.rebuild = () => {
  const S = App.S, { pieces, bad } = App.validPieces();
  App.bad = bad;
  if (!pieces.length) { App.setStatus('no valid pieces to simulate'); return; }
  const q = App.QUALITY[S.quality], fab = FL.fabricById(S.fabric), job = ++App.jobId;
  const ids = new Set(pieces.map((p) => p.id));
  // the mesh body (or null: legacy primitive mannequin); the worker re-bakes its collider only when `key` changes
  const body = App.body ? { key: App.body.key, pos: App.body.pos.slice(), tris: App.bodyTris, sections: App.body.sections } : null;
  const spec = {
    job, meas: S.meas, fabric: fab, quality: q, rampFrames: S.rampFrames, seamStiffness: S.seamStiffness, selfCollision: S.selfCollision, frames: q.frames + (S.extraFrames || 0),
    pieces: pieces.map((p) => ({ id: p.id, outline: p.outline, grain: p.grain, wrap: p.wrap })),
    seams: S.seams.filter((s) => ids.has(s.a[0]) && ids.has(s.b[0])), body,
  };
  App.worker.postMessage({ type: 'cancel' });
  App.worker.postMessage({ type: 'build', spec });
  App.pending = { job, pieces: JSON.parse(JSON.stringify(spec.pieces)), seams: JSON.parse(JSON.stringify(spec.seams)), meta: null, frames: [], target: spec.frames, done: false,
    body: body ? { sections: body.sections } : null };
  if (V.T >= 3) V.T = 4;    // follow the live drape while editing
  App.setStatus('meshing...'); Ed.dirty = true;
};

// ------------------------------------------------------------------ body
// The mannequin: MakeHuman CC0 mesh + blend shapes fitted to the measurements (body.js). Regenerated synchronously
// on every slider move (10-30 ms); the worker re-bakes its collider on the next rebuild.
App.loadBodyModel = () => {
  try {
    const el = $('#bodyB64'); if (!el) throw new Error('no body asset in this build');
    const bin = atob(el.textContent.trim()), u8 = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) u8[i] = bin.charCodeAt(i);
    App.bodyModel = FL.Body.load(u8); App.bodyTris = Int32Array.from(App.bodyModel.tris);
  } catch (e) { console.error(e); App.bodyModel = null; App.toast('Body model could not be loaded - using the simple mannequin'); }
};
App.regenBody = () => { App.body = App.bodyModel ? FL.Body.generate(App.bodyModel, App.S.meas, App.body) : null; };

// ------------------------------------------------------------------ change routing
App.onChange((kind) => {
  Ed.dirty = true; V.dirty = true;
  if (kind === 'load') {   // a new plan: drop the old simulation until the new one has been meshed
    if (V.cloth) V.cloth.visible = false; V.stitches.concat(V.seamLines).forEach((o) => (o.line.visible = false)); V.sim = null; App.sim = null;
    App.regenBody(); App.refreshAll(); V.buildMat(); V.buildBody(); Ed.fit(); V.T = 0; V.playing = false; V.pan = [0, 0, 0]; V.zoom = 1; App.rebuild(); return;
  }
  if (kind === 'structure' || kind === 'structure-drag') { App.refreshPanels(); if (kind === 'structure') { V.buildMat(); App.scheduleRebuild(120); } else { App.scheduleRebuild(260); } }
  else if (kind === 'table' || kind === 'table-done') { V.buildMat(); }
  else if (kind === 'body') { App.regenBody(); V.buildBody(); App.refreshPanels(); App.scheduleRebuild(200); }
  else if (kind === 'fabric') { V.recolor(); App.scheduleRebuild(150); }
  else { App.refreshPanels(); if (V.recolor) V.recolor(); }
});

// ------------------------------------------------------------------ side panels
const rowHtml = (id, label, min, max, step) => `<div class="row"><label>${label}</label><input type="range" id="${id}" min="${min}" max="${max}" step="${step}"><output id="${id}_o"></output></div>`;
App.bindSlider = (id, get, set, fmt) => {
  const el = $('#' + id), out = $('#' + id + '_o');
  el.oninput = () => { set(+el.value); out.textContent = fmt(+el.value); };
  el.onpointerdown = () => App.pushUndo();
  el._sync = () => { const v = get(); if (v === undefined) return; el.value = v; out.textContent = fmt(v); };
};
App.buildPanels = () => {
  const S = App.S;
  // ---- piece
  $('#tab-piece').innerHTML = `
    <h4>Selected piece</h4><div id="pcNone" class="note">Click a piece on the cutting table.</div>
    <div id="pcBox">
      <div class="row" style="grid-template-columns:78px 1fr"><label>Name</label><input type="text" id="pcName"></div>
      <table class="kv"><tr><td>Size</td><td id="pcSize"></td></tr><tr><td>Area</td><td id="pcArea"></td></tr></table>
      <h4>Grain (warp direction)</h4>
      ${rowHtml('pcGrain', 'Angle', 0, 180, 1)}
      <div class="btns"><button data-g="90">Straight 90&deg;</button><button data-g="0">Cross 0&deg;</button><button data-g="45">Bias 45&deg;</button><button data-g="135">Bias 135&deg;</button></div>
      <h4>Placement on the body</h4>
      ${rowHtml('pcTheta', 'Around', 0, 360, 1)}${rowHtml('pcY', 'Height', 0, 150, 1)}${rowHtml('pcClear', 'Clearance', 0, 40, 1)}${rowHtml('pcRing', 'Flare', 0, 6, 0.1)}
      <div class="note">Around: 0&deg; = front, 180&deg; = back. Height is where the piece's bottom edge starts. Flare lets a wide piece open into an A-line.</div>
      <h4>Edges</h4><table class="kv" id="pcEdges"></table>
      <div class="btns"><button id="pcMirror">Mirror</button><button id="pcDup">Duplicate</button><button id="pcDel">Delete</button></div>
    </div>
    <h4>Add a piece</h4><div class="btns"><button data-add="rect">Rectangle</button><button data-add="trap">Trapezoid</button><button data-add="tri">Triangle</button></div>`;
  const P = () => App.piece(S.sel);
  $('#pcName').oninput = (e) => { const p = P(); if (p) { p.name = e.target.value; Ed.dirty = true; } };
  App.bindSlider('pcGrain', () => P() && Math.round((((P().grain * 180 / Math.PI) % 180) + 180) % 180), (v) => { P().grain = v * Math.PI / 180; App.changed('structure-drag'); }, (v) => v + '°');
  App.bindSlider('pcTheta', () => P() && P().wrap.theta, (v) => { P().wrap.theta = v; App.changed('structure-drag'); }, (v) => v + '°');
  App.bindSlider('pcY', () => P() && Math.round(P().wrap.y * 100), (v) => { P().wrap.y = v / 100; App.changed('structure-drag'); }, (v) => v + ' cm');
  App.bindSlider('pcClear', () => P() && Math.round(P().wrap.clear * 1000), (v) => { P().wrap.clear = v / 1000; App.changed('structure-drag'); }, (v) => v + ' mm');
  App.bindSlider('pcRing', () => P() && (P().wrap.ring || 0), (v) => { P().wrap.ring = v; App.changed('structure-drag'); }, (v) => v.toFixed(1));
  for (const b of $$('#pcBox [data-g]')) b.onclick = () => { App.pushUndo(); P().grain = +b.dataset.g * Math.PI / 180; App.changed('structure'); };
  for (const b of $$('[data-add]')) b.onclick = () => { App.pushUndo(); App.addPiece(b.dataset.add); Ed.fit(); App.changed('structure'); };
  $('#pcMirror').onclick = () => { App.pushUndo(); App.mirrorPiece(P()); App.changed('structure'); };
  $('#pcDup').onclick = () => { App.pushUndo(); App.duplicatePiece(P()); App.changed('structure'); };
  $('#pcDel').onclick = () => { App.pushUndo(); App.removePiece(S.sel); App.changed('structure'); };
  // ---- body
  const cmRow = (id, label, min, max, auto) => `<div class="row${auto ? ' row4' : ''}"><label>${label}</label><input type="range" id="${id}" min="${min}" max="${max}" step="0.5"><output id="${id}_o"></output>${auto ? `<button class="auto" id="${id}_a" title="Back to auto (follow the body type)">auto</button>` : ''}</div>`;
  const PRIM = [['height', 'Height', 140, 200], ['bust', 'Bust', 70, 140], ['waist', 'Waist', 55, 130], ['hip', 'Hip', 75, 145], ['shoulder', 'Shoulder', 30, 48]];
  const SEC = [['arm', 'Arm length', 44, 68], ['inseam', 'Inseam', 62, 96], ['neck', 'Neck', 28, 48], ['thigh', 'Thigh', 40, 85], ['upperarm', 'Upper arm', 20, 45], ['back', 'Back length', 30, 48]];
  const TYPE = [['muscle', 'Muscle', 0, 100], ['cup', 'Bust shape', -100, 100], ['belly', 'Belly', -100, 100]];
  $('#tab-body').innerHTML = `<h4>Preset</h4><div class="btns" id="presets">${Object.keys(FL.PRESETS).map((k) => `<button data-p="${k}">${k}</button>`).join('')}</div>
    <h4>Measurements</h4>${PRIM.map(([k, l, a, b]) => cmRow('m_' + k, l, a, b)).join('')}
    <h4>More measurements</h4>${SEC.map(([k, l, a, b]) => cmRow('m_' + k, l, a, b, true)).join('')}
    <div class="note">Auto: follows the body type. Drag a slider to set the value; the button resets it.</div>
    <h4>Body type</h4>${TYPE.map(([k, l, a, b]) => `<div class="row"><label>${l}</label><input type="range" id="t_${k}" min="${a}" max="${b}" step="1"><output id="t_${k}_o"></output></div>`).join('')}
    <div class="note" id="bodyNote"></div>
    <div class="note">Shoulder is the straight distance between the shoulder points; hip is taken at seat level. The mannequin is MakeHuman's CC0 base mesh fitted to these measurements. Patterns are not rescaled: change the body to see how the same pieces fit someone else.</div>`;
  const fmtCm = (v) => (Math.round(v * 2) / 2).toFixed(1).replace(/\.0$/, '') + ' cm';
  for (const [k] of PRIM) App.bindSlider('m_' + k, () => S.meas[k] * 100, (v) => { S.meas[k] = v / 100; App.changed('body'); }, fmtCm);
  for (const [k] of SEC) {
    App.bindSlider('m_' + k, () => (S.meas[k] != null ? S.meas[k] * 100 : App.body ? App.body.m[k] * 100 : undefined), (v) => { S.meas[k] = v / 100; App.changed('body'); }, fmtCm);
    $('#m_' + k + '_a').onclick = () => { App.pushUndo && App.pushUndo(); S.meas[k] = null; App.changed('body'); };
  }
  App.bindSlider('t_muscle', () => Math.round((S.meas.muscle == null ? 0.5 : S.meas.muscle) * 100), (v) => { S.meas.muscle = v / 100; App.changed('body'); }, (v) => v + '%');
  for (const k of ['cup', 'belly']) App.bindSlider('t_' + k, () => Math.round((S.meas[k] || 0) * 100), (v) => { S.meas[k] = v / 100; App.changed('body'); }, (v) => (v > 0 ? '+' : '') + v);
  for (const b of $$('#presets button')) b.onclick = () => { S.meas = Object.assign({}, FL.PRESETS[b.dataset.p]); App.changed('body'); };
  App.bodySliders = PRIM.concat(SEC).map(([k]) => 'm_' + k).concat(TYPE.map(([k]) => 't_' + k));
  App.secKeys = SEC.map(([k]) => k);
  // ---- fabric
  $('#tab-fabric').innerHTML = `<h4>Fabric</h4><select id="fabSel" style="width:100%">${FL.FABRICS.map((f) => `<option value="${f.id}">${f.name}</option>`).join('')}</select>
    <div class="note" id="fabNote"></div><table class="kv" id="fabTab"></table>
    <h4>Seams and solver</h4>${rowHtml('sStiff', 'Seam k', 2, 50, 1)}${rowHtml('sRamp', 'Pull-in', 0, 60, 1)}
    <label class="chk" style="margin:8px 0"><input type="checkbox" id="sSelf"> self collision (slower)</label>
    <div class="note">Values are starter estimates, not measured data. Seam k is in kN/m. Pull-in is how many frames the seams take to close fully.</div>
    <h4>Plan</h4><div class="note" id="planNote"></div>`;
  $('#fabSel').onchange = (e) => { S.fabric = e.target.value; App.changed('fabric'); App.refreshPanels(); };
  App.bindSlider('sStiff', () => Math.round(S.seamStiffness / 1000), (v) => { S.seamStiffness = v * 1000; App.scheduleRebuild(300); }, (v) => v + ' kN/m');
  App.bindSlider('sRamp', () => S.rampFrames, (v) => { S.rampFrames = v; App.scheduleRebuild(300); }, (v) => v + ' fr');
  $('#sSelf').onchange = (e) => { S.selfCollision = e.target.checked; App.scheduleRebuild(100); };
  // ---- seams
  $('#tab-seams').innerHTML = `<h4>Seams</h4><div id="seamList"></div><div class="note">Pick the <b>Sew</b> tool on the cutting table, click one edge, then the edge it joins. Edge numbers are shown beside each edge length. Edge lengths that differ give ease (the longer side is gathered into the shorter).</div>`;
  // tabs
  for (const b of $$('#tabs button')) b.onclick = () => App.showTab(b.dataset.tab);
};
App.showTab = (t) => { for (const b of $$('#tabs button')) b.classList.toggle('on', b.dataset.tab === t); for (const d of $$('.tab')) d.classList.toggle('on', d.id === 'tab-' + t); };
App.refreshPanels = () => {
  const S = App.S, p = App.piece(S.sel);
  $('#pcNone').style.display = p ? 'none' : ''; $('#pcBox').style.display = p ? '' : 'none';
  if (p) {
    if (document.activeElement !== $('#pcName')) $('#pcName').value = p.name;
    const bb = FL.bbox(p.outline); $('#pcSize').textContent = `${((bb.x1 - bb.x0) * 100).toFixed(1)} x ${((bb.y1 - bb.y0) * 100).toFixed(1)} cm`;
    $('#pcArea').textContent = `${(Math.abs(FL.signedArea(p.outline)) * 1e4).toFixed(0)} cm²`;
    for (const id of ['pcGrain', 'pcTheta', 'pcY', 'pcClear', 'pcRing']) { const el = $('#' + id); if (document.activeElement !== el) el._sync(); }
    $('#pcEdges').innerHTML = p.outline.map((_, i) => {
      const si = S.seams.findIndex((s) => (s.a[0] === p.id && s.a[1] === i) || (s.b[0] === p.id && s.b[1] === i));
      return `<tr><td>edge ${i}${si >= 0 ? ` <span style="color:${SEAM_COLORS[si % 8]}">&#9679; seam ${si + 1}</span>` : ''}</td><td>${(FL.edgeLen(p.outline, i) * 100).toFixed(1)} cm</td></tr>`;
    }).join('');
  }
  for (const id of (App.bodySliders || []).concat(['sStiff', 'sRamp'])) { const el = $('#' + id); if (el && document.activeElement !== el) el._sync(); }
  for (const k of App.secKeys || []) { const b = $('#m_' + k + '_a'); if (b) { const auto = S.meas[k] == null; b.classList.toggle('on', auto); b.textContent = auto ? 'auto' : '\u21ba'; } }
  App.refreshBodyNote();
  $('#fabSel').value = S.fabric; $('#sSelf').checked = !!S.selfCollision;
  const f = FL.fabricById(S.fabric), pr = f.params;
  $('#fabNote').textContent = f.notes || '';
  $('#fabTab').innerHTML = [['Weight', (pr.density_kg_m2 * 1000).toFixed(0) + ' g/m²'], ['Thickness', (pr.thickness_m * 1000).toFixed(2) + ' mm'], ['Stretch warp / weft', pr.tensile_warp_N_m + ' / ' + pr.tensile_weft_N_m + ' N/m'],
    ['Shear', pr.shear_N_m_rad + ' N/m/rad'], ['Bending warp / weft', (pr.bending_warp_Nm * 1e6).toFixed(1) + ' / ' + (pr.bending_weft_Nm * 1e6).toFixed(1) + ' µN·m']].map(([a, b]) => `<tr><td>${a}</td><td>${b}</td></tr>`).join('');
  $('#planNote').textContent = (S.planName || '') + ' - ' + (S.planNotes || '');
  // seams list
  const list = $('#seamList');
  list.innerHTML = S.seams.length ? '' : '<div class="note">No seams yet.</div>';
  S.seams.forEach((s, i) => {
    const A = App.piece(s.a[0]), B = App.piece(s.b[0]); if (!A || !B) return;
    const inf = App.seamInfo(s), ease = inf.ease * 100;
    const row = document.createElement('div'); row.className = 'seamRow' + (S.hotSeam === i ? ' hot' : '');
    row.innerHTML = `<span class="sw" style="background:${SEAM_COLORS[i % 8]}"></span><div><b>${A.name} #${s.a[1]}</b> &harr; <b>${B.name} #${s.b[1]}</b><small>${(inf.la * 100).toFixed(1)} / ${(inf.lb * 100).toFixed(1)} cm - ease ${ease >= 0 ? '+' : ''}${ease.toFixed(1)}%${Math.abs(ease) > 8 ? ' (large)' : ''}</small></div><div><button title="Flip direction">&#8645;</button> <button title="Delete seam">&#10005;</button></div>`;
    const [bf, bd] = row.querySelectorAll('button');
    bf.onclick = () => { App.pushUndo(); s.reverse = !s.reverse; App.changed('structure'); };
    bd.onclick = () => { App.pushUndo(); S.seams.splice(i, 1); App.changed('structure'); };
    row.onmouseenter = () => { S.hotSeam = i; Ed.dirty = true; V.recolor(); }; row.onmouseleave = () => { S.hotSeam = -1; Ed.dirty = true; V.recolor(); };
    list.appendChild(row);
  });
  $('#hint2d') && Ed.updateHint();
};
// fit status of the mannequin: which requested measurements the body model could not reach, timings
App.refreshBodyNote = () => {
  const el = $('#bodyNote'), b = App.body; if (!el) return;
  if (!b) { el.textContent = 'Simple mannequin (body model not available).'; return; }
  const off = Object.entries(b.residual).filter(([, r]) => Math.abs(r) > 0.003)
    .map(([k, r]) => `${FL.Body.LABELS[k]} reached ${(b.m[k] * 100).toFixed(1)} of ${(b.req[k] * 100).toFixed(1)} cm`);
  const bake = App.lastBake ? `, collider ${App.lastBake.ms.toFixed(0)} ms` : '';
  el.textContent = (off.length ? 'Out of the body model\'s range: ' + off.join('; ') + '.' : 'All measurements matched (within 1 mm).') + ` Body ${b.ms.toFixed(0)} ms${bake}.`;
};
App.refreshAll = () => {
  const S = App.S; $('#qualSel').value = S.quality;
  App.refreshPanels();
};

// ------------------------------------------------------------------ timeline
App.initTimeline = () => {
  const rng = $('#tlRange');
  rng.oninput = () => { V.T = +rng.value / 1000; V.playing = false; V.dirty = true; };
  $('#tlPlay').onclick = () => App.togglePlay();
  $('#tlStart').onclick = () => { V.T = 0; V.playing = false; V.dirty = true; };
  $('#tlBack').onclick = () => { V.T = clamp(V.T - 0.1, 0, 4); V.playing = false; V.dirty = true; };
  $('#tlFwd').onclick = () => { V.T = clamp(V.T + 0.1, 0, 4); V.playing = false; V.dirty = true; };
  $('#tlSpeed').onchange = (e) => (V.speed = +e.target.value);
  for (const b of $$('#stages button')) b.onclick = () => { V.T = +b.dataset.t; V.playing = false; V.dirty = true; };
  $('#tlMore').onclick = () => { const s = App.sim; if (!s) return; s.target += 60; s.done = false; App.worker.postMessage({ type: 'more', job: s.job, count: 60 }); App.updateSimUi(); };
  $('#tlResim').onclick = () => App.rebuild();
  window.addEventListener('keydown', (e) => { if (e.code === 'Space' && !/INPUT|SELECT|TEXTAREA|BUTTON/.test(document.activeElement.tagName)) { e.preventDefault(); App.togglePlay(); } });
};
App.togglePlay = () => { if (V.T >= 3.999) V.T = 0; V.playing = !V.playing; V.last = performance.now(); V.dirty = true; };
App.tickTimeline = (now) => {
  if (V.playing) {
    const dt = Math.min(0.1, (now - (V.last || now)) / 1000), s = App.sim;
    const dur = V.T < 3 ? 2.4 : Math.max(1.5, (s ? s.target : 48) / 24);
    V.T += dt * V.speed / dur; if (V.T >= 4) { V.T = 4; V.playing = false; }
  }
  V.last = now;
  const rng = $('#tlRange'); if (document.activeElement !== rng || V.playing) rng.value = Math.round(V.T * 1000);
  const st = Math.min(3, Math.floor(V.T)); $$('#stages button').forEach((b, i) => b.classList.toggle('on', i === st));
  $('#tlPlay').innerHTML = V.playing ? '&#10074;&#10074;' : '&#9654;'; $('#stageName').textContent = V.stageLabel(V.T);
};

// ------------------------------------------------------------------ boot
App.boot = () => {
  FL.FABRICS = JSON.parse($('#fabricsJson').textContent);
  App.S = { pieces: [], seams: [], fabric: 'cotton_poplin', meas: Object.assign({}, App.DEFAULT_MEAS), quality: 'standard', seamStiffness: 20000, rampFrames: 20, selfCollision: false,
    tool: 'select', snap: true, sel: null, selV: -1, sewA: null, hotSeam: -1, planName: '', planNotes: '' };
  const sel = $('#planSel'); sel.innerHTML = Object.keys(PLANS).map((k) => `<option value="${k}">${PLANS[k].name}</option>`).join('');
  sel.onchange = () => App.loadPattern(PLANS[sel.value]);
  $('#qualSel').onchange = (e) => { App.S.quality = e.target.value; App.scheduleRebuild(50); };
  $('#snap').onchange = (e) => (App.S.snap = e.target.checked);
  $('#btnSave').onclick = () => { const a = document.createElement('a'); a.href = URL.createObjectURL(new Blob([App.exportPattern()], { type: 'application/json' })); a.download = (App.S.planName || 'pattern').replace(/\W+/g, '-').toLowerCase() + '.failoring.json'; a.click(); };
  $('#btnOpen').onclick = () => $('#fileIn').click();
  $('#fileIn').onchange = async (e) => { const f = e.target.files[0]; if (!f) return; try { App.loadPattern(JSON.parse(await f.text())); } catch (err) { App.toast('Could not read that pattern: ' + err.message); } e.target.value = ''; };
  App.loadBodyModel(); App.regenBody();
  V.init(); Ed.init(); App.buildPanels(); App.initTimeline(); App.initWorker();
  if (App.shootInit) App.shootInit();
  App.loadPattern(PLANS.pencil);
  // one failing frame must not stop the render loop
  const loop = (now) => { try { App.tickTimeline(now); Ed.draw(); V.render(); } catch (e) { console.error(e); } requestAnimationFrame(loop); };
  requestAnimationFrame(loop);
};
window.addEventListener('DOMContentLoaded', () => { try { App.boot(); } catch (e) { document.body.insertAdjacentHTML('beforeend', '<pre style="position:fixed;top:50px;left:10px;color:#f66;z-index:9">' + e.stack + '</pre>'); throw e; } });
