/* SPDX-License-Identifier: GPL-3.0-only */
/* The battle mode's screens (D42): the arena as it is heard, the beat on its frame, both robots' heat and rounds;
 * the countdown, a round won, and the match: the winner, big. Included after rs_screen.c and rs_splash.c. */

static const char *const BT_NAME[2] = {"SCOUT-7", "ECHO-3"};
static const uint8_t BT_KEY[2][5] = {{0, 1, 2, 4, 6}, {19, 20, 21, 23, 24}};   /* the keys: left, up, down, right, strike */
static const uint8_t BT_DIR[4] = {RS_W, RS_N, RS_S, RS_E};

static void bt_banner(const char *big, const char *small, uint16_t c)   /* a band over the arena, its words in it */
{
    int32_t h = small ? 58 : 44, y = 120 - h / 2;
    cv_rect(0, y, 240, h, RGB(0x05, 0x10, 0x10));
    cv_rect(0, y, 240, 2, c), cv_rect(0, y + h - 2, 240, 2, c);
    cv_text(120 - text_w(&FONT_L, big) / 2, y + 6, &FONT_L, big, c);
    if (small) cv_text(120 - text_w(&FONT_S, small) / 2, y + 38, &FONT_S, small, C_TEXT);
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

static void bt_keymap(int32_t y)                 /* the two players' keys on the little keyboard */
{
    uint16_t col[27] = {0};
    for (int p = 0; p < 2; p++)
        for (int k = 0; k < 5; k++)
            col[BT_KEY[p][k]] = k == 4 ? C_WHITE : p ? C_BOT_B : C_BOT_A;
    draw_keymap(y, col);
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

static void draw_battle(const struct rs_view *v, const struct rs_fx *fx, uint32_t now_ms, uint8_t to_win, uint8_t arena)
{
    const struct rs_battle *t = &v->battle;
    int beat = v->loop_len % 3 == 0 && v->loop_len % 4 != 0 ? 3 : 4, b, live = v->playing && t->phase != RS_BT_OFF;
    uint32_t goal = live ? t->to_win : to_win;
    if (live && t->phase == RS_BT_MATCH) { bt_winner(t); return; }
    cv_begin(240, 240, C_BG);
    /* the header: the names, the round between; down the sides the score bars */
    cv_text(4, 1, &FONT_S, BT_NAME[0], C_BOT_A);
    cv_text(236 - text_w(&FONT_S, BT_NAME[1]), 1, &FONT_S, BT_NAME[1], C_BOT_B);
    {
        char r[12] = "ROUND 1";
        r[6] = (char)('0' + (live ? t->round : 1) % 10u);
        int hot = live && t->phase == RS_BT_FIGHT && t->beats >= RS_BT_OVERHEAT;
        const char *mid = hot ? "OVERHEAT" : live ? r : "VS";
        cv_text(120 - text_w(&FONT_S, mid) / 2, 1, &FONT_S, mid, hot ? C_DANGER : C_MUTED);
    }
    bt_score(0, live ? t->wins[0] : 0, goal), bt_score(1, live ? t->wins[1] : 0, goal);
    /* the arena; its frame flashes on the beat (what the robots step to) */
    int on_beat = live && t->phase == RS_BT_FIGHT && v->step >= 0 && v->step % beat == 0;
    frame(BX - 2, BY - 2, 5 * CELL + 4, 5 * CELL + 4, 2, on_beat ? C_WHITE : mix565(C_MUTED, C_BG, 90));
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
        if (live && t->did[b] == RS_BT_STRIKE && v->step % beat == 0) {   /* the strike: a flash the way it faces */
            int32_t cx = x + CELL / 2, cy = y + CELL / 2, dx = DX[t->sdir[b] & 3], dy = DY[t->sdir[b] & 3];
            uint16_t c = t->hit[b] ? C_WHITE : b ? C_BOT_B : C_BOT_A;
            for (int32_t k = 14; k < CELL + 10; k++) cv_rect(cx + dx * k - (dy ? 3 : 1), cy + dy * k - (dx ? 3 : 1), dy ? 7 : 3, dx ? 7 : 3, c);
        }
        if ((int32_t)(fx->clash_until[b] - now_ms) > 0) draw_clash(sx, y + 1, 2);   /* hit: the star */
    }
    for (b = 0; b < 2; b++)
        if ((int32_t)(fx->boom_until[b] - now_ms) > 0) draw_explosion(BX + v->bot[b].x * CELL, BY + v->bot[b].y * CELL, b);
    /* the foot: each robot's life (6, less its heat) */
    for (b = 0; b < 2; b++) {
        int32_t heat = v->bot[b].heat < 0 ? 0 : v->bot[b].heat > 6 ? 6 : v->bot[b].heat, hp = live ? 6 - heat : 6, x = b ? 124 : 4;
        uint16_t c = hp <= 2 ? C_DANGER : hp <= 4 ? C_WARN : C_PULSE;
        for (int32_t k = 0; k < 6; k++) cv_rect(x + k * 19, 226, 17, 10, k < hp ? c : RGB(0x1a, 0x1a, 0x2e));
        if (b) cv_rect(x, 224, 112, 1, C_BOT_B);
        else cv_rect(x, 224, 112, 1, C_BOT_A);
    }
    /* what is happening */
    if (!live) {                                /* stopped: how to play, the match's settings */
        static const char *const AR[RS_NARENAS] = {"SAME", "NEXT ONE", "RANDOM"};
        char ro[4] = "3", arena_name[12];
        const char *an = v->board_id < RS_NBOARDS + 2 ? rs_board_name[v->board_id] : "?";
        int i = 0;
        for (; an[i] && i < 11; i++) arena_name[i] = (char)(an[i] >= 'a' && an[i] <= 'z' ? an[i] - 32 : an[i]);
        arena_name[i] = 0;
        ro[0] = (char)('0' + to_win % 10u);
        cv_rect(10, 26, 220, 176, RGB(0x05, 0x10, 0x10));
        frame(10, 26, 220, 176, 1, C_MUTED);
        cv_text(120 - text_w(&FONT_L, "BATTLE") / 2, 30, &FONT_L, "BATTLE", C_WHITE);
        cv_text(18, 66, &FONT_S, "SCOUT-7 F3 F#3 G3 A3", C_BOT_A), cv_text(18, 80, &FONT_S, "  STRIKE / PLAY B3", C_BOT_A);
        cv_text(18, 96, &FONT_S, "ECHO-3  C5 C#5 D5 E5", C_BOT_B), cv_text(18, 110, &FONT_S, "  STRIKE / PLAY F5", C_BOT_B);
        cv_rect(18, 126, 204, 1, mix565(C_MUTED, C_BG, 90));
        cv_text(18, 132, &FONT_S, "K2 ROUNDS TO WIN", C_MUTED), cv_text(222 - text_w(&FONT_S, ro), 132, &FONT_S, ro, C_WHITE);
        cv_text(18, 146, &FONT_S, "K3 ARENA", C_MUTED), cv_text(222 - text_w(&FONT_S, arena_name), 146, &FONT_S, arena_name, C_WHITE);
        cv_text(18, 160, &FONT_S, "K4 EACH ROUND", C_MUTED);
        cv_text(222 - text_w(&FONT_S, AR[arena % RS_NARENAS]), 160, &FONT_S, AR[arena % RS_NARENAS], C_WHITE);
        cv_text(120 - text_w(&FONT_S, "PLAY: FIGHT") / 2, 182, &FONT_S, "PLAY: FIGHT", C_ACCENT);
        bt_keymap(204);
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
