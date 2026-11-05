/* SPDX-License-Identifier: GPL-3.0-only */
/* RoboSeq's game (rs_model.h): reference/roboseq.html's advanceSequence() and what it calls, in its order. Each
 * function names the HTML function it follows; where the HTML has a quirk, this keeps it (the HTML wins,
 * CLAUDE.md), and says so. */
#include "rs_model.h"

const char *const rs_action_name[RS_NACTIONS] = {"move", "back", "turnL", "turnR", "dash", "ping", "wait"};
const char *const rs_modifier_name[RS_NMODIFIERS] = {"steady", "bright", "low", "wobble", "glitch"};
const char *const rs_event_name[RS_NEVENTS] = {"reset", "collision", "move", "belt", "rotator", "zap",
                                               "pulse", "turn", "wait", "ping", "death", "love"};
const char *const rs_board_name[RS_NBOARDS] = {"default", "corridor", "garden", "gauntlet", "spinner", "maze"};
const char *const rs_drum_name[RS_NDRUMS] = {"four", "break", "half", "offbeat", "triplet", "minimal"};
const char rs_dir_name[4] = {'N', 'E', 'S', 'W'};

static const int8_t DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
static const struct { int8_t x, y; uint8_t dir; } START[2] = {{0, 4, RS_E}, {4, 0, RS_W}};   /* BOARD.starts */

/* ---- the boards (BOARD_PRESETS): x, y, tile, a belt's direction */
struct rs_preset_tile { uint8_t x, y, type, dir; };
#define BELT(x, y, d) {x, y, RS_T_BELT, RS_##d}
#define TILE(x, y, t) {x, y, RS_T_##t, 0}
static const struct rs_preset_tile P_DEFAULT[] = {
    BELT(1, 0, E), TILE(2, 0, ZAP), TILE(3, 0, ROTR), TILE(0, 1, PULSE), TILE(2, 1, BLOCK), BELT(4, 1, S),
    TILE(1, 2, ROTL), TILE(3, 2, ZAP), BELT(0, 3, N), TILE(2, 3, PULSE), TILE(4, 3, ROTR), TILE(3, 4, ROTL)};
static const struct rs_preset_tile P_CORRIDOR[] = {
    BELT(0, 0, E), BELT(1, 0, E), BELT(2, 0, S), BELT(2, 1, S), TILE(2, 2, ZAP), BELT(2, 3, N),
    TILE(1, 2, BLOCK), TILE(3, 2, BLOCK), TILE(0, 2, PULSE), TILE(4, 2, PULSE), BELT(4, 4, W), BELT(3, 4, W)};
static const struct rs_preset_tile P_GARDEN[] = {
    TILE(1, 1, PULSE), TILE(3, 1, PULSE), TILE(1, 3, PULSE), TILE(3, 3, PULSE), TILE(2, 2, PULSE),
    TILE(0, 2, ROTL), TILE(4, 2, ROTR), TILE(2, 0, ROTR), TILE(2, 4, ROTL)};
static const struct rs_preset_tile P_GAUNTLET[] = {
    TILE(1, 0, ZAP), TILE(3, 0, ZAP), TILE(0, 1, ZAP), TILE(2, 1, ZAP), TILE(4, 1, ZAP), TILE(1, 2, PULSE),
    TILE(3, 2, PULSE), TILE(2, 3, BLOCK), TILE(0, 3, ROTL), TILE(4, 3, ROTR), TILE(1, 4, ZAP), TILE(3, 4, ZAP)};
static const struct rs_preset_tile P_SPINNER[] = {
    TILE(1, 0, ROTR), TILE(3, 0, ROTL), TILE(0, 1, ROTL), TILE(4, 1, ROTR), TILE(2, 2, ZAP), BELT(1, 2, E),
    BELT(3, 2, W), TILE(0, 3, ROTR), TILE(4, 3, ROTL), TILE(1, 4, ROTL), TILE(3, 4, ROTR), TILE(2, 0, PULSE)};
static const struct rs_preset_tile P_MAZE[] = {
    TILE(1, 0, BLOCK), TILE(3, 0, BLOCK), BELT(0, 1, S), TILE(2, 1, ROTR), TILE(4, 1, ZAP), TILE(1, 2, BLOCK),
    TILE(3, 2, BLOCK), TILE(0, 3, ZAP), TILE(2, 3, ROTL), BELT(4, 3, N), TILE(1, 4, PULSE), TILE(3, 4, PULSE)};
#define N_OF(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))
static const struct { const struct rs_preset_tile *t; uint8_t n; } PRESETS[RS_NBOARDS] = {
    {P_DEFAULT, N_OF(P_DEFAULT)}, {P_CORRIDOR, N_OF(P_CORRIDOR)}, {P_GARDEN, N_OF(P_GARDEN)},
    {P_GAUNTLET, N_OF(P_GAUNTLET)}, {P_SPINNER, N_OF(P_SPINNER)}, {P_MAZE, N_OF(P_MAZE)}};

static void board_from_preset(struct rs_board *b, uint8_t id)
{
    for (int i = 0; i < RS_ROWS * RS_COLS; i++) b->t[i].type = RS_T_NONE, b->t[i].dir = 0;
    for (uint8_t i = 0; i < PRESETS[id].n; i++) {
        const struct rs_preset_tile *p = &PRESETS[id].t[i];
        b->t[p->y * RS_COLS + p->x].type = p->type;
        b->t[p->y * RS_COLS + p->x].dir = p->dir;
    }
}

/* ---- the scales (SCALES), as bit masks of the semitones */
static const uint16_t SCALE_MASK[RS_NSCALES] = {
    0x0FFF,                                              /* chromatic */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* major */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* minor */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* dorian */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* phrygian */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 11),   /* lydian */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* mixolydian */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                          /* pentatonic */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),                         /* min-penta */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 9) | (1 << 11),   /* diminished */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 8) | (1 << 10),              /* whole-tone */
    (1 << 0) | (1 << 3) | (1 << 4) | (1 << 7) | (1 << 8) | (1 << 11),              /* augmented */
};

static int floor_div12(int m) { return m >= 0 ? m / 12 : -((11 - m) / 12); }

int rs_scale_quantize(int midi, uint8_t scale)          /* scaleQuantize(): the nearest, the lower on a tie */
{
    uint16_t mask = SCALE_MASK[scale < RS_NSCALES ? scale : RS_SC_CHROMATIC];
    int oct = floor_div12(midi), semi = midi - oct * 12, best = 0, bestd = 99;
    for (int n = 0; n < 12; n++) {
        if (!(mask >> n & 1)) continue;
        int d = n > semi ? n - semi : semi - n;
        if (d < bestd) bestd = d, best = n;
    }
    return oct * 12 + best;
}

int rs_scale_shift(int midi, uint8_t scale, int steps)  /* scaleIntervalShift() */
{
    uint16_t mask = SCALE_MASK[scale < RS_NSCALES ? scale : RS_SC_CHROMATIC];
    int cur = rs_scale_quantize(midi, scale), dir = steps > 0 ? 1 : -1, left = steps > 0 ? steps : -steps;
    while (left > 0) {
        cur += dir;
        int semi = cur - floor_div12(cur) * 12;
        if (mask >> semi & 1) left--;
    }
    return cur;
}

/* ---- the drums (DRUM_PRESETS, getDrumSteps) */
uint8_t rs_drum_steps(uint8_t loop_len) { return (loop_len == 12 || loop_len == 24 || loop_len == 48) ? 12 : 16; }

void rs_set_drums(struct rs_state *s, uint8_t id)
{
    uint8_t n = rs_drum_steps(s->loop_len);
    s->drum_preset = id;
    for (int i = 0; i < RS_MAX_STEPS; i++) {
        uint8_t k = 0, sn = 0, h = 0, b8 = (uint8_t)(i % 8), b6 = (uint8_t)(i % 6);
        if (i < n) switch (id) {
            case RS_D_FOUR: k = i % 4 == 0; sn = i % 4 == 2; h = 1; break;
            case RS_D_BREAK: k = b8 == 0 || b8 == 3 || b8 == 6; sn = b8 == 2 || b8 == 5; h = b8 % 2 == 0; break;
            case RS_D_HALF: k = b8 == 0; sn = b8 == 4; h = b8 % 2 == 0; break;
            case RS_D_OFFBEAT: k = b8 == 0 || b8 == 5; sn = b8 == 3 || b8 == 7; h = b8 % 2 == 1; break;
            case RS_D_TRIPLET: k = b6 == 0; sn = b6 == 3; h = b6 == 0 || b6 == 2 || b6 == 4; break;
            default: k = b8 == 0; h = b8 == 4; break;   /* minimal */
            }
        s->drum_k[i] = k, s->drum_s[i] = sn, s->drum_h[i] = h;
    }
}

/* ---- the state */
static const struct rs_step DEFAULT_STEPS[2][16] = {
    {{RS_DASH, RS_STEADY, 100}, {RS_MOVE, RS_BRIGHT, 100}, {RS_MOVE, RS_WOBBLE, 100}, {RS_DASH, RS_GLITCH, 100},
     {RS_BACK, RS_WOBBLE, 100}, {RS_TURNL, RS_LOW, 100}, {RS_MOVE, RS_LOW, 100}, {RS_DASH, RS_BRIGHT, 100},
     {RS_MOVE, RS_WOBBLE, 100}, {RS_DASH, RS_GLITCH, 100}, {RS_TURNR, RS_GLITCH, 100}, {RS_TURNR, RS_BRIGHT, 100},
     {RS_DASH, RS_BRIGHT, 100}, {RS_MOVE, RS_WOBBLE, 100}, {RS_PING, RS_WOBBLE, 75}, {RS_WAIT, RS_WOBBLE, 100}},
    {{RS_MOVE, RS_LOW, 100}, {RS_MOVE, RS_LOW, 100}, {RS_MOVE, RS_STEADY, 100}, {RS_MOVE, RS_WOBBLE, 100},
     {RS_WAIT, RS_BRIGHT, 100}, {RS_TURNR, RS_WOBBLE, 100}, {RS_DASH, RS_STEADY, 100}, {RS_DASH, RS_STEADY, 100},
     {RS_MOVE, RS_BRIGHT, 100}, {RS_PING, RS_GLITCH, 100}, {RS_DASH, RS_BRIGHT, 100}, {RS_BACK, RS_BRIGHT, 100},
     {RS_WAIT, RS_WOBBLE, 100}, {RS_TURNR, RS_WOBBLE, 100}, {RS_TURNR, RS_GLITCH, 100}, {RS_PING, RS_BRIGHT, 100}},
};

static void board_state_new(struct rs_state *s)         /* createBoardState() */
{
    for (int b = 0; b < 2; b++) {
        struct rs_bot *o = &s->bot[b];
        o->x = START[b].x, o->y = START[b].y, o->dir = START[b].dir;
        o->heat = 0, o->deaths = 0, o->dying = 0, o->stepped_tile = RS_T_NONE;
    }
    s->clash_cooldown = 0, s->love_steps = 0;
}

void rs_default(struct rs_state *s)                     /* buildDefaultState() */
{
    static const struct rs_bassmod_step BM[4] = {{RS_SC_MAJOR, 3, 0}, {RS_SC_MAJOR, 0, 0}, {RS_SC_MAJOR, 5, 0},
                                                 {RS_SC_MAJOR, 9, 0}};
    uint8_t *p = (uint8_t *)s;
    for (unsigned i = 0; i < sizeof *s; i++) p[i] = 0;
    s->loop_len = 16, s->tpm = 1, s->vm_manual = 0, s->vm_freq4 = 4, s->bassmod_len = 4;
    for (int i = 0; i < RS_BASSMOD_MAX; i++) s->bassmod[i] = i < 4 ? BM[i] : (struct rs_bassmod_step){RS_SC_MAJOR, 0, 0};
    s->drums_on = 1;
    rs_set_drums(s, RS_D_FOUR);
    s->seq2_index = 0;
    for (int i = 0; i < RS_SEQ2_STEPS; i++) s->seq2[i] = (struct rs_seq2_step){RS_B_NONE, RS_D_NONE, 0};
    for (int b = 0; b < 2; b++) {
        s->prog[b].len = 16;
        for (int i = 0; i < RS_MAX_STEPS; i++)
            s->prog[b].s[i] = i < 16 ? DEFAULT_STEPS[b][i] : (struct rs_step){RS_WAIT, RS_STEADY, 100};
    }
    board_from_preset(&s->board, RS_B_DEFAULT);
    s->board_id = RS_B_DEFAULT, s->pending_board = RS_B_NONE;
    board_state_new(s);
    s->step = -1;
}

void rs_reset(struct rs_state *s)                       /* resetBoardState() */
{
    board_state_new(s);
    s->step = -1, s->move_tick = 0, s->drum_tick = 0, s->vm_index = 0, s->vm_count = 0, s->cycle = 0;
}

void rs_load_board(struct rs_state *s, uint8_t id)      /* applyBoardPreset() */
{
    if (id >= RS_NBOARDS) return;
    if (s->playing) { s->pending_board = id; return; }  /* queued for the next loop reset */
    board_from_preset(&s->board, id);
    s->board_id = id;
    rs_reset(s);
}

void rs_start(struct rs_state *s) { rs_reset(s); s->playing = 1; }

void rs_stop(struct rs_state *s)                        /* stopPlayback() */
{
    s->playing = 0, s->step = -1, s->move_tick = 0, s->drum_tick = 0, s->vm_index = 0, s->vm_count = 0;
}

/* ---- one tick */
static void ev(struct rs_tick_out *o, uint8_t kind)
{
    if (o->nevents < RS_MAX_EVENTS) o->events[o->nevents++] = kind;
}

static uint8_t tile_at(const struct rs_state *s, int x, int y) { return s->board.t[y * RS_COLS + x].type; }
static int inside(int x, int y) { return x >= 0 && y >= 0 && x < RS_COLS && y < RS_ROWS; }
static int blocked(const struct rs_state *s, int x, int y) { return tile_at(s, x, y) == RS_T_BLOCK; }

static int attempt_move(struct rs_state *s, int b, int heading, struct rs_tick_out *o)   /* attemptMove() */
{
    struct rs_bot *bot = &s->bot[b];
    int nx = bot->x + DX[heading], ny = bot->y + DY[heading];
    if (!inside(nx, ny) || blocked(s, nx, ny)) { ev(o, RS_EV_COLLISION); return 0; }   /* the wall, a crate */
    bot->x = (int8_t)nx, bot->y = (int8_t)ny;           /* bots can share a tile */
    ev(o, RS_EV_MOVE);
    return 1;
}

static void perform_ping(struct rs_state *s, int b, struct rs_tick_out *o)   /* performPing() */
{
    struct rs_bot *bot = &s->bot[b], *other = &s->bot[!b];
    int x = bot->x + DX[bot->dir], y = bot->y + DY[bot->dir];
    while (inside(x, y) && !blocked(s, x, y)) {
        if (other->x == x && other->y == y) { other->heat += 1; ev(o, RS_EV_ZAP); return; }
        x += DX[bot->dir], y += DY[bot->dir];
    }
    ev(o, RS_EV_PING);
}

static void check_death(struct rs_state *s, int b, struct rs_tick_out *o)   /* checkDeath(): dies on the next move */
{
    if (s->bot[b].heat < 6) return;
    s->bot[b].dying = 1;
    ev(o, RS_EV_ZAP);
}

static void process_death(struct rs_state *s, int b, struct rs_tick_out *o)   /* processDeath() */
{
    static const int8_t CX[4] = {0, RS_COLS - 1, 0, RS_COLS - 1}, CY[4] = {0, 0, RS_ROWS - 1, RS_ROWS - 1};
    struct rs_bot *bot = &s->bot[b];
    if (!bot->dying) return;
    bot->deaths += 1, bot->heat = 0, bot->dying = 0;
    int best = -1, bx = START[b].x, by = START[b].y;     /* the corner farthest from where it burned out */
    for (int c = 0; c < 4; c++) {
        int d = (CX[c] > bot->x ? CX[c] - bot->x : bot->x - CX[c]) + (CY[c] > bot->y ? CY[c] - bot->y : bot->y - CY[c]);
        if (d > best) best = d, bx = CX[c], by = CY[c];
    }
    bot->x = (int8_t)bx, bot->y = (int8_t)by, bot->dir = START[b].dir;
    o->died[b] = 1;
    ev(o, RS_EV_DEATH);
}

static void apply_tile_effects(struct rs_state *s, int b, struct rs_tick_out *o)   /* applyTileEffects() */
{
    for (int safety = 0; safety < 3;) {
        struct rs_bot *bot = &s->bot[b];
        const struct rs_tile_t *t = &s->board.t[bot->y * RS_COLS + bot->x];
        switch (t->type) {
        case RS_T_BELT: {
            int moved = attempt_move(s, b, t->dir, o);
            ev(o, RS_EV_BELT);
            if (!moved) return;
            safety++;
            continue;
        }
        case RS_T_ROTL: bot->dir = (uint8_t)((bot->dir + 3) & 3); ev(o, RS_EV_ROTATOR); return;
        case RS_T_ROTR: bot->dir = (uint8_t)((bot->dir + 1) & 3); ev(o, RS_EV_ROTATOR); return;
        case RS_T_ZAP: bot->heat = (int16_t)(bot->heat + 1 < 6 ? bot->heat + 1 : 6); ev(o, RS_EV_ZAP); return;   /* (a cap of 6) */
        case RS_T_PULSE: bot->heat = (int16_t)(bot->heat - 1 > 0 ? bot->heat - 1 : 0); ev(o, RS_EV_PULSE); return;
        default: return;                                /* nothing, or a crate */
        }
    }
}

static void execute_step(struct rs_state *s, int b, const struct rs_step *st, struct rs_tick_out *o)   /* executeStepAction() */
{
    struct rs_bot *bot = &s->bot[b], *other = &s->bot[!b];
    switch (st->action) {
    case RS_MOVE: attempt_move(s, b, bot->dir, o); return;
    case RS_BACK: attempt_move(s, b, (bot->dir + 2) & 3, o); return;
    case RS_TURNL: bot->dir = (uint8_t)((bot->dir + 3) & 3); ev(o, RS_EV_TURN); return;
    case RS_TURNR: bot->dir = (uint8_t)((bot->dir + 1) & 3); ev(o, RS_EV_TURN); return;
    case RS_DASH:
        attempt_move(s, b, bot->dir, o);
        attempt_move(s, b, bot->dir, o);                /* (the second even when the first was stopped) */
        if (bot->x == other->x && bot->y == other->y) {
            other->heat += 2;
            s->clash_cooldown = 8;
            ev(o, RS_EV_ZAP);
        }
        return;
    case RS_PING: perform_ping(s, b, o); return;
    default: ev(o, RS_EV_WAIT); return;
    }
}

static void advance_seq2(struct rs_state *s)            /* advanceSeq2() */
{
    s->seq2_index = (int8_t)((s->seq2_index + 1) % RS_SEQ2_STEPS);
    const struct rs_seq2_step *st = &s->seq2[s->seq2_index];
    if (st->board != RS_B_NONE) rs_load_board(s, st->board);
    if (st->drums != RS_D_NONE) rs_set_drums(s, st->drums);
    s->swapped = st->regswap ? 1 : 0;                   /* (every step sets it: the HTML's regSwap is a boolean) */
    /* the bots' registers rolled when seq 2 wraps (rndBotA / rndBotB): not yet (ROADMAP phase 1) */
}

void rs_advance_voice_mod(struct rs_state *s)           /* advanceVoiceMod() */
{
    uint8_t prev = s->vm_index;
    s->vm_index = (uint8_t)((s->vm_index + 1) % s->bassmod_len);
    if (s->vm_index == 0 && prev != 0 && s->seq2_pending) {
        s->seq2_active = 1, s->seq2_pending = 0, s->seq2_index = -1;
    }
    if (s->vm_index == 0 && prev != 0 && s->seq2_active) advance_seq2(s);
}

void rs_tick(struct rs_state *s, struct rs_tick_out *o)  /* advanceSequence() */
{
    uint8_t *p = (uint8_t *)o;
    for (unsigned i = 0; i < sizeof *o; i++) p[i] = 0;
    if (!s->playing) return;
    uint32_t tpm = s->tpm ? s->tpm : 1;
    int is_move = s->move_tick % tpm == 0, skip = 0;

    if (s->step == -1) {                                /* the first tick: the bots stand at their pads */
        s->step = 0, s->drum_tick = 0, s->move_tick = 0;
        skip = 1;
    } else if (is_move) {
        int next = s->step + 1;
        if (!s->vm_manual) {
            s->vm_count++;
            int thr = s->vm_freq4 * s->loop_len / 4;    /* round(freq * loopLength): exact, the lengths are 4 n */
            if (thr < 1) thr = 1;
            if (s->vm_count >= thr) { s->vm_count = 0; rs_advance_voice_mod(s); }
        }
        if (next >= s->loop_len) {                      /* the loop resets */
            s->cycle++;
            o->loop_reset = 1;
            for (int b = 0; b < 2; b++) process_death(s, b, o);
            if (s->pending_board != RS_B_NONE) {
                board_from_preset(&s->board, s->pending_board);
                s->board_id = s->pending_board, s->pending_board = RS_B_NONE;
            }
            uint16_t da = s->bot[0].deaths, db = s->bot[1].deaths;
            board_state_new(s);
            s->bot[0].deaths = da, s->bot[1].deaths = db;
            ev(o, RS_EV_RESET);
            s->step = 0, s->drum_tick = 0, s->move_tick = 0, s->vm_count = 0;
            skip = 1;
        } else {
            s->step = (int16_t)next;
        }
    }

    if (is_move && !skip) {
        int si = s->step;
        o->move_tick = 1;
        for (int b = 0; b < 2; b++) process_death(s, b, o);   /* deaths marked last step land on the beat */
        for (int b = 0; b < 2; b++) {
            if (s->bot[b].dying) continue;
            const struct rs_step *st = &s->prog[b].s[si % (s->prog[b].len ? s->prog[b].len : s->loop_len)];
            if (rs_skips(b, (uint32_t)si, s->cycle, st->prob)) { ev(o, RS_EV_WAIT); continue; }   /* hesitated */
            execute_step(s, b, st, o);
            s->bot[b].stepped_tile = tile_at(s, s->bot[b].x, s->bot[b].y);
            apply_tile_effects(s, b, o);
            check_death(s, b, o);
            o->acted[b] = 1, o->played[b] = *st;
        }
        /* the love check: both on one tile, two steps running, no dash coming */
        if (s->clash_cooldown > 0) s->clash_cooldown--;
        const struct rs_bot *a = &s->bot[0], *b = &s->bot[1];
        if (a->x == b->x && a->y == b->y && !a->dying && !b->dying && s->clash_cooldown == 0) {
            if (s->love_steps < 255) s->love_steps++;
            if (s->love_steps == 2) {
                int ni = (si + 1) % s->loop_len;
                const struct rs_step *na = &s->prog[0].s[ni % (s->prog[0].len ? s->prog[0].len : s->loop_len)];
                const struct rs_step *nb = &s->prog[1].s[ni % (s->prog[1].len ? s->prog[1].len : s->loop_len)];
                if (na->action != RS_DASH && nb->action != RS_DASH) { ev(o, RS_EV_LOVE); o->love = 1; }
            }
        } else {
            s->love_steps = 0;
        }
    } else if (tpm > 1) {                               /* a ghost tick (also the reset tick's, phase 0: the HTML's) */
        o->ghost = 1, o->ghost_phase = (uint8_t)(s->move_tick % tpm);
    }

    if (is_move) {                                      /* the drums, locked to the moves */
        if (s->drums_on) {
            uint8_t i = (uint8_t)(s->drum_tick % rs_drum_steps(s->loop_len));
            o->drum_k = s->drum_k[i], o->drum_s = s->drum_s[i], o->drum_h = s->drum_h[i];
        }
        s->drum_tick = (uint8_t)((s->drum_tick + 1) % rs_drum_steps(s->loop_len));
    } else if (tpm > 1 && s->drums_on) {
        o->drum_ghost = 1;
    }
    s->move_tick++;
}

/* ---- a fingerprint of the model: the default state's first 400 ticks (tests/golden/traces/default.trace) and the
 * chance of 64 steps x 32 loops for both bots, hashed (FNV-1a). The same on any machine that runs the model right:
 * the host tests print it, the firmware computes it at boot, make emu reads it back from the chip's RAM. */
static uint32_t fnv(uint32_t h, uint32_t v)
{
    for (int i = 0; i < 4; i++) h = (h ^ ((v >> (8 * i)) & 0xFFu)) * 16777619u;
    return h;
}

uint32_t rs_selfcheck(void)
{
    static struct rs_state s;
    struct rs_tick_out o;
    uint32_t h = 2166136261u;
    rs_default(&s);
    rs_start(&s);
    for (int t = 0; t < 400; t++) {
        rs_tick(&s, &o);
        for (int b = 0; b < 2; b++)
            h = fnv(h, (uint32_t)(uint8_t)s.bot[b].x | (uint32_t)(uint8_t)s.bot[b].y << 8 | (uint32_t)s.bot[b].dir << 16 |
                           (uint32_t)(uint16_t)s.bot[b].heat << 20);
        for (int i = 0; i < o.nevents; i++) h = fnv(h, o.events[i]);
        h = fnv(h, (uint32_t)s.cycle << 8 | (uint32_t)(uint16_t)s.step);
    }
    for (int b = 0; b < 2; b++)
        for (uint32_t c = 0; c < 32; c++)
            for (uint32_t st = 0; st < 64; st++) h = fnv(h, rs_chance_q32(b, st, c, 0));
    return h;
}
