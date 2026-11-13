/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Joakim (joakimskurk) */
/* The battle mode (D42): the robots move on the beat as their players hold, strike, heat up and burn out; the rounds
 * and the match; the tiles as in the game; the music runs on. battle_test: exit 0 all pass */
#include <stdio.h>
#include <string.h>
#include "rs_model.c"
#include "rs_score.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static struct rs_state s;
static struct rs_tick_out o;

static void arena(uint8_t engine)                       /* an empty board, a match to 3, past the countdown */
{
    rs_default(&s);
    for (int i = 0; i < RS_ROWS * RS_COLS; i++) s.board.t[i] = (struct rs_tile_t){RS_T_NONE, 0};
    s.engine = engine;
    rs_start(&s);
    rs_battle_start(&s, 3, RS_ARENA_SAME);
}

static int beat(void)                                    /* tick to the next beat; the ticks between it */
{
    int n = 0;
    do rs_tick(&s, &o), n++; while (!(o.move_tick && s.step % rs_battle_beat_steps(&s) == 0));
    return n;
}

static void fight(void)                                  /* through the countdown */
{
    for (int k = 0; k < 8 && s.battle.phase == RS_BT_READY; k++) beat();
    CHECK(s.battle.phase == RS_BT_FIGHT, "the countdown ends in the fight");
}

static void put(int b, int x, int y, uint8_t dir) { s.bot[b].x = (int8_t)x, s.bot[b].y = (int8_t)y, s.bot[b].dir = dir, s.bot[b].heat = 0, s.battle.guard[b] = 0; }
static void keys(int b, int hold, int act) { rs_battle_keys(&s, b, (uint8_t)hold, (uint8_t)act, 0); }
#define HOLD(d) ((d) + 1)

static void test_countdown_and_beat(void)
{
    arena(RS_ENGINE_BAND);
    keys(0, HOLD(RS_N), 0);
    int moved_early = 0, beats = 0;
    int counts = 0, beeps = 0;
    while (s.battle.phase == RS_BT_READY) {
        beat(), beats++, moved_early |= s.bot[0].y != 4;
        for (int i = 0; i < o.nnotes; i++) beeps += o.notes[i].voice == RS_V_COIN;
        counts += s.battle.phase == RS_BT_READY && s.battle.count == 5 - beats;
    }
    CHECK(!moved_early && beats == 5 && counts == 4, "the countdown: 4 3 2 1 on four beats, FIGHT on the fifth, nobody moves (%d beats)", beats);
    CHECK(beeps == 5, "a beep on each, FIGHT too (%d)", beeps);
    CHECK(s.bot[0].y == 4, "on FIGHT's beat they stand; from the next they go");
    int y0 = s.bot[0].y, off_beat_moves = 0;
    for (int k = 0; k < 3; k++) {                       /* holding north: a tile a beat, never between */
        for (int t = 1; t < rs_battle_beat_steps(&s); t++) {
            rs_tick(&s, &o);
            off_beat_moves += s.bot[0].y != y0 - k;
        }
        beat();
        CHECK(s.bot[0].y == y0 - k - 1, "beat %d: one tile north (%d)", k, s.bot[0].y);
    }
    CHECK(off_beat_moves == 0, "between the beats it stands");
    for (int k = 0; k < 3; k++) beat();
    CHECK(s.bot[0].y == 0 && (o.bumped[0] & 1), "at the top it bumps the wall");
    keys(0, 0, 0);
    int y = s.bot[0].y;
    beat();
    CHECK(s.bot[0].y == y && o.played[0].action == RS_WAIT, "nothing held: it waits");
}

static void test_bumps(void)
{
    arena(RS_ENGINE_BAND);
    fight();
    s.board.t[2 * RS_COLS + 2] = (struct rs_tile_t){RS_T_BLOCK, 0};
    put(0, 1, 2, RS_E), put(1, 4, 4, RS_W);
    keys(0, HOLD(RS_E), 0);
    beat();
    CHECK(s.bot[0].x == 1 && (o.bumped[0] & 2), "a crate stops it");
    s.board.t[2 * RS_COLS + 2] = (struct rs_tile_t){RS_T_NONE, 0};
    put(0, 1, 0, RS_E), put(1, 3, 0, RS_W);             /* both into the tile between: both stay */
    keys(0, HOLD(RS_E), 0), keys(1, HOLD(RS_W), 0);
    beat();
    CHECK(s.bot[0].x == 1 && s.bot[1].x == 3, "both into one tile: both bump (%d, %d)", s.bot[0].x, s.bot[1].x);
    put(0, 1, 4, RS_E), put(1, 2, 4, RS_W);             /* through each other: both stay */
    keys(0, HOLD(RS_E), 0), keys(1, HOLD(RS_W), 0);
    beat();
    CHECK(s.bot[0].x == 1 && s.bot[1].x == 2, "through each other: both bump");
    put(0, 1, 3, RS_E), put(1, 2, 3, RS_N);             /* the other moves away: it follows */
    keys(0, HOLD(RS_E), 0), keys(1, HOLD(RS_N), 0);
    beat();
    CHECK(s.bot[0].x == 2 && s.bot[1].y == 2, "into the tile the other leaves");
    put(0, 1, 1, RS_E), put(1, 2, 1, RS_N);             /* onto the other where it stays: a bump */
    keys(0, HOLD(RS_E), 0), keys(1, 0, 0);
    beat();
    CHECK(s.bot[0].x == 1, "onto the other: a bump");
}

static void test_strikes(void)
{
    arena(RS_ENGINE_BAND);
    fight();
    put(0, 1, 2, RS_E), put(1, 2, 2, RS_W);
    keys(0, 0, 1), keys(1, 0, 0);
    beat();
    CHECK(s.bot[1].heat == 2 && s.bot[1].x == 4 && s.bot[0].x == 0 && o.clash[1] && s.battle.hit[0],
          "a hit: +2, two tiles back, the striker one the other way (%d at %d, striker at %d)", s.bot[1].heat, s.bot[1].x, s.bot[0].x);
    CHECK(o.played[0].action == RS_DASH, "the music hears a strike as a dash");
    beat();
    CHECK(s.bot[1].heat == 2, "and apart: the next beat's strike does not reach");
    put(0, 2, 2, RS_E), put(1, 3, 2, RS_W);             /* a tile from the wall: one back, slammed once */
    beat();
    CHECK(s.bot[1].heat == 3 && s.bot[1].x == 4 && s.bot[0].x == 1, "a tile from the wall: +3 (%d)", s.bot[1].heat);
    put(0, 3, 2, RS_E), put(1, 4, 2, RS_W);             /* at the wall: slammed twice */
    beat();
    CHECK(s.bot[1].heat == 4 && s.bot[1].x == 4, "at the wall: +4, it stays (%d)", s.bot[1].heat);
    put(0, 0, 0, RS_E), put(1, 2, 0, RS_W);             /* two tiles on: a lunge, then the recoil */
    beat();
    CHECK(s.bot[0].x == 0 && s.bot[1].heat == 2 && s.bot[1].x == 4, "two tiles on: it lunges, hits, recoils (%d, %d, %d)",
          s.bot[0].x, s.bot[1].heat, s.bot[1].x);
    put(0, 0, 4, RS_E), put(1, 3, 4, RS_W);             /* three on: nothing */
    beat();
    CHECK(s.bot[0].x == 0 && s.bot[1].heat == 0, "three tiles on: a swing at the air");
    s.board.t[4 * RS_COLS + 1] = (struct rs_tile_t){RS_T_BLOCK, 0};
    put(0, 0, 4, RS_E), put(1, 2, 4, RS_W);             /* a crate between: no lunge */
    beat();
    CHECK(s.bot[1].heat == 0, "a crate between: no hit");
    s.board.t[4 * RS_COLS + 1] = (struct rs_tile_t){RS_T_NONE, 0};
    put(0, 1, 1, RS_E), put(1, 2, 1, RS_W);             /* both strike: both hit, both driven back */
    keys(1, 0, 1);
    beat();
    CHECK(s.bot[0].heat >= 2 && s.bot[1].heat >= 2 && s.bot[1].x - s.bot[0].x >= 3, "a trade: both +2 at least, apart (%d, %d)", s.bot[0].x, s.bot[1].x);
    keys(0, HOLD(RS_S), 1), keys(1, 0, 0);              /* a way held and the action: it turns and strikes */
    put(0, 2, 1, RS_E), put(1, 2, 2, RS_N);
    beat();
    CHECK(s.bot[0].dir == RS_S && s.bot[1].heat == 2 && s.bot[1].y == 4 && s.bot[0].y == 0, "it turns to the way held and strikes");
    keys(0, 0, 0);
    put(0, 1, 3, RS_E), put(1, 2, 3, RS_W);
    rs_battle_keys(&s, 0, 0, 0, 1);                     /* a tap between two beats */
    beat();
    CHECK(s.bot[1].heat == 2, "a tap between the beats strikes on the next");
    put(0, 1, 3, RS_E), put(1, 2, 3, RS_W), s.bot[1].heat = 2;
    beat();
    CHECK(s.bot[1].heat == 2, "once");
}

static void test_tiles(void)
{
    arena(RS_ENGINE_BAND);
    fight();
    keys(0, 0, 0), keys(1, 0, 0);
    s.board.t[2 * RS_COLS + 0] = (struct rs_tile_t){RS_T_ZAP, 0};
    s.board.t[0 * RS_COLS + 0] = (struct rs_tile_t){RS_T_PULSE, 0};
    s.board.t[2 * RS_COLS + 4] = (struct rs_tile_t){RS_T_BELT, RS_S};
    s.board.t[0 * RS_COLS + 4] = (struct rs_tile_t){RS_T_ROTR, 0};
    put(0, 0, 2, RS_N), put(1, 4, 0, RS_W);
    beat(), beat();
    CHECK(s.bot[0].heat == 2, "a zap rail: +1 a beat (%d)", s.bot[0].heat);
    CHECK(s.bot[1].dir == RS_E, "a rotator turns it every beat (%c)", rs_dir_name[s.bot[1].dir]);   /* W -> N -> E */
    put(0, 0, 0, RS_N), s.bot[0].heat = 3;
    beat();
    CHECK(s.bot[0].heat == 2, "a pulse pad cools it");
    put(1, 4, 1, RS_S);
    keys(1, HOLD(RS_S), 0);
    beat();
    CHECK(s.bot[1].y == 3, "onto a belt: it carries it on (%d)", s.bot[1].y);
}

static void test_rounds(void)
{
    arena(RS_ENGINE_BAND);
    fight();
    for (int r = 0; r < 3; r++) {                       /* Scout-7 takes three rounds */
        for (int k = 0; k < 8 && s.battle.phase == RS_BT_READY; k++) beat();
        CHECK(s.battle.phase == RS_BT_FIGHT && s.bot[0].heat == 0 && s.bot[1].heat == 0 && s.bot[0].x == 0 && s.bot[1].x == 4,
              "round %d: from the pads, cool", r + 1);
        put(0, 1, 2, RS_E), put(1, 2, 2, RS_W);
        keys(0, 0, 1), keys(1, 0, 0);
        for (int k = 0; k < 10 && s.battle.phase == RS_BT_FIGHT; k++) {
            if (s.bot[1].x != s.bot[0].x + 1 || s.bot[1].y != s.bot[0].y)   /* (back in front of it, as hot as it is) */
                s.bot[1].x = (int8_t)(s.bot[0].x + 1), s.bot[1].y = s.bot[0].y;
            beat();
        }
        CHECK(s.battle.phase == RS_BT_ROUND && s.battle.winner == 0 && s.battle.wins[0] == r + 1 && o.died[1],
              "round %d: Echo-3 burns out, Scout-7 takes it (%d wins)", r + 1, s.battle.wins[0]);
        keys(0, 0, 0);
        for (int k = 0; k < 9 && s.battle.phase == RS_BT_ROUND; k++) beat();
        if (r < 2) CHECK(s.battle.phase == RS_BT_READY && s.battle.round == r + 2, "then round %d", r + 2);
    }
    CHECK(s.battle.phase == RS_BT_MATCH && s.battle.winner == 0, "three rounds: Scout-7 takes the match");
    for (int k = 0; k < 8; k++) beat();
    CHECK(s.battle.phase == RS_BT_MATCH, "the winner stays on");
    rs_battle_start(&s, 1, RS_ARENA_SAME);                             /* a draw: both burn out on one beat */
    fight();
    put(0, 1, 2, RS_E), put(1, 2, 2, RS_W);
    s.bot[0].heat = 4, s.bot[1].heat = 4;
    keys(0, 0, 1), keys(1, 0, 1);
    beat();
    CHECK(s.battle.phase == RS_BT_ROUND && s.battle.winner == 2 && !s.battle.wins[0] && !s.battle.wins[1], "both out, as many hits: a draw");
    for (int k = 0; k < 9 && s.battle.phase == RS_BT_ROUND; k++) beat();
    CHECK(s.battle.phase == RS_BT_READY && s.battle.round == 2, "and the round again");
    fight();
    put(0, 1, 2, RS_E), put(1, 2, 2, RS_W);
    s.bot[0].heat = 4, s.bot[1].heat = 4;
    beat();
    CHECK(s.battle.phase == RS_BT_ROUND && s.battle.winner < 2 && s.battle.coin && s.battle.wins[s.battle.winner] == 1,
          "a draw again: a coin decides it");
}

static void test_music(void)                            /* the music runs on, and it is what they do */
{
    for (int eng = 0; eng < 2; eng++) {
        arena((uint8_t)eng);
        fight();
        keys(0, HOLD(RS_N), 0), keys(1, HOLD(RS_S), 0);
        int lead = 0, drums = 0, bass = 0, steps = 0;
        for (int t = 0; t < 64; t++) {
            rs_tick(&s, &o), steps++;
            for (int i = 0; i < o.nnotes; i++) {
                lead += o.notes[i].voice == RS_V_A_MAIN && o.notes[i].vel;
                drums += o.notes[i].voice >= RS_V_KICK && o.notes[i].voice <= RS_V_HIHAT;
                bass += o.notes[i].voice == RS_V_B_SUB;
            }
            if (t == 20) keys(0, HOLD(RS_S), 0), keys(1, HOLD(RS_N), 0);
        }
        printf("battle_test: %s: in 64 steps %d of Scout-7's notes, %d drum hits, %d bass notes\n", eng ? "BAND" : "ORIGINAL", lead, drums, bass);
        CHECK(lead >= 6 && drums >= 16 && bass >= 8, "%s: Scout-7 sings as it moves, the band plays on", eng ? "BAND" : "ORIGINAL");
    }
    {                                                   /* the battle off: the registers play again, the traces' game */
        static struct rs_state ref;
        arena(RS_ENGINE_BAND);
        rs_battle_stop(&s);
        rs_default(&ref);
        for (int i = 0; i < RS_ROWS * RS_COLS; i++) ref.board.t[i] = (struct rs_tile_t){RS_T_NONE, 0};
        ref.engine = RS_ENGINE_BAND;
        rs_start(&s), rs_start(&ref);
        int same = 1;
        static struct rs_tick_out o2;
        for (int t = 0; t < 200; t++) rs_tick(&s, &o), rs_tick(&ref, &o2), same &= !memcmp(s.bot, ref.bot, sizeof s.bot);
        CHECK(same, "after a battle the registers play the same game");
    }
}

static void test_arenas(void)                           /* between rounds: the same arena, the next, a rolled one */
{
    for (uint8_t mode = 0; mode < RS_NARENAS; mode++) {
        rs_default(&s);
        rs_load_board(&s, RS_B_DEFAULT);
        rs_start(&s);
        rs_battle_start(&s, 9, mode);
        uint8_t ids[4];
        struct rs_board boards[4];
        for (int r = 0; r < 4; r++) {
            fight();
            ids[r] = s.board_id, boards[r] = s.board;
            int pads = s.board.t[START[0].y * RS_COLS + START[0].x].type == RS_T_NONE && s.board.t[START[1].y * RS_COLS + START[1].x].type == RS_T_NONE;
            CHECK(pads, "arena %d round %d: the pads open", mode, r + 1);
            s.bot[1].heat = 6;                          /* (Echo-3 burns out: the round ends) */
            s.bot[1].x = 2, s.bot[1].y = 2;
            keys(0, 0, 0), keys(1, 0, 0);
            beat();
            for (int k = 0; k < 10 && s.battle.phase == RS_BT_ROUND; k++) beat();
        }
        if (mode == RS_ARENA_SAME) CHECK(ids[0] == ids[3] && !memcmp(&boards[0], &boards[3], sizeof boards[0]), "SAME: one arena all match");
        if (mode == RS_ARENA_NEXT) CHECK(ids[1] == (ids[0] + 1) % RS_NBOARDS && ids[2] == (ids[1] + 1) % RS_NBOARDS, "NEXT: the presets in turn (%d %d %d)", ids[0], ids[1], ids[2]);
        if (mode == RS_ARENA_RANDOM) CHECK(ids[1] == RS_B_RANDOM && memcmp(&boards[1], &boards[2], sizeof boards[1]), "RANDOM: a new roll each round");
    }
}

static void test_overheat(void)                         /* a round that would not end: the arena heats them */
{
    arena(RS_ENGINE_BAND);
    fight();
    keys(0, 0, 0), keys(1, 0, 0);
    int beats = 0;
    while (s.battle.phase == RS_BT_FIGHT && beats < 200) beat(), beats++;
    CHECK(s.battle.phase == RS_BT_ROUND && beats >= RS_BT_OVERHEAT && beats <= RS_BT_OVERHEAT + 8 && s.battle.winner == 2,
          "two who stand still at their pads: the glowing edge ends the round, a draw (%d beats)", beats);
    for (int k = 0; k < 10 && s.battle.phase == RS_BT_ROUND; k++) beat();
    fight();
    put(0, 2, 2, RS_N), put(1, 4, 0, RS_W);              /* one in the middle, one at the edge: the edge burns */
    beats = 0;
    while (s.battle.phase == RS_BT_FIGHT && beats < 200) beat(), beats++;
    CHECK(s.battle.phase == RS_BT_ROUND && s.battle.winner == 0, "the middle outlasts the edge");
}

static void test_playtest(void)                         /* two greedy players: no free chains, rounds that last */
{
    long rounds = 0, beats = 0, hits = 0, chains = 0, strikes = 0;
    for (int board = 0; board < RS_NBOARDS; board++) {
        rs_default(&s);
        rs_load_board(&s, (uint8_t)board);
        rs_start(&s);
        rs_battle_start(&s, 3, RS_ARENA_SAME);
        int last_hit[2] = {-9, -9}, n = 0, bumped[2] = {0, 0};
        for (int t = 0; t < 4000 && s.battle.phase != RS_BT_MATCH; t++) {
            for (int b = 0; b < 2; b++) {               /* go at the other along the longer gap; strike when in line, near */
                const struct rs_bot *me = &s.bot[b], *ot = &s.bot[!b];
                int dx = ot->x - me->x, dy = ot->y - me->y, adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
                int in_line = (dx == 0 && ady <= 2) || (dy == 0 && adx <= 2);
                int way = adx >= ady ? (dx > 0 ? RS_E : RS_W) : (dy > 0 ? RS_S : RS_N);
                if (rs_battle_hot(&s.battle, me->x, me->y) && !in_line) {   /* (the floor glows: to the middle) */
                    int mx = RS_COLS / 2 - me->x, my = RS_ROWS / 2 - me->y;
                    way = (mx < 0 ? -mx : mx) >= (my < 0 ? -my : my) ? (mx > 0 ? RS_E : RS_W) : (my > 0 ? RS_S : RS_N);
                    keys(b, HOLD(way), 0);
                    continue;
                }
                if (b) {                                /* (Echo-3 plays another way: the shorter gap, strikes only up close) */
                    way = adx < ady && adx ? (dx > 0 ? RS_E : RS_W) : ady ? (dy > 0 ? RS_S : RS_N) : way;
                    in_line = (dx == 0 && ady == 1) || (dy == 0 && adx == 1);
                }
                if (bumped[b] && !in_line)              /* (bumped last beat: the other way round, as a player would) */
                    way = way == RS_E || way == RS_W ? (dy > 0 ? RS_S : RS_N) : (dx > 0 ? RS_E : RS_W);
                keys(b, in_line ? HOLD(dx > 0 ? RS_E : dx < 0 ? RS_W : dy > 0 ? RS_S : RS_N) : HOLD(way), in_line);
            }
            rs_tick(&s, &o);
            if (!(o.move_tick && s.step % 4 == 0)) continue;
            n++, bumped[0] = o.bumped[0] != 0, bumped[1] = o.bumped[1] != 0;
            if (s.battle.phase == RS_BT_FIGHT) beats++;
            for (int b = 0; b < 2; b++) {
                strikes += s.battle.did[b] == RS_BT_STRIKE;
                if (s.battle.hit[b]) hits++, chains += last_hit[b] == n - 1, last_hit[b] = n;
            }
            if (s.battle.phase == RS_BT_ROUND && s.battle.count == 8) rounds++;
        }
        printf("battle_test:   board %d: %d beats, wins %d-%d, phase %d, heat %d %d\n", board, n, s.battle.wins[0], s.battle.wins[1], s.battle.phase, s.bot[0].heat, s.bot[1].heat);
        CHECK(s.battle.phase == RS_BT_MATCH, "board %d: the greedy two finish a match", board);
    }
    printf("battle_test: greedy play on six arenas: %ld rounds, %.1f beats a round, %ld hits of %ld strikes, %ld hit again on the next beat\n",
           rounds, (double)beats / (double)(rounds ? rounds : 1), hits, strikes, chains);
    CHECK(chains == 0, "no one is hit two beats running: it reels, untouchable (%ld)", chains);
    CHECK(rounds >= 12 && beats / rounds >= 6, "rounds that last a while (%.1f beats)", (double)beats / (double)(rounds ? rounds : 1));
}

int main(void)
{
    test_countdown_and_beat();
    test_bumps();
    test_strikes();
    test_tiles();
    test_rounds();
    test_arenas();
    test_overheat();
    test_playtest();
    test_music();
    printf("battle_test: %s\n", fails ? "FAILED" : "all pass");
    return fails != 0;
}
