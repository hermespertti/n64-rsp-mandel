/* mandel — Mandelbrot on the RSP (libdragon)
 *
 * Stage A (this file): CPU reference in q10 fixed-point, matching the exact
 * semantics the RSP ucode will implement (see DESIGN.md + RSP guide VMADl/m/adh).
 * Stage B: rsp_mandel.S ucode replaces the inner loop via accumulator fragments.
 *
 * Test loop: make && tools/n64test_ares.sh mandel.z64 25  -> vision-check fractal.
 */
#include <libdragon.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define W 320
#define H 240
#define LANES 8
#define MAXITER 60

/* q10 fixed point: |value| < 32768/1024 = 32; escape radius 4 (q10: 4096) */
#define Q 10
#define ESC_R2_Q20 (4 * 4 << (2 * Q))   /* |z|^2 > 4, in q20 */

/* Stage B: rsp_mandel.S ucode replaces this inner loop via accumulator fragments. */
/* DEFINE_RSP_UCODE(rsp_mandel);  -- enable when rsp_mandel.S exists */

static int32_t q10(double v)
{
    double s = v * (double)(1 << Q);
    if (s > 32000.0) s = 32000.0;
    if (s < -32000.0) s = -32000.0;
    return (int32_t)s;
}

/* CPU reference using integer q10 math only (mirrors RSP fragment ops):
 * z = z^2 + c ; escape when zr^2+zi^2 > 4<<20 ; clamp |z|<=3.9 after each step */
static int mandel_q10(int32_t cr, int32_t ci, int maxit)
{
    int32_t zr = 0, zi = 0;
    int i;
    for (i = 0; i < maxit; i++) {
        int64_t zr2 = ((int64_t)zr * zr) >> Q;
        int64_t zi2 = ((int64_t)zi * zi) >> Q;
        int64_t zr2i = ((int64_t)zr * zi);          /* q20 */
        if (zr2 + zi2 > (4LL << Q)) return i;        /* q10 domain test */
        int64_t nzr = zr2 - zi2 + cr;                 /* q10 */
        int64_t nzi = ((2 * zr2i) >> Q) + ci;        /* q10 */
        /* clamp +-3993 (q10 of 3.9) */
        if (nzr >  3993) nzr =  3993;
        if (nzr < -3993) nzr = -3993;
        if (nzi >  3993) nzi =  3993;
        if (nzi < -3993) nzi = -3993;
        zr = (int32_t)nzr; zi = (int32_t)nzi;
    }
    return i;
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

/* row-by-row progressive paint state */
static int row = 0;

int main(void)
{
    debug_init_emulog();
    display_init(RESOLUTION_320x240, DEPTH_32_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();
    rdpq_text_register_font(1, rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_VAR));

    const double x0 = -2.0, x1 = 0.6, y0 = -1.1, y1 = 1.1;
    long frame = 0;
    printf("[n64] mandel stage-A boot: %dx%d maxiter=%d q10\n", W, H, MAXITER);

    while (1) {
        surface_t *fb = display_get();

        /* CPU writes directly into the framebuffer (RGBA32, R in high byte) */
        for (int rr = 0; rr < 4; rr++) {
            if (row >= H) row = 0;   /* loop re-render */
            double cy = y0 + (y1 - y0) * row / H;
            int32_t ci = q10(cy);
            uint32_t *dst = (uint32_t*)fb->buffer + (size_t)row * W;
            for (int px_x = 0; px_x < W; px_x++) {
                double cx = x0 + (x1 - x0) * px_x / W;
                dst[px_x] = color_to_packed32(palette(mandel_q10(q10(cx), ci, MAXITER)));
            }
            row++;
        }

        rdpq_attach(fb, NULL);
        char msg[48];
        snprintf(msg, sizeof(msg), "frame=%ld row=%d/%d", frame, row, H);
        rdpq_text_print(NULL, 1, 4, 224, msg);
        rdpq_detach_show();
        frame++;
    }
}
