/* SPDX-License-Identifier: GPL-3.0-only */
/* The README's first tour: every page as the firmware draws it (rs_screen.c, rs_splash.c), from a real game a few
 * steps in, into docs/screens/<name>.ppm (make screens turns them into PNG). */
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
#include "rs_battle_screen.c"

static void shot(const char *name)
{
    char path[128];
    snprintf(path, sizeof path, "docs/screens/%s.ppm", name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6 240 240 255\n");
    for (int i = 0; i < 240 * 240; i++) {
        uint16_t c = fb[i];
        unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("screen -> %s\n", path);
}

static struct rs_state g;
static struct rs_tick_out o;
static struct rs_view v;
static struct rs_ui u;
static struct rs_fx fx;

static void view_ui(void)                       /* roboseq.c's view_of and ui_of */
{
    v.step = g.step, v.loop_len = g.loop_len, v.board_id = g.board_id, v.playing = g.playing, v.cycle = g.cycle;
    v.bot[0] = g.bot[0], v.bot[1] = g.bot[1], v.board = g.board, v.drum_tick = g.drum_tick, v.active = o.active;
    for (int b = 0; b < 2; b++) {
        int len = g.prog[b].len ? g.prog[b].len : g.loop_len;
        u.len[b] = (uint8_t)len, u.playing_at[b] = g.playing && g.step >= 0 ? (int16_t)(g.step % len) : -1;
        for (int k = 0; k < 16; k++) u.steps[b][k] = g.prog[b].s[k];
    }
    u.swapped = g.swapped;
}

int main(void)
{
    static const int32_t LEVEL[RS_NLEVEL] = {29491, 29491, 26214, 18022, 19661, 22938, 19661};
    draw_splash(2000), shot("splash");
    rs_default(&g), view_ui();
    draw_board(&v, &fx, &u, 0), shot("home");
    rs_start(&g);
    for (int t = 0; t < 7; t++) rs_tick(&g, &o);
    view_ui(), fx.lit = o.active, fx.lit_until = 100;
    draw_board(&v, &fx, &u, 0), shot("home-playing");
    lead_badge = 0, lead_panel_v[0] = 13, lead_panel_v[1] = 12, lead_panel_v[2] = 8, lead_panel_v[3] = 0, lead_panel_arp = 2;
    draw_board(&v, &fx, &u, 0), shot("home-lead");
    lead_badge = -99, lead_panel_arp = -1;
    memset(&fx, 0, sizeof fx);
    {                                           /* the two meet on a tile: a heart over them */
        struct rs_view w = v;
        w.bot[0].x = w.bot[1].x = 2, w.bot[0].y = w.bot[1].y = 3, w.bot[0].dir = RS_E, w.bot[1].dir = RS_W;
        fx.heart_until = 100, fx.heart_x = 2, fx.heart_y = 3;
        draw_board(&w, &fx, &u, 0), shot("home-love");
        memset(&fx, 0, sizeof fx);
    }
    u.sel = 0, u.layer = RS_LAYER_ACTION;
    draw_seq(&v, &u, &g, 0, 3), shot("seq");
    rs_place_tile(&g, 0, 2, RS_T_BELT, RS_E), rs_place_tile(&g, 1, 2, RS_T_BELT, RS_E);
    view_ui(), draw_editor(&v, &g, 2, 2, 1, 1), shot("builder");
    rs_default(&g), rs_start(&g), rs_load_board(&g, RS_B_SPINNER);   /* the boards: DEFAULT playing, SPINNER coming */
    draw_boards(&g, 1, 0), shot("boards");
    rs_default(&g), rs_start(&g);
    g.engine = RS_ENGINE_BAND, draw_glo(&g, 32), shot("glo");   /* (the app starts in BAND: D33) */
    rs_apply_prog(&g, 1, 5), rs_set_follow(&g, 1);   /* (D38: I V vi IV in G, FOLLOW on, the next one ready) */
    g.vm_index = 2, rs_advance_voice_mod(&g);            /* (on the last step: the next one ready) */
    draw_scl(&g, 1), shot("voicemod");
    g.vm_index = 0, g.next_ready = 0;
    g.seq2[0] = (struct rs_seq2_step){RS_B_GARDEN, RS_D_BREAK, 0, 2}, g.seq2[2] = (struct rs_seq2_step){RS_B_MAZE, RS_D_NONE, 1, 26};
    g.seq2_active = 1, g.seq2_index = 0, g.seq2_rnd[0] = 1;
    draw_arp(&g, 2), shot("seq2");
    for (int t = 0; t < 5; t++) rs_tick(&g, &o);
    view_ui(), draw_drums(&v, &g, 1), shot("drums");
    draw_mix(LEVEL, 6, 40), shot("mix");
    {                                           /* the battle mode (D42): how to play, the countdown, a strike, a round, the winner */
        rs_default(&g), g.engine = RS_ENGINE_BAND, view_ui(), v.battle = g.battle;
        draw_battle(&v, &fx, 0, 3, RS_ARENA_NEXT, 0), shot("battle");
        rs_start(&g), rs_battle_start(&g, 3, RS_ARENA_SAME);
        while (g.battle.count > 2) rs_tick(&g, &o);
        view_ui(), v.battle = g.battle;
        draw_battle(&v, &fx, 0, 3, RS_ARENA_NEXT, 0), shot("battle-countdown");
        while (g.battle.phase == RS_BT_READY) rs_tick(&g, &o);
        g.bot[0].x = 1, g.bot[0].y = 2, g.bot[0].dir = RS_E, g.bot[1].x = 2, g.bot[1].y = 2, g.bot[1].dir = RS_W, g.bot[1].heat = 2;
        rs_battle_keys(&g, 0, 0, 1, 0), rs_battle_keys(&g, 1, RS_N + 1, 0, 0);
        do rs_tick(&g, &o); while (!(o.move_tick && g.step % 4 == 0));
        view_ui(), v.battle = g.battle, fx.clash_until[1] = 100;
        draw_battle(&v, &fx, 0, 3, RS_ARENA_NEXT, 1u << 7 | 1u << 20), shot("battle-strike");   /* (C4 and C#5 held) */
        memset(&fx, 0, sizeof fx);
        g.battle.phase = RS_BT_ROUND, g.battle.winner = 0, g.battle.wins[0] = 1, g.bot[1].dying = 1, g.bot[1].heat = 6;
        view_ui(), v.battle = g.battle;
        draw_battle(&v, &fx, 0, 3, RS_ARENA_NEXT, 0), shot("battle-round");
        g.battle.phase = RS_BT_MATCH, g.battle.winner = 1, g.battle.wins[0] = 1, g.battle.wins[1] = 3;
        view_ui(), v.battle = g.battle;
        draw_battle(&v, &fx, 0, 3, RS_ARENA_NEXT, 0), shot("battle-winner");
    }
    return 0;
}
