// Visual check of the mannequin for every preset:  node tools/body_sheets.js [outdir]   (default docs/images)
//
// The standing body of each FL.PRESETS entry (Body.generate, then Gait.stand: the A-pose brought down, as in
// walk_sim.js) drawn from the front, three-quarter and side, flat shaded with a painter's algorithm (triangles
// sorted by centroid depth, back faces culled), the same style as gait_sheet.js. Writes:
//   body_<preset>.png   one contact sheet per preset (front, three-quarter, side)
//   body_overview.png   all presets side by side, front view, same scale
// Each PNG is downscaled if it is over 400 KB. The painter's ordering is not a depth buffer: speckle where
// triangles nearly coincide or where the order flips is a drawing artefact, not an intersection of the mesh.
'use strict';
const fs = require('fs'), path = require('path');
const { execFileSync } = require('child_process');
const root = path.join(__dirname, '..'), R = (...p) => path.join(root, ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js'));
const outdir = path.resolve(process.argv[2] || R('docs/images'));
fs.mkdirSync(outdir, { recursive: true });
const MAX_BYTES = 400 * 1024;
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const VIEWS = [['front', 0], ['three-quarter', 45], ['side', 90]];   // camera angle about +y, 0 = from +z

// Standing body of one preset, plus the sign that makes face normals point outward (checked, not assumed).
function standing(name) {
  const body = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[name], null));
  const P = body.pos, T = body.tris, nb = P.length / 3;
  let max = 0; for (const i of T) max = Math.max(max, i);
  if (max >= nb) throw new Error(`${name}: triangle index ${max} outside ${nb} vertices`);
  let cx = 0, cy = 0, cz = 0; for (let i = 0; i < nb; i++) { cx += P[3 * i]; cy += P[3 * i + 1]; cz += P[3 * i + 2]; }
  cx /= nb; cy /= nb; cz /= nb;
  let s = 0;   // sum of n . (centroid - mean vertex) over all faces: positive when the normals point outward
  for (let t = 0; t < T.length; t += 3) {
    const a = 3 * T[t], b = 3 * T[t + 1], c = 3 * T[t + 2];
    const ux = P[b] - P[a], uy = P[b + 1] - P[a + 1], uz = P[b + 2] - P[a + 2], vx = P[c] - P[a], vy = P[c + 1] - P[a + 1], vz = P[c + 2] - P[a + 2];
    const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    const gx = (P[a] + P[b] + P[c]) / 3 - cx, gy = (P[a + 1] + P[b + 1] + P[c + 1]) / 3 - cy, gz = (P[a + 2] + P[b + 2] + P[c + 2]) / 3 - cz;
    s += nx * gx + ny * gy + nz * gz;
  }
  return { name, body, P, T, nb, flip: s < 0 ? -1 : 1, faces: T.length / 3, ms: body.ms };
}

// Polygons of one view in panel pixels (x relative to the panel centre, y from the floor), far to near.
// Returns flat rows [depth, x1,y1, x2,y2, x3,y3, shade] for the browser to fill.
function viewPolys(S, angleDeg, sc, pw, ph) {
  const { P, T, nb, flip } = S, th = angleDeg * Math.PI / 180;
  const d = [Math.sin(th), 0, Math.cos(th)], r = [Math.cos(th), 0, -Math.sin(th)];   // toward camera; screen right
  const base = ph - 30;
  let mx = 0; for (let i = 0; i < nb; i++) mx += P[3 * i] * r[0] + P[3 * i + 2] * r[2]; mx /= nb;   // centre the figure
  const X = (i) => pw / 2 + (P[3 * i] * r[0] + P[3 * i + 2] * r[2] - mx) * sc, Y = (i) => base - P[3 * i + 1] * sc;
  const rows = [];
  for (let t = 0; t < T.length; t += 3) {
    const a = 3 * T[t], b = 3 * T[t + 1], c = 3 * T[t + 2];
    const ux = P[b] - P[a], uy = P[b + 1] - P[a + 1], uz = P[b + 2] - P[a + 2], vx = P[c] - P[a], vy = P[c + 1] - P[a + 1], vz = P[c + 2] - P[a + 2];
    let nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx; const l = Math.hypot(nx, ny, nz) || 1;
    nx = flip * nx / l; ny = flip * ny / l; nz = flip * nz / l;
    const facing = nx * d[0] + ny * d[1] + nz * d[2];
    if (facing <= 0) continue;   // back face
    const depth = ((P[a] + P[b] + P[c]) * d[0] + (P[a + 2] + P[b + 2] + P[c + 2]) * d[2]) / 3;
    const k = Math.round(Math.min(1, 0.32 + 0.5 * facing + 0.12 * Math.max(0, ny)) * 225);
    const ia = T[t], ib = T[t + 1], ic = T[t + 2];
    rows.push([depth, +X(ia).toFixed(1), +Y(ia).toFixed(1), +X(ib).toFixed(1), +Y(ib).toFixed(1), +X(ic).toFixed(1), +Y(ic).toFixed(1), k]);
  }
  rows.sort((p, q) => p[0] - q[0]);
  return rows.map((p) => p.slice(1));
}

// Browser side: fill the polygons of each panel into one canvas.
const PAGE_SCRIPT = `(() => {
const SHEET = __SHEET__;
const c = document.getElementById('c'); c.width = SHEET.W; c.height = SHEET.H; const g = c.getContext('2d');
g.fillStyle = '#e9e6e0'; g.fillRect(0, 0, c.width, c.height);
g.fillStyle = '#222'; g.font = 'bold 18px sans-serif'; g.fillText(SHEET.title, 12, 28);
for (const p of SHEET.panels) {
  g.save(); g.beginPath(); g.rect(p.ox, p.oy, p.pw, p.ph); g.clip();
  for (const row of p.polys) {
    const k = row[6], col = 'rgb(' + k + ',' + Math.round(k * 0.93) + ',' + Math.round(k * 0.88) + ')';
    g.fillStyle = col; g.strokeStyle = col; g.lineWidth = 0.6;
    g.beginPath(); g.moveTo(p.ox + row[0], p.oy + row[1]); g.lineTo(p.ox + row[2], p.oy + row[3]); g.lineTo(p.ox + row[4], p.oy + row[5]); g.closePath(); g.fill(); g.stroke();
  }
  g.restore();
  g.strokeStyle = '#888'; g.lineWidth = 1; g.beginPath(); g.moveTo(p.ox, p.oy + p.ph - 30); g.lineTo(p.ox + p.pw, p.oy + p.ph - 30); g.stroke();
  g.fillStyle = '#000'; g.font = '15px sans-serif'; g.fillText(p.label, p.ox + 8, p.oy - 8);
}
window.DONE = true;
})();`;

async function drawSheet(browser, file, sheet) {
  const page = await browser.newPage({ viewport: { width: sheet.W, height: sheet.H } });
  await page.setContent('<!doctype html><body style="margin:0"><canvas id=c></canvas></body>');
  await page.evaluate(PAGE_SCRIPT.replace('__SHEET__', JSON.stringify(sheet)));
  await page.waitForFunction(() => window.DONE === true);
  await (await page.$('#c')).screenshot({ path: file, type: 'png' });
  await page.close();
  return fitBytes(file);
}

// Downscale a PNG until it is under MAX_BYTES (Pillow, resampled with LANCZOS).
const PY = 'import sys; from PIL import Image; p, s = sys.argv[1], float(sys.argv[2]); im = Image.open(p).convert("RGB"); ' +
  'im = im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.LANCZOS); im.save(p, optimize=True)';
function fitBytes(file) {
  for (let tries = 0; tries < 4; tries++) {
    const size = fs.statSync(file).size; if (size <= MAX_BYTES) return size;
    execFileSync('python3', ['-I', '-c', PY, file, String(Math.sqrt(MAX_BYTES * 0.9 / size))]);
  }
  return fs.statSync(file).size;
}

(async () => {
  const { chromium } = require('/opt/npm-tools/node_modules/playwright');
  const browser = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const names = Object.keys(FL.PRESETS), SC = 330, PW = 300, PH = 640, GAP = 10, TITLE = 44, LABEL = 26;
  const bodies = {};
  for (const name of names) {
    const S = standing(name); bodies[name] = S;
    const m = FL.PRESETS[name];
    const title = `${name}   height ${m.height} m   bust ${m.bust} m   waist ${m.waist} m   hip ${m.hip} m   (${S.faces} faces, flip ${S.flip}, pose ${S.ms.toFixed(0)} ms)`;
    const sheet = { W: 3 * PW + 4 * GAP, H: TITLE + LABEL + PH, title, panels: [] };
    VIEWS.forEach(([vname, ang], i) => {
      sheet.panels.push({ label: vname, ox: GAP + i * (PW + GAP), oy: TITLE + LABEL, pw: PW, ph: PH, polys: viewPolys(S, ang, SC, PW, PH) });
    });
    const file = path.join(outdir, `body_${name}.png`);
    const bytes = await drawSheet(browser, file, sheet);
    console.log(`${name}: ${file} ${(bytes / 1024).toFixed(0)} KB, ${S.faces} faces, flip ${S.flip}`);
  }
  // overview: front view of every preset, same scale, all in one row
  const OPW = 200, OPH = 470, OSC = 220, OG = 8, OT = 44, OL = 26;
  const overview = { W: names.length * OPW + (names.length + 1) * OG, H: OT + OL + OPH, title: 'all presets, front view (same scale)', panels: [] };
  names.forEach((name, i) => {
    overview.panels.push({ label: name, ox: OG + i * (OPW + OG), oy: OT + OL, pw: OPW, ph: OPH, polys: viewPolys(bodies[name], 0, OSC, OPW, OPH) });
  });
  const ofile = path.join(outdir, 'body_overview.png');
  const obytes = await drawSheet(browser, ofile, overview);
  console.log(`overview: ${ofile} ${(obytes / 1024).toFixed(0)} KB`);
  await browser.close();
})().catch((e) => { console.error(e); process.exit(1); });
