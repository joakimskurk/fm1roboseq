/* SPDX-License-Identifier: GPL-3.0-only */
/* RoboSeq's board on the FM-1's 240 x 240 screen: the HTML's renderBoard() as pixel art. The 5 x 5 board in cells
 * of 40 px (the HTML's tile art is drawn on a 40 x 40 grid, so it comes over 1:1), the bots on a 32 x 32 grid as
 * the HTML's robotPixelArt, the tiles an event lit, the explosion, the heart and the clash star for as long as the
 * HTML shows them; above it the step and the cycle, below it the bots' health as the HTML's status cards.
 * Main loop only (it draws into the canvas, gfx.c). Colours are the HTML's CSS. */

/* ---- what the screen shows: the game as it is heard (roboseq.c keeps one a step, for when it sounds) */
struct rs_view {
    uint32_t t0;                                /* when it sounds (audio frames) */
    int16_t step;
    uint8_t loop_len, board_id, playing, died[2], love, clash[2];
    uint32_t cycle, active;
    struct rs_bot bot[2];
    struct rs_board board;
};

#define BX 20                                   /* the board's corner on the screen */
#define BY 20
#define CELL 40

/* the HTML's colours */
#define C_CELL RGB(0x0c, 0x0c, 0x18)
#define C_CELL_LINE RGB(0x1a, 0x1a, 0x30)
#define C_DITHER RGB(0x15, 0x15, 0x27)          /* #1a1a30 at 0.6 over the cell */
#define C_BELT RGB(0x63, 0xd9, 0xff)
#define C_ROT RGB(0xff, 0xcc, 0x00)
#define C_ZAP RGB(0xff, 0x33, 0x66)
#define C_PULSE RGB(0x73, 0xff, 0x9f)
#define C_PANEL RGB(0x10, 0x38, 0x36)
#define C_BOT_A RGB(0xff, 0x66, 0x66)
#define C_BOT_A2 RGB(0xcc, 0x33, 0x33)
#define C_BOT_B RGB(0xff, 0x99, 0x00)
#define C_BOT_B2 RGB(0xcc, 0x77, 0x00)
#define C_ACCENT RGB(0x8f, 0xff, 0xd1)
#define C_WARN RGB(0xff, 0xcc, 0x00)
#define C_DANGER RGB(0xff, 0x33, 0x66)
#define C_BG RGB(0x09, 0x2e, 0x2c)
#define C_TEXT RGB(0xeb, 0xff, 0xf6)
#define C_MUTED RGB(0x98, 0xd6, 0xbe)

static uint16_t mix565(uint16_t a, uint16_t b, uint32_t a256)   /* a over b, a256 / 256 of a */
{
    uint32_t r = (((a >> 11) & 31u) * a256 + ((b >> 11) & 31u) * (256u - a256)) >> 8;
    uint32_t g = (((a >> 5) & 63u) * a256 + ((b >> 5) & 63u) * (256u - a256)) >> 8;
    uint32_t bl = ((a & 31u) * a256 + (b & 31u) * (256u - a256)) >> 8;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

/* ---- shapes the SVGs use */
static void poly(int32_t ox, int32_t oy, const int8_t *xy, uint32_t n, uint16_t c)   /* filled, nonzero (SVG's rule) */
{
    int32_t y, ymin = 127, ymax = -128;
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (xy[2 * i + 1] < ymin) ymin = xy[2 * i + 1];
        if (xy[2 * i + 1] > ymax) ymax = xy[2 * i + 1];
    }
    for (y = ymin; y < ymax; y++) {             /* the pixel row y covers y .. y+1: cut it at its middle */
        int32_t xs[8], ws[8], k = 0, a, b, wind = 0;
        for (i = 0; i < n && k < 8; i++) {
            int32_t x0 = xy[2 * i], y0 = xy[2 * i + 1], x1 = xy[2 * ((i + 1) % n)], y1 = xy[2 * ((i + 1) % n) + 1];
            int32_t yc2 = 2 * y + 1;                                    /* 2 x (y + 0.5) */
            if ((2 * y0 <= yc2) == (2 * y1 <= yc2)) continue;
            xs[k] = (2 * x0 * (y1 - y0) + (x1 - x0) * (yc2 - 2 * y0)) / (2 * (y1 - y0));
            ws[k++] = y1 > y0 ? 1 : -1;
        }
        for (a = 1; a < k; a++)                 /* sort (a few) */
            for (b = a; b > 0 && xs[b - 1] > xs[b]; b--) {
                int32_t t = xs[b]; xs[b] = xs[b - 1]; xs[b - 1] = t;
                t = ws[b]; ws[b] = ws[b - 1]; ws[b - 1] = t;
            }
        for (a = 0; a + 1 < k; a++) {           /* inside where the winding is not 0 */
            wind += ws[a];
            if (wind) cv_rect(ox + xs[a], oy + y, xs[a + 1] - xs[a], 1, c);
        }
    }
}

static void ring(int32_t cx, int32_t cy, int32_t r, uint16_t c)  /* a circle's outline, 1 px */
{
    int32_t x, y;
    for (y = -r - 1; y <= r + 1; y++)
        for (x = -r - 1; x <= r + 1; x++) {
            int32_t d = x * x + y * y;
            if (d >= (r - 1) * r + 1 && d <= r * (r + 1)) cv_pset(cx + x, cy + y, c);
        }
}

static void disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t x, y;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r / 2) cv_pset(cx + x, cy + y, c);
}

static void thick(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)   /* stroke-width 2 */
{
    cv_line(x0, y0, x1, y1, c);
    if (x0 == x1) cv_line(x0 - 1, y0, x1 - 1, y1, c);
    else cv_line(x0, y0 - 1, x1, y1 - 1, c);
}

static void frame(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint16_t c)
{
    cv_rect(x, y, w, t, c), cv_rect(x, y + h - t, w, t, c), cv_rect(x, y, t, h, c), cv_rect(x + w - t, y, t, h, c);
}

/* ---- the tiles (tilePixelArt and the cells' CSS) */
static void draw_tile(int32_t x, int32_t y, const struct rs_tile_t *t)
{
    static const int8_t ARROW[4][14] = {        /* the belt's arrow, N E S W */
        {20, 8, 14, 18, 17, 18, 17, 28, 23, 28, 23, 18, 26, 18},
        {32, 20, 22, 14, 22, 17, 12, 17, 12, 23, 22, 23, 22, 26},
        {20, 32, 14, 22, 17, 22, 17, 12, 23, 12, 23, 22, 26, 22},
        {8, 20, 18, 14, 18, 17, 28, 17, 28, 23, 18, 23, 18, 26}};
    static const int8_t BOLT1[16] = {14, 10, 16, 14, 12, 18, 16, 20, 10, 30, 18, 24, 14, 26, 20, 12};
    static const int8_t BOLT2[16] = {24, 12, 26, 16, 22, 20, 26, 22, 20, 32, 28, 26, 24, 28, 30, 14};
    static const int8_t DIA_L[8] = {20, 14, 16, 20, 20, 26, 24, 20}, DIA_R[8] = {20, 14, 24, 20, 20, 26, 16, 20};
    int32_t i;
    switch (t->type) {
    case RS_T_BELT: {
        uint16_t bg = RGB(0x00, 0x1a, 0x2e);
        cv_rect(x, y, CELL, CELL, bg);
        frame(x, y, CELL, CELL, 1, C_BELT);
        if (t->dir == RS_N || t->dir == RS_S) cv_rect(x + 7, y + 2, 2, 36, C_BELT), cv_rect(x + 31, y + 2, 2, 36, C_BELT);
        else cv_rect(x + 2, y + 7, 36, 2, C_BELT), cv_rect(x + 2, y + 31, 36, 2, C_BELT);
        poly(x, y, ARROW[t->dir & 3], 7, C_BELT);
        break;
    }
    case RS_T_ROTL:
    case RS_T_ROTR: {
        cv_rect(x, y, CELL, CELL, RGB(0x1a, 0x14, 0x00));
        frame(x, y, CELL, CELL, 1, C_ROT);
        thick(x + 20, y + 1, x + 20, y + 6, C_ROT), thick(x + 20, y + 34, x + 20, y + 39, C_ROT);
        thick(x + 1, y + 20, x + 6, y + 20, C_ROT), thick(x + 34, y + 20, x + 39, y + 20, C_ROT);
        thick(x + 34, y + 6, x + 30, y + 10, C_ROT), thick(x + 34, y + 34, x + 30, y + 30, C_ROT);
        thick(x + 6, y + 34, x + 10, y + 30, C_ROT), thick(x + 6, y + 6, x + 10, y + 10, C_ROT);
        disc(x + 20, y + 20, 5, C_ROT);
        poly(x, y, t->type == RS_T_ROTL ? DIA_L : DIA_R, 4, C_PANEL);
        break;
    }
    case RS_T_ZAP: {
        uint16_t bg = RGB(0x1a, 0x00, 0x12);
        cv_rect(x, y, CELL, CELL, bg);
        frame(x, y, CELL, CELL, 1, C_ZAP);
        for (i = 10; i <= 30; i += 10) cv_rect(x + i, y + 4, 1, 32, mix565(C_ZAP, bg, 128));
        poly(x, y, BOLT1, 8, C_ZAP);
        poly(x, y, BOLT2, 8, mix565(C_ZAP, bg, 179));
        cv_rect(x + 31, y + 7, 2, 2, C_ZAP), cv_rect(x + 7, y + 33, 2, 2, C_ZAP);
        break;
    }
    case RS_T_PULSE: {
        uint16_t bg = RGB(0x00, 0x1a, 0x00);
        cv_rect(x, y, CELL, CELL, bg);
        frame(x, y, CELL, CELL, 1, C_PULSE);
        ring(x + 20, y + 20, 4, C_PULSE);
        ring(x + 20, y + 20, 8, mix565(C_PULSE, bg, 154));
        ring(x + 20, y + 20, 12, mix565(C_PULSE, bg, 77));
        cv_rect(x + 16, y + 16, 8, 8, C_PULSE);
        cv_rect(x + 8, y + 8, 3, 3, mix565(C_PULSE, bg, 128)), cv_rect(x + 29, y + 29, 3, 3, mix565(C_PULSE, bg, 128));
        break;
    }
    case RS_T_BLOCK: {
        int32_t k;
        for (k = 0; k < CELL; k++)              /* the cell's 45-degree stripes */
            for (i = 0; i < CELL; i++)
                cv_pset(x + i, y + k, ((i + k) / 6) & 1 ? RGB(0x1a, 0x1a, 0x2e) : RGB(0x22, 0x22, 0x38));
        cv_rect(x + 4, y + 4, 32, 32, RGB(0x5c, 0x3d, 0x1e));
        cv_rect(x + 4, y + 4, 32, 6, RGB(0x6e, 0x4b, 0x28)), cv_rect(x + 4, y + 17, 32, 6, RGB(0x6e, 0x4b, 0x28));
        cv_rect(x + 4, y + 30, 32, 6, RGB(0x6e, 0x4b, 0x28));
        for (i = 0; i < 4; i++) cv_rect(x + 4, y + (i == 0 ? 10 : i == 1 ? 17 : i == 2 ? 23 : 30), 32, 1, RGB(0x4a, 0x2e, 0x14));
        for (i = 0; i < 29; i++) {              /* the cross braces */
            cv_rect(x + 6 + i, y + 6 + i, 2, 1, RGB(0x8b, 0x69, 0x39));
            cv_rect(x + 33 - i, y + 6 + i, 2, 1, RGB(0x8b, 0x69, 0x39));
        }
        frame(x + 4, y + 4, 32, 32, 2, RGB(0x4a, 0x2e, 0x14));
        cv_rect(x + 7, y + 7, 2, 2, RGB(0xaa, 0xaa, 0xaa)), cv_rect(x + 31, y + 7, 2, 2, RGB(0xaa, 0xaa, 0xaa));
        cv_rect(x + 7, y + 31, 2, 2, RGB(0xaa, 0xaa, 0xaa)), cv_rect(x + 31, y + 31, 2, 2, RGB(0xaa, 0xaa, 0xaa));
        cv_rect(x + 19, y + 19, 2, 2, RGB(0xaa, 0xaa, 0xaa));
        break;
    }
    default:                                    /* open: the dither, the line */
        cv_rect(x, y, CELL, CELL, C_CELL);
        frame(x, y, CELL, CELL, 1, C_CELL_LINE);
        for (i = 4; i < CELL; i += 8) {
            int32_t k;
            for (k = 4; k < CELL; k += 8) cv_pset(x + i, y + k, C_DITHER);
        }
        break;
    }
}

/* ---- the bots (robotPixelArt: Scout-7 and Echo-3, 32 x 32) */
enum { P_MAIN, P_DARK, P_ACC, P_VISOR, P_WHITE, P_SHINE, P_THRUST };
static const uint8_t BOT_RECTS[][5] = {         /* x, y, w, h, colour: both bots */
    {10, 4, 12, 2, P_MAIN}, {8, 6, 16, 10, P_MAIN}, {9, 6, 3, 2, P_SHINE}, {9, 8, 14, 6, P_VISOR},
    {10, 9, 4, 4, P_ACC}, {12, 10, 2, 2, P_WHITE}, {18, 9, 4, 4, P_ACC}, {18, 10, 2, 2, P_WHITE},
    {13, 14, 6, 1, P_DARK}, {6, 8, 2, 4, P_DARK}, {24, 8, 2, 4, P_DARK}, {11, 16, 10, 6, P_MAIN},
    {14, 17, 4, 2, P_ACC}, {8, 17, 3, 4, P_DARK}, {7, 21, 3, 2, P_ACC}, {21, 17, 3, 4, P_DARK},
    {22, 21, 3, 2, P_ACC}, {12, 22, 3, 4, P_DARK}, {17, 22, 3, 4, P_DARK}, {11, 26, 4, 2, P_MAIN},
    {17, 26, 4, 2, P_MAIN}, {12, 28, 2, 2, P_THRUST}, {18, 28, 2, 2, P_THRUST}};
static const uint8_t ANT_A[][5] = {{15, 0, 2, 2, P_ACC}, {15, 2, 2, 2, P_DARK}};
static const uint8_t ANT_B[][5] = {{9, 0, 2, 2, P_ACC}, {21, 0, 2, 2, P_ACC}, {10, 2, 2, 2, P_DARK}, {20, 2, 2, 2, P_DARK}};

static void sprite_rects(int32_t x, int32_t y, const uint8_t (*r)[5], uint32_t n, const uint16_t *pal, uint32_t half)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (half) {
            int32_t x0 = r[i][0] >> 1, y0 = r[i][1] >> 1, x1 = (r[i][0] + r[i][2] + 1) >> 1, y1 = (r[i][1] + r[i][3] + 1) >> 1;
            cv_rect(x + x0, y + y0, x1 - x0, y1 - y0, pal[r[i][4]]);
        } else
            cv_rect(x + r[i][0], y + r[i][1], r[i][2], r[i][3], pal[r[i][4]]);
    }
}

static void draw_bot(int32_t x, int32_t y, int b, uint32_t half, uint16_t under)
{
    uint16_t main = b ? C_BOT_B : C_BOT_A, dark = b ? C_BOT_B2 : C_BOT_A2, acc = b ? C_WARN : C_ACCENT;
    uint16_t pal[7] = {main, dark, acc, RGB(0x0a, 0x0a, 0x1a), C_WHITE, mix565(dark, main, 128), mix565(acc, under, 154)};
    sprite_rects(x, y, BOT_RECTS, sizeof BOT_RECTS / sizeof BOT_RECTS[0], pal, half);
    if (b) sprite_rects(x, y, ANT_B, 4, pal, half);
    else sprite_rects(x, y, ANT_A, 2, pal, half);
}

static void draw_arrow(int32_t x, int32_t y, uint32_t dir, uint16_t c)   /* the facing: a triangle at the cell's edge */
{
    static const int8_t T[4][6] = {{20, 1, 16, 5, 24, 5}, {39, 20, 35, 16, 35, 24}, {20, 39, 16, 35, 24, 35}, {1, 20, 5, 16, 5, 24}};
    poly(x, y, T[dir & 3], 3, c);
}

/* ---- the effects (explosionPixelArt 16 x 16 at 2 px, heartPixelArt, clashPixelArt) */
static void draw_explosion(int32_t x, int32_t y, int b)
{
    static const uint8_t R[][4] = {{7, 0, 2, 2}, {3, 2, 2, 2}, {11, 2, 2, 2}, {0, 7, 2, 2}, {14, 7, 2, 2}, {3, 11, 2, 2},
                                   {11, 11, 2, 2}, {7, 14, 2, 2}};
    uint16_t c = b ? C_BOT_B : C_BOT_A;
    uint32_t i;
    for (i = 0; i < sizeof R / sizeof R[0]; i++) cv_rect(x + 4 + 2 * R[i][0], y + 4 + 2 * R[i][1], 2 * R[i][2], 2 * R[i][3], c);
    cv_rect(x + 14, y + 14, 12, 12, C_WHITE);
    cv_rect(x + 16, y + 16, 8, 8, c);
    cv_rect(x + 18, y + 18, 4, 4, C_WHITE);
}

static void draw_heart(int32_t x, int32_t y)
{
    static const char *const ROWS[8] = {"  ##  ## ", " ########", "#########", "#########", " ####### ", "  #####  ",
                                        "   ###   ", "    #    "};
    uint32_t r, k;
    for (r = 0; r < 8; r++)
        for (k = 0; k < 9; k++)
            if (ROWS[r][k] == '#') cv_rect(x + 2 + 4 * (int32_t)k, y + 4 + 4 * (int32_t)r, 4, 4, r < 2 ? RGB(0xff, 0x8e, 0xc4) : RGB(0xff, 0x4d, 0xa6));
}

static void draw_clash(int32_t x, int32_t y)
{
    static const uint8_t W[][2] = {{18, 0}, {18, 36}, {0, 18}, {36, 18}};
    static const uint8_t Y[][2] = {{6, 6}, {30, 6}, {6, 30}, {30, 30}, {14, 10}, {22, 10}, {10, 14}, {26, 14},
                                   {10, 22}, {26, 22}, {14, 26}, {22, 26}};
    uint32_t i;
    for (i = 0; i < 4; i++) cv_rect(x + W[i][0], y + W[i][1], 4, 4, C_WHITE);
    for (i = 0; i < 12; i++) cv_rect(x + Y[i][0], y + Y[i][1], 4, 4, RGB(0xff, 0xdd, 0x44));
    cv_rect(x + 14, y + 14, 12, 12, C_WHITE);
    cv_rect(x + 16, y + 16, 8, 8, RGB(0xff, 0xdd, 0x44));
    cv_rect(x + 18, y + 18, 4, 4, C_WHITE);
}

/* ---- the whole screen */
struct rs_fx {                                  /* what is showing, until when (fm1_ms), as the HTML times them */
    uint32_t boom_until[2], heart_until, clash_until[2], lit_until, hidden_until[2];
    int8_t boom_x[2], boom_y[2], heart_x, heart_y;
    uint32_t lit;
};

static void screen_effects(struct rs_fx *fx, const struct rs_view *prev, const struct rs_view *v, uint32_t now_ms)
{
    int b;
    for (b = 0; b < 2; b++) {
        if (v->died[b]) {                       /* where it burned out: where it stood before the respawn */
            fx->boom_x[b] = prev->bot[b].x, fx->boom_y[b] = prev->bot[b].y;
            fx->boom_until[b] = now_ms + 700, fx->hidden_until[b] = now_ms + 600;
        }
        if (v->clash[b]) fx->clash_until[b] = now_ms + 500;
    }
    if (v->love) fx->heart_x = v->bot[0].x, fx->heart_y = v->bot[0].y, fx->heart_until = now_ms + 700;
    fx->lit = v->active, fx->lit_until = now_ms + 300;
}

static uint32_t fx_due(const struct rs_fx *fx, uint32_t now_ms)   /* when the next effect runs out (0: none) */
{
    const uint32_t *t = &fx->boom_until[0];
    uint32_t i, best = 0;
    for (i = 0; i < 8; i++)                     /* the eight "until"s, in the struct's order */
        if ((int32_t)(t[i] - now_ms) > 0 && (!best || (int32_t)(t[i] - best) < 0)) best = t[i];
    return best;
}

static void put_num(char *p, uint32_t v, uint32_t w)   /* v in w digits, zeros in front */
{
    while (w--) p[w] = (char)('0' + v % 10u), v /= 10u;
}

static void draw_board(const struct rs_view *v, const struct rs_fx *fx, uint32_t now_ms)
{
    int32_t i, b;
    char top[32] = "STEP 00/00", cyc[16] = "CYCLE 0000";
    cv_begin(240, 240, C_BG);
    /* the step and the cycle; the board's name */
    put_num(top + 5, (uint32_t)(v->step < 0 ? 0 : v->step + 1), 2);
    put_num(top + 8, v->loop_len, 2);
    put_num(cyc + 6, (v->cycle + 1u) % 10000u, 4);
    cv_text(4, 2, &FONT_S, top, v->playing ? C_TEXT : C_MUTED);
    cv_text(236 - text_w(&FONT_S, cyc), 2, &FONT_S, cyc, C_MUTED);
    /* the board */
    for (i = 0; i < RS_ROWS * RS_COLS; i++) {
        int32_t x = BX + (i % RS_COLS) * CELL, y = BY + (i / RS_COLS) * CELL;
        draw_tile(x, y, &v->board.t[i]);
        if (fx->lit >> i & 1u && (int32_t)(fx->lit_until - now_ms) > 0) frame(x, y, CELL, CELL, 2, C_WHITE);
    }
    for (b = 0; b < 2; b++) {                   /* the launch pads' marks (the HTML's trails never grow past them) */
        int32_t x = BX + START[b].x * CELL, y = BY + START[b].y * CELL;
        if (v->bot[b].x == START[b].x && v->bot[b].y == START[b].y) continue;
        disc(b ? x + 34 : x + 5, y + 34, 2, mix565(b ? C_BOT_B : C_BOT_A, C_CELL, 64));
    }
    for (b = 0; b < 2; b++) {
        const struct rs_bot *t = &v->bot[b];
        int32_t x = BX + t->x * CELL, y = BY + t->y * CELL;
        int together = v->bot[0].x == v->bot[1].x && v->bot[0].y == v->bot[1].y;
        if ((int32_t)(fx->hidden_until[b] - now_ms) > 0) continue;
        if (together) draw_bot(x + (b ? 21 : 3), y + 12, b, 1, C_CELL);
        else draw_bot(x + 4, y + 4, b, 0, C_CELL);
        draw_arrow(x, y, t->dir, b ? C_WARN : C_ACCENT);
        if ((int32_t)(fx->clash_until[b] - now_ms) > 0) draw_clash(x, y);
    }
    for (b = 0; b < 2; b++)
        if ((int32_t)(fx->boom_until[b] - now_ms) > 0) draw_explosion(BX + fx->boom_x[b] * CELL, BY + fx->boom_y[b] * CELL, b);
    if ((int32_t)(fx->heart_until - now_ms) > 0) draw_heart(BX + fx->heart_x * CELL, BY + fx->heart_y * CELL);
    /* the bots' health (the status cards): 6 less the heat */
    for (b = 0; b < 2; b++) {
        int32_t x = b ? 124 : 4, hp = 6 - (v->bot[b].heat > 6 ? 6 : v->bot[b].heat < 0 ? 0 : v->bot[b].heat);
        uint16_t c = hp <= 2 ? C_DANGER : hp <= 4 ? C_WARN : C_PULSE;
        int32_t w = cv_text(x, 222, &FONT_S, b ? "ECHO-3" : "SCOUT-7", b ? C_BOT_B : C_BOT_A) + 4;
        cv_rect(w, 228, x + 112 - w, 6, RGB(0x1a, 0x1a, 0x2e));
        cv_rect(w, 228, (x + 112 - w) * hp / 6, 6, c);
    }
    cv_blit(0, 0);
}
