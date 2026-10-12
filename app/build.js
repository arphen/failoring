// Builds one self-contained failoring.html (three.js, wasm, worker, body asset, fabrics all inlined).
const fs = require('fs'), path = require('path');
const R = (...p) => fs.readFileSync(path.join(__dirname, ...p), 'utf8');
const esc = (s) => s.replace(/<\/script/gi, '<\\/script');
const appFiles = ['shared.js', 'body.js', 'gait.js', 'draft.js', 'fsimport.js', 'patterns.js', 'state.js', 'editor2d.js', 'view3d.js', 'main.js'];
if (fs.existsSync(path.join(__dirname, 'src/shoot.js'))) appFiles.push('shoot.js');
const app = appFiles.map((f) => '// ---- ' + f + '\n' + R('src', f)).join('\n');
const worker = R('src/shared.js') + '\n' + R('src/body.js') + '\n' + R('src/gait.js') + '\n' + R('src/worker.js');
const wasm = fs.readFileSync(path.join(__dirname, '../wasm/failoring.wasm')).toString('base64');
// mannequin asset (tools/convert_makehuman.py): MakeHuman CC0 base mesh + targets, raw-deflate, decoded by body.js
const bodyAsset = fs.readFileSync(path.join(__dirname, 'assets/body_mh.bin')).toString('base64');
const fabrics = JSON.stringify(JSON.parse(fs.readFileSync(path.join(__dirname, '../materials/fabrics.json'), 'utf8')).fabrics);
// FreeSewing pre-drafted parts, one per design and preset (assets/freesewing/raw_<design>_<preset>.json): {design: {preset: raw}}, compact
const fsDir = path.join(__dirname, 'assets/freesewing'), fsRaw = {};
for (const f of fs.readdirSync(fsDir).filter((f) => /^raw_.+\.json$/.test(f)).sort()) {
  const [, design, preset] = /^raw_(.+)_(.+)\.json$/.exec(f);
  (fsRaw[design] = fsRaw[design] || {})[preset] = JSON.parse(fs.readFileSync(path.join(fsDir, f), 'utf8'));
}
const fsRawJson = JSON.stringify(fsRaw);
let html = R('src/index.html');
html = html.replace('/*CSS*/', () => R('src/style.css')).replace('<script>/*THREE*/</script>', () => '<script>' + esc(R('vendor/three.min.js')) + '</script>')
  .replace('<script>/*APP*/</script>', () => '<script id="workerSrc" type="text/plain">' + esc(worker) + '</script>\n<script id="wasmB64" type="text/plain">' + wasm + '</script>\n<script id="bodyB64" type="text/plain">' + bodyAsset + '</script>\n<script id="fabricsJson" type="application/json">' + esc(fabrics) + '</script>\n<script id="fsRaw" type="application/json">' + esc(fsRawJson) + '</script>\n<script>' + esc(app) + '</script>');
const out = process.argv[2] || path.join(__dirname, '../failoring.html');
fs.writeFileSync(out, html); console.log('wrote', out, (html.length / 1e6).toFixed(2), 'MB');
