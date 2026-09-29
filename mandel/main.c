/* mandel — Mandelbrot on the N64 RSP (libdragon, stage E: zoom tour)
 *
 * Stage B verified pixel-exact (mism=0 vs semantic CPU reference).
 * Stage C added mirroring + full-screen redraw + LUTs; stage D added
 * smooth coloring via escape-radius LUT.
 *
 * Stage E: animated zoom tour through seahorse valley.
 *  - keyframe list; center interpolated linearly (smoothstep-eased),
 *    span interpolated geometrically → constant octave-rate zoom velocity
 *  - coordinates packed on CPU in double precision into the q12 stage,
 *    so the view is exact at any depth; the ITERATION runs q12 z
 *    (range ±8, escape overshoot safe). Depth cap for smooth iteration
 *    detail ≈ span 0.02 (q12 pixel-step); deeper needs vmad chaining
 *    with 32-bit coordinate offsets (roadmap).
 *  - real-axis mirroring kept ONLY while the view still contains the
 *    axis symmetric about its center (home view); off-axis views render
 *    all rows
 *  - VERIFY=1 cross-checks every pixel against the CPU q12 twin
 */

#include <libdragon.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define W 320
#define H 240
#define MAXITER 120

#ifndef USE_RSP
#define USE_RSP 1
#endif
#ifndef VERIFY
#define VERIFY 1     /* 1 = full CPU cross-check of escape counts */
#endif

/* CPU q12 reference mirroring ucode semantics EXACTLY:
   vmudm(|z|,|z|) → (z*z)>>16 q8; cross term via (zr+zi)² identity;
   vadd/vsub saturating s16 — mirrored with sat(). Escape test before
   update, S q8 ≥ 1024 ⇔ |z| ≥ 2. */
static inline int16_t sat16(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static int mandel_q12(int cr, int ci)
{
    int zr = 0, zi = 0;
    for (int i = 0; i < MAXITER; i++) {
        int32_t zr2 = ((int32_t)zr * zr) >> 16;   /* q8 */
        int32_t zi2 = ((int32_t)zi * zi) >> 16;
        if (zr2 + zi2 >= 1024) return i;           /* |z|² >= 4 */
        int32_t s = (int32_t)zr + zi;
        int32_t cr2 = (s * s) >> 16;
        int32_t cross = cr2 - zr2 - zi2;           /* 2·zr·zi q8 (trunc-order) */
        int32_t d = zr2 - zi2;
        zr = sat16((d << 4) + cr);
        zi = sat16((cross << 4) + ci);
    }
    return MAXITER;
}

static inline int q12(double v)
{
    double s = v * 4096.0;
    if (s > 32767.0) s = 32767.0;
    if (s < -32768.0) s = -32768.0;
    return (int)lrint(s);
}

#if USE_RSP
#include <rsp.h>
DEFINE_RSP_UCODE(rsp_mandel);

#define NCHUNK   (W / 8)                  /* 40 chunks of 8 lanes */
/* stage halfword-index helpers (byte offset >> 1).
   sout (escape S q8) occupies 0x0A80..0x0D00; constants/probe at 0x0D00+ */
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

/* ---------------- zoom tour keyframes ----------------
 * hold = frames parked at this key before moving to next;
 * span interpolates geometrically across a fixed 150-frame move. */
typedef struct { double cx, cy, span; int hold; } Key;
static const Key keys[] = {
    { -0.700000,  0.000000, 2.600,  45 },   /* home */
    { -0.743500, -0.131400, 0.0800, 30 },  /* seahorse valley entry */
    { -0.745800, -0.114900, 0.0200, 45 },  /* seahorse bodies */
    { -0.744500, -0.116000, 0.0500, 20 },  /* shift + pull back */
    { -0.235125,  0.827215, 0.0200, 40 },  /* elephant valley wing */
    { -0.700000,  0.000000, 2.600,  60 },  /* back home, park */
};
#define NKEY ((int)(sizeof(keys)/sizeof(keys[0])))
#define MOVE_FRAMES 150

static double view_x0 = -2.0, view_x1 = 0.6;   /* home view (verify ref) */

static uint16_t cxq[W];                  /* q12 c_x for current view */
static uint16_t cyq[H];                  /* q12 c_y per row */
static uint16_t rows_cnt[H][W];         /* ucode escape counts */
static uint16_t rows_s[H][W];           /* ucode escape S q8 */

/* smooth palette */
#define PALN 4096
static uint32_t pal_lut[PALN];
static int16_t  smooth_tab[32768];     /* S q8 → 4096·(P_B − P(|z|)) */
#define P_B_LOG  (-0.5287663729449458)  /* log2(log(2.0)) */

static void build_smooth_tab(void)
{
    for (int s = 0; s < 32768; s++) {
        if (s < 1024) { smooth_tab[s] = 0; continue; }
        double z2 = (double)s / 256.0;         /* |z|² (q8) */
        double p  = log2(0.5 * log(z2));      /* log2(log|z|) */
        double v  = 4096.0 * (P_B_LOG - p);
        if (v < -32768.0) v = -32768.0;
        if (v > 32767.0)  v = 32767.0;
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

static inline uint32_t smooth_color(int n, uint16_t S)
{
    if (n >= MAXITER) return color_to_packed32(RGBA32(8, 4, 16, 255));
    int sm = n * 4096 + (int)smooth_tab[S > 32767 ? 32767 : S];
    int idx = (sm * 4) >> 11;
    return pal_lut[idx & (PALN - 1)];
}

/* view interpolation for global frame f */
static double cycle_len = 0;
static void view_at(long f, double *cx, double *cy, double *span)
{
    if (cycle_len == 0) {
        double t = 0;
        for (int i = 0; i < NKEY; i++)
            t += keys[i].hold + MOVE_FRAMES;
        cycle_len = t;
    }
    double tf = (double)(f % (long)cycle_len);
    int i = 0;
    double seg = keys[i].hold + MOVE_FRAMES;
    while (tf >= seg) { tf -= seg; i = (i + 1) % NKEY; seg = keys[i].hold + MOVE_FRAMES; }
    int next = (i + 1) % NKEY;
    double e = 0;
    if (tf >= keys[i].hold) {
        double u = (tf - keys[i].hold) / MOVE_FRAMES;   /* 0..1 across move */
        e = u * u * (3.0 - 2.0 * u);                     /* smoothstep */
    }
    *cx = keys[i].cx + (keys[next].cx - keys[i].cx) * e;
    *cy = keys[i].cy + (keys[next].cy - keys[i].cy) * e;
    /* geometric span interp: linear in log-space = constant zoom rate */
    double ls = log(keys[i].span), ln = log(keys[next].span);
    *span = exp(ls + (ln - ls) * e);
}

int main(void)
{
    debug_init_emulog();   /* rdpq stays OFF (it would own the RSP) */
    display_init(RESOLUTION_320x240, DEPTH_32_BPP, 2, GAMMA_NONE, FILTERS_DISABLED);
    timer_init();

    build_palette();
    build_smooth_tab();

#if USE_RSP
    rsp_init();
    rsp_load(&rsp_mandel);
    for (int i = 0; i < 8; i++) {
        stage[ESC_HW + i] = 1024;    /* q8 radius-² = 4.0 */
        stage[ONE_HW + i] = 1;
        stage[ESCM_HW + i] = 1023;   /* esc-1: borrow test S >= 1024 */
    }
    uint32_t nch = NCHUNK;
    memcpy(&stage[NC_HW], &nch, 4);
#endif

    long frame = 0;
    long long us_rsp = 0, us_pack = 0, us_paint = 0;

    printf("[n64] mandel stage-E boot: %dx%d zoom tour NKEY=%d verify=%d\n",
           W, H, NKEY, VERIFY);

    while (1) {
        surface_t *fb = display_get();
        uint32_t *pix = (uint32_t *)fb->buffer;

        double cx0, cy0, span;
        view_at(frame, &cx0, &cy0, &span);
        double dx = span / W, dy = span / H;

        long long t0 = timer_ticks();
        for (int x = 0; x < W; x++)
            cxq[x] = (uint16_t)q12(cx0 - span / 2 + dx * x);
        for (int y = 0; y < H; y++)
            cyq[y] = (uint16_t)q12(cy0 - span / 2 + dy * y);
        /* save home-view bounds when parked at home (span >= 2) */
        if (span >= 2.0 && cy0 == 0.0) {
            view_x0 = cx0 - span / 2; view_x1 = cx0 + span / 2;
        }
        us_pack += TIMER_MICROS_LL(timer_ticks() - t0);

#if USE_RSP
        memcpy(&stage[CR_HW], cxq, sizeof(cxq));
        /* one rsp_run per row */
        int rowmax = H;
        int mirror = 0;
        double ytop = cy0 - span / 2;
        double ybot = cy0 + span / 2;
        if (ytop >= 0.0) {
            mirror = 0;         /* whole view above axis: no pairing possible */
            rowmax = H;
        } else if (fabs(ytop + ybot) < 1e-9) {
            mirror = 1;         /* symmetric about real axis: compute rows 0..H/2 */
            rowmax = H / 2 + 1;
        } else {
            mirror = 0;
            rowmax = H;
        }

        for (int y = 0; y < rowmax; y++) {
            uint16_t ci = cyq[y];
            for (int x = 0; x < W; x++) stage[CI_HW + x] = ci;
            data_cache_hit_writeback_invalidate(stage, sizeof(stage));

            rsp_load_data(stage, sizeof(stage), 0);
            t0 = timer_ticks();
            rsp_run();
            rsp_read_data(rdbuf, sizeof(rdbuf), OUT_BYTE);
            us_rsp += TIMER_MICROS_LL(timer_ticks() - t0);
            memcpy(rows_cnt[y], rdbuf, W * 2);
            memcpy(rows_s[y], rdbuf + W, W * 2);
        }

        int minc = 999, maxc = 0, mismatch = 0;
        int mm_y[8], mm_x[8], mm_r[8], mm_v[8], mmc = 0;
        for (int y = 0; y < rowmax; y++)
            for (int x = 0; x < W; x++) {
                int v = rows_cnt[y][x];
                if (v < minc) minc = v;
                if (v > maxc) maxc = v;
#if VERIFY
                int ref = mandel_q12((int)(int16_t)cxq[x], (int)(int16_t)cyq[y]);
                if (v != ref) {
                    mismatch++;
                    if (mmc < 8) { mm_y[mmc]=y; mm_x[mmc]=x; mm_r[mmc]=ref; mm_v[mmc]=v; mmc++; }
                }
#endif
            }
        if (mmc) {
            printf("[mm] ");
            for (int i = 0; i < mmc; i++)
                printf("y%d x%d rom=%d ref=%d(cr=%d ci=%d) ", mm_y[i], mm_x[i],
                       mm_v[i], mm_r[i], (int)(int16_t)cxq[mm_x[i]], (int)cyq[mm_y[i]]);
            printf("\n");
        }
#else
        int rowmax = H, mirror = 0;
        for (int y = 0; y < rowmax; y++) {
            for (int x = 0; x < W; x++) {
                int cr = (int)(int16_t)cxq[x], ci = (int)(int16_t)cyq[y];
                int zr = 0, zi = 0, S = 0, cnt = MAXITER;
                for (int i = 0; i < MAXITER; i++) {
                    int32_t zr2 = ((int32_t)zr * zr) >> 16;
                    int32_t zi2 = ((int32_t)zi * zi) >> 16;
                    S = zr2 + zi2;
                    if (S >= 1024) { cnt = i; break; }
                    int32_t s = (int32_t)zr + zi;
                    int32_t cr2 = (s * s) >> 16;
                    int32_t cross = cr2 - zr2 - zi2;
                    int32_t d = zr2 - zi2;
                    zr = sat16((d << 4) + cr);
                    zi = sat16((cross << 4) + ci);
                }
                rows_cnt[y][x] = (uint16_t)cnt;
                rows_s[y][x] = (uint16_t)(S > 32767 ? 32767 : S);
            }
        }
#endif

        long long t1 = timer_ticks();
        for (int y = 0; y < rowmax; y++) {
            uint32_t *d = pix + (size_t)y * W;
            uint16_t *cn = rows_cnt[y], *ss = rows_s[y];
            for (int x = 0; x < W; x++) d[x] = smooth_color(cn[x], ss[x]);
        }
        /* mirrored paint: row y (ci) shares count with row mirroring -ci */
        if (mirror) {
            for (int y = 0; y < rowmax; y++) {
                /* find symmetric row index where cyq = -cyq[y] */
                int ys = (H - 1) - y;
                if (ys >= H) continue;
                uint32_t *d = pix + (size_t)ys * W;
                uint16_t *cn = rows_cnt[y], *ss = rows_s[y];
                for (int x = 0; x < W; x++) d[x] = smooth_color(cn[x], ss[x]);
            }
        }
        us_paint += TIMER_MICROS_LL(timer_ticks() - t1);

        if ((frame % 60) == 0) {
            char line[256];
            int n = snprintf(line, sizeof(line),
                "[probe] f=%ld cx=%.6f cy=%.6f span=%.5f mism=%d max=%d mm0=%d/%d/%d/%d mm1=%d/%d/%d/%d\n",
                (long)frame, cx0, cy0, span, mismatch, maxc,
                mmc > 0 ? mm_y[0] : -1, mmc > 0 ? mm_x[0] : -1,
                mmc > 0 ? mm_v[0] : -1, mmc > 0 ? mm_r[0] : -1,
                mmc > 1 ? mm_y[1] : -1, mmc > 1 ? mm_x[1] : -1,
                mmc > 1 ? mm_v[1] : -1, mmc > 1 ? mm_r[1] : -1);
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

        display_show(fb);
        frame++;
    }
}
