import sys
from PIL import Image
tag, out = sys.argv[1], sys.argv[2]
presets = ['petite', 'average', 'tall', 'curvy', 'athletic', 'plus']
W, H = 300, 640
canvas = Image.new('RGB', (W * len(presets), H * 2), (233, 230, 224))
for i, p in enumerate(presets):
    im = Image.open(f'{sys.argv[3]}/out_{sys.argv[4]}_{tag}_{p}/walk_file_{p}.png').convert('RGB')
    last = im.width - W
    canvas.paste(im.crop((last, 0, last + W, H)), (i * W, 0))
    canvas.paste(im.crop((last, H, last + W, 2 * H)), (i * W, H))
canvas.save(out)
print(canvas.size)
