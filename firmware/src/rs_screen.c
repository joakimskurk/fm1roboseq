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

static void draw_heart(int32_t x, int32_t y, int32_t px)   /* px: 4 a tile's own, 2 small over two bots */
{
    static const char *const ROWS[8] = {"  ##  ## ", " ########", "#########", "#########", " ####### ", "  #####  ",
                                        "   ###   ", "    #    "};
    uint32_t r, k;
    for (r = 0; r < 8; r++)
        for (k = 0; k < 9; k++)
            if (ROWS[r][k] == '#') cv_rect(x + px * (int32_t)k, y + px * (int32_t)r, px, px, r < 2 ? RGB(0xff, 0x8e, 0xc4) : RGB(0xff, 0x4d, 0xa6));
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

#include "rs_icons.h"                         /* the step icons (tools/art/icons.py) */

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
            draw_heart(BX + fx->heart_x * CELL + 11, BY + fx->heart_y * CELL + 2, 2);
    }
    for (b = 0; b < 2; b++) {
        int32_t x = b ? 122 : 24, hp = 6 - (v->bot[b].heat > 6 ? 6 : v->bot[b].heat < 0 ? 0 : v->bot[b].heat);
        uint16_t c = hp <= 2 ? C_DANGER : hp <= 4 ? C_WARN : C_PULSE;
        int32_t w = cv_text(x, 222, &FONT_S, b ? "ECHO" : "SCOUT", b ? C_BOT_B : C_BOT_A) + 3;
        cv_rect(w, 228, x + 82 - w, 6, RGB(0x1a, 0x1a, 0x2e));
        cv_rect(w, 228, (x + 82 - w) * hp / 6, 6, c);
    }
    if (u->swapped) icon(107, 223, RS_NACTIONS, C_TEXT);        /* the registers swapped */
    cv_blit(0, 0);
}
