// Playwright check of the Pattern tab (app/src/info.js). Loads a built failoring.html, shows a FreeSewing plan that carries
// plan.info (from tools/fs_plan.js) and then a hand-drafted plan, checks the panel contents and a piece-row click, and
// screenshots both.
// usage: node tools/test_info_panel.js <failoring.html> <plan-with-info.json> <out-dir>
'use strict';
const fs = require('fs'), path = require('path');
const { chromium } = require('/opt/npm-tools/node_modules/playwright');
const [html, planFile, outDir] = process.argv.slice(2);
if (!html || !planFile || !outDir) { console.error('usage: node tools/test_info_panel.js <failoring.html> <plan-with-info.json> <out-dir>'); process.exit(2); }
const plan = JSON.parse(fs.readFileSync(planFile, 'utf8'));
const info = plan.info;
const fails = [];
const check = (ok, msg) => { console.log((ok ? 'ok    ' : 'FAIL  ') + msg); if (!ok) fails.push(msg); };

// expected values come from the plan file, not from the panel
const roles = [...new Set(info.pieces.map((q) => q.role))];
const pairKeys = [...new Set(info.seams.map((s) => (s.a === s.b ? 'self|' + s.a : [s.a, s.b].sort().join('|'))))];
const measCount = Object.values(info.measurements).filter((v) => typeof v === 'number').length;

(async () => {
  fs.mkdirSync(outDir, { recursive: true });
  const browser = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  try {
    const page = await browser.newPage({ viewport: { width: 1400, height: 860 } });
    const errors = [];
    page.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
    page.on('console', (m) => { if (m.type() === 'error') errors.push('console: ' + m.text()); });
    await page.goto('file://' + path.resolve(html));
    // App is a top-level const (not a window property), so it is reached by name
    await page.waitForFunction(() => typeof App !== 'undefined' && App.showInfo && App.S && App.S.pieces.length, null, { timeout: 120000 });

    // 1. FreeSewing plan with info
    await page.evaluate((p) => App.showInfo(p), plan);
    await page.waitForFunction(() => document.querySelector('#tab-info').textContent.includes('Cutting list'), null, { timeout: 20000 });
    const f = await page.evaluate(() => {
      const t = document.querySelector('#tab-info');
      return {
        active: document.querySelector('#tabs button.on').dataset.tab,
        title: t.querySelector('.infoTitle').textContent,
        links: [...t.querySelectorAll('a')].map((a) => a.href),
        cutRows: [...t.querySelectorAll('tr.infoRow')].map((r) => r.dataset.sel),
        seamHeads: [...t.querySelectorAll('tr.grpHead b')].map((b) => b.textContent),
        ease: [...t.querySelectorAll('td.ease')].map((x) => x.textContent),
        steps: [...t.querySelectorAll('ol.order li')].map((x) => x.textContent),
        measRows: t.querySelectorAll('table.tbl').length,
        text: t.textContent,
      };
    });
    check(f.active === 'info', 'Pattern tab is the active tab');
    check(f.title === info.title, `title shows "${info.title}"`);
    check(f.links.includes(info.url), `link to the design page ${info.url}`);
    check(/Pattern by .*FreeSewing.* \(MIT licence\)/.test(f.text), 'credit line "Pattern by FreeSewing (MIT licence)"');
    check(f.cutRows.length === roles.length, `cutting list has one row per part (${roles.length}): got ${f.cutRows.length}`);
    check(f.seamHeads.length === pairKeys.length, `seams grouped by piece pair (${pairKeys.length} groups): got ${f.seamHeads.length}`);
    check(f.steps.length > 1, `sewing order has steps (${f.steps.length})`);
    check(f.steps.length > 0 && /^Join Front and Back at the side seam \(88\.0 cm in 2 seams\)/.test(f.steps[0]), `first step joins the longest seam: "${f.steps[0]}"`);
    check(/derived automatically|worked out automatically/i.test(f.text), 'panel says the order is derived automatically');
    check(f.text.includes('Measurements') && measCount > 0, `measurement set shown (${measCount} values)`);
    console.log('       ease notes: ' + (f.ease.join(' | ') || 'none'));
    console.log('       first steps: ' + f.steps.slice(0, 3).join(' || '));

    // piece-row click selects the piece on the cutting table
    await page.click('#tab-info tr.infoRow[data-sel="sleeve-L"]');
    const sel = await page.evaluate(() => ({ sel: App.S.sel, on: document.querySelector('#tab-info tr.infoRow[data-sel="sleeve-L"]').classList.contains('on') }));
    check(sel.sel === 'sleeve-L' && sel.on, `clicking the Sleeve row selects sleeve-L (sel=${sel.sel}, row highlighted=${sel.on})`);
    await page.screenshot({ path: path.join(outDir, 'info_panel.png') });

    // 2. hand-drafted plan without info
    await page.evaluate(() => App.showInfo(PLANS.pencil));
    await page.waitForFunction(() => document.querySelector('#tab-info').textContent.includes('Hand-drafted'), null, { timeout: 20000 });
    const g = await page.evaluate(() => {
      const t = document.querySelector('#tab-info');
      return { cutting: t.textContent.includes('Cutting list'), rows: [...t.querySelectorAll('tr.infoRow')].map((r) => r.innerText.replace(/\s+/g, ' ')) };
    });
    check(!g.cutting, 'hand-drafted plan shows no FreeSewing cutting list');
    check(g.rows.length === 2, `hand-drafted plan lists its 2 pieces: got ${g.rows.length} (${g.rows.join(' | ')})`);
    await page.screenshot({ path: path.join(outDir, 'info_generic.png') });

    // 3. ease branch: the sleeve-to-back seam made 10% longer on the back side (the real plan has no seam over 3%)
    const eased = JSON.parse(JSON.stringify(plan));
    for (const s of eased.info.seams) if (s.a === 'sleeve-L' && s.b === 'back') s.lb = +(s.la * 1.1).toFixed(2);
    await page.evaluate((p) => App.showInfo(p), eased);
    await page.waitForFunction(() => document.querySelector('#tab-info').textContent.includes('Cutting list'), null, { timeout: 20000 });
    const e = await page.evaluate(() => ({
      ease: [...document.querySelectorAll('#tab-info td.ease')].map((x) => x.textContent),
      steps: [...document.querySelectorAll('#tab-info ol.order li')].map((x) => x.textContent),
    }));
    check(e.ease.some((t) => /^ease: \d+\.\d cm gathered/.test(t)), `ease note appears when lengths differ by more than 3%: "${e.ease.join(' | ')}"`);
    check(e.steps.some((t) => /Ease: \d+\.\d cm gathered into the shorter side/.test(t)), 'sewing step mentions the ease');

    check(errors.length === 0, 'no page errors' + (errors.length ? ': ' + errors.slice(0, 5).join(' / ') : ''));
  } finally {
    await browser.close();
  }
  console.log(fails.length ? `${fails.length} check(s) failed` : 'all checks passed');
  process.exitCode = fails.length ? 1 : 0;
})().catch((e) => { console.error(e); process.exitCode = 1; });
