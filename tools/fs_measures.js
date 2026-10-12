// node tools/fs_measures.js [preset] -> JSON of body measures (metres) read off the standing mannequin, for the FreeSewing export
'use strict';
const fs = require('fs'), path = require('path'), R = (...p) => path.join(__dirname, '..', ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js')); require(R('app/src/draft.js'));
const preset = process.argv[2] || 'average';
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
const body = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[preset], null));
const m = FL.draft.measures(body); let top = 0; for (let i = 1; i < body.pos.length; i += 3) top = Math.max(top, body.pos[i]);
m.height = top; const S = body.sections, J = body.joints, L = body.landmarks;
const neckRow = Math.round((L.cervicale[1] - 0.045) / S.dy); delete m.yNeckRow;
m.shoulderToShoulder = Math.abs(L['acromion.L'][0] - L['acromion.R'][0]);
m.upperArmLen = Math.hypot(...J['l-shoulder'].map((v, i) => v - J['l-elbow'][i]));
m.foreArmLen = Math.hypot(...J['l-elbow'].map((v, i) => v - J['l-hand'][i]));
console.log(JSON.stringify(m));
