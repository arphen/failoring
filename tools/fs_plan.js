// node tools/fs_plan.js <raw.json> [preset] [out.json]  -> imported plan for the preset body (also prints seam summary)
'use strict';
const fs = require('fs'), path = require('path'), R = (...p) => path.join(__dirname, '..', ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js')); require(R('app/src/fsimport.js'));
const [rawFile, preset = 'average', outFile] = process.argv.slice(2);
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const body = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[preset], null));
const plan = FL.fsimport.plan(JSON.parse(fs.readFileSync(rawFile, 'utf8')), body, { debug: !!process.env.DBG });
if (process.env.DBG) fs.writeFileSync(process.env.DBG, JSON.stringify(plan.info.dbg));
console.log(plan.name, plan.pieces.map((p) => p.id + '(' + p.outline.length + ')').join(' '), 'seams', plan.seams.length);
for (const s of plan.info.seams) console.log(' ', s.a, '<->', s.b, s.la + ' / ' + s.lb + ' cm', s.n + ' edges', s.labels.join('~'), '#' + s.ch.join('/'));
if (process.env.DBG) console.log(plan.info.cov.join('\n'));
if (outFile) fs.writeFileSync(outFile, JSON.stringify(plan));
