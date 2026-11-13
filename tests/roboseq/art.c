/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Joakim (joakimskurk) */
/* The README's art, drawn by the firmware's own code (rs_screen.c, rs_splash.c) and written as SVG, every pixel kept
 * (a path per colour, crisp at any size): the two robots on cards, the tiles, the moves and the modifiers as the
 * screen's chips, the steering in each robot's colour, a strip of the board between the README's parts, and the
 * battle's two face to face. make screens writes them into assets/readme/. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    for (uint32_t j = 0; j < h; j++)
        for (uint32_t i = 0; i < w; i++) fb[(y + j) * 240 + x + i] = (uint16_t)(p[j * w + i] >> 8 | p[j * w + i] << 8);
}
#define CV_MAX (240u * 240u)
#include "gfx.c"
#include "rs_model.c"
#include "rs_chance.c"
#include "rs_score.c"
#include "rs_voice.c"
#include "rs_mix.h"
#include "rs_screen.c"
#include "rs_splash.c"

static void svg(const char *name, const char *title)   /* the canvas as assets/readme/<name>.svg, 4x its pixels */
{
    static uint16_t cols[1024];
    uint32_t nc = 0, i, k, x, y;
    char path[128];
    snprintf(path, sizeof path, "assets/readme/%s.svg", name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    for (i = 0; i < cv_w * cv_h; i++) {          /* the colours, as they first appear */
        uint16_t c = swap16(cv_px[i]);
        for (k = 0; k < nc && cols[k] != c; k++) {}
        if (k == nc && nc < 1024u) cols[nc++] = c;
    }
    fprintf(f, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %u %u\" width=\"%u\" height=\"%u\" "
               "shape-rendering=\"crispEdges\"><title>%s</title>\n", cv_w, cv_h, cv_w * 4, cv_h * 4, title);
    for (k = 0; k < nc; k++) {
        uint32_t r = (cols[k] >> 11) & 31u, g = (cols[k] >> 5) & 63u, b = cols[k] & 31u;
        fprintf(f, "<path fill=\"#%02x%02x%02x\" d=\"", (r * 255u + 15u) / 31u, (g * 255u + 31u) / 63u, (b * 255u + 15u) / 31u);
        for (y = 0; y < cv_h; y++)
            for (x = 0; x < cv_w;) {
                uint32_t n = 0;
                while (x + n < cv_w && swap16(cv_px[y * cv_w + x + n]) == cols[k]) n++;
                if (n) fprintf(f, "M%u %uh%uv1h-%uz", x, y, n, n), x += n;
                else x++;
            }
        fprintf(f, "\"/>\n");
    }
    fprintf(f, "</svg>\n");
    fclose(f);
    printf("art    -> %s\n", path);
}

#define S_BG RGB(0x0b, 0x23, 0x24)               /* the splash's ground, grid and line */
#define S_GRID RGB(0x10, 0x2f, 0x2f)
#define S_LINE RGB(0x2f, 0x6a, 0x63)

static void ground(int32_t w, int32_t h, uint16_t corner_l, uint16_t corner_r)   /* a card as the splash: grid, frame, corners */
{
    int32_t i;
    cv_begin((uint32_t)w, (uint32_t)h, S_BG);
    for (i = 4; i < w - 2; i += 6) cv_rect(i, 3, 1, h - 6, S_GRID);
    for (i = 4; i < h - 2; i += 6) cv_rect(3, i, w - 6, 1, S_GRID);
    frame(2, 2, w - 4, h - 4, 1, S_LINE);
    cv_rect(0, 0, 6, 1, corner_l), cv_rect(0, 0, 1, 6, corner_l), cv_rect(0, h - 1, 6, 1, corner_l), cv_rect(0, h - 6, 1, 6, corner_l);
    cv_rect(w - 6, 0, 6, 1, corner_r), cv_rect(w - 1, 0, 1, 6, corner_r), cv_rect(w - 6, h - 1, 6, 1, corner_r);
    cv_rect(w - 1, h - 6, 1, 6, corner_r);
}

static void chip(uint32_t i, uint16_t c, uint16_t edge)   /* a step icon as the header's steering chip: 18 x 15, framed */
{
    cv_begin(18, 15, C_BG);
    frame(0, 0, 18, 15, 1, edge);
    icon(2, 1, i, c);
}

static void put_tile(int32_t x, int32_t y, uint8_t type, uint8_t dir)
{
    struct rs_tile_t t = {type, dir};
    draw_tile(x, y, &t);
}

static void put_bot(int32_t x, int32_t y, int b, uint32_t dir)   /* a robot on its half of a tile, as on HOME */
{
    int32_t sx = x + (b ? 20 : 0);
    dim_cell(x, y);
    draw_bot(sx + 1, y + 20, b, 1, C_CELL);
    draw_arrow_at(sx + 10, y + 17, dir, b ? C_WARN : C_ACCENT);
}

int main(void)
{
    static const char *const ACT[RS_NACTIONS] = {"move", "back", "turn-l", "turn-r", "dash", "ping", "wait"};
    static const char *const MOD[RS_NMODIFIERS] = {"steady", "bright", "low", "wobble", "glitch"};
    static const uint8_t STEER[5] = {RS_TURNL, RS_MOVE, RS_TURNR, RS_DASH, RS_PING};   /* the black keys' order */
    static const struct { const char *name, *title; uint8_t type, dir; } TILE[] = {
        {"tile-belt", "Belt", RS_T_BELT, RS_E}, {"tile-rot-l", "Rotator left", RS_T_ROTL, 0},
        {"tile-rot-r", "Rotator right", RS_T_ROTR, 0}, {"tile-zap", "Zap rail", RS_T_ZAP, 0},
        {"tile-pulse", "Pulse pad", RS_T_PULSE, 0}, {"tile-crate", "Crate", RS_T_BLOCK, 0},
        {"tile-open", "Open ground", RS_T_NONE, 0}};
    char name[64];
    uint32_t i;
    for (int b = 0; b < 2; b++) {                /* the robots, on cards of the splash's */
        ground(48, 48, b ? C_BOT_B : C_BOT_A, b ? C_BOT_B : C_BOT_A);
        cv_rect(6, 41, 36, 1, S_LINE);
        big_bot_px(8, 8, b, 1);
        svg(b ? "echo" : "scout", b ? "Echo-3" : "Scout-7");
    }
    for (i = 0; i < sizeof TILE / sizeof TILE[0]; i++) {   /* the tiles, 40 x 40 as on the board */
        cv_begin(40, 40, C_CELL);
        put_tile(0, 0, TILE[i].type, TILE[i].dir);
        svg(TILE[i].name, TILE[i].title);
    }
    for (i = 0; i < RS_NACTIONS; i++) {          /* the moves, in their colours */
        chip(i, ACT_COLOUR[i], mix565(ACT_COLOUR[i], C_BG, 96));
        snprintf(name, sizeof name, "move-%s", ACT[i]), svg(name, rs_action_name[i]);
    }
    for (i = 0; i < RS_NMODIFIERS; i++) {        /* the modifiers */
        chip(RS_NACTIONS + 1 + i, MOD_COLOUR[i], mix565(MOD_COLOUR[i], C_BG, 96));
        snprintf(name, sizeof name, "mod-%s", MOD[i]), svg(name, MOD[i]);
    }
    for (int b = 0; b < 2; b++)                  /* the steering: each robot's in its colour, as the header shows it */
        for (i = 0; i < 5; i++) {
            chip(STEER[i], b ? C_BOT_B : C_BOT_A, b ? C_BOT_B : C_BOT_A);
            snprintf(name, sizeof name, "steer-%s-%s", b ? "echo" : "scout", ACT[STEER[i]]);
            svg(name, rs_action_name[STEER[i]]);
        }
    chip(RS_NACTIONS, C_BELT, C_BELT), svg("steer-swap", "swap");
    {                                            /* the heart, as over two in love */
        cv_begin(19, 17, S_BG);
        draw_heart(2, 2, 1, 0);
        svg("heart", "love");
    }
    {                                            /* strips of the board between the README's parts, a story in three:
                                                    the two set off, they meet in love, they clash */
        static const uint8_t ROW[3][8][2] = {
            {{RS_T_BELT, RS_E}, {RS_T_ZAP, 0}, {RS_T_NONE, 0}, {RS_T_ROTL, 0}, {RS_T_PULSE, 0}, {RS_T_NONE, 0},
             {RS_T_BLOCK, 0}, {RS_T_BELT, RS_N}},
            {{RS_T_PULSE, 0}, {RS_T_BELT, RS_S}, {RS_T_ROTR, 0}, {RS_T_NONE, 0}, {RS_T_BLOCK, 0}, {RS_T_ZAP, 0},
             {RS_T_BELT, RS_W}, {RS_T_PULSE, 0}},
            {{RS_T_ROTL, 0}, {RS_T_BLOCK, 0}, {RS_T_BELT, RS_E}, {RS_T_NONE, 0}, {RS_T_NONE, 0}, {RS_T_ZAP, 0},
             {RS_T_ROTR, 0}, {RS_T_BELT, RS_S}}};
        for (int s = 0; s < 3; s++) {
            cv_begin(320, 40, C_CELL);
            for (i = 0; i < 8; i++) put_tile((int32_t)i * 40, 0, ROW[s][i][0], ROW[s][i][1]);
            if (s == 0) put_bot(80, 0, 0, RS_E), put_bot(200, 0, 1, RS_W);
            else if (s == 1) {                   /* both on one tile, each on its half, the heart over them */
                put_bot(120, 0, 0, RS_E), draw_bot(141, 20, 1, 1, C_CELL), draw_arrow_at(150, 17, RS_W, C_WARN);
                draw_heart(133, 4, 1, 0);
            } else {                             /* Scout-7 dashes into Echo-3: the star over the one hit */
                put_bot(120, 0, 0, RS_E), put_bot(160, 0, 1, RS_W);
                draw_clash(180, 1, 2);
            }
            snprintf(name, sizeof name, "board-strip-%d", s + 1);
            svg(name, s == 0 ? "The board" : s == 1 ? "The two in love" : "A clash");
        }
    }
    {                                            /* the battle: the two face to face, the clash between them */
        ground(128, 48, C_BOT_A, C_BOT_B);
        cv_rect(6, 41, 116, 1, S_LINE);
        big_bot_px(8, 8, 0, 1), big_bot_px(88, 8, 1, 1);
        draw_clash(44, 4, 1);
        svg("battle-vs", "Scout-7 against Echo-3");
    }
    return 0;
}
