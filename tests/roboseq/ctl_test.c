/* SPDX-License-Identifier: GPL-3.0-only */
/* The controls, as the product owner played them and found them wrong (2026-11-10, D45): each a check on the app's
 * own passes and keys, after tests/roboseq/p4_test.c (included at its end, built into roboseq.c with -DRS_P4_TEST):
 *
 *   knobs    a knob a page does not read is let go, not kept for the next page (K3 on HOME was GLO's loop later)
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

static int ctl_main(void)
{
    ctl_knobs();
    printf("ctl_test: %s\n", ctl_fails ? "FAILED" : "all pass");
    return ctl_fails != 0;
}
