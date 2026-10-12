// Contact sheet of the walk cycle: node tools/gait_sheet.js [preset] [outdir]  (Playwright + 2D canvas, flat shaded)
'use strict';
const fs = require('fs'), path = require('path');
const root = path.join(__dirname, '..'), R = (...p) => path.join(root, ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js'));
const preset = process.argv[2] || 'average', outdir = path.resolve(process.argv[3] || R('out/gait'));
fs.mkdirSync(outdir, { recursive: true });
const M = Body.load(new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin'))));
Body.calibrate && Body.calibrate(M);
const body = Body.generate(M, FL.PRESETS[preset], null);
const N = 8, frames = [];
for (let i = 0; i < N; i++) { const t0 = Date.now(); const P = FL.Gait.pose(M, body, i / N); frames.push({ pos: Array.from(P.pos, (v) => +v.toFixed(4)), ms: Date.now() - t0 }); }
console.log('pose ms', frames.map((f) => f.ms).join(' '));
const html = `<canvas id=c></canvas><script>
const T=${JSON.stringify(Array.from(M.tris))}, F=${JSON.stringify(frames.map((f) => f.pos))};
const N=F.length, W=260, H=620, c=document.getElementById('c'); c.width=W*N; c.height=H*2; const g=c.getContext('2d');
g.fillStyle='#e9e6e0'; g.fillRect(0,0,c.width,c.height);
function draw(P, ox, oy, view){
  const sc=330, proj=(i)=>{const x=P[3*i],y=P[3*i+1],z=P[3*i+2]; return view==='side'?[ox+W/2+z*sc*-1,oy+H-30-y*sc,x]:[ox+W/2+x*sc*-1,oy+H-30-y*sc,z];};
  const tris=[]; for(let t=0;t<T.length/3;t++){const a=T[3*t],b=T[3*t+1],c2=T[3*t+2];
    const ax=P[3*a],ay=P[3*a+1],az=P[3*a+2],bx=P[3*b]-ax,by=P[3*b+1]-ay,bz=P[3*b+2]-az,cx=P[3*c2]-ax,cy=P[3*c2+1]-ay,cz=P[3*c2+2]-az;
    let nx=by*cz-bz*cy,ny=bz*cx-bx*cz,nz=bx*cy-by*cx; const l=Math.hypot(nx,ny,nz)||1; nx/=l;ny/=l;nz/=l;
    const v=view==='side'?[ -1,0,0]:[0,0,1]; // view dir toward camera
    const facing = view==='side'? nx*(1) : nz; // side camera at +x looking -x
    if(facing<=0) continue;
    const pa=proj(a),pb=proj(b),pc=proj(c2); const depth=(pa[2]+pb[2]+pc[2])/3*(view==='side'?1:1);
    const sh=Math.max(0.15, 0.5*nz+0.45*ny+0.3*nx*(view==='side'?1:0)+0.35); tris.push([depth,pa,pb,pc,sh]);}
  tris.sort((p,q)=>p[0]-q[0]);
  for(const [d,pa,pb,pc,sh] of tris){const k=Math.round(Math.min(1,sh)*225); g.fillStyle='rgb('+k+','+Math.round(k*0.93)+','+Math.round(k*0.88)+')'; g.strokeStyle=g.fillStyle; g.lineWidth=0.6; g.beginPath(); g.moveTo(pa[0],pa[1]); g.lineTo(pb[0],pb[1]); g.lineTo(pc[0],pc[1]); g.closePath(); g.fill(); g.stroke();}
  g.strokeStyle='#888'; g.beginPath(); g.moveTo(ox,oy+H-30); g.lineTo(ox+W,oy+H-30); g.stroke();
}
F.forEach((P,i)=>{draw(P,i*W,0,'side'); draw(P,i*W,H,'front'); g.fillStyle='#000'; g.font='16px sans-serif'; g.fillText('phase '+(i/N).toFixed(3),i*W+8,20);});
</script>`;
fs.writeFileSync(path.join(outdir, 'gait_sheet.html'), html);
const { chromium } = require('/opt/npm-tools/node_modules/playwright');
(async () => {
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 2100, height: 1250 } });
  await p.goto('file://' + path.join(outdir, 'gait_sheet.html')); await p.waitForTimeout(500);
  await (await p.$('#c')).screenshot({ path: path.join(outdir, 'gait_' + preset + '.png') });
  await b.close(); console.log('wrote', path.join(outdir, 'gait_' + preset + '.png'));
})();
