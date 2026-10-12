#!/usr/bin/env node
// Browser smoke test for the Failoring app (see docs/TESTING.md).
//
//   node tools/smoke_browser.js
//
// Builds the app to out/smoke/failoring.html (the tracked root failoring.html is not touched), opens it in headless
// Chromium over file://, and runs the main flows in order:
//   load -> every body preset -> one body slider -> every garment plan simulated to completion
//   -> Motion: walking (a few frames) -> Photo shoot (one preview shot) -> responsiveness.
// A step fails on a wrong result, a timeout, an uncaught page error, or any console.error during the step.
// Prints a PASS/FAIL table, saves one screenshot per step in out/smoke/, exits 1 if any step did not pass.
'use strict';
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const { pathToFileURL } = require('url');

function loadPlaywright() {
  const tries = [process.env.PLAYWRIGHT_MODULE, 'playwright', '/opt/npm-tools/node_modules/playwright'].filter(Boolean);
  for (const t of tries) { try { return require(t); } catch (e) { /* try the next location */ } }
  throw new Error('playwright not found: install it or set PLAYWRIGHT_MODULE to its path');
}

const ROOT = path.join(__dirname, '..');
const OUT = path.join(ROOT, 'out', 'smoke');
const HTML = path.join(OUT, 'failoring.html');
const CHROME_ARGS = ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
const T = { load: 150000, sim: 180000, shoot: 240000, walk: 180000 };   // generous: software GL and 2 shared cores

const rows = [];          // { name, status, secs, detail }
const errors = [];        // { kind, text } - every uncaught page error and console.error, in order
let shotN = 0;

const check = (cond, msg) => { if (!cond) throw new Error(msg); };
const slug = (s) => s.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');

function record(name, status, secs, detail) { rows.push({ name, status, secs, detail }); }

async function main() {
  const started = Date.now();

  // ---- build (to out/smoke so the tracked failoring.html stays unchanged)
  fs.mkdirSync(OUT, { recursive: true });
  for (const f of fs.readdirSync(OUT)) if (/^\d\d-.*\.png$/.test(f)) fs.unlinkSync(path.join(OUT, f));
  let t0 = Date.now();
  try {
    const log = execFileSync(process.execPath, [path.join(ROOT, 'app', 'build.js'), HTML], { encoding: 'utf8' }).trim();
    record('build', 'PASS', (Date.now() - t0) / 1000, log.replace(/^wrote .*? /, '').trim());
  } catch (e) {
    record('build', 'FAIL', (Date.now() - t0) / 1000, String(e.stderr || e.message).split('\n').slice(0, 3).join(' | '));
    return finish(started);
  }

  const { chromium } = loadPlaywright();
  const browser = await chromium.launch({ headless: true, args: CHROME_ARGS });
  let page;
  try {
    page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
    page.setDefaultTimeout(30000);
    // waits also end early on an uncaught error, so a crash does not sit out the whole timeout
    await page.addInitScript(() => { window.__pageErrs = 0; window.addEventListener('error', () => window.__pageErrs++); window.addEventListener('unhandledrejection', () => window.__pageErrs++); });
    page.on('pageerror', (e) => errors.push({ kind: 'pageerror', text: String(e && e.stack || e) }));
    page.on('console', (m) => { if (m.type() === 'error') errors.push({ kind: 'console.error', text: m.text() }); });
    page.on('crash', () => errors.push({ kind: 'crash', text: 'page crashed' }));

    // runs one step: fn returns a detail string or throws; a page error during the step fails it; screenshot after
    let loaded = false;
    const step = async (name, fn) => {
      const s0 = Date.now(), before = errors.length;
      let status = 'PASS', detail = '';
      try { detail = (await fn()) || ''; } catch (e) {
        status = 'FAIL'; detail = String(e.message || e).split('\n')[0];
        if (process.env.SMOKE_DEBUG) console.log(String(e.stack || e).split('\n').slice(0, 6).join('\n'));
      }
      const fresh = errors.slice(before);
      if (fresh.length) { status = 'FAIL'; detail += ` [${fresh.length} page error(s): ${fresh.map((x) => x.text.split('\n')[0]).join(' | ')}]`; }
      const file = path.join(OUT, `${String(++shotN).padStart(2, '0')}-${slug(name)}.png`);
      try { await page.screenshot({ path: file, timeout: 60000 }); } catch (e) { detail += ' (screenshot failed)'; }
      record(name, status, (Date.now() - s0) / 1000, detail);
      console.log(`${status.padEnd(4)} ${name} (${((Date.now() - s0) / 1000).toFixed(1)} s) ${detail}`);
      return status === 'PASS';
    };
    const skipUnlessLoaded = (name) => { record(name, 'FAIL', 0, 'skipped: load did not succeed'); console.log(`FAIL ${name}: skipped, load did not succeed`); };

    // ---- 1. load
    let plans = [], presets = [];
    loaded = await step('load', async () => {
      await page.goto(pathToFileURL(HTML).href, { waitUntil: 'load', timeout: T.load });
      await page.waitForFunction(() => /vertices|error/i.test(document.querySelector('#status').textContent),
        null, { timeout: T.load, polling: 200 });
      const info = await page.evaluate(() => ({
        status: document.querySelector('#status').textContent,
        plans: Object.keys(PLANS).map((k) => ({ key: k, name: PLANS[k].name })),
        presets: Object.keys(FL.PRESETS),
        bodyModel: !!App.bodyModel,
      }));
      check(!/error/i.test(info.status), `status shows an error: "${info.status}"`);
      check(info.bodyModel, 'body model did not load (App.bodyModel is null)');
      check(info.plans.length > 0 && info.presets.length > 0, 'no plans or presets found');
      plans = info.plans; presets = info.presets;
      return `"${info.status}"; ${plans.length} plans, ${presets.length} presets`;
    });
    if (!loaded) skipUnlessLoaded('remaining steps');
    else {
      // ---- 2. every body preset: the sliders must show the preset's values
      await step('body presets', async () => {
        await page.click('#tabs button[data-tab="body"]');
        const seen = [];
        for (const name of presets) {
          await page.click(`#presets button[data-p="${name}"]`);
          const bad = await page.evaluate((n) => {
            const P = FL.PRESETS[n], sl = (id) => +document.getElementById(id).value, miss = [];
            for (const k of ['height', 'bust', 'waist', 'hip', 'shoulder']) if (Math.abs(sl('m_' + k) - P[k] * 100) > 0.6) miss.push(`${k} ${sl('m_' + k)}`);
            if (Math.abs(sl('t_muscle') - (P.muscle == null ? 50 : P.muscle * 100)) > 0.6) miss.push('muscle');
            for (const k of ['cup', 'belly']) if (Math.abs(sl('t_' + k) - (P[k] || 0) * 100) > 0.6) miss.push(k);
            return miss;
          }, name);
          check(bad.length === 0, `preset "${name}": slider mismatch ${bad.join(', ')}`);
          seen.push(name);
        }
        return `applied ${seen.join(', ')}`;
      });

      // ---- 3. move a body slider (bust to 100 cm, muscle to 80%)
      await step('body slider', async () => {
        const r = await page.evaluate(() => {
          const set = (id, v) => { const el = document.getElementById(id); el.value = v; el.dispatchEvent(new Event('input', { bubbles: true })); };
          set('m_bust', 100); set('t_muscle', 80);
          return { bust: App.S.meas.bust, muscle: App.S.meas.muscle, out: document.getElementById('m_bust_o').textContent, bodyOk: !!App.body };
        });
        check(Math.abs(r.bust - 1.0) < 1e-9, `S.meas.bust is ${r.bust}, expected 1.0`);
        check(Math.abs(r.muscle - 0.8) < 1e-9, `S.meas.muscle is ${r.muscle}, expected 0.8`);
        check(r.bodyOk, 'App.body is null after the slider moved');
        check(r.out === '100 cm', `bust readout is "${r.out}", expected "100 cm"`);
        return `bust 100 cm, muscle 80%; body regenerated`;
      });

      // ---- 4. every garment plan, simulated to completion at Draft quality (36 frames)
      await page.selectOption('#qualSel', 'draft');
      for (const p of plans) {
        await step(`plan ${p.key}`, async () => {
          const e0 = await page.evaluate(() => window.__pageErrs);
          await page.selectOption('#planSel', p.key);
          await page.waitForFunction(
            (e0) => (App.sim && App.sim.done && App.sim.job === App.jobId) || /error/i.test(document.querySelector('#status').textContent) || window.__pageErrs > e0,
            e0, { timeout: T.sim, polling: 250 });
          const r = await page.evaluate(() => {
            const s = App.sim, st = document.querySelector('#status').textContent;
            if (!s || !s.done) return { st, ok: false };
            const last = s.frames[s.frames.length - 1];
            const finite = Array.from(last).every(Number.isFinite) && Number.isFinite(s.ke) && Number.isFinite(s.gap);
            return { st, ok: finite, frames: s.frames.length, target: s.target, ke: s.ke, gap: s.gap, n: s.meta.n, plan: App.S.planName };
          });
          check(!/error/i.test(r.st), `status shows an error: "${r.st}"`);
          check(r.ok, `simulation did not finish (status "${r.st}")`);
          check(r.frames >= r.target, `only ${r.frames} of ${r.target} frames`);
          return `${r.frames} frames, ${r.n} vertices, KE ${r.ke.toExponential(1)} J, seam gap ${(r.gap * 1000).toFixed(1)} mm`;
        });
      }

      // ---- 5. Motion: walking, a few frames
      await step('motion walking', async () => {
        const e0 = await page.evaluate(() => window.__pageErrs);
        await page.selectOption('#motionSel', 'walk');
        await page.waitForFunction(
          (e0) => window.__pageErrs > e0 || (App.S.motion === 'walk' && App.sim && App.sim.job === App.jobId && App.sim.frames.length >= 8 && App.sim.bodyFrames.filter(Boolean).length >= 8),
          e0, { timeout: T.walk, polling: 250 });
        const r = await page.evaluate(() => {
          const s = App.sim, last = s.frames[s.frames.length - 1];
          return { frames: s.frames.length, body: s.bodyFrames.filter(Boolean).length, finite: Array.from(last).every(Number.isFinite), st: document.querySelector('#status').textContent };
        });
        check(!/error/i.test(r.st), `status shows an error: "${r.st}"`);
        check(r.finite, 'walking frame contains non-finite values');
        return `${r.frames} frames (${r.body} with a posed body)`;
      });

      // ---- 6. Photo shoot: one preview-size shot of the current drape
      await step('photo shoot', async () => {
        await page.click('#tabs button[data-tab="shoot"]');
        await page.selectOption('#shotSize', 'preview');
        await page.evaluate(() => { for (const c of document.querySelectorAll('[data-shot]')) if (c.checked !== (c.dataset.shot === 'front')) c.click(); });
        const e0 = await page.evaluate(() => window.__pageErrs);
        await page.click('#shootGo');
        try {
          await page.waitForFunction((e0) => !document.getElementById('gallery').hidden || window.__pageErrs > e0, e0, { timeout: T.shoot, polling: 250 });
        } catch (e) {
          const toast = await page.evaluate(() => document.getElementById('toast').textContent);
          throw new Error(`gallery did not open${toast ? ` (toast: "${toast}")` : ''}`);
        }
        check(await page.evaluate((e0) => window.__pageErrs === e0 && !document.getElementById('gallery').hidden, e0), 'gallery did not open cleanly');
        const r = await page.evaluate(async () => {
          const im = document.querySelector('#gGrid img'); await im.decode();
          return { shots: Sh.shots.length, imgs: document.querySelectorAll('#gGrid img').length, w: im.naturalWidth, h: im.naturalHeight, busy: document.getElementById('shootBusy').hidden };
        });
        check(r.shots === 1 && r.imgs === 1, `expected 1 shot, got ${r.shots} (${r.imgs} images)`);
        check(r.w === 800 && r.h === 1000, `shot is ${r.w} x ${r.h}, expected 800 x 1000`);
        check(r.busy, 'busy indicator still visible after shooting');
        return `1 shot, ${r.w} x ${r.h}px`;
      });

      // ---- 7. responsiveness: the page answers and keeps drawing frames
      await step('responsive', async () => {
        const closed = await page.evaluate(() => { document.getElementById('gallery').hidden = true; return true; });
        check(closed, 'could not close the gallery');
        const ts = Date.now();
        await page.click('#tabs button[data-tab="piece"]');
        const clickMs = Date.now() - ts;
        const rtt0 = Date.now(); await page.evaluate(() => 1); const rttMs = Date.now() - rtt0;
        const rafs = await page.evaluate(() => new Promise((res) => {
          let n = 0; const t0 = performance.now();
          const f = () => { n++; if (performance.now() - t0 < 2000) requestAnimationFrame(f); else res(n); };
          requestAnimationFrame(f);
        }));
        check(clickMs < 15000, `tab click took ${clickMs} ms`);
        check(rttMs < 5000, `page round trip took ${rttMs} ms`);
        check(rafs >= 2, `only ${rafs} animation frames in 2 s`);
        return `tab click ${clickMs} ms, evaluate ${rttMs} ms, ${rafs} animation frames in 2 s`;
      });
    }
  } finally {
    await browser.close();
  }
  return finish(started);
}

function finish(started) {
  const failed = rows.filter((r) => r.status !== 'PASS');
  const w = Math.max(4, ...rows.map((r) => r.name.length));
  console.log('\n' + ['STEP'.padEnd(w), 'RESULT', 'SECS', 'DETAIL'].join('  '));
  for (const r of rows) console.log([r.name.padEnd(w), r.status.padEnd(6), r.secs.toFixed(1).padStart(6), r.detail].join('  '));
  console.log(`\n${rows.length - failed.length} PASS, ${failed.length} FAIL, total ${((Date.now() - started) / 1000).toFixed(0)} s`);
  if (errors.length) console.log(`page errors seen: ${errors.length} (first: ${errors[0].kind}: ${errors[0].text.split('\n')[0]})`);
  console.log(`screenshots: ${OUT}`);
  process.exitCode = failed.length ? 1 : 0;
}

main().catch((e) => { console.error(e); process.exitCode = 1; });
