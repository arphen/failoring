import sys
from PIL import Image
tag, out, S = sys.argv[1], sys.argv[2], sys.argv[3]
presets = ['petite', 'average', 'tall', 'curvy', 'athletic', 'plus']
ims = [Image.open(f'{S}/out_{tag}_{p}/walk_file_{p}.png').convert('RGB') for p in presets]
W, H = ims[0].size
canvas = Image.new('RGB', (W * len(ims), H), (233, 230, 224))
for i, im in enumerate(ims):
    canvas.paste(im, (i * W, 0))
canvas.save(out)
print(canvas.size)
