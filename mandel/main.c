/* mandel — Mandelbrot on the N64 RSP (libdragon, stage D: smooth color)
 *
 * Stage B verified: ucode matches CPU q11 reference pixel-exact (mism=0)
 * on ares headless. Stage C added mirroring + full-screen redraw + LUTs.
 * Stage D adds smooth (continuous) coloring:
 *  - ucode also stores escape S q6 (= |z|²·64) per pixel (sout region);
 *    frozen lanes recompute the exact escape value, so post-loop S is exact
 *  - CPU maps S → log2(log|z|) correction via 64K-entry LUT (no per-pixel
 *    logs), smooth count = n + (P_B − P(z_esc)), scaled 4096
 *  - 2048-entry cyclic rainbow palette indexed by smooth count
 *  - mirroring across real axis preserved (|z̄| = |z|)
 * Classic tricks active: real-axis mirroring, full-screen redraw, LUTs,
 * escape-radius smoothing. VERIFY=1 cross-checks counts vs CPU reference.
 */

#include <libdragon.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define W 320
#define H 240
#define MAXITER 60

#ifndef USE_RSP
#define USE_RSP 1
#endif
#ifndef VERIFY
#define VERIFY 1     /* 1 = full CPU cross-check of escape counts */
#endif

/* CPU q11 reference mirroring ucode semantics EXACTLY:
   vmudm(|z|,|z|) → (z*z)>>16 q6; cross term via (zr+zi)² identity;
   vadd/vsub saturating s16 — mirrored with sat(). Escape test before
   update, S q6 ≥ 256 ⇔ |z| ≥ 2. */
static inline int16_t sat16(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static int mandel_q11(int cr, int ci)
{
    int zr = 0, zi = 0;
    for (int i = 0; i < MAXITER; i++) {
        int32_t zr2 = ((int32_t)zr * zr) >> 16;   /* q6 */
        int32_t zi2 = ((int32_t)zi * zi) >> 16;
        if (zr2 + zi2 >= 256) return i;            /* |z|² >= 4 */
        int32_t s = (int32_t)zr + zi;
        int32_t cr2 = (s * s) >> 16;
        int32_t cross = cr2 - zr2 - zi2;           /* 2·zr·zi q6 (trunc-order) */
        int32_t d = zr2 - zi2;
        zr = sat16((d << 5) + cr);
        zi = sat16((cross << 5) + ci);
    }
    return MAXITER;
}

static inline int q11(double v)
{
    double s = v * 2048.0;
    if (s > 32767.0) s = 32767.0;
    if (s < -32768.0) s = -32768.0;
    return (int)lrint(s);
}

#if USE_RSP
#include <rsp.h>
DEFINE_RSP_UCODE(rsp_mandel);

#define NCHUNK   (W / 8)                  /* 40 chunks of 8 lanes */
/* stage halfword-index helpers (byte offset >> 1).
   sout (escape S q6) occupies 0x0A80..0x0D00, so the constant/probe
   block lives at 0x0D00 and up — ucode base t7 = 0x0D00 must match. */
#define CR_HW    (0x0000 / 2)
#define CI_HW    (0x0400 / 2)
#define OUT_BYTE 0x0800                   /* counts (640B) + sout (640B) */
#define NC_HW    (0x0D00 / 2)
#define ESC_HW   (0x0D40 / 2)
#define ONE_HW   (0x0D50 / 2)
#define ESCM_HW  (0x0D60 / 2)
#define PROBE_HW (0x0D80 / 2)
#define STAGE_HW (0x0E70 / 2)

static uint16_t stage[STAGE_HW] __attribute__((aligned(8)));
static uint16_t rdbuf[W * 2] __attribute__((aligned(8)));  /* counts|sout */
#endif /* USE_RSP */

static const double view_x0 = -2.0, view_x1 = 0.6;
static const double view_yhalf = 1.1;    /* full view = [-1.1, +1.1] */
#define YMID 120                           /* pixel row of cy = 0 */
#define NUP  121                           /* upper rows u=0..120 (ci ≥ 0) */

static uint16_t cxq[W];                  /* q11 c_x, constant per view */
static int16_t  cyq[NUP];              /* q11 c_y ≥ 0 (computed half) */
static uint16_t upper[NUP][W];         /* ucode escape counts, upper half */
static uint16_t supper[NUP][W];        /* ucode escape S q6, upper half */

/* smooth-count LUTs */
#define PALN 2048
static uint32_t pal_lut[PALN];
static int16_t  smooth_tab[32768];     /* S q6 → 4096·(P_B − P(|z|)) */
#define P_B_LOG  (-0.5287663729449458)  /* log2(log(2.0)) */

static void build_smooth_tab(void)
{
    for (int s = 0; s < 32768; s++) {
        if (s < 256) { smooth_tab[s] = 0; continue; }  /* interior-ish */
        double z2 = (double)s / 64.0;        /* |z|² */
        double z  = sqrt(z2);
        double p  = log2(log(z));            /* potential */
        double v  = 4096.0 * (P_B_LOG - p); /* smooth correction ≤ 0 */
        if (v < -32768.0) v = -32768.0;
        if (v > 0.0) v = 0.0;
        smooth_tab[s] = (int16_t)lrint(v);
    }
}

static void build_palette(void)
{
    for (int i = 0; i < PALN; i++) {
        double t = (double)i / PALN;
        double r = 0.5 + 0.5 * sin(6.28318530718 * (t + 0.00));
        double g = 0.5 + 0.5 * sin(6.28318530718 * (t + 0.33));
        double b = 0.5 + 0.5 * sin(6.28318530718 * (t + 0.67));
        pal_lut[i] = color_to_packed32(RGBA32((uint8_t)(r * 255),
                                               (uint8_t)(g * 255),
                                               (uint8_t)(b * 255), 255));
    }
}

/* smooth4096 → palette index (clamped, cyclic) */
static inline uint32_t smooth_color(int n, uint16_t S)
{
    if (n >= MAXITER) return color_to_packed32(RGBA32(8, 4, 16, 255));
    int sm = n * 4096 + (int)smooth_tab[S > 32767 ? 32767 : S];
    if (sm < 0) sm = 0;
    int idx = (sm * 8) >> 11;            /* ≈ /122 → 0..PALN-1 over 60 iter */
    return pal_lut[idx & (PALN - 1)];
}

int main(void)
{
    debug_init_emulog();   /* rdpq stays OFF (it would own the RSP) */
    display_init(RESOLUTION_320x240, DEPTH_32_BPP, 2, GAMMA_NONE, FILTERS_DISABLED);
    timer_init();

    build_palette();
    build_smooth_tab();
    for (int x = 0; x < W; x++)
        cxq[x] = (uint16_t)q11(view_x0 + (view_x1 - view_x0) * x / W);
    for (int u = 0; u < NUP; u++)
        cyq[u] = (int16_t)q11(view_yhalf * u / YMID);

#if USE_RSP
    rsp_init();
    rsp_load(&rsp_mandel);
    for (int i = 0; i < 8; i++) {
        stage[ESC_HW + i] = 256;     /* q6 radius-² = 4.0 */
        stage[ONE_HW + i] = 1;
        stage[ESCM_HW + i] = 255;    /* esc-1: borrow test S >= 256 */
    }
    uint32_t nch = NCHUNK;
    memcpy(&stage[NC_HW], &nch, 4);
    memcpy(&stage[CR_HW], cxq, sizeof(cxq));   /* c_x constant per view */
#endif

    long frame = 0;
    long long us_rsp = 0, us_pack = 0, us_paint = 0;

    printf("[n64] mandel stage-D boot: %dx%d mirror NUP=%d smooth=on verify=%d\n",
           W, H, NUP, VERIFY);

    while (1) {
        surface_t *fb = display_get();
        uint32_t *pix = (uint32_t *)fb->buffer;

#if USE_RSP
        /* compute the symmetric ci ≥ 0 half: one rsp_run per row */
        for (int u = 0; u < NUP; u++) {
            int16_t ci = cyq[u];
            long long t0 = timer_ticks();
            for (int x = 0; x < W; x++) stage[CI_HW + x] = (uint16_t)ci;
            data_cache_hit_writeback_invalidate(stage, sizeof(stage));
            us_pack += TIMER_MICROS_LL(timer_ticks() - t0);

            rsp_load_data(stage, sizeof(stage), 0);
            t0 = timer_ticks();
            rsp_run();
            /* single DMA read-back: counts @0x0800 (640B) + sout @0x0A80 */
            rsp_read_data(rdbuf, sizeof(rdbuf), OUT_BYTE);
            us_rsp += TIMER_MICROS_LL(timer_ticks() - t0);
            memcpy(upper[u], rdbuf, W * 2);
            memcpy(supper[u], rdbuf + W, W * 2);
        }

        int minc = 999, maxc = 0, mismatch = 0;
        for (int u = 0; u < NUP; u++) {
            for (int x = 0; x < W; x++) {
                int v = upper[u][x];
                if (v < minc) minc = v;
                if (v > maxc) maxc = v;
#if VERIFY
                if (v != mandel_q11((int)(int16_t)cxq[x], (int)cyq[u])) mismatch++;
#endif
            }
        }

        /* paint upper rows + mirror across real axis */
        long long t1 = timer_ticks();
        for (int u = 0; u < NUP; u++) {
            uint16_t *cn = upper[u];
            uint16_t *ss = supper[u];
            if (u > 0) {
                uint32_t *d = pix + (size_t)(YMID - u) * W;
                for (int x = 0; x < W; x++) d[x] = smooth_color(cn[x], ss[x]);
            }
            if (YMID + u < H) {
                uint32_t *d = pix + (size_t)(YMID + u) * W;
                for (int x = 0; x < W; x++) d[x] = smooth_color(cn[x], ss[x]);
            }
        }
        us_paint += TIMER_MICROS_LL(timer_ticks() - t1);

        if ((frame % 30) == 0) {
            char line[256];
            int n = snprintf(line, sizeof(line),
                "[probe] f=%ld mism=%d min=%d max=%d rsp_ms=%lld pack_ms=%lld paint_ms=%lld\n",
                (long)frame, mismatch, minc, maxc,
                (long long)(us_rsp / 1000), (long long)(us_pack / 1000),
                (long long)(us_paint / 1000));
            static uint8_t isvbuf[256] __attribute__((aligned(8)));
            for (int i = 0; i < n; i++) isvbuf[i] = (uint8_t)line[i];
            for (int i = 0; i < n; i += 4) {
                uint32_t v = 0;
                for (int j = 0; j < 4; j++) v |= (uint32_t)isvbuf[i + j] << (24 - 8 * j);
                io_write(0x13FF0020 + i, v);
            }
            io_write(0x13FF0014, (uint32_t)n);
            us_rsp = us_pack = us_paint = 0;
        }
#else
        /* CPU-only fallback (same mirror + smooth path) */
        for (int u = 0; u < NUP; u++) {
            for (int x = 0; x < W; x++) {
                /* recompute S too: cheap CPU version stores |z|²q6 at escape */
                int cr = (int)(int16_t)cxq[x], ci = cyq[u];
                int zr = 0, zi = 0, S = 0, cnt = MAXITER;
                for (int i = 0; i < MAXITER; i++) {
                    int32_t zr2 = ((int32_t)zr * zr) >> 16;
                    int32_t zi2 = ((int32_t)zi * zi) >> 16;
                    S = zr2 + zi2;
                    if (S >= 256) { cnt = i; break; }
                    int32_t s = (int32_t)zr + zi;
                    int32_t cr2 = (s * s) >> 16;
                    int32_t cross = cr2 - zr2 - zi2;
                    int32_t d = zr2 - zi2;
                    zr = sat16((d << 5) + cr);
                    zi = sat16((cross << 5) + ci);
                }
                upper[u][x] = (uint16_t)cnt;
                supper[u][x] = (uint16_t)(S > 32767 ? 32767 : S);
            }
            if (u > 0) {
                uint32_t *d = pix + (size_t)(YMID - u) * W;
                for (int x = 0; x < W; x++) d[x] = smooth_color(upper[u][x], supper[u][x]);
            }
            if (YMID + u < H) {
                uint32_t *d = pix + (size_t)(YMID + u) * W;
                for (int x = 0; x < W; x++) d[x] = smooth_color(upper[u][x], supper[u][x]);
            }
        }
#endif
        display_show(fb);
        frame++;
    }
}
