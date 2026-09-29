#!/usr/bin/env python3
"""Stage L exact bit model: q16 two-word pair Mandelbrot on RSP accumulator
semantics. This is the oracle for rsp_deep.S — every op maps 1:1.

State per pixel, per component: zq = z*2^16 as PAIR (H s16, L u16),
  |H| <= 71 (live box: escape when |H| > 71 tested BEFORE update —
  same box-radius convention as q12 stage, radius ~sqrt(5)).

Iteration (mirrors ucode exactly):
  split z:   h, l   (l u16; l8 = l>>8 for u8-operand frags)
  zr² q32 accumulator build:
     acc  = h*h            << 16     (vmudm h8,h8 gives h²<<16 into acc;
                                        h8 = h<<8 keeps u16 operands)
     acc += (h8 * l8)      << ...    etc per fragment table below
  q32 pair of z² is (S>>16, S&0xffff).
  Escape test on z's h slice done BEFORE update: |H| >= 72 ⇒ escaped.
  Fold c: c folded as q16 pair add (cH + borrow from cL).
  zn_q16 = (z²_q32 pair) + c_q16 pair, clamped to box.
  Cross: w = zr+zi q16 pair (s16 add, |w|<=142 fits q16 s16 range for
  h slice? NO: w q16 value ≤ 2·71.5 = 143 → h slice |w_h| ≤ 143 fits s16 ✓)

Fragment table (all operands u16 products, value ≤ 2^32, q32 target):
  z²q32  = (h·2^8)² · 2^16            [h8² >>16 gives h²; accumulate >>16-scaled
                                          — model: acc += h² << 16]
         + 2·(h·2^8)·l · 2^8          [acc += 2·h·l << 8]
         + l²                          [acc += l²]
  (h ≤ 71 signed: split sign into accumulator side, products unsigned)
"""
import numpy as np, time

Q = np.int64
BOX = Q(71)          # |H| >= 72 escapes (pre-update test)
CLZ = (Q(71) << 16) + Q(0xFFFF)   # clamp |zq| here (~71.999985 ≈ sqrt5*2^16*0.99986)

def to_q16(v):
    q = np.round(v * 65536.0).astype(Q)
    return np.clip(q, -CLZ, CLZ)

def zsq_q32(zq):
    """zq² in q32 via fragment identities, h signed split out."""
    h = (zq >> Q(16)).astype(Q)
    l = zq - (h << Q(16))          # u16, 0..65535
    hs = np.abs(h)
    h8 = hs << Q(8)
    l8 = l >> Q(8)
    lo = l & Q(0xFF)
    acc = (h8 * h8) << Q(16)       # h²·2^32
    acc += (h8 * l) << Q(8)        # h·l·2^24 ; doubled below for 2hl
    acc += (h8 * l) << Q(8)
    acc += (l8 * l8) << Q(16)      # l²... l² = (l8·2^8+lo)² = l8²·2^16 + 2·l8·lo·2^8 + lo²
    acc += (l8 * lo) << Q(9)
    acc += lo * lo
    # NOTE l² split exactly: l8²<<16 + 2·l8·lo<<8 + lo² ✓ (above adds l8*l8<<16, l8*lo<<9, lo*lo)
    # h²<<32 check vs acc cap: h≤71 → 71²·2^32·2 (both comps) = 1.01e13 < 2^47 ✓
    return acc

def q32_pair_add(acc, cq):
    """acc (q32 z²) + cq (q16 c) -> q16 result int: (acc>>16)+c, borrow lost?
    RSP extracts pair (vmach/vmacm); model keeps integer = trunc((acc + c<<32... no:
    c folds at q16: result_q16 = (acc >> 16) + cq, then clamp; sub-q16 bits dropped."""
    return ((acc >> Q(16)) + cq)

def clamp_pair(qv):
    return np.clip(qv, -CLZ, CLZ)

def model_iter(crq, ciq, nmax):
    shape = crq.shape
    zr = np.zeros(shape, dtype=Q); zi = np.zeros(shape, dtype=Q)
    n = np.full(shape, nmax, dtype=np.int32)
    alive = np.ones(shape, dtype=bool)
    for i in range(nmax):
        h_r = zr >> Q(16); h_i = zi >> Q(16)
        esc = alive & ((h_r >= BOX + Q(1)) | (h_r <= -BOX - Q(1)) |
                       (h_i >= BOX + Q(1)) | (h_i <= -BOX - Q(1)))
        n[esc] = i
        live = alive & ~esc
        zr2 = zsq_q32(zr)
        zi2 = zsq_q32(zi)
        w = zr + zi
        w2 = zsq_q32(w)
        re = clamp_pair(q32_pair_add(zr2, crq) - (zi2 >> Q(16)))
        im = clamp_pair(q32_pair_add(w2, ciq) - (zr2 >> Q(16)) - (zi2 >> Q(16)))
        # NOTE im: (w²−zr²−zi²)>>16 + ci — do subtracts BEFORE >>16 for exactness:
        im = clamp_pair(((w2 - zr2 - zi2) >> Q(16)) + ciq)
        re = clamp_pair(((zr2 - zi2) >> Q(16)) + crq)
        zr = np.where(live, re, zr)
        zi = np.where(live, im, zi)
        alive = alive & ~esc
        if not alive.any():
            break
    return n

def render(W, H, cx, cy, span, nmax):
    xs = cx - span / 2 + (span / W) * np.arange(W)
    ys = cy - span / 2 + (span / H) * np.arange(H)
    CR, CI = np.meshgrid(xs, ys)
    return model_iter(to_q16(CR), to_q16(CI), nmax)

def mandel_box(CR, CI, nmax):
    zr = np.zeros_like(CR); zi = np.zeros_like(CI)
    n = np.full(CR.shape, nmax, dtype=np.int32)
    alive = np.ones(CR.shape, dtype=bool)
    R = 71.99 / 2.0    # radius in plain units: |z| box escape, h>=72 ⇔ |z|>=72/65536*2? z is plain; zq=z*2^16 but z plain here
    # careful: model zq = z*2^16, escape h = zq>>16 = floor(z) region... escape |H|>=72 ⇔ |z| >= 72 (NOT 72/65536!)
    R = 72.0
    for i in range(nmax):
        esc = alive & ((np.abs(zr) >= R) | (np.abs(zi) >= R))
        n[esc] = i
        zr2, zi2 = zr * zr, zi * zi
        zi = np.where(alive, 2 * zr * zi + CI, zi)
        zr = np.where(alive, zr2 - zi2 + CR, zr)
        alive &= ~esc
    return n

if __name__ == "__main__":
    W, H, nmax = 320, 240, 200
    views = [
        ("home",     -0.7,        0.0,    2.6),
        ("valley",   -0.745,     -0.115,  0.30),
        ("seahorse", -0.7458,     -0.1149, 0.02),
        ("mid",      -0.745,      -0.115, 5e-3),
    ]
    for name, cx, cy, span in views:
        t0 = time.time()
        n = render(W, H, cx, cy, span, nmax)
        xs = cx - span / 2 + (span / W) * np.arange(W)
        ys = cy - span / 2 + (span / H) * np.arange(H)
        CR, CI = np.meshgrid(xs, ys)
        nd = mandel_box(CR, CI, nmax)
        agree = ((n == nmax) == (nd == nmax)).mean()
        print(f"{name:9s} span={span:<8} agree={100*agree:.2f}% interior={100*(n==nmax).mean():.2f}% t={time.time()-t0:.1f}s")
