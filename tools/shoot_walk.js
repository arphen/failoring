// Browser check: walking sim + photo shoot with a different gait phase per shot.
// node tools/shoot_walk.js <html> <outdir> [plan] [preset] [quality] [shots,comma] [size]
const { chromium } = require('/opt/npm-tools/node_modules/playwright');
const fs = require('fs');
(async () => {
  const [file, outdir, plan = 'pencil', preset = 'average', quality = 'standard', shotsArg = 'front,three,profile,low,hem', size = 'preview'] = process.argv.slice(2);
  fs.mkdirSync(outdir, { recursive: true });
  const b = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const p = await b.newPage({ viewport: { width: 1500, height: 900 } });
  p.on('pageerror', (e) => console.log('pageerror:', e.message)); p.on('console', (m) => { if (m.type() === 'error') console.log('console error:', m.text()); });
  await p.goto('file://' + require('path').resolve(file));
  await p.waitForFunction(() => typeof App !== 'undefined' && App.S && App.sim && App.sim.done, null, { timeout: 120000, polling: 1000 });
  await p.evaluate(([plan, preset, quality]) => {
    App.loadPattern(PLANS[plan]); App.S.meas = Object.assign({}, FL.PRESETS[preset]); App.S.quality = quality; App.S.motion = 'walk'; App.regenBody(); V.buildBody(); App.refreshAll(); App.rebuild();
  }, [plan, preset, quality]);
  const t0 = Date.now();
  await p.waitForFunction(() => App.sim && App.sim.motionDone !== undefined || (App.sim && App.sim.done && App.sim.walkCycle), null, { timeout: 1500000, polling: 2000 });
  console.log('sim done in', ((Date.now() - t0) / 1000).toFixed(0), 's', await p.evaluate(() => App.sim.frames.length + ' frames, bodyFrames ' + App.sim.bodyFrames.filter((x) => x).length));
  await p.evaluate(([shots, size]) => { for (const k in Sh.enabled) Sh.enabled[k] = shots.split(',').includes(k); Sh.size = size; App.S.planName = App.S.planName || 'walk'; }, [shotsArg, size]);
  p.evaluate(() => App.takeShots());
  for (let k = 0; k < 600; k++) { await p.waitForTimeout(1000); const st = await p.evaluate(() => [Sh.busy, Sh.shots.length]); if (!st[0] && st[1] && k > 1) break; }
  const shots = await p.evaluate(() => Sh.shots.map((s) => ({ name: s.name, url: s.url })));
  shots.forEach((s, i) => fs.writeFileSync(`${outdir}/${plan}_${preset}_${i + 1}_${s.name.replace(/\W+/g, '-').toLowerCase()}.png`, Buffer.from(s.url.split(',')[1], 'base64')));
  console.log('shots', shots.length);
  await b.close();
})().catch((e) => { console.error(e); process.exit(1); });
