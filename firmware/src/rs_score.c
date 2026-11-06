/* SPDX-License-Identifier: GPL-3.0-only */
/* RoboSeq's score: what reference/roboseq.html's audio plays, note by note, at each point of advanceSequence(), as
 * rs_note (voice, midi, velocity, start, length). playBoardSound(), playGhostTick(), playEventSound(),
 * playDrumStep() and the love check's coin, in the HTML's order and with its quirks; tests/roboseq checks every note
 * of every golden scenario against the HTML's own calls. No float: times in 1/256 sample at 44.1 kHz, the HTML's
 * factors in Q20 (each within 1e-6), velocities in Q20 until they are written as Q15. Main loop, not the audio
 * interrupt. */
#include "rs_model.h"

const char *const rs_voice_name[RS_NVOICES] = {
    "A.main", "A.ping", "B.fm", "B.sub", "fx.collision", "fx.belt", "fx.rotator", "fx.zap", "fx.pulse",
    "drum.kick", "drum.snare", "drum.hihat", "coin", "ex.thump", "ex.noise", "ex.crackle"};

#define Q 20
#define ONE (1u << Q)
#define F(x) ((uint32_t)((x) * (double)ONE + 0.5))      /* a constant factor, Q20 (folded at compile time) */

static uint32_t mulq(uint32_t a, uint32_t f) { return (uint32_t)(((uint64_t)a * f + (ONE >> 1)) >> Q); }

/* seconds as 1/256 samples: s * 44100 * 256 */
#define SEC(x) ((uint32_t)((x) * 44100.0 * 256.0 + 0.5))

static void note(struct rs_tick_out *o, uint8_t voice, int midi, uint32_t dur, uint32_t at, uint32_t vel_q20)
{
    if (o->nnotes >= RS_MAX_NOTES) return;
    for (int i = 0; i < o->nnotes; i++)                /* a voice's second start at a time it starts at: dropped (D10) */
        if (o->notes[i].voice == voice && o->notes[i].at == at) return;
    struct rs_note *n = &o->notes[o->nnotes++];
    n->voice = voice, n->midi = (int8_t)midi, n->dur = dur, n->at = at;
    n->vel = (uint16_t)((vel_q20 + 16u) >> 5);
}

/* ---- the scales as lists (SCALES), and the HTML's note helpers */
static const uint8_t SCALE_LIST[RS_NSCALES][12] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10},
    {0, 1, 3, 5, 7, 8, 10}, {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 2, 4, 7, 9}, {0, 3, 5, 7, 10},
    {0, 2, 3, 5, 6, 8, 9, 11}, {0, 2, 4, 6, 8, 10}, {0, 3, 4, 7, 8, 11}};
static const uint8_t SCALE_LEN[RS_NSCALES] = {12, 7, 7, 7, 7, 7, 7, 5, 5, 8, 6, 6};

struct range { int min, max; };
static const struct range RANGE_HI = {48, 84}, RANGE_LO = {36, 72};   /* MIDI_RANGE_A, MIDI_RANGE_B */
static struct range range_of(const struct rs_state *s, int b) { return (b == 0) != (s->swapped != 0) ? RANGE_HI : RANGE_LO; }

static int fold(int m, struct range r)                  /* foldToRange() */
{
    while (m > r.max && m - 12 >= r.min) m -= 12;
    while (m < r.min && m + 12 <= r.max) m += 12;
    return m < r.min ? r.min : m > r.max ? r.max : m;
}
static int safe_note(int m, uint8_t sc, struct range r) { return rs_scale_quantize(fold(m, r), sc); }
static int safe_shift(int m, uint8_t sc, int deg, struct range r) { return fold(rs_scale_shift(m, sc, deg), r); }
static int mod12(int m) { return m - (m >= 0 ? m / 12 : -((11 - m) / 12)) * 12; }

static const struct rs_bassmod_step *mod_step(const struct rs_state *s)   /* getActiveVoiceModStep() */
{
    return &s->bassmod[s->vm_index % (s->bassmod_len ? s->bassmod_len : 1)];
}

static int note_from_position(const struct rs_state *s, int b)   /* noteFromPosition() */
{
    const struct rs_bassmod_step *ms = mod_step(s);
    uint8_t sc = ms->scale < RS_NSCALES ? ms->scale : RS_SC_MAJOR;
    int shift = s->transpose_root ? ms->root : 0;
    const struct rs_bot *a = &s->bot[0], *bot = &s->bot[b];
    int a_oct = 1 - a->y / 2;
    int a_midi = safe_note((5 + a_oct) * 12 + shift + SCALE_LIST[sc][a->x % SCALE_LEN[sc]], sc, range_of(s, 0));
    if (b == 0) return a_midi;
    static const int8_t HARMONY[5] = {0, 2, 4, 4, 7};   /* unison, 3rd, 5th, 5th, octave */
    int harm = rs_scale_shift(a_midi, sc, HARMONY[bot->x % 5]) + (1 - bot->y / 2) * 12;
    return fold(rs_scale_quantize(harm, sc), range_of(s, b));
}

static uint8_t scale_of(const struct rs_state *s)
{
    uint8_t sc = mod_step(s)->scale;
    return sc < RS_NSCALES ? sc : RS_SC_MAJOR;
}

/* the tile under a bot for its sound: the one it landed on this step, else where it stands (steppedTile || tileAt) */
static void sound_tile(const struct rs_state *s, int b, uint8_t *type, uint8_t *dir)
{
    const struct rs_bot *bot = &s->bot[b];
    if (bot->stepped_tile != RS_T_NONE) { *type = bot->stepped_tile, *dir = bot->stepped_dir; return; }
    const struct rs_tile_t *t = &s->board.t[bot->y * RS_COLS + bot->x];
    *type = t->type, *dir = t->dir;
}

uint32_t rs_step_q8(const struct rs_state *s)           /* 60 / (bpm * rate * tpm) / 4 s */
{
    uint32_t bpm = s->bpm ? s->bpm : 80, tpm = s->tpm ? s->tpm : 1;
    uint32_t slow = s->quarter == 1 ? 4u : s->quarter == 2 ? 2u : 1u;   /* 1/4 (the HTML's playbackRate), 1/2 (D22) */
    return (RS_SR * 256u * 15u * slow + (bpm * tpm) / 2u) / (bpm * tpm);
}

/* ---- the arps */
enum { CH_TRIAD, CH_SEVENTH, CH_SUS4, CH_ADD9, CH_POWER, CH_SIXTH };
static const uint8_t CHORD[6][4] = {{0, 2, 4}, {0, 2, 4, 6}, {0, 3, 4}, {0, 2, 4, 8}, {0, 4, 7}, {0, 2, 4, 5}};
static const uint8_t CHORD_N[6] = {3, 4, 3, 4, 3, 4};
static int chord_shape(int x, uint32_t cycle) { return (int)((x + cycle / 3) % 6); }   /* getChordShape() */
static int complement(int sh)                           /* getComplementShape() */
{
    return sh == CH_TRIAD ? CH_SEVENTH : sh == CH_SEVENTH ? CH_ADD9 : sh == CH_SUS4 ? CH_TRIAD : sh == CH_POWER ? CH_SIXTH : CH_SEVENTH;
}

static int arp_chord(int root, uint8_t sc, int shape, struct range r, int octaves, int *out)   /* buildArpChord() */
{
    int n = 0;
    for (int oct = 0; oct < octaves; oct++)
        for (int d = 0; d < CHORD_N[shape]; d++) {
            int m = safe_shift(root + oct * 12, sc, CHORD[shape][d], r), dup = 0;
            for (int k = 0; k < n; k++) dup |= out[k] == m;
            if (!dup) out[n++] = m;
        }
    for (int i = 1; i < n; i++)                         /* sort ascending */
        for (int k = i; k > 0 && out[k - 1] > out[k]; k--) { int t = out[k]; out[k] = out[k - 1]; out[k - 1] = t; }
    return n;
}

enum { P_UP, P_DOWN, P_UPDOWN, P_PINGPONG };
static int arp_order(const int *in, int n, int pat, int *out)   /* ARP_PATTERNS */
{
    int k = 0;
    if (pat == P_UP) for (int i = 0; i < n; i++) out[k++] = in[i];
    else if (pat == P_DOWN) for (int i = n - 1; i >= 0; i--) out[k++] = in[i];
    else if (pat == P_UPDOWN) {
        for (int i = 0; i < n; i++) out[k++] = in[i];
        if (n > 2) for (int i = n - 2; i >= 1; i--) out[k++] = in[i];
    } else {
        int lo = 0, hi = n - 1;
        while (lo <= hi) { out[k++] = in[lo++]; if (lo <= hi) out[k++] = in[hi--]; }
    }
    return k;
}
static int dir_pattern(uint8_t dir) { return dir == RS_N ? P_UP : dir == RS_E ? P_UPDOWN : dir == RS_S ? P_DOWN : P_PINGPONG; }

static void arp(struct rs_tick_out *o, uint8_t voice, const int *notes, int n, int pat, uint32_t at, uint32_t rate,
                uint32_t gate, uint32_t vel, int max)   /* scheduleArp() */
{
    int ord[24];
    if (!n) return;
    int len = arp_order(notes, n, pat, ord), count = max ? max : len;
    for (int i = 0; i < count; i++) {
        uint32_t dv = (uint32_t)i * F(0.012), v = vel > dv && vel - dv > F(0.12) ? vel - dv : F(0.12);
        note(o, voice, ord[i % len], i < count - 1 ? mulq(rate, F(0.98)) : gate, at + (uint32_t)i * rate, v);
    }
}

/* ---- playBoardSound */
static const uint32_t DIR_VEL[4] = {F(1.0), F(0.85), F(0.75), F(0.65)}, DIR_DUR[4] = {F(0.6), F(1.0), F(1.4), F(0.8)};
static const uint32_t MOD_VEL[5] = {F(1.0), F(1.1), F(0.85), F(0.95), F(1.05)};
static const uint32_t MOD_DUR[5] = {F(1.0), F(0.8), F(1.15), F(0.95), F(0.4)};
static const uint32_t ACT_VEL[7] = {F(0.8), F(0.7), F(0.6), F(0.6), F(1.0), F(0.9), F(0.2)};
static const uint32_t ACT_DUR[7] = {F(1.0), F(0.9), F(0.7), F(0.7), F(0.5), F(0.6), F(0.3)};
static const uint32_t GATE_MOD[5] = {F(1.0), F(0.8), F(1.3), F(1.1), F(0.6)};
static const uint32_t HEAT_VEL[6] = {F(1.0), F(1.08), F(1.16), F(1.24), F(1.32), F(1.40)};

static int heat_deg(int heat) { return heat < 2 ? (heat < 0 ? 0 : heat) : 2; }

static int final_note(const struct rs_state *s, int b, int hot, uint8_t sc)   /* the unison avoidance: A only */
{
    if (b != 0) return hot;
    int other = safe_shift(note_from_position(s, 1), sc, heat_deg(s->bot[1].heat), range_of(s, 1));
    return mod12(hot) == mod12(other) ? safe_shift(hot, sc, 2, range_of(s, 0)) : hot;
}

void rs_score_board(const struct rs_state *s, int b, const struct rs_step *st, struct rs_tick_out *o)
{
    const struct rs_bot *bot = &s->bot[b], *other = &s->bot[!b];
    uint8_t ttype, tdir, sc = scale_of(s);
    sound_tile(s, b, &ttype, &tdir);
    struct range r = range_of(s, b);
    int midi = note_from_position(s, b);
    uint32_t dur = rs_step_q8(s) * (s->tpm ? s->tpm : 1);
    int heat = bot->heat < 0 ? 0 : bot->heat;
    uint32_t vel = mulq(mulq(mulq(DIR_VEL[bot->dir], MOD_VEL[st->modifier]), ACT_VEL[st->action]), HEAT_VEL[heat > 5 ? 5 : heat]);
    if (vel > ONE) vel = ONE;
    uint32_t base_dur = mulq(mulq(mulq(dur, DIR_DUR[bot->dir]), MOD_DUR[st->modifier]), ACT_DUR[st->action]);
    uint32_t note_dur = ttype == RS_T_PULSE ? mulq(base_dur, F(1.8)) : ttype == RS_T_ZAP ? mulq(base_dur, F(0.5)) : base_dur;
    int hot = safe_shift(midi, sc, heat_deg(heat), r);
    int fin = final_note(s, b, hot, sc);
    uint8_t belt_dir = ttype == RS_T_BELT ? tdir : bot->dir;
    int dist = (bot->x > other->x ? bot->x - other->x : other->x - bot->x) + (bot->y > other->y ? bot->y - other->y : other->y - bot->y);
    int tpm = s->tpm ? s->tpm : 1;
    int shape = chord_shape(bot->x, s->cycle);
    int octaves = heat >= 4 ? 2 : 1;
    uint32_t rate = mulq(dur, F(0.5));
    uint32_t gate = mulq(mulq(dur, F(0.9)), GATE_MOD[st->modifier]);
    int max_n = heat >= 3 ? 3 : 2;
    if (max_n > 3 * tpm) max_n = 3 * tpm;
    if (dist >= 4 && b == 1) shape = complement(shape);
    int chord[24], nch = arp_chord(fin, sc, shape, r, octaves, chord);

    if (b == 0) {
        note(o, RS_V_A_MAIN, fin, ttype == RS_T_BELT ? mulq(dur, F(0.07)) : mulq(note_dur, F(0.95)), 0, vel);
        if (st->action != RS_WAIT)
            note(o, RS_V_A_PING, safe_shift(fin, sc, 2, r), mulq(dur, F(0.18)), mulq(dur, F(0.03)), mulq(vel, F(0.4)));
        if (ttype == RS_T_BELT)
            arp(o, RS_V_A_MAIN, chord, nch, (belt_dir == RS_S || belt_dir == RS_W) ? P_DOWN : P_UP, mulq(dur, F(0.08)),
                rate, gate, mulq(vel, F(0.45)), max_n);
    } else {
        note(o, RS_V_B_FM, fin, ttype == RS_T_BELT ? mulq(dur, F(0.1)) : mulq(note_dur, F(0.9)), 0, mulq(vel, F(0.7)));
        note(o, RS_V_B_SUB, fold(hot - 12, r), mulq(note_dur, F(1.3)), 0, mulq(vel, F(0.55)));
        uint8_t at, ad;
        sound_tile(s, 0, &at, &ad);
        if (ttype == RS_T_BELT && at != RS_T_BELT)      /* one arp voice at a time: not while Scout's runs */
            note(o, RS_V_B_FM, fold(fin + 12, r), gate, mulq(dur, F(0.12)), mulq(vel, F(0.40)));
    }
}

/* ---- playGhostTick: between moves, tpm 2 and 4 */
void rs_score_ghost(const struct rs_state *s, int phase, struct rs_tick_out *o)
{
    static const uint32_t PHASE_VEL[4] = {0, F(0.28), F(0.34), F(0.40)};
    uint32_t dur = rs_step_q8(s);
    uint8_t sc = scale_of(s), at, ad;
    int tpm = s->tpm ? s->tpm : 1;
    sound_tile(s, 0, &at, &ad);
    int scout_arp = at == RS_T_BELT;
    for (int b = 0; b < 2; b++) {
        const struct rs_bot *bot = &s->bot[b], *other = &s->bot[!b];
        uint8_t ttype, tdir;
        sound_tile(s, b, &ttype, &tdir);
        int suppress = b == 1 && scout_arp;
        struct range r = range_of(s, b);
        int hd = heat_deg(bot->heat), hot = safe_shift(note_from_position(s, b), sc, hd, r);
        int fin = final_note(s, b, hot, sc);
        uint32_t heat_vel = ONE + (uint32_t)hd * F(0.06);
        uint8_t belt_dir = ttype == RS_T_BELT ? tdir : bot->dir;
        int dist = (bot->x > other->x ? bot->x - other->x : other->x - bot->x) + (bot->y > other->y ? bot->y - other->y : other->y - bot->y);
        int heat = bot->heat < 0 ? 0 : bot->heat, oct = heat >= 4 ? 2 : 1;
        int chord[24], n;
        if (tpm == 2) {
            uint32_t gv = mulq(F(0.35), heat_vel), gd = mulq(dur, F(0.45));
            if (gv > ONE) gv = ONE;
            if (b == 0) {
                note(o, RS_V_A_PING, safe_shift(fin, sc, 2, r), mulq(gd, F(0.6)), 0, gv);
                if (ttype == RS_T_BELT) {
                    n = arp_chord(fin, sc, chord_shape(bot->x, s->cycle), r, oct, chord);
                    arp(o, RS_V_A_MAIN, chord, n, (belt_dir == RS_S || belt_dir == RS_W) ? P_UP : P_DOWN, mulq(dur, F(0.1)),
                        mulq(dur, F(0.45)), mulq(dur, F(0.35)), mulq(gv, F(0.5)), 2);
                }
            } else {
                note(o, RS_V_B_FM, fin, mulq(gd, F(0.5)), 0, mulq(gv, F(0.7)));
                note(o, RS_V_B_SUB, fold(hot - 12, r), mulq(gd, F(0.8)), 0, mulq(gv, F(0.5)));
                if (ttype == RS_T_BELT && !suppress)
                    note(o, RS_V_B_FM, fold(fin + 12, r), mulq(dur, F(0.35)), mulq(dur, F(0.1)), mulq(gv, F(0.4)));
            }
        } else if (tpm == 4) {
            uint32_t pv = mulq(PHASE_VEL[phase & 3], heat_vel);
            if (pv > ONE) pv = ONE;
            if (phase == 1) {
                if (b == 0) {
                    note(o, RS_V_A_PING, safe_shift(fin, sc, 4, r), mulq(dur, F(0.3)), 0, pv);
                    note(o, RS_V_A_MAIN, fin, mulq(dur, F(0.15)), 0, mulq(pv, F(0.4)));
                    if (ttype == RS_T_BELT) {
                        n = arp_chord(fin, sc, chord_shape(bot->x, s->cycle), r, oct, chord);
                        arp(o, RS_V_A_MAIN, chord, n, P_DOWN, mulq(dur, F(0.04)), mulq(dur, F(0.18)), mulq(dur, F(0.10)),
                            mulq(pv, F(0.5)), 3);
                    }
                } else {
                    note(o, RS_V_B_FM, fin, mulq(dur, F(0.35)), 0, mulq(pv, F(0.7)));
                    note(o, RS_V_B_SUB, fold(hot - 12, r), mulq(dur, F(0.5)), 0, mulq(pv, F(0.5)));
                    if (ttype == RS_T_BELT && !suppress)
                        note(o, RS_V_B_FM, fold(fin + 12, r), mulq(dur, F(0.2)), mulq(dur, F(0.05)), mulq(pv, F(0.45)));
                }
            } else if (phase == 2) {
                int o2 = oct > 2 ? oct : 2;
                if (b == 0) {
                    n = arp_chord(fin, sc, chord_shape(bot->x, s->cycle), r, o2, chord);
                    arp(o, RS_V_A_MAIN, chord, n, dir_pattern(bot->dir), 0, mulq(dur, F(0.45)), mulq(dur, F(0.3)),
                        mulq(pv, F(0.5)), 2);
                } else if (!suppress) {
                    int sh = chord_shape(bot->x, s->cycle);
                    n = arp_chord(fin, sc, dist >= 4 ? complement(sh) : sh, r, o2, chord);
                    arp(o, RS_V_B_FM, chord, n, dir_pattern(bot->dir), 0, mulq(dur, F(0.5)), mulq(dur, F(0.3)),
                        mulq(pv, F(0.45)), 2);
                }
            } else if (phase == 3) {
                if (b == 0) {
                    int lean = safe_shift(fin, sc, 1, r);
                    note(o, RS_V_A_PING, lean, mulq(dur, F(0.22)), 0, mulq(pv, F(0.8)));
                    if (ttype == RS_T_BELT || heat >= 2) {
                        n = arp_chord(lean, sc, chord_shape(bot->x, s->cycle), r, 1, chord);
                        arp(o, RS_V_A_MAIN, chord, n, P_PINGPONG, mulq(dur, F(0.05)), mulq(dur, F(0.45)), mulq(dur, F(0.3)),
                            mulq(pv, F(0.35)), 2);
                    }
                } else {
                    int lean = safe_shift(fin, sc, -1, r);
                    note(o, RS_V_B_FM, lean, mulq(dur, F(0.25)), 0, mulq(pv, F(0.6)));
                    if ((ttype == RS_T_BELT || heat >= 2) && !suppress)
                        note(o, RS_V_B_FM, fold(lean + 12, r), mulq(dur, F(0.2)), mulq(dur, F(0.05)), mulq(pv, F(0.3)));
                }
            }
        }
    }
}

/* ---- playEventSound, playDrumStep, the coin */
void rs_score_event(const struct rs_state *s, uint8_t kind, uint32_t at, struct rs_tick_out *o)
{
    (void)s;
    switch (kind) {
    case RS_EV_COLLISION: note(o, RS_V_COLLISION, 43, SEC(0.08), at, F(0.7)); break;   /* G2 */
    case RS_EV_BELT: note(o, RS_V_BELT, RS_NOISE, SEC(0.04), at, F(0.35)); break;
    case RS_EV_ROTATOR: note(o, RS_V_ROTATOR, 81, SEC(0.04), at, F(0.45)); break;     /* A5 */
    case RS_EV_ZAP: note(o, RS_V_ZAP, RS_NOISE, SEC(0.06), at, F(0.6)); break;
    case RS_EV_PULSE: note(o, RS_V_PULSE, 84, SEC(0.06), at, F(0.4)); break;           /* C6 */
    case RS_EV_RESET:                                   /* E5, A5, C6 */
        note(o, RS_V_PULSE, 76, SEC(0.07), at, F(0.42));
        note(o, RS_V_PULSE, 81, SEC(0.07), at + SEC(0.07), F(0.4));
        note(o, RS_V_PULSE, 84, SEC(0.06), at + SEC(0.14), F(0.36));
        break;
    case RS_EV_DEATH:                                   /* the explosion: thump C1, brown noise, white crackle */
        note(o, RS_V_EX_THUMP, 24, SEC(0.4), at, F(0.95));
        note(o, RS_V_EX_NOISE, RS_NOISE, SEC(0.35), at, F(0.9));
        note(o, RS_V_EX_CRACKLE, RS_NOISE, SEC(0.18), at + SEC(0.04), F(0.6));
        break;
    default: break;                                     /* move, turn, wait, ping, love: no sound of their own */
    }
}

void rs_score_drums(const struct rs_state *s, uint8_t i, struct rs_tick_out *o)
{
    if (s->drum_k[i]) note(o, RS_V_KICK, 24, SEC(0.18), 0, ONE);   /* C1 */
    if (s->drum_s[i]) note(o, RS_V_SNARE, RS_NOISE, SEC(0.1), 0, F(0.7));
    if (s->drum_h[i]) note(o, RS_V_HIHAT, RS_NOISE, SEC(0.03), 0, F(0.5));
}

void rs_score_ghost_hat(const struct rs_state *s, int phase, struct rs_tick_out *o)
{
    int tpm = s->tpm ? s->tpm : 1;
    note(o, RS_V_HIHAT, RS_NOISE, SEC(0.02), 0, phase == tpm / 2 ? F(0.25) : F(0.12));
}

void rs_score_coin(const struct rs_state *s, struct rs_tick_out *o)
{
    uint8_t sc = scale_of(s);
    int lo = rs_scale_quantize(83, sc), hi = rs_scale_quantize(lo + 5, sc);   /* ~B5, a 4th-ish above */
    note(o, RS_V_COIN, lo, SEC(0.06), 0, F(0.7));
    note(o, RS_V_COIN, hi, SEC(0.08), SEC(0.07), F(0.8));
}

/* ---- PLUS (D33): RoboSeq's own, on top of the tick's score as the HTML made it (CLASSIC never comes here). The
 * robots still choose the notes; this voices them on the harmony Voice Mod is on, and gives the bar a pulse. */
static int bar_len(const struct rs_state *s) { return s->loop_len % 3 == 0 && s->loop_len % 4 != 0 ? 12 : 16; }
static int beat_len(const struct rs_state *s) { return bar_len(s) / 4; }

static int chord_root(const struct rs_state *s, const struct rs_bassmod_step *ms)   /* the chord's root, a pitch class */
{
    return s->transpose_root ? ms->root % 12 : 0;
}

static int chord_tone(int pc, uint8_t sc)               /* pc above the root: is it the chord's 1, 3 or 5? */
{
    uint16_t m = 0;
    for (int i = 0; i < SCALE_LEN[sc]; i++) m |= (uint16_t)(1u << SCALE_LIST[sc][i]);
    if (pc == 0) return 1;
    if (pc == 3 || pc == 4) return (m >> pc & 1) && !(pc == 3 && (m >> 4 & 1));   /* its third: major's, else minor's */
    if (pc == 7) return m >> 7 & 1;
    if (pc == 6 || pc == 8) return (m >> pc & 1) && !(m >> 7 & 1);                  /* no fifth: the one it has */
    return 0;
}

static int snap_chord(int m, int root, uint8_t sc, struct range r)   /* the nearest chord tone, down first */
{
    for (int d = 0; d <= 6; d++) {
        if (chord_tone(mod12(m - d - root), sc) && m - d >= r.min) return m - d;
        if (chord_tone(mod12(m + d - root), sc) && m + d <= r.max) return m + d;
    }
    return m;
}

static uint16_t scale_vel(uint16_t v, uint32_t f)       /* a velocity times f (Q20), at most 1 */
{
    uint32_t x = mulq(v, f);
    return (uint16_t)(x > 32767u ? 32767u : x);
}

/* PLUS's drums (D33c): the pattern as the player set it, played by a drummer. A phrase is a Voice Mod round (its
 * steps x how often it moves; 4 loops when it is manual or shorter than 2); in its second half (section B) the
 * hats open up and fill in; its last bar ends in a fill and the next one starts with a crash. Every choice is a hash
 * of the loop and the step: the same each time the same loop comes round, and no draw from the game's random. */
static uint32_t mix32(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ c * 0xC2B2AE3Du;
    h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12, h *= 0x297A2D39u, h ^= h >> 15;
    return h;
}

static struct rs_note *find_note(struct rs_tick_out *o, uint8_t voice)   /* the voice's note at the tick's start */
{
    for (int i = 0; i < o->nnotes; i++)
        if (o->notes[i].voice == voice && o->notes[i].at == 0) return &o->notes[i];
    return 0;
}

static void drum(struct rs_tick_out *o, uint8_t voice, uint32_t dur, uint32_t vel_q20)   /* set or add its hit */
{
    struct rs_note *n = find_note(o, voice);
    if (n) n->dur = dur, n->vel = (uint16_t)((vel_q20 + 16u) >> 5);
    else note(o, voice, voice == RS_V_KICK ? 24 : RS_NOISE, dur, 0, vel_q20);
}

static void plus_drums(const struct rs_state *s, struct rs_tick_out *o, int bar, int beat)
{
    int len = rs_drum_steps(s->loop_len), dp = (s->drum_tick + len - 1) % len, any = 0, i;
    if (!s->drums_on || (!o->move_tick && !o->loop_reset)) return;
    for (i = 0; i < len; i++) any |= s->drum_k[i] | s->drum_s[i] | s->drum_h[i];
    if (!any) return;                                   /* (CLEAR: the player wants them quiet) */
    if (len != bar) bar = len, beat = len / 4;
    uint32_t phrase = s->vm_manual ? 4u : (uint32_t)s->bassmod_len * s->vm_freq4 / 4u;
    if (phrase < 2u) phrase = 4u;
    uint32_t in = s->cycle % phrase, sec_b = in >= phrase / 2u, last = in == phrase - 1u;
    int nk = s->drum_k[dp], ns = s->drum_s[dp], nh = s->drum_h[dp], nxs = s->drum_s[(dp + 1) % len];
    int hs = s->bot[0].heat + s->bot[1].heat;
    uint32_t h = mix32(s->cycle, (uint32_t)dp, 0x5EEDu), heat = hs > 0 ? (uint32_t)hs : 0u;
    uint32_t hum = F(0.96) + (h & 31u) * (F(0.08) / 31u);   /* a hair either way, never two hits the same */
    if (last && dp >= bar - 4) {                        /* the fill: the bar's last four, no hats */
        struct rs_note *hat = find_note(o, RS_V_HIHAT);
        int k = dp - (bar - 4);
        if (hat) hat->vel = 0;                          /* (silent: the voice keeps its place in the order) */
        if (mix32(s->cycle, 99u, 1u) & 1u) {            /* a roll, rising */
            static const uint32_t ROLL[4] = {F(0.30), F(0.42), F(0.56), F(0.74)};
            drum(o, RS_V_SNARE, SEC(0.07), ROLL[k]);
            if (k == 3) drum(o, RS_V_KICK, SEC(0.18), F(0.9));
        } else {                                        /* kick, snare, kick, snare */
            if (k & 1) drum(o, RS_V_SNARE, SEC(0.09), k == 3 ? F(0.78) : F(0.6));
            else drum(o, RS_V_KICK, SEC(0.18), F(0.88));
        }
        return;
    }
    if (dp == 0 && in == 0 && s->cycle > 0)             /* a new phrase: the crash (the hat's noise, long) */
        drum(o, RS_V_HIHAT, SEC(0.55), F(0.5));
    else if (nh)                                        /* the hats: the beats, the eighths, the sixteenths in between */
        drum(o, RS_V_HIHAT, sec_b && dp == bar - 2 ? SEC(0.16) : SEC(0.03),
             mulq(dp % beat == 0 ? F(0.5) : dp % 2 == 0 ? F(0.4) : F(0.27), hum));
    else if ((dp & 1) && (sec_b || heat >= 6u) && (h >> 8 & 255u) < 140u)
        drum(o, RS_V_HIHAT, SEC(0.025), mulq(F(0.2), hum));   /* B, or the bots hot: the sixteenths fill in */
    else if (sec_b && dp == bar - 2)
        drum(o, RS_V_HIHAT, SEC(0.16), F(0.4));         /* B: the open hat on four's and */
    if (nk)
        drum(o, RS_V_KICK, SEC(0.18), mulq(dp == 0 ? F(1.0) : dp == bar / 2 ? F(0.92) : F(0.82), hum));
    else if ((s->cycle & 1u) && dp == bar - 6 && (h >> 16 & 255u) < 160u)
        drum(o, RS_V_KICK, SEC(0.16), F(0.7));          /* every other loop: a kick pushing into four */
    if (ns)
        drum(o, RS_V_SNARE, SEC(0.1), mulq(dp % beat == 0 ? F(0.72) : F(0.5), hum));
    else if ((dp & 1) && nxs && (h >> 24) < 110u)
        drum(o, RS_V_SNARE, SEC(0.05), F(0.16));        /* a ghost before the backbeat */
}

void rs_score_plus(const struct rs_state *s, struct rs_tick_out *o)
{
    /* (the moves and the loop's reset tick, its one: the bots stand at their pads there, the drums and now the bass
     * play; the ghost ticks stay the HTML's) */
    if ((!o->move_tick && !o->loop_reset) || s->step < 0) return;
    const struct rs_bassmod_step *ms = mod_step(s);
    uint8_t sc = scale_of(s);
    int root = chord_root(s, ms), bar = bar_len(s), beat = beat_len(s), p = s->step % bar;
    struct range bass = {31, 52};                       /* G1 .. E3: under the bots */
    int thr = s->vm_freq4 * s->loop_len / 4, change = !s->vm_manual && s->vm_count + 1 >= (thr < 1 ? 1 : thr);
    int has_sub = 0;
    /* the harmony: on the beats the bots land on the chord's tones (the one is the reset's, but in a loop longer
     * than the bar), in between they move as the board has them */
    for (int i = 0; i < o->nnotes; i++) {
        struct rs_note *n = &o->notes[i];
        if (n->at != 0) continue;
        if (n->voice == RS_V_A_MAIN || n->voice == RS_V_B_FM || n->voice == RS_V_A_PING) {
            struct range r = range_of(s, n->voice == RS_V_B_FM);
            int m = n->midi;
            if (p % beat == 0) m = snap_chord(m, root, sc, r);
            n->midi = (int8_t)m;
        }
        if (n->voice == RS_V_B_SUB) {                   /* the bass: the root on one, the fifth on three, a lead-in */
            int m = fold(n->midi, bass);
            if (p == 0) m = fold(48 + root, bass);
            else if (p == bar / 2) m = fold(48 + root + (chord_tone(7, sc) ? 7 : 0), bass);
            else if (p % beat == 0) m = snap_chord(m, root, sc, bass);
            if (change && thr >= 4) {                   /* the step before the chord changes: a step into its root */
                const struct rs_bassmod_step *nx = &s->bassmod[(s->vm_index + 1) % (s->bassmod_len ? s->bassmod_len : 1)];
                int to = fold(48 + chord_root(s, nx), bass), below = to - 1, above = to + 2;
                m = (m - below < 0 ? below - m : m - below) <= (above - m < 0 ? m - above : above - m) ? below : above;
                m = fold(m, bass);
            }
            n->midi = (int8_t)m, has_sub = 1;
        }
    }
    if (!has_sub && (p == 0 || p == bar / 2)) {         /* the reset's one, or Echo-3 sat it out: the bass anyway */
        uint32_t step = rs_step_q8(s) * (uint32_t)(s->tpm ? s->tpm : 1);
        note(o, RS_V_B_SUB, fold(48 + root + (p ? (chord_tone(7, sc) ? 7 : 0) : 0), bass), step * 2u, 0, p ? F(0.42) : F(0.55));
    }
    plus_drums(s, o, bar, beat);
    /* the pulse: the one leans in, the beats a little, the off-sixteenths fall back */
    {
        uint32_t f = p == 0 ? F(1.18) : p == bar / 2 ? F(1.14) : p % beat == 0 ? F(1.08) : (p & 1) ? F(0.82) : F(0.95);
        for (int i = 0; i < o->nnotes; i++) {
            struct rs_note *n = &o->notes[i];
            if (n->voice <= RS_V_B_SUB && n->at < rs_step_q8(s)) n->vel = scale_vel(n->vel, f);
        }
    }
}

