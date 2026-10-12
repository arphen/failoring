import json,sys,matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
fs=sys.argv[1:-1]; out=sys.argv[-1]
pcs=[]
for f in fs:
    m=json.load(open(f))
    for p in m['pieces']: pcs.append((m['design'],p))
fig,axs=plt.subplots(1,len(pcs),figsize=(4*len(pcs),6))
if len(pcs)==1: axs=[axs]
for ax,(d,p) in zip(axs,pcs):
    for i,c in enumerate(p['chains']):
        xs=[q[0] for q in c['pts']]; ys=[-q[1] for q in c['pts']]
        ax.plot(xs,ys,'-',lw=1.5); ax.text(xs[len(xs)//2],ys[len(ys)//2],f"{i}:{c.get('label','')}",fontsize=6)
    ax.set_aspect('equal'); ax.set_title(p['id']+' x'+str(p['cut']),fontsize=8)
plt.tight_layout(); plt.savefig(out,dpi=70)
