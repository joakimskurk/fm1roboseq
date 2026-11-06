/* SPDX-License-Identifier: GPL-3.0-only */
/* The model's editing, randomizers and undo (rs_model.c). The HTML draws its random values from Math.random, so
 * these can't be traces: each test holds the HTML's rules instead (what can come out, how often, what is kept). */
#include <stdio.h>
#include <string.h>
#include "rs_model.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static struct rs_state s, t;

static void test_randomize_steps(void)
{
    int early_ok = 1, valid = 1, p75 = 0, n = 0, seen[RS_NACTIONS] = {0};
    rs_default(&s);
    for (int r = 0; r < 1000; r++) {
        rs_randomize_steps(&s, r & 1);
        for (int i = 0; i < RS_MAX_STEPS; i++) {
            const struct rs_step *st = &s.prog[r & 1].s[i];
            if (i < 3 && !(st->action == RS_MOVE || st->action == RS_DASH || st->action == RS_TURNL || st->action == RS_TURNR))
                early_ok = 0;
            if (st->action >= RS_NACTIONS || st->modifier >= RS_NMODIFIERS || (st->prob != 100 && st->prob != 75)) valid = 0;
            if (i >= 3) seen[st->action]++;
            p75 += st->prob == 75, n++;
        }
    }
    CHECK(early_ok, "the first three steps come from the movement pool");
    CHECK(valid, "every step valid, chance 1 or 0.75");
    CHECK(p75 * 100 > n * 8 && p75 * 100 < n * 12, "0.75 on about 10 %% of the steps: %d of %d", p75, n);
    for (int a = 0; a < RS_NACTIONS; a++) CHECK(seen[a] > 1000 * 61 / 7 * 8 / 10, "action %d drawn enough (%d)", a, seen[a]);
    CHECK(rs_undo_depth() == RS_UNDO_MAX, "the undo ring is full at %d", RS_UNDO_MAX);
}

static void test_randomize_board(void)
{
    long tiles = 0;
    int start_ok = 1, blocks_ok = 1, belts_ok = 1;
    rs_default(&s);
    for (int r = 0; r < 2000; r++) {
        s.density = r < 1000 ? 45 : 60;
        rs_randomize_board(&s);
        int blocks = 0;
        for (int i = 0; i < RS_ROWS * RS_COLS; i++) {
            if (s.board.t[i].type != RS_T_NONE && r < 1000) tiles++;
            if (s.board.t[i].type == RS_T_BLOCK) blocks++;
            if (s.board.t[i].type == RS_T_BELT && s.board.t[i].dir > RS_W) belts_ok = 0;
        }
        if (s.board.t[4 * RS_COLS + 0].type || s.board.t[0 * RS_COLS + 4].type) start_ok = 0;
        if (blocks > (r < 1000 ? 4 : 5)) blocks_ok = 0;
        CHECK(s.board_id == RS_B_RANDOM, "a random board is \"random\"");
    }
    CHECK(start_ok, "the launch pads stay free");
    CHECK(blocks_ok, "no more crates than the density allows (4, 5)");
    CHECK(belts_ok, "belts have a direction");
    CHECK(tiles > 9000 && tiles < 11000, "at 0.45 about 23 x 0.45 tiles a board (less a few crates): %ld / 1000", tiles);
    /* while playing it waits for the loop reset */
    rs_default(&s);
    rs_start(&s);
    struct rs_tick_out o;
    rs_tick(&s, &o);
    rs_randomize_board(&s);
    CHECK(s.board_id == RS_B_DEFAULT && s.pending_board == RS_B_RANDOM, "queued while playing");
    for (int i = 0; i < 16; i++) rs_tick(&s, &o);
    CHECK(s.board_id == RS_B_RANDOM && s.pending_board == RS_B_NONE && o.loop_reset, "in place at the loop reset");
    CHECK(!memcmp(&s.board, &s.pending_tiles, sizeof s.board), "the queued tiles");
}

static void test_bassmod_seq2(void)
{
    int ok = 1, swaps = 0, none_b = 0, none_d = 0;
    rs_default(&s);
    s.bassmod_len = 8;
    for (int r = 0; r < 500; r++) {
        rs_randomize_bassmod(&s);
        for (int i = 0; i < 8; i++) {
            const struct rs_bassmod_step *b = &s.bassmod[i];
            int sc = b->scale;
            if (!(sc == RS_SC_MAJOR || sc == RS_SC_MINOR || sc == RS_SC_PENTATONIC || sc == RS_SC_DORIAN ||
                  sc == RS_SC_MIXOLYDIAN || sc == RS_SC_CHROMATIC) || b->root > 11 || b->semitone < -3 || b->semitone > 3)
                ok = 0;
        }
        rs_randomize_seq2(&s);
        for (int i = 0; i < RS_SEQ2_STEPS; i++) {
            swaps += s.seq2[i].regswap;
            none_b += s.seq2[i].board == RS_B_NONE, none_d += s.seq2[i].drums == RS_D_NONE;
            if ((s.seq2[i].board != RS_B_NONE && s.seq2[i].board >= RS_NBOARDS) ||
                (s.seq2[i].drums != RS_D_NONE && s.seq2[i].drums >= RS_NDRUMS))
                ok = 0;
        }
    }
    CHECK(ok, "Seq 1: the six scales, root 0..11, semitone -3..3; seq 2: \"--\" or a preset");
    CHECK(swaps > 500 && swaps < 700, "a swap on about 30 %% of seq 2's steps: %d of 2000", swaps);
    CHECK(none_b > 200 && none_d > 200, "\"--\" is in the pools (%d, %d)", none_b, none_d);
}

static void test_undo(void)
{
    rs_default(&s);
    rs_undo_clear();
    CHECK(!rs_pop_undo(&s), "nothing to undo");
    t = s;
    rs_randomize_all(&s);
    CHECK(rs_undo_depth() == 2, "two pushes for both registers");
    rs_pop_undo(&s);
    rs_pop_undo(&s);
    CHECK(!memcmp(s.prog, t.prog, sizeof s.prog), "undone: the registers as before");
    /* 25 pushes keep the last 20 */
    rs_undo_clear();
    for (int i = 0; i < 25; i++) { s.bpm = (uint16_t)(100 + i); rs_push_undo(&s); }
    CHECK(rs_undo_depth() == RS_UNDO_MAX, "20 kept");
    int n = 0, last = 0;
    while (rs_pop_undo(&s)) last = s.bpm, n++;
    CHECK(n == 20 && last == 105, "the oldest five are gone (%d pops, the last %d)", n, last);
    /* the snapshot holds what pushUndo holds: drums and Seq 1 too, not the board */
    rs_default(&s);
    rs_undo_clear();
    rs_push_undo(&s);
    rs_set_drums(&s, RS_D_TRIPLET);
    s.bassmod[0].root = 7;
    rs_load_board(&s, RS_B_MAZE);
    rs_pop_undo(&s);
    CHECK(s.drum_preset == RS_D_FOUR && s.drum_k[4] == 1 && s.bassmod[0].root == 3, "drums and Seq 1 undone");
    CHECK(s.board_id == RS_B_MAZE, "the board is not in the undo (as the HTML)");
}

static void test_editing(void)
{
    rs_default(&s);
    rs_set_prog_len(&s, 0, 40);
    CHECK(s.prog[0].len == 16, "a program is at most the loop: %u", s.prog[0].len);
    rs_set_loop_len(&s, 48);
    rs_set_prog_len(&s, 0, 40);
    rs_set_loop_len(&s, 12);
    CHECK(s.prog[0].len == 12 && s.prog[1].len == 12, "a shorter loop caps the programs");
    CHECK(s.drum_k[0] && !s.drum_k[12] && rs_drum_steps(12) == 12, "the drums re-made at 12 steps");
    CHECK(!s.playing && s.step == -1, "a loop change stops");
    rs_cycle_prob(&s, 0, 0);
    CHECK(s.prog[0].s[0].prob == 75, "1 -> 0.75");
    rs_cycle_prob(&s, 0, 0);
    rs_cycle_prob(&s, 0, 0);
    CHECK(s.prog[0].s[0].prob == 100, "0.5 -> 1");
    rs_cycle_action(&s, 1, 15);
    CHECK(s.prog[1].s[15].action == RS_WAIT, "ping -> wait: %u", s.prog[1].s[15].action);
    rs_cycle_action(&s, 1, 15);
    CHECK(s.prog[1].s[15].action == RS_MOVE, "wait -> move (it wraps): %u", s.prog[1].s[15].action);
    s.bassmod[0].semitone = 12;
    rs_cycle_bassmod(&s, 0, 2, +1);
    CHECK(s.bassmod[0].semitone == -12, "+12 wraps to -12");
    s.bassmod[0].scale = RS_SC_CHROMATIC;
    rs_cycle_bassmod(&s, 0, 0, +1);
    CHECK(s.bassmod[0].scale == RS_SC_MAJOR, "chromatic wraps to major");
    rs_cycle_bassmod(&s, 0, 1, -1);
    CHECK(s.bassmod[0].root == 2, "root 3 - 1");
    rs_cycle_density(&s);
    CHECK(s.density == 60, "0.45 -> 0.6");
    /* the user board */
    rs_load_board(&s, RS_B_SPINNER);
    rs_save_user_board(&s);
    rs_load_board(&s, RS_B_GARDEN);
    rs_load_user_board(&s);
    CHECK(s.board_id == RS_B_USER && s.board.t[2 * RS_COLS + 2].type == RS_T_ZAP, "the user board comes back");
    /* the boards' overview (D32): each preset's tiles as loading it gives them, and the state untouched */
    for (uint8_t id = 0; id < RS_NBOARDS; id++) {
        struct rs_board look, before = s.board;
        rs_preset_board(&look, id);
        CHECK(!memcmp(&before, &s.board, sizeof before), "looking at preset %d leaves the board alone", id);
        rs_load_board(&s, id);
        CHECK(!memcmp(&look, &s.board, sizeof look), "preset %d looks as it loads", id);
    }
}

static void test_seq2_rolls(void)
{
    struct rs_tick_out o;
    rs_default(&s);
    rs_undo_clear();
    s.vm_freq4 = 1;                                     /* Voice Mod every 4 steps: seq 2 every loop */
    s.seq2_active = 1, s.seq2_rnd[0] = 1;
    rs_start(&s);
    struct rs_prog before = s.prog[0];
    int rolled_at = -1;
    for (int t2 = 0; t2 < 400 && rolled_at < 0; t2++) {
        int8_t prev_index = s.seq2_index;
        rs_tick(&s, &o);
        if (memcmp(&before, &s.prog[0], sizeof before)) {
            rolled_at = t2;
            CHECK(s.seq2_index == 0 && prev_index != 0, "rolled as seq 2 wrapped");
            CHECK(!o.acted[0] || !memcmp(&o.played[0], &before.s[s.step % before.len], sizeof o.played[0]),
                  "the tick it rolled in still played the old register");
        }
    }
    CHECK(rolled_at > 0, "bot A's register rolled when seq 2 wrapped");
    CHECK(rs_undo_depth() == 1, "and that roll is undoable");
}

static void test_steer(void)                     /* D16: a steer is the next move's action, once */
{
    static struct rs_state x, y;
    static struct rs_tick_out ox, oy;
    rs_default(&x), rs_start(&x), rs_default(&y), rs_start(&y);
    for (int t = 0; t < 3; t++) rs_tick(&x, &ox), rs_tick(&y, &oy);
    rs_steer(&y, 1, RS_DASH);
    rs_tick(&x, &ox), rs_tick(&y, &oy);
    CHECK(oy.acted[1] && oy.played[1].action == RS_DASH, "the steered move dashes");
    CHECK(ox.played[1].action != RS_DASH, "(the register's own step is not a dash)");
    CHECK(oy.played[1].modifier == ox.played[1].modifier, "it keeps its step's modifier");
    CHECK(!y.steer[1] && oy.played[0].action == ox.played[0].action, "once, and bot A untouched");
    rs_tick(&x, &ox), rs_tick(&y, &oy);
    CHECK(oy.played[1].action == ox.played[1].action, "then the register again");
    rs_swap_registers(&y);
    CHECK(y.swapped == 1, "the registers swap");
}

static void test_pages(void)                     /* the model's parts of the pages (GLO, SCL, ARP) */
{
    static struct rs_state s;
    static struct rs_tick_out o;
    rs_default(&s);
    rs_seq2_toggle(&s);
    CHECK(s.seq2_active && !s.seq2_pending, "Seq 2 on at once when stopped");
    rs_seq2_toggle(&s), rs_start(&s), rs_seq2_toggle(&s);
    CHECK(!s.seq2_active && s.seq2_pending, "queued while playing");
    for (int k = 0; k < RS_NBOARDS + 2; k++) rs_seq2_cycle(&s, 1, 0, 1);
    CHECK(s.seq2[1].board == RS_B_NONE, "the boards wrap: --, six presets, user, -- (%d)", s.seq2[1].board);
    rs_seq2_cycle(&s, 1, 0, -1);
    CHECK(s.seq2[1].board == RS_B_USER, "back from -- is user");
    rs_seq2_cycle(&s, 2, 1, -1);
    CHECK(s.seq2[2].drums == RS_NDRUMS - 1, "the drums wrap back to the last");
    rs_reset_seq2(&s);
    CHECK(s.seq2[1].board == RS_B_NONE && s.seq2[2].drums == RS_D_NONE && !s.seq2_pending, "Seq 2 reset");
    rs_set_voice_mod(&s, 1, 0);
    CHECK(s.vm_manual && s.vm_freq4 == 4, "MANUAL keeps its frequency");
    rs_set_voice_mod(&s, 0, 16);
    CHECK(!s.vm_manual && s.vm_freq4 == 16, "every 4 loops");
    rs_cycle_bassmod(&s, 0, 1, 1), rs_reset_bassmod(&s);
    CHECK(s.bassmod[0].root == 3 && s.bassmod_len == 4, "Voice Mod reset to its defaults");
    rs_default(&s), rs_start(&s);
    for (int t = 0; t < 5; t++) rs_tick(&s, &o);
    rs_set_loop_len_live(&s, 32);
    CHECK(s.playing && s.loop_len == 32 && s.step == 4, "the loop's length changed without a stop");
    rs_set_loop_len_live(&s, 8);
    CHECK(s.prog[0].len <= 8 && s.prog[1].len <= 8, "the registers capped to it");
    {
        uint32_t full = rs_step_q8(&s);
        s.quarter = 2;
        CHECK(rs_step_q8(&s) == 2 * full || rs_step_q8(&s) == 2 * full + 1, "1/2 speed: steps twice as long (D22)");
        s.quarter = 1;
        CHECK(rs_step_q8(&s) >= 4 * full - 2 && rs_step_q8(&s) <= 4 * full + 2, "1/4 speed: four times");
        s.quarter = 0;
    }
    rs_default(&s);
    rs_drum_toggle(&s, 1, 3);
    CHECK(s.drum_s[3] && s.drum_preset == RS_D_CUSTOM, "a drum step by hand: custom");
    rs_set_loop_len(&s, 12);
    CHECK(s.drum_s[3], "a custom pattern kept when the drum steps change (the HTML finds no preset)");
    rs_drum_preset(&s, RS_D_HALF);
    CHECK(s.drum_preset == RS_D_HALF && s.drum_k[0], "a preset");
    rs_drum_clear(&s);
    CHECK(!s.drums_on && !s.drum_k[0] && s.drum_preset == RS_D_CUSTOM, "cleared: empty, custom, off");
    rs_default(&s);
    rs_save_user_board(&s);
    CHECK(s.board_id == RS_B_USER && s.user_board.t[1].type == s.board.t[1].type, "the board saved as the user's");
    rs_clear_user_board(&s);
    int empty = 1;
    for (int i = 0; i < RS_ROWS * RS_COLS; i++) empty &= s.board.t[i].type == RS_T_NONE && s.user_board.t[i].type == RS_T_NONE;
    CHECK(empty, "the user board cleared, and the board with it while it is on");
    rs_default(&s);                             /* D28: steps typed in, tiles placed by hand */
    rs_set_step(&s, 1, 3, RS_DASH, -1, -1);
    CHECK(s.prog[1].s[3].action == RS_DASH && s.prog[1].s[3].prob == 100, "an action typed in, the rest kept");
    rs_set_step(&s, 1, 3, -1, RS_GLITCH, 50);
    CHECK(s.prog[1].s[3].action == RS_DASH && s.prog[1].s[3].modifier == RS_GLITCH && s.prog[1].s[3].prob == 50, "its modifier and chance");
    rs_pop_undo(&s);
    CHECK(s.prog[1].s[3].modifier != RS_GLITCH, "a typed step undone");
    rs_clear_steps(&s);
    CHECK(s.prog[0].s[0].action == RS_WAIT && s.prog[1].s[15].action == RS_WAIT, "the registers cleared to wait");
    rs_place_tile(&s, 2, 2, RS_T_BELT, RS_W);
    CHECK(s.board.t[12].type == RS_T_BELT && s.board.t[12].dir == RS_W, "a belt placed, pointing west");
    rs_place_tile(&s, 2, 2, RS_T_NONE, 0);
    CHECK(s.board.t[12].type == RS_T_NONE, "and erased");
    rs_place_tile(&s, 5, 0, RS_T_ZAP, 0);
    CHECK(1, "off the board: nothing (no crash)");
}

int main(void)
{
    test_steer();
    test_pages();
    test_randomize_steps();
    test_randomize_board();
    test_bassmod_seq2();
    test_undo();
    test_editing();
    test_seq2_rolls();
    printf("edit_test: %s\n", fails ? "FAILED" : "all pass");
    return fails != 0;
}
