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
/* stage halfword-index helpers (byte offset >> 1) — stage-F ucode map:
   0x0000 row0 out | 0x0280 row0 sout | 0x0500 row1 out | 0x0780 row1 sout
   0x0A00 x-LUT (resident) | 0x0C80 nchunks | 0x0C90 one | 0x0CA0 escm
   0x0CB0 ci0 | 0x0CC0 ci1 | 0x0D80 probe */
#define OUT_BYTE 0x0000                   /* read-back span start (2 KB) */
#define NC_HW    (0x0C80 / 2)
#define ONE_HW   (0x0C90 / 2)
#define ESCM_HW  (0x0CA0 / 2)
#define XLUT_HW  (0x0A00 / 2)
#define CI0_HW   (0x0CB0 / 2)
#define RUNUP_HW (0x0CD0 / 2)           /* upload span 0x0A00..0x0CC0 */
#define PROBE_HW (0x0D80 / 2)
#define STAGE_HW (0x0E70 / 2)

static uint16_t stage[STAGE_HW] __attribute__((aligned(8)));
static uint16_t rdbuf[1280] __attribute__((aligned(8)));   /* 2 rows: cnt|S ×2 */
#endif /* USE_RSP */

/* ---------------- zoom tour keyframes ----------------
 * hold = frames parked at this key before moving to next;
 * span interpolates geometrically across a fixed 150-frame move. */
typedef struct { double cx, cy, span; int hold; } Key;
static const Key keys[] = {
    { -0.700000,  0.000000, 2.600,  45 },   /* home */
    { -0.743500, -0.131400, 0.0800, 30 },  /* seahorse valley entry */
    { -0.745800, -0.114900, 0.0200, 45 },  /* seahorse bodies (RSP) */
    { -0.7436438870371, 0.1318259042053, 5e-5, 60 }, /* SEAHORSE TAIL — CPU */
    { -0.745800, -0.114900, 0.0200, 20 },  /* pull back to RSP range */
    { -0.235125,  0.827215, 0.0200, 40 },  /* elephant valley wing */
    { -0.700000,  0.000000, 2.600,  60 },  /* back home, park */
};
#define NKEY ((int)(sizeof(keys)/sizeof(keys[0])))
#define MOVE_FRAMES 150

/* RSP q12 iteration is exact only while the per-pixel step is ≥ quantum
   2^-12 ⇒ span ≥ 320·2^-12 = 0.078; below that deeper views run the CPU
   double path (seahorse-tail grade coordinates). */
#define RSP_MIN_SPAN 0.078

#ifndef START_FRAME
#define START_FRAME 0
#endif

/* (home-view bounds kept only for legacy verify refs; deep path recomputes) */

/* ---- stage J: HUD + preset fly-to ---- */
#include "font8x8_min.h"

static int   hud_on = 1;

/* ---- stage L2: keyframe ring (fluid deep playback) ----
 * CPU deep zone (span < RSP_MIN_SPAN) renders at seconds/frame. Live deep
 * views instead AFFINE-MORPH between two stored keyframes: geometrically
 * exact mapping, cost ~2 fetches/pixel instead of NITER loops. Keys are
 * saved by genuine renders whenever the live span strays more than
 * RING_RATIO from every stored key (or no shallower key exists), so the
 * ring auto-populates while touring and hitches only at new depth. */
#define RING_CAP   24             /* full tour deep leg = ~21 octave buckets;
                                     capacity >= that means no re-render churn.
                                     24 half-res keys ~ 1.8 MB RDRAM. */
#define RING_RATIO 2.0
#define RING_TOP   0.078          /* span of bucket 0 == RSP_MIN_SPAN */
#define RW         (W / 2)         /* ring key resolution: half, upscaled on */
#define RH         (H / 2)

typedef struct { double span, cx, cy; int valid, fq; uint32_t px[RW * RH]; } RKey;
static RKey ring[RING_CAP] = { {0} };
static int  ring_n = 0;

/* stage P hold-upgrade: first deep frame per bucket renders quarter-res
   (fast); once the view holds still for HOLD_FRAMES, one full-res CPU
   render upgrades that bucket's key to full quality (fq=1). */
#ifndef HOLD_FRAMES
#define HOLD_FRAMES 3
#endif
static double hold_cx = 0, hold_cy = 0, hold_span = -1;
static int    hold_n = 0;

/* stage P static caching: deep frames recompute only when the view moves;
   a held deep view displays its cached painted buffer for FREE, and one
   full-res render per bucket upgrades quality (fq keys). */
static uint32_t hold_pix[W * H];
static int      hold_valid = 0;




/* Can the ring represent this view? Bracketing keys within log-span window
   and pan tolerance; affine map is exact, slight pan shows as edge clamp
   smear during fast diagonal moves (acceptable, standard zoom-morph look). */
static int ring_lookup(double cx, double cy, double span, int *ia, int *ib)
{
    double lv = log(span);
    int a = -1, b = -1;
    double da = 1e30, db = 1e30;
    const double DMAX = log(RING_RATIO * 2.0);
    for (int i = 0; i < ring_n; i++) {
        if (!ring[i].valid) continue;
        double dl = fabs(log(ring[i].span) - lv);
        if (dl > DMAX) continue;
        double mx = (ring[i].span + span) * 0.75;   /* generous pan tolerance:
            morph mapping is geometrically exact; off-center pans just clamp
            at texture edges, which the zoom hides almost entirely. */
        if (fabs(cx - ring[i].cx) > mx || fabs(cy - ring[i].cy) > mx) continue;
        if (ring[i].span >= span) { if (dl < da) { da = dl; a = i; } }
        else                      { if (dl < db) { db = dl; b = i; } }
    }
    if (a < 0 && b < 0) return 0;
    if (a < 0) a = b;
    if (b < 0) b = a;
    *ia = a; *ib = b;
    return 1;
}


static void ring_save(double cx, double cy, double span, const uint32_t *pix)
{
    /* bucket = depth-octave below the RSP boundary; 0 = shallowest deep
       octave, +1 per 2x deeper. One slot per octave, re-entry overwrites. */
    int bkt = (int)floor(log(RING_TOP / span) / log(2.0));
    if (bkt < 0) bkt = 0;
    int slot = bkt % RING_CAP;
    RKey *k = &ring[slot];
    k->cx = cx; k->cy = cy; k->span = span;
    uint32_t *d = k->px;
    for (int y = 0; y < RH; y++) {
        const uint32_t *s = pix + (size_t)(y * 2) * W;
        for (int x = 0; x < RW; x++) d[y * RW + x] = s[x * 2];
    }
    k->valid = 1; k->fq = 1;
    if (slot + 1 > ring_n) ring_n = slot + 1;
}

static inline uint32_t blend32(uint32_t c0, uint32_t c1, int t8)
{
    int r = ((int)(c0 & 0xFF) * (256 - t8) + (int)(c1 & 0xFF) * t8) >> 8;
    int g = ((int)((c0 >> 8) & 0xFF) * (256 - t8) + (int)((c1 >> 8) & 0xFF) * t8) >> 8;
    int b = ((int)((c0 >> 16) & 0xFF) * (256 - t8) + (int)((c1 >> 16) & 0xFF) * t8) >> 8;
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | 0xFF000000u;
}

/* stage M: bilinear texel fetch — half-res keys upscale with AA instead
   of stair-stepped nearest neighbour. fx/fy are 8-bit fractions. */
static inline uint32_t bil_key(const RKey *k, int xa, int ya)
{
    int sx = xa >> 16, sy = ya >> 16;
    int fx = (xa >> 8) & 255, fy = (ya >> 8) & 255;
    if (sx < 0) { sx = 0; fx = 0; }
    if (sy < 0) { sy = 0; fy = 0; }
    if (sx > RW - 2) sx = RW - 2;
    if (sy > RH - 2) sy = RH - 2;
    const uint32_t *r0 = k->px + (size_t)sy * RW + sx;
    const uint32_t *r1 = r0 + RW;
    return blend32(blend32(r0[0], r0[1], fx),
                   blend32(r1[0], r1[1], fx), fy);
}

/* Affine map each output pixel into both keys' textures (integer q16 steps,
   MIPS-friendly) and blend. Exact geometry: u = (w - key.c)/key.span + 0.5 */
static void ring_morph(uint32_t *out, double cx, double cy, double span, int ia, int ib)
{
    const RKey *ka = &ring[ia], *kb = &ring[ib];
    double la = log(ka->span), lb = log(kb->span), lv = log(span);
    double t = (la == lb) ? 0.0 : (lv - la) / (lb - la);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int t8 = (int)(t * 256.0);
    double vy0 = cy - span / 2, vx0 = cx - span / 2;
    double dx = span / W, dy = span / H;
    double sa = RW / ka->span, sb = RW / kb->span;
    /* row-independent column increments (q16 of texel coords) */
    int dxa = (int)(dx * sa * 65536.0), dxb = (int)(dx * sb * 65536.0);
    for (int y = 0; y < H; y++) {
        double wy = vy0 + dy * y;
        int ya = (int)(((wy - ka->cy) * sa + 0.5) * 65536.0);
        int yb = (int)(((wy - kb->cy) * sb + 0.5) * 65536.0);
        int xa = (int)(((vx0 - ka->cx) * sa + 0.5) * 65536.0);
        int xb = (int)(((vx0 - kb->cx) * sb + 0.5) * 65536.0);
        uint32_t *o = out + (size_t)y * W;
        for (int x = 0; x < W; x++) {
            o[x] = blend32(bil_key(ka, xa, ya), bil_key(kb, xb, yb), t8);
            xa += dxa; xb += dxb;
        }
        /* vertical steps handled by wy at row top */
        (void)ya; (void)yb;
    }
    (void)dy;
}

typedef struct { double cx, cy, span; const char *name; } Preset;
static const Preset presets[] = {
    { -0.700000,          0.000000,         2.6,   "HOME" },
    { -0.745800,         -0.114900,         0.02,  "SEAHORSE" },
    { -0.7436438870371,   0.1318259042053, 5e-5,  "TAIL" },
    { -0.745000,         -0.115000,         0.30,  "VALLEY" },
};
#define NPRESET ((int)(sizeof(presets)/sizeof(presets[0])))

/* stage M: user bookmarks — save current view on X, fly with ZL */
#define NBOOK 8
typedef struct { double cx, cy, span; int valid; } Book;
static Book books[NBOOK] = { {0} };
static int  book_slot = 0;

/* stage M: palette cycling — hue phase drift folded into smooth lookup */
static int pal_phase = 0;        /* added to smooth_color idx */
static int cycle_on = 1;
static int book_blink = 0;       /* HUD blink frames after save/recall */
static long long hud_tprev = 0;  /* fps EMA state */
static long long hud_fps = 60000;/* fps * 1000, EMA over frame times */

static int   fly_active = 0;
static double f_cx, f_cy, f_span;
static const char *fly_name = "PRESET";
#ifdef AUTOTEST
static int fly_req = -1;
#endif

static void fmt_span(double s, char *out)
{
    if (s >= 0.001) snprintf(out, 16, "%.4f", s);
    else snprintf(out, 16, "%.1E", s);       /* uppercase E — font lacks e */
}

static void hud_char(uint32_t *fb, int x, int y, char c, uint32_t fg, uint32_t bg)
{
    if (c < FONT_FIRST || c > FONT_LAST) c = '?';
    const uint8_t *g = font8x8[c - FONT_FIRST];
    for (int ry = 0; ry < 8; ry++) {
        uint32_t *d = fb + (size_t)(y + ry) * W + x;
        uint8_t bm = g[ry];
        for (int rx = 0; rx < 8; rx++)
            d[rx] = (bm & (0x80 >> rx)) ? fg : bg;
    }
}

static void hud_text(uint32_t *fb, int x, int y, const char *s, uint32_t fg, uint32_t bg)
{
    while (*s && x + 8 <= W) {
        hud_char(fb, x, y, *s++, fg, bg);
        x += 8;
    }
}

static uint16_t cxq[W];                  /* q12 c_x for current view */
static uint16_t cyq[H];                  /* q12 c_y per row */
static uint16_t rows_cnt[H][W];         /* ucode escape counts */
static uint16_t rows_s[H][W];           /* ucode escape S q8 */

/* stage P: quarter-res progressive deep rendering (CPU path).
 * Deep first-visit frames render the exact iteration field on a 4x coarser
 * lattice (QW x QH = 80x60 = 1/16 the pixels) and display via bilinear 4x
 * upscale — ~16x cheaper than full CPU, ~19 ms budget at 1200 iter. Painted
 * colors live in qring slots; motion morphs between quarter keys (qring_*),
 * holds upgrade half-res keys via occasional full CPU frames (ring_save).
 * NO_PROG reverts to per-frame full-res CPU deep rendering. */
#define QW (W / 4)
#define QH (H / 4)
#define QBANDS 8
static uint16_t q_cnt[QH][QW];
static uint16_t q_s[QH][QW];
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

static int g_interior = 120;               /* counts ≥ this = interior */

static inline uint32_t smooth_color(int n, uint16_t S)
{
    if (n >= g_interior) return color_to_packed32(RGBA32(8, 4, 16, 255));
    int sm = n * 4096 + (int)smooth_tab[S > 32767 ? 32767 : S];
    int idx = ((sm * 4) >> 11) + pal_phase;
    return pal_lut[idx & (PALN - 1)];
}

static void ring_save_q(double cx, double cy, double span, const uint16_t *qc, const uint16_t *qs)
{
    /* quarter lattice (q_cnt/q_s) -> half-res key via bilinear smooth color;
       never clobber a full-quality key. */
    int bkt = (int)floor(log(RING_TOP / span) / log(2.0));
    if (bkt < 0) bkt = 0;
    int slot = bkt % RING_CAP;
    RKey *k = &ring[slot];
    if (k->valid && k->fq) return;
    k->cx = cx; k->cy = cy; k->span = span;
    for (int y = 0; y < RH; y++) {
        uint32_t *dd = k->px + (size_t)y * RW;
        double qy = y * 0.5;
        int y0q = (int)qy; double ty = qy - y0q;
        if (y0q >= QH - 1) { y0q = QH - 2; ty = 1.0; }
        int tyi = (int)(ty * 255);
        for (int x = 0; x < RW; x++) {
            double qx = x * 0.5;
            int x0q = (int)qx; double tx = qx - x0q;
            if (x0q >= QW - 1) { x0q = QW - 2; tx = 1.0; }
            int txi = (int)(tx * 255);
            uint32_t c00 = smooth_color(qc[y0q * QW + x0q], qs[y0q * QW + x0q]);
            uint32_t c10 = smooth_color(qc[y0q * QW + x0q + 1], qs[y0q * QW + x0q + 1]);
            uint32_t c01 = smooth_color(qc[(y0q + 1) * QW + x0q], qs[(y0q + 1) * QW + x0q]);
            uint32_t c11 = smooth_color(qc[(y0q + 1) * QW + x0q + 1], qs[(y0q + 1) * QW + x0q + 1]);
            dd[x] = blend32(blend32(c00, c10, txi), blend32(c01, c11, txi), tyi);
        }
    }
    k->valid = 1; k->fq = 0;
    if (slot + 1 > ring_n) ring_n = slot + 1;
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
    if (keys[i].span < RSP_MIN_SPAN || keys[next].span < RSP_MIN_SPAN) {
        /* deep-touching segment: zoom to corridor -> pan fast at corridor
           span -> dive/zoom at one fixed center. Dive frames share the
           target center with ring keys (delta=0), so after one pass the
           whole descent morphs at full fps instead of re-rendering. */
        double u = 0;
        if (tf >= keys[i].hold) u = (tf - keys[i].hold) / MOVE_FRAMES;
        double s0 = keys[i].span, s1 = keys[next].span;
        double corr = (s0 < s1 ? s0 : s1);            /* shallowest endpoint */
        if (corr < RSP_MIN_SPAN) corr = RSP_MIN_SPAN;/* ...but stay on RSP */
        if (u < 0.25) {                                /* phase 1: reach corridor */
            double p = (u / 0.25); p = p * p * (3 - 2 * p);
            *span = exp(log(s0) + (log(corr) - log(s0)) * p);
            *cx = keys[i].cx; *cy = keys[i].cy;
        } else if (u < 0.5) {                        /* phase 2: pan at corridor */
            double p = (u - 0.25) / 0.25; p = p * p * (3 - 2 * p);
            *cx = keys[i].cx + (keys[next].cx - keys[i].cx) * p;
            *cy = keys[i].cy + (keys[next].cy - keys[i].cy) * p;
            *span = corr;
        } else {                                     /* phase 3: zoom at target c */
            double p = (u - 0.5) / 0.5; p = p * p * (3 - 2 * p);
            *cx = keys[next].cx; *cy = keys[next].cy;
            *span = exp(log(corr) + (log(s1) - log(corr)) * p);
        }
    } else {
        double e = 0;
        if (tf >= keys[i].hold) {
            double u = (tf - keys[i].hold) / MOVE_FRAMES;
            e = u * u * (3.0 - 2.0 * u);
        }
        *cx = keys[i].cx + (keys[next].cx - keys[i].cx) * e;
        *cy = keys[i].cy + (keys[next].cy - keys[i].cy) * e;
        double ls = log(keys[i].span), ln = log(keys[next].span);
        *span = exp(ls + (ln - ls) * e);
    }
}

int main(void)
{
    debug_init_emulog();   /* rdpq stays OFF (it would own the RSP) */
    display_init(RESOLUTION_320x240, DEPTH_32_BPP, 2, GAMMA_NONE, FILTERS_DISABLED);
    timer_init();
    joypad_init();

    build_palette();
    build_smooth_tab();

#if USE_RSP
    rsp_init();
    rsp_load(&rsp_mandel);
    for (int i = 0; i < 8; i++) {
        stage[ONE_HW + i] = 1;
        stage[ESCM_HW + i] = 1023;   /* esc-1: borrow test S >= 1024 */
    }
    uint32_t nch = NCHUNK;
    memcpy(&stage[NC_HW], &nch, 4);
    /* ucode boots once; x-LUT + ci-pair + nchunks re-uploaded per run */
    rsp_load_data(stage, sizeof(stage), 0);
#endif

    long frame = START_FRAME;
    long long us_rsp = 0, us_pack = 0, us_paint = 0;

    /* interactive view state (stage H): auto=1 → zoom tour, auto=0 → pad */
    int auto_mode = 1;
    double vcx = keys[0].cx, vcy = keys[0].cy, vspan = keys[0].span;
    int pad_active = 0, pad_inactive_frames = 0;

    printf("[n64] mandel stage-E boot: %dx%d zoom tour NKEY=%d verify=%d\n",
           W, H, NKEY, VERIFY);

    while (1) {
        surface_t *fb = display_get();
        uint32_t *pix = (uint32_t *)fb->buffer;

        /* ---- stage H: controller input ---- */
        joypad_inputs_t jin = joypad_get_inputs(JOYPAD_PORT_1);
#ifdef AUTOTEST
        /* scripted input for headless verification (SI not polled in fork
           headless JS runner): cycle presets through the same fly path */
        {
            static int auto_prev = -1;
            int slot = (int)((frame / 20) % (NPRESET + 1));
            if (slot != auto_prev) {
                auto_prev = slot;
                fly_req = (slot < NPRESET) ? slot : -2;   /* -2 = Start */
            }
        }
#endif
        int sx = jin.stick_x, sy = jin.stick_y;
        if (sx > -7 && sx < 7) sx = 0;            /* stick deadzone */
        if (sy > -7 && sy < 7) sy = 0;
        int bx = (int)jin.btn.d_right - (int)jin.btn.d_left;
        int by = (int)jin.btn.d_down - (int)jin.btn.d_up;
#ifdef AUTOTEST
        if (fly_req >= 0) { int pidx = fly_req; fly_active = 1;
            f_cx = presets[pidx].cx; f_cy = presets[pidx].cy;
            f_span = presets[pidx].span; fly_name = presets[pidx].name;
            auto_mode = 0; pad_active = 1; fly_req = -1; }
        else if (fly_req == -2) { auto_mode = 1; pad_active = 0; fly_active = 0; fly_req = -1; }
#endif
        int any_btn = jin.btn.a || jin.btn.b || jin.btn.z || jin.btn.l ||
                      jin.btn.r || jin.btn.start;
        if (sx || sy || bx || by || any_btn) pad_inactive_frames = 0;
        else if (pad_active) pad_inactive_frames++;
        if (jin.btn.start) { auto_mode = 1; pad_active = 0; fly_active = 0; }
        /* C-buttons: fly to presets */
        {
            int pidx = -1;
            if (jin.btn.c_up)    pidx = 0;
            if (jin.btn.c_right) pidx = 1;
            if (jin.btn.c_down)  pidx = 2;
            if (jin.btn.c_left)  pidx = 3;
            if (pidx >= 0) {
                fly_active = 1;
                f_cx = presets[pidx].cx; f_cy = presets[pidx].cy;
                f_span = presets[pidx].span;
                fly_name = presets[pidx].name;
                auto_mode = 0; pad_active = 1;
            }
        }
        if ((sx || sy || bx || by || jin.btn.z ||
             jin.btn.l || jin.btn.r) && !jin.btn.start) {
            auto_mode = 0;
            pad_active = 1;
        }
        static int prev_b = 0;
        if (jin.btn.b && !prev_b) hud_on = !hud_on;
        prev_b = jin.btn.b;
        /* stage M: Z save bookmark, A recall, L+R palette cycle toggle */
        static int prev_z = 0, prev_a = 0, prev_lr = 0;
        if (jin.btn.z && !prev_z) {
            books[book_slot].cx = vcx; books[book_slot].cy = vcy;
            books[book_slot].span = vspan; books[book_slot].valid = 1;
            book_slot = (book_slot + 1) % NBOOK;
            book_blink = 90;
        }
        if (jin.btn.a && !prev_a) {
            int s = -1;
            for (int i = NBOOK - 1; i >= 0; i--)
                if (books[i].valid) { s = i; break; }
            if (s >= 0) {
                fly_active = 1;
                f_cx = books[s].cx; f_cy = books[s].cy; f_span = books[s].span;
                fly_name = "BOOKMARK";
                auto_mode = 0; pad_active = 1;
                book_blink = 90;
            }
        }
        int lr = jin.btn.l && jin.btn.r;
        if (lr && !prev_lr) cycle_on = !cycle_on;
        prev_z = jin.btn.z; prev_a = jin.btn.a; prev_lr = lr;
        if (pad_active && pad_inactive_frames > 300) { auto_mode = 1; pad_active = 0; }

        double cx0, cy0, span;
        const char *view_name;
        if (auto_mode) {
            view_at(frame, &cx0, &cy0, &span);
            vcx = cx0; vcy = cy0; vspan = span;
            view_name = "TOUR";
        } else if (fly_active) {
            /* exponential ease toward preset; geometric span glide */
            double e = 0.06;
            vcx += (f_cx - vcx) * e;
            vcy += (f_cy - vcy) * e;
            double ls = log(vspan), ln = log(f_span);
            vspan = exp(ls + (ln - ls) * e);
            if (fabs(vspan - f_span) < f_span * 1e-4 &&
                fabs(vcx - f_cx) < 1e-9 && fabs(vcy - f_cy) < 1e-9) {
                vcx = f_cx; vcy = f_cy; vspan = f_span;
                fly_active = 0;
            }
            cx0 = vcx; cy0 = vcy; span = vspan;
            view_name = fly_name;
        } else {
            view_name = "MANUAL";
            double zoomf = 1.0;
            /* stage M: ZR zoom in, ZL zoom out (C-buttons keep preset fly) */
            if (jin.btn.r) zoomf = 0.94;                     /* ZR */
            if (jin.btn.l) zoomf = 1.06;                     /* ZL */
            double pan_scale = vspan / 900.0;
            double mx = (sx ? (double)sx : (double)bx * 60.0);
            double my = (sy ? -(double)sy : (double)by * 60.0); /* stick up = -imag */
            vcx += mx * pan_scale;
            vcy += my * pan_scale;
            vspan *= zoomf;
            if (vspan < 5e-5) vspan = 5e-5;
            if (vspan > 4.0)  vspan = 4.0;
            cx0 = vcx; cy0 = vcy; span = vspan;
        }
        double dx = span / W, dy = span / H;
        double x0 = cx0 - span / 2, y0 = cy0 - span / 2;

        long long t0 = timer_ticks();
        for (int x = 0; x < W; x++)
            cxq[x] = (uint16_t)q12(x0 + dx * x);
        for (int y = 0; y < H; y++)
            cyq[y] = (uint16_t)q12(y0 + dy * y);
        us_pack += TIMER_MICROS_LL(timer_ticks() - t0);

        long long tdeep0 = timer_ticks();   /* stage P: deep compute cost */

        int use_rsp = 0;
#if USE_RSP
        use_rsp = span >= RSP_MIN_SPAN;
#endif
        int iters = (int)(60.0 + 90.0 * log2(2.6 / (span < 1e-9 ? 1e-9 : span)));
        if (iters < MAXITER) iters = MAXITER;
        if (iters > 1200) iters = 1200;

        int rowmax = H, mirror = 0;
        int mismatch = 0, maxc = 0;
        static int mm_y[4], mm_x[4], mm_v[4], mm_r[4];
        for (int i = 0; i < 4; i++) { mm_y[i] = -1; mm_x[i] = -1; mm_v[i] = -1; mm_r[i] = -1; }
        g_interior = use_rsp ? 120 : iters;   /* ucode saturates at NITER=120 */

#if USE_RSP
        if (use_rsp) {
            memcpy(&stage[XLUT_HW], cxq, sizeof(cxq));
            double ytop = y0, ybot = cy0 + span / 2;
            if (ytop >= 0.0) {
                mirror = 0;
            } else if (fabs(ytop + ybot) < 1e-9) {
                mirror = 1;
                rowmax = H / 2 + 1;
            }

            for (int y = 0; y < rowmax; y += 2) {
                uint16_t c0 = cyq[y];
                uint16_t c1 = (y + 1 < rowmax) ? cyq[y + 1] : c0;
                for (int i = 0; i < 8; i++) {
                    stage[CI0_HW + i] = c0;
                    stage[CI0_HW + 8 + i] = c1;
                }
                data_cache_hit_writeback_invalidate(stage, sizeof(stage));

                rsp_load_data(stage, sizeof(stage), 0);
                t0 = timer_ticks();
                rsp_run();
                rsp_read_data(rdbuf, sizeof(rdbuf), OUT_BYTE);
                us_rsp += TIMER_MICROS_LL(timer_ticks() - t0);
                int y2 = y + 1;
                memcpy(rows_cnt[y], rdbuf, W * 2);
                memcpy(rows_s[y], rdbuf + W, W * 2);
                if (y2 < rowmax) {
                    memcpy(rows_cnt[y2], rdbuf + 2 * W, W * 2);
                    memcpy(rows_s[y2], rdbuf + 3 * W, W * 2);
                }
            }

            for (int y = 0; y < rowmax; y++)
                for (int x = 0; x < W; x++) {
                    int v = rows_cnt[y][x];
                    if (v > maxc) maxc = v;
#if VERIFY
                    if (v != mandel_q12((int)(int16_t)cxq[x], (int)(int16_t)cyq[y])) {
                        if (mismatch < 4) {
                            mm_y[mismatch] = y; mm_x[mismatch] = x;
                            mm_v[mismatch]  = v;
                            mm_r[mismatch]  = mandel_q12((int)(int16_t)cxq[x], (int)(int16_t)cyq[y]);
                        }
                        mismatch++;
                    }
#endif
                }
        }
#endif /* USE_RSP */

        int morphed = 0, qpainted = 0;
        /* stage P hold tracker: same view spans consecutive frames */
        if (cx0 == hold_cx && cy0 == hold_cy && span == hold_span) hold_n++;
        else { hold_cx = cx0; hold_cy = cy0; hold_span = span; hold_n = 1; }
        if (use_rsp) hold_valid = 0;
        if (!use_rsp && hold_valid && hold_n >= 2) {
            /* deep view held: re-display cached paint for FREE unless a
               full-quality upgrade is pending (bucket without fq key) */
            int bkt = (int)floor(log(RING_TOP / span) / log(2.0));
            if (bkt < 0) bkt = 0;
            int slot = bkt % RING_CAP;
            int upg = hold_n >= HOLD_FRAMES && !(ring[slot].valid && ring[slot].fq);
            if (!upg) {
                memcpy(pix, hold_pix, sizeof(hold_pix));
                qpainted = 1;
            }
        }
#ifndef RING_OFF
        if (!use_rsp && !qpainted) {
            /* deep zone: prefer ring morph over CPU render; a held view
               whose bracket lacks fq keys falls through to the upgrade */
            int ia, ib;
            if (ring_lookup(cx0, cy0, span, &ia, &ib) &&
                ((ring[ia].fq && ring[ib].fq) || hold_n < HOLD_FRAMES)) {
                ring_morph(pix, cx0, cy0, span, ia, ib);
                morphed = 1;
            }
        }
#endif
        if (!use_rsp && !morphed && !qpainted) {
            /* first frame of a view = quarter lattice (~0.84 s vs 6.5 s);
               once held, one full-res render upgrades the bucket key
               (fq=1) and refreshes the static cache. */
            int fullq = (hold_n >= HOLD_FRAMES);
            if (fullq) {
                /* full-res CPU pass: upgrade rows_cnt + fq ring key */
                for (int y = 0; y < H; y++) {
                    double ci = y0 + dy * y;
                    double ci2 = ci * ci;
                    for (int x = 0; x < W; x++) {
                        double cr = x0 + dx * x;
                        double zr = 0, zi = 0, S = 0;
                        int cnt = iters;
#if !defined(NO_L1)
                        double crm = cr - 0.25;
                        double q = crm * crm + ci2;
                        if (q * (q + crm) < 0.25 * ci2) {
                            cnt = iters; S = q;
                        } else if ((cr + 1.0) * (cr + 1.0) + ci2 < 0.0625) {
                            cnt = iters; S = 0.0;
                        } else
#else
                        if (0) { } else
#endif
                        {
                            for (int i = 0; i < iters; i++) {
                                double r2 = zr * zr, i2 = zi * zi;
                                S = r2 + i2;
                                if (S >= 4.0) { cnt = i; break; }
                                double t = r2 - i2;
                                zi = 2.0 * zr * zi + ci;
                                zr = t + cr;
                            }
                        }
                        if (cnt > maxc) maxc = cnt;
                        int s8 = (int)(S * 1024.0);
                        rows_cnt[y][x] = (uint16_t)cnt;
                        rows_s[y][x] = (uint16_t)(s8 > 32767 ? 32767 : (s8 < 0 ? 0 : s8));
                    }
                }
            } else {
                /* quarter lattice pass (stage P) */
                for (int qy = 0; qy < QH; qy++) {
                    double ci = y0 + dy * (qy * 4);
                    double ci2 = ci * ci;
                    for (int qx = 0; qx < QW; qx++) {
                        double cr = x0 + dx * (qx * 4);
                        double zr = 0, zi = 0, S = 0;
                        int cnt = iters;
#if !defined(NO_L1)
                        double crm = cr - 0.25;
                        double q = crm * crm + ci2;
                        if (q * (q + crm) < 0.25 * ci2) {
                            cnt = iters; S = q;
                        } else if ((cr + 1.0) * (cr + 1.0) + ci2 < 0.0625) {
                            cnt = iters; S = 0.0;
                        } else
#else
                        if (0) { } else
#endif
                        {
                            for (int i = 0; i < iters; i++) {
                                double r2 = zr * zr, i2 = zi * zi;
                                S = r2 + i2;
                                if (S >= 4.0) { cnt = i; break; }
                                double t = r2 - i2;
                                zi = 2.0 * zr * zi + ci;
                                zr = t + cr;
                            }
                        }
                        if (cnt > maxc) maxc = cnt;
                        int s8 = (int)(S * 1024.0);
                        q_cnt[qy][qx] = (uint16_t)cnt;
                        q_s[qy][qx] = (uint16_t)(s8 > 32767 ? 32767 : (s8 < 0 ? 0 : s8));
                    }
                }
                /* bilinear color upsample q -> full display */
                qpainted = 1;
                for (int y = 0; y < H; y++) {
                    double qy = y * 0.25;
                    int y0q = (int)qy;
                    double ty = qy - y0q;
                    if (y0q >= QH - 1) { y0q = QH - 2; ty = 1.0; }
                    uint32_t *d = pix + (size_t)y * W;
                    for (int x = 0; x < W; x++) {
                        double qx = x * 0.25;
                        int x0q = (int)qx;
                        double tx = qx - x0q;
                        if (x0q >= QW - 1) { x0q = QW - 2; tx = 1.0; }
                        uint32_t c00 = smooth_color(q_cnt[y0q][x0q], q_s[y0q][x0q]);
                        uint32_t c10 = smooth_color(q_cnt[y0q][x0q + 1], q_s[y0q][x0q + 1]);
                        uint32_t c01 = smooth_color(q_cnt[y0q + 1][x0q], q_s[y0q + 1][x0q]);
                        uint32_t c11 = smooth_color(q_cnt[y0q + 1][x0q + 1], q_s[y0q + 1][x0q + 1]);
                        int txi = (int)(tx * 255), tyi = (int)(ty * 255);
                        d[x] = blend32(blend32(c00, c10, txi), blend32(c01, c11, txi), tyi);
                    }
                }
                /* quarter key: bilinear q -> half-res slot (skips fq keys) */
                ring_save_q(cx0, cy0, span, &q_cnt[0][0], &q_s[0][0]);
            }
        }

        long long us_deep = TIMER_MICROS_LL(timer_ticks() - tdeep0);
        long long t1 = timer_ticks();
        if (!morphed && !qpainted) {
        for (int y = 0; y < rowmax; y++) {
            uint32_t *d = pix + (size_t)y * W;
            uint16_t *cn = rows_cnt[y], *ss = rows_s[y];
            for (int x = 0; x < W; x++) d[x] = smooth_color(cn[x], ss[x]);
        }
        if (mirror) {
            for (int y = 0; y < rowmax; y++) {
                int ys = (H - 1) - y;
                if (ys >= H) continue;
                uint32_t *d = pix + (size_t)ys * W;
                uint16_t *cn = rows_cnt[y], *ss = rows_s[y];
                for (int x = 0; x < W; x++) d[x] = smooth_color(cn[x], ss[x]);
            }
        }
        us_paint += TIMER_MICROS_LL(timer_ticks() - t1);

        /* deep render: harvest a ring key only from genuine full-res CPU
           frames (not morph playback). */
        if (!use_rsp && !morphed)
            ring_save(cx0, cy0, span, pix);
        }

        /* static cache refresh: any freshly computed/morphed deep frame
           becomes the free-display content for a held view */
        if (!use_rsp) {
            memcpy(hold_pix, pix, sizeof(hold_pix));
            hold_valid = 1;
        }

        /* ---- HUD ---- */
        if (cycle_on) pal_phase = (pal_phase + 3) & (PALN - 1);
        if (book_blink > 0) book_blink--;
        /* fps: EMA over real elapsed ticks (2 ms granularity timer) */
        long long now = timer_ticks();
        long long dt_us = TIMER_MICROS_LL(now - hud_tprev);
        hud_tprev = now;
        if (dt_us > 0 && dt_us < 60000000)
            hud_fps += (1000000000 / dt_us - hud_fps) / 8;
        if (hud_fps < 1) hud_fps = 1;
        long long fps_milli = hud_fps;   /* fps * 1000 */
        if (hud_on) {
            char sp[16]; fmt_span(span, sp);
            char xs[12], ys[12];
            snprintf(xs, sizeof(xs), "%.6f", cx0);
            snprintf(ys, sizeof(ys), "%.6f", cy0);
            char l1[64], l2[64], l3[64];
            snprintf(l1, sizeof(l1), "MANDEL 64  %s", view_name);
            snprintf(l2, sizeof(l2), "X%s Y%s SPAN %s %s IT%d",
                     xs, ys, sp, use_rsp ? "RSP" : "CPU", iters);
            snprintf(l3, sizeof(l3), "%dFPS  %s CYC%d BK%d",
                     (int)(hud_fps / 1000),
                     morphed ? "MORPH" : "RENDER",
                     cycle_on, book_slot);
            uint32_t cblack = color_to_packed32(RGBA32(0, 0, 0, 255));
            uint32_t cwhite = color_to_packed32(RGBA32(255, 255, 255, 255));
            uint32_t cteal  = color_to_packed32(RGBA32(0, 255, 170, 255));
            uint32_t cyel   = color_to_packed32(RGBA32(255, 220, 60, 255));
            for (int y = 0; y < 10; y++)
                for (int x = 0; x < W; x++) pix[y * W + x] = cblack;
            hud_text(pix, 4, 1, l1, cwhite, cblack);
            for (int y = 10; y < 20; y++)
                for (int x = 0; x < W; x++) pix[y * W + x] = cblack;
            hud_text(pix, 4, 11, l2, cteal, cblack);
            /* ring occupancy bar + morph badge */
            for (int y = 20; y < 28; y++)
                for (int x = 0; x < W; x++) pix[y * W + x] = cblack;
            hud_text(pix, 4, 21, l3, book_blink ? cyel : cteal, cblack);
            int bx0 = W - 8 * 24 - 6;
            for (int i = 0; i < RING_CAP; i++) {
                uint32_t cc = ring[i].valid ? cteal : color_to_packed32(RGBA32(40, 40, 40, 255));
                hud_char(pix, bx0 + i * 8, 21, ring[i].valid ? '#' : '.', cc, cblack);
            }
        }

        display_show(fb);

        /* probe AFTER flip: probe-gated screenshots (deepcap) then always see
           the frame they describe — pre-flip gating captured black buffers. */
        if ((frame % 60) == 0
#ifdef AUTOTEST
            || 1
#endif
        ) {
            char line[256];
            int n = snprintf(line, sizeof(line),
                "[probe] f=%ld cx=%.9f cy=%.9f span=%.8f path=%s mism=%d max=%d it=%d rsp_ms=%lld deep_us=%lld fps1000=%lld btn=%04X conn=%d fly=%d name=%s keys=%d morph=%d mm0=%d/%d/%d/%d\n",
                (long)frame, cx0, cy0, span, use_rsp ? "rsp" : "cpu",
                mismatch, maxc, iters,
                (long long)(us_rsp / 1000), us_deep, fps_milli, jin.btn.raw,
                (int)joypad_is_connected(JOYPAD_PORT_1), fly_active, view_name,
                ring_n, morphed,
                mm_y[0], mm_x[0], mm_v[0], mm_r[0]);
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

        frame++;
    }
}
