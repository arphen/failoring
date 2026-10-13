// speed.js <dump> : where the cloth still moves (posPrev -> pos), by piece and height band
const fs = require('fs');
const d = JSON.parse(fs.readFileSync(process.argv[2], 'utf8')), n = d.n;
const pieceOf = (i) => d.ranges.findIndex((r) => i >= r.first && i < r.first + r.count);
const rows = [];
for (let i = 0; i < n; i++) {
  const dx = d.pos[3*i]-d.posPrev[3*i], dy = d.pos[3*i+1]-d.posPrev[3*i+1], dz = d.pos[3*i+2]-d.posPrev[3*i+2];
  rows.push({ i, s: Math.hypot(dx,dy,dz)*1000, p: [d.pos[3*i], d.pos[3*i+1], d.pos[3*i+2]], pc: pieceOf(i), v: [dx*1000,dy*1000,dz*1000] });
}
const mean = rows.reduce((a,r)=>a+r.s,0)/n; console.log('n', n, 'mean speed mm/frame', mean.toFixed(2));
const byPc = {}; for (const r of rows) { (byPc[r.pc] = byPc[r.pc] || []).push(r.s); }
for (const k of Object.keys(byPc)) { const a = byPc[k]; console.log('piece', k, d.ids[k], 'n', a.length, 'mean', (a.reduce((x,y)=>x+y,0)/a.length).toFixed(2), 'max', Math.max(...a).toFixed(2)); }
rows.sort((a,b)=>b.s-a.s);
console.log('fastest 20: speed | pos cm | piece | velocity mm');
for (const r of rows.slice(0,20)) console.log(r.s.toFixed(1), r.p.map(v=>(v*100).toFixed(1)).join(' '), d.ids[r.pc], r.v.map(v=>v.toFixed(1)).join(' '));
