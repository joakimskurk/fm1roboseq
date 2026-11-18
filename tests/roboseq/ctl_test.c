/* SPDX-License-Identifier: GPL-3.0-only */
/* The controls, as the product owner played them and found them wrong (2026-11-10, D45): each a check on the app's
 * own passes and keys, after tests/roboseq/p4_test.c (included at its end, built into roboseq.c with -DRS_P4_TEST):
 *
 *   knobs    a knob a page does not read is let go, not kept for the next page (K3 on HOME was GLO's loop later)
 *   swap     F#5 (HOME) and F5 (SEQ) swap the robots' programs, steps and lengths; SEQ's undo brings them back
 *   roll     on HOME, K2 rolls the picked robot's program, K3 both, a roll a turn, while it plays; anticlockwise
 *            back through the rolls, clockwise forward again (D47), an edit made after going back kept
 *   loop     a program as long as the loop follows GLO's K3 up and down, stopped or as the loop comes round;
 *            one made shorter stays (the HTML only cuts: a loop of 8 once left the programs at 8)
 *   quick    PRESETS the board and ALGORITHM the chords on every page but the battle, at once or queued (D47)
 *   keys     the key map while a page's own button is held 400 ms (off when let go, not on a short press), and
 *            a key's name in the header after a press (D46: the product owner could not tell what was what)
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
    quick_on = 0, rolls_n = 0, rolls_at = 0, toast_until = 0;
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

static void ctl_roll(void)                      /* new programs on HOME while it plays, and back and forward (D47) */
{
    ctl_fresh(PAGE_HOME);
    play();
    sel_bot = 1;                                /* (EDIT: Echo-3's on the keys) */
    struct rs_prog a[2], b[2], c[2];
    memcpy(a, game.prog, sizeof a);             /* a: as it starts */
    host_enc[ENC_KNOB2] = 1;
    app_pass();
    CTL_CHECK(!memcmp(&game.prog[0], &a[0], sizeof a[0]) && memcmp(&game.prog[1], &a[1], sizeof a[1]),
              "roll: K2 on HOME did not roll the picked robot's program (and only it)");
    memcpy(b, game.prog, sizeof b);             /* b: Echo-3 rolled */
    host_enc[ENC_KNOB3] = 2;
    app_pass();
    CTL_CHECK(memcmp(&game.prog[0], &b[0], sizeof b[0]) && memcmp(&game.prog[1], &b[1], sizeof b[1]), "roll: K3 on HOME did not roll both");
    CTL_CHECK(game.playing, "roll: a roll stopped the music");
    memcpy(c, game.prog, sizeof c);             /* c: both rolled */
    host_enc[ENC_KNOB3] = -1;
    app_pass();
    CTL_CHECK(!memcmp(game.prog, b, sizeof b), "roll: K3 anticlockwise did not go back to the roll before");
    host_enc[ENC_KNOB2] = -3;                   /* (a turn is one step, whatever its size) */
    app_pass();
    CTL_CHECK(!memcmp(game.prog, a, sizeof a), "roll: K2 anticlockwise did not go back to the start");
    host_enc[ENC_KNOB2] = -1;
    app_pass();
    CTL_CHECK(!memcmp(game.prog, a, sizeof a), "roll: back past the start moved the programs");
    host_enc[ENC_KNOB2] = 1;
    app_pass();
    CTL_CHECK(!memcmp(game.prog, b, sizeof b), "roll: K2 clockwise did not go forward again");
    host_enc[ENC_KNOB3] = 1;
    app_pass();
    CTL_CHECK(!memcmp(game.prog, c, sizeof c) && !strncmp(toast, "FORWARD 3/3", 11), "roll: forward to the last (\"%s\")", toast);
    host_enc[ENC_KNOB2] = 1;                    /* at the last: a new one */
    app_pass();
    CTL_CHECK(memcmp(game.prog, c, sizeof c) && rolls_n == 4 && rolls_at == 3, "roll: at the last, K2 did not roll anew");
    host_enc[ENC_KNOB2] = -1;
    app_pass();
    rs_set_step(&game, 0, 0, RS_PING, -1, -1);  /* an edit after going back: what was ahead goes, the edit is kept */
    host_enc[ENC_KNOB2] = -1;
    app_pass();
    host_enc[ENC_KNOB2] = 1;
    app_pass();
    CTL_CHECK(game.prog[0].s[0].action == RS_PING && rolls_at + 1u == rolls_n, "roll: an edit after going back was lost");
    page = PAGE_SEQ;
    keys(1u << 20);                             /* SEQ's undo still undoes */
    CTL_CHECK(game.prog[0].s[0].action != RS_PING || memcmp(game.prog, c, sizeof c), "roll: SEQ's undo did nothing");
    toast_until = 0;
    rs_stop(&game);
}

static void ctl_roll_full(void)                 /* more rolls than the history keeps: the oldest go, the rest in order */
{
    struct rs_prog keep[ROLLS][2];
    int i;
    ctl_fresh(PAGE_HOME);
    for (i = 0; i < ROLLS + 3; i++) {
        host_enc[ENC_KNOB3] = 1;
        app_pass();
        if (i >= 3) memcpy(keep[i - 3], game.prog, sizeof keep[0]);
    }
    CTL_CHECK(rolls_n == ROLLS && rolls_at == ROLLS - 1, "roll: a full history kept %u, at %u", rolls_n, rolls_at);
    for (i = ROLLS - 2; i >= 0; i--) {
        host_enc[ENC_KNOB2] = -1;
        app_pass();
        CTL_CHECK(!memcmp(game.prog, keep[i], sizeof keep[0]), "roll: a full history, back to %d was not that roll", i);
    }
    host_enc[ENC_KNOB2] = -1;
    app_pass();
    CTL_CHECK(!memcmp(game.prog, keep[0], sizeof keep[0]) && rolls_at == 0, "roll: back past the oldest kept moved");
    toast_until = 0;
}

static void ctl_quick(void)                     /* PRESETS the board, ALGORITHM the chords (D47) */
{
    ctl_fresh(PAGE_HOME);
    host_enc[ENC_PRESETS] = 1;                  /* stopped: at once */
    app_pass();
    CTL_CHECK(game.board_id == RS_B_CORRIDOR && quick_on == QK_BOARDS, "quick: PRESETS stopped: board %u, shown %u",
              game.board_id, quick_on);
    play();
    host_enc[ENC_PRESETS] = 2;                  /* playing: queued for the loop */
    app_pass();
    CTL_CHECK(game.board_id == RS_B_CORRIDOR && game.pending_board == RS_B_GAUNTLET, "quick: PRESETS playing: %u, queued %u",
              game.board_id, game.pending_board);
    host_enc[ENC_PRESETS] = -4;                 /* back past the first: the user's, last in the round */
    app_pass();
    CTL_CHECK(game.pending_board == RS_B_USER, "quick: PRESETS did not go round to the user's (%u)", game.pending_board);
    rs_stop(&game);
    uint8_t was = game.prog_id;
    host_enc[ENC_ALGORITHM] = 1;                /* stopped: the next progression at once */
    app_pass();
    CTL_CHECK(game.prog_id == (was ? was + 1u : 1u) && quick_on == QK_CHORDS, "quick: ALGORITHM stopped: %u", game.prog_id);
    play();
    was = game.prog_id;
    host_enc[ENC_ALGORITHM] = 3;                /* playing: for the next round, the one now kept */
    app_pass();
    CTL_CHECK(game.prog_id == was && game.next_ready && game.next_id == was + 3u, "quick: ALGORITHM playing: %u, next %u (%u)",
              game.prog_id, game.next_id, game.next_ready);
    {
        rs_set_follow(&game, 0);                /* (FOLLOW off: the one picked comes all the same) */
        game.next_ready = 0;
        host_enc[ENC_ALGORITHM] = 3;
        app_pass();
        for (int i = 0; i < game.bassmod_len; i++) rs_advance_voice_mod(&game);   /* a round of Voice Mod */
        CTL_CHECK(game.prog_id == was + 3u, "quick: ALGORITHM's pick did not come in on the round (%u)", game.prog_id);
    }
    page = PAGE_BATTLE;                         /* the battle: neither */
    quick_on = 0;
    uint8_t board = game.board_id, pend = game.pending_board, next = game.next_id;
    host_enc[ENC_PRESETS] = 1, host_enc[ENC_ALGORITHM] = 1;
    app_pass();
    CTL_CHECK(game.board_id == board && game.pending_board == pend && game.next_id == next && !quick_on,
              "quick: PRESETS or ALGORITHM did something in the battle");
    page = PAGE_HOME;
    app_pass();                                 /* (and the turns made there are gone, not kept for HOME) */
    CTL_CHECK(game.pending_board == pend && game.next_id == next, "quick: a turn in the battle came through on HOME");
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

static void ctl_keys(void)                      /* (fm1_ms moved by hand and put back: no host script runs) */
{
    uint32_t ms = fm1_ms;
    ctl_fresh(PAGE_HOME);
    help_on = 0, fm1_in.buttons = 1u << BTN_HOME;   /* HOME held on HOME */
    keyhelp_poll(), fm1_ms += 200u, keyhelp_poll();
    CTL_CHECK(!help_on, "keys: the map came on a short press");
    fm1_ms += 250u;
    CTL_CHECK(keyhelp_poll() && help_on, "keys: HOME held 450 ms did not show the map");
    fm1_in.buttons = 0;
    CTL_CHECK(keyhelp_poll() && !help_on, "keys: the map stayed when HOME was let go");
    fm1_in.buttons = 1u << BTN_SEQ;             /* (another page's button, not this page's: no map) */
    keyhelp_poll(), fm1_ms += 500u, keyhelp_poll();
    CTL_CHECK(!help_on, "keys: SEQ held on HOME showed a map");
    fm1_in.buttons = 0, keyhelp_poll();
    fm1_ms = ms;
    page = PAGE_SEQ;                            /* a key's name: D5 on SEQ */
    keyhelp_say(1u << 21);
    CTL_CHECK(!strcmp(toast, "D5: ROLL SCOUT-7") && toast_until, "keys: D5 on SEQ said \"%s\"", toast);
    page = PAGE_HOME;
    rs_set_step(&game, 0, 4, RS_DASH, -1, -1);
    keyhelp_say(1u << 7);                       /* C4: the fifth white key, step 5 */
    CTL_CHECK(!strcmp(toast, "C4: STEP 5 DASH"), "keys: C4 on HOME said \"%s\"", toast);
    keyhelp_say(1u << 25);
    CTL_CHECK(!strcmp(toast, "F#5: SWAP PROGRAMS"), "keys: F#5 on HOME said \"%s\"", toast);
    page = PAGE_GLO;
    keyhelp_say(1u << 0);
    CTL_CHECK(!strcmp(toast, "F3: NOT USED HERE"), "keys: F3 on GLO said \"%s\"", toast);
    toast_until = 0;
}

static int ctl_main(void)
{
    ctl_knobs();
    ctl_swap();
    ctl_roll();
    ctl_loop();
    ctl_keys();
    ctl_roll_full();
    ctl_quick();
    printf("ctl_test: %s\n", ctl_fails ? "FAILED" : "all pass");
    return ctl_fails != 0;
}
