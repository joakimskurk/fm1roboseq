/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Joakim (joakimskurk) */
/* The board's colour (rs_mix_colour, D24): 0 % is the HTML's sound exactly; at 100 % each board pulls the way its
 * tiles say (garden: a bigger, wetter, warmer room; gauntlet: drier, brighter, driven; maze: a smaller room), and
 * rendered, two boards sound apart. colour_test: exit 0 all pass */
#include <stdio.h>
#include "rs_mix.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL colour_test: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static struct rs_mix m;
static struct rs_state g;

static void colour(uint8_t board, uint32_t amount)
{
    rs_default(&g), rs_load_board(&g, board);
    rs_mix_colour(&m, &g.board, amount);
}

int main(void)
{
    rs_mix_init(&m, 1);
    for (uint8_t b = 0; b < RS_NBOARDS; b++) {
        colour(b, 0);
        int same = m.send_t == SEND && m.tilt_t == 0 && m.drive_t == 0;
        for (int i = 0; i < 8; i++) same &= m.rev_k[i] == REV_KT[8 + i];
        CHECK(same, "board %d at 0 %%: the HTML's sound", b);
    }
    colour(RS_B_GARDEN, 100);
    CHECK(m.rev_k[0] > REV_KT[8] && m.send_t > SEND && m.tilt_t < 0, "garden: a bigger, wetter, warmer room");
    colour(RS_B_GAUNTLET, 100);
    CHECK(m.send_t < SEND && m.tilt_t > 0 && m.drive_t > 0, "gauntlet: drier, brighter, driven");
    colour(RS_B_MAZE, 100);
    CHECK(m.rev_k[0] < REV_KT[8], "maze: a smaller room");
    colour(RS_B_DEFAULT, 100);
    CHECK(m.rev_k[0] == REV_KT[8], "default: the HTML's room");
    /* rendered: a note on the melody through gauntlet and through garden, the energy after it is gone (the room) */
    static int32_t out[2 * 4096];
    double tail[2];
    for (int k = 0; k < 2; k++) {
        rs_mix_init(&m, 1);
        colour(k ? RS_B_GARDEN : RS_B_GAUNTLET, 100);
        rs_mix_note(&m, RS_V_A_MAIN, 1000, 4000, 69, 26000);
        double e = 0;
        for (uint32_t t = 0; t < 44100u * 2u; t += 4096) {
            rs_mix_render(&m, out, 4096, t);
            if (t >= 44100u / 2u) for (int i = 0; i < 2 * 4096; i++) e += (double)out[i] * out[i];
        }
        tail[k] = e;
    }
    CHECK(tail[1] > 1.5 * tail[0], "garden rings longer than gauntlet (%.3g vs %.3g)", tail[1], tail[0]);
    printf("colour_test: %s\n", fails ? "FAILED" : "all pass");
    return fails != 0;
}
