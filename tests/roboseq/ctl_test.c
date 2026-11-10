/* SPDX-License-Identifier: GPL-3.0-only */
/* The controls, as the product owner played them and found them wrong (2026-11-10, D45): each a check on the app's
 * own passes and keys, after tests/roboseq/p4_test.c (included at its end, built into roboseq.c with -DRS_P4_TEST):
 *
 *   knobs    a knob a page does not read is let go, not kept for the next page (K3 on HOME was GLO's loop later)
 *   swap     F#5 (HOME) and F5 (SEQ) swap the robots' programs, steps and lengths; SEQ's undo brings them back
 *   roll     on HOME, K2 rolls the picked robot's program, K3 both, a roll a turn, while it plays
 *   loop     a program as long as the loop follows GLO's K3 up and down, stopped or as the loop comes round;
 *            one made shorter stays (the HTML only cuts: a loop of 8 once left the programs at 8)
 *
 * Exits through p4_main: 0 when all hold. */

static void app_pass(void);
static int ctl_fails;
#define CTL_CHECK(c, ...) do { if (!(c)) { ctl_fails++; printf("FAIL ctl_test: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void ctl_fresh(uint8_t pg)               /* a new game on a page, stopped, the keys and knobs at rest */
{
    rs_default(&game);
    game.engine = RS_ENGINE_BAND;
    page = pg, lead_on = 0, arp_mode = 0, held_n = 0, pending_loop = 0, sel_bot = 0, bank = 0;
    fm1_in.notes = 0, ml.prev = 0;
    for (int i = 0; i < HOST_NENC; i++) host_enc[i] = 0;
}

static void ctl_knobs(void)                     /* a turn a page leaves unread is gone when another page opens */
{
    ctl_fresh(PAGE_HOME);
    uint8_t loop = game.loop_len, quarter = game.quarter, tpm = game.tpm;
    host_enc[ENC_KNOB3] = 3, host_enc[ENC_KNOB4] = 2;   /* (HOME reads neither) */
    app_pass();
    page = PAGE_GLO;
    app_pass();
    CTL_CHECK(game.loop_len == loop && !pending_loop, "knobs: K3 turned on HOME moved GLO's loop (%u -> %u, pending %u)",
              loop, game.loop_len, pending_loop);
    CTL_CHECK(game.quarter == quarter && game.tpm == tpm, "knobs: K4 turned on HOME moved GLO's speed");
    ctl_fresh(PAGE_SEQ);                        /* (SEQ reads K1 and K2: K3, K4 are not its) */
    host_enc[ENC_KNOB3] = -2, host_enc[ENC_KNOB4] = 1;
    app_pass();
    page = PAGE_GLO;
    app_pass();
    CTL_CHECK(game.loop_len == loop && game.quarter == quarter, "knobs: K3 / K4 turned on SEQ moved GLO's loop or speed");
}

static void ctl_swap(void)                      /* the swap is of the programs, not of who plays high */
{
    ctl_fresh(PAGE_HOME);
    rs_randomize_all(&game);
    rs_set_prog_len(&game, 1, 12);
    struct rs_prog a = game.prog[0], b = game.prog[1];
    uint8_t swapped = game.swapped;
    keys(1u << 25);                             /* F#5 on HOME */
    CTL_CHECK(!memcmp(&game.prog[0], &b, sizeof b) && !memcmp(&game.prog[1], &a, sizeof a),
              "swap: F#5 on HOME did not swap the programs");
    CTL_CHECK(game.swapped == swapped, "swap: F#5 on HOME swapped who plays high (REG SWAP), not the programs");
    CTL_CHECK(game.prog[0].len == 12 && game.prog[1].len == a.len, "swap: the lengths did not go with the programs");
    page = PAGE_SEQ;
    keys(1u << 24);                             /* F5 on SEQ: back */
    CTL_CHECK(!memcmp(&game.prog[0], &a, sizeof a) && !memcmp(&game.prog[1], &b, sizeof b), "swap: F5 on SEQ did not swap back");
    keys(1u << 20);                             /* C#5: undo, the swap before it */
    CTL_CHECK(!memcmp(&game.prog[0], &b, sizeof b) && !memcmp(&game.prog[1], &a, sizeof a), "swap: undo did not bring the swap back");
}

static void ctl_roll(void)                      /* new programs on HOME, while it plays */
{
    ctl_fresh(PAGE_HOME);
    play();
    sel_bot = 1;                                /* (EDIT: Echo-3's on the keys) */
    struct rs_prog a = game.prog[0], b = game.prog[1];
    host_enc[ENC_KNOB2] = 1;
    app_pass();
    CTL_CHECK(!memcmp(&game.prog[0], &a, sizeof a) && memcmp(&game.prog[1], &b, sizeof b),
              "roll: K2 on HOME did not roll the picked robot's program (and only it)");
    a = game.prog[0], b = game.prog[1];
    host_enc[ENC_KNOB3] = -1;
    app_pass();
    CTL_CHECK(memcmp(&game.prog[0], &a, sizeof a) && memcmp(&game.prog[1], &b, sizeof b), "roll: K3 on HOME did not roll both");
    CTL_CHECK(game.playing, "roll: a roll stopped the music");
    page = PAGE_SEQ;
    keys(1u << 20);                             /* SEQ's undo: the roll before */
    CTL_CHECK(!memcmp(&game.prog[0], &a, sizeof a) && !memcmp(&game.prog[1], &b, sizeof b), "roll: undo did not bring K3's roll back");
    rs_stop(&game);
}

static uint32_t ctl_len(int b) { return game.prog[b].len ? game.prog[b].len : game.loop_len; }

static void ctl_loop(void)
{
    ctl_fresh(PAGE_GLO);
    rs_set_prog_len(&game, 1, 12);              /* Echo-3's made shorter on purpose */
    host_enc[ENC_KNOB3] = 2;                    /* stopped: 16 -> 32 at once */
    app_pass();
    CTL_CHECK(game.loop_len == 32 && ctl_len(0) == 32 && ctl_len(1) == 12,
              "loop: 16 -> 32 stopped: the loop %u, Scout-7 %u (32: it was the loop's), Echo-3 %u (12: made shorter)",
              game.loop_len, ctl_len(0), ctl_len(1));
    host_enc[ENC_KNOB3] = -3;                   /* 32 -> 8: both cut */
    app_pass();
    CTL_CHECK(game.loop_len == 12 && ctl_len(0) == 12 && ctl_len(1) == 12, "loop: 32 -> 12: the loop %u, %u, %u",
              game.loop_len, ctl_len(0), ctl_len(1));
    host_enc[ENC_KNOB3] = -1;
    app_pass();
    host_enc[ENC_KNOB3] = 2;                    /* 8 -> 16: both follow it back (the bug: they stayed at 8) */
    app_pass();
    CTL_CHECK(game.loop_len == 16 && ctl_len(0) == 16 && ctl_len(1) == 16,
              "loop: 8 -> 16: the programs did not follow the loop back (the loop %u, %u, %u)", game.loop_len, ctl_len(0), ctl_len(1));
    play();                                     /* playing: as the loop comes round */
    host_enc[ENC_KNOB3] = 1;
    app_pass();
    CTL_CHECK(game.loop_len == 16 && pending_loop == 24, "loop: playing, K3 did not wait for the loop (%u, pending %u)",
              game.loop_len, pending_loop);
    for (int i = 0; i < 400 && pending_loop; i++) audio_now += 2048u, schedule();
    CTL_CHECK(game.loop_len == 24 && ctl_len(0) == 24 && ctl_len(1) == 24 && game.playing,
              "loop: playing, 16 -> 24 as it came round: the loop %u, %u, %u", game.loop_len, ctl_len(0), ctl_len(1));
    rs_stop(&game);
}

static int ctl_main(void)
{
    ctl_knobs();
    ctl_swap();
    ctl_roll();
    ctl_loop();
    printf("ctl_test: %s\n", ctl_fails ? "FAILED" : "all pass");
    return ctl_fails != 0;
}
