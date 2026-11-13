/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Joakim (joakimskurk) */
/* The battle mode (D42): two players, one robot each, on the board's tiles and to the music's beat. Included at the
 * end of rs_model.c: the game's own moves (attempt_move's walls and crates) and tiles (apply_tile_effects: belts
 * carry, rotators turn, zap rails heat, pulse pads cool) are the battle's.
 *
 * On every beat each robot does what its player holds: a way held, it faces that way and steps there; the action
 * held (or pressed since the last beat) with the other in reach (a tile on, or two with the tile between open), it
 * strikes the way it faces instead (out of reach the key plays the lead: rs_battle_lead_note). Heat is the damage: a hit is +2 and
 * drives them apart, the other two tiles back (+1 for each a wall or a crate stops) and the striker one; a zap rail
 * +1 a beat, a pulse pad -1. After RS_BT_OVERHEAT beats of a round the arena shrinks: no pad cools, its edge glows
 * (+1 a beat), later all but the middle tile, and a hit is +3, so the two meet and the round ends. At 6
 * a robot burns out and the other takes the round; the first to `to_win` rounds takes the match. Between the beats
 * the robots stand: the music runs on, and its notes are what they do. */

int rs_battle_beat_steps(const struct rs_state *s) { return s->loop_len % 3 == 0 && s->loop_len % 4 != 0 ? 3 : 4; }

static void battle_pads(struct rs_state *s)             /* a new round: the robots at their pads, cool */
{
    for (int b = 0; b < 2; b++) {
        struct rs_bot *o = &s->bot[b];
        o->x = START[b].x, o->y = START[b].y, o->dir = START[b].dir;
        o->heat = 0, o->dying = 0, o->stepped_tile = RS_T_NONE;
        s->battle.guard[b] = 0, s->battle.rhits[b] = 0;
    }
    s->battle.phase = RS_BT_READY, s->battle.count = 5, s->battle.beats = 0;   /* (the round, then 4 3 2 1 on the beats) */
}

static void battle_arena(struct rs_state *s)            /* a new round's arena: the same, the next preset, or a roll */
{
    if (s->battle.arena == RS_ARENA_NEXT) {
        uint8_t id = (uint8_t)(s->board_id < RS_NBOARDS ? (s->board_id + 1) % RS_NBOARDS : 0);
        board_from_preset(&s->board, id), s->board_id = id;
    } else if (s->battle.arena == RS_ARENA_RANDOM) {
        roll_board(s, &s->board), s->board_id = RS_B_RANDOM;
    } else
        return;
    s->pending_board = RS_B_NONE;                       /* (now, at the pads: nothing queued for the loop's end) */
    for (int b = 0; b < 2; b++) s->board.t[START[b].y * RS_COLS + START[b].x] = (struct rs_tile_t){RS_T_NONE, 0};
}

void rs_battle_start(struct rs_state *s, uint8_t to_win, uint8_t arena)
{
    struct rs_battle *t = &s->battle;
    uint8_t keep[2][3] = {{t->hold[0], t->act[0], t->press[0]}, {t->hold[1], t->act[1], t->press[1]}};
    for (unsigned i = 0; i < sizeof *t; i++) ((uint8_t *)t)[i] = 0;
    t->to_win = to_win < 1 ? 1 : to_win > 9 ? 9 : to_win, t->round = 1, t->arena = arena < RS_NARENAS ? arena : 0;
    for (int b = 0; b < 2; b++) t->hold[b] = keep[b][0], t->act[b] = keep[b][1], t->press[b] = keep[b][2];
    battle_pads(s);
}

void rs_battle_stop(struct rs_state *s)
{
    s->battle.phase = RS_BT_OFF;
    for (int b = 0; b < 2; b++) s->bot[b].heat = 0, s->bot[b].dying = 0;
}

void rs_battle_keys(struct rs_state *s, int player, uint8_t hold, uint8_t act, uint8_t pressed)
{
    struct rs_battle *t = &s->battle;
    int b = player & 1;
    t->hold[b] = hold <= 4 ? hold : 0, t->act[b] = act ? 1 : 0;
    if (pressed) t->press[b] = 1;                       /* (a tap between two beats still strikes on the next) */
}

static int open_cell(const struct rs_state *s, int x, int y) { return inside(x, y) && !blocked(s, x, y); }

int rs_battle_hot(const struct rs_battle *t, int x, int y)   /* the arena shrinks: its edge, then all but the middle */
{
    int ring = x < y ? x : y;                           /* (0 the edge, 1 the next, 2 the middle) */
    if (RS_COLS - 1 - x < ring) ring = RS_COLS - 1 - x;
    if (RS_ROWS - 1 - y < ring) ring = RS_ROWS - 1 - y;
    return t->phase == RS_BT_FIGHT && ((t->beats >= RS_BT_OVERHEAT && ring == 0) || (t->beats >= RS_BT_OVERHEAT2 && ring < 2));
}
static uint32_t mix_beats(const struct rs_battle *t)  /* a coin from the round and its length */
{
    uint32_t h = (uint32_t)t->round * 0x9E3779B1u ^ (uint32_t)t->beats * 0x85EBCA77u;
    h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12;
    return h >> 7;
}
static int to_middle(const struct rs_bot *b)
{
    int dx = b->x - RS_COLS / 2, dy = b->y - RS_ROWS / 2;
    dx = dx < 0 ? -dx : dx, dy = dy < 0 ? -dy : dy;
    return dx > dy ? dx : dy;
}

int rs_battle_in_reach(const struct rs_state *s, int player)   /* the other one tile on, or two with the tile between open */
{
    const struct rs_bot *me = &s->bot[player & 1], *ot = &s->bot[!(player & 1)];
    uint8_t d = s->battle.hold[player & 1] ? (uint8_t)(s->battle.hold[player & 1] - 1) : me->dir;
    int x1 = me->x + DX[d], y1 = me->y + DY[d];
    return (ot->x == x1 && ot->y == y1) || (open_cell(s, x1, y1) && ot->x == x1 + DX[d] && ot->y == y1 + DY[d]);
}

static void battle_fight(struct rs_state *s, struct rs_tick_out *o)
{
    struct rs_battle *t = &s->battle;
    int intent[2], tx[2] = {0, 0}, ty[2] = {0, 0}, ok[2] = {0, 0};
    for (int b = 0; b < 2; b++) {                       /* what each player holds: the way faced first */
        if (t->hold[b]) s->bot[b].dir = (uint8_t)(t->hold[b] - 1);
        intent[b] = (t->act[b] || t->press[b]) && rs_battle_in_reach(s, b) ? RS_BT_STRIKE   /* (out of reach the key plays) */
                  : t->hold[b] ? RS_BT_MOVE : RS_BT_WAIT;
        t->press[b] = 0;
        if (intent[b] == RS_BT_MOVE) {
            tx[b] = s->bot[b].x + DX[s->bot[b].dir], ty[b] = s->bot[b].y + DY[s->bot[b].dir];
            ok[b] = open_cell(s, tx[b], ty[b]);
        }
    }
    /* the steps, both at once: into a wall or a crate, into the same tile, through each other, or onto the other
     * where it stays: a bump */
    int free_[2] = {ok[0], ok[1]};                      /* (each decided from both as they were: at once) */
    for (int b = 0; b < 2; b++) {
        struct rs_bot *me = &s->bot[b], *ot = &s->bot[!b];
        if (intent[b] != RS_BT_MOVE) continue;
        int other_goes = intent[!b] == RS_BT_MOVE && free_[!b];
        int same = other_goes && tx[!b] == tx[b] && ty[!b] == ty[b];
        int swap = other_goes && tx[b] == ot->x && ty[b] == ot->y && tx[!b] == me->x && ty[!b] == me->y;
        int onto = tx[b] == ot->x && ty[b] == ot->y && !other_goes;
        if (!ok[b]) {
            evk(o, RS_EV_COLLISION, inside(tx[b], ty[b]) ? tx[b] : me->x, inside(tx[b], ty[b]) ? ty[b] : me->y);
            o->bumped[b] |= inside(tx[b], ty[b]) ? 2 : 1;
        } else if (same || swap || onto) {
            evk(o, RS_EV_COLLISION, tx[b], ty[b]), o->bumped[b] |= 1;
            ok[b] = 0;
        }
    }
    for (int b = 0; b < 2; b++)
        if (intent[b] == RS_BT_MOVE && ok[b]) {
            s->bot[b].x = (int8_t)tx[b], s->bot[b].y = (int8_t)ty[b];
            evk(o, RS_EV_MOVE, tx[b], ty[b]);
        }
    /* the strikes, from where they stand now, both at once: the other one tile on is hit; two tiles on with the
     * tile between open, the striker lunges a tile and hits; else a swing at nothing */
    int hit[2] = {0, 0}, lunge[2] = {0, 0};
    for (int b = 0; b < 2; b++) {
        if (intent[b] != RS_BT_STRIKE) continue;
        const struct rs_bot *me = &s->bot[b], *ot = &s->bot[!b];
        int x1 = me->x + DX[me->dir], y1 = me->y + DY[me->dir], x2 = x1 + DX[me->dir], y2 = y1 + DY[me->dir];
        t->sdir[b] = me->dir;                           /* (the way it struck: a rotator may turn it after) */
        if (ot->x == x1 && ot->y == y1) hit[b] = 1;
        else if (open_cell(s, x1, y1) && ot->x == x2 && ot->y == y2) hit[b] = 1, lunge[b] = 1;
        if (hit[b] && t->guard[!b]) hit[b] = 0, lunge[b] = 0;   /* (it was hit last beat: it reels, untouchable) */
        if (!hit[b]) evk(o, RS_EV_PING, me->x, me->y);  /* (a swing at the air) */
    }
    t->guard[0] = t->guard[1] = 0;
    for (int b = 0; b < 2; b++)
        if (lunge[b]) s->bot[b].x = (int8_t)(s->bot[b].x + DX[s->bot[b].dir]), s->bot[b].y = (int8_t)(s->bot[b].y + DY[s->bot[b].dir]);
    /* a hit drives them apart: the one hit two tiles back (+1 for each a wall or a crate stops), the striker a tile
     * the other way, so no one hits again on the next beat and the two must come at each other once more */
    for (int b = 0; b < 2; b++) {
        if (!hit[b]) continue;
        struct rs_bot *me = &s->bot[b], *ot = &s->bot[!b];
        uint8_t d = t->sdir[b];
        ot->heat = (int16_t)(ot->heat + (t->beats >= RS_BT_OVERHEAT ? 3 : 2));   /* (overheat: hits bite harder) */
        if (t->rhits[b] < 255) t->rhits[b]++;
        for (int k = 0; k < 2; k++) {
            int kx = ot->x + DX[d], ky = ot->y + DY[d];
            if (open_cell(s, kx, ky) && !(kx == me->x && ky == me->y)) ot->x = (int8_t)kx, ot->y = (int8_t)ky;   /* knocked back */
            else ot->heat = (int16_t)(ot->heat + 1), evk(o, RS_EV_COLLISION, ot->x, ot->y);   /* slammed into a wall, a crate */
        }
        o->clash[!b] = 1, t->hit[b] = 1, t->guard[!b] = 1;
        evk(o, RS_EV_ZAP, ot->x, ot->y);
    }
    for (int b = 0; b < 2; b++) {                       /* the striker's recoil */
        if (!hit[b]) continue;
        struct rs_bot *me = &s->bot[b], *ot = &s->bot[!b];
        int rx = me->x - DX[t->sdir[b]], ry = me->y - DY[t->sdir[b]];
        if (open_cell(s, rx, ry) && !(rx == ot->x && ry == ot->y)) me->x = (int8_t)rx, me->y = (int8_t)ry;
    }
    /* then the tiles under them, as in the game, and what each did, for the music */
    for (int b = 0; b < 2; b++) {
        struct rs_bot *me = &s->bot[b];
        me->stepped_tile = tile_at(s, me->x, me->y);
        me->stepped_dir = s->board.t[me->y * RS_COLS + me->x].dir;
        int16_t before = me->heat;
        apply_tile_effects(s, b, o);
        if (t->beats >= RS_BT_OVERHEAT) {               /* overheat: no pad cools, a glowing tile burns: it ends */
            if (me->heat < before) me->heat = before;
            if (rs_battle_hot(t, me->x, me->y)) me->heat = (int16_t)(me->heat + 1);
        }
        if (me->heat > 6) me->heat = 6;
        struct rs_step st = {intent[b] == RS_BT_STRIKE ? RS_DASH : intent[b] == RS_BT_MOVE && ok[b] ? RS_MOVE : RS_WAIT, RS_STEADY, 100};
        if (st.action == RS_WAIT) evk(o, RS_EV_WAIT, me->x, me->y);
        rs_score_board(s, b, &st, o);                   /* (the HTML's notes for it: ORIGINAL plays them) */
        o->acted[b] = 1, o->played[b] = st;
        t->did[b] = (uint8_t)intent[b];
    }
    t->beats++;
    int out0 = s->bot[0].heat >= 6, out1 = s->bot[1].heat >= 6;
    if (out0 || out1) {                                 /* burned out: the round is the other's (both: a draw) */
        for (int b = 0; b < 2; b++)
            if (b ? out1 : out0) o->died[b] = 1, s->bot[b].dying = 1;
        ev(o, RS_EV_DEATH);
        rs_score_event(s, RS_EV_DEATH, 0, o);
        int m0 = to_middle(&s->bot[0]), m1 = to_middle(&s->bot[1]);   /* (both: the more hits, else the nearer the middle) */
        t->winner = (uint8_t)(out0 && out1 ? (t->rhits[0] != t->rhits[1] ? (t->rhits[0] > t->rhits[1] ? 0 : 1) : m0 != m1 ? (m0 < m1 ? 0 : 1) : 2)
                                           : out0 ? 1 : 0);
        t->coin = 0;
        if (t->winner == 2 && t->draws)                 /* a draw again: a coin (no match goes round for ever) */
            t->winner = (uint8_t)(mix_beats(t) & 1u), t->coin = 1;
        t->draws = t->winner == 2 ? (uint8_t)(t->draws + 1) : 0;
        if (t->winner < 2) t->wins[t->winner]++;
        t->phase = RS_BT_ROUND, t->count = 8;           /* (two bars to see it) */
    }
}

static void battle_beat(struct rs_state *s, struct rs_tick_out *o, int si)
{
    struct rs_battle *t = &s->battle;
    t->hit[0] = t->hit[1] = 0;
    if (si % rs_battle_beat_steps(s)) return;          /* between the beats the robots stand */
    t->did[0] = t->did[1] = RS_BT_WAIT;
    switch (t->phase) {
    case RS_BT_READY:                                   /* 4, 3, 2, 1 on the beats, a beep each; then FIGHT */
        if (t->count) t->count--;
        rs_score_count(s, t->count, o);
        if (!t->count) t->phase = RS_BT_FIGHT;          /* (the robots go from the next beat) */
        return;
    case RS_BT_ROUND:
        if (t->count) t->count--;
        if (!t->count) {
            if (t->winner < 2 && t->wins[t->winner] >= t->to_win) t->phase = RS_BT_MATCH;
            else t->round++, battle_arena(s), battle_pads(s);
        }
        return;
    case RS_BT_FIGHT: battle_fight(s, o); return;
    default: return;                                    /* the match is over: the winner shows */
    }
}
