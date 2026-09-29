#!/usr/bin/env python3
"""Stage L model: q24 pair-iterated Mandelbrot, numpy int64 vectorized,
mirroring the planned RSP ucode fragment arithmetic op-for-op.

  zq = z * 2^24 as (h s16 = zq>>16, l u16 = zq & 0xffff)
  sq40(z) = h*h*2^24 + (2*h*l)*2^8 + (l*l >> 8)   # z^2 * 2^40, sub-q24 dropped
  escape before update: zr2_40 + zi2_40 >= 4*2^40
  cross40 = sq40(zr+zi) - zr2_40 - zi2_40          # = 2*zr*zi * 2^40
  zn_q24 = clamp28((frag40 >> 16) + c_q24)
"""
import numpy as np, time

Q = np.int64
ESC = Q(4) << Q(40)
M28 = Q((1 << 27) - 1)
CLAMP = Q(round(2.236 * 2**24))   # ±√5 — same escape-overshoot bound as q8 design

def clamp28(v):
    return np.clip(v, -CLAMP, CLAMP)

def split(zq):
    return (zq >> Q(16)).astype(np.int16).astype(Q), (zq & Q(0xFFFF))

def sq40(zq):
    h, l = split(zq)
    return (h * h << Q(24)) + ((Q(2) * h * l) << Q(8)) + (l * l >> Q(8))

def model_iter(crq, ciq, nmax):
    zr = np.zeros_like(crq)
    zi = np.zeros_like(ciq)
    n = np.full(crq.shape, nmax, dtype=np.int32)
    alive = np.ones(crq.shape, dtype=bool)
    s40 = np.zeros_like(crq)
    for i in range(nmax):
        zr2 = sq40(zr)
        zi2 = sq40(zi)
        s40 = zr2 + zi2
        esc = alive & (s40 >= ESC)
        n[esc] = i
        newly = esc & alive
        # freeze: keep z of escaped lanes; only update alive
        live = alive & ~esc
        w_r = zr + zi
        w2 = sq40(clamp28(w_r))
        cross = w2 - zr2 - zi2
        zr_n = clamp28(((zr2 - zi2) >> Q(16)) + crq)   # Re = zr²−zi²+cr
        zi_n = clamp28((cross >> Q(16)) + ciq)          # Im = 2·zr·zi+ci
        zr = np.where(live, zr_n, zr)
        zi = np.where(live, zi_n, zi)
        alive = alive & ~esc
        if not alive.any():
            break
    return n, s40

def to_q24(v):
    return clamp28(np.round(v * float(1 << 24)).astype(Q))

def render(W, H, cx, cy, span, nmax):
    xs = cx - span / 2 + (span / W) * np.arange(W, dtype=float)
    ys = cy - span / 2 + (span / H) * np.arange(H, dtype=float)
    CR, CI = np.meshgrid(xs, ys)
    return model_iter(to_q24(CR), to_q24(CI), nmax)

def mandel_double(CR, CI, nmax):
    zr = np.zeros_like(CR); zi = np.zeros_like(CI)
    n = np.full(CR.shape, nmax, dtype=np.int32)
    alive = np.ones(CR.shape, dtype=bool)
    for i in range(nmax):
        s = zr * zr + zi * zi
        esc = alive & (s >= 4.0)
        n[esc] = i
        zr2, zi2 = zr * zr, zi * zi
        cr2 = (zr + zi) ** 2
        zi = np.where(alive, 2 * zr * zi + CI, zi)
        zr = np.where(alive, zr2 - zi2 + CR, zr)
        alive &= ~esc
    return n

if __name__ == "__main__":
    W, H = 320, 240
    views = [
        ("home",     -0.7,            0.0,           2.6),
        ("valley",   -0.745,         -0.115,         0.30),
        ("seahorse", -0.7458,        -0.1149,        0.02),
        ("deep",     -0.7436438870371, 0.1318259042053, 5e-4),
    ]
    nmax = 400
    for name, cx, cy, span in views:
        t0 = time.time()
        n, _ = render(W, H, cx, cy, span, nmax)
        xs = cx - span / 2 + (span / W) * np.arange(W, dtype=float)
        ys = cy - span / 2 + (span / H) * np.arange(H, dtype=float)
        CR, CI = np.meshgrid(xs, ys)
        nd = mandel_double(CR, CI, nmax)
        agree = ((n == nmax) == (nd == nmax)).mean()
        print(f"{name:9s} span={span:<10} agree={100*agree:.2f}% interior={100*(n==nmax).mean():.2f}% t={time.time()-t0:.1f}s")
