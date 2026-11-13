/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Joakim (joakimskurk) */
/* The battle mode's screens (D42): the arena as it is heard, the beat on its frame, both robots' heat and rounds;
 * the countdown, a round won, and the match: the winner, big. Included after rs_screen.c and rs_splash.c. */

static const char *const BT_NAME[2] = {"SCOUT-7", "ECHO-3"};
static const uint8_t BT_KEY[2][5] = {{0, 1, 2, 4, 7}, {19, 20, 21, 23, 26}};   /* left, up, down, right; strike a white key on */
static const uint8_t BT_DIR[4] = {RS_W, RS_N, RS_S, RS_E};
#define BT_RESET 13u                                    /* F#4, the keyboard's middle: the match again (D42) */

static void bt_banner(const char *big, const char *small, uint16_t c)   /* a band over the arena, its words in it */
{
    int32_t h = small ? 58 : 44, y = 120 - h / 2;
    cv_rect(0, y, 240, h, RGB(0x05, 0x10, 0x10));
    cv_rect(0, y, 240, 2, c), cv_rect(0, y + h - 2, 240, 2, c);
    cv_text(120 - text_w(&FONT_L, big) / 2, y + 6, &FONT_L, big, c);
    if (small) cv_text(120 - text_w(&FONT_S, small) / 2, y + 38, &FONT_S, small, C_TEXT);
}

static void bt_chevron(int32_t cx, int32_t cy, uint32_t dir, uint16_t c)   /* a chevron pointing dir, its tip at cx, cy:
                                                                          * 3 px thick, 7 px each side, outlined, its
                                                                          * front edge lit (the step icons' way) */
{
    uint16_t lit = mix565(C_WHITE, c, 120);
    for (int pass = 0; pass < 2; pass++)        /* the outline under it all, then the body */
        for (int32_t i = -7; i <= 7; i++)
            for (int32_t d = 0; d < 3; d++) {
                int32_t a = -(i < 0 ? -i : i) - d, x, y;   /* back from the tip as it widens, d into the stroke */
                if (dir == RS_E) x = cx + a, y = cy + i;
                else if (dir == RS_W) x = cx - a, y = cy + i;
                else if (dir == RS_S) x = cx + i, y = cy + a;
                else x = cx + i, y = cy - a;
                if (pass == 0) cv_rect(x - 1, y - 1, 3, 3, RGB(0x05, 0x05, 0x0c));
                else cv_pset(x, y, d == 0 ? lit : c);
            }
}

static void bt_corners(int on_beat)             /* the arena's corners, as the splash's: Scout-7's side red, Echo-3's
                                                 * orange; lit on the beat */
{
    static const int32_t X0 = BX - 2, Y0 = BY - 2, S = 5 * CELL + 4, ARM = 22, T = 3;   /* (a pixel into the arena) */
    for (int b = 0; b < 2; b++) {
        uint16_t c = b ? C_BOT_B : C_BOT_A;
        int32_t x = b ? X0 + S - ARM : X0, xv = b ? X0 + S - T : X0;
        if (!on_beat) c = mix565(c, C_BG, 110);
        cv_rect(x, Y0, ARM, T, c), cv_rect(xv, Y0, T, ARM, c);                  /* the top corner */
        cv_rect(x, Y0 + S - T, ARM, T, c), cv_rect(xv, Y0 + S - ARM, T, ARM, c);   /* the bottom one */
    }
}

static void bt_score(int b, uint32_t wins, uint32_t to_win)   /* a robot's score bar down its side of the arena (where
                                                               * HOME shows its register): a segment a round to win,
                                                               * lit from the bottom as it takes them */
{
    int32_t x = b ? 222 : 1, top = BY, h = 5 * CELL, n = (int32_t)(to_win ? to_win : 1), gap = 3, seg = (h - gap * (n - 1)) / n;
    uint16_t c = b ? C_BOT_B : C_BOT_A;
    for (int32_t i = 0; i < n; i++) {
        int32_t y = top + h - (i + 1) * seg - i * gap;
        int lit = (uint32_t)i < wins;
        cv_rect(x, y, 17, seg, lit ? c : mix565(c, C_BG, 36));
        frame(x, y, 17, seg, 1, lit ? mix565(C_WHITE, c, 90) : mix565(c, C_BG, 130));
    }
}

static uint16_t bt_key_colour(uint32_t k, uint32_t held)   /* a battle key's colour, lit while held; 0 a key at rest */
{
    for (int p = 0; p < 2; p++)
        for (int i = 0; i < 5; i++)
            if (BT_KEY[p][i] == k) {
                uint16_t c = i == 4 ? (p ? C_WARN : C_ACCENT) : p ? C_BOT_B : C_BOT_A;
                return held >> k & 1u ? mix565(C_WHITE, c, 100) : mix565(c, C_BG, 120);
            }
    if (k == BT_RESET) return held >> k & 1u ? C_WHITE : C_MUTED;
    return 0;
}

static void bt_keymap(int32_t y, uint32_t held)  /* the two players' keys on the little keyboard, lit as they are held */
{
    uint16_t col[27] = {0};
    for (uint32_t k = 0; k < 27u; k++) col[k] = bt_key_colour(k, held);
    draw_keymap(y, col);
}

static void bt_mark(int32_t cx, int32_t cy, uint32_t k, uint16_t c)   /* what a battle key does: a way, the strike, reset */
{
    for (int p = 0; p < 2; p++)
        for (int i = 0; i < 5; i++)
            if (BT_KEY[p][i] == k) {
                if (i < 4) {                            /* an arrow: a wedge and its shaft */
                    static const uint8_t WAY[4] = {RS_W, RS_N, RS_S, RS_E};
                    uint8_t d = WAY[i];
                    draw_arrow_at(cx + (d == RS_E ? 2 : d == RS_W ? -2 : 0), cy + (d == RS_S ? 2 : d == RS_N ? -2 : 0), d, c);
                    if (d == RS_E || d == RS_W) cv_rect(cx - 3 + (d == RS_E ? -1 : 2), cy - 1, 4, 3, c);
                    else cv_rect(cx - 1, cy - 3 + (d == RS_S ? -1 : 2), 3, 4, c);
                } else {                                /* the strike: a burst */
                    cv_rect(cx - 4, cy - 1, 9, 3, c), cv_rect(cx - 1, cy - 4, 3, 9, c);
                    cv_rect(cx - 3, cy - 3, 2, 2, c), cv_rect(cx + 2, cy - 3, 2, 2, c), cv_rect(cx - 3, cy + 2, 2, 2, c), cv_rect(cx + 2, cy + 2, 2, 2, c);
                }
                return;
            }
    if (k == BT_RESET) cv_text(cx - 4, cy - 8, &FONT_S, "R", c);
}

static void bt_bigkeys(int32_t x0, int32_t y, uint32_t held)   /* the keyboard, big: the players' keys marked and lit */
{
    const int32_t W = 14, H = 52, BW = 10, BH = 30;
    uint32_t k, w = 0;
    for (k = 0; k < 27u; k++)                   /* the white keys */
        if (!((0x54Au >> ((53u + k) % 12u)) & 1u)) {
            int32_t x = x0 + (int32_t)w * W;
            uint16_t c = bt_key_colour(k, held);
            cv_rect(x, y, W - 1, H, c ? c : RGB(0x2a, 0x3c, 0x3a));
            if (c) bt_mark(x + W / 2 - 1, y + H - 11, k, RGB(0x05, 0x10, 0x10));
            w++;
        }
    for (k = 0, w = 0; k < 27u; k++)            /* the black keys over the lines between */
        if ((0x54Au >> ((53u + k) % 12u)) & 1u) {
            int32_t x = x0 + (int32_t)w * W - BW / 2 - 1;
            uint16_t c = bt_key_colour(k, held);
            cv_rect(x - 1, y, BW + 2, BH + 1, RGB(0x05, 0x10, 0x10));
            cv_rect(x, y, BW, BH, c ? c : RGB(0x10, 0x1c, 0x1c));
            if (c) bt_mark(x + BW / 2, y + BH - 9, k, RGB(0x05, 0x10, 0x10));
        } else
            w++;
}

static void bt_life(int32_t x, int b, int32_t hp)   /* a robot's life in the header: six pips */
{
    uint16_t c = hp <= 2 ? C_DANGER : hp <= 4 ? C_WARN : C_PULSE;
    for (int32_t k = 0; k < 6; k++) cv_rect(x + k * 7, 5, 5, 8, k < hp ? c : RGB(0x1a, 0x1a, 0x2e));
    (void)b;
}

static void bt_winner(const struct rs_battle *t)
{
    int w = t->winner & 1;
    uint16_t c = w ? C_BOT_B : C_BOT_A;
    char score[8] = "0 - 0", line[24];
    cv_begin(240, 240, mix565(c, C_BG, 40));
    for (int32_t r = 0; r < 6; r++) frame(8 + r * 6, 8 + r * 6, 224 - r * 12, 160 - r * 12, 1, mix565(c, C_BG, 60 + r * 25));
    big_bot_px(56, 22, w, 4);                    /* the winner, 128 x 128 */
    int i = 0;
    for (const char *n = BT_NAME[w]; *n; n++) line[i++] = *n;
    line[i] = 0;
    cv_text(120 - text_w(&FONT_L, line) / 2, 160, &FONT_L, line, c);
    cv_text(120 - text_w(&FONT_L, "WINS!") / 2, 188, &FONT_L, "WINS!", C_WHITE);
    score[0] = (char)('0' + t->wins[0] % 10u), score[4] = (char)('0' + t->wins[1] % 10u);
    cv_text(4, 224, &FONT_S, score, C_TEXT);
    cv_text(236 - text_w(&FONT_S, "PLAY: AGAIN"), 224, &FONT_S, "PLAY: AGAIN", C_MUTED);
    screen_out();
}

static void draw_battle(const struct rs_view *v, const struct rs_fx *fx, uint32_t now_ms, uint8_t to_win, uint8_t arena,
                        uint32_t held)
{
    const struct rs_battle *t = &v->battle;
    int beat = v->loop_len % 3 == 0 && v->loop_len % 4 != 0 ? 3 : 4, b, live = v->playing && t->phase != RS_BT_OFF;
    uint32_t goal = live ? t->to_win : to_win;
    if (live && t->phase == RS_BT_MATCH) { bt_winner(t); return; }
    cv_begin(240, 240, C_BG);
    /* the header: the names and their lives, the round between; down the sides the score bars */
    cv_text(4, 1, &FONT_S, BT_NAME[0], C_BOT_A);
    cv_text(236 - text_w(&FONT_S, BT_NAME[1]), 1, &FONT_S, BT_NAME[1], C_BOT_B);
    for (int r = 0; r < 2; r++) {
        int32_t heat = v->bot[r].heat < 0 ? 0 : v->bot[r].heat > 6 ? 6 : v->bot[r].heat;
        bt_life(r ? 236 - text_w(&FONT_S, BT_NAME[1]) - 46 : 64, r, live ? 6 - heat : 6);
    }
    {
        char r[4] = "R1";
        r[1] = (char)('0' + (live ? t->round : 1) % 10u);
        int hot = live && t->phase == RS_BT_FIGHT && t->beats >= RS_BT_OVERHEAT;
        const char *mid = hot ? "HOT" : live ? r : "VS";
        cv_text(120 - text_w(&FONT_S, mid) / 2, 1, &FONT_S, mid, hot ? C_DANGER : C_MUTED);
    }
    bt_score(0, live ? t->wins[0] : 0, goal), bt_score(1, live ? t->wins[1] : 0, goal);
    /* the arena; its frame flashes on the beat (what the robots step to): the line mint, the splash's corners lit in
     * each robot's colour on its side */
    int on_beat = live && t->phase == RS_BT_FIGHT && v->step >= 0 && v->step % beat == 0;
    frame(BX - 2, BY - 2, 5 * CELL + 4, 5 * CELL + 4, 2, on_beat ? mix565(C_ACCENT, C_BG, 170) : mix565(C_MUTED, C_BG, 90));
    bt_corners(on_beat);
    for (int i = 0; i < RS_ROWS * RS_COLS; i++) {
        int32_t x = BX + (i % RS_COLS) * CELL, y = BY + (i / RS_COLS) * CELL;
        draw_tile(x, y, &v->board.t[i]);
        if (live && rs_battle_hot(t, i % RS_COLS, i / RS_COLS)) {   /* the overheat: the glowing tiles, red */
            for (int32_t k = 1; k < CELL - 1; k++)
                for (int32_t j = 1; j < CELL - 1; j++) {
                    uint16_t *px = &cv_px[(uint32_t)(y + k) * cv_w + (uint32_t)(x + j)];
                    *px = swap16(mix565(C_DANGER, swap16(*px), (k + j + (int32_t)(v->step & 3) * 4) % 16 < 8 ? 110 : 70));
                }
        }
    }
    int together = v->bot[0].x == v->bot[1].x && v->bot[0].y == v->bot[1].y;
    for (b = 0; b < 2; b++) {
        const struct rs_bot *r = &v->bot[b];
        int32_t x = BX + r->x * CELL, y = BY + r->y * CELL, sx = together ? x + (b ? 20 : 0) : x + 10;
        if ((int32_t)(fx->hidden_until[b] - now_ms) > 0 || (live && t->phase == RS_BT_ROUND && r->dying)) continue;
        dim_cell(x, y);
        draw_bot(sx + 1, y + 16, b, 1, C_CELL);
        draw_arrow_at(sx + 10, y + 13, r->dir, b ? C_WARN : C_ACCENT);
        if (live && t->did[b] == RS_BT_STRIKE && v->step % beat == 0) {   /* the strike: chevrons the way it faces, in
                                                                           * its colour, brighter to the front; a hit's
                                                                           * front one white */
            int32_t cx = x + CELL / 2, cy = y + CELL / 2, dx = DX[t->sdir[b] & 3], dy = DY[t->sdir[b] & 3];
            uint16_t c = b ? C_BOT_B : C_BOT_A;
            for (int32_t n = 0; n < 3; n++) {
                uint16_t cn = n == 0 ? mix565(c, C_CELL, 150) : n == 1 ? c : t->hit[b] ? C_WHITE : mix565(C_WHITE, c, 80);
                int32_t k = 22 + 10 * n;            /* the tips 22, 32 and 42 px on: to the next tile's middle */
                bt_chevron(cx + dx * k, cy + dy * k, t->sdir[b] & 3u, cn);
            }
        }
        if ((int32_t)(fx->clash_until[b] - now_ms) > 0) draw_clash(sx, y + 1, 2);   /* hit: the star */
    }
    for (b = 0; b < 2; b++)
        if ((int32_t)(fx->boom_until[b] - now_ms) > 0) draw_explosion(BX + v->bot[b].x * CELL, BY + v->bot[b].y * CELL, b);
    if (live) bt_keymap(222, held);             /* the foot: the players' keys, lit as they are held */
    /* what is happening */
    if (!live) {                                /* stopped: how to play, the match's settings */
        static const char *const AR[RS_NARENAS] = {"SAME", "NEXT ONE", "RANDOM"};
        char ro[4] = "3", arena_name[12];
        const char *an = v->board_id < RS_NBOARDS + 2 ? rs_board_name[v->board_id] : "?";
        int i = 0;
        for (; an[i] && i < 11; i++) arena_name[i] = (char)(an[i] >= 'a' && an[i] <= 'z' ? an[i] - 32 : an[i]);
        arena_name[i] = 0;
        ro[0] = (char)('0' + to_win % 10u);
        cv_rect(4, 24, 232, 186, RGB(0x05, 0x10, 0x10));
        frame(4, 24, 232, 186, 1, C_MUTED);
        cv_text(120 - text_w(&FONT_L, "BATTLE") / 2, 28, &FONT_L, "BATTLE", C_WHITE);
        bt_bigkeys(8, 66, held);                    /* the keys, marked: the ways, the strike, reset in the middle */
        cv_text(18, 124, &FONT_S, "SCOUT-7", C_BOT_A), cv_text(222 - text_w(&FONT_S, "ECHO-3"), 124, &FONT_S, "ECHO-3", C_BOT_B);
        cv_text(120 - text_w(&FONT_S, "R RESET") / 2, 124, &FONT_S, "R RESET", C_MUTED);
        cv_rect(18, 141, 204, 1, mix565(C_MUTED, C_BG, 90));
        cv_text(18, 146, &FONT_S, "K2 ROUNDS TO WIN", C_MUTED), cv_text(222 - text_w(&FONT_S, ro), 146, &FONT_S, ro, C_WHITE);
        cv_text(18, 160, &FONT_S, "K3 ARENA", C_MUTED), cv_text(222 - text_w(&FONT_S, arena_name), 160, &FONT_S, arena_name, C_WHITE);
        cv_text(18, 174, &FONT_S, "K4 EACH ROUND", C_MUTED);
        cv_text(222 - text_w(&FONT_S, AR[arena % RS_NARENAS]), 174, &FONT_S, AR[arena % RS_NARENAS], C_WHITE);
        cv_text(120 - text_w(&FONT_S, "PLAY: FIGHT") / 2, 190, &FONT_S, "PLAY: FIGHT", C_ACCENT);
    } else if (t->phase == RS_BT_READY) {       /* the countdown: the round, then 4 3 2 1 */
        char r[12] = "ROUND 1", d[2] = "4";
        r[6] = (char)('0' + t->round % 10u), d[0] = (char)('0' + (t->count < 5 ? t->count : 4));
        if (t->count >= 5) bt_banner(r, "HOLD A WAY: A STEP A BEAT", C_WHITE);
        else bt_banner(d, r, t->count == 1 ? C_DANGER : t->count == 2 ? C_WARN : C_WHITE);
    } else if (t->phase == RS_BT_FIGHT && t->beats < 1) {
        bt_banner("FIGHT!", 0, C_ACCENT);
    } else if (t->phase == RS_BT_ROUND) {        /* a round taken, or a draw */
        char r[24] = "TAKES ROUND 1";
        r[12] = (char)('0' + t->round % 10u);
        if (t->winner < 2) bt_banner(BT_NAME[t->winner], t->coin ? "TAKES IT ON A COIN" : r, t->winner ? C_BOT_B : C_BOT_A);
        else bt_banner("DRAW", "BOTH BURNED OUT", C_WHITE);
    }
    screen_out();
}
