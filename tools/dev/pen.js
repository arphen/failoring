// pen.js <dump> <preset> : cloth vertices closest to the body surface (point-triangle distance), grouped by height
const fs = require('fs'), path = require('path'), root = '/home/claude/failoring';
const FL = require(root + '/app/src/shared.js'), Body = require(root + '/app/src/body.js'); require(root + '/app/src/gait.js');
const [dumpf, preset] = process.argv.slice(2);
const asset = new Uint8Array(fs.readFileSync(root + '/app/assets/body_mh.bin'));
const M = Body.load(asset), rest = Body.generate(M, FL.PRESETS[preset], null), body = FL.Gait.stand(M, rest);
const d = JSON.parse(fs.readFileSync(dumpf, 'utf8'));
const P = body.pos, T = M.tris, nt = T.length / 3;
function ptTri(p, a, b, c) { // closest point distance (Ericson)
  const ab = [b[0]-a[0],b[1]-a[1],b[2]-a[2]], ac = [c[0]-a[0],c[1]-a[1],c[2]-a[2]], ap = [p[0]-a[0],p[1]-a[1],p[2]-a[2]];
  const dot = (u,v)=>u[0]*v[0]+u[1]*v[1]+u[2]*v[2];
  const d1=dot(ab,ap), d2=dot(ac,ap); if (d1<=0&&d2<=0) return Math.sqrt(dot(ap,ap));
  const bp=[p[0]-b[0],p[1]-b[1],p[2]-b[2]], d3=dot(ab,bp), d4=dot(ac,bp); if (d3>=0&&d4<=d3) return Math.sqrt(dot(bp,bp));
  const vc=d1*d4-d3*d2; if (vc<=0&&d1>=0&&d3<=0){const v=d1/(d1-d3); const q=[a[0]+v*ab[0]-p[0],a[1]+v*ab[1]-p[1],a[2]+v*ab[2]-p[2]]; return Math.sqrt(dot(q,q));}
  const cp=[p[0]-c[0],p[1]-c[1],p[2]-c[2]], d5=dot(ab,cp), d6=dot(ac,cp); if (d6>=0&&d5<=d6) return Math.sqrt(dot(cp,cp));
  const vb=d5*d2-d1*d6; if (vb<=0&&d2>=0&&d6<=0){const w=d2/(d2-d6); const q=[a[0]+w*ac[0]-p[0],a[1]+w*ac[1]-p[1],a[2]+w*ac[2]-p[2]]; return Math.sqrt(dot(q,q));}
  const va=d3*d6-d5*d4; if (va<=0&&(d4-d3)>=0&&(d5-d6)>=0){const w=(d4-d3)/((d4-d3)+(d5-d6)); const q=[b[0]+w*(c[0]-b[0])-p[0],b[1]+w*(c[1]-b[1])-p[1],b[2]+w*(c[2]-b[2])-p[2]]; return Math.sqrt(dot(q,q));}
  const den=1/(va+vb+vc), v=vb*den, w=vc*den; const q=[a[0]+ab[0]*v+ac[0]*w-p[0],a[1]+ab[1]*v+ac[1]*w-p[1],a[2]+ab[2]*v+ac[2]*w-p[2]]; return Math.sqrt(dot(q,q));
}
const V = (i) => [P[3*i],P[3*i+1],P[3*i+2]];
const tri = []; for (let t = 0; t < nt; t++) { const a=V(T[3*t]), b=V(T[3*t+1]), c=V(T[3*t+2]); tri.push({a,b,c,lo:[Math.min(a[0],b[0],c[0]),Math.min(a[1],b[1],c[1]),Math.min(a[2],b[2],c[2])],hi:[Math.max(a[0],b[0],c[0]),Math.max(a[1],b[1],c[1]),Math.max(a[2],b[2],c[2])]}); }
const n = d.n, out = [];
for (let i = 0; i < n; i++) {
  const p = [d.pos[3*i], d.pos[3*i+1], d.pos[3*i+2]]; let best = 1e9;
  for (const t of tri) { if (p[0]<t.lo[0]-best||p[0]>t.hi[0]+best||p[1]<t.lo[1]-best||p[1]>t.hi[1]+best||p[2]<t.lo[2]-best||p[2]>t.hi[2]+best) continue; best = Math.min(best, ptTri(p,t.a,t.b,t.c)); }
  out.push({ i, d: best, p });
}
out.sort((a,b)=>a.d-b.d);
console.log('closest 25 (unsigned distance mm, x y z):');
for (const o of out.slice(0,25)) console.log((o.d*1000).toFixed(1), o.p.map(v=>(v*100).toFixed(1)).join(' '), 'piece', (d.ranges.findIndex((r)=>o.i>=r.vFirst&&o.i<r.vFirst+r.vCount)));
console.log('ranges sample', JSON.stringify(d.ranges[0]));
