/* rsp_mandel.S — Mandelbrot escape-time on the RSP vector unit (stage F)
 *
 * RSP vector = 128 bits = 8 lanes of s16 halfwords → 8 pixels/op.
 *
 * STAGE F: PAIRED-RUN BATCHING. One rsp_run computes TWO rows: the CPU
 * packs a ci-pair (row y, row ym) into DMEM; the x-LUT and constants ride
 * resident from boot. CPU round-trips per frame drop ~2x even in non-
 * symmetric views, ~4x on the symmetric home view (mirrored pair → the
 * second row's escape counts are reused for its conjugate by the CPU
 * painter... actually both halves are computed here; mirroring saves the
 * CPU paint loop only when ym==H-1-y symmetry holds, so we compute both
 * and the painter just uses rows_cnt directly).
 *
 * Fixed point: z q12 (±8 covers escape overshoot). vmudm(|z|,|z|) → q8
 * exact >>16; escape S ≥ 1024 ⇔ |z| ≥ 2, tested BEFORE update. Cross term
 * via (zr+zi)² − zr² − zi² keeps every vmudm operand ≥ 0 (vt is u16!).
 * Escaped lanes FREEZE (VCO borrow-chain) so the post-loop S store is
 * exactly S(z_esc) → feeds the CPU smooth-color LUT.
 *
 * STAGE K: PER-CHUNK EARLY-OUT. After the escape mask is computed, the
 * live mask (v12) is stored to DMEM scratch and scalar-OR'd across ALL
 * FOUR words (16 bytes = 8 lanes — reading only 2 words missed lanes
 * 2-3/6-7 and broke exactness: mism=688!). If zero, every lane is
 * frozen: the remaining iterations would recompute the same z, add 0 to
 * cnt, and leave S final → break is bit-exact. Home view: 892→338 µs
 * (2.6×). The check is scalar-only ops so the vector VCIN chain that
 * feeds vadd folding is untouched.
 *
 * GOTCHAS (verified vs ares interpreter-vpu.cpp / decoder.cpp):
 *   lhv/shv = stride-2 scatter, NOT contiguous → use lqv/sqv.
 *   vabs THREE-operand (vd = vs<0?-vt : vt) → pass vt = vs.
 *   vmudh = sat16(FULL product), NOT >>16.
 *   vadd/vsub fold then CLEAR VCOL; vsubc SETS it; mul/logical don't.
 *   Scalar lw/sw wrap mod 4096 — constants must live below 0x1000... they
 *   don't have to, but DMEM is 4096 B total, so everything must fit.
 *
 * DMEM map (4096 B — all addresses < 0x1000, lqv/lw wrap mod 4096):
 *   0x0000 row0 out 640  | 0x0280 row0 sout 640
 *   0x0500 row1 out 640  | 0x0780 row1 sout 640
 *   0x0A00 x-LUT 640 B (q12 c_x, resident from boot)
 *   0x0C80 nchunks u32 | one vec @0x0C90 | escm vec @0x0CA0
 *   0x0CB0 ci0 vec | 0x0CC0 ci1 vec                      (uploaded per run)
 *   0x0D80 probe 224 B
 */

#include <rsp.inc>

    .set noreorder
    .set at

    .equ NITER, 120

    .data
    .align 8
_dummy: .word 0

    .text
    .globl _start
_start:
    li   t7, 0x0C80
    lw   t2, 0x00(t7)            /* nchunks (40) */
    slti t4, t2, 0
    bnez t4, nc_neg
    nop
    li   t4, 41
    sltu t4, t2, t4
    beqz t4, nc_hi
    nop
    j    nc_ok
    nop
nc_hi:
    li   t2, 40
    j    nc_ok
    nop
nc_neg:
    li   t2, 0
nc_ok:
    vxor $v00, $v00, $v00
    vsubc $v10, $v00, $v00       /* force VCOL=0 deterministically */
    vsub  $v10, $v00, $v00

    lqv  $v15, 0x10, t7          /* one vec @0x0C90 */
    lqv  $v11, 0x20, t7          /* escm vec @0x0CA0 = 1023/lane */

    li   s6, 0x0CB0              /* ci-pair cursor */
    li   s0, 0x0000              /* row0 out cursor */
    li   s2, 0x0280              /* row0 sout cursor */
    li   s4, 2                   /* rows in this run */
    li   s1, 0x0D80              /* probe base */
    li   s7, 0x0CE0              /* K: live-mask scratch (8 lanes) */

row:
    lqv  $v03, 0x00, s6          /* ci 8 lanes q12 */
    li   t0, 0                   /* chunk index */
chunk:
    li   t4, 0x0A00
    sll  t5, t0, 4
    addu t4, t4, t5
    lqv  $v02, 0x00, t4          /* c_r 8 lanes q12 from resident x-LUT */

    vxor $v13, $v13, $v13        /* cnt = 0 */
    vxor $v04, $v04, $v04        /* z_r = 0 */
    vxor $v05, $v05, $v05        /* z_i = 0 */
    li   t1, NITER

loop:
    vabs  $v08, $v04, $v04
    vmudm $v06, $v08, $v08       /* zr² q8 */
    vabs  $v08, $v05, $v05
    vmudm $v07, $v08, $v08       /* zi² q8 */

    vadd  $v08, $v04, $v05       /* (zr+zi): folds prior VCIN */
    vabs  $v08, $v08, $v08
    vmudm $v14, $v08, $v08
    vsub  $v14, $v14, $v06
    vsub  $v14, $v14, $v07       /* cross = 2·zr·zi q8 */

    vadd  $v09, $v06, $v07       /* S q8 */
    vsubc $v10, $v11, $v09       /* VCOL = S > escm ⇒ escaped */
    vsub  $v10, $v00, $v00       /* fold → 0xFFFF escaped lanes */
    vnor  $v12, $v10, $v10       /* live mask */

    /* K: per-chunk early-out. Pure scalar ops here — vector VCIN chain
       untouched, so still bit-exact. v12==0 ⇒ all lanes frozen: new z
       would equal frozen z, cnt add would be +0, S final. Safe to break. */
    sqv  $v12, 0x00, s7
    nop
    nop
    lw   t3, 0x00(s7)
    lw   t6, 0x04(s7)
    or   t3, t3, t6
    lw   t6, 0x08(s7)
    or   t3, t3, t6
    lw   t6, 0x0C(s7)
    or   t3, t3, t6
    beqz t3, loopend
    nop

    vsub  $v08, $v06, $v07       /* D = zr²−zi² q8 */
    vadd  $v08, $v08, $v08
    vadd  $v08, $v08, $v08
    vadd  $v08, $v08, $v08
    vadd  $v08, $v08, $v08
    vadd  $v08, $v08, $v02       /* re' q12 */

    vadd  $v14, $v14, $v14
    vadd  $v14, $v14, $v14
    vadd  $v14, $v14, $v14
    vadd  $v14, $v14, $v14
    vadd  $v14, $v14, $v03       /* im' q12 */

    vand  $v06, $v08, $v12
    vand  $v08, $v04, $v10
    vor   $v04, $v06, $v08       /* freeze z_r on escaped lanes */
    vand  $v06, $v14, $v12
    vand  $v08, $v05, $v10
    vor   $v05, $v06, $v08       /* freeze z_i */

    vand  $v06, $v15, $v12
    vadd  $v13, $v13, $v06       /* cnt++ live lanes */

    addiu t1, t1, -1
    bnez  t1, loop
    nop
loopend:
    sqv  $v13, 0x00, s0          /* out chunk */
    sqv  $v09, 0x00, s2          /* sout chunk (S frozen-exact) */
    addiu s0, 0x10
    addiu s2, 0x10
    addiu t0, 0x01
    sltu  t4, t0, t2
    bnez  t4, chunk
    nop

    addiu s6, 0x10               /* next ci vector */
    addiu s0, 0x280              /* row0 out end 0x0280 → row1 out 0x0500 */
    addiu s2, 0x280              /* row0 sout end 0x0500 → row1 sout 0x0780 */
    addiu s4, -1
    bnez  s4, row_reload
    nop
    j     done
    nop
row_reload:
    lqv  $v15, 0x10, t7          /* reload constants (VCOL hygiene between rows) */
    lqv  $v11, 0x20, t7
    j     row
    nop

done:
    /* probe: last-row tail values */
    sqv  $v09, 0x50, s1          /* S @+80 */
    sqv  $v13, 0x70, s1          /* cnt @+112 */
    break 0
    nop
    nop
