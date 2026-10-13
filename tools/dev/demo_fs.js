// demo_fs.js <html> <outdir> <planKey> <bodyIdx> [quality] : Demo mode on one FreeSewing garment, screenshots at three moments of the walk
const { chromium } = require('/opt/npm-tools/node_modules/playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const [file, outdir, key, bodyIdx = '0', quality = 'draft'] = process.argv.slice(2);
  fs.mkdirSync(outdir, { recursive: true });
  const b = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const p = await b.newPage({ viewport: { width: 1400, height: 860 } });
  const errors = [];
  p.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
  p.on('console', (m) => { if (m.type() === 'error') errors.push('console: ' + m.text()); });
  await p.goto('file://' + path.resolve(file));
  await p.waitForFunction(() => typeof App !== 'undefined' && App.S && App.worker && App.body, null, { timeout: 60000, polling: 500 });
  await p.evaluate((q) => { App.S.quality = q; document.querySelector('#qualSel').value = q; }, quality);
  await p.click('#btnDemo');
  await p.evaluate(([k, bi]) => { demoSt.outfit = DEMO.plans().indexOf(k); demoSt.body = +bi; DEMO.load(); }, [key, bodyIdx]);
  await p.waitForFunction(() => App.sim && App.sim.frames.length >= 3, null, { timeout: 600000, polling: 1000 });
  console.log('first frames', await p.evaluate(() => document.querySelector('#demoInfo').textContent));
  const t0 = Date.now();
  await p.waitForFunction(() => App.sim && App.sim.done, null, { timeout: 1500000, polling: 2000 });
  console.log('sim done after', ((Date.now() - t0) / 1000).toFixed(0), 's;', await p.evaluate(() => App.sim.frames.length + ' frames, ' + document.querySelector('#status').textContent));
  for (const T of [2.0, 3.2, 4.4]) {
    await p.evaluate((T) => { V.playing = false; V.T = T; V.dirty = true; }, T);
    await p.waitForTimeout(900);
    await p.screenshot({ path: path.join(outdir, key + '_b' + bodyIdx + '_T' + T + '.png'), timeout: 180000 });
  }
  console.log('errors:', errors.length ? errors.join('\n') : 'none');
  await b.close();
})().catch((e) => { console.error('FAILED', e); process.exit(1); });
