// Demo mode: one click shows the app to someone who will not touch the controls.
// Body preset -> garment -> walking Motion -> timeline playback, with the 3D camera slowly orbiting.
// Next outfit / Next body cycle the lists. A garment whose simulation errors (or stalls) is skipped.
// Stop returns to normal editing: standing Motion, the cutting table, the camera at its default angle.
'use strict';
const DEMO = {
  garments: ['pencil', 'aline', 'pants', 'shirt', 'tank', 'fs-teagan', 'fs-penelope', 'fs-charlie'],   // only those present in PLANS are used
  startBody: 'average',
  orbitRate: 0.12,      // radians per second
  loopPause: 2.5,       // seconds to hold the finished drape before the playback restarts
  stallMs: 300000,      // a simulation with no 'done' after this long is skipped
};
const demoSt = { active: false, body: 0, outfit: 0, prev: 'stand', failures: 0, outfitStart: 0, pauseUntil: 0, orbitLast: 0 };

DEMO.bodies = () => Object.keys(FL.PRESETS);
DEMO.plans = () => DEMO.garments.filter((k) => PLANS[k] && Array.isArray(PLANS[k].pieces) && PLANS[k].pieces.length);
DEMO.label = () => {
  const list = DEMO.plans(), k = list[demoSt.outfit % Math.max(1, list.length)];
  $('#demoInfo').textContent = `${DEMO.bodies()[demoSt.body]} body - ${k && PLANS[k] ? PLANS[k].name : 'no garment'}`;
};

// Loads the current outfit: body preset, garment (re-drafted to that body), walking Motion. The 'load' handler
// rebuilds the simulation once, already with the walk.
DEMO.load = () => {
  if (!demoSt.active) return false;
  const list = DEMO.plans();
  if (!list.length) { App.toast('No garments are available for the demo'); DEMO.stop(); return false; }
  demoSt.outfit = ((demoSt.outfit % list.length) + list.length) % list.length;
  const key = list[demoSt.outfit];
  try {
    App.S.meas = Object.assign({}, FL.PRESETS[DEMO.bodies()[demoSt.body]]);
    App.S.motion = 'walk';
    App.regenBody(); App.draftPlans();
    // a plan may carry its own measurements; the demo keeps the chosen body preset
    const src = Object.assign({}, PLANS[key]); delete src.meas;
    $('#planSel').value = key; $('#btnDraft').hidden = !PLANS[key].draft; $('#motionSel').value = 'walk';
    App.loadPattern(src);
  } catch (e) { console.error(e); DEMO.skip('could not load ' + key); return false; }
  demoSt.outfitStart = performance.now();
  V.T = 0; V.playing = true; V.last = 0; V.dirty = true;   // V.last = 0: the first step has dt 0 (rAF timestamps can precede performance.now())
  DEMO.pauseUntil = 0; DEMO.label();
  return true;
};
// A failed outfit moves on to the next one. Gives up after a full cycle with no success in between.
DEMO.skip = (why) => {
  if (!demoSt.active) return;
  demoSt.failures++;
  if (demoSt.failures >= Math.max(1, DEMO.plans().length)) { App.toast('Demo: no garment could be simulated'); DEMO.stop(); return; }
  App.toast(`${why} - trying the next outfit`);
  demoSt.outfit++;
  setTimeout(() => { if (demoSt.active) DEMO.load(); }, 300);
};
DEMO.nextOutfit = () => { if (!demoSt.active) return; demoSt.failures = 0; demoSt.outfit++; DEMO.load(); };
DEMO.nextBody = () => { if (!demoSt.active) return; demoSt.failures = 0; demoSt.body = (demoSt.body + 1) % DEMO.bodies().length; DEMO.load(); };

// one rAF loop: camera orbit (throttled to 30 fps), hold-then-replay at the end of the timeline, stall watchdog
DEMO.tick = (now) => {
  if (!demoSt.active) return;
  requestAnimationFrame(DEMO.tick);
  if (now - demoSt.orbitLast >= 33) { V.yaw += DEMO.orbitRate * Math.min(0.2, (now - demoSt.orbitLast) / 1000); demoSt.orbitLast = now; V.dirty = true; }
  if (!V.playing && V.T >= 3.999) {
    if (!demoSt.pauseUntil) demoSt.pauseUntil = now + DEMO.loopPause * 1000;
    else if (now >= demoSt.pauseUntil) { V.T = 0; V.playing = true; V.last = 0; V.dirty = true; demoSt.pauseUntil = 0; }
  }
  if (App.sim && !App.sim.done && now - demoSt.outfitStart > DEMO.stallMs) DEMO.skip('simulation stalled');
};

DEMO.start = () => {
  if (demoSt.active) return;
  demoSt.active = true; demoSt.prev = App.S.motion || 'stand'; demoSt.failures = 0; demoSt.outfit = 0;
  demoSt.body = Math.max(0, DEMO.bodies().indexOf(DEMO.startBody));
  $('#btnDemo').hidden = true; $('#demoBar').hidden = false;
  demoSt.orbitLast = performance.now();
  DEMO.load();
  requestAnimationFrame(DEMO.tick);
};
DEMO.stop = () => {
  if (!demoSt.active) return;
  demoSt.active = false;
  $('#demoBar').hidden = true; $('#btnDemo').hidden = false;
  V.playing = false; V.T = 0; V.yaw = 0.55; V.pitch = 0.42; V.zoom = 1; V.pan = [0, 0, 0]; V.dirty = true;
  App.S.motion = demoSt.prev; App.refreshAll(); App.scheduleRebuild(50);
};

// worker hooks: a simulation error skips the outfit; a finished simulation clears the failure count
const demoBaseOnWorker = App.onWorker;
App.onWorker = (d) => {
  demoBaseOnWorker(d);
  if (!demoSt.active) return;
  if (d.type === 'error') DEMO.skip('simulation error');
  else if (d.type === 'done' && App.sim && d.job === App.sim.job) demoSt.failures = 0;
};

DEMO.init = () => {
  $('#btnDemo').onclick = () => DEMO.start();
  $('#btnStop').onclick = () => DEMO.stop();
  $('#btnNextOutfit').onclick = () => DEMO.nextOutfit();
  $('#btnNextBody').onclick = () => DEMO.nextBody();
};
window.addEventListener('DOMContentLoaded', () => DEMO.init());
