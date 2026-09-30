#!/usr/bin/env python3
"""Perturbation deep-zoom feasibility model (stage Q, host harness, v2).

Correct COMPLEX delta algebra this time (v1 dropped the cross terms):

  reference orbit:  R_{k+1} = R_k^2 + C           (Decimal 90, once/span)
  per pixel:        t = 2R_k + d_hi               (each comp rounded: HW add)
                    p   = t * d_hi                (complex mul:
                       px = tx*dx - ty*dy,  py = tx*dy + ty*dx
                     each real product rounded, exact residual via Dekker)
                    d'  = p + dC                  (dd model folds residuals;
                                                   d model drops them)
  escape:           |R_{k+1} + d_hi| > 2.5        (HW add, then square)
  dC:               cpx - CX exact per column (Sterbenz), cdy per row.

Metric vs the per-pixel Decimal reference orbit (computed once per span):
  pattern  : escape/bounded agreement
  iter_ok  : |escape-iter diff| <= 1 (coloring-safe agreement)
  dmax/spn : worst |d|/span at decision time -> restart-policy headroom
  dupcols  : columns whose double-rounded cpx collide with a neighbor
             (resolution floor: at extreme span, columns share pixels)

Usage: python3 perturb.py [span ...]
"""
import sys
from decimal import Decimal, getcontext

getcontext().prec = 90
MULT = 134217729.0

def split(a):
    t = MULT * a
    hi = t - (t - a)
    return hi, a - hi

def prod_err(a, b, p):
    """exact a*b - p for finite doubles (Dekker)."""
    if a == 0.0 or b == 0.0:
        return 0.0
    ah, al = split(a)
    bh, bl = split(b)
    return ((ah * bh - p) + ah * bl + al * bh) + al * bl

CX = -0.7436438870371
CY = 0.1318259042053
ESC2 = Decimal("6.25")

def decimal_escape(cpx, cpy, iters):
    z = (Decimal(0), Decimal(0))
    for k in range(iters):
        if z[0] * z[0] + z[1] * z[1] > ESC2:
            return k
        z = (z[0] * z[0] - z[1] * z[1] + Decimal(repr(cpx)),
             2 * z[0] * z[1] + Decimal(repr(cpy)))
    return None

def model_run(span, W, H, iters, dd, DECREF):
    step = span / W
    left = CX - span / 2.0
    bot = CY - span / 2.0
    # reference orbit R at C=(CX,CY), Decimal, length iters+1
    DCX = Decimal(repr(CX)); DCY = Decimal(repr(CY))
    R = [(Decimal(0), Decimal(0))]
    for _ in range(iters):
        x, y = R[-1]
        R.append((x * x - y * y + DCX, 2 * x * y + DCY))
    Rf = [(float(a), float(b)) for a, b in R]   # ROM would store doubles? NO:
    # the reference stored for the pixel pass must itself be higher precision
    # than double. In the model, float(Rk) rounds R to double at LOAD, which
    # is exactly the cheap-ROM scenario. dd-with-double-ref == d-with-double-
    # ref except for product residuals; a true-hi-ref variant is ref-hi/lo.
    out = {}
    same = iok = tot = 0
    dmax = 0.0
    worst = []
    dupcols = 0
    cpxs = [left + step * j for j in range(W)]
    for j in range(W):
        if j and cpxs[j] == cpxs[j - 1]:
            dupcols += 1
    for j in range(W):
        cdx = cpxs[j] - CX                      # exact (Sterbenz: |cdx|<|CX|)
        for i in range(H):
            cpy = bot + step * i
            cdy = cpy - CY
            dhx = dhy = lox = loy = 0.0
            esc = None
            for k in range(iters):
                Rx, Ry = Rf[k]
                # t = 2R + d  (HW add, one rounding each)
                tx = 2.0 * Rx + dhx
                ty = 2.0 * Ry + dhy
                # p = t*d complex; each real product rounded + exact resid
                a1 = tx * dhx; e1 = prod_err(tx, dhx, a1)
                a2 = ty * dhy; e2 = prod_err(ty, dhy, a2)
                b1 = tx * dhy; f1 = prod_err(tx, dhy, b1)
                b2 = ty * dhx; f2 = prod_err(ty, dhx, b2)
                phx = a1 - a2; phy = b1 + b2
                plx = (e1 - e2) if dd else 0.0
                ply = (f1 + f2) if dd else 0.0
                # d' = p + dC
                nhx = phx + cdx; nhy = phy + cdy
                if dd:
                    # fold dC rounding loss + renorm
                    plx += (nhx - phx) - cdx
                    ply += (nhy - phy) - cdy
                    eh = nhx + lox + plx
                    lox = ((nhx - eh) + lox) + plx
                    nhx = eh
                    eh = nhy + loy + ply
                    loy = ((nhy - eh) + loy) + ply
                    nhy = eh
                dhx, dhy = nhx, nhy
                # escape on R_{k+1} + d  (HW add rounds)
                zx = Rf[k + 1][0] + dhx
                zy = Rf[k + 1][1] + dhy
                if zx * zx + zy * zy > 6.25:
                    esc = k; break
            d = (dhx * dhx + dhy * dhy) ** 0.5
            if d > 0:
                dmax = max(dmax, d / span)
            eref = DECREF[(j, i)]
            tot += 1
            b1f, b2f = esc is None, eref is None
            if b1f == b2f:
                same += 1
                if b1f or abs(esc - eref) <= 1:
                    iok += 1
                elif len(worst) < 4:
                    worst.append((j, i, esc, eref))
    return (100 * same / tot, 100 * iok / tot, dmax, worst, dupcols)

def run(span, W=24, H=16, iters=300):
    step = span / W
    left = CX - span / 2.0
    bot = CY - span / 2.0
    # Decimal reference per pixel (once)
    DECREF = {}
    for j in range(W):
        cpx = left + step * j
        for i in range(H):
            DECREF[(j, i)] = decimal_escape(cpx, bot + step * i, iters)
    for dd in (True, False):
        p, io, dm, w, dc = model_run(span, W, H, iters, dd, DECREF)
        m = "dd" if dd else "d "
        print(f"span={span:<9g} model={m} pattern={p:6.2f}%  iter_ok(±1)={io:6.2f}%  "
              f"dmax/span={dm:.2e}  dupcols={dc}  worst(j,i,e,ref)={w}")

if __name__ == "__main__":
    spans = [float(a) for a in sys.argv[1:]] or [4e-5, 4e-7, 4e-9, 4e-12, 4e-15]
    for s in spans:
        run(s)
