#!/usr/bin/env python3
"""Stage-G: 48-bit bignum (3-word) complex Mandelbrot iteration model.

Everything is lane-simulation of the planned ucode:
  - complex value z = (re, im), each a signed 48-bit integer at q34
    (quantum 2^-34 ≈ 5.8e-11 → 320-px span 1e-6 = 17 quantum/px ✓)
  - vm-family ops only: mul-add into 48-bit ACC with product shift k∈{0,16,32}
    (pre-shifted operand slices bounded to 16 bits), accumulate full product,
    ACC >>= s at the end (vmacq >>1; extra >>s via repeat), slice-extract.
  - escape |z|² ≥ 4 decided on z² >> 34 (q34² → integer) ≥ 4·2^34.

z² (48×48 → keep 48 via >>34): three words a0,a1,a2 (u16/s16 splits):
  full = a0² + 2·a0a1·2^16 + (2·a0a2 + a1²)·2^32 + 2·a1a2·2^48 + a2²·2^64
  RSP path must ADD each term with a >>34 alignment using only
  >>{0,16,32}-sliced products:  k_align per term ∈ {…} — we check FEASIBILITY:
  term/2^34 must itself be produced from a single product with allowed slice.
"""
import math, random

Q = 34
QZ = 1 << Q
ESC = 4 * QZ              # |z|² ≥ 4 in q(34·2)... NO: z² real ≥4 ⇔ int(z²·2^34) ≥ 2^36
# z_q34 = z·2^34. z²·2^34 (real²·q) = (z_q34)² / 2^34.
ESC_I = 4 * QZ           # target compare: (zq² >> 34) ≥ 4·2^34? z=2: (2·2^34)²>>34 = 4·2^34 ✓
MAXITER = 120

def q48(x):
    v = math.floor(x * QZ)
    if not (-2**47 <= v < 2**47): return None
    return v

def words(v):
    """signed 48-bit → (w0,w1,w2) unsigned 16-bit chunks."""
    assert -2**47 <= v < 2**47
    u = v & (2**48 - 1)
    return u & 0xFFFF, (u >> 16) & 0xFFFF, (u >> 32) & 0xFFFF

def from_words(w0, w1, w2):
    u = w0 | (w1 << 16) | (w2 << 32)
    return u - 2**48 if u >= 2**47 else u

def sat48(v):
    if v >= 2**47: return 2**47 - 1
    if v < -2**47: return -2**47
    return v

def sq_shift34(a):
    """model: (a²) >> 34 with ucode-slice feasibility.
    terms: t0=a0² (align -34: >>34 needs >>16 twice → bits lost at <34 ok
    via ACC: keep exact — ACC collects FULL products shifted by their word
    alignment using allowed (>>0,>>16,>>32) per op:
      t0: a0² align +0; want >>34 total: feed ACC a0² then ACC>>...
    FEASIBILITY: a² = Σ terms; (a²)>>34 = Σ term_i >> (34 - align_i).
    term aligns: t0@0 → >>34 = >>16,>>16 (a0² ≤ 2^32 fits 2-slice ✓ but
    2nd >>16 of value ≤ 2^16 → ≤1 — precision LOSS for t0 alone acceptable?
    t0 at q34·2² = z² scale... all we need is ≥ ~16 valid bits of |z|² for
    escape — |z|² ∈ [0,16] real → integer part alone decides escape ✓✓
    SUB-pixel smoothness comes from S_q8 store, tolerant to few-bit error.
    => compute (a²)>>34 in PURE integer here; per-term RSP realization:
       acc = Σ (term >> align_diff), truncating each term to 16-bit slice."""
    full = a * a
    return full >> 34

def step(zr, zi, cr, ci):
    """full-integer z² path (q34). Returns (zr',zi', S, escaped)."""
    Sr = sq_shift34(zr)
    Si = sq_shift34(zi)
    S = Sr + Si
    if S >= ESC_I:
        return zr, zi, S, True
    # cross = 2·zr·zi >> 34 (signed)
    cross = (2 * zr * zi) >> 34
    nr = sat48(Sr - Si + cr)
    ni = sat48(cross + ci)
    return nr, ni, S, False

def mandel_q48(cr, ci, maxiter=MAXITER):
    zr = zi = 0
    for n in range(maxiter):
        zr, zi, S, esc = step(zr, zi, cr, ci)
        if esc:
            return n, S
    return maxiter, 0

def mandel_dbl(cr, ci, maxiter=MAXITER):
    z = 0j
    for n in range(maxiter):
        z = z*z + cr
        if abs(z) >= 2: return n
    return maxiter

def run(cx, cy, span, maxiter=MAXITER, wz=17, cz_ref=40, cz_dw=46):
    """two-stage: q34 full-precision while |z|² < 4·2^(70-2q2) (exact),
    then offset iteration w = z − z_ref at wz (double ref, dw at cz_dw).
    wz/cz chosen per span so quantization stays < 1e-8·span-class."""
    dx = span / 320
    WQ = 1 << wz; CQ = 1 << cz_dw
    bad = 0; tol1 = 0; tol2 = 0; worst = (0, None); pts = []
    for px in (0, 40, 80, 160, 259, 319):
        for py in (0, 60, 120, 180, 239):
            cxd = cx - span/2 + dx*px
            cyd = cy - span/2 + (span/240)*py
            nd = mandel_dbl(cxd, cyd, maxiter)
            # ---- stage A: q34 until ref escapes or n_ref ----
            cr = q48(cxd); ci = q48(cyd)
            if cr is None or ci is None: bad += 1; continue
            na, zq = stage_a(cr, ci, min(nd, MAXITER))
            if nd == MAXITER and na == min(nd, MAXITER) and not zq_esc(zq):
                nc = na          # never escaped within stage A window
            else:
                # ---- stage B: offset at wz from z_ref(na) ----
                nc = stage_b(zq, cxd, cyd, na, nd, wz, cz_dw, maxiter)
            if nc != nd:
                d = abs(nc - nd)
                tol1 += (d <= 1); tol2 += (d > 1)
                if d > worst[0]: worst = (d, (px, py, nc, nd))
            pts.append((px, py, nc, nd))
    print(f"span={span:g} wz=q{wz}: pts={len(pts)} bad={bad} exact={len(pts)-bad-tol1-tol2} ±1={tol1} >1={tol2} worst={worst}")

if __name__ == "__main__":
    for span in (2.6, 0.08, 1e-4, 1e-6):
        run(-0.743643887037151, 0.13182590420533, span)   # seahorse-ish
        run(-0.7, 0.0, span)
