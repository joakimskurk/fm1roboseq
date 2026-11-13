/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Joakim (joakimskurk) */
/* P4 (ROADMAP): the playability rules P1-P3 as a test, page by page. Built into firmware/src/roboseq.c on the host
 * (tests/run_host.sh: -DRS_P4_TEST), so it presses the app's own keys on its own pages, while the robots play:
 *
 *   P1  something to play at every moment: on every page keys change the game, the music or the page (11 at least,
 *       the black keys' worth), the music running;
 *   P2  the robots steered live: on every page but the builders and the battle the black keys steer (the next move
 *       of each robot, and the swap); on the builders the keys change the robots' world (their programs, their
 *       board), in the battle the players move them;
 *   P3  unorthodox, not unclear: a key that does something shows it on the screen or is heard on the next step.
 *
 * Exits 0 when all hold. */

static struct rs_state p4_game;                         /* the state before a key, to go back to */
static struct rs_lead p4_lead;
static uint8_t p4_page, p4_cur[8];
static uint16_t p4_px[240 * 240];
static int p4_fails;
#define P4_CHECK(c, ...) do { if (!(c)) { p4_fails++; printf("FAIL p4_test: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void p4_save(void)
{
    p4_game = game, p4_lead = mix.lead, p4_page = page;
    p4_cur[0] = scl_cursor, p4_cur[1] = arp_cursor, p4_cur[2] = drum_row, p4_cur[3] = mix_cursor;
    p4_cur[4] = (uint8_t)seq_cursor, p4_cur[5] = (uint8_t)env_cx, p4_cur[6] = (uint8_t)env_cy, p4_cur[7] = sel_bot;
}

static void p4_restore(void)
{
    game = p4_game, mix.lead = p4_lead, page = p4_page;
    scl_cursor = p4_cur[0], arp_cursor = p4_cur[1], drum_row = p4_cur[2], mix_cursor = p4_cur[3];
    seq_cursor = p4_cur[4], env_cx = p4_cur[5], env_cy = p4_cur[6], sel_bot = p4_cur[7];
    lead_on = 0, arp_mode = 0, held_n = 0, bt_bpm_was = 0, swap_until = 0;
    memset(bt_last, 0, sizeof bt_last), memset(bt_note_on, 0, sizeof bt_note_on);   /* (each key alone: none held before) */
}

static int p4_changed(void)                             /* the game, the lead, the page or a cursor moved */
{
    return memcmp(&game, &p4_game, sizeof game) || memcmp(&mix.lead, &p4_lead, sizeof mix.lead) || page != p4_page ||
           scl_cursor != p4_cur[0] || arp_cursor != p4_cur[1] || drum_row != p4_cur[2] || mix_cursor != p4_cur[3] ||
           seq_cursor != p4_cur[4] || env_cx != p4_cur[5] || env_cy != p4_cur[6] || sel_bot != p4_cur[7];
}

static void p4_draw(uint32_t notes, uint16_t *out)       /* the page as the app draws it, into out */
{
    static struct rs_view v;
    static struct rs_fx fx;
    static struct rs_ui ui;
    memset(&fx, 0, sizeof fx);
    view_of(&v, &tick_out, 0);
    draw_page(&v, &fx, &ui, notes);
    memcpy(out, cv_px, sizeof p4_px);
}

static void p4_press(uint32_t k)                        /* a key down, as the main loop has it */
{
    if (page == PAGE_BATTLE) battle_keys(1u << k, 1u << k);
    else keys(1u << k);
}

static int p4_heard(void)                               /* the next step from here unlike the next step without the key */
{
    static struct rs_state a, b;
    static struct rs_tick_out oa, ob;
    a = game, b = p4_game;
    rs_tick(&a, &oa), rs_tick(&b, &ob);
    return memcmp(a.bot, b.bot, sizeof a.bot) || oa.nnotes != ob.nnotes ||
           memcmp(oa.notes, ob.notes, sizeof oa.notes[0] * oa.nnotes);
}

static void p4_page_test(uint8_t pg, const char *name)
{
    static uint16_t before[240 * 240];
    int effect = 0, steer = 0, world = 0, unshown = 0;
    page = pg;
    if (pg == PAGE_BATTLE) {                            /* a match fighting */
        rs_battle_start(&game, 3, RS_ARENA_SAME);
        for (int t = 0; t < 64 && game.battle.phase != RS_BT_FIGHT; t++) rs_tick(&game, &tick_out);
    }
    p4_save();
    p4_draw(0, before);
    for (uint32_t k = 0; k < 27u; k++) {
        p4_restore();
        p4_press(k);
        if (!p4_changed()) continue;
        effect++;
        int is_black = (0x54Au >> ((53u + k) % 12u)) & 1u;
        if (is_black && (memcmp(game.steer, p4_game.steer, sizeof game.steer) || game.swapped != p4_game.swapped ||
                         memcmp(game.prog, p4_game.prog, sizeof game.prog))) steer++;   /* (F#5: the programs swapped, D45) */
        if (memcmp(game.prog, p4_game.prog, sizeof game.prog) || memcmp(&game.board, &p4_game.board, sizeof game.board) ||
            memcmp(game.battle.hold, p4_game.battle.hold, sizeof game.battle.hold))
            world++;
        p4_draw(page == PAGE_BATTLE ? 1u << k : 0, p4_px);
        int shown = memcmp(before, p4_px, sizeof p4_px) != 0 || page != p4_page;
        if (!shown && !p4_heard()) unshown++, printf("p4_test: %s: key %u does something, not shown, not heard\n", name, k);
    }
    p4_restore();
    printf("p4_test: %-7s %2d keys do something, %2d steer, %2d change the robots' world, %d unclear\n", name, effect, steer, world, unshown);
    P4_CHECK(effect >= 11, "P1 %s: only %d keys do something while it plays", name, effect);
    if (pg == PAGE_SEQ) P4_CHECK(world >= 10, "P2 %s: the keys change the robots' programs (%d)", name, world);
    else if (pg == PAGE_ENV) P4_CHECK(world >= 6, "P2 %s: the keys change the robots' board (%d)", name, world);
    else if (pg == PAGE_BATTLE) P4_CHECK(world >= 8, "P2 %s: the players move the robots (%d)", name, world);
    else P4_CHECK(steer == 11, "P2 %s: the eleven black keys steer (%d)", name, steer);
    P4_CHECK(unshown == 0, "P3 %s: %d keys that do something are neither shown nor heard", name, unshown);
    if (pg == PAGE_BATTLE) rs_battle_stop(&game);
}

#include "ctl_test.c"                           /* the controls as they were played (D45) */

static int p4_main(void)
{
    static const struct { uint8_t page; const char *name; } PAGES[] = {
        {PAGE_HOME, "HOME"}, {PAGE_SEQ, "SEQ"}, {PAGE_GLO, "GLO"}, {PAGE_SCL, "SCL"}, {PAGE_ARP, "SEQ 2"},
        {PAGE_LFO, "LFO"}, {PAGE_FX, "FX"}, {PAGE_BOARDS, "BOARDS"}, {PAGE_ENV, "BUILDER"}, {PAGE_BATTLE, "BATTLE"}};
    rs_mix_init(&mix, 0x5EEDu);
    rs_default(&game);
    game.engine = RS_ENGINE_BAND;
    keys_init();
    play();
    for (int t = 0; t < 21; t++) rs_tick(&game, &tick_out);   /* (the robots a few steps in) */
    for (unsigned i = 0; i < sizeof PAGES / sizeof PAGES[0]; i++) p4_page_test(PAGES[i].page, PAGES[i].name);
    printf("p4_test: %s\n", p4_fails ? "FAILED" : "all pass");
    int ctl = ctl_main();                       /* (the controls as they were played: tests/roboseq/ctl_test.c) */
    return p4_fails != 0 || ctl;
}
