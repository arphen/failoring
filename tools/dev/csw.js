// csw.js : taut cross seam (CF waist -> crotch -> CB waist, draft.measures) over the gait cycle for each preset
const root = '/home/claude/failoring', fs = require('fs');
const FL = require(root + '/app/src/shared.js'), Body = require(root + '/app/src/body.js'); require(root + '/app/src/gait.js'); require(root + '/app/src/draft.js');
const M = Body.load(new Uint8Array(fs.readFileSync(root + '/app/assets/body_mh.bin')));
for (const name of Object.keys(FL.PRESETS)) {
  const rest = Body.generate(M, FL.PRESETS[name], null), st = FL.Gait.stand(M, rest), m0 = FL.draft.measures(st);
  const vals = [];
  for (let k = 0; k < 16; k++) {
    const P = FL.Gait.pose(M, rest, k / 16, { stride: 1, arms: 1, armAbduct: st.armAbduct });
    const b = { sections: Body.sections(M, P.allPos, 0.01), landmarks: {}, joints: P.joints };
    for (const [n, id] of Object.entries(M.lm)) if (typeof id === 'number') b.landmarks[n] = [P.allPos[3 * id], P.allPos[3 * id + 1], P.allPos[3 * id + 2]];
    const m = FL.draft.measures(b); vals.push([m.crossSeam, m.crossSeamFront]);
  }
  const cs = vals.map((v) => v[0]), cf = vals.map((v) => v[1]);
  console.log(name.padEnd(9), 'stand', (m0.crossSeam * 1000).toFixed(0), '/', (m0.crossSeamFront * 1000).toFixed(0), ' walk cross seam min/max', (Math.min(...cs) * 1000).toFixed(0), (Math.max(...cs) * 1000).toFixed(0), ' front', (Math.min(...cf) * 1000).toFixed(0), (Math.max(...cf) * 1000).toFixed(0), ' max/stand', (Math.max(...cs) / m0.crossSeam).toFixed(3));
}
