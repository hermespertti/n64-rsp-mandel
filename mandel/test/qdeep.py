#!/usr/bin/env python3
"""Stage 1: exact fragment-product audit of a 2-word half-scale deep ucode.

Design (validated against qfit constraints):
  w = z/2 at q(K) two-word (h s16 = w>>16 grid... wq = h*2^16 + m).
  K=22 -> span floor 320*2^-22 = 7.6e-5 (covers tail 5e-5? margin: no!
  tail span 5e-5 needs pixel step span/W = 1.56e-7; c at q22 step = 2^-22
  = 2.4e-7. Use K=23 for c grid? Test both.)
  Iteration: S = |w|^2 at q(2K) via exact fragments; escape S >= 2^(2K)
  (|w|>=1); recombine w' = 2*D + cr_q(2K)/2 ... shift back to qK by RSP
  off-grid slice assist (simulated: quantize >>K with truncation toward zero
  like vmach-style extraction + explicit +1 rounding option).

Audit outputs:
  - fragment bound violations (accumulator 48-bit, s16/u16 operand ranges)
  - escape-count exactness vs numpy double oracle at tail view
  - max S deviation, smooth-color index deviation
"""
import numpy as np
import math, sys

SPAN = float(sys.argv[1]) if len(sys.argv) > 1 else 5e-5
CX, CY = -0.7436438870371, 0.1318259042053
ITERS = 1200
W, H = 320, 240

def model(K, WB, name):
    """w=z/2 at qK, two-word, clamp |w'|<=WB before squaring."""
    dx, dy = SPAN / W, SPAN / H
    xs = CX - SPAN/2 + dx * np.arange(W)
    ys = CY - SPAN/2 + dy * np.arange(H)
    # c at qK (exact trunc quantization like ROM LUT rebuild)
    cr = np.round((xs + 0.0) * 2.0**K).astype(np.int64)  # qK, full (scalar-assisted)
    ci_row = np.round(ys * 2.0**K).astype(np.int64)
    crq = np.broadcast_to(cr[None, :], (H, W)).astype(np.int64)
    ciq = np.broadcast_to(ci_row[:, None], (H, W)).astype(np.int64)
    # state
    wr = np.zeros((H, W), dtype=np.int64)
    wi = np.zeros((H, W), dtype=np.int64)
    cnt = np.full((H, W), ITERS, dtype=np.int32)
    Sfin = np.zeros((H, W), dtype=np.int64)
    ESC = np.int64(1) << (2*K)          # escape at |w|>=1 at q(2K)
    WBq = np.int64(round(WB * 2.0**K))
    viol = {"acc": 0, "h16": 0, "m16": 0}
    live = np.ones((H, W), dtype=bool)
    TWO_K = np.int64(2*K)
    for i in range(ITERS):
        if not live.any(): break
        # decompose live |w|<1 (invariant pre-square): h = w>>16 u? signed
        hr = (wr >> 16); mr = (wr & 0xFFFF)
        hi = (wi >> 16); mi = (wi & 0xFFFF)
        # clamp check applies to stored w (post-recombine clamp), so h range:
        hmax = max(int(np.abs(hr[live]).max()) if live.any() else 0,
                   int(np.abs(hi[live]).max()) if live.any() else 0)
        if hmax >= 32768: viol["h16"] += 1
        # fragments at bit positions (relative q0): h^2 at 32, 2hm at 16, m^2 at 0
        hh = hr*hr; hm2 = 2*hr*mr; mm = mr*mr
        ii = hi*hi; im2 = 2*hi*mi; ii2 = mi*mi
        # accumulator audit: frag*2^bitpos <= 2^47 signed
        for v, bp in ((hh,32),(hm2,16),(mm,0),(ii,32),(im2,16),(ii2,0)):
            mx = int(np.abs(v[live]).max())*2**bp
            if mx > 2**47: viol["acc"] += 1
        # S q(2K):
        S = (hh << 32) + (hm2 << 16) + mm + ((ii << 32) + (im2 << 16) + ii2)
        if int(np.abs(S[live]).max()) > 2**47: viol["acc"] += 1
        # cross D/X: D = wr^2 - wi^2 fragwise; X = 2 wr wi:
        # 2*wr*wi = (2hr*hi)<<32 + (2hr*mi + 2mr*hi)<<16 + 2mr*mi
        xh = 2*hr*hi
        xm = 2*(hr*mi + mr*hi)
        xl = 2*mr*mi
        for v, bp in ((xh,32),(xm,16),(xl,0)):
            mx = int(np.abs(v[live]).max())*2**bp
            if mx > 2**47: viol["acc"] += 1
        esc = (S >= ESC)
        # freeze S on escaped
        newly = esc & live
        Sfin = np.where(newly, S, Sfin)
        cnt = np.where(newly, i, cnt)
        nlive = live & ~esc
        # recombine w' q(2K) -> qK with truncation (vmach >>16 twice style:
        # acc>>16 twice == truncation toward -inf; do arithmetic shift)
        D = ((hh << 32) + (hm2 << 16) + mm) - ((ii << 32) + (im2 << 16) + ii2)
        X = (xh << 32) + (xm << 16) + xl
        wr_new = ((2*D + (crq << (K - 1))) >> K)
        wi_new = ((2*X + (ciq << (K - 1))) >> K)
        # clamp
        wr_new = np.clip(wr_new, -WBq, WBq)
        wi_new = np.clip(wi_new, -WBq, WBq)
        # but clamp changes w^2 we squared? clamp applies pre-square next iter
        # clamp-violation audit: did clamp bite an escaping lane?
        wr = np.where(nlive, wr_new, wr)
        wi = np.where(nlive, wi_new, wi)
        # clamped-but-live lanes that would escape from clamp:
        wr = np.where(esc & ~newly, wr, wr)
        live = nlive
    return cnt, Sfin, viol, K, WB

def oracle():
    dx, dy = SPAN / W, SPAN / H
    xs = CX - SPAN/2 + dx * np.arange(W)
    ys = CY - SPAN/2 + dy * np.arange(H)
    C = xs[None, :] + 1j*ys[:, None]
    Z = np.zeros_like(C)
    N = np.full(C.shape, ITERS, dtype=np.int32)
    Sref = np.zeros(C.shape)
    alive = np.ones(C.shape, dtype=bool)
    for i in range(ITERS):
        if not alive.any(): break
        S = Z.real*Z.real + Z.imag*Z.imag
        esc = alive & (S >= 4.0)
        N[esc] = i
        Sref[esc] = S[esc]
        alive &= ~esc
        Z[alive] = Z[alive]*Z[alive] + C[alive]
    return N, Sref

Nref, Sref = oracle()
for K, WB in [(22, 1.99), (23, 1.25), (23, 1.5)]:
    cnt, Sfin, viol, _, _ = model(K, WB, "")
    escm = Nref != ITERS
    same = int((cnt == Nref).sum())
    tol1 = int((np.abs(cnt.astype(np.int64) - Nref.astype(np.int64)) <= 1).sum())
    ref_i = Nref == ITERS
    mdl_i = cnt == ITERS
    print(f"K={K} WB={WB}: escape-exact {same}/{Nref.size} ({100*same/Nref.size:.2f}%)"
          f"  +-1 {100*tol1/Nref.size:.2f}%  interior-agree {100*float((ref_i==mdl_i).mean()):.2f}%"
          f"  viol={viol}")
    # clamp bite count: escaping lanes where |w'| hit WB
    # S deviation on escaped pixels (in double units):
    d = np.abs(Sfin[escm].astype(np.float64)/2.0**(2*K) - Sref[escm])
    print(f"          S dev max(dbl-units): {d.max():.6f}  frac>0.01: {float((d>0.01).mean())*100:.3f}%")
