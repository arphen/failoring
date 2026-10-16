#!/usr/bin/env node
// cp_export.mjs - CrochetPARADE pattern text -> Failoring crochet JSON.
//
// This is the T4.0 spike exporter: it runs CrochetPARADE's parser headless with node and
// writes the JSON described in docs/CROCHET_PLAN.md section 5.0:
//
//   {"stitches": [{"id", "type", "piece", "row", "index", "loop", "targets", "prev", "line"}],
//    "rows":     [{"id", "piece", "kind", "turned"}],
//    "links":    [],
//    "positions":[[x, y, z], ...],
//    "errors":   [{"line", "msg"}]}
//
// Usage:
//   node tools/crochet/cp_export.mjs [--layout] [--time] [--out FILE] PATTERN.txt
//
// Options:
//   --layout   also run CrochetPARADE's 3D layout (graph64 wasm) and fill "positions"
//   --time     print {"parse_ms", "layout_ms", "stitches"} to stderr
//   --out FILE write the JSON to FILE instead of stdout
//
// CrochetPARADE itself is not vendored in this repository (third_party/crochetparade is
// gitignored; see THIRD_PARTY.md). Clone github.com/stassev/CrochetPARADE at commit
// 06e987b13ccfc94cfdfcebc1fe4664d37d2c6687, or point CP_DIR at a checkout.
//
// Mapping notes (see docs/crochet/T4.0_crochetparade.md for the full list):
//   - `sk` and `start_anew` are not stitches and are skipped; the `turn` marker is not a
//     stitch either; it becomes rows[].turned.
//   - `ring` (magic ring) is exported as {"type": "ch", "loop": "space"}.
//   - type: ch->ch, sc/scbl/scfl/fpsc/bpsc->sc, ss/ssbl/ssfl->slst.
//   - loop: sc/ss->both, *bl->blo, *fl->flo, fpsc->fpost, bpsc->bpost, ch/ring->space.
//   - targets come from the parser's resolved attachment ids (id_attach), translated to
//     exported stitch ids; `prev` is the previously exported stitch in creation order.
//   - row is CrochetPARADE's nrow; line is the 1-based source line. The parser numbers
//     rows after stripping `# ...` comments, so line maps nrow through the pattern's
//     non-comment lines (fixtures have comments only at the top, one line per row).
//   - `piece` is "main" for every stitch: CrochetPARADE has no piece construct. Turning
//     rows vs rounds is rows[].turned.
//   - `links` is empty: CrochetPARADE cannot express one stitch worked through two layers;
//     the through-both-layers seam workarounds are described in the T4.0 report.

import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, '..', '..');

function usage(exitCode) {
  // process.exit() aborts pending asynchronous writes to a pipe on POSIX, so
  // write the message synchronously before exiting (see emit()).
  fs.writeSync(2, 'usage: node tools/crochet/cp_export.mjs [--layout] [--time] [--out FILE] PATTERN.txt\n');
  process.exit(exitCode);
}

const argv = process.argv.slice(2);
let layout = false, time = false, outFile = null, patternFile = null;
for (let i = 0; i < argv.length; i++) {
  const a = argv[i];
  if (a === '--layout') layout = true;
  else if (a === '--time') time = true;
  else if (a === '--out') { outFile = argv[++i]; if (!outFile) usage(2); }
  else if (a === '--help' || a === '-h') usage(0);
  else if (a.startsWith('-')) { fs.writeSync(2, 'unknown option: ' + a + '\n'); usage(2); }
  else patternFile = a;
}
if (!patternFile) usage(2);

const cpDir = process.env.CP_DIR || path.join(repoRoot, 'third_party', 'crochetparade');
if (!fs.existsSync(path.join(cpDir, 'parse64.js'))) {
  fs.writeSync(2,
    'CrochetPARADE not found at ' + cpDir + '\n' +
    'Clone https://github.com/stassev/CrochetPARADE at commit 06e987b13ccfc94cfdfcebc1fe4664d37d2c6687\n' +
    'into third_party/crochetparade (gitignored), or set CP_DIR.\n');
  process.exit(2);
}

const text = fs.readFileSync(patternFile, 'utf8');

// nrow -> 1-based source line. The parser strips `# ...` comments (and blank lines do not
// match its row regex), so nrow counts only the pattern lines that keep content.
function buildLineMap(src) {
  const lines = src.split('\n');
  const map = [];
  for (let i = 0; i < lines.length; i++) {
    if (lines[i].split('#')[0].trim() !== '') map.push(i + 1);
  }
  return map;
}
const lineMap = buildLineMap(text);
const lineOf = (nrow) => (nrow >= 0 && nrow < lineMap.length ? lineMap[nrow] : -1);

const out = { stitches: [], rows: [], links: [], positions: [], errors: [] };

// Write the output and resolve with the exit code once it is complete.
// process.exit() must not run before then: on POSIX it aborts pending
// asynchronous writes to a pipe and truncates stdout (file writes are
// synchronous, which is why --out never showed this).
async function emit() {
  const json = JSON.stringify(out, null, 2) + '\n';
  const code = out.errors.length > 0 ? 1 : 0;
  if (outFile) {
    fs.writeFileSync(outFile, json);
    return code;
  }
  return new Promise((resolve) => process.stdout.write(json, () => resolve(code)));
}

// ---- load the parser in a vm context (parse64.js expects a browser-ish global) ----
const parseSrc = fs.readFileSync(path.join(cpDir, 'parse64.js'), 'utf8');
const parseCtx = vm.createContext({
  console: { log() {}, error: console.error, warn: console.warn },
  alert: (m) => console.error('[alert] ' + m),
});
vm.runInContext('globalThis = this;', parseCtx);
vm.runInContext(parseSrc, parseCtx, { filename: 'parse64.js' });

const tParse0 = performance.now();
let stitches;
try {
  stitches = vm.runInContext('final(' + JSON.stringify(text) + ')', parseCtx);
} catch (e) {
  const msg = String(e.message).split('\n')[0];
  const m = /"nrow":\s*(\d+)/.exec(String(e.message));
  out.errors.push({ line: m ? lineOf(Number(m[1])) : -1, msg });
  process.exit(await emit()); // stdout fully flushed; safe to exit
}
const parseMs = performance.now() - tParse0;

// ---- map parser stitches to the Failoring schema ----
const TYPE_MAP = {
  ch: ['ch', 'space'], ring: ['ch', 'space'],
  sc: ['sc', 'both'], scbl: ['sc', 'blo'], scfl: ['sc', 'flo'],
  fpsc: ['sc', 'fpost'], bpsc: ['sc', 'bpost'],
  ss: ['slst', 'both'], ssbl: ['slst', 'blo'], ssfl: ['slst', 'flo'],
  slst: ['slst', 'both'],
};
const SKIP = new Set(['sk', 'start_anew']);

const idMap = new Map(); // CrochetPARADE node id -> exported stitch id
const uids = []; // exported stitch id -> CrochetPARADE stitch uid (for layout lookup)
const rowIndex = new Map(); // nrow -> exported stitches so far

for (const s of stitches) {
  if (SKIP.has(s.type)) continue;
  const t = TYPE_MAP[s.type];
  if (!t) {
    out.errors.push({ line: lineOf(s.nrow), msg: 'unsupported stitch type "' + s.type + '" skipped' });
    for (const nid of s.id) idMap.set(nid, -1); // later targets stay explicit
    continue;
  }
  const id = out.stitches.length;
  const targets = [];
  for (const a of s.id_attach) {
    if (idMap.has(a)) targets.push(idMap.get(a));
    else { targets.push(-1); out.errors.push({ line: lineOf(s.nrow), msg: 'unresolved target id ' + a + ' on stitch ' + id }); }
  }
  const idx = rowIndex.get(s.nrow) || 0;
  rowIndex.set(s.nrow, idx + 1);
  out.stitches.push({
    id, type: t[0], piece: 'main', row: s.nrow, index: idx, loop: t[1],
    targets, prev: id - 1, line: lineOf(s.nrow),
  });
  for (const nid of s.id) idMap.set(nid, id);
  uids.push(s.uid);
}

// ---- rows: from the parser's instruction list, so `turn` detection is exact ----
const listJson = vm.runInContext(
  'JSON.stringify((LIST || []).map(function(r){return r.map(function(n){return n.contents;})}))', parseCtx);
const listRows = JSON.parse(listJson);
const exportedRows = new Set(out.stitches.map((s) => s.row));
for (let r = 0; r < listRows.length; r++) {
  if (!exportedRows.has(r)) continue;
  const turned = listRows[r].length > 0 && listRows[r][listRows[r].length - 1] === 'turn';
  out.rows.push({ id: r, piece: 'main', kind: turned ? 'row' : 'round', turned });
}

// ---- positions via CrochetPARADE's 3D layout (graph64.wasm) ----
let layoutMs = 0;
if (layout) {
  const tLayout0 = performance.now();
  try {
    const r = vm.runInContext("processText(" + JSON.stringify(text) + ", '')", parseCtx);
    const dot = r[1];
    const posByUid = await runLayout(cpDir, dot);
    for (let id = 0; id < out.stitches.length; id++) {
      const p = posByUid.get(String(uids[id]));
      if (p) out.positions.push(p);
      else {
        out.positions.push([null, null, null]);
        out.errors.push({ line: out.stitches[id].line, msg: 'no layout position for stitch ' + id });
      }
    }
  } catch (e) {
    out.errors.push({ line: -1, msg: 'layout failed: ' + String(e.message).split('\n')[0] });
  }
  layoutMs = performance.now() - tLayout0;
}

async function runLayout(cpDir, dotSimple) {
  // Load graph64.js (Emscripten) in its own vm context; it finds graph64.wasm next to
  // itself. Same approach as the T4.0 spike harness.
  const graphSrc = fs.readFileSync(path.join(cpDir, 'graph64.js'), 'utf8');
  const gctx = vm.createContext({
    console: { log() {}, error: console.error, warn: console.warn },
    process, Buffer,
    require: (m) => require(m),
    setTimeout, clearTimeout,
    performance: globalThis.performance,
    TextDecoder, TextEncoder,
    __dirname: cpDir,
    __filename: path.join(cpDir, 'graph64.js'),
  });
  vm.runInContext('globalThis = this;', gctx);
  vm.runInContext(graphSrc, gctx, { filename: 'graph64.js' });
  await new Promise((res) => {
    const M = gctx.Module;
    if (M.calledRun) return res();
    M.onRuntimeInitialized = () => res();
  });
  const raw = vm.runInContext(`(function (dot) {
    var inputLength = lengthBytesUTF8(dot);
    var inputPointer = Module._malloc(inputLength + 1);
    Module.stringToUTF8(dot, inputPointer, inputLength + 1);
    var resultPointer = Module.ccall('performLayout', 'number', ['number'], [inputPointer]);
    var result = Module.UTF8ToString(resultPointer);
    Module._free(inputPointer);
    return result;
  })(${JSON.stringify(dotSimple)})`, gctx);
  // Layout output holds {"name": "nrow,pos|uid","pos": "x,y,z"} objects (trailing comma).
  const posByUid = new Map();
  const re = /\{"name": "([^"]+)","pos": "([^"]+)"\},?/g;
  let m;
  while ((m = re.exec(raw)) !== null) {
    const uid = m[1].split('|')[1];
    // Keep the first node per stitch (the stitch's top node); later entries are its other nodes.
    if (uid !== undefined && !posByUid.has(uid)) posByUid.set(uid, m[2].split(',').map(Number));
  }
  if (posByUid.size === 0) throw new Error('empty layout output');
  return posByUid;
}

if (time) {
  console.error(JSON.stringify({
    pattern: patternFile,
    stitches: out.stitches.length,
    parse_ms: Math.round(parseMs * 100) / 100,
    layout_ms: layout ? Math.round(layoutMs * 100) / 100 : null,
  }));
}

process.exit(await emit()); // stdout fully flushed; safe to exit
