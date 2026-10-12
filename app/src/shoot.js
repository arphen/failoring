// Photo shoot: curated "catwalk photographer" shots of the finished drape, composited with a studio-style look.
'use strict';
const SHOTS = [
  { id: 'front', name: 'Full length - front', cam: [0, 1.0, 6.8], tgt: [0, 0.86, 0], fov: 18, yaw: 0 },
  { id: 'three', name: 'Three-quarter pass', cam: [2.7, 1.12, 5.6], tgt: [0, 0.87, 0], fov: 20, yaw: 0.12 },
  { id: 'profile', name: 'Profile', cam: [6.6, 1.02, 0.3], tgt: [0, 0.87, 0], fov: 20, yaw: 0 },
  { id: 'low', name: 'Low-angle hero', cam: [0.9, 0.28, 3.8], tgt: [0, 0.92, 0], fov: 30, yaw: -0.1 },
  { id: 'waist', name: 'Waist and seams detail', cam: [0.9, 1.0, 2.6], tgt: [0, 0.86, 0], fov: 17, yaw: 0.2 },
  { id: 'back', name: 'Back three-quarter', cam: [-2.5, 1.1, -5.4], tgt: [0, 0.87, 0], fov: 20, yaw: 0 },
  { id: 'hem', name: 'Hem detail', cam: [1.5, 0.46, 2.2], tgt: [0, 0.46, 0], fov: 24, yaw: 0.35 },
];
const SHOOT_SIZES = { preview: [800, 1000], standard: [1200, 1500], high: [1600, 2000] };
const Sh = { shots: [], enabled: {}, size: 'standard', busy: false, runway: null, sameMoment: false };
SHOTS.forEach((s) => (Sh.enabled[s.id] = true));

const rng = (seed) => { let a = seed >>> 0; return () => { a = (a + 0x6D2B79F5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; };

App.shootInit = () => {
  $('#tab-shoot').innerHTML = `<h4>Shots</h4><div id="shotList"></div>
    <h4>Resolution</h4><select id="shotSize" style="width:100%"><option value="preview">Preview 800 x 1000</option><option value="standard" selected>Standard 1200 x 1500</option><option value="high">High 1600 x 2000</option></select>
    <label class="chk" style="display:flex;margin:8px 0"><input type="checkbox" id="shotSame"> Same moment for every shot</label>
    <div class="btns"><button id="shootGo" class="accent" style="width:100%;padding:9px">&#128247; Take photos</button></div>
    <div class="note">Each shot is rendered from the finished drape (the latest simulated frame) with long lenses, studio key, fill and rim lights, a runway, bokeh backdrop, vignette and film grain. With Motion set to Walking (top bar) the mannequin is in mid-stride and every shot catches a different moment of the gait cycle - a different leg position, the skirt or trousers swinging differently - unless "Same moment" is ticked. Standing: the mannequin stays still and only turns slightly.</div>`;
  $('#shotList').innerHTML = SHOTS.map((s) => `<label class="chk" style="display:flex;margin:5px 0"><input type="checkbox" data-shot="${s.id}" checked> ${s.name}</label>`).join('');
  for (const c of $$('[data-shot]')) c.onchange = () => (Sh.enabled[c.dataset.shot] = c.checked);
  $('#shotSame').onchange = (e) => (Sh.sameMoment = e.target.checked);
  $('#shotSize').onchange = (e) => (Sh.size = e.target.value);
  $('#shootGo').onclick = () => App.takeShots();
  $('#btnShoot').onclick = () => { App.showTab('shoot'); App.takeShots(); };
  $('#gClose').onclick = () => ($('#gallery').hidden = true);
  $('#gAll').onclick = () => { $$('#gGrid a').forEach((a, i) => setTimeout(() => a.click(), i * 250)); };
};

Sh.buildRunway = () => {
  const g = new THREE.Group(), floor = new THREE.Mesh(new THREE.PlaneGeometry(3.4, 18), new THREE.MeshStandardMaterial({ color: '#15171c', roughness: 0.28, metalness: 0.1 }));
  floor.rotation.x = -Math.PI / 2; floor.position.y = -0.001; floor.receiveShadow = true; g.add(floor);   // just below the soles
  const em = new THREE.MeshBasicMaterial({ color: '#6f6a5c' });
  for (const x of [-1.6, 1.6]) { const strip = new THREE.Mesh(new THREE.BoxGeometry(0.04, 0.02, 18), em); strip.position.set(x, 0.01, 0); g.add(strip); }
  g.visible = false; V.scene.add(g); return g;
};

Sh.composite = (shot, i, w, h) => {
  const cv = document.createElement('canvas'); cv.width = w; cv.height = h; const c = cv.getContext('2d'), R = rng(1234 + i * 77);
  // backdrop: deep gradient with a soft spotlight behind the subject
  const warm = i % 2 === 0, g = c.createLinearGradient(0, 0, 0, h);
  g.addColorStop(0, warm ? '#1b1620' : '#10151d'); g.addColorStop(1, warm ? '#0b0a0d' : '#07090c'); c.fillStyle = g; c.fillRect(0, 0, w, h);
  const sp = c.createRadialGradient(w * 0.5, h * 0.42, 0, w * 0.5, h * 0.42, h * 0.6); sp.addColorStop(0, warm ? 'rgba(255,214,170,0.22)' : 'rgba(170,200,255,0.20)'); sp.addColorStop(1, 'rgba(0,0,0,0)');
  c.fillStyle = sp; c.fillRect(0, 0, w, h);
  // bokeh discs: audience / stage lights far behind the model
  for (let k = 0; k < 46; k++) {
    const x = R() * w, y = (0.05 + R() * 0.55) * h, r = (0.012 + R() * R() * 0.05) * h, hue = R() < 0.6 ? 36 + R() * 14 : 205 + R() * 25;
    const bg = c.createRadialGradient(x, y, r * 0.2, x, y, r); bg.addColorStop(0, `hsla(${hue},80%,78%,${0.10 + R() * 0.2})`); bg.addColorStop(0.85, `hsla(${hue},80%,70%,${0.08 + R() * 0.12})`); bg.addColorStop(1, 'hsla(0,0%,0%,0)');
    c.fillStyle = bg; c.beginPath(); c.arc(x, y, r, 0, 6.2832); c.fill();
  }
  c.drawImage(V.renderer.domElement, 0, 0, w, h);
  // grade: gentle warm highlights, cool shadows
  c.globalCompositeOperation = 'soft-light'; c.fillStyle = warm ? 'rgba(255,190,120,0.16)' : 'rgba(150,185,255,0.14)'; c.fillRect(0, 0, w, h);
  // vignette
  c.globalCompositeOperation = 'source-over';
  const vg = c.createRadialGradient(w / 2, h * 0.48, h * 0.35, w / 2, h * 0.5, h * 0.82); vg.addColorStop(0, 'rgba(0,0,0,0)'); vg.addColorStop(1, 'rgba(0,0,0,0.62)'); c.fillStyle = vg; c.fillRect(0, 0, w, h);
  // grain
  const n = document.createElement('canvas'); n.width = n.height = 256; const nc = n.getContext('2d'), im = nc.createImageData(256, 256);
  for (let p = 0; p < im.data.length; p += 4) { const v = 128 + (R() - 0.5) * 120; im.data[p] = im.data[p + 1] = im.data[p + 2] = v; im.data[p + 3] = 255; }
  nc.putImageData(im, 0, 0); c.globalCompositeOperation = 'overlay'; c.globalAlpha = 0.14; c.fillStyle = c.createPattern(n, 'repeat'); c.fillRect(0, 0, w, h);
  c.globalAlpha = 1; c.globalCompositeOperation = 'source-over';
  // small caption bar
  c.fillStyle = 'rgba(255,255,255,0.55)'; c.font = `${Math.round(h * 0.014)}px "Avenir Next","Segoe UI",sans-serif`; c.textAlign = 'left';
  c.fillText((App.S.planName || 'Failoring').toUpperCase() + '  /  ' + shot.name.toUpperCase(), w * 0.04, h * 0.972);
  return cv;
};

App.takeShots = async () => {
  if (Sh.busy) return; if (!V.sim || !V.sim.frames.length) { App.toast('Nothing to photograph yet - wait for the drape to appear'); return; }
  const list = SHOTS.filter((s) => Sh.enabled[s.id]); if (!list.length) { App.toast('Pick at least one shot'); return; }
  Sh.busy = true; const busy = $('#shootBusy'); busy.hidden = false;
  const r = V.renderer, sc = V.scene, [w, h] = SHOOT_SIZES[Sh.size];
  const saved = { bg: sc.background, fog: sc.fog, T: V.T, playing: V.playing, key: [V.key.intensity, V.key.position.clone()], rim: [V.rim.intensity, V.rim.position.clone()], hemi: sc.children.find((o) => o.isHemisphereLight).intensity,
    aspect: V.cam.aspect, fov: V.cam.fov, exp: r.toneMappingExposure };
  const hemi = sc.children.find((o) => o.isHemisphereLight);
  V.shoot = true; V.playing = false; Sh.runway = Sh.runway || Sh.buildRunway();
  sc.background = null; sc.fog = null; r.setClearColor(0x000000, 0); V.floor.visible = false; V.matGroup.visible = false; Sh.runway.visible = true;
  V.stitches.concat(V.seamLines).forEach((o) => (o.line.visible = false)); V.noSel = true; V.recolor();
  r.setPixelRatio(1); r.setSize(w, h, false); V.cam.aspect = w / h; r.toneMappingExposure = 1.0;
  V.T = 4; V.pose(4);
  // walking: spread the shots over the last gait cycle of the simulation (frames after the stride has built up)
  const bf = V.sim.bodyFrames, walking = !!(bf && bf.some((b) => b)), last = V.sim.frames.length - 1, cyc = V.sim.walkCycle || 64;
  const H = App.S.meas.height / 1.68, out = [];
  for (let i = 0; i < list.length; i++) {
    const s = list[i]; busy.textContent = `Shooting ${i + 1} / ${list.length}: ${s.name}`; await new Promise((res) => setTimeout(res, 30));
    V.modelGroup.rotation.y = s.yaw;
    if (walking) { V.forceFrame = Sh.sameMoment ? last : Math.max(0, last - Math.round(cyc * (1 - i / list.length))); V._bodyShown = null; V.pose(4); }
    const cam = new THREE.Vector3(s.cam[0], s.cam[1] * H, s.cam[2]), tgt = new THREE.Vector3(s.tgt[0], s.tgt[1] * H, s.tgt[2]);
    V.cam.fov = s.fov; V.cam.position.copy(cam); V.cam.lookAt(tgt); V.cam.updateProjectionMatrix();
    // photographer lights follow the camera: key high and to the camera's left, fill from the right, two rims from behind the model
    const dir = tgt.clone().sub(cam).setY(0).normalize(), side = new THREE.Vector3(-dir.z, 0, dir.x);
    V.key.position.copy(cam).addScaledVector(side, -1.8).add(new THREE.Vector3(0, 1.6, 0)); V.key.target.position.copy(tgt); V.key.intensity = 2.6;
    V.rim.position.copy(tgt).addScaledVector(dir, 3).addScaledVector(side, 2).add(new THREE.Vector3(0, 1.5, 0)); V.rim.intensity = 1.8;
    hemi.intensity = 0.38;
    r.render(sc, V.cam);
    const cv = Sh.composite(s, i, w, h); out.push({ name: s.name, url: cv.toDataURL('image/png') });
  }
  // restore
  V.forceFrame = null; V._bodyShown = null;
  V.modelGroup.rotation.y = 0; V.floor.visible = true; V.matGroup.visible = true; Sh.runway.visible = false;
  sc.background = saved.bg; sc.fog = saved.fog; r.setClearColor(0x12151a, 1); V.key.intensity = saved.key[0]; V.key.position.copy(saved.key[1]); V.key.target.position.set(0, 0, 0);
  V.rim.intensity = saved.rim[0]; V.rim.position.copy(saved.rim[1]); hemi.intensity = saved.hemi; V.cam.fov = saved.fov; r.toneMappingExposure = saved.exp;
  V.stitches.concat(V.seamLines).forEach((o) => (o.line.visible = true)); V.noSel = false; V.recolor();
  r.setPixelRatio(Math.min(2, devicePixelRatio || 1)); V.shoot = false; V.T = saved.T; V.resize(); V.dirty = true;
  busy.hidden = true; Sh.busy = false; Sh.shots = out;
  $('#gInfo').textContent = `${out.length} shots - ${w} x ${h}px - ${App.S.planName}`;
  $('#gGrid').innerHTML = out.map((o, i) => `<div class="shot"><img src="${o.url}" alt="${o.name}"><div class="cap"><span>${o.name}</span><a href="${o.url}" download="${(App.S.planName || 'shot').replace(/\W+/g, '-').toLowerCase()}-${i + 1}.png">PNG</a></div></div>`).join('');
  $('#gallery').hidden = false;
};
