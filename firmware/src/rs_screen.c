/* SPDX-License-Identifier: GPL-3.0-only */
/* RoboSeq's board on the FM-1's 240 x 240 screen: the HTML's renderBoard() as pixel art. The 5 x 5 board in cells
 * of 40 px (the HTML's tile art is drawn on a 40 x 40 grid, so it comes over 1:1), the bots on a 32 x 32 grid as
 * the HTML's robotPixelArt, the tiles an event lit, the explosion, the heart and the clash star for as long as the
 * HTML shows them; down its sides the two registers (Scout-7 left, Echo-3 right), above it the step and the cycle,
 * below it the bots' health as the HTML's status cards.
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

static void frame(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint16_t c)
{
    cv_rect(x, y, w, t, c), cv_rect(x, y + h - t, w, t, c), cv_rect(x, y, t, h, c), cv_rect(x + w - t, y, t, h, c);
}

/* ---- the tiles: pixel art of RoboSeq's own on a 20 x 20 grid of 2 px (the robots' and the effects' rhythm), after
 * the HTML's tilePixelArt, lit from the top left (tools/art/tiles.py makes it): the belt's arrow, the rotator's
 * gear that is its turn, the zap's bolt, the pulse's rings, the crate. A letter is a colour of the tile's palette
 * ('.' its ground, 'g' that in shadow, 'a' its colour, 'h' lit, 'b' and 'd' dimmed, 'k' dark, 'c' a light one; the
 * crate its woods). The belt is drawn pointing east and turned; the right rotator is the left one mirrored. */
#include "rs_tiles.h"                         /* the art (tools/art/tiles.py) */

static uint16_t tile_colour(uint32_t type, char ch)
{
    uint16_t ground, main, light = C_WHITE;
    switch (type) {
    case RS_T_BELT: ground = RGB(0x00, 0x1a, 0x2e), main = C_BELT; break;
    case RS_T_ROTL:
    case RS_T_ROTR: ground = RGB(0x1a, 0x14, 0x00), main = C_ROT, light = RGB(0xff, 0xf3, 0xb0); break;
    case RS_T_ZAP: ground = RGB(0x1a, 0x00, 0x12), main = C_ZAP, light = RGB(0xff, 0xd6, 0xe0); break;
    case RS_T_PULSE: ground = RGB(0x00, 0x1a, 0x00), main = C_PULSE, light = RGB(0xea, 0xff, 0xf0); break;
    default:                                    /* the crate */
        switch (ch) {
        case 'a': return RGB(0x6e, 0x4b, 0x28);
        case 'b': return RGB(0x5c, 0x3d, 0x1e);
        case 'h': return RGB(0x8a, 0x62, 0x36);
        case 'k': return RGB(0x2e, 0x1c, 0x0c);
        case 'e': return RGB(0x8b, 0x69, 0x39);
        case 'n': return RGB(0xaa, 0xaa, 0xaa);
        case 'c':
        case 's': return RGB(0x4a, 0x2e, 0x14);
        default: return RGB(0x1a, 0x1a, 0x2e);
        }
    }
    switch (ch) {
    case 'a': return main;
    case 'h': return mix565(C_WHITE, main, 115);
    case 'b': return mix565(main, ground, 115);
    case 'd': return mix565(main, ground, 64);
    case 'k': return mix565(main, C_BLACK, 166);
    case 'g': return mix565(ground, C_BLACK, 141);
    case 'c': return light;
    default: return ground;
    }
}

static void draw_tile(int32_t x, int32_t y, const struct rs_tile_t *t)
{
    const char (*art)[21] = 0;
    int32_t gx, gy;
    switch (t->type) {
    case RS_T_BELT: art = ART_BELT; break;
    case RS_T_ROTL:
    case RS_T_ROTR: art = ART_ROTL; break;
    case RS_T_ZAP: art = ART_ZAP; break;
    case RS_T_PULSE: art = ART_PULSE; break;
    case RS_T_BLOCK: art = ART_BLOCK; break;
    default:                                    /* open: the ground, a dot every 8 px, a line above and left */
        cv_rect(x, y, CELL, CELL, C_CELL);
        cv_rect(x, y, CELL, 2, C_CELL_LINE), cv_rect(x, y, 2, CELL, C_CELL_LINE);
        for (gy = 2; gy < 20; gy += 4)
            for (gx = 2; gx < 20; gx += 4) cv_rect(x + 2 * gx, y + 2 * gy, 2, 2, C_DITHER);
        return;
    }
    for (gy = 0; gy < 20; gy++)
        for (gx = 0; gx < 20; gx++) {
            int32_t sx = gx, sy = gy;           /* where in the art this grid cell comes from */
            if (t->type == RS_T_BELT) {
                if (t->dir == RS_N) sx = 19 - gy, sy = gx;
                else if (t->dir == RS_S) sx = gy, sy = 19 - gx;
                else if (t->dir == RS_W) sx = 19 - gx;
            } else if (t->type == RS_T_ROTR)
                sx = 19 - gx;
            cv_rect(x + 2 * gx, y + 2 * gy, 2, 2, tile_colour(t->type, art[sy][sx]));
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

static void draw_arrow(int32_t x, int32_t y, uint32_t dir, uint16_t c)   /* the facing: a 3-step wedge at the cell's edge */
{
    int32_t k;
    for (k = 0; k < 3; k++) {                   /* row k of the wedge: 2 (3 - k) grid cells wide, k from the edge */
        int32_t w = 2 * (3 - k) * 2, o = 20 - (3 - k) * 2;   /* px */
        if (dir == RS_N) cv_rect(x + o, y + 2 * k, w, 2, c);
        else if (dir == RS_S) cv_rect(x + o, y + CELL - 2 - 2 * k, w, 2, c);
        else if (dir == RS_W) cv_rect(x + 2 * k, y + o, 2, w, c);
        else cv_rect(x + CELL - 2 - 2 * k, y + o, 2, w, c);
    }
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

/* ---- the keys' side (D16): the register on the white keys, the steering on the black ones */
struct rs_ui {
    uint8_t sel, bank, swapped;                 /* the bot on the white keys, the 16-step page shown */
    uint8_t len[2];                             /* each register's length */
    int16_t playing_at[2];                      /* the step of each register playing now (-1: none) */
    uint8_t steer[2];                           /* a bot's next move, by hand (action + 1) */
    struct rs_step steps[2][16];                /* the page of each */
};

static const char *const ACT_GLYPH[RS_NACTIONS + 1][7] = {   /* the HTML's icons (⇧ ⇩ ↺ ↻ ⇶ ✦ ·), 7 x 7 */
    {"...#...", "..###..", ".#####.", "...#...", "...#...", "...#...", "...#..."},
    {"...#...", "...#...", "...#...", "...#...", ".#####.", "..###..", "...#..."},
    {"#.###..", "##...#.", "###...#", "......#", "......#", ".#...#.", "..###.."},
    {"..###.#", ".#...##", "#...###", "#......", "#......", ".#...#.", "..###.."},
    {"#..#...", ".#..#..", "..#..#.", "...#..#", "..#..#.", ".#..#..", "#..#..."},
    {"...#...", "...#...", "..###..", "#######", "..###..", "...#...", "...#..."},
    {".......", ".......", "..###..", "..###..", "..###..", ".......", "......."},
    {".#.....", "###....", ".#...#.", ".#...#.", ".#...#.", "....###", ".....#."}};   /* (the last: ⇅, swapped) */
static const uint16_t ACT_COLOUR[RS_NACTIONS] = {   /* renderStepCard's actionColorMap */
    C_PULSE, RGB(0x73, 0xb7, 0xa9), C_ROT, C_BELT, C_DANGER, RGB(0x59, 0xd9, 0xc2), C_MUTED};

static void glyph7(int32_t x, int32_t y, uint32_t g, int32_t px, uint16_t c)
{
    int32_t r, k;
    for (r = 0; r < 7; r++)
        for (k = 0; k < 7; k++)
            if (ACT_GLYPH[g][r][k] == '#') cv_rect(x + k * px, y + r * px, px, px, c);
}

static void draw_strip(const struct rs_ui *u, int b)   /* a register down the board's side: Scout-7 left, Echo-3 right */
{
    uint32_t k;
    int32_t x0 = b ? 220 : 0, on = u->sel == b;  /* the one on the white keys bright, the other dimmed */
    uint16_t bot = b ? C_BOT_B : C_BOT_A, cell = RGB(0x1a, 0x1a, 0x2e);
    for (k = 0; k < 16; k++) {
        int32_t y = (int32_t)k * 15, i = (int32_t)(u->bank * 16u + k);
        const struct rs_step *st = &u->steps[b][k];
        if (i < u->len[b]) {
            uint16_t c = ACT_COLOUR[st->action];
            if (st->prob != 100) c = mix565(c, cell, 140);
            if (!on) c = mix565(c, cell, 110);
            cv_rect(x0 + 1, y, 18, 14, cell);
            glyph7(x0 + 3, y, st->action, 2, c);
            if (i == u->playing_at[b]) frame(x0, y, 20, 15, 1, on ? C_WHITE : C_MUTED);
        } else
            cv_rect(x0 + 9, y + 6, 2, 2, cell); /* past its length */
    }
    if (on) cv_rect(b ? 219 : 20, 0, 1, 240, bot);   /* on the white keys: its colour along the board */
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

static void draw_board(const struct rs_view *v, const struct rs_fx *fx, const struct rs_ui *u, uint32_t now_ms)
{
    int32_t i, b;
    char top[32] = "STEP 00/00", cyc[8] = "C0000";
    cv_begin(240, 240, C_BG);
    draw_strip(u, 0), draw_strip(u, 1);
    put_num(top + 5, (uint32_t)(v->step < 0 ? 0 : v->step + 1), 2);   /* the loop's step, the cycle */
    put_num(top + 8, v->loop_len, 2);
    put_num(cyc + 1, (v->cycle + 1u) % 10000u, 4);
    cv_text(24, 2, &FONT_S, top, v->playing ? C_TEXT : C_MUTED);
    cv_text(216 - text_w(&FONT_S, cyc), 2, &FONT_S, cyc, C_MUTED);
    /* the board */
    for (i = 0; i < RS_ROWS * RS_COLS; i++) {
        int32_t x = BX + (i % RS_COLS) * CELL, y = BY + (i / RS_COLS) * CELL;
        draw_tile(x, y, &v->board.t[i]);
        if (fx->lit >> i & 1u && (int32_t)(fx->lit_until - now_ms) > 0) frame(x, y, CELL, CELL, 2, C_WHITE);
    }
    for (b = 0; b < 2; b++) {                   /* the launch pads' marks (the HTML's trails never grow past them) */
        int32_t x = BX + START[b].x * CELL, y = BY + START[b].y * CELL;
        if (v->bot[b].x == START[b].x && v->bot[b].y == START[b].y) continue;
        cv_rect(b ? x + 32 : x + 4, y + 32, 4, 4, mix565(b ? C_BOT_B : C_BOT_A, C_CELL, 64));
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
        if (u->steer[b]) {                      /* steered: what its next move will be, on it */
            cv_rect(x + 1, y + 1, 16, 16, b ? C_BOT_B : C_BOT_A);
            glyph7(x + 2, y + 2, u->steer[b] - 1u, 2, C_WHITE);
        }
    }
    for (b = 0; b < 2; b++)
        if ((int32_t)(fx->boom_until[b] - now_ms) > 0) draw_explosion(BX + fx->boom_x[b] * CELL, BY + fx->boom_y[b] * CELL, b);
    if ((int32_t)(fx->heart_until - now_ms) > 0) draw_heart(BX + fx->heart_x * CELL, BY + fx->heart_y * CELL);
    /* the bots' health (the status cards): 6 less the heat */
    for (b = 0; b < 2; b++) {
        int32_t x = b ? 122 : 24, hp = 6 - (v->bot[b].heat > 6 ? 6 : v->bot[b].heat < 0 ? 0 : v->bot[b].heat);
        uint16_t c = hp <= 2 ? C_DANGER : hp <= 4 ? C_WARN : C_PULSE;
        int32_t w = cv_text(x, 222, &FONT_S, b ? "ECHO" : "SCOUT", b ? C_BOT_B : C_BOT_A) + 3;
        cv_rect(w, 228, x + 86 - w, 6, RGB(0x1a, 0x1a, 0x2e));
        cv_rect(w, 228, (x + 86 - w) * hp / 6, 6, c);
    }
    if (u->swapped) glyph7(113, 225, RS_NACTIONS, 1, C_TEXT);   /* the registers swapped */
    cv_blit(0, 0);
}
