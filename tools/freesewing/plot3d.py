import json,sys,matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
d=json.load(open(sys.argv[1])); out=sys.argv[2]
ids=sorted(set(c['id'] for c in d)); cols=dict(zip(ids,plt.cm.tab10.colors))
fig,axs=plt.subplots(1,3,figsize=(15,7))
for ax,(ix,iy,t) in zip(axs,[(0,1,'front x-y'),(2,1,'side z-y'),(0,2,'top x-z')]):
    for c in d:
        xs=[p[ix] for p in c['P3']]; ys=[p[iy] for p in c['P3']]
        ax.plot(xs,ys,'-',color=cols[c['id']],lw=1.5); ax.text(xs[len(xs)//2],ys[len(ys)//2],f"{c['id'][:4]}{c['k']}",fontsize=6)
    ax.set_aspect('equal'); ax.set_title(t)
plt.tight_layout(); plt.savefig(out,dpi=60)
