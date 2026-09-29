#!/usr/bin/env python3
"""Descent A/B: ring-morphed vs CPU ground-truth across a span-changing leg.

offd_g*.png = ring-OFF build (CPU renders every deep frame)   START_FRAME=1600
ond_g*.png  = ring-ON  build (deep frames morph from the ring) same start

Reported per pair: identical-bit / pct-px-differ / exterior MAE / navy-class
agreement. Expectation: small but nonzero differences concentrated at fine
filaments (half-res key upscale), identical class map away from boundaries.
"""
import numpy as np
from PIL import Image
import glob, os

B = "/home/lex/n64/mandel/test/deep/"

def load(p):
    return np.asarray(Image.open(p).convert("RGB")).astype(np.int32)

pairs = []
for f in sorted(glob.glob(B + "ond_g*.png")):
    i = f.split("g")[-1].split(".")[0]
    o = B + f"offd_g{i}.png"
    if os.path.exists(o):
        pairs.append((i, load(o), load(f)))

if not pairs:
    print("no descent pairs found yet")
    raise SystemExit

for i, off, on in pairs:
    nonblack = (off.max(axis=2) > 8).mean()
    if nonblack < 0.05:
        print(f"pair {i}: near-black capture, skipped")
        continue
    d = np.abs(off - on)
    navy_off = (np.abs(off - off[off.shape[0]//2, off.shape[1]//2]).max(axis=2) < 24)
    navy_on  = (np.abs(on  - on [on .shape[0]//2, on .shape[1]//2]).max(axis=2) < 24)
    ext = ~navy_off & ~navy_on
    print(f"pair {i}: identical={(off==on).all()}  px_differ={((d.max(axis=2)>0).mean()*100):.2f}%  "
          f"exterior_MAE={d[ext].mean() if ext.any() else float('nan'):.2f}  "
          f"class_agree={(navy_off==navy_on).mean()*100:.2f}%")
print("done:", len(pairs), "pairs")
