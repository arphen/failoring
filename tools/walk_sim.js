// Headless walking drape: node tools/walk_sim.js <plan> [preset] [frames] [outdir]  (env: SETTLE, CYCLE, SUB, EDGE, RAMP)
// Runs the real worker (wasm) with the posed, moving body and renders a contact sheet (side + front) of chosen frames.
'use strict';
const vm = require('vm'), fs = require('fs'), path = require('path');
const root = path.join(__dirname, '..'), R = (...p) => path.join(root, ...p);
const FL = require(R('app/src/shared.js')), Body = require(R('app/src/body.js')); require(R('app/src/gait.js')); require(R('app/src/draft.js'));
const [plan = 'pencil', preset = 'average', nframes = '120', outArg] = process.argv.slice(2);
const outdir = path.resolve(outArg || R('out/walk')); fs.mkdirSync(outdir, { recursive: true });
const PLANS = vm.runInNewContext(fs.readFileSync(R('app/src/patterns.js'), 'utf8') + ';PLANS', {});
const FABRICS = JSON.parse(fs.readFileSync(R('materials/fabrics.json'), 'utf8')).fabrics; FL.FABRICS = FABRICS;
const asset = new Uint8Array(fs.readFileSync(R('app/assets/body_mh.bin')));
const M = Body.load(asset), rest = Body.generate(M, FL.PRESETS[preset], null), body = FL.Gait.stand(M, rest);
const posted = [];
const ctx = vm.createContext({ console, performance, setTimeout, clearTimeout, Date, Math, JSON, WebAssembly, BigInt, Proxy, Promise, Error, String, Map, Set,
  Float32Array, Float64Array, Int32Array, Uint32Array, Uint8Array, Uint16Array, Int16Array, DataView, ArrayBuffer, Array, Object, Number, Infinity, NaN, isFinite, TextDecoder,
  postMessage: (m) => posted.push(m) });
const src = ['shared.js', 'body.js', 'gait.js', 'worker.js'].map((f) => fs.readFileSync(R('app/src', f), 'utf8')).join('\n');
vm.runInContext(src, ctx, { filename: 'worker.js' });
ctx.onmessage({ data: { type: 'init', wasm: new Uint8Array(fs.readFileSync(R('wasm/failoring.wasm'))).buffer, fabrics: FABRICS, bodyAsset: asset } });
const P = plan === 'file' ? JSON.parse(fs.readFileSync(process.env.PLANFILE, 'utf8')) : plan === 'pants' ? FL.draft.pants(body) : plan === 'shirt' ? FL.draft.shirt(body) : plan === 'tank' ? FL.draft.tank(body) : JSON.parse(JSON.stringify(PLANS[plan])); P.pieces.forEach(FL.normalizePiece);
const fab = FL.fabricById(process.env.FABRIC || P.fabric);
const q = { edge: +(process.env.EDGE || 0.04), sub: +(process.env.SUB || 2), newton: 8, frames: +nframes };
const settle = +(process.env.SETTLE || 40), cycle = +(process.env.CYCLE || 64);
const spec = { job: 1, meas: FL.PRESETS[preset], fabric: fab, quality: q, rampFrames: +(process.env.RAMPF || 20), seamStiffness: 20000, selfCollision: false, frames: +nframes,
  pieces: P.pieces.map((p) => ({ id: p.id, outline: p.outline, grain: p.grain, wrap: p.wrap })), seams: P.seams,
  body: { key: body.key, pos: body.pos.slice(), tris: Int32Array.from(M.tris), sections: body.sections, arms: body.arms },
  walk: process.env.STAND ? null : { rest: rest.allPos, joints: rest.joints, settle, cycle, ramp: +(process.env.RAMP || 24), stride: 1 } };
const t0 = Date.now();
ctx.onmessage({ data: { type: 'build', spec } });
const iv = setInterval(() => {
  const err = posted.find((m) => m.type === 'error'); if (err) { console.error(err.msg); process.exit(1); }
  const fr = posted.filter((m) => m.type === 'frame').length; process.stdout.write(`\rframes ${fr}/${nframes}`);
  if (posted.some((m) => m.type === 'done')) { clearInterval(iv); finish(); }
}, 500);
function finish() {
  console.log(`\nsim ${((Date.now() - t0) / 1000).toFixed(0)} s`);
  const meta = posted.find((m) => m.type === 'meta'), frames = posted.filter((m) => m.type === 'frame');
  const bakes = frames.filter((f) => f.bake).map((f) => f.bake), ms = frames.map((f) => f.ms).filter((v) => v != null);
  console.log('bake ms avg', (bakes.reduce((a, b) => a + b, 0) / (bakes.length || 1)).toFixed(0), ' step ms avg', (ms.reduce((a, b) => a + b, 0) / (ms.length || 1)).toFixed(0));
  // is the cloth at rest? mean vertex speed (m per frame) over the last frames, seam gap and clearance of the last frame
  { const nv = meta.n, last = frames.length - 1, k = Math.min(10, last), sp = [];
    for (let f = last - k + 1; f <= last; f++) { let s = 0; for (let i = 0; i < nv; i++) s += Math.hypot(frames[f].pos[3 * i] - frames[f - 1].pos[3 * i], frames[f].pos[3 * i + 1] - frames[f - 1].pos[3 * i + 1], frames[f].pos[3 * i + 2] - frames[f - 1].pos[3 * i + 2]); sp.push(s / nv); }
    const fl = frames[last];
    console.log('settle: mean vertex step (mm/frame) over last ' + k + ' frames ' + sp.map((v) => (v * 1000).toFixed(2)).join(' ') + ' | ke ' + (fl.ke == null ? 'n/a' : (+fl.ke).toExponential(2)) + ' gap ' + (fl.gap == null ? 'n/a' : (+fl.gap * 1000).toFixed(1) + 'mm') + ' clear ' + (fl.clear == null ? 'n/a' : (+fl.clear * 1000).toFixed(1) + 'mm')); }
  if (process.env.DUMP) fs.writeFileSync(process.env.DUMP, JSON.stringify({ pos0: Array.from(frames[0].pos), pos: Array.from(frames[frames.length - 1].pos), n: meta.n, nd: meta.nd, uv: Array.from(meta.uv), ranges: meta.ranges, chains: meta.chains, seams: P.seams, ids: P.pieces.map((p) => p.id) }));
  const pick = []; const n = 8, k0 = settle + 1, span = Math.min(cycle, frames.length - k0);
  if (process.env.PICK) process.env.PICK.split(',').forEach((v) => pick.push(Math.min(frames.length - 1, +v)));   // PICK=1,10,30,70: explicit frame indices
  else for (let i = 0; i < n; i++) pick.push(Math.min(frames.length - 1, k0 + Math.round(i * (span - 1) / (n - 1))));
  const sel = pick.map((i) => ({ i, cloth: Array.from(frames[i].pos, (v) => +v.toFixed(4)), body: Array.from(frames[i].body || body.pos, (v) => +v.toFixed(4)) }));
  // env: VIEWS=side,front,back  COLORBY=piece  YR=y0,y1 (metres shown, default whole body)  FW=frame width px
  const views = (process.env.VIEWS || 'side,front').split(','), yr = (process.env.YR || '0,1.9').split(',').map(Number), fw = +(process.env.FW || 300), fh = 640;
  const sc = (fh - 40) / (yr[1] - yr[0]), pal = ['#3b5bdb', '#e03131', '#2f9e44', '#f08c00', '#9c36b5', '#0c8599', '#868e96', '#c2255c'];
  let pcOf = null;   // piece index per display triangle (display tris come four per simulation triangle)
  if (process.env.COLORBY === 'piece' && meta.dtris.length === 4 * (meta.tris ? meta.tris.length : meta.dtris.length / 4)) { pcOf = new Int8Array(meta.dtris.length / 3); meta.ranges.forEach((r, k) => { for (let s = r.triFirst; s < r.triFirst + r.triCount; s++) for (let q = 0; q < 4; q++) pcOf[4 * s + q] = k; }); }
  const html = `<canvas id=c></canvas><script>
const BT=${JSON.stringify(Array.from(M.tris))}, CT=${JSON.stringify(Array.from(meta.dtris))}, F=${JSON.stringify(sel)}, VIEWS=${JSON.stringify(views)}, PC=${JSON.stringify(pcOf ? Array.from(pcOf) : null)}, PAL=${JSON.stringify(pal)}, SC=${sc}, Y0=${yr[0]};
const N=F.length, W=${fw}, H=${fh}, c=document.getElementById('c'); c.width=W*N; c.height=H*VIEWS.length; const g=c.getContext('2d');
g.fillStyle='#e9e6e0'; g.fillRect(0,0,c.width,c.height);
const hex=(h)=>[parseInt(h.slice(1,3),16),parseInt(h.slice(3,5),16),parseInt(h.slice(5,7),16)];
function collect(P,T,view,ox,oy,col,out){ const sc=SC; const yy=(i)=>oy+H-20-(P[3*i+1]-Y0)*sc;
  const pr=(i)=>view==='side'?[ox+W/2-P[3*i+2]*sc,yy(i),P[3*i]]:view==='back'?[ox+W/2+P[3*i]*sc,yy(i),-P[3*i+2]]:[ox+W/2-P[3*i]*sc,yy(i),P[3*i+2]];
  for(let t=0;t<T.length/3;t++){const a=T[3*t],b=T[3*t+1],d=T[3*t+2];
    const ax=P[3*a],ay=P[3*a+1],az=P[3*a+2],bx=P[3*b]-ax,by=P[3*b+1]-ay,bz=P[3*b+2]-az,cx=P[3*d]-ax,cy=P[3*d+1]-ay,cz=P[3*d+2]-az;
    let nx=by*cz-bz*cy,ny=bz*cx-bx*cz,nz=bx*cy-by*cx; const l=Math.hypot(nx,ny,nz)||1; nx/=l;ny/=l;nz/=l;
    const f=view==='side'?nx:view==='back'?-nz:nz; const pa=pr(a),pb=pr(b),pd=pr(d);
    const sh=Math.max(0.18,Math.min(1,0.5*Math.abs(nz)+0.4*ny+0.25*Math.abs(nx)+0.3)); if(f<=0 && col!=='cloth') continue;
    out.push([(pa[2]+pb[2]+pd[2])/3,pa,pb,pd,sh,col,f,col==='cloth'&&PC?PC[t]:-1]);}}
F.forEach((fr,i)=>{ VIEWS.forEach((view,vi)=>{ g.save(); g.beginPath(); g.rect(i*W,vi*H,W,H); g.clip(); const out=[]; collect(fr.body,BT,view,i*W,vi*H,'body',out); collect(fr.cloth,CT,view,i*W,vi*H,'cloth',out);
  out.sort((p,q)=>p[0]-q[0]);
  for(const [d,pa,pb,pd,sh,col,f,pi] of out){ const k=Math.round(sh*225);
    if(col==='cloth'){ if(pi>=0){ const h=hex(PAL[pi%PAL.length]), s=sh*(f>0?1:0.65); g.fillStyle='rgb('+Math.round(h[0]*s)+','+Math.round(h[1]*s)+','+Math.round(h[2]*s)+')'; } else g.fillStyle='rgb('+Math.round(k*0.35)+','+Math.round(k*0.45)+','+Math.round(k*0.8)+')'; }
    else g.fillStyle='rgb('+k+','+Math.round(k*0.93)+','+Math.round(k*0.88)+')';
    g.strokeStyle=g.fillStyle; g.lineWidth=0.6; g.beginPath(); g.moveTo(pa[0],pa[1]); g.lineTo(pb[0],pb[1]); g.lineTo(pd[0],pd[1]); g.closePath(); g.fill(); g.stroke(); }
  g.fillStyle='#000'; g.font='16px sans-serif'; g.fillText(view+' frame '+fr.i,i*W+8,vi*H+20); g.restore(); }); });
</script>`;
  fs.writeFileSync(path.join(outdir, `walk_${plan}_${preset}.html`), html);
  const { chromium } = require('/opt/npm-tools/node_modules/playwright');
  (async () => {
    const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: fw * Math.max(8, sel.length) + 100, height: fh * views.length + 100 } });
    await p.goto('file://' + path.join(outdir, `walk_${plan}_${preset}.html`)); await p.waitForTimeout(800);
    await (await p.$('#c')).screenshot({ path: path.join(outdir, `walk_${plan}_${preset}.png`) });
    await b.close(); console.log('wrote', path.join(outdir, `walk_${plan}_${preset}.png`));
    fs.writeFileSync(path.join(outdir, `walk_${plan}_${preset}.json`), JSON.stringify({ ms, bakes, ring: frames.map((f) => f.gap) }));
    process.exit(0);
  })();
}
