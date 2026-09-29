#!/usr/bin/env python3
"""Stage L q-sizing: find the largest q at which z^2 fragment products fit
the RSP constraints.

RSP facts (fork interpreter-vpu.cpp, verified):
  - accumulator: 48-bit signed per lane; every vmad* adds a PRODUCT of two
    16-bit operands (optionally >>16) at bit position 0 of the accumulator.
    There is NO accumulator shift op. So every fragment VALUE must itself be
    <= 2^31 (s) / 2^32 (u) — one multiply, operands pre-shifted inside 16 bits.
  - extraction slices: bits15:0 (vmacq), bits31:16 s16 (vmacm), bits47:32 s16 (vmach).
  => pair (H,L) of the accumulator can be extracted at q = '32' via (vmach, vmacm)
     i.e. acc holds z'^2 scaled at q32 exactly, and (bits31:16, bits15:0) = q16 pair?
     NO — pair of z^2 q32 at target q(k): H = acc>>32+? slices fixed at 16-bit grid:
     (vmach,vmacm) = bits 47:16 = acc scaled q16 relative to its own LSB.

Constraint solver: zq = h*2^32 + m*2^16 + l (h s16, m u16, l u16) three-word z at q(48+b).
z^2 fragments (q(2(48+b))) reduced to ACC at q32 grid requires each fragment value
after folding (operand shifts) <= 2^32. Accumulator target q(64+b) overflows 48-bit
for any b > -18, so acc target = q47 max => ... solve numerically:
"""
import math

def check(qb):
    """z three-word at q(48+qb): returns (ok, detail)."""
    hmax = 32767.0
    zmax = math.sqrt(5.0)
    hmax_q = zmax * 2**(qb + 16)   # h = z*2^(16+qb)... zq = z*2^(48+qb), h = zq>>32
    if hmax_q > hmax: return False, f"h {hmax_q:.0f} > 32767"
    frags = {
        "h2*2^64": (hmax_q**2 * 2**64),
        "2hm*2^48": (2*hmax_q*65535 * 2**48),
        "(2hl+m2)*2^32": ((2*hmax_q*65535 + 65535**2) * 2**32),
        "2ml*2^16": 2*65535*65535 * 2**16,
        "l2": 65535.0**2,
    }
    det = {k: f"{v:.3e} {'OK' if v <= 2**32 else 'OVER'}" for k, v in frags.items()}
    ok = all(v <= 2**32 for v in frags.values())
    # accumulator holds z^2 q(2*(48+qb)) <= 5*2^(...) <= 2^47 ?
    acc = zmax**2 * 2**(2*(48+qb))
    det["acc_q"] = f"acc max {acc:.3e} vs 2^47={2.0**47:.3e} {'OK' if acc <= 2**47 else 'OVER'}"
    return ok and acc <= 2**47, det

for qb in range(-24, -17):
    ok, det = check(qb)
    zq = 48 + qb
    span_floor = 320.0 * 2.0**-zq   # LUT/quantization span floor if CR at z-q
    print(f"z q{zq}: span_floor={span_floor:.2e} ok={ok}")
    if ok:
        for k, v in det.items(): print(f"    {k}: {v}")

# half-scale trick: iterate w = z/2 so box |w| <= sqrt5/2
print("\nhalf-scale w=z/2, escape |w|>=1:")
for zq in range(16, 27):
    # two-word z=w*2^zq: h = w*2^(zq-16), box 1.118
    h = 1.118 * 2**(zq-16)
    f1 = h*h * 2**32                      # h^2 frag at acc q32
    p = 2*h*65535 * 2**16
    f3 = 65535.0**2
    acc = 1.25 * 2**(2*zq)
    ok = f1 <= 2**32 and p <= 2**32 and f3 <= 2**32 and acc <= 2**47
    print(f"  w q{zq}: h2frag={f1:.2e} 2hl={p:.2e} acc={acc:.2e} -> {'OK' if ok else 'over'}")
