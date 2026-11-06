/* SPDX-License-Identifier: GPL-3.0-only */
/* The board's moments that a song may take a while to reach, drawn straight from a view (rs_screen.c): two bots on
 * one tile after a dash (the clash star over the one hit), in love (the heart over them), apart; the sequence page
 * of a 32-step loop half way scrolled.
 *   make scenes     -> build/scenes/clash.png, love.png, apart.png (in Docker on macOS: the pool section) */
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

static void ppm(const char *name)
{
    char path[128];
    snprintf(path, sizeof path, "build/scenes/%s.ppm", name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6 240 240 255\n");
    for (int i = 0; i < 240 * 240; i++) {
        uint16_t c = fb[i];
        unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("scene  -> %s\n", path);
}

int main(void)
{
    static struct rs_state g;
    static struct rs_view v;
    static struct rs_fx fx;
    static struct rs_ui u;
    rs_default(&g), rs_start(&g);
    v.board = g.board, v.loop_len = 16, v.step = 5, v.playing = 1, v.bot[0] = g.bot[0], v.bot[1] = g.bot[1];
    u.len[0] = u.len[1] = 16, u.playing_at[0] = u.playing_at[1] = 5;
    for (int k = 0; k < 16; k++) u.steps[0][k] = g.prog[0].s[k], u.steps[1][k] = g.prog[1].s[k];
    v.bot[0].x = v.bot[1].x = 2, v.bot[0].y = v.bot[1].y = 3, v.bot[0].dir = RS_E, v.bot[1].dir = RS_W;
    fx.clash_until[1] = 1000;                   /* Scout-7 dashed into Echo-3 */
    draw_board(&v, &fx, &u, 0), ppm("clash");
    memset(&fx, 0, sizeof fx);
    fx.heart_until = 1000, fx.heart_x = 2, fx.heart_y = 3;
    draw_board(&v, &fx, &u, 0), ppm("love");
    memset(&fx, 0, sizeof fx);
    v.bot[1].x = 4;
    draw_board(&v, &fx, &u, 0), ppm("apart");
    g.loop_len = 32, g.prog[0].len = 32, g.prog[1].len = 24;   /* the sequence page, a 32-step loop, scrolled */
    rs_randomize_all(&g);
    v.step = 21, u.sel = 1, u.layer = RS_LAYER_MODIFIER;
    draw_seq(&v, &u, &g, 9 * SEQ_ROW + 5, 9), ppm("seq");
    draw_splash(250), ppm("splash-0250");             /* the splash: the letters dropping, then all in */
    draw_splash(2000), ppm("splash-2000");
    return 0;
}
