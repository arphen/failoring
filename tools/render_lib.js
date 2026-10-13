// Shared by walk_sim.js and render_dump.js: a z-buffered software rasteriser that runs in headless Chromium (canvas 2D), so
// the pictures have no painter's-order artefacts (where the body shows through the cloth, it really does).
//   page({ BT, CT, F, views, PC, PAL, sc, y0, ss, W, H }) -> html string
//     BT: body triangles, CT: cloth display triangles, F: [{ i, label, cloth: positions, body: positions, CT?, PC? }] (CT/PC per cell override the shared ones),
//     PC: piece index per cloth triangle (or null)
//     views: 'front' | 'side' (from the body's left) | 'back' | 'a<degrees>' (camera azimuth from the front towards the body's left side: a45 = front three-quarter
//     from the left, a135 = back three-quarter from the left, a-45 = from the right). Front and back are true views (the body's right on the picture's left in 'front').
//     sc: pixels per metre, y0: height shown at the bottom, ss: supersampling factor, W x H: pixels of one cell
//   shoot(htmlPath, pngPath, width, height) -> screenshot of the canvas
'use strict';
const fs = require('fs'), path = require('path');
exports.PAL = ['#3b5bdb', '#e03131', '#2f9e44', '#f08c00', '#9c36b5', '#0c8599', '#868e96', '#c2255c'];
exports.page = ({ BT, CT, F, views, PC, PAL, sc, y0, ss, W, H }) => `<canvas id=c></canvas><script>
const BT=${JSON.stringify(BT)}, CT=${JSON.stringify(CT)}, F=${JSON.stringify(F)}, VIEWS=${JSON.stringify(views)}, PC=${JSON.stringify(PC)}, PAL=${JSON.stringify(PAL || exports.PAL)}, SC=${sc}, Y0=${y0}, SS=${ss || 2};
const N=F.length, W=${W}, H=${H}, c=document.getElementById('c'); c.width=W*N; c.height=H*VIEWS.length; const g=c.getContext('2d');
g.fillStyle='#e9e6e0'; g.fillRect(0,0,c.width,c.height);
const hex=(h)=>[parseInt(h.slice(1,3),16),parseInt(h.slice(3,5),16),parseInt(h.slice(5,7),16)];
const azOf=(v)=>v==='front'?0:v==='side'?90:v==='back'?180:parseFloat(v.slice(1));
function tri(buf,zb,w,h,x0,y0,d0,x1,y1,d1,x2,y2,d2,r,gg,b){
  const minx=Math.max(0,Math.floor(Math.min(x0,x1,x2))),maxx=Math.min(w-1,Math.ceil(Math.max(x0,x1,x2))),miny=Math.max(0,Math.floor(Math.min(y0,y1,y2))),maxy=Math.min(h-1,Math.ceil(Math.max(y0,y1,y2)));
  const den=(y1-y2)*(x0-x2)+(x2-x1)*(y0-y2); if(Math.abs(den)<1e-12) return; const inv=1/den;
  for(let y=miny;y<=maxy;y++){ const py=y+0.5; for(let x=minx;x<=maxx;x++){ const px=x+0.5;
    const l0=((y1-y2)*(px-x2)+(x2-x1)*(py-y2))*inv, l1=((y2-y0)*(px-x2)+(x0-x2)*(py-y2))*inv, l2=1-l0-l1;
    if(l0<-1e-3||l1<-1e-3||l2<-1e-3) continue; const z=l0*d0+l1*d1+l2*d2, k=y*w+x;
    if(z>zb[k]){ zb[k]=z; buf[4*k]=r; buf[4*k+1]=gg; buf[4*k+2]=b; buf[4*k+3]=255; } } } }
function draw(P,T,az,buf,zb,w,h,cloth,PC){ const sc=SC*SS, n=P.length/3, X=new Float32Array(n), Y=new Float32Array(n), D=new Float32Array(n), ca=Math.cos(az), sa=Math.sin(az);
  for(let i=0;i<n;i++){ const x=P[3*i],y=P[3*i+1],z=P[3*i+2]; Y[i]=h-20*SS-(y-Y0)*sc; X[i]=w/2+(x*ca-z*sa)*sc; D[i]=z*ca+x*sa; }
  for(let t=0;t<T.length/3;t++){ const a=T[3*t],b=T[3*t+1],d=T[3*t+2];
    const ax=P[3*a],ay=P[3*a+1],az0=P[3*a+2],bx=P[3*b]-ax,by=P[3*b+1]-ay,bz=P[3*b+2]-az0,cx=P[3*d]-ax,cy=P[3*d+1]-ay,cz=P[3*d+2]-az0;
    let nx=by*cz-bz*cy,ny=bz*cx-bx*cz,nz=bx*cy-by*cx; const l=Math.hypot(nx,ny,nz)||1; nx/=l;ny/=l;nz/=l;
    const f=nx*sa+nz*ca; if(!cloth&&f<=0) continue;
    const sh=Math.max(0.18,Math.min(1,0.5*Math.abs(nz*ca+nx*sa)+0.4*ny+0.25*Math.abs(nx*ca-nz*sa)+0.3)); let r,gg,bb;
    if(cloth){ const pi=PC?PC[t]:-1; if(pi>=0){ const hh=hex(PAL[pi%PAL.length]), s=sh*(f>0?1:0.65); r=hh[0]*s; gg=hh[1]*s; bb=hh[2]*s; } else { const k=sh*225; r=k*0.35; gg=k*0.45; bb=k*0.8; } }
    else { const k=sh*225; r=k; gg=k*0.93; bb=k*0.88; }
    tri(buf,zb,w,h,X[a],Y[a],D[a],X[b],Y[b],D[b],X[d],Y[d],D[d],r,gg,bb); } }
F.forEach((fr,i)=>{ VIEWS.forEach((view,vi)=>{ const w=W*SS,h=H*SS, buf=new Uint8ClampedArray(w*h*4), zb=new Float32Array(w*h).fill(-1e9), az=azOf(view)*Math.PI/180;
  for(let k=0;k<w*h;k++){buf[4*k]=0xe9;buf[4*k+1]=0xe6;buf[4*k+2]=0xe0;buf[4*k+3]=255;}
  draw(fr.body,BT,az,buf,zb,w,h,false,null); draw(fr.cloth,fr.CT||CT,az,buf,zb,w,h,true,fr.PC||PC);
  const im=g.createImageData(W,H), s2=SS*SS;
  for(let y=0;y<H;y++)for(let x=0;x<W;x++){ let r=0,gg=0,b=0; for(let v=0;v<SS;v++)for(let u=0;u<SS;u++){ const k=((y*SS+v)*w+x*SS+u)*4; r+=buf[k];gg+=buf[k+1];b+=buf[k+2]; } const o=(y*W+x)*4; im.data[o]=r/s2;im.data[o+1]=gg/s2;im.data[o+2]=b/s2;im.data[o+3]=255; }
  g.putImageData(im,i*W,vi*H); g.fillStyle='#000'; g.font='16px sans-serif'; g.fillText(view+(fr.label?' '+fr.label:' frame '+fr.i),i*W+8,vi*H+20); }); });
</script>`;
exports.shoot = async (htmlPath, pngPath, width, height) => {
  const { chromium } = require('/opt/npm-tools/node_modules/playwright');
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: Math.max(width + 100, 800), height: height + 100 } });
  await p.goto('file://' + path.resolve(htmlPath)); await p.waitForTimeout(500);
  await (await p.$('#c')).screenshot({ path: pngPath }); await b.close();
};
