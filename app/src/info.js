// Pattern tab: sewing information for the selected plan. Read-only: it never changes the pattern.
// Plans imported from FreeSewing carry plan.info (FL.fsimport.plan): pattern title and link, cutting list, seams grouped by
// piece pair, the measurement set. Hand-drafted plans get a short summary computed from their pieces and seams.
(function () {
  'use strict';
  const esc = (s) => String(s == null ? '' : s).replace(/[&<>"']/g, (c) => '&#' + c.charCodeAt(0) + ';');
  const f1 = (x) => (Math.round(x * 10) / 10).toFixed(1);
  const words = (s) => String(s || '').replace(/([a-z])([A-Z])/g, '$1 $2').replace(/[._-]+/g, ' ').toLowerCase().trim();
  const EASE = 0.03;   // lengths that differ by more than this are reported as ease
  const CM_KEYS = ['height', 'bust', 'waist', 'hip', 'shoulder', 'arm', 'inseam', 'neck', 'thigh', 'upperarm', 'back'];

  // the plan's own info survives loadPattern (state.js drops everything but geometry)
  App.planInfo = null;
  const loadPatternPlain = App.loadPattern;
  App.loadPattern = (src, keepView) => { App.planInfo = (src && src.info) || null; return loadPatternPlain(src, keepView); };

  const nameOf = (id) => {
    const p = App.piece(id); if (p) return p.name;
    const q = App.planInfo && App.planInfo.pieces.find((x) => x.id === id); return q ? q.role || q.id : id;
  };
  const fabricName = (material) => (material && material !== 'fabric' ? material : FL.fabricById(App.S.fabric).name);

  // seams of one piece pair (both directions, several runs) as one group; a piece sewn to itself is a 'self' group.
  // byType: also split by seam type (side seam, sleeve cap, unlabelled), which is how the sewing steps are worded
  function seamGroups(seams, byType) {
    const map = new Map();
    for (const s of seams) {
      let key = s.a === s.b ? 'self|' + s.a : [s.a, s.b].sort().join('|');
      if (byType) key += '|' + words((s.labels || []).find(Boolean));
      let g = map.get(key);
      if (!g) map.set(key, g = { a: s.a, b: s.b, runs: [], la: 0, lb: 0 });
      g.runs.push(s); g.la += s.la; g.lb += s.lb;
    }
    return [...map.values()].map((g) => {
      g.diff = Math.abs(g.lb - g.la);
      g.ease = Math.min(g.la, g.lb) > 0 && g.diff > EASE * Math.min(g.la, g.lb);
      g.labels = [...new Set(g.runs.map((r) => words((r.labels || []).find(Boolean))).filter(Boolean))];
      return g;
    });
  }
  const lenText = (g) => (g.diff < 0.05 ? f1(g.la) + ' cm' : f1(g.la) + ' / ' + f1(g.lb) + ' cm');

  // suggested order, derived here from the seam graph: the longest seam first, then each piece joined to what is already sewn
  function sewingSteps(groups, pieceIds) {
    const left = groups.slice().sort((x, y) => Math.max(y.la, y.lb) - Math.max(x.la, x.lb));
    const done = new Set(), steps = [];
    while (left.length) {
      let i = left.findIndex((g) => done.has(g.a) || done.has(g.b));
      if (i < 0) i = 0;   // nothing touches the work so far: start another part
      const g = left.splice(i, 1)[0];
      const A = nameOf(g.a), B = nameOf(g.b), where = g.labels.join(', ') || 'seam';
      const len = lenText(g) + (g.runs.length > 1 ? ' in ' + g.runs.length + ' seams' : '');
      let t;
      if (g.a === g.b) t = `Sew ${A} along its ${where} (${len})`;
      else if (done.has(g.a) && done.has(g.b)) t = `Also sew ${A} to ${B} at the ${where} (${len})`;
      else t = `Join ${A} and ${B} at the ${where} (${len})`;
      if (g.ease) t += `. Ease: ${f1(g.diff)} cm gathered into the shorter side`;
      steps.push(t); done.add(g.a); done.add(g.b);
    }
    steps.push(`Finish the edges that were not joined (hems, necklines, cuffs, openings) on: ${pieceIds.map(nameOf).join(', ')}`);
    return steps;
  }

  // one cutting row per part: mirrored copies (cut 2 as a pair) and cut-on-fold halves are counted once
  function cuttingRows(info) {
    const rows = new Map();
    for (const q of info.pieces) {
      const k = q.role || q.id;
      let r = rows.get(k);
      if (!r) rows.set(k, r = { role: k, ids: [], count: 0, fold: false, mirrored: false, grain: q.grain, material: q.material });
      const note = q.cutNote || '', m = /cut (\d+)/.exec(note);
      r.ids.push(q.id);
      if (/mirrored pair/.test(note)) { r.mirrored = true; r.count = Math.max(r.count, m ? +m[1] : 2); } else r.count += q.cut || 1;
      if (/on the fold/.test(note)) r.fold = true;
    }
    return [...rows.values()];
  }

  const sectionSeams = (groups) => groups.map((g) => {
    const self = g.a === g.b, runs = g.runs.map((r) => {
      const lab = words((r.labels || []).find(Boolean)) || 'seam';
      return `<tr><td>${esc(lab)} <small>${r.n} edge${r.n > 1 ? 's' : ''}</small></td><td class="n">${f1(r.la)}${Math.abs(r.la - r.lb) >= 0.05 ? ' / ' + f1(r.lb) : ''}</td></tr>`;
    }).join('');
    const head = `<tr class="grpHead"><td colspan="2"><b>${esc(nameOf(g.a))}${self ? '' : ' &harr; ' + esc(nameOf(g.b))}</b>${self ? ' <small>own edges</small>' : ''}<br><span class="len">${lenText(g)}</span> <small>${g.runs.length} seam${g.runs.length > 1 ? 's' : ''}</small></td></tr>`;
    const ease = g.ease ? `<tr><td colspan="2" class="ease">ease: ${f1(g.diff)} cm gathered (the longer side goes into the shorter)</td></tr>` : '';
    return head + runs + ease;
  }).join('');

  function render() {
    const el = $('#tab-info'), S = App.S; if (!el || !S) return;
    const info = App.planInfo; let h = '';
    if (info) {
      const url = /^https:\/\//.test(info.url || '') ? info.url : '';
      const link = (t) => (url ? `<a href="${esc(url)}" target="_blank" rel="noopener">${t}</a>` : t);
      h += `<h4>Pattern</h4><div class="infoTitle">${esc(info.title || S.planName)}</div>
        <div class="note credit">Pattern by ${link('FreeSewing')} (MIT licence)${url ? ' &middot; ' + link('design page') : ''}</div>`;
      const rows = cuttingRows(info).map((r) => {
        const det = [`on fold: ${r.fold ? 'yes' : 'no'}`, r.mirrored ? 'mirrored pair' : '', r.grain != null ? `grain ${r.grain}°` : '', fabricName(r.material)].filter(Boolean).join(' · ');
        return `<tr class="infoRow" data-sel="${esc(r.ids[0])}"><td><b>${esc(r.role)}</b><small>${esc(det)}</small></td><td class="n">${r.count}</td></tr>`;
      }).join('');
      h += `<h4>Cutting list</h4><table class="tbl"><tr><td><small>piece</small></td><td class="n"><small>cut</small></td></tr>${rows}</table>
        <div class="note">Grain is the grainline angle from the FreeSewing draft. Check the pattern's own instructions for placement.</div>`;
      const groups = seamGroups(info.seams, false);
      h += `<h4>Seams</h4><table class="tbl">${sectionSeams(groups) || '<tr><td>No seams.</td></tr>'}</table>
        <div class="note">Lengths in cm, from the design for these measurements. They do not update if you edit the pieces.</div>`;
      h += `<h4>Suggested sewing order</h4><div class="note">Worked out automatically from the seams above, not the designer's instructions. Follow the pattern's own instructions where they differ.</div>
        <ol class="order">${sewingSteps(seamGroups(info.seams, true), info.pieces.map((q) => q.id)).map((t) => `<li>${esc(t)}</li>`).join('')}</ol>`;
      h += `<h4>Measurements</h4><table class="tbl">${Object.entries(info.measurements || {}).filter(([, v]) => typeof v === 'number')
        .map(([k, v]) => `<tr><td>${esc(words(k))}</td><td class="n">${f1(v / 10)} cm</td></tr>`).join('')}</table>
        <div class="note">The measurement set this pattern was drafted for. Shown in cm.</div>`;
    } else {
      const seams = S.seams.filter((s) => App.piece(s.a[0]) && App.piece(s.b[0]));
      const per = (id) => seams.filter((s) => s.a[0] === id || s.b[0] === id).length;
      h += `<h4>Pattern</h4><div class="infoTitle">${esc(S.planName || 'Untitled')}</div>
        <div class="note">Hand-drafted in this app, not from a published pattern. Summary computed from the pieces and seams.</div>`;
      h += `<h4>Pieces</h4><table class="tbl">${S.pieces.map((p) => {
        const bb = FL.bbox(p.outline), n = per(p.id);
        return `<tr class="infoRow" data-sel="${esc(p.id)}"><td><b>${esc(p.name)}</b><small>${n} seam${n === 1 ? '' : 's'}</small></td><td class="n">${f1((bb.x1 - bb.x0) * 100)} x ${f1((bb.y1 - bb.y0) * 100)} cm</td></tr>`;
      }).join('')}</table>
        <div class="note">${seams.length} seam${seams.length === 1 ? '' : 's'} in total. Click a piece to select it on the cutting table.</div>`;
      h += `<h4>Measurements</h4><table class="tbl">${CM_KEYS.filter((k) => k in S.meas).map((k) => {
        const v = S.meas[k];
        return `<tr><td>${esc(words(k))}</td><td class="n">${v == null ? 'auto' : f1(v * 100) + ' cm'}</td></tr>`;
      }).join('')}</table>
        <div class="note">The body measurements in use (Body tab), not a pattern set.</div>`;
    }
    el.innerHTML = h;
    highlight();
  }
  function highlight() {
    const sel = App.S && App.S.sel;
    for (const r of $$('#tab-info [data-sel]')) r.classList.toggle('on', r.dataset.sel === sel);
  }

  $('#tab-info').onclick = (e) => {
    const r = e.target.closest && e.target.closest('[data-sel]'); if (!r || !App.piece(r.dataset.sel)) return;
    App.S.sel = r.dataset.sel; App.S.selV = -1; App.changed('ui');
  };
  App.onChange((kind) => { if (kind === 'load' || kind === 'structure' || kind === 'body') render(); else if (kind === 'ui') highlight(); });
  // debug and test hook: load a plan (with or without info) and open the Pattern tab
  App.showInfo = (plan) => { App.loadPattern(plan); App.showTab('info'); };
})();
