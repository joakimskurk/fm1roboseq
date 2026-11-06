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
    uint8_t drum_tick;
    struct rs_bot bot[2];
    struct rs_board board;
};

#include "rs_icons.h"                         /* the step icons, the heart, the digits (tools/art/icons.py) */

static int8_t lead_badge = -99;                 /* the keys play the lead (D21): its octave, else -99 */
static void screen_out(void);                   /* (every page ends with it: the lead's badge, then to the screen) */

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

/* ---- the tiles: pixel art of RoboSeq's own, 40 x 40 at 1 px, after the HTML's tilePixelArt, lit from the top left (tools/art/tiles.py makes it): the belt's arrow, the rotator's
 * gear that is its turn, the zap's bolt, the pulse's rings, the crate. A letter is a colour of the tile's palette
 * ('.' its ground, 'g' that in shadow, 'a' its colour, 'h' lit, 'b' and 'd' dimmed, 'k' dark, 'c' a light one; the
 * crate its woods). Each way a belt points, and each rotator, is its own picture, so the light stays top left. */
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
    const char (*art)[41] = 0;
    int32_t gx, gy;
    switch (t->type) {
    case RS_T_BELT: art = t->dir == RS_N ? ART_BELT_N : t->dir == RS_S ? ART_BELT_S : t->dir == RS_W ? ART_BELT_W : ART_BELT_E; break;
    case RS_T_ROTL: art = ART_ROTL; break;
    case RS_T_ROTR: art = ART_ROTR; break;
    case RS_T_ZAP: art = ART_ZAP; break;
    case RS_T_PULSE: art = ART_PULSE; break;
    case RS_T_BLOCK: art = ART_BLOCK; break;
    default:                                    /* open: the ground, a dot every 8 px, a line above and left */
        cv_rect(x, y, CELL, CELL, C_CELL);
        cv_rect(x, y, CELL, 1, C_CELL_LINE), cv_rect(x, y, 1, CELL, C_CELL_LINE);
        for (gy = 4; gy < CELL; gy += 8)
            for (gx = 4; gx < CELL; gx += 8) cv_pset(x + gx, y + gy, C_DITHER);
        return;
    }
    for (gy = 0; gy < CELL; gy++)
        for (gx = 0; gx < CELL; gx++) cv_pset(x + gx, y + gy, tile_colour(t->type, art[gy][gx]));
}

/* ---- the bots: Scout-7 and Echo-3 (tools/art/robots.py, after robotPixelArt), 18 x 18 at 1 px, each on its own
 * side of a tile (D17) */
#include "rs_robots.h"

static void draw_bot(int32_t x, int32_t y, int b, uint32_t mini, uint16_t under)   /* 18 x 18 (mini: the only size now) */
{
    uint16_t main = b ? C_BOT_B : C_BOT_A, dark = b ? C_BOT_B2 : C_BOT_A2, acc = b ? C_WARN : C_ACCENT;
    int32_t gx, gy;
    (void)mini;
    for (gy = 0; gy < 18; gy++)
        for (gx = 0; gx < 18; gx++) {
            uint16_t c;
            switch ((b ? ART_MINI_B : ART_MINI_A)[gy][gx]) {
            case 'o': c = RGB(0x05, 0x05, 0x0c); break;
            case 'm': c = main; break;
            case 'M': c = mix565(C_WHITE, main, 90); break;
            case 'n': c = mix565(main, dark, 128); break;
            case 'd': c = dark; break;
            case 'a': c = acc; break;
            case 'v': c = RGB(0x0a, 0x0a, 0x1a); break;
            case 'w': c = C_WHITE; break;
            case 't': c = mix565(acc, under, 154); break;
            default: continue;
            }
            cv_pset(x + gx, y + gy, c);
        }
}

static void dim_cell(int32_t x, int32_t y)       /* the tile under a bot, darker: the bot stands out on any tile */
{
    int32_t i, k;
    for (k = 1; k < CELL - 1; k++)
        for (i = 1; i < CELL - 1; i++) {
            uint16_t *p = &cv_px[(uint32_t)(y + k) * cv_w + (uint32_t)(x + i)];
            *p = swap16(mix565(swap16(*p), C_BLACK, 110));
        }
}

static void draw_arrow_at(int32_t cx, int32_t cy, uint32_t dir, uint16_t c)   /* a small facing wedge around cx, cy */
{
    int32_t k, i;
    for (k = 0; k < 3; k++)                     /* row k from the tip: 2 k + 1 px across (+ an outline) */
        for (i = -k - 1; i <= k + 1; i++) {
            uint16_t col = i < -k || i > k ? RGB(0x05, 0x05, 0x0c) : c;
            int32_t d = k - 1;
            if (dir == RS_N) cv_pset(cx + i, cy + d, col);
            else if (dir == RS_S) cv_pset(cx + i, cy - d, col);
            else if (dir == RS_W) cv_pset(cx + d, cy + i, col);
            else cv_pset(cx - d, cy + i, col);
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

static void draw_heart(int32_t x, int32_t y, int32_t px, int glow)   /* the heart (tools/art/icons.py), 15 x 13 x px; */
{                                                                    /* glow: a beat, a light ring round it */
    int32_t r, k;
    if (glow)                                   /* a ring of light round its outline, one of its pixels wide */
        for (r = 0; r < 13; r++)
            for (k = 0; k < 15; k++)
                if (HEART[r][k] == 'o') {
                    uint16_t c = RGB(0xff, 0xa8, 0xd2);
                    cv_rect(x + px * (k - 1), y + px * r, px, px, c), cv_rect(x + px * (k + 1), y + px * r, px, px, c);
                    cv_rect(x + px * k, y + px * (r - 1), px, px, c), cv_rect(x + px * k, y + px * (r + 1), px, px, c);
                }
    for (r = 0; r < 13; r++)
        for (k = 0; k < 15; k++) {
            char ch = HEART[r][k];
            uint16_t c = ch == 'o' ? RGB(0x14, 0x05, 0x0c) : ch == 'h' ? RGB(0xff, 0x8e, 0xc4) : ch == 'w' ? C_WHITE :
                         ch == 'n' ? RGB(0xd0, 0x2a, 0x7c) : RGB(0xff, 0x4d, 0xa6);
            if (ch != '.') cv_rect(x + px * k, y + px * r, px, px, c);
        }
}

static void draw_clash(int32_t x, int32_t y, int32_t h)   /* h: 1 a tile's size (40), 2 half */
{
    static const uint8_t W[][2] = {{18, 0}, {18, 36}, {0, 18}, {36, 18}};
    static const uint8_t Y[][2] = {{6, 6}, {30, 6}, {6, 30}, {30, 30}, {14, 10}, {22, 10}, {10, 14}, {26, 14},
                                   {10, 22}, {26, 22}, {14, 26}, {22, 26}};
    uint32_t i;
    for (i = 0; i < 4; i++) cv_rect(x + W[i][0] / h, y + W[i][1] / h, 4 / h, 4 / h, C_WHITE);
    for (i = 0; i < 12; i++) cv_rect(x + Y[i][0] / h, y + Y[i][1] / h, 4 / h, 4 / h, RGB(0xff, 0xdd, 0x44));
    cv_rect(x + 14 / h, y + 14 / h, 12 / h, 12 / h, C_WHITE);
    cv_rect(x + 16 / h, y + 16 / h, 8 / h, 8 / h, RGB(0xff, 0xdd, 0x44));
    cv_rect(x + 18 / h, y + 18 / h, 4 / h, 4 / h, C_WHITE);
}

/* ---- the keys' side (D16): the register on the white keys, the steering on the black ones */
enum { RS_LAYER_ACTION, RS_LAYER_MODIFIER, RS_LAYER_CHANCE, RS_NLAYERS };   /* what the white keys turn (SELECT) */
struct rs_ui {
    uint8_t sel, bank, swapped, layer;          /* the bot on the white keys, the 16-step page shown, which part of a step */
    uint8_t len[2];                             /* each register's length */
    int16_t playing_at[2];                      /* the step of each register playing now (-1: none) */
    uint8_t steer[2];                           /* a bot's next move, by hand (action + 1) */
    struct rs_step steps[2][16];                /* the page of each */
};

static const uint16_t ACT_COLOUR[RS_NACTIONS] = {   /* renderStepCard's actionColorMap */
    C_PULSE, RGB(0x73, 0xb7, 0xa9), C_ROT, C_BELT, C_DANGER, RGB(0x59, 0xd9, 0xc2), C_MUTED};

static const uint16_t MOD_COLOUR[RS_NMODIFIERS] = {   /* renderStepCard's modColorMap */
    C_MUTED, C_WARN, C_BOT_B, C_ACCENT, C_DANGER};

static void icon(int32_t x, int32_t y, uint32_t i, uint16_t c)   /* a step icon, 14 x 13, in colour c, lit and outlined */
{
    int32_t r, k;
    for (r = 0; r < 13; r++)
        for (k = 0; k < 14; k++) {
            char ch = ICON[i][r][k];
            if (ch != '.') cv_pset(x + k, y + r, ch == 'o' ? RGB(0x08, 0x08, 0x10) : ch == 'h' ? mix565(C_WHITE, c, 115) : c);
        }
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
            uint32_t layer = on ? u->layer : RS_LAYER_ACTION;   /* (the other register always shows its actions) */
            uint16_t c = layer == RS_LAYER_MODIFIER ? MOD_COLOUR[st->modifier % RS_NMODIFIERS] : ACT_COLOUR[st->action];
            if (layer == RS_LAYER_ACTION && st->prob != 100) c = mix565(c, cell, 140);
            if (!on) c = mix565(c, cell, 110);
            cv_rect(x0 + 1, y, 18, 14, cell);
            if (layer == RS_LAYER_MODIFIER) icon(x0 + 3, y + 1, 8u + st->modifier % RS_NMODIFIERS, c);
            else if (layer == RS_LAYER_CHANCE) {        /* the chance as bars: three 100 %, two 75 %, one 50 % */
                uint32_t n = st->prob >= 100 ? 3u : st->prob >= 75 ? 2u : 1u, k2;
                uint16_t pc = n == 3u ? C_ACCENT : n == 2u ? C_WARN : C_DANGER;
                for (k2 = 0; k2 < 3u; k2++) {
                    int32_t h = 4 + 3 * (int32_t)k2;
                    cv_rect(x0 + 4 + 4 * (int32_t)k2, y + 13 - h, 3, h, k2 < n ? pc : mix565(C_MUTED, cell, 60));
                }
            } else
                icon(x0 + 3, y + 1, st->action, c);
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
    static const char *const LAYER[RS_NLAYERS] = {"ACTION", "MODIFIER", "CHANCE"};
    char top[32] = "STEP 00/00";
    cv_begin(240, 240, C_BG);
    draw_strip(u, 0), draw_strip(u, 1);
    put_num(top + 5, (uint32_t)(v->step < 0 ? 0 : v->step + 1), 2);   /* the loop's step, the cycle */
    put_num(top + 8, v->loop_len, 2);
    cv_text(24, 2, &FONT_S, top, v->playing ? C_TEXT : C_MUTED);
    cv_text(216 - text_w(&FONT_S, LAYER[u->layer % RS_NLAYERS]), 2, &FONT_S, LAYER[u->layer % RS_NLAYERS],
            u->sel ? C_BOT_B : C_BOT_A);       /* what the white keys turn, in the colour of whose */
    /* the board */
    for (i = 0; i < RS_ROWS * RS_COLS; i++) {
        int32_t x = BX + (i % RS_COLS) * CELL, y = BY + (i / RS_COLS) * CELL;
        draw_tile(x, y, &v->board.t[i]);
        if (fx->lit >> i & 1u && (int32_t)(fx->lit_until - now_ms) > 0) frame(x, y, CELL, CELL, 2, C_WHITE);
    }
    for (b = 0; b < 2; b++) {                   /* the launch pads' marks (the HTML's trails never grow past them) */
        int32_t x = BX + START[b].x * CELL, y = BY + START[b].y * CELL;
        if (v->bot[b].x == START[b].x && v->bot[b].y == START[b].y) continue;
        cv_rect(b ? x + 33 : x + 4, y + 33, 3, 3, mix565(b ? C_BOT_B : C_BOT_A, C_CELL, 64));
    }
    {                                           /* the bots: each always on its own side of a tile, Scout-7 left,
                                                   Echo-3 right, so two on one tile is just both there */
        int hid[2] = {(int32_t)(fx->hidden_until[0] - now_ms) > 0, (int32_t)(fx->hidden_until[1] - now_ms) > 0};
        int together = v->bot[0].x == v->bot[1].x && v->bot[0].y == v->bot[1].y && !hid[0] && !hid[1];
        for (b = 0; b < 2; b++) {
            const struct rs_bot *t = &v->bot[b];
            int32_t x = BX + t->x * CELL, y = BY + t->y * CELL, sx = x + (b ? 20 : 0);   /* its half */
            if (hid[b]) continue;
            if (b == 0 || !together) dim_cell(x, y);
            draw_bot(sx + 1, y + 20, b, 1, C_CELL);
            draw_arrow_at(sx + 10, y + 17, t->dir, b ? C_WARN : C_ACCENT);   /* its facing, over its head */
            if ((int32_t)(fx->clash_until[b] - now_ms) > 0) draw_clash(sx, y + 1, 2);   /* hit: the star over it */
        }
        for (b = 0; b < 2; b++)
            if ((int32_t)(fx->boom_until[b] - now_ms) > 0)   /* where it burned out, on its side */
                draw_explosion(BX + fx->boom_x[b] * CELL + (b ? 10 : -10), BY + fx->boom_y[b] * CELL, b);
        if ((int32_t)(fx->heart_until - now_ms) > 0)   /* in love: a heart over the two */
            draw_heart(BX + fx->heart_x * CELL + 13, BY + fx->heart_y * CELL + 4, 1, 0);
    }
    for (b = 0; b < 2; b++) {
        int32_t x = b ? 122 : 24, hp = 6 - (v->bot[b].heat > 6 ? 6 : v->bot[b].heat < 0 ? 0 : v->bot[b].heat);
        uint16_t c = hp <= 2 ? C_DANGER : hp <= 4 ? C_WARN : C_PULSE;
        int32_t w = cv_text(x, 222, &FONT_S, b ? "ECHO" : "SCOUT", b ? C_BOT_B : C_BOT_A) + 3;
        cv_rect(w, 228, x + 82 - w, 6, RGB(0x1a, 0x1a, 0x2e));
        cv_rect(w, 228, (x + 82 - w) * hp / 6, 6, c);
    }
    if (u->swapped) icon(107, 223, RS_NACTIONS, C_TEXT);        /* the registers swapped */
    screen_out();
}

/* ---- SEQ: the sequence, both registers as one list (the HTML's planner, the step cards side by side), 16 rows of
 * 14 px on the screen and the rest a smooth scroll away. Scout-7's steps left, Echo-3's right, the step's number
 * between: each step its action, its modifier and its chance; the step each register plays framed; a bar along
 * the left edge marks the rows on the white keys. */
#define SEQ_TOP 16
#define SEQ_ROW 14

static void digits(int32_t x, int32_t y, uint32_t v, uint32_t w, uint16_t c)   /* v in w digits of 5 x 7, 6 px apart */
{
    while (w--) {
        uint32_t d = v % 10u, r, k;
        for (r = 0; r < 7; r++)
            for (k = 0; k < 5; k++)
                if (DIGIT[d][r][k] == '#') cv_pset(x + 6 * (int32_t)w + (int32_t)k, y + (int32_t)r, c);
        v /= 10u;
    }
}

static void chance_bars(int32_t x, int32_t y, uint8_t prob, uint16_t under, int on)   /* one to three bars, 12 x 10 */
{
    uint32_t n = prob >= 100 ? 3u : prob >= 75 ? 2u : 1u, k;
    uint16_t c = n == 3u ? C_ACCENT : n == 2u ? C_WARN : C_DANGER;
    if (!on) c = mix565(c, under, 110);
    for (k = 0; k < 3u; k++) {
        int32_t h = 4 + 3 * (int32_t)k;
        cv_rect(x + 4 * (int32_t)k, y + 10 - h, 3, h, k < n ? c : mix565(C_MUTED, under, 50));
    }
}

static void draw_seq(const struct rs_view *v, const struct rs_ui *u, const struct rs_state *g, int32_t scroll,
                     uint32_t keys_top)
{
    static const char *const LAYER[RS_NLAYERS] = {"ACTION", "MODIFIER", "CHANCE"};
    uint16_t cell = RGB(0x1a, 0x1a, 0x2e);
    int32_t rows = g->loop_len, first = scroll / SEQ_ROW, off = scroll % SEQ_ROW, r, b;
    cv_begin(240, 240, C_BG);
    for (r = 0; r <= 16; r++) {
        int32_t i = first + r, y = SEQ_TOP + r * SEQ_ROW - off;
        if (i >= rows) break;
        uint16_t num = v->playing && v->step == i ? C_TEXT : mix565(C_MUTED, C_BG, 150);
        digits(108, y + 3, (uint32_t)i + 1u, 2, num);
        for (b = 0; b < 2; b++) {
            int32_t len = g->prog[b].len ? g->prog[b].len : g->loop_len, x = b ? 132 : 4, on = u->sel == b;
            const struct rs_step *st = &g->prog[b].s[i & 63];
            if (i >= len) { cv_rect(x + 50, y + 6, 2, 2, cell); continue; }   /* past its length */
            cv_rect(x, y, 104, SEQ_ROW - 1, cell);
            cv_rect(b ? x + 102 : x, y, 2, SEQ_ROW - 1, mix565(ACT_COLOUR[st->action], cell, on ? 200 : 90));   /* its colour at the edge */
            icon(x + 6, y, st->action, on ? ACT_COLOUR[st->action] : mix565(ACT_COLOUR[st->action], cell, 110));
            icon(x + 24, y, 8u + st->modifier % RS_NMODIFIERS,
                 on ? MOD_COLOUR[st->modifier % RS_NMODIFIERS] : mix565(MOD_COLOUR[st->modifier % RS_NMODIFIERS], cell, 110));
            chance_bars(x + 44, y + 2, st->prob, cell, on);
            if (on && u->layer != RS_LAYER_ACTION)  /* what the white keys turn: underlined */
                cv_rect(x + (u->layer == RS_LAYER_MODIFIER ? 24 : 42), y + SEQ_ROW - 2, 15, 1, C_WHITE);
            else if (on)
                cv_rect(x + 6, y + SEQ_ROW - 2, 14, 1, C_WHITE);
            if (v->playing && v->step >= 0 && v->step % len == i)   /* the step it plays */
                frame(x, y, 104, SEQ_ROW - 1, 1, on ? C_WHITE : C_MUTED);
        }
        if ((uint32_t)i >= keys_top && (uint32_t)i < keys_top + 16u)   /* on the white keys */
            cv_rect(0, y, 2, SEQ_ROW - 1, u->sel ? C_BOT_B : C_BOT_A);
    }
    if (rows > 16) {                            /* where in the list: a thumb at the right edge */
        int32_t h = 224 * 16 / rows, y = SEQ_TOP + (224 - h) * scroll / ((rows - 16) * SEQ_ROW);
        cv_rect(238, y, 2, h, C_MUTED);
    }
    cv_rect(0, 0, 240, SEQ_TOP, C_BG);          /* the header, over what scrolled under it */
    cv_text(4, 0, &FONT_S, "SCOUT", u->sel ? mix565(C_BOT_A, C_BG, 120) : C_BOT_A);
    cv_text(236 - text_w(&FONT_S, "ECHO"), 0, &FONT_S, "ECHO", u->sel ? C_BOT_B : mix565(C_BOT_B, C_BG, 120));
    cv_text(120 - text_w(&FONT_S, LAYER[u->layer % RS_NLAYERS]) / 2, 0, &FONT_S, LAYER[u->layer % RS_NLAYERS], C_TEXT);
    cv_rect(0, SEQ_TOP - 1, 240, 1, C_CELL_LINE);
    screen_out();
}

/* ---- GLO: the global settings, a knob each (the HTML's transport bar): the tempo, how often the bots move, the
 * loop's length (it changes when the loop comes round: D20), the speed (1/1, 1/2, 1/4: D22) */
static void knob_tag(int32_t x, int32_t y, uint32_t n)   /* "K" n: which knob */
{
    frame(x, y, 15, 11, 1, C_MUTED);
    digits(x + 5, y + 2, n, 1, C_TEXT);
}

static void draw_glo(const struct rs_state *g, uint8_t pending_loop)
{
    char num[8];
    int32_t r;
    static const char *const LABEL[4] = {"TEMPO", "BOTS MOVE EVERY", "LOOP", "SPEED"};
    cv_begin(240, 240, C_BG);
    cv_text(120 - text_w(&FONT_S, "GLOBAL") / 2, 0, &FONT_S, "GLOBAL", C_TEXT);
    cv_rect(0, SEQ_TOP - 1, 240, 1, C_CELL_LINE);
    for (r = 0; r < 4; r++) {
        int32_t y = 20 + r * 55, x;
        const char *unit = "";
        uint16_t c = C_TEXT;
        knob_tag(6, y + 4, (uint32_t)r + 1u);
        cv_text(28, y + 1, &FONT_S, LABEL[r], C_MUTED);
        switch (r) {
        case 0: num[0] = (char)('0' + g->bpm / 100u % 10u), num[1] = (char)('0' + g->bpm / 10u % 10u);
                num[2] = (char)('0' + g->bpm % 10u), num[3] = 0;
                if (g->bpm < 100u) num[0] = num[1], num[1] = num[2], num[2] = 0;
                unit = "BPM"; break;
        case 1: num[0] = (char)('0' + (g->tpm ? g->tpm : 1)), num[1] = 0; unit = g->tpm > 1 ? "TICKS" : "TICK"; break;
        case 2: num[0] = (char)('0' + g->loop_len / 10u), num[1] = (char)('0' + g->loop_len % 10u), num[2] = 0;
                if (g->loop_len < 10u) num[0] = num[1], num[1] = 0;
                unit = "STEPS"; break;
        default: num[0] = '1', num[1] = '/', num[2] = g->quarter == 1 ? '4' : g->quarter == 2 ? '2' : '1', num[3] = 0;
                 c = g->quarter ? C_WARN : C_TEXT; break;
        }
        x = cv_text(28, y + 18, &FONT_L, num, c);
        cv_text(x + 6, y + 32, &FONT_S, unit, C_MUTED);
        if (r == 2 && pending_loop && pending_loop != g->loop_len) {   /* the next length, when the loop comes round */
            char p[12] = "> 00";
            p[2] = (char)('0' + pending_loop / 10u), p[3] = (char)('0' + pending_loop % 10u);
            cv_text(150, y + 24, &FONT_L, p, C_WARN);
        }
        cv_rect(0, y + 53, 240, 1, C_CELL_LINE);
    }
    screen_out();
}

/* ---- SCL: Voice Mod (the HTML's Seq 1): its four steps as cards, each a scale, a root and a semitone; the step it
 * is on lit, the one the knobs edit framed; when it moves on below; the white keys' words at the foot */
static void key_labels(const char *const *w, uint32_t n, uint32_t first)   /* the white keys' words: n from key first */
{
    uint32_t k;
    int32_t cw = 240 / (int32_t)n;
    for (k = 0; k < n; k++) {
        int32_t x = (int32_t)k * cw;
        cv_rect(x + 2, 214, cw - 4, 24, RGB(0x1a, 0x1a, 0x2e));
        digits(x + 4, 216, first + k, first + k > 9u ? 2u : 1u, C_MUTED);
        cv_text(x + cw / 2 - text_w(&FONT_S, w[k]) / 2, 222, &FONT_S, w[k], C_TEXT);
    }
}

static void draw_scl(const struct rs_state *g, uint32_t cursor)
{
    static const char *const SCALE[RS_NSCALES] = {"CHROM", "MAJOR", "MINOR", "DORIA", "PHRYG", "LYDIA", "MIXO",
                                                  "PENTA", "MPENT", "DIM", "WHOLE", "AUG"};
    static const char *const ROOT[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char *const KEYS[4] = {"NEXT", "ROOT", "RST", "RND"};
    static const char *const FREQ[6] = {"MANUAL", "1/4 LOOP", "1/2 LOOP", "1 LOOP", "2 LOOPS", "4 LOOPS"};
    uint32_t n = g->bassmod_len ? g->bassmod_len : 4, i, f;
    uint16_t cell = RGB(0x1a, 0x1a, 0x2e);
    char st[8];
    cv_begin(240, 240, C_BG);
    cv_text(120 - text_w(&FONT_S, "VOICE MOD") / 2, 0, &FONT_S, "VOICE MOD", C_TEXT);
    cv_rect(0, SEQ_TOP - 1, 240, 1, C_CELL_LINE);
    for (i = 0; i < 4u && i < n; i++) {         /* (this HTML's Seq 1 has four) */
        const struct rs_bassmod_step *b = &g->bassmod[i];
        int32_t x = (int32_t)i * 60, on = i == g->vm_index % n, s = b->semitone;
        cv_rect(x + 2, 20, 56, 150, on ? mix565(C_ACCENT, cell, 60) : cell);
        if (i == cursor) frame(x + 2, 20, 56, 150, 2, C_WHITE);
        digits(x + 27, 25, i + 1u, 1, on ? C_ACCENT : C_MUTED);
        cv_text(x + 30 - text_w(&FONT_S, SCALE[b->scale % RS_NSCALES]) / 2, 44, &FONT_S, SCALE[b->scale % RS_NSCALES], C_TEXT);
        cv_text(x + 30 - text_w(&FONT_L, ROOT[b->root % 12u]) / 2, 74, &FONT_L, ROOT[b->root % 12u], on ? C_ACCENT : C_TEXT);
        st[0] = s > 0 ? '+' : s < 0 ? '-' : ' ', s = s < 0 ? -s : s;
        st[1] = (char)(s >= 10 ? '1' : '0' + s), st[2] = s >= 10 ? (char)('0' + s - 10) : 0, st[3] = 0;
        if (st[0] == ' ') st[0] = '0', st[1] = 0;
        cv_text(x + 30 - text_w(&FONT_S, st) / 2, 124, &FONT_S, st, C_MUTED);
        cv_text(x + 30 - text_w(&FONT_S, "SEMI") / 2, 144, &FONT_S, "SEMI", mix565(C_MUTED, cell, 120));
    }
    f = g->vm_manual ? 0u : g->vm_freq4 >= 16 ? 5u : g->vm_freq4 >= 8 ? 4u : g->vm_freq4 >= 4 ? 3u : g->vm_freq4 >= 2 ? 2u : 1u;
    knob_tag(4, 177, 4);
    cv_text(24, 175, &FONT_S, FREQ[f], g->vm_manual ? C_WARN : C_TEXT);
    cv_text(236 - text_w(&FONT_S, g->transpose_root ? "ROOT ON" : "ROOT OFF"), 175, &FONT_S,
            g->transpose_root ? "ROOT ON" : "ROOT OFF", g->transpose_root ? C_ACCENT : C_MUTED);
    cv_text(4, 195, &FONT_S, "K1 SCALE K2 ROOT K3 SEMI", mix565(C_MUTED, C_BG, 160));
    key_labels(KEYS, 4, 13);
    screen_out();
}

/* ---- ARP: Seq 2 (the HTML's), its four steps as cards: a board, a drum pattern, the registers swapped; on, queued
 * or off over them; the step it is on lit, the one the knobs edit framed */
static void draw_arp(const struct rs_state *g, uint32_t cursor)
{
    static const char *const KEYS[5] = {"ON", "RNDA", "RNDB", "RST", "RND"};
    static const char *const BOARD[RS_NBOARDS + 2] = {"DEFLT", "CORR", "GARDN", "GAUNT", "SPIN", "MAZE", "RAND", "USER"};
    static const char *const DRUM[RS_NDRUMS] = {"FOUR", "BREAK", "HALF", "OFFBT", "TRIPL", "MINI"};
    uint16_t cell = RGB(0x1a, 0x1a, 0x2e);
    const char *state = g->seq2_pending ? "QUEUED" : g->seq2_active ? "ON" : "OFF";
    uint32_t i;
    cv_begin(240, 240, C_BG);
    cv_text(4, 0, &FONT_S, "SEQ 2", C_TEXT);
    cv_text(236 - text_w(&FONT_S, state), 0, &FONT_S, state, g->seq2_pending ? C_WARN : g->seq2_active ? C_ACCENT : C_MUTED);
    cv_rect(0, SEQ_TOP - 1, 240, 1, C_CELL_LINE);
    for (i = 0; i < RS_SEQ2_STEPS; i++) {
        const struct rs_seq2_step *st = &g->seq2[i];
        int32_t x = (int32_t)i * 60, on = g->seq2_active && g->seq2_index == (int8_t)i;
        const char *bn = st->board == RS_B_NONE ? "--" : BOARD[st->board < RS_NBOARDS + 2 ? st->board : 0];
        const char *dn = st->drums == RS_D_NONE ? "--" : DRUM[st->drums < RS_NDRUMS ? st->drums : 0];
        cv_rect(x + 2, 20, 56, 150, on ? mix565(C_ACCENT, cell, 60) : cell);
        if (i == cursor) frame(x + 2, 20, 56, 150, 2, C_WHITE);
        digits(x + 27, 25, i + 1u, 1, on ? C_ACCENT : C_MUTED);
        cv_text(x + 30 - text_w(&FONT_S, "BOARD") / 2, 44, &FONT_S, "BOARD", mix565(C_MUTED, cell, 120));
        cv_text(x + 30 - text_w(&FONT_S, bn) / 2, 62, &FONT_S, bn, st->board == RS_B_NONE ? C_MUTED : C_TEXT);
        cv_text(x + 30 - text_w(&FONT_S, "DRUMS") / 2, 92, &FONT_S, "DRUMS", mix565(C_MUTED, cell, 120));
        cv_text(x + 30 - text_w(&FONT_S, dn) / 2, 110, &FONT_S, dn, st->drums == RS_D_NONE ? C_MUTED : C_TEXT);
        if (st->regswap) icon(x + 23, 142, RS_NACTIONS, C_TEXT);   /* the registers swapped */
        else cv_rect(x + 28, 148, 4, 2, mix565(C_MUTED, cell, 120));
    }
    cv_text(4, 176, &FONT_S, "K1 BOARD K2 DRUMS K3 SWAP", mix565(C_MUTED, C_BG, 160));
    cv_text(4, 194, &FONT_S, "ROLL ON WRAP:", mix565(C_MUTED, C_BG, 160));
    cv_text(124, 194, &FONT_S, "A", g->seq2_rnd[0] ? C_BOT_A : mix565(C_BOT_A, C_BG, 70));
    cv_text(144, 194, &FONT_S, "B", g->seq2_rnd[1] ? C_BOT_B : mix565(C_BOT_B, C_BG, 70));
    key_labels(KEYS, 5, 12);
    screen_out();
}

/* ---- LFO: the drum machine (the HTML's): kick, snare and hihat over the drum steps (16, or 12 in a 12-step loop),
 * the row on the white keys marked, the step playing framed; on / off and the pattern over them */
static void draw_drums(const struct rs_view *v, const struct rs_state *g, uint32_t row)
{
    static const char *const NAME[3] = {"KICK", "SNARE", "HIHAT"};
    static const char *const PRESET[RS_NDRUMS] = {"FOUR", "BREAK", "HALF", "OFFBEAT", "TRIPLET", "MINIMAL"};
    const uint8_t *pat[3] = {g->drum_k, g->drum_s, g->drum_h};
    const uint16_t col[3] = {C_DANGER, C_WARN, C_ACCENT};
    uint16_t cell = RGB(0x1a, 0x1a, 0x2e);
    uint32_t n = rs_drum_steps(g->loop_len), r, i;
    const char *pn = g->drum_preset < RS_NDRUMS ? PRESET[g->drum_preset] : "CUSTOM";
    cv_begin(240, 240, C_BG);
    cv_text(4, 0, &FONT_S, "DRUMS", C_TEXT);
    cv_text(120 - text_w(&FONT_S, pn) / 2, 0, &FONT_S, pn, C_MUTED);
    cv_text(236 - text_w(&FONT_S, g->drums_on ? "ON" : "OFF"), 0, &FONT_S, g->drums_on ? "ON" : "OFF", g->drums_on ? C_ACCENT : C_MUTED);
    cv_rect(0, SEQ_TOP - 1, 240, 1, C_CELL_LINE);
    for (r = 0; r < 3u; r++) {
        int32_t y = 22 + (int32_t)r * 58;
        cv_text(4, y, &FONT_S, NAME[r], r == row ? col[r] : C_MUTED);
        if (r == row) cv_rect(0, y + 18, 240, 1, col[r]);       /* on the white keys */
        for (i = 0; i < 16u; i++) {
            int32_t x = (int32_t)i * 15;
            if (i >= n) { cv_rect(x + 6, y + 34, 2, 2, cell); continue; }
            cv_rect(x + 1, y + 22, 13, 30, pat[r][i] ? (g->drums_on ? col[r] : mix565(col[r], cell, 110)) : cell);
            if (i % 4u == 0) cv_rect(x + 1, y + 53, 13, 1, C_CELL_LINE);   /* the beats */
            if (v->playing && i == v->drum_tick % n) frame(x + 1, y + 22, 13, 30, 1, C_WHITE);
        }
    }
    cv_text(4, 198, &FONT_S, "SELECT ROW K1 PATTERN", mix565(C_MUTED, C_BG, 160));
    cv_text(4, 218, &FONT_S, "K4 ON/OFF", mix565(C_MUTED, C_BG, 160));
    screen_out();
}

/* ---- FX: the mix (the HTML's MIX panel): master, the five buses and the lead as faders, the one on KNOB1 framed */
static void draw_mix(const volatile int32_t *level, uint32_t cursor, uint32_t colour)
{
    static const char *const NAME[RS_NLEVEL] = {"MAST", "MEL", "BOTB", "PERC", "FX", "DRUM", "LEAD"};
    uint16_t cell = RGB(0x1a, 0x1a, 0x2e);
    uint32_t k;
    cv_begin(240, 240, C_BG);
    cv_text(120 - text_w(&FONT_S, "MIX") / 2, 0, &FONT_S, "MIX", C_TEXT);
    cv_rect(0, SEQ_TOP - 1, 240, 1, C_CELL_LINE);
    for (k = 0; k < RS_NLEVEL; k++) {
        int32_t x = (int32_t)k * 34 + 1, pct = (level[k] * 100 + 16384) >> 15, h = pct * 128 / 100;
        uint16_t c = k == 0 ? C_TEXT : k == 1 ? C_BOT_A : k == 2 ? C_BOT_B : k == 3 ? C_PULSE : k == 4 ? C_BELT : k == 5 ? C_DANGER : C_ACCENT;
        cv_rect(x + 10, 52, 14, 128, cell);
        cv_rect(x + 10, 180 - h, 14, h, k == cursor ? c : mix565(c, cell, 140));
        if (k == cursor) frame(x + 8, 50, 18, 132, 1, C_WHITE);
        digits(x + 17 - (pct >= 100 ? 9 : pct >= 10 ? 6 : 3), 186, (uint32_t)pct, pct >= 100 ? 3u : pct >= 10 ? 2u : 1u, C_TEXT);
        cv_text(x + 17 - text_w(&FONT_S, NAME[k]) / 2, k & 1u ? 33 : 17, &FONT_S, NAME[k], k == cursor ? C_TEXT : C_MUTED);   /* (staggered) */
    }
    {
        char t[24] = "K2 BOARD COLOUR    %";
        uint32_t c = colour > 100u ? 100u : colour;
        t[16] = c >= 100u ? '1' : ' ', t[17] = c >= 10u ? (char)('0' + c / 10u % 10u) : ' ', t[18] = (char)('0' + c % 10u);
        cv_text(4, 200, &FONT_S, "KEYS 1-7 PICK  K1 LEVEL", mix565(C_MUTED, C_BG, 160));
        cv_text(4, 222, &FONT_S, t, colour ? C_ACCENT : C_MUTED);
    }
    screen_out();
}

/* ---- ENV: the boards (the HTML's board panel): the board now and the one queued for the next loop, small; the
 * presets, a reroll, the user board and its saving and clearing, the bots back to their pads, on the white keys */
static void mini_board(int32_t x, int32_t y, const struct rs_board *bd)   /* 5 x 5 of 16 px */
{
    int32_t i;
    for (i = 0; i < RS_ROWS * RS_COLS; i++) {
        const struct rs_tile_t *t = &bd->t[i];
        int32_t cx = x + (i % RS_COLS) * 16, cy = y + (i / RS_COLS) * 16;
        uint16_t c = t->type == RS_T_BELT ? C_BELT : t->type == RS_T_ROTL || t->type == RS_T_ROTR ? C_ROT :
                     t->type == RS_T_ZAP ? C_ZAP : t->type == RS_T_PULSE ? C_PULSE : t->type == RS_T_BLOCK ? RGB(0x8b, 0x69, 0x39) : C_CELL;
        cv_rect(cx, cy, 15, 15, t->type == RS_T_NONE ? C_CELL : mix565(c, C_CELL, 150));
        if (t->type == RS_T_BELT) {             /* the way it runs: a stub from the middle */
            int32_t dx = t->dir == RS_E ? 1 : t->dir == RS_W ? -1 : 0, dy = t->dir == RS_S ? 1 : t->dir == RS_N ? -1 : 0, k;
            for (k = 0; k < 6; k++) cv_rect(cx + 6 + dx * k, cy + 6 + dy * k, 3, 3, C_WHITE);
        }
    }
}

static void draw_boards(const struct rs_state *g)
{
    static const char *const KEY[11] = {"DFLT", "CORR", "GARD", "GAUN", "SPIN", "MAZE", "ROLL", "USER", "SAVE", "CLR", "RSET"};
    static const char *const DENS[3] = {"SPARSE", "BALANCED", "CHAOS"};
    uint16_t cell = RGB(0x1a, 0x1a, 0x2e);
    uint32_t k, dens = g->density >= 60 ? 2u : g->density >= 45 ? 1u : 0u;
    cv_begin(240, 240, C_BG);
    cv_text(4, 0, &FONT_S, "BOARDS", C_TEXT);
    cv_rect(0, SEQ_TOP - 1, 240, 1, C_CELL_LINE);
    mini_board(8, 22, &g->board);
    if (g->pending_board != RS_B_NONE) {        /* queued: it comes with the next loop */
        cv_text(96, 44, &FONT_S, ">", C_WARN);
        mini_board(112, 22, &g->pending_tiles);
        frame(110, 20, 84, 84, 1, C_WARN);
    }
    knob_tag(200, 24, 1);
    cv_text(236 - text_w(&FONT_S, DENS[dens]), 40, &FONT_S, DENS[dens], C_MUTED);
    for (k = 0; k < 11u; k++) {
        int32_t x = (int32_t)(k % 6u) * 40, y = 112 + (int32_t)(k / 6u) * 60;
        uint32_t id = k < 6u ? k : k == 6u ? RS_B_RANDOM : k == 7u ? RS_B_USER : 0xFFu;
        int on = id != 0xFFu && g->board_id == id, queued = id != 0xFFu && g->pending_board == id;
        cv_rect(x + 2, y, 36, 52, queued ? mix565(C_WARN, cell, 70) : on ? mix565(C_ACCENT, cell, 70) : cell);
        if (on) frame(x + 2, y, 36, 52, 1, C_ACCENT);
        digits(x + 5, y + 4, k + 1u, k + 1u > 9u ? 2u : 1u, C_MUTED);
        cv_text(x + 20 - text_w(&FONT_S, KEY[k]) / 2, y + 26, &FONT_S, KEY[k], k >= 8u ? C_WARN : C_TEXT);
    }
    screen_out();
}

static void screen_out(void)                    /* the lead's badge over any page, then the canvas to the screen */
{
    if (lead_badge > -99) {
        char t[8] = "LEAD  ";
        t[4] = lead_badge < 0 ? '-' : lead_badge > 0 ? '+' : ' ';
        t[5] = (char)('0' + (lead_badge < 0 ? -lead_badge : lead_badge));
        if (!lead_badge) t[4] = 0;
        cv_rect(178, 0, 62, 15, C_ACCENT);
        cv_text(182, 0, &FONT_S, t, RGB(0x05, 0x05, 0x0c));
    }
    cv_blit(0, 0);
}
