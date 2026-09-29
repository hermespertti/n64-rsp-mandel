/* rsp_mandel.S — Mandelbrot escape-time on the RSP vector unit
 *
 * RSP vector = 128 bits = 8 lanes of s16 halfwords.
 * One chunk = one lqv = 16 contiguous bytes = 8 halfwords = 8 pixels.
 * 40 chunks = 320 px = one display row.
 *
 * *** TRANSFER GOTCHA (cost hours): lhv/shv are PACKED-HALF stride-2
 * gather/scatter (every OTHER byte). For CONTIGUOUS halfword arrays use
 * lqv/sqv. Verified against ares interpreter-vpu.cpp + SGI guide Table 3-4:
 *   vmudh = low partial products → sat16(FULL product): q12² clamps at 32767!
 *   vmudm = signed·unsigned, NO shift, result bits[31:16] = exact >>16.
 *   (vmulq = b32-17 + round; ares adds &~15 — avoid.)
 *
 * Fixed point: z in q12 (s16 range ±8 covers z overshoot to 6.5).
 * vmudm(zr²)→q8, escape S=q8 ≥ 1024 (|z| ≥ 2). Deep zoom beyond span
 * ~0.08 needs 32-bit accumulator chaining (roadmap).
 * Cross term by identity 2·zr·zi = (zr+zi)² − zr² − zi² so every vmudm
 * operand is non-negative (vt is u16!).
 * Escape test BEFORE update; escaped lanes FREEZE (old z, cnt stops).
 *
 * VCO discipline (ares interpreter-vpu.cpp verified):
 *   VADD/VSUB fold VCOL then CLEAR it; VSUBC SETS VCOL (ignores incoming);
 *   vabs/vmudm/logical do NOT touch VCOL.
 * Mask: vsubc escm,S → VCOL=(S>escm); vsub $0,$0 folds → 0xFFFF escaped;
 * vnor → live mask.
 *
 /* DMEM map (CPU uploads stage bytes 0x0000..0x0E60 via rsp_load_data):
 *   0x0000  c_r q11 (320 halfwords)   0x0400  c_i q11
 *   0x0800  out   iters (ucode writes)
 *   0x0A80  sout  escape S q6 (ucode writes; = |z|²·64 at escape; frozen
 *            lanes keep S(z_esc) so a post-loop sqv of S is exact)
 *   0x0C00  nchunks u32 (=40)
 *   0x0C40  esc=256   0x0C50 one=1   0x0C60 escm=255  (per-lane vectors)
 *   0x0C80  probe 224 B: +0 nc | +16 esc | +32 one | +48 cr0 | +64 ci0 |
 *            +80 S | +96 escmask | +112 cnt | +128 cr-last |
 *            +144 diag z_r | +160 diag z_i | +176 diag zr²q6 | +192 zi²q6 |
 *            +208 diag S q6
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
    li   t7, 0x0D00
    lw   t2, 0x00(t7)              /* nchunks */
    slti t4, t2, 0
    bnez t4, nc_neg
    nop
    li   t3, 64
    sltu t4, t3, t2
    beqz t4, nc_ok
    nop
    li   t2, 64
    j    nc_ok
    nop
nc_neg:
    li   t2, 0
nc_ok:
    li   s1, 0x0D80
    sw   t2, 0x00(s1)              /* probe nc */

    vxor $v00, $v00, $v00          /* zero vector */
    vsubc $v10, $v00, $v00         /* force VCOL = 0 deterministically */
    vsub  $v10, $v00, $v00

    lqv  $v15, 0x50, t7            /* one vector @0x0D50 = 1 per lane */
    lqv  $v11, 0x60, t7            /* escm vector @0x0D60 = 255 */

    li   t8, 0x0000                /* c_r cursor */
    li   t9, 0x0400                /* c_i cursor */
    li   s0, 0x0800                /* out cursor */
    li   s2, 0x0A80                /* sout cursor (escape S q6) */

chunk:
    lqv  $v02, 0x00, t8            /* c_r 8 lanes q11 */
    lqv  $v03, 0x00, t9            /* c_i 8 lanes q11 */

    vxor $v13, $v13, $v13          /* cnt = 0 */
    vxor $v04, $v04, $v04          /* z_r = 0 */
    vxor $v05, $v05, $v05          /* z_i = 0 */
    li   t0, 0

loop:
    /* ---- squares q6 (vmudm exact >>16; vt is u16 → |z| via vabs) ----
       NOTE: RSP vabs is THREE-operand: vd = vs<0?-vt : vs>0?vt : 0.
       Must pass vt = vs to get |vs|. */
    vabs  $v08, $v04, $v04
    vmudm $v06, $v08, $v08         /* zr² q6 */
    vabs  $v08, $v05, $v05
    vmudm $v07, $v08, $v08         /* zi² q6 */

    /* ---- cross = 2·zr·zi q6 via (zr+zi)² − zr² − zi² (all ≥ 0 inputs) ---- */
    vadd  $v08, $v04, $v05         /* folds VCOL (=0 here), clears */
    vabs  $v08, $v08, $v08
    vmudm $v14, $v08, $v08         /* (zr+zi)² q6 */
    vsub  $v14, $v14, $v06
    vsub  $v14, $v14, $v07         /* cross q6 (signed result) */

    /* ---- escape test S ≥ 256 (radius 2), BEFORE update ---- */
    vadd  $v09, $v06, $v07         /* S q6 */
    vsubc $v10, $v11, $v09         /* VCOL = borrow(S > 255) = escaped */
    vsub  $v10, $v00, $v00         /* fold VCOL → 0xFFFF escaped */
    vnor  $v12, $v10, $v10         /* live mask */

    /* DIAG at t0==1: z_r→+144 z_i→+160 zr²→+176 zi²→+192 S→+208 */
    li    t4, 1
    bne   t0, t4, no_zprobe
    nop
    sqv   $v04, 0x90, s1
    sqv   $v05, 0xA0, s1
    sqv   $v06, 0xB0, s1
    sqv   $v07, 0xC0, s1
    sqv   $v09, 0xD0, s1
no_zprobe:

    /* ---- re' = (zr²−zi²)<<4 + cr   (q8 → q12) ---- */
    vsub  $v08, $v06, $v07         /* D q8 */
    vadd  $v08, $v08, $v08
    vadd  $v08, $v08, $v08
    vadd  $v08, $v08, $v08
    vadd  $v08, $v08, $v08
    vadd  $v08, $v08, $v02         /* re' q12 */

    /* ---- im' = cross<<4 + ci ---- */
    vadd  $v14, $v14, $v14
    vadd  $v14, $v14, $v14
    vadd  $v14, $v14, $v14
    vadd  $v14, $v14, $v14
    vadd  $v14, $v14, $v03         /* im' q12 */

    /* ---- freeze escaped lanes (keep old z) ---- */
    vand  $v06, $v08, $v12
    vand  $v08, $v04, $v10
    vor   $v04, $v06, $v08
    vand  $v06, $v14, $v12
    vand  $v08, $v05, $v10
    vor   $v05, $v06, $v08

    /* ---- cnt++ live lanes ---- */
    vand  $v06, $v15, $v12
    vadd  $v13, $v13, $v06

    addiu t0, t0, 1
    slti  t1, t0, NITER
    bne   t1, zero, loop
    nop

    sqv  $v13, 0x00, s0            /* out 8 lanes */
    sqv  $v09, 0x00, s2            /* sout: S q6 at escape (frozen-exact) */

    addiu t8, 0x10
    addiu t9, 0x10
    addiu s0, 0x10
    addiu s2, 0x10
    addiu t2, -1
    bnez  t2, chunk
    nop

    /* end probe: S @+80, escmask @+96, cnt @+112, cr-last @+128 */
    sqv  $v09, 0x50, s1
    sqv  $v10, 0x60, s1
    sqv  $v13, 0x70, s1
    sqv  $v02, 0x80, s1
    break 0
    nop
    nop
