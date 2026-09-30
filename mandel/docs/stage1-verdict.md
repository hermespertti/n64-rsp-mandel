# Stage 1 — RSP deep-iteration ucode: measured verdict (CLOSED, not built)

## Question
Can the RSP vector unit replace the CPU double path for the deep zone
(span < 0.078) at 1200+ iterations, keeping pixel parity with the oracle?

## Method
- `test/qfit.py` — accumulator/operand feasibility solver (48-bit acc,
  u16/s16 operands, single-product-at-bit-0 constraint, verified against
  the ares fork interpreter).
- `test/qdeep.py` — full pixel audit of the surviving design: two-word
  half-scale (w = z/2) at q23, clamp at escape bound, big-int model of the
  exact fragment accumulation, compared against numpy double oracle at the
  tail view (span 5e-5, 1200 iter, 320×240).
- Depth scan across spans 1e-2 → 5e-5.

## Results
| span    | interior-agree | escape-count exact |
|---------|----------------|--------------------|
| 1e-2    | 98.5%          | low                |
| 5e-3–2e-4 | 99.97–99.99% | ≈33–42%            |
| 1e-4    | 99.98%         | ≈42% (q23/WB1.0)   |
| 5e-5    | 99.92%         | ≈42%               |

- q23, clamp |w| ≤ 1.0: **zero** accumulator/operand violations (fits HW).
- q23 clamp 1.25/1.5: cross-fragment overflows the 48-bit grid — rejected.
- Escape counts diverge ≈±1 on ~44% of boundary pixels at every depth.
  Cause: at span 5e-5 the pixel c-step (1.6e-7) and the q23 quantum (1.2e-7)
  are the same magnitude; escape timing near the boundary is chaotic.
  53-bit CPU doubles stay ahead of the chaos; 39-bit (two-word q23) cannot.

## Verdict
- RSP deep ucode is HW-feasible and ~10 ms/frame, but APPROXIMATE colour
  (escape-count ±1 banding on edge pixels vs CPU keyframes).
- The L2 ring already delivers 0 ms playback for revisited/tour views and
  100% exact keyframes. The ucode would only speed FIRST visits with a
  visible colour-mode switch, for ~200 lines of fragment asm + a tolerant
  CI lane.
- Closed without building. Reopen if manual free-roam depth UX demands it;
  `test/qdeep.py` is the ready-made oracle harness for the day it ships.
