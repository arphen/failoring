// Browser check for Demo mode: click Demo, wait for frames, screenshot three moments, then Next outfit.
// node tools/test_demo.js <html> <outdir> [quality]
const { chromium } = require('/opt/npm-tools/node_modules/playwright');
const fs = require('fs');
const path = require('path');
(async () => {
  const [file, outdir, quality = 'draft'] = process.argv.slice(2);
  fs.mkdirSync(outdir, { recursive: true });
  const b = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const p = await b.newPage({ viewport: { width: 1400, height: 860 } });
  const errors = [];
  p.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
  p.on('console', (m) => { if (m.type() === 'error') errors.push('console: ' + m.text()); });
  const shot = async (name) => { const f = path.join(outdir, name + '.png'); await p.screenshot({ path: f, timeout: 180000 }); return f; };
  const state = () => p.evaluate(() => ({
    demo: !document.querySelector('#demoBar').hidden, info: document.querySelector('#demoInfo').textContent,
    plan: document.querySelector('#planSel').value, motion: App.S.motion, meas: App.S.meas.height,
    T: +V.T.toFixed(2), playing: V.playing, yaw: +V.yaw.toFixed(3), frames: App.sim ? App.sim.frames.length : 0,
    target: App.sim ? App.sim.target : 0, done: !!(App.sim && App.sim.done), status: document.querySelector('#status').textContent,
  }));

  await p.goto('file://' + path.resolve(file));
  await p.waitForFunction(() => typeof App !== 'undefined' && App.S && App.worker && App.body, null, { timeout: 60000, polling: 500 });
  await p.evaluate((q) => { App.S.quality = q; document.querySelector('#qualSel').value = q; }, quality);
  const beforeYaw = await p.evaluate(() => V.yaw);
  await p.click('#btnDemo');
  const garments = await p.evaluate(() => DEMO.plans());
  console.log('garments in cycle:', garments.join(', '));

  // moment 1: frames have started to arrive
  await p.waitForFunction(() => App.sim && App.sim.frames.length >= 3, null, { timeout: 600000, polling: 1000 });
  console.log('moment 1', JSON.stringify(await state()));
  console.log('screenshot', await shot('1-frames-arriving'));

  // moment 2: simulation finished, timeline in the drape stage
  await p.waitForFunction(() => App.sim && App.sim.done, null, { timeout: 900000, polling: 2000 });
  await p.evaluate(() => { V.playing = false; V.T = 3.6; V.dirty = true; });
  await p.waitForTimeout(800);
  console.log('moment 2', JSON.stringify(await state()), 'orbit yaw moved by', (await p.evaluate(() => V.yaw)) - beforeYaw);
  console.log('screenshot', await shot('2-drape-walking'));

  // moment 3: Next outfit
  const before = await p.evaluate(() => DEMO.plans()[0] && document.querySelector('#demoInfo').textContent);
  await p.click('#btnNextOutfit');
  await p.waitForFunction(() => App.sim && App.sim.frames.length >= 3, null, { timeout: 900000, polling: 1000 });
  console.log('after Next outfit', JSON.stringify(await state()), 'was:', before);
  console.log('screenshot', await shot('3-next-outfit'));

  // failure path: a simulation error must move on to another outfit
  const idx0 = await p.evaluate(() => document.querySelector('#demoInfo').textContent);
  await p.evaluate(() => App.onWorker({ type: 'error', msg: 'test error' }));
  await p.waitForTimeout(1200);
  const idx1 = await p.evaluate(() => document.querySelector('#demoInfo').textContent);
  console.log('error skip:', idx0, '->', idx1);

  await p.click('#btnStop');
  await p.waitForTimeout(500);
  console.log('after Stop', JSON.stringify(await state()), 'demo button visible:', await p.evaluate(() => !document.querySelector('#btnDemo').hidden));
  console.log('screenshot', await shot('4-stopped'));
  console.log('errors:', errors.length ? errors.join('\n') : 'none');
  await b.close();
})().catch((e) => { console.error('FAILED', e); process.exit(1); });
