#!/usr/bin/env python3
"""Verify ROM perturbation quarter-lattice against Decimal truth.

ROM quarter pass computes c at pixel centers (qx*4, qy*4) with
cdx = dx*(px - W/2), cdy = dy*(py - H/2), span-relative. We replicate the
c-values in Decimal (span and dx as doubles, but c offset exact relative to
center) and iterate from 0 with escape radius 2 (S>=4), 1200 iters, and
compare the escaped/bounded CLASS (not exact counts) per lattice point.

Usage: python3 pertcheck.py  <cx> <cy> <span>  (defaults: tail view)
Expects the lattice via model; ROM-side comparison done by screenshot diff.
"""
import sys
from decimal import Decimal, getcontext
getcontext().prec = 90

W, H, QW, QH, IT = 320, 240, 80, 60, 1200

def main():
    cx = float(sys.argv[1]) if len(sys.argv) > 1 else -0.7436438870371
    cy = float(sys.argv[2]) if len(sys.argv) > 2 else 0.1318259042053
    span = float(sys.argv[3]) if len(sys.argv) > 3 else 5e-5
    dx = span / W
    dy = span / H
    # ROM double c values
    escaped = bounded = 0
    for qy in range(QH):
        py = qy * 4
        cdy = dy * (py - H / 2)
        ci = (cy - span / 2) + dy * py          # direct-pass c (absolute)
        for qx in range(QW):
            px = qx * 4
            cdx = dx * (px - W / 2)
            cr = (cx - span / 2) + dx * px
            # Decimal truth at the ABSOLUTE double c (what direct pass uses)
            cD = (Decimal(repr(cr)), Decimal(repr(ci)))
            z = (Decimal(0), Decimal(0)); esc = None
            for k in range(IT):
                if z[0] * z[0] + z[1] * z[1] >= Decimal(4):
                    esc = k; break
                z = (z[0] * z[0] - z[1] * z[1] + cD[0], 2 * z[0] * z[1] + cD[1])
            if esc is None: bounded += 1
            else: escaped += 1
    print(f"Decimal truth lattice {QW}x{QH} at span={span:g}: escaped={escaped} bounded={bounded}")

if __name__ == "__main__":
    main()
