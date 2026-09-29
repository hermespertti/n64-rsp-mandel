#!/usr/bin/env python3
"""Compare ROM deep-frame vs numpy double reference at the tail keyframe."""
import numpy as np
from PIL import Image

SPAN = 5e-5
CX, CY = -0.7436438870371, 0.1318259042053
ITERS = 1200
W, H = 320, 240

x0, y0 = CX - SPAN/2, CY - SPAN/2
xs = x0 + SPAN * np.arange(W) / W
ys = y0 + SPAN * np.arange(H) / H
C = xs[None, :] + 1j * ys[:, None]

Z = np.zeros_like(C)
N = np.full(C.shape, ITERS, dtype=np.int32)   # interior stays at ITERS
alive = np.ones(C.shape, dtype=bool)
for i in range(ITERS):
    if not alive.any():
        break
    Z[alive] = Z[alive] * Z[alive] + C[alive]
    esc = (Z.real*Z.real + Z.imag*Z.imag) >= 4.0
    newly = alive & esc
    N[newly] = i
    alive &= ~esc

navy_ref = float((N == ITERS).mean())
print(f"numpy navy fraction: {navy_ref:.4f}")

im = Image.open("test/deep/g1.png").convert("RGB").resize((W, H), Image.NEAREST)
px = np.asarray(im)
rom_navy = float(((px[:,:,0] == 8) & (px[:,:,1] == 4) & (px[:,:,2] == 16)).mean())
print(f"ROM   navy fraction: {rom_navy:.4f}")

# pixel-level agreement of interior/exterior classification
ref_interior = (N == ITERS)
rom_interior = (px[:,:,0] == 8) & (px[:,:,1] == 4) & (px[:,:,2] == 16)
agree = float((ref_interior == rom_interior).mean())
print(f"interior/exterior agreement: {agree*100:.2f}%")
