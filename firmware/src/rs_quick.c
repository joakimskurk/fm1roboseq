/* SPDX-License-Identifier: GPL-3.0-only */
/* Quick controls on the knobs no page had (D47), so nothing new crowds a page: each shows itself only while it turns.
 *   PRESETS    the board, on every page but the battle: the boards over the page as on ENV, the one coming framed
 *              yellow; it comes in as the loop comes round (at once, stopped)
 *   ALGORITHM  the chords: the named progressions over the page, one picked for the next round (at once, stopped)
 *   K2 / K3    on HOME, a history of the rolls: clockwise a new program (K2 the picked robot's, K3 both), or forward
 *              again to one turned back from; anticlockwise back to the programs before, a roll at a time
 * Main loop only. Included in roboseq.c after rs_keyhelp.c (the header's toast) and the app's state it reads. */

enum { QK_NONE, QK_BOARDS, QK_CHORDS };
static uint8_t quick_on;                        /* the overlay over the page, while its knob turns */
static uint32_t quick_until;

static void quick_show(uint8_t what) { quick_on = what, quick_until = (fm1_ms + 1200u) | 1u; }

static void quick_board(int32_t e)              /* PRESETS: the six presets, then the user's, round and round */
{
    const int32_t n = RS_NBOARDS + 1;
    int32_t at = game.pending_board != RS_B_NONE ? game.pending_board : game.board_id;
    at = at == RS_B_USER ? RS_NBOARDS : at >= RS_NBOARDS ? -1 : at;   /* (a rolled one: from the first) */
    at = ((at + e) % n + n) % n;
    rs_push_undo(&game);
    if (at == RS_NBOARDS) rs_load_user_board(&game);
    else rs_load_board(&game, (uint8_t)at);
    env_edited = 0;
}

static void quick_chords(int32_t e)             /* ALGORITHM: the named progressions 1 .. n, round and round */
{
    int32_t n = (int32_t)rs_nprogs();
    int32_t at = game.playing && game.next_ready ? game.next_id : game.prog_id;
    at = ((at - 1 + e) % n + n) % n + 1;
    if (game.playing) rs_queue_prog(&game, (uint8_t)at, rs_prog_home_key(&game));
    else rs_apply_prog(&game, (uint8_t)at, rs_prog_home_key(&game));
}

static void draw_chords_pick(void)              /* the progressions, the one picked framed, its neighbours round it */
{
    int32_t n = (int32_t)rs_nprogs(), sel = game.playing && game.next_ready ? game.next_id : game.prog_id, r;
    char t[32], key[4];
    cv_begin(240, 240, C_BG);
    cv_text(4, 0, &FONT_S, "CHORDS", C_ACCENT);
    put_num(t, (uint32_t)(sel ? sel : 0), 2), t[2] = '/', put_num(t + 3, (uint32_t)n, 2), t[5] = 0;
    cv_text(64, 0, &FONT_S, t, C_MUTED);
    {
        const char *when = !sel ? "" : game.playing ? (game.next_ready ? "NEXT ROUND" : "PLAYING") : "NOW";
        cv_text(236 - text_w(&FONT_S, when), 0, &FONT_S, when, C_WARN);
    }
    cv_rect(0, 16, 240, 1, mix565(C_ACCENT, C_BG, 90));
    for (r = -2; r <= 2; r++) {                 /* five rows, the picked one in the middle */
        int32_t id = sel ? ((sel - 1 + r) % n + n) % n + 1 : r + 3, y = 98 + r * 34;
        int on = r == 0 && sel;
        uint16_t c = on ? C_TEXT : mix565(C_MUTED, C_BG, r == -1 || r == 1 ? 200 : 120);
        if (on) cv_rect(2, y - 6, 236, 30, mix565(C_ACCENT, C_BG, 40)), frame(2, y - 6, 236, 30, 2, C_ACCENT);
        put_num(t, (uint32_t)id, 2), t[2] = 0;
        cv_text(10, y, &FONT_S, t, on ? C_ACCENT : c);
        cv_text(36, y, &FONT_S, rs_prog_name((uint8_t)id), c);
        rs_prog_key_name((uint8_t)id, rs_prog_home_key(&game), key);
        cv_text(232 - text_w(&FONT_S, key), y, &FONT_S, key, on ? C_WARN : c);
    }
    cv_rect(0, 200, 240, 1, mix565(C_MUTED, C_BG, 90));
    if (game.prog_id) {                         /* what plays now */
        char *p = t;
        const char *s = "NOW: ", *nm = rs_prog_name(game.prog_id);
        while (*s) *p++ = *s++;
        while (*nm && p < t + 24) *p++ = *nm++;
        *p = 0;
        cv_text(4, 214, &FONT_S, t, C_MUTED);
    } else
        cv_text(4, 214, &FONT_S, "NOW: CHORDS MADE BY HAND", C_MUTED);
    badge_steer[0] = badge_steer[1] = 0, badge_swap = 0, lead_badge = -99, toast_until = 0;   /* (the list alone) */
    screen_out();
}

static void draw_quick(void)
{
    if (quick_on == QK_BOARDS) {
        badge_steer[0] = badge_steer[1] = 0, badge_swap = 0, lead_badge = -99, toast_until = 0;
        draw_boards(&game, (fm1_ms / 300u) & 1u, env_edited, 1);
    } else
        draw_chords_pick();
}

/* ---- the rolls' history (HOME's K2 and K3): both robots' programs after each roll, and before the first */
#define ROLLS 8
static struct rs_prog rolls[ROLLS][2];
static uint8_t rolls_n, rolls_at;               /* how many kept; the one the programs are now */

static int rolls_here(void) { return rolls_n && !memcmp(rolls[rolls_at], game.prog, sizeof game.prog); }

static void rolls_add(void)                     /* the programs now, after the one we are at (what was ahead goes) */
{
    if (rolls_n && rolls_at + 1u < rolls_n) rolls_n = (uint8_t)(rolls_at + 1u);
    if (rolls_n == ROLLS) {                     /* full: the oldest goes (no memmove on the FM-1) */
        for (uint32_t i = 0; i + 1u < ROLLS; i++) memcpy(rolls[i], rolls[i + 1u], sizeof rolls[0]);
        rolls_n--;
    }
    memcpy(rolls[rolls_n], game.prog, sizeof game.prog);
    rolls_at = rolls_n++;
}

static void roll_turn(int32_t e, int both)      /* a turn of K2 (both 0) or K3 (1) on HOME, one step whatever its size */
{
    char *p = toast;
    const char *what;
    if (!rolls_here()) rolls_add();             /* (the programs as they are: one to come back to) */
    if (e < 0) {
        what = "BACK ";
        if (rolls_at > 0) rolls_at--, rs_push_undo(&game), memcpy(game.prog, rolls[rolls_at], sizeof game.prog);
    } else if (rolls_at + 1u < rolls_n) {
        what = "FORWARD ";
        rolls_at++, rs_push_undo(&game), memcpy(game.prog, rolls[rolls_at], sizeof game.prog);
    } else {
        what = both ? "NEW: BOTH " : sel_bot ? "NEW: ECHO-3 " : "NEW: SCOUT-7 ";
        if (both) rs_randomize_all(&game);
        else rs_randomize_steps(&game, (int)sel_bot);
        rolls_add();
    }
    while (*what) *p++ = *what++;               /* "BACK 2/5": where in the history the programs are */
    put_num(p, rolls_at + 1u, rolls_at + 1u >= 10u ? 2 : 1), p += rolls_at + 1u >= 10u ? 2 : 1;
    *p++ = '/';
    put_num(p, rolls_n, rolls_n >= 10u ? 2 : 1), p += rolls_n >= 10u ? 2 : 1;
    *p = 0;
    toast_until = (fm1_ms + 1000u) | 1u;
}
