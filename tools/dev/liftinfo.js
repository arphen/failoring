const fs = require('fs'); const FL = require('/home/claude/failoring/app/src/shared.js'); const Body = require('/home/claude/failoring/app/src/body.js'); require('/home/claude/failoring/app/src/gait.js');
const M = Body.load(new Uint8Array(fs.readFileSync('/home/claude/failoring/app/assets/body_mh.bin')));
for (const P of (process.argv[3] || 'petite,average,tall,curvy,athletic,plus').split(',')) {
  const body = FL.Gait.stand(M, Body.generate(M, FL.PRESETS[P], null)); const yc = body.sections.yCrotch;
  const plan = JSON.parse(fs.readFileSync(process.argv[2].replace('%', P), 'utf8'));
  const out = [];
  for (const pc of plan.pieces) { const L = pc.wrap && pc.wrap.lift; if (!L) continue; let mn = 9, mx = 0; for (const t of L.y) { if (t > yc + 0.02) { const f = (L.H - yc) / (t - yc); mn = Math.min(mn, f); mx = Math.max(mx, f); } }
    out.push(`${pc.id}: stretch ${mn.toFixed(2)}..${mx.toFixed(2)} (H ${(L.H * 100).toFixed(1)}, tops ${(Math.min(...L.y) * 100).toFixed(1)}..${(Math.max(...L.y) * 100).toFixed(1)}, yc ${(yc * 100).toFixed(1)})`); }
  console.log(P.padEnd(9), out.join(' | '));
}
