#!/usr/bin/env python3
"""Composite a ScummVM screenshot (PNG) + a depth dump (PGM) into left/right
eye images using the same rules as the 3DS backend (stereo_compose.h).
   make_stereo.py shot.png depth.pgm out_prefix [shift_px=2.5]
"""
import sys
import numpy as np
from PIL import Image

shot = Image.open(sys.argv[1]).convert("RGB")
depth = np.array(Image.open(sys.argv[2]))
shift = float(sys.argv[4]) if len(sys.argv) > 4 else 2.5
dw, dh = depth.shape[1], depth.shape[0]
shot = shot.resize((dw, dh), Image.NEAREST)          # screenshots may be scaled
img = np.array(shot)
lvl = (depth // 32).astype(int)
levels = int(lvl.max()) + 1

# split into layers + inpaint ground like stereo_compose.h
layers = []
for l in range(levels):
    layer = np.zeros((dh, dw, 4), dtype=np.uint8)
    m = lvl == l
    layer[m, :3] = img[m]; layer[m, 3] = 255
    layers.append(layer)
g = layers[0]
for y in range(dh):
    row = lvl[y]
    ground = np.where(row == 0)[0]
    if len(ground) == 0:
        if y > 0: g[y] = g[y - 1]
        else: g[y, :, 3] = 255
        continue
    for x in np.where(row != 0)[0]:
        left = ground[ground < x]; right = ground[(ground > x) & (ground < x + 24)]
        cands = []
        if len(left): cands.append(left[-1])
        if len(right): cands.append(right[0])
        if not cands:
            cands = [ground[-1]]  # only ground far to the left: nearest is the last one before x? use any
        pick = min(cands, key=lambda k: abs(k - x))
        g[y, x, :3] = img[y, pick]; g[y, x, 3] = 255

for eye, name in ((-1, "left"), (1, "right")):
    out = np.zeros((dh, dw, 3), dtype=np.uint8)
    for l in range(levels):
        par = (l - 1) * shift
        dx = int(round(-eye * par))
        src = layers[l]
        if dx >= 0:
            dst = out[:, dx:]; s = src[:, :dw - dx]
        else:
            dst = out[:, :dw + dx]; s = src[:, -dx:]
        m = s[:, :, 3] > 0
        dst[m] = s[m, :3]
    Image.fromarray(out).save("%s_%s.png" % (sys.argv[3], name))
print("levels used:", levels)
