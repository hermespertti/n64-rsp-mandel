/* mandel — Mandelbrot on the N64 RSP (libdragon, stage B: ucode path)
 *
 * Each frame: CPU packs one display row (20 chunks x 16 px) of c values
 * as q12 halfwords, DMAs the stage into RSP DMEM, runs rsp_mandel.S
 * (60 iterations, exact q12 fixed point, lane-freeze escape), DMA-reads
 * the escape counts back, palettes them straight into the framebuffer.
 *
 * USE_RSP=0 builds the pure-CPU reference for A/B comparison.
 */

#include <libdragon.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#define W 320
#define H 240
#define MAXITER 60

#ifndef USE_RSP
#define USE_RSP 1
#endif

#ifndef VERIFY
#define VERIFY 1     /* CPU-reference cross-check of every RSP pixel */
#endif

#if USE_RSP
#include <rsp.h>
DEFINE_RSP_UCODE(rsp_mandel);

#define NCHUNK   (W / 8)                  /* 40 chunks of 8 lanes */
/* stage halfword-index helpers (byte offset >> 1) */
#define CR_HW    (0x0000 / 2)
#define CI_HW    (0x0400 / 2)
#define OUT_BYTE 0x0800
#define NC_HW    (0x0C00 / 2)
#define ESC_HW   (0x0C40 / 2)
#define ONE_HW   (0x0C50 / 2)
#define ESCM_HW  (0x0C60 / 2)
#define PROBE_HW (0x0C80 / 2)
#define STAGE_HW (PROBE_HW + 120)   /* probe region 0x0C80..0x0D70 */

static uint16_t stage[STAGE_HW] __attribute__((aligned(8)));
static uint16_t outbuf[NCHUNK * 16] __attribute__((aligned(8)));

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
#endif /* USE_RSP */

static inline int q11(double v)
{
    double s = v * 2048.0;
    if (s > 32767.0) s = 32767.0;
    if (s < -32768.0) s = -32768.0;
    return (int)lrint(s);
}

static color_t palette(int iter)
{
    if (iter >= MAXITER) return RGBA32(8, 4, 16, 255);
    double t = (double)iter / MAXITER;
    double r = 40.0 + 215.0 * (1.0 - t) * (1.0 - t);
    double g = 30.0 + 480.0 * (1.0 - t) * t;
    double b = 60.0 + 195.0 * t;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return RGBA32((uint8_t)r, (uint8_t)g, (uint8_t)b, 255);
}

static const double view_x0 = -2.0, view_x1 = 0.6, view_y0 = -1.1, view_y1 = 1.1;

int main(void)
{
    debug_init_emulog();   /* rdpq stays OFF (it would own the RSP) */
    display_init(RESOLUTION_320x240, DEPTH_32_BPP, 2, GAMMA_NONE, FILTERS_DISABLED);
    timer_init();

#if USE_RSP
    rsp_init();
    rsp_load(&rsp_mandel);
    /* constants live in the stage buffer so one DMA covers everything.
       NOTE: slots are 16 BYTE apart = 8 halfwords; looping i<16 made esc
       trample one (overlap) — one must be exactly 1 per lane. */
    for (int i = 0; i < 8; i++) {
        stage[ESC_HW + i] = 256;     /* q6 radius-² = 4.0 */
        stage[ONE_HW + i] = 1;
        stage[ESCM_HW + i] = 255;    /* esc-1: borrow test S >= 256 */
    }
    uint32_t nch = NCHUNK;
    memcpy(&stage[NC_HW], &nch, 4);
#endif

    int row = 0;
    long frame = 0;
    long long rsp_us_total = 0;
    printf("[n64] mandel stage-B boot: %dx%d use_rsp=%d\n", W, H, USE_RSP);

    while (1) {
        surface_t *fb = display_get();
        uint32_t *pix = (uint32_t *)fb->buffer;
        if (row >= H) row = 0;

        double cy = view_y0 + (view_y1 - view_y0) * row / H;

#if USE_RSP
        /* pack one row of c values */
        for (int px = 0; px < W; px++) {
            double cx = view_x0 + (view_x1 - view_x0) * px / W;
            stage[CR_HW + px] = (uint16_t)q11(cx);
            stage[CI_HW + px] = (uint16_t)q11(cy);
        }

        long long t0 = timer_ticks();
        data_cache_hit_writeback_invalidate(stage, sizeof(stage));
        rsp_load(&rsp_mandel);   /* rdpq overlay clobbers IMEM between frames */
        rsp_load_data(stage, sizeof(stage), 0);
        rsp_run();
        rsp_read_data(outbuf, sizeof(outbuf), OUT_BYTE);
        rsp_us_total += TIMER_MICROS_LL(timer_ticks() - t0);

        static uint8_t probe[224] __attribute__((aligned(8)));
        rsp_read_data(probe, sizeof(probe), 0x0C80);
        uint32_t p_nc; memcpy(&p_nc, probe, 4);

        /* verify pass first so the ISV line can carry the count */
        int minc = 999, maxc = 0, mismatch = 0;
        for (int px_x = 0; px_x < W; px_x++) {
            int iter = outbuf[px_x];
            if (iter < minc) minc = iter;
            if (iter > maxc) maxc = iter;
            int ref = mandel_q11(q11(view_x0 + (view_x1 - view_x0) * px_x / W), q11(cy));
#if VERIFY
            if (iter != ref) { mismatch++; }
#endif
            if (iter != ref)
                pix[(size_t)row * W + px_x] = color_to_packed32(RGBA32(255, 0, 255, 255));
            else
                pix[(size_t)row * W + px_x] = color_to_packed32(palette(ref));
        }

        /* direct ISViewer channel: bypass stdio hooks, speak the PI protocol
           ourselves (buffer @0x13FF0020 bytes, length @0x13FF0014 word) */
        if ((frame % 30) == 0) {
            char line[1024];
            int n = snprintf(line, sizeof(line),
                "[probe] f=%ld nc=%lu min=%d max=%d mism=%d\n[bytes] ",
                (long)frame, (unsigned long)p_nc, minc, maxc, mismatch);
            for (int i = 0; i < 224; i++)
                n += snprintf(line + n, sizeof(line) - n, "%02x", probe[i]);
            n += snprintf(line + n, sizeof(line) - n, "\n[out8] ");
            for (int i = 0; i < 16; i++)
                n += snprintf(line + n, sizeof(line) - n, "%d ", (int)outbuf[i]);
            n += snprintf(line + n, sizeof(line) - n, "\n");
            static uint8_t isvbuf[1024] __attribute__((aligned(8)));
            for (int i = 0; i < 1024; i++) isvbuf[i] = 0;
            for (int i = 0; i < n; i++) isvbuf[i] = (uint8_t)line[i];
            for (int i = 0; i < n; i += 4) {
                uint32_t v = 0;
                for (int j = 0; j < 4; j++) v |= (uint32_t)isvbuf[i + j] << (24 - 8 * j);
                io_write(0x13FF0020 + i, v);
            }
            io_write(0x13FF0014, (uint32_t)n);
        }

        uint16_t p_S[8], p_mask[8], p_cnt[8], p_esc[8], p_one[8], p_cr[8], p_ci[8], p_ol[8];
        memcpy(p_esc,  probe + 16,  16);
        memcpy(p_one,  probe + 32,  16);
        memcpy(p_cr,   probe + 48,  16);
        memcpy(p_ci,   probe + 64,  16);
        memcpy(p_S,    probe + 80,  16);
        memcpy(p_mask, probe + 96,  16);
        memcpy(p_cnt,  probe + 112, 16);
        memcpy(p_ol,   probe + 128, 16);
        if ((frame % 240) == 0) {
            printf("[probe] nc=%lu\n", (unsigned long)p_nc);
            printf("[esc]  ");
            for (int i = 0; i < 4; i++) printf("%d ", (int)p_esc[i]);
            printf(" [one] ");
            for (int i = 0; i < 4; i++) printf("%d ", (int)p_one[i]);
            printf("\n[cr]   ");
            for (int i = 0; i < 4; i++) printf("%d ", (int)p_cr[i]);
            printf(" [ci]  ");
            for (int i = 0; i < 4; i++) printf("%d ", (int)p_ci[i]);
            printf("\n[S]    ");
            for (int i = 0; i < 4; i++) printf("%d ", (int)p_S[i]);
            printf(" [msk] ");
            for (int i = 0; i < 4; i++) printf("%d ", (int)p_mask[i]);
            printf(" [cnt] ");
            for (int i = 0; i < 4; i++) printf("%d ", (int)p_cnt[i]);
            printf("\n");
        }

        /* unique-signature probe: (0,255,byte) can never occur in palette
           (palette g maxes ~150). Rows 0..1, 144 bytes = 3 rows × 48 px. */
        for (int b = 0; b < 144; b++) {
            uint32_t col = color_to_packed32(RGBA32(0, 255, probe[b], 255));
            pix[(size_t)(b / 48) * W + (b % 48)] = col;
        }
        display_show(fb);
#else
        for (int px = 0; px < W; px++) {
            double cx = view_x0 + (view_x1 - view_x0) * px / W;
            int iter = mandel_q11(q11(cx), q11(cy));
            pix[(size_t)row * W + px] = color_to_packed32(palette(iter));
        }
        display_show(fb);
#endif

        row++;
        frame++;
    }
}
