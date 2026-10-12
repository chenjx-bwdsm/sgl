/* sgl/source/components/bench/sgl_bench.c
 *
 * SGL performance benchmark, modeled after LVGL's "benchmark" demo.
 *
 * Every scene fills the screen with widgets and marks the scene root dirty each frame, so
 * each scene redraws the whole screen and the numbers stay comparable. Scenes go from
 * primitives (full refresh, rect, circle, arc, ring, label) through canvas rasterization to
 * a crowd of real widgets; the full-refresh baseline changes colour every frame.
 *
 * The component is self contained: it uses SGL plus standard C, writes nothing to the file
 * system and keeps the console quiet while it runs, the HUD shows the live numbers instead.
 *
 * Two caveats worth knowing before editing this file:
 *   - sgl_log() formats through sgl_snprintf, which has no "%f" and no "%u" and ignores the
 *     width of "%s", so floats are emitted as "%d.%02d" pairs, unsigned values are cast to
 *     int and padded text is built by hand. Its buffer is 120 bytes, so log lines stay well
 *     under 100 characters.
 *   - sgl_task_handler() is rate limited by CONFIG_SGL_SYSTICK_MS (10 ms), so the FPS column
 *     saturates at 1000 / CONFIG_SGL_SYSTICK_MS; compare scenes by ms/frame instead.
 */

#include <sgl.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include "sgl_bench.h"

#ifndef SGL_BENCH_SCENE_MS
#define SGL_BENCH_SCENE_MS        2000u     /* sampling window per scene (ms) */
#endif

#ifndef SGL_BENCH_WARMUP_MS
#define SGL_BENCH_WARMUP_MS       300u      /* warm-up discarded per scene (ms) */
#endif

#define BENCH_MAX_SCENES          16
#define BENCH_MAX_OBJS            256
#define BENCH_SCENE_N             12
#define BENCH_PALETTE_N           8
#define BENCH_TEXT_N              6
#define BENCH_DIR_N               16
#define BENCH_NAME_W              12        /* scene name column width, characters */
#define BENCH_ROW_BUF_SZ          96
#define BENCH_HUD_PERIOD_MS       250
#define BENCH_GRID_TARGET_MAX     60        /* main grid cells on a 480 short side */
#define BENCH_ARC_TARGET_MAX      24        /* arc / ring cells on a 480 short side */

#if defined(TIME_UTC)
#define BENCH_HAVE_CLOCK          1         /* standard C11 clock is available */
#else
#define BENCH_HAVE_CLOCK          0
#endif

typedef struct {
    int16_t w, h;                           /* screen size */
    int16_t short_side;
    int16_t scale;                          /* short side as a percentage of 480, 35..220 */
    int16_t pad;
    int16_t cols, rows, count;              /* main grid */
    int16_t cw, ch;
    int16_t ax_cols, ax_rows, ax_n;         /* coarse grid used by arc / ring */
    int16_t ax_cw, ax_ch;
    int16_t line_cols, line_rows;           /* canvas line scene grid */
    int16_t rect_cols, rect_rows;           /* canvas rect scene grid */
    const sgl_font_t *font_sm;              /* body font, always available */
    const sgl_font_t *font_lg;              /* title font, larger screens only */
    int16_t font_sm_h, font_lg_h;
    int16_t char_w;
    int16_t hud_x, hud_y, hud_w, hud_h;
    int16_t hud_pad, hud_line_h;
    const char *hud_fmt;                    /* live stats format, chosen by width */
} bench_layout_t;

typedef struct {
    double   fps;                           /* measured frame rate, systick capped */
    double   busy_ms;                       /* real per-frame cost: draw + flush + present */
    double   max_fps;                       /* 1000 / busy_ms */
    double   mpx_per_s;                     /* real pixel throughput */
    uint32_t frames;                        /* frames within the sample window */
    uint32_t elapsed_ms;
} bench_result_t;

typedef struct {
    const char *name;
    void (*create)(sgl_obj_t *root);
    void (*update)(int32_t phase);
} bench_scene_t;

enum {
    SUM_FULL = 0,
    SUM_MID,
    SUM_SHORT
};

enum {
    W_BUTTON = 0,
    W_PROGRESS,
    W_BAR,
    W_SLIDER,
    W_CHECKBOX,
    W_SWITCH,
    W_KIND_N
};

static const sgl_color_t bench_palette[BENCH_PALETTE_N] = {
    SGL_COLOR_RED,
    SGL_COLOR_GREEN,
    SGL_COLOR_BLUE,
    SGL_COLOR_YELLOW,
    SGL_COLOR_ORANGE,
    SGL_COLOR_CYAN,
    SGL_COLOR_MAGENTA,
    SGL_COLOR_DEEP_SKY_BLUE,
};

static const char *const bench_texts[BENCH_TEXT_N] = {
    "SGL",
    "benchmark",
    "Performance",
    "0123456789",
    "abcdefgh",
    "SGL style",
};

static const int8_t bench_dir[BENCH_DIR_N][2] = {
    {  16,   0}, {  15,   6}, {  11,  11}, {   6,  15}, {   0,  16}, {  -6,  15},
    { -11,  11}, { -15,   6}, { -16,   0}, { -15,  -6}, { -11, -11}, {  -6, -15},
    {   0, -16}, {   6, -15}, {  11, -11}, {  15,  -6},
};

/* HUD statistic formats: sample text used to measure, then the format itself */
static const char *const bench_hud_samples[3] = {
    "FPS 100.0  busy 12.34ms  56.7 Mpx/s",
    "FPS 100.0 12.34ms 56.7Mpx",
    "100.0fps 12.34ms 56.7Mpx",
};

static const char *const bench_hud_fmts[3] = {
    "FPS %d.%d  busy %d.%02dms  %d.%d Mpx/s",
    "FPS %d.%d %d.%02dms %d.%dMpx",
    "%d.%dfps %d.%02dms %d.%dMpx",
};

static bench_layout_t g_lay;

static sgl_obj_t    *g_root;                       /* current scene root container */
static sgl_obj_t    *g_objs[BENCH_MAX_OBJS];       /* children of the current scene */
static int           g_obj_cnt;
static int32_t       g_phase;                      /* animation phase, bumped every frame */

static sgl_obj_t     *g_hud;
static sgl_obj_t     *g_hud_title;
static sgl_obj_t     *g_hud_name;
static sgl_obj_t     *g_hud_stat;
static char           g_hud_stat_buf[64];

static bool           g_running;
static int            g_scene_idx;
static bool           g_sampled;
static uint32_t       g_scene_start;
static uint32_t       g_frames0;
static uint32_t       g_hud_tick;
static uint32_t       g_live_tick;
static uint32_t       g_live_frames;
static uint64_t       g_live_px;
static uint64_t       g_live_busy;
static uint32_t       g_live_drawn;
static uint64_t       g_live_busy_px;

static uint64_t       g_busy_us;
static uint64_t       g_busy_px;
static uint32_t       g_drawn;
static uint64_t       g_t0;
static uint32_t       g_tick0;
static uint64_t       g_busy0;
static uint64_t       g_busy_px0;
static uint32_t       g_drawn0;

static bench_result_t g_results[BENCH_MAX_SCENES];
static char           g_row_buf[BENCH_MAX_SCENES][BENCH_ROW_BUF_SZ];
static int            g_sum_fmt = SUM_FULL;

static int bench_clamp_i(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static uint64_t bench_now_us(void)
{
#if BENCH_HAVE_CLOCK
    struct timespec ts;

    if (timespec_get(&ts, TIME_UTC) == TIME_UTC)
        return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
#endif
    return (uint64_t)sgl_tick_get() * 1000ULL;
}

static void bench_counters(uint32_t *frames, uint64_t *pixels)
{
    if (frames != NULL)
        *frames = g_drawn;
    if (pixels != NULL)
        *pixels = g_busy_px;
}

static void bench_choose_grid(int sw, int sh, int target, int min_cols, int max_cols, int16_t *out_cols, int16_t *out_rows)
{
    int best_c = min_cols;
    int best_r = 1;
    long best_score = -1;
    int c;

    for (c = min_cols; c <= max_cols; c++) {
        int r = (target + c - 1) / c;
        int cw, ch, diff, pen;
        long score;

        if (r < 1)
            r = 1;
        cw = sw / c;
        ch = sh / r;
        if (cw < 12 || ch < 10)
            continue;

        diff = (cw > ch) ? (cw - ch) : (ch - cw);
        pen = c * r - target;
        score = (long)diff * 4 + (long)pen * pen / 4;    /* squareness first */

        if (best_score < 0 || score < best_score) {
            best_score = score;
            best_c = c;
            best_r = r;
        }
    }

    *out_cols = (int16_t)best_c;
    *out_rows = (int16_t)best_r;
}

static int16_t bench_cell_radius(int16_t cw, int16_t ch)
{
    int16_t m = (cw < ch) ? cw : ch;
    int16_t r = (int16_t)(m / 2 - g_lay.pad);

    return r < 3 ? 3 : r;
}

static void bench_layout_init(void)
{
    bench_layout_t *L = &g_lay;
    int sw = (int)SGL_SCREEN_WIDTH;
    int sh = (int)SGL_SCREEN_HEIGHT;
    int target;
    int max_w;
    int line_h;
    int inner;
    int need;
    int pick;
    int i;

    memset(L, 0, sizeof(*L));
    L->w = (int16_t)sw;
    L->h = (int16_t)sh;
    L->short_side = (int16_t)((sw < sh) ? sw : sh);
    L->scale = (int16_t)bench_clamp_i(L->short_side * 100 / 480, 35, 220);
    L->pad = (int16_t)bench_clamp_i(L->short_side / 80, 2, 12);

    /* Cell budget of the main grid, relaxed as the screen shrinks */
    target = BENCH_GRID_TARGET_MAX;
    if (L->short_side < 400)
        target = 48;
    if (L->short_side < 300)
        target = 36;
    if (L->short_side < 200)
        target = 24;
    if (L->short_side < 120)
        target = 12;

    bench_choose_grid(sw, sh, target, 2, 16, &L->cols, &L->rows);
    L->count = (int16_t)(L->cols * L->rows);
    L->cw = (int16_t)(sw / L->cols);
    L->ch = (int16_t)(sh / L->rows);

    /* Arc and ring need more room per cell, so they use a coarser grid */
    target = BENCH_ARC_TARGET_MAX;
    if (L->short_side < 300)
        target = 16;
    if (L->short_side < 200)
        target = 9;
    if (L->short_side < 120)
        target = 6;

    bench_choose_grid(sw, sh, target, 2, 10, &L->ax_cols, &L->ax_rows);
    L->ax_n = (int16_t)(L->ax_cols * L->ax_rows);
    L->ax_cw = (int16_t)(sw / L->ax_cols);
    L->ax_ch = (int16_t)(sh / L->ax_rows);

    L->font_sm = &consolas14;
    L->font_sm_h = sgl_font_get_height(L->font_sm);
#if CONFIG_SGL_FONT_CONSOLAS24
    L->font_lg = (L->short_side >= 300) ? &consolas24 : &consolas14;
#else
    L->font_lg = &consolas14;
#endif
    L->font_lg_h = sgl_font_get_height(L->font_lg);
    L->char_w = (int16_t)((L->font_sm_h + 1) / 2);

    L->line_cols = (int16_t)bench_clamp_i(sw / 40, 3, 26);
    L->line_rows = (int16_t)bench_clamp_i(sh / 40, 2, 20);
    L->rect_cols = (int16_t)bench_clamp_i(sw / 40, 3, 26);
    L->rect_rows = (int16_t)bench_clamp_i(sh / 30, 2, 20);

    /* HUD box: the widest statistic format that still fits, plus its height */
    max_w = sw - 4 * L->pad;
    line_h = L->font_sm_h + 2 + (L->scale >= 150 ? 2 : 1);
    inner = L->pad + 2;
    pick = 2;

    for (i = 0; i < 3; i++) {
        need = (int)sgl_font_get_string_width(bench_hud_samples[i], L->font_sm) + 2 * inner;
        if (need <= max_w) {
            pick = i;
            break;
        }
    }
    need = (int)sgl_font_get_string_width(bench_hud_samples[pick], L->font_sm) + 2 * inner;

    L->hud_fmt = bench_hud_fmts[pick];
    L->hud_pad = (int16_t)inner;
    L->hud_line_h = (int16_t)line_h;
    L->hud_w = (int16_t)bench_clamp_i(need, 80, max_w > 80 ? max_w : 80);
    L->hud_h = (int16_t)(2 * inner + 3 * line_h);
    L->hud_x = (int16_t)(2 * L->pad);
    L->hud_y = (int16_t)(2 * L->pad);
}

static void bench_grid(int i, int16_t *x, int16_t *y)
{
    *x = (int16_t)((i % g_lay.cols) * g_lay.cw);
    *y = (int16_t)((i / g_lay.cols) * g_lay.ch);
}

static void bench_ax_grid(int i, int16_t *x, int16_t *y)
{
    *x = (int16_t)((i % g_lay.ax_cols) * g_lay.ax_cw);
    *y = (int16_t)((i / g_lay.ax_cols) * g_lay.ax_ch);
}

static void scene_full_refresh_create(sgl_obj_t *root)
{
    SGL_UNUSED(root);
}

static void scene_full_refresh_update(int32_t phase)
{
    uint32_t t = sgl_tick_get() / 8;

    SGL_UNUSED(phase);
    if (g_root != NULL)
        sgl_rect_set_color(g_root, sgl_rgb((uint8_t)(t * 3), (uint8_t)(t * 5), (uint8_t)(t * 7)));
}

static void scene_rect_radius_create(sgl_obj_t *root)
{
    const int16_t p = g_lay.pad;
    int i;

    for (i = 0; i < g_lay.count; i++) {
        int16_t x, y;
        sgl_obj_t *o;

        bench_grid(i, &x, &y);
        o = sgl_rect_create(root);
        if (o == NULL)
            return;
        sgl_obj_set_pos(o, (int16_t)(x + p), (int16_t)(y + p));
        sgl_obj_set_size(o, (int16_t)(g_lay.cw - 2 * p), (int16_t)(g_lay.ch - 2 * p));
        sgl_rect_set_color(o, bench_palette[i % BENCH_PALETTE_N]);
        sgl_rect_set_alpha(o, 255);
        sgl_rect_set_radius(o, 10);
        g_objs[g_obj_cnt++] = o;
    }
}

static void scene_rect_radius_update(int32_t phase)
{
    int16_t half = (int16_t)(((g_lay.cw < g_lay.ch) ? g_lay.cw : g_lay.ch) / 2);
    int16_t span = (int16_t)(half > 2 ? half : 2);
    int i;

    for (i = 0; i < g_obj_cnt; i++)
        sgl_rect_set_radius(g_objs[i], (uint16_t)((phase + i * 3) % (span + 1)));
}

static void scene_rect_border_create(sgl_obj_t *root)
{
    const int16_t p = (int16_t)(g_lay.pad + 2);
    const int16_t w = (int16_t)bench_clamp_i(g_lay.scale / 25, 1, 3);
    int i;

    for (i = 0; i < g_lay.count; i++) {
        int16_t x, y;
        sgl_obj_t *o;

        bench_grid(i, &x, &y);
        o = sgl_rect_create(root);
        if (o == NULL)
            return;
        sgl_obj_set_pos(o, (int16_t)(x + p), (int16_t)(y + p));
        sgl_obj_set_size(o, (int16_t)(g_lay.cw - 2 * p), (int16_t)(g_lay.ch - 2 * p));
        sgl_rect_set_alpha(o, 255);
        sgl_rect_set_color(o, sgl_rgb(30, 32, 40));
        sgl_rect_set_border_color(o, bench_palette[i % BENCH_PALETTE_N]);
        sgl_rect_set_border_width(o, (uint8_t)w);
        sgl_rect_set_radius(o, (uint16_t)bench_clamp_i(g_lay.pad, 1, 8));
        g_objs[g_obj_cnt++] = o;
    }
}

static void scene_rect_border_update(int32_t phase)
{
    uint8_t wmax = (uint8_t)bench_clamp_i(g_lay.scale / 16, 2, 8);
    int i;

    for (i = 0; i < g_obj_cnt; i++) {
        sgl_rect_set_border_width(g_objs[i], (uint8_t)((phase + i) % (wmax + 1)));
        sgl_rect_set_border_color(g_objs[i], bench_palette[(phase / 4 + i) % BENCH_PALETTE_N]);
    }
}

static void scene_rect_opacity_create(sgl_obj_t *root)
{
    const int16_t p = g_lay.pad;
    int i;

    /* Deliberately larger than the cell so neighbours overlap */
    for (i = 0; i < g_lay.count; i++) {
        int16_t x, y;
        sgl_obj_t *o;

        bench_grid(i, &x, &y);
        o = sgl_rect_create(root);
        if (o == NULL)
            return;
        sgl_obj_set_pos(o, (int16_t)(x - p), (int16_t)(y - p));
        sgl_obj_set_size(o, (int16_t)(g_lay.cw + 2 * p), (int16_t)(g_lay.ch + 2 * p));
        sgl_rect_set_color(o, bench_palette[i % BENCH_PALETTE_N]);
        sgl_rect_set_alpha(o, 140);
        sgl_rect_set_radius(o, (uint16_t)bench_clamp_i(g_lay.cw / 6, 2, 16));
        g_objs[g_obj_cnt++] = o;
    }
}

static void scene_rect_opacity_update(int32_t phase)
{
    int i;

    for (i = 0; i < g_obj_cnt; i++)
        sgl_rect_set_alpha(g_objs[i], (uint8_t)(40 + ((phase * 5 + i * 13) % 216)));
}

static void scene_circle_create(sgl_obj_t *root)
{
    const int16_t p = g_lay.pad;
    int i;

    for (i = 0; i < g_lay.count; i++) {
        int16_t x, y;
        sgl_obj_t *o;

        bench_grid(i, &x, &y);
        o = sgl_circle_create(root);
        if (o == NULL)
            return;
        sgl_obj_set_pos(o, (int16_t)(x + p), (int16_t)(y + p));
        sgl_obj_set_size(o, (int16_t)(g_lay.cw - 2 * p), (int16_t)(g_lay.ch - 2 * p));
        sgl_circle_set_color(o, bench_palette[i % BENCH_PALETTE_N]);
        sgl_circle_set_alpha(o, 255);
        sgl_circle_set_radius(o, (uint16_t)bench_cell_radius(g_lay.cw, g_lay.ch));
        g_objs[g_obj_cnt++] = o;
    }
}

static void scene_circle_update(int32_t phase)
{
    int16_t r_max = bench_cell_radius(g_lay.cw, g_lay.ch);
    int16_t r_min = (int16_t)(r_max / 3);
    int16_t extent = (int16_t)(r_max - r_min);
    int i;

    if (r_min < 2)
        r_min = 2;
    if (extent < 1)
        extent = 1;
    for (i = 0; i < g_obj_cnt; i++)
        sgl_circle_set_radius(g_objs[i], (uint16_t)(r_min + (phase + i * 2) % extent));
}

static void scene_arc_create(sgl_obj_t *root)
{
    const int16_t p = g_lay.pad;
    const int16_t r_o = bench_cell_radius(g_lay.ax_cw, g_lay.ax_ch);
    int i;

    for (i = 0; i < g_lay.ax_n; i++) {
        int16_t x, y;
        sgl_obj_t *o;

        bench_ax_grid(i, &x, &y);
        o = sgl_arc_create(root);
        if (o == NULL)
            return;
        sgl_obj_set_pos(o, (int16_t)(x + p), (int16_t)(y + p));
        sgl_obj_set_size(o, (int16_t)(g_lay.ax_cw - 2 * p), (int16_t)(g_lay.ax_ch - 2 * p));
        sgl_arc_set_mode(o, SGL_ARC_MODE_RING);
        sgl_arc_set_color(o, bench_palette[i % BENCH_PALETTE_N]);
        sgl_arc_set_bg_color(o, sgl_rgb(40, 42, 50));
        sgl_arc_set_radius(o, (int16_t)(r_o / 3), r_o);
        g_objs[g_obj_cnt++] = o;
    }
}

static void scene_arc_update(int32_t phase)
{
    int16_t r_o = bench_cell_radius(g_lay.ax_cw, g_lay.ax_ch);
    int16_t r_min = (int16_t)(r_o / 3);
    int16_t extent = (int16_t)(r_o - r_min);
    int i;

    if (r_min < 2)
        r_min = 2;
    if (extent < 1)
        extent = 1;
    for (i = 0; i < g_obj_cnt; i++) {
        sgl_arc_set_radius(g_objs[i], (int16_t)(r_min + (phase + i) % extent), r_o);
        sgl_arc_set_color(g_objs[i], bench_palette[(phase / 5 + i) % BENCH_PALETTE_N]);
    }
}

static void scene_ring_create(sgl_obj_t *root)
{
    const int16_t p = g_lay.pad;
    const int16_t r_o = bench_cell_radius(g_lay.ax_cw, g_lay.ax_ch);
    int i;

    for (i = 0; i < g_lay.ax_n; i++) {
        int16_t x, y;
        sgl_obj_t *o;

        bench_ax_grid(i, &x, &y);
        o = sgl_ring_create(root);
        if (o == NULL)
            return;
        sgl_obj_set_pos(o, (int16_t)(x + p), (int16_t)(y + p));
        sgl_obj_set_size(o, (int16_t)(g_lay.ax_cw - 2 * p), (int16_t)(g_lay.ax_ch - 2 * p));
        sgl_ring_set_color(o, bench_palette[i % BENCH_PALETTE_N]);
        sgl_ring_set_alpha(o, 255);
        sgl_ring_set_radius(o, (int16_t)(r_o / 2), r_o);
        g_objs[g_obj_cnt++] = o;
    }
}

static void scene_ring_update(int32_t phase)
{
    int16_t r_o = bench_cell_radius(g_lay.ax_cw, g_lay.ax_ch);
    int16_t r_min = (int16_t)(r_o / 4);
    int16_t extent = (int16_t)(r_o - r_min);
    int i;

    if (r_min < 1)
        r_min = 1;
    if (extent < 1)
        extent = 1;
    for (i = 0; i < g_obj_cnt; i++) {
        uint16_t r_in = (uint16_t)(r_min + (phase + i) % extent);

        sgl_ring_set_radius(g_objs[i], r_in, (uint16_t)r_o);
    }
}

static int16_t bench_label_inset(void)
{
    return (int16_t)(g_lay.pad > 2 ? g_lay.pad - 1 : 1);
}

static const char *bench_pick_text(int i, int slot)
{
    int16_t lw = (int16_t)(g_lay.cw - 2 * bench_label_inset());
    int t;

    for (t = 0; t < BENCH_TEXT_N; t++) {
        const char *cand = bench_texts[(slot + i + t) % BENCH_TEXT_N];

        if ((int)sgl_font_get_string_width(cand, g_lay.font_sm) <= lw)
            return cand;
    }
    return "SGL";
}

static void scene_label_create(sgl_obj_t *root)
{
    const int16_t p = bench_label_inset();
    const int16_t fh = g_lay.font_sm_h;
    int i;

    for (i = 0; i < g_lay.count; i++) {
        int16_t x, y;
        sgl_obj_t *o;

        bench_grid(i, &x, &y);
        o = sgl_label_create(root);
        if (o == NULL)
            return;
        sgl_obj_set_pos(o, (int16_t)(x + p), (int16_t)(y + g_lay.ch / 2 - fh / 2));
        sgl_obj_set_size(o, (int16_t)(g_lay.cw - 2 * p), (int16_t)(fh + 2));
        /* The font must be set first, set_text needs it to compute the width */
        sgl_label_set_font(o, g_lay.font_sm);
        sgl_label_set_text_color(o, bench_palette[i % BENCH_PALETTE_N]);
        sgl_label_set_text(o, bench_pick_text(i, 0));
        g_objs[g_obj_cnt++] = o;
    }
}

static void scene_label_update(int32_t phase)
{
    int i;

    for (i = 0; i < g_obj_cnt; i++) {
        sgl_label_set_text(g_objs[i], bench_pick_text(i, phase / 8));
        sgl_label_set_text_color(g_objs[i], bench_palette[(phase / 3 + i) % BENCH_PALETTE_N]);
    }
}

static void bench_painter_lines(sgl_surf_t *surf, sgl_area_t *area, sgl_obj_t *obj)
{
    const int cols = g_lay.line_cols;
    const int rows = g_lay.line_rows;
    const int16_t cw = (int16_t)(SGL_SCREEN_WIDTH / cols);
    const int16_t ch = (int16_t)(SGL_SCREEN_HEIGHT / rows);
    const int16_t r = (int16_t)bench_clamp_i((cw < ch ? cw : ch) / 3, 3, 16);
    const uint8_t lw = (uint8_t)bench_clamp_i(g_lay.scale / 25, 2, 6);
    int i;

    SGL_UNUSED(obj);
    for (i = 0; i < cols * rows; i++) {
        const int8_t *d = bench_dir[(g_phase / 3 + i) % BENCH_DIR_N];
        int16_t cx = (int16_t)((i % cols) * cw + cw / 2);
        int16_t cy = (int16_t)((i / cols) * ch + ch / 2);
        int16_t dx = (int16_t)((int32_t)d[0] * r / 16);
        int16_t dy = (int16_t)((int32_t)d[1] * r / 16);

        sgl_draw_line_noaa(surf, area,
                           (int16_t)(cx - dx), (int16_t)(cy - dy),
                           (int16_t)(cx + dx), (int16_t)(cy + dy),
                           bench_palette[(i + g_phase / 5) % BENCH_PALETTE_N], lw, 255);
    }
}

static void scene_canvas_lines_create(sgl_obj_t *root)
{
    sgl_obj_t *canvas = sgl_canvas_create(root);

    if (canvas == NULL)
        return;
    sgl_obj_set_pos(canvas, 0, 0);
    sgl_obj_set_size(canvas, SGL_SCREEN_WIDTH, SGL_SCREEN_HEIGHT);
    sgl_canvas_set_painter_cb(canvas, bench_painter_lines);
    g_objs[g_obj_cnt++] = canvas;
}

static void scene_canvas_lines_update(int32_t phase)
{
    SGL_UNUSED(phase);
}

static void bench_painter_rects(sgl_surf_t *surf, sgl_area_t *area, sgl_obj_t *obj)
{
    const int cols = g_lay.rect_cols;
    const int rows = g_lay.rect_rows;
    const int16_t cw = (int16_t)(SGL_SCREEN_WIDTH / cols);
    const int16_t ch = (int16_t)(SGL_SCREEN_HEIGHT / rows);
    const int16_t inset = (int16_t)bench_clamp_i((cw < ch ? cw : ch) / 8, 1, 4);
    const int16_t rmax = (int16_t)bench_clamp_i((cw < ch ? cw : ch) / 2 - inset, 1, 14);
    int i;

    SGL_UNUSED(obj);
    for (i = 0; i < cols * rows; i++) {
        sgl_area_t r;
        int16_t x = (int16_t)((i % cols) * cw);
        int16_t y = (int16_t)((i / cols) * ch);

        r.x1 = (int16_t)(x + inset);
        r.y1 = (int16_t)(y + inset);
        r.x2 = (int16_t)(x + cw - inset - 1);
        r.y2 = (int16_t)(y + ch - inset - 1);
        sgl_draw_fill_rect(surf, area, &r, (int16_t)((g_phase + i) % (rmax + 1)),
                           bench_palette[(i + g_phase / 4) % BENCH_PALETTE_N],
                           (uint8_t)(60 + ((g_phase * 3 + i * 7) % 196)));
    }
}

static void scene_canvas_rects_create(sgl_obj_t *root)
{
    sgl_obj_t *canvas = sgl_canvas_create(root);

    if (canvas == NULL)
        return;
    sgl_obj_set_pos(canvas, 0, 0);
    sgl_obj_set_size(canvas, SGL_SCREEN_WIDTH, SGL_SCREEN_HEIGHT);
    sgl_canvas_set_painter_cb(canvas, bench_painter_rects);
    g_objs[g_obj_cnt++] = canvas;
}

static void scene_canvas_rects_update(int32_t phase)
{
    SGL_UNUSED(phase);
}

static void bench_painter_arcs(sgl_surf_t *surf, sgl_area_t *area, sgl_obj_t *obj)
{
    const int16_t cw = g_lay.cw;
    const int16_t ch = g_lay.ch;
    const int16_t r_out = bench_cell_radius(cw, ch);
    const int16_t r_min = (int16_t)(r_out / 3);
    const int16_t ring = (int16_t)((r_out - r_min) > 1 ? (r_out - r_min) : 1);
    int i;

    SGL_UNUSED(obj);
    for (i = 0; i < g_lay.count; i++) {
        sgl_draw_arc_t desc;
        int16_t s = (int16_t)((g_phase + i * 17) % 360);
        int16_t span = (int16_t)(40 + (g_phase * 3 + i * 11) % 300);
        int16_t e = (int16_t)(s + span);
        int16_t r_in;

        if (e > 360)
            e -= 360;
        r_in = (int16_t)(r_min + (g_phase + i * 3) % ring);

        desc.cx = (int16_t)((i % g_lay.cols) * cw + cw / 2);
        desc.cy = (int16_t)((i / g_lay.cols) * ch + ch / 2);
        desc.radius_in = r_in;
        desc.radius_out = (int16_t)(r_out - (g_phase + i) % 3);
        if (desc.radius_out <= desc.radius_in)
            desc.radius_out = (int16_t)(desc.radius_in + 2);
        desc.start_angle = (uint32_t)s;
        desc.end_angle = (uint32_t)e;
        desc.mode = SGL_ARC_MODE_RING;
        desc.color = bench_palette[(i + g_phase / 6) % BENCH_PALETTE_N];
        desc.bg_color = sgl_rgb(40, 42, 50);
        desc.alpha = 255;

        sgl_draw_fill_arc(surf, area, &desc);
    }
}

static void scene_canvas_arcs_create(sgl_obj_t *root)
{
    sgl_obj_t *canvas = sgl_canvas_create(root);

    if (canvas == NULL)
        return;
    sgl_obj_set_pos(canvas, 0, 0);
    sgl_obj_set_size(canvas, SGL_SCREEN_WIDTH, SGL_SCREEN_HEIGHT);
    sgl_canvas_set_painter_cb(canvas, bench_painter_arcs);
    g_objs[g_obj_cnt++] = canvas;
}

static void scene_canvas_arcs_update(int32_t phase)
{
    SGL_UNUSED(phase);
}

static void scene_widgets_create(sgl_obj_t *root)
{
    const int16_t p = g_lay.pad;
    const int16_t pw = (int16_t)(g_lay.cw - 2 * p);
    const int16_t avail = (int16_t)(g_lay.ch - 2 * p);
    const int16_t h_btn = (int16_t)bench_clamp_i(avail * 55 / 100, 12, 34);
    const int16_t h_bar = (int16_t)bench_clamp_i(avail * 30 / 100, 5, 16);
    const int16_t h_sl = (int16_t)bench_clamp_i(avail * 45 / 100, 8, 22);
    /* Text is omitted when it does not fit, keeping only the primitives */
    const uint8_t with_text = (uint8_t)((h_btn >= g_lay.font_sm_h + 4 &&
                                         pw >= sgl_font_get_string_width("SGL", g_lay.font_sm)) ? 1 : 0);
    int i;

    for (i = 0; i < g_lay.count; i++) {
        int16_t x, y;
        sgl_obj_t *o = NULL;

        bench_grid(i, &x, &y);
        switch (i % W_KIND_N) {
        case W_BUTTON:
            o = sgl_button_create(root);
            if (o != NULL) {
                sgl_obj_set_pos(o, (int16_t)(x + p), (int16_t)(y + g_lay.ch / 2 - h_btn / 2));
                sgl_obj_set_size(o, pw, h_btn);
                sgl_button_set_radius(o, (uint16_t)bench_clamp_i(h_btn / 5, 2, 8));
                if (with_text) {
                    sgl_button_set_font(o, g_lay.font_sm);
                    sgl_button_set_text(o, "SGL");
                    sgl_button_set_text_color(o, SGL_COLOR_WHITE);
                }
            }
            break;

        case W_PROGRESS:
            o = sgl_progress_create(root);
            if (o != NULL) {
                sgl_obj_set_pos(o, (int16_t)(x + p), (int16_t)(y + g_lay.ch / 2 - h_bar / 2));
                sgl_obj_set_size(o, pw, h_bar);
                sgl_progress_set_radius(o, (uint16_t)bench_clamp_i(h_bar / 2, 1, 8));
                sgl_progress_set_value(o, 50);
            }
            break;

        case W_BAR:
            o = sgl_bar_create(root);
            if (o != NULL) {
                sgl_obj_set_pos(o, (int16_t)(x + p), (int16_t)(y + g_lay.ch / 2 - h_bar / 2));
                sgl_obj_set_size(o, pw, h_bar);
                sgl_bar_set_radius(o, (uint16_t)bench_clamp_i(h_bar / 2, 1, 8));
                sgl_bar_set_value(o, 50);
            }
            break;

        case W_SLIDER:
            o = sgl_slider_create(root);
            if (o != NULL) {
                sgl_obj_set_pos(o, (int16_t)(x + p), (int16_t)(y + g_lay.ch / 2 - h_sl / 2));
                sgl_obj_set_size(o, pw, h_sl);
                sgl_slider_set_value(o, 50);
            }
            break;

        case W_CHECKBOX:
            o = sgl_checkbox_create(root);
            if (o != NULL) {
                sgl_obj_set_pos(o, (int16_t)(x + p), (int16_t)(y + g_lay.ch / 2 - h_sl / 2));
                sgl_obj_set_size(o, pw, h_sl);
                sgl_checkbox_set_radius(o, (uint16_t)bench_clamp_i(h_sl / 5, 2, 6));
                if (with_text) {
                    sgl_checkbox_set_font(o, g_lay.font_sm);
                    sgl_checkbox_set_text(o, "ok");
                }
            }
            break;

        default:
            o = sgl_switch_create(root);
            if (o != NULL) {
                sgl_obj_set_pos(o, (int16_t)(x + p), (int16_t)(y + g_lay.ch / 2 - h_btn / 2));
                sgl_obj_set_size(o, pw, h_btn);
            }
            break;
        }

        if (o != NULL)
            g_objs[g_obj_cnt++] = o;
    }
}

static void scene_widgets_update(int32_t phase)
{
    int i;

    for (i = 0; i < g_obj_cnt; i++) {
        sgl_obj_t *o = g_objs[i];

        switch (i % W_KIND_N) {
        case W_BUTTON:
            sgl_button_set_color(o, bench_palette[(phase / 4 + i) % BENCH_PALETTE_N]);
            break;

        case W_PROGRESS:
            sgl_progress_set_value(o, (uint8_t)((phase * 3 + i * 7) % 101));
            break;

        case W_BAR:
            sgl_bar_set_value(o, (uint8_t)((phase * 5 + i * 3) % 101));
            break;

        case W_SLIDER:
            sgl_slider_set_value(o, (uint8_t)((phase * 2 + i * 11) % 101));
            break;

        case W_CHECKBOX:
            sgl_checkbox_set_status(o, ((phase / 6 + i) & 1) != 0);
            break;

        default:
            sgl_switch_set_status(o, ((phase / 5 + i) & 1) != 0);
            break;
        }
    }
}

static void bench_hud_create(void)
{
    const bench_layout_t *L = &g_lay;
    const int16_t lw = (int16_t)(L->hud_w - 2 * L->hud_pad);
    const int16_t lh = L->font_sm_h;

    g_hud = sgl_rect_create(NULL);
    if (g_hud == NULL)
        return;
    sgl_obj_set_pos(g_hud, L->hud_x, L->hud_y);
    sgl_obj_set_size(g_hud, L->hud_w, L->hud_h);
    sgl_rect_set_color(g_hud, sgl_rgb(14, 20, 34));
    sgl_rect_set_alpha(g_hud, 235);
    sgl_rect_set_radius(g_hud, (uint16_t)bench_clamp_i(L->pad, 2, 8));

    g_hud_title = sgl_label_create(g_hud);
    sgl_obj_set_pos(g_hud_title, L->hud_pad, L->hud_pad);
    sgl_obj_set_size(g_hud_title, lw, lh);
    sgl_label_set_font(g_hud_title, L->font_sm);
    sgl_label_set_text_color(g_hud_title, SGL_COLOR_WHITE);

    g_hud_name = sgl_label_create(g_hud);
    sgl_obj_set_pos(g_hud_name, L->hud_pad, (int16_t)(L->hud_pad + L->hud_line_h));
    sgl_obj_set_size(g_hud_name, lw, lh);
    sgl_label_set_font(g_hud_name, L->font_sm);
    sgl_label_set_text_color(g_hud_name, SGL_COLOR_GOLD);

    g_hud_stat = sgl_label_create(g_hud);
    sgl_obj_set_pos(g_hud_stat, L->hud_pad, (int16_t)(L->hud_pad + 2 * L->hud_line_h));
    sgl_obj_set_size(g_hud_stat, lw, lh);
    sgl_label_set_font(g_hud_stat, L->font_sm);
    sgl_label_set_text_color(g_hud_stat, sgl_rgb(170, 190, 210));
    /* The stat text changes every frame, a fixed buffer avoids dynamic memory */
    sgl_label_set_text_buffer(g_hud_stat, g_hud_stat_buf, sizeof(g_hud_stat_buf));
    sgl_label_set_text_fmt(g_hud_stat, "%s", "sampling...");
}

static void bench_hud_update(void)
{
    uint32_t now;
    uint32_t frames = 0;
    uint64_t px = 0;
    uint32_t dt;
    uint32_t df = 0;
    uint32_t ddrawn = 0;
    uint64_t dbusy = 0;
    uint64_t dbusy_px = 0;
    uint32_t fps_x10;
    uint32_t busy_us = 0;
    uint32_t mpx_x10 = 0;

    if (g_hud == NULL)
        return;

    bench_counters(&frames, &px);
    now = sgl_tick_get();
    dt = now - g_live_tick;
    if (dt == 0)
        dt = 1;
    if (frames >= g_live_frames)
        df = frames - g_live_frames;
    if (g_drawn >= g_live_drawn)
        ddrawn = g_drawn - g_live_drawn;
    if (g_busy_us >= g_live_busy)
        dbusy = g_busy_us - g_live_busy;
    if (g_busy_px >= g_live_busy_px)
        dbusy_px = g_busy_px - g_live_busy_px;

    fps_x10 = (uint32_t)(((uint64_t)df * 10000u) / dt);
    if (ddrawn != 0) {
        busy_us = (uint32_t)(dbusy / ddrawn);
        if (dbusy != 0)
            mpx_x10 = (uint32_t)((double)dbusy_px * 10.0 / (double)dbusy);
    }

    sgl_label_set_text_fmt(g_hud_stat, g_lay.hud_fmt,
                           fps_x10 / 10, fps_x10 % 10,
                           busy_us / 1000, (busy_us % 1000) / 10,
                           mpx_x10 / 10, mpx_x10 % 10);

    g_live_tick = now;
    g_live_frames = frames;
    g_live_px = px;
    g_live_busy = g_busy_us;
    g_live_busy_px = g_busy_px;
    g_live_drawn = g_drawn;
}

static const bench_scene_t g_scenes[BENCH_SCENE_N] = {
    { "full refresh", scene_full_refresh_create, scene_full_refresh_update },
    { "rect radius",  scene_rect_radius_create,  scene_rect_radius_update  },
    { "rect border",  scene_rect_border_create,  scene_rect_border_update  },
    { "rect opacity", scene_rect_opacity_create, scene_rect_opacity_update },
    { "circle",       scene_circle_create,       scene_circle_update       },
    { "arc ring",     scene_arc_create,          scene_arc_update          },
    { "ring",         scene_ring_create,         scene_ring_update         },
    { "label",        scene_label_create,        scene_label_update        },
    { "canvas lines", scene_canvas_lines_create, scene_canvas_lines_update },
    { "canvas rects", scene_canvas_rects_create, scene_canvas_rects_update },
    { "canvas arcs",  scene_canvas_arcs_create,  scene_canvas_arcs_update  },
    { "widget mix",   scene_widgets_create,      scene_widgets_update      },
};

static void bench_hud_set_scene(int idx)
{
    if (g_hud_title == NULL)
        return;
    sgl_label_set_text_fmt(g_hud_title, "SGL Bench  %d/%d", idx + 1, BENCH_SCENE_N);
    sgl_label_set_text(g_hud_name, g_scenes[idx].name);
}

static void bench_scene_begin(int idx)
{
    if (g_root != NULL) {
        sgl_obj_delete(g_root);         /* deferred free, reclaimed in the next task handler */
        g_root = NULL;
    }
    g_obj_cnt = 0;
    g_phase = 0;
    g_sampled = false;
    g_scene_idx = idx;

    /* Scene root: a full-screen plate that also parents all child widgets */
    g_root = sgl_rect_create(NULL);
    if (g_root == NULL)
        return;
    sgl_obj_set_pos(g_root, 0, 0);
    sgl_obj_set_size(g_root, SGL_SCREEN_WIDTH, SGL_SCREEN_HEIGHT);
    sgl_rect_set_radius(g_root, 0);
    sgl_rect_set_alpha(g_root, 255);
    sgl_rect_set_color(g_root, sgl_rgb(26, 28, 36));

    g_scenes[idx].create(g_root);

    if (g_hud != NULL)
        sgl_obj_move_top(g_hud);        /* keep the HUD on top */

    sgl_obj_set_dirty(g_root);
    bench_hud_set_scene(idx);

    g_scene_start = sgl_tick_get();
}

static void bench_scene_end(uint32_t frames, uint32_t elapsed)
{
    bench_result_t *r = &g_results[g_scene_idx];
    uint64_t busy = g_busy_us - g_busy0;
    uint64_t busy_px = g_busy_px - g_busy_px0;
    uint32_t drawn = g_drawn - g_drawn0;

    r->frames = frames;
    r->elapsed_ms = elapsed;
    r->fps = elapsed ? ((double)frames * 1000.0) / (double)elapsed : 0.0;
    r->busy_ms = 0.0;
    r->max_fps = 0.0;
    r->mpx_per_s = 0.0;

    if (drawn != 0 && busy != 0) {
        double busy_s = (double)busy / 1000000.0;

        r->busy_ms = (double)busy / 1000.0 / (double)drawn;
        r->max_fps = r->busy_ms > 0.0 ? 1000.0 / r->busy_ms : 0.0;
        r->mpx_per_s = busy_s > 0.0 ? (double)busy_px / busy_s / 1000000.0 : 0.0;
    }
}

static void bench_name_pad(char *dst, size_t cap, const char *name)
{
    size_t n = strlen(name);

    if (n > BENCH_NAME_W)
        n = BENCH_NAME_W;
    memcpy(dst, name, n);
    while (n + 1 < cap && n < BENCH_NAME_W)
        dst[n++] = ' ';
    dst[n] = '\0';
}

/* One console table row. Every metric uses an integer pair because sgl_snprintf has no "%f" */
static void bench_report_row(char *dst, size_t cap, const char *name_pad, int i)
{
    const bench_result_t *r = &g_results[i];

    sgl_snprintf(dst, cap, "%s%5d.%d %9d.%02d %8d.%d %10d.%02d",
                 name_pad,
                 (int)r->fps, (int)(r->fps * 10) % 10,
                 (int)r->busy_ms, (int)(r->busy_ms * 100) % 100,
                 (int)r->max_fps, (int)(r->max_fps * 10) % 10,
                 (int)r->mpx_per_s, (int)(r->mpx_per_s * 100) % 100);
}

static void bench_report_console(void)
{
    char head[BENCH_NAME_W + 2];
    char row[BENCH_NAME_W + 64];
    int i;

    SGL_LOG_INFO("=================== SGL benchmark summary ===================");
    SGL_LOG_INFO("screen %dx%d @ %d bpp | %d scenes x %d ms (+%d ms warm-up)",
                 (int)SGL_SCREEN_WIDTH, (int)SGL_SCREEN_HEIGHT, (int)CONFIG_SGL_FBDEV_PIXEL_DEPTH,
                 BENCH_SCENE_N, (int)SGL_BENCH_SCENE_MS, (int)SGL_BENCH_WARMUP_MS);

    bench_name_pad(head, sizeof(head), "scene");
    SGL_LOG_INFO("%s    FPS    ms/frame    max fps         Mpx/s", head);

    for (i = 0; i < BENCH_SCENE_N; i++) {
        char name_pad[BENCH_NAME_W + 2];

        bench_name_pad(name_pad, sizeof(name_pad), g_scenes[i].name);
        bench_report_row(row, sizeof(row), name_pad, i);
        SGL_LOG_INFO("%s", row);
    }

    SGL_LOG_INFO("=============================================================");
}

static void bench_row_text(char *dst, size_t cap, int fmt, const char *name_pad, int i)
{
    const bench_result_t *r = &g_results[i];

    switch (fmt) {
    case SUM_FULL:
        sgl_snprintf(dst, cap, "%s %d.%d %d.%02d %d.%d %d.%02d", name_pad,
                     (int)r->fps, (int)(r->fps * 10) % 10,
                     (int)r->busy_ms, (int)(r->busy_ms * 100) % 100,
                     (int)r->max_fps, (int)(r->max_fps * 10) % 10,
                     (int)r->mpx_per_s, (int)(r->mpx_per_s * 100) % 100);
        break;

    case SUM_MID:
        sgl_snprintf(dst, cap, "%s %d.%d %d.%02dms %d.%dMpx", name_pad,
                     (int)r->fps, (int)(r->fps * 10) % 10,
                     (int)r->busy_ms, (int)(r->busy_ms * 100) % 100,
                     (int)r->mpx_per_s, (int)(r->mpx_per_s * 100) % 100);
        break;

    default:
        sgl_snprintf(dst, cap, "%s %d.%d %d.%02dms", name_pad,
                     (int)r->fps, (int)(r->fps * 10) % 10,
                     (int)r->busy_ms, (int)(r->busy_ms * 100) % 100);
        break;
    }
}

static int bench_pick_summary_fmt(void)
{
    static const struct {
        int fmt;
        const char *sample;
    } tab[3] = {
        { SUM_FULL,  "widget mix   100.0 12.34 100.0 100.00" },
        { SUM_MID,   "widget mix   100.0 12.34ms 100.0Mpx"  },
        { SUM_SHORT, "widget mix   100.0 12.34ms"           },
    };
    int margin = 2 * (g_lay.pad * 2 + 2);
    int avail = (int)g_lay.w - margin;
    int i;

    for (i = 0; i < 3; i++) {
        if ((int)sgl_font_get_string_width(tab[i].sample, g_lay.font_sm) <= avail)
            return tab[i].fmt;
    }
    return SUM_SHORT;
}

static const char *bench_summary_head(void)
{
    switch (g_sum_fmt) {
    case SUM_FULL:
        return "scene   FPS   ms/fr  maxfp  Mpx/s";

    case SUM_MID:
        return "scene   FPS   ms/frame  Mpx/s";

    default:
        return "scene   FPS   ms/frame";
    }
}

static void bench_summary_page(void)
{
    const bench_layout_t *L = &g_lay;
    sgl_obj_t *bg;
    sgl_obj_t *title;
    int margin, title_h, head_h, top, avail_h, row_h, col_w;
    int ncols, per_col;
    int c, i;
    char name_pad[BENCH_NAME_W + 4];
    char probe[BENCH_ROW_BUF_SZ];

    g_sum_fmt = bench_pick_summary_fmt();

    margin = L->pad * 2 + 2;
    title_h = L->font_lg_h + 4;
    head_h = L->font_sm_h + 2;
    top = margin + title_h + 4 + head_h + 4;
    avail_h = (int)L->h - top - margin;

    memset(name_pad, 's', BENCH_NAME_W);
    name_pad[BENCH_NAME_W] = ' ';
    name_pad[BENCH_NAME_W + 1] = '\0';
    bench_row_text(probe, sizeof(probe), g_sum_fmt, name_pad, 0);
    col_w = (int)sgl_font_get_string_width(probe, L->font_sm) + L->pad * 2;

    ncols = ((int)L->w - 2 * margin) / (col_w > 0 ? col_w : 1);
    if (ncols < 1)
        ncols = 1;
    if (ncols > 3)
        ncols = 3;
    per_col = (BENCH_SCENE_N + ncols - 1) / ncols;

    row_h = L->font_sm_h + 2;
    if (avail_h > 0) {
        int fit = avail_h / per_col;

        if (fit < row_h)
            row_h = (fit > 8) ? fit : 8;
    }

    bg = sgl_rect_create(NULL);
    if (bg == NULL)
        return;
    sgl_obj_set_pos(bg, 0, 0);
    sgl_obj_set_size(bg, SGL_SCREEN_WIDTH, SGL_SCREEN_HEIGHT);
    sgl_rect_set_color(bg, sgl_rgb(18, 20, 28));
    sgl_rect_set_radius(bg, 0);

    title = sgl_label_create(bg);
    sgl_obj_set_pos(title, (int16_t)margin, (int16_t)margin);
    sgl_obj_set_size(title, (int16_t)(L->w - 2 * margin), (int16_t)(title_h - 4));
    sgl_label_set_font(title, L->font_lg);
    sgl_label_set_text_color(title, SGL_COLOR_WHITE);
    sgl_label_set_text(title, "SGL Benchmark result");

    for (c = 0; c < ncols; c++) {
        sgl_obj_t *head = sgl_label_create(bg);

        sgl_obj_set_pos(head, (int16_t)(margin + c * col_w), (int16_t)(margin + title_h + 4));
        sgl_obj_set_size(head, (int16_t)(col_w - L->pad), (int16_t)head_h);
        sgl_label_set_font(head, L->font_sm);
        sgl_label_set_text_color(head, sgl_rgb(150, 170, 190));
        sgl_label_set_text(head, bench_summary_head());
    }

    for (i = 0; i < BENCH_SCENE_N; i++) {
        char pad[BENCH_NAME_W + 4];
        sgl_obj_t *row;
        int col = i / per_col;
        int lin = i % per_col;

        bench_name_pad(pad, sizeof(pad), g_scenes[i].name);
        bench_row_text(g_row_buf[i], sizeof(g_row_buf[i]), g_sum_fmt, pad, i);

        row = sgl_label_create(bg);
        if (row == NULL)
            return;
        sgl_obj_set_pos(row, (int16_t)(margin + col * col_w), (int16_t)(top + lin * row_h));
        sgl_obj_set_size(row, (int16_t)(col_w - L->pad), (int16_t)row_h);
        sgl_label_set_font(row, L->font_sm);
        sgl_label_set_text_color(row, (i == 0) ? SGL_COLOR_GOLD : sgl_rgb(210, 220, 230));
        sgl_label_set_text(row, g_row_buf[i]);
    }
}

static void bench_finish(void)
{
    g_running = false;

    if (g_root != NULL) {
        sgl_obj_delete(g_root);
        g_root = NULL;
    }
    if (g_hud != NULL) {
        sgl_obj_delete(g_hud);
        g_hud = NULL;
        g_hud_title = NULL;
        g_hud_name = NULL;
        g_hud_stat = NULL;
    }

    bench_report_console();
    bench_summary_page();
}

void sgl_bench_start(void)
{
    sgl_obj_delete_children(sgl_screen_act());
    memset(g_results, 0, sizeof(g_results));
    memset(g_objs, 0, sizeof(g_objs));

    g_running = true;
    g_hud = NULL;
    g_obj_cnt = 0;
    g_scene_idx = 0;
    g_busy_us = 0;
    g_busy_px = 0;
    g_drawn = 0;

    bench_layout_init();

    bench_scene_begin(0);
    bench_hud_create();
    bench_hud_set_scene(0);

    bench_counters(&g_live_frames, &g_live_px);
    g_live_tick = sgl_tick_get();
    g_hud_tick = g_live_tick;
    g_live_busy = g_busy_us;
    g_live_busy_px = g_busy_px;
    g_live_drawn = g_drawn;
}

void sgl_bench_frame_begin(void)
{
    g_t0 = bench_now_us();
    g_tick0 = sgl_last_tick_get();
}

void sgl_bench_frame_end(void)
{
    uint64_t t1 = bench_now_us();
    if (g_running && (sgl_last_tick_get() != g_tick0)) {
        g_busy_us += t1 - g_t0;
        g_busy_px += (uint64_t)SGL_SCREEN_WIDTH * (uint64_t)SGL_SCREEN_HEIGHT;
        g_drawn++;
    }
}

bool sgl_bench_loop(void)
{
    uint32_t now;

    if (!g_running)
        return false;
    now = sgl_tick_get();

    if (g_root != NULL) {
        g_phase++;
        g_scenes[g_scene_idx].update(g_phase);
        sgl_obj_set_dirty(g_root);
    }

    if (!g_sampled && (now - g_scene_start) >= SGL_BENCH_WARMUP_MS) {
        bench_counters(&g_frames0, NULL);
        g_busy0 = g_busy_us;
        g_busy_px0 = g_busy_px;
        g_drawn0 = g_drawn;
        g_scene_start = now;
        g_sampled = true;
    }

    if ((now - g_hud_tick) >= BENCH_HUD_PERIOD_MS) {
        g_hud_tick = now;
        bench_hud_update();
    }

    if (g_sampled && (now - g_scene_start) >= SGL_BENCH_SCENE_MS) {
        uint32_t frames1;

        bench_counters(&frames1, NULL);
        bench_scene_end(frames1 - g_frames0, now - g_scene_start);

        if (g_scene_idx + 1 < BENCH_SCENE_N)
            bench_scene_begin(g_scene_idx + 1);
        else
            bench_finish();
    }

    return g_running;
}
