#!/usr/bin/env python3
"""A/B morph check: ring-morphed deep frames vs CPU ground truth.

on_g*.png  = deep tail view rendered via ring keyframe morph (ring-on build)
off_g*.png = same view rendered by CPU double pipeline (ring-off build)

Metrics per pair:
  navy_agree  % of pixels whose class (interior-navy vs exterior) matches
  color_mae   mean absolute RGB error over exterior pixels
Interior/exterior truth comes from the numpy oracle at the tail view.
"""
import sys
import numpy as np
from PIL import Image

TAIl = (-0.7436438870371, 0.1318259042053, 5.0e-05)  # tail view (matches deepcheck.py)
W, H = 320, 240
NAVY = (10, 20, 40)  # interior color approx; refine from off frame

def load(p):
    return np.asarray(Image.open(p).convert("RGB")).astype(np.int32)

def oracle_mask(cx, cy, span, n=400, maxiter=1200):
    xs = np.linspace(cx - span / 2, cx + span / 2, W)
    ys = np.linspace(cy - span / 2, cy + span / 2, H)
    c = xs[None, :] + 1j * ys[:, None]
    z = np.zeros_like(c)
    m = np.ones(c.shape, bool)
    for _ in range(maxiter):
        z = np.where(m, z * z + c, z)
        m &= np.abs(z) <= 2.0
    return m

def main():
    pairs = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    om = oracle_mask(*TAIl)
    navy = None
    for i in range(pairs):
        off = load(f"test/deep/off_g{i}.png")
        on = load(f"test/deep/on_g{i}.png")
        if navy is None:
            navy = off[20, 20]  # center pixel is deep interior at tail view
        navy = np.array(navy)
        off_navy_px = (np.abs(off - navy).max(axis=2) < 24)
        on_navy_px = (np.abs(on - navy).max(axis=2) < 24)
        agree = (off_navy_px == on_navy_px).mean() * 100
        # oracle agreement vs off ground truth (sanity, expect ~99%)
        oagree = (om == off_navy_px).mean() * 100
        ext = ~on_navy_px & ~off_navy_px
        mae = np.abs(on - off)[ext].mean() if ext.any() else float("nan")
        print(f"pair {i}: class_agree={agree:.2f}%  oracle_vs_cpu={oagree:.2f}%  exterior_MAE={mae:.1f}")
    print("done")

if __name__ == "__main__":
    main()
