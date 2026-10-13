// zoom_shot.js <html> <outprefix> <planKey> <bodyIdx> <quality> <T,yaw,zoom,panY;...> : Demo-loaded garment, orbit stopped, zoomed screenshots of the 3D view
const { chromium } = require('/opt/npm-tools/node_modules/playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const [file, outp, key, bodyIdx = '0', quality = 'draft', views = '3.2,0.6,0.4,-0.12'] = process.argv.slice(2);
  const b = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const p = await b.newPage({ viewport: { width: 1500, height: 1000 } });
  const errors = [];
  p.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
  await p.goto('file://' + path.resolve(file));
  await p.waitForFunction(() => typeof App !== 'undefined' && App.S && App.worker && App.body, null, { timeout: 60000, polling: 500 });
  await p.evaluate((q) => { App.S.quality = q; document.querySelector('#qualSel').value = q; }, quality);
  await p.click('#btnDemo');
  await p.evaluate(([k, bi]) => { demoSt.outfit = DEMO.plans().indexOf(k); demoSt.body = +bi; DEMO.load(); }, [key, bodyIdx]);
  await p.waitForFunction(() => App.sim && App.sim.frames.length >= 3, null, { timeout: 600000, polling: 1000 });
  await p.waitForFunction(() => App.sim && App.sim.done, null, { timeout: 1500000, polling: 2000 });
  console.log('done', await p.evaluate(() => App.sim.frames.length + ' frames, ' + document.querySelector('#status').textContent));
  await p.evaluate(() => { DEMO.orbitRate = 0; document.querySelector('#demoBar').hidden = true; });
  let k = 0;
  for (const v of views.split(';')) {
    const [T, yaw, zoom, panY] = v.split(',').map(Number);
    await p.evaluate(([T, yaw, zoom, panY]) => { V.playing = false; V.T = T; V.yaw = yaw; V.zoom = zoom; V.pan = [0, panY, 0]; V.dirty = true; }, [T, yaw, zoom, panY]);
    await p.waitForTimeout(1000);
    const f = outp + '_' + (k++) + '.png';
    await p.screenshot({ path: f, timeout: 180000, clip: { x: 575, y: 80, width: 575, height: 700 } });
    console.log('wrote', f);
  }
  console.log('errors:', errors.length ? errors.join('\n') : 'none');
  await b.close();
})().catch((e) => { console.error('FAILED', e); process.exit(1); });
