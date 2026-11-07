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
    n->voice = voice, n->midi = (int8_t)midi, n->dur = dur, n->at = at, n->fx = 0;
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

/* ---- BAND (D33): RoboSeq's own, on top of the tick's score as the HTML made it (ORIGINAL never comes here). The
 * robots still choose the notes; this voices them on the harmony Voice Mod is on, and gives the bar a pulse. */
static int bar_len(const struct rs_state *s) { return s->loop_len % 3 == 0 && s->loop_len % 4 != 0 ? 12 : 16; }
static int beat_len(const struct rs_state *s) { return bar_len(s) / 4; }

/* The HTML's harmony as it really sounds: every note is quantized to the step's scale ON C (scaleQuantize works on
 * pitch classes from C), and the step's root only shifts the board's degrees before that. So the chord a step
 * sounds is its scale's triad on the scale's note nearest the root (the lower on a tie): the default song's
 * Eb C F A major is ii I IV vi of C major, Dm C F Am. BAND's chord tones, bass and lead-ins are all of that scale. */
static uint16_t scale_mask(uint8_t sc)
{
    uint16_t m = 0;
    for (int i = 0; i < SCALE_LEN[sc]; i++) m |= (uint16_t)(1u << SCALE_LIST[sc][i]);
    return m;
}

static int chord_root(const struct rs_state *s, const struct rs_bassmod_step *ms, uint8_t sc)   /* a pitch class */
{
    return mod12(rs_scale_quantize(60 + (s->transpose_root ? ms->root % 12 : 0), sc));
}

static int third_of(int root, uint8_t sc)               /* the triad's third over root in the scale: 4, 3, or none */
{
    uint16_t m = scale_mask(sc);
    return m >> mod12(root + 4) & 1 ? 4 : m >> mod12(root + 3) & 1 ? 3 : -1;
}

static int fifth_of(int root, uint8_t sc)               /* its fifth: 7, else the 6 or 8 the scale has, or none */
{
    uint16_t m = scale_mask(sc);
    return m >> mod12(root + 7) & 1 ? 7 : m >> mod12(root + 6) & 1 ? 6 : m >> mod12(root + 8) & 1 ? 8 : -1;
}

static int chord_tone(int midi, int root, uint8_t sc)   /* is midi the chord's 1, 3 or 5? */
{
    int rel = mod12(midi - root);
    return rel == 0 || rel == third_of(root, sc) || rel == fifth_of(root, sc);
}

static int snap_chord(int m, int root, uint8_t sc, struct range r)   /* the nearest chord tone, down first */
{
    for (int d = 0; d <= 6; d++) {
        if (chord_tone(m - d, root, sc) && m - d >= r.min) return m - d;
        if (chord_tone(m + d, root, sc) && m + d <= r.max) return m + d;
    }
    return m;
}

static uint16_t scale_vel(uint16_t v, uint32_t f)       /* a velocity times f (Q20), at most 1 */
{
    uint32_t x = mulq(v, f);
    return (uint16_t)(x > 32767u ? 32767u : x);
}

/* BAND's drums (D33c): the pattern as the player set it, played by a drummer. A phrase is a Voice Mod round (its
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

uint32_t rs_band_phrase(const struct rs_state *s)      /* a phrase: a Voice Mod round in loops (4 when manual or short) */
{
    uint32_t phrase = s->vm_manual ? 4u : (uint32_t)s->bassmod_len * s->vm_freq4 / 4u;
    return phrase < 2u ? 4u : phrase;
}

int rs_band_section(const struct rs_state *s)           /* the phrase's second half: section B */
{
    uint32_t phrase = rs_band_phrase(s);
    return s->cycle % phrase >= phrase / 2u;
}

int rs_band_peak(const struct rs_state *s)              /* every other phrase's B: the big one, as loud as the HTML's */
{
    return rs_band_section(s) && (s->cycle / rs_band_phrase(s) & 1u);
}

static void band_drums(const struct rs_state *s, struct rs_tick_out *o, int bar, int beat)
{
    int len = rs_drum_steps(s->loop_len), dp = (s->drum_tick + len - 1) % len, any = 0, i;
    if (!s->drums_on || (!o->move_tick && !o->loop_reset)) return;
    for (i = 0; i < len; i++) any |= s->drum_k[i] | s->drum_s[i] | s->drum_h[i];
    if (!any) return;                                   /* (CLEAR: the player wants them quiet) */
    if (len != bar) bar = len, beat = len / 4;
    uint32_t phrase = rs_band_phrase(s), in = s->cycle % phrase, sec_b = (uint32_t)rs_band_section(s), last = in == phrase - 1u;
    int nk = s->drum_k[dp], ns = s->drum_s[dp], nh = s->drum_h[dp], nxs = s->drum_s[(dp + 1) % len];
    int hs = s->bot[0].heat + s->bot[1].heat, peak = rs_band_peak(s);
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
             mulq(dp % beat == 0 ? F(0.5) : dp % 2 == 0 ? F(0.4) : F(0.27), peak ? mulq(hum, F(1.15)) : hum));
    else if ((dp & 1) && (sec_b || heat >= 6u) && (peak || (h >> 8 & 255u) < 140u))
        drum(o, RS_V_HIHAT, SEC(0.025), mulq(peak ? F(0.26) : F(0.2), hum));   /* B, the bots hot: the sixteenths fill in */
    else if (sec_b && dp == bar - 2)
        drum(o, RS_V_HIHAT, SEC(0.16), F(0.4));         /* B: the open hat on four's and */
    if (nk)
        drum(o, RS_V_KICK, SEC(0.18), mulq(dp == 0 ? F(1.0) : dp == bar / 2 ? F(0.92) : F(0.82), hum));
    else if ((s->cycle & 1u) && dp == bar - 6 && (h >> 16 & 255u) < 160u)
        drum(o, RS_V_KICK, SEC(0.16), F(0.7));          /* every other loop: a kick pushing into four */
    else if (peak && dp == bar / 2 + 2 && (h >> 16 & 255u) < 170u)
        drum(o, RS_V_KICK, SEC(0.16), F(0.74));         /* the big one: a kick on three's and */
    if (ns)
        drum(o, RS_V_SNARE, SEC(0.1), mulq(dp % beat == 0 ? F(0.72) : F(0.5), hum));
    else if ((dp & 1) && nxs && (h >> 24) < 110u)
        drum(o, RS_V_SNARE, SEC(0.05), F(0.16));        /* a ghost before the backbeat */
}

static int clashes_but(const struct rs_tick_out *o, int m, const struct rs_note *self)   /* against the others' notes */
{
    for (int i = 0; i < o->nnotes; i++) {
        const struct rs_note *n = &o->notes[i];
        if (n == self || n->at || !n->vel || n->voice > RS_V_B_SUB || n->voice == RS_V_A_PING) continue;
        if (self && self->voice == RS_V_B_FM && n->voice == RS_V_B_SUB) continue;   /* (the bass then moves off it) */
        int ic = mod12(n->midi - m);
        if (ic == 1 || ic == 11 || ic == 6) return 1;
    }
    return 0;
}

static int clashes(const struct rs_tick_out *o, int m)  /* a semitone, a major 7th or a tritone against a bot's note */
{
    for (int i = 0; i < o->nnotes; i++) {
        const struct rs_note *n = &o->notes[i];
        if (n->at || !n->vel || (n->voice != RS_V_A_MAIN && n->voice != RS_V_B_FM)) continue;
        int ic = mod12(n->midi - m);
        if (ic == 1 || ic == 11 || ic == 6) return 1;
    }
    return 0;
}

static void unrub(const struct rs_state *s, struct rs_tick_out *o, int root, uint8_t sc, struct range bass)
{
    for (int i = 0; i < o->nnotes; i++) {               /* no rub between them: Echo-3 a scale step off Scout-7's */
        struct rs_note *n = &o->notes[i];                /* semitone or tritone, the bass off both */
        if (n->at || !n->vel || (n->voice != RS_V_B_FM && n->voice != RS_V_B_SUB)) continue;
        struct range r = n->voice == RS_V_B_SUB ? bass : range_of(s, 1);
        int m = n->midi;
        if (n->voice == RS_V_B_SUB && clashes(o, m)) m = snap_chord(m, root, sc, r);
        static const int8_t TRY[5] = {0, -1, 1, -2, 2};
        for (int k = 0; k < 5; k++) {
            int c = k ? fold(rs_scale_shift(m, sc, TRY[k]), r) : m;
            if (!clashes_but(o, c, n)) { m = c; break; }
        }
        n->midi = (int8_t)m;
    }
}

/* ---- BAND's own music (D34, D35): the same game, the same voices, a new way from the one to the other. The HTML
 * gives each bot a note from where it stands, every step. Here each bot is a musician, and the board's cells are a
 * keyboard: a cell is a degree of the key, x plus the rows up, so a step on the board is a step in the scale and a
 * dash a third. The bots go back to their pads every loop and walk the same way again, so what they sing comes back
 * every bar: a tune, its rhythm from when they move (a note starts where a bot gets to a new cell and holds while it
 * turns or pushes at a wall; a wait is a rest). Scout-7 sings it, Echo-3 answers in its gaps and, in a phrase's
 * second half, sings with it; the bass plays the board's own groove; all in the chord Voice Mod is on, in its
 * scale. The HTML's notes for the bots' four voices are dropped; the tiles' and events' sounds stay, in tune with
 * the chord; the drums are D33c's drummer. */
static int iabs(int x) { return x < 0 ? -x : x; }   /* (no libc on the chip) */
static int fdiv(int a, int n) { return a >= 0 ? a / n : -((n - 1 - a) / n); }
static int note_of(int idx, uint8_t sc)                 /* the scale's idx-th note from C0 */
{
    int n = SCALE_LEN[sc], o = fdiv(idx, n);
    return 12 * o + SCALE_LIST[sc][idx - o * n];
}
static int idx_of(int midi, uint8_t sc)                 /* a note's place in the scale (quantized first) */
{
    int q = rs_scale_quantize(midi, sc), o = fdiv(q, 12), pc = q - 12 * o, n = SCALE_LEN[sc];
    for (int i = 0; i < n; i++)
        if (SCALE_LIST[sc][i] == pc) return o * n + i;
    return o * n;
}
static int snap_idx(int idx, int root, uint8_t sc)      /* the nearest chord tone, in scale steps (down first) */
{
    static const int8_t D[5] = {0, -1, 1, -2, 2};
    for (int k = 0; k < 5; k++)
        if (chord_tone(note_of(idx + D[k], sc), root, sc)) return idx + D[k];
    return idx;
}
static int keep_in(int idx, uint8_t sc, int lo, int hi)  /* an octave at a time into [lo, hi] (midi) */
{
    int n = SCALE_LEN[sc];
    while (note_of(idx, sc) > hi) idx -= n;
    while (note_of(idx, sc) < lo) idx += n;
    return idx;
}
static void drop_lines(struct rs_tick_out *o)           /* the HTML's notes for the bots' four voices go */
{
    int k = 0;
    for (int i = 0; i < o->nnotes; i++)
        if (o->notes[i].voice > RS_V_B_SUB) o->notes[k++] = o->notes[i];
    o->nnotes = (uint8_t)k;
}
static void tune_sfx(struct rs_tick_out *o, int root, uint8_t sc)   /* the tiles' and events' pitches: chord tones */
{
    for (int i = 0; i < o->nnotes; i++) {
        struct rs_note *n = &o->notes[i];
        if (n->midi == RS_NOISE || n->voice <= RS_V_B_SUB || n->voice == RS_V_BELT || n->voice == RS_V_ZAP) continue;
        for (int d = 0; d <= 6; d++) {
            if (chord_tone(n->midi - d, root, sc)) { n->midi = (int8_t)(n->midi - d); break; }
            if (chord_tone(n->midi + d, root, sc)) { n->midi = (int8_t)(n->midi + d); break; }
        }
    }
}

static int cell_deg(const struct rs_bot *b) { return b->x + (RS_ROWS - 1 - b->y); }   /* 0 (bottom left) .. 8 */
static int hold_of(const struct rs_state *s, int b, int p, int end, int cap)   /* steps it holds: to its register's wait */
{
    const struct rs_prog *pr = &s->prog[b];
    int len = pr->len ? pr->len : s->loop_len, k = 1;
    while (p + k < end && k < cap && pr->s[(s->step + k) % len].action != RS_WAIT) k++;
    return k;
}
static int home_idx(int idx, int pc, uint8_t sc)        /* the nearest note of pitch class pc, in scale steps */
{
    static const int8_t D[9] = {0, -1, 1, -2, 2, -3, 3, -4, 4};
    for (int k = 0; k < 9; k++)
        if (mod12(note_of(idx + D[k], sc)) == mod12(pc)) return idx + D[k];
    return idx;
}

static int under_lead(int idx, int lead, int root, uint8_t sc)   /* the chord tone nearest idx that sounds with lead */
{
    static const int8_t TRY[7] = {0, -1, 1, -2, 2, -3, 3};
    for (int t = 0; t < 7; t++) {
        int c = note_of(idx + TRY[t], sc), ic = mod12(lead - c);
        if (chord_tone(c, root, sc) && ic != 1 && ic != 2 && ic != 6 && ic != 10 && ic != 11) return idx + TRY[t];
    }
    return idx;
}
/* what the tile a bot landed on and its step's modifier do to a note's pitch (D37a, D37b): a left rotator bends it in
 * from below, a right one from above; a pulse pad (it heals: long and sustained) and WOBBLE sing with a wide vibrato
 * at once; any other held note grows a slow one after a quarter second; a zap rail's and GLITCH's notes stay plain */
static uint8_t gesture(uint8_t tile, uint8_t modifier)
{
    if (modifier == RS_GLITCH || tile == RS_T_ZAP) return 0;
    if (tile == RS_T_ROTL) return RS_FX_BEND_UP | RS_FX_VIB_LATE;
    if (tile == RS_T_ROTR) return RS_FX_BEND_DOWN | RS_FX_VIB_LATE;
    if (modifier == RS_WOBBLE || tile == RS_T_PULSE) return RS_FX_VIB_WIDE;
    return RS_FX_VIB_LATE;
}
static void note_fx(struct rs_tick_out *o, uint8_t voice, int midi, uint32_t dur, uint32_t at, uint32_t vel_q20, uint8_t fx)
{
    uint8_t n0 = o->nnotes;
    note(o, voice, midi, dur, at, vel_q20);
    if (o->nnotes > n0) o->notes[n0].fx = fx;
}
/* the arpeggio's tones by the HTML's own arp rules (D37c): the chord's shape from where Scout-7 stands and the loop
 * (getChordShape: triad, seventh, sus4, add9, power, sixth), its span from the heat (two octaves at 4), the other
 * shape when the bots are four tiles or more apart (getComplementShape). From the chord's root at or over lo, up;
 * sus4 is the triad here (its fourth would rub on the third Echo-3 and the bass sound) */
static int arp_tones(const struct rs_state *s, int root, uint8_t sc, int lo, int *out)
{
    const struct rs_bot *a = &s->bot[0], *b = &s->bot[1];
    int shape = chord_shape(a->x, s->cycle), dist = iabs(a->x - b->x) + iabs(a->y - b->y);
    if (dist >= 4) shape = complement(shape);
    if (shape == CH_SUS4) shape = CH_TRIAD;
    int span = a->heat >= 4 ? 2 : 1, n = SCALE_LEN[sc], rm = lo + mod12(root - lo), ri = idx_of(rm, sc), c = 0;
    for (int o = 0; o <= span; o++)
        for (int d = 0; d < CHORD_N[shape]; d++) {
            int m = note_of(ri + CHORD[shape][d] + o * n, sc), dup = 0;
            if (m > rm + 12 * span || c >= 12) continue;
            for (int k = 0; k < c; k++) dup |= out[k] == m;
            if (dup) continue;
            int k = c++;
            for (; k > 0 && out[k - 1] > m; k--) out[k] = out[k - 1];
            out[k] = m;
        }
    return c;
}
static int pat_index(int pat, int u, int n)             /* the u-th note of a contour over n tones (ARP_PATTERNS) */
{
    int w = 2 * n - 2 > 0 ? 2 * n - 2 : 1, v = u % w;
    return pat == P_UP ? u % n : pat == P_DOWN ? n - 1 - u % n : pat == P_UPDOWN ? (v < n ? v : w - v)
         : (u & 1) ? n - 1 - (u / 2) % n : (u / 2) % n;
}
static const uint32_t ARP_GATE[RS_NMODIFIERS] = {F(0.9), F(0.7), F(1.2), F(1.0), F(0.5)};   /* getArpGate: steady .. glitch */

static int snap_dir(int idx, int root, uint8_t sc, int dir)   /* the nearest chord tone, the way the tune goes first */
{
    static const int8_t D[5] = {0, 1, -1, 2, -2};
    int w = dir > 0 ? 1 : -1;
    for (int k = 0; k < 5; k++)
        if (chord_tone(note_of(idx + w * D[k], sc), root, sc)) return idx + w * D[k];
    return idx;
}
static int step_on(int idx, int root, uint8_t sc, int dir, int chord)   /* a step on that way (to a chord tone) */
{
    int w = dir > 0 ? 1 : -1, j = idx + w;
    for (int k = 0; chord && k < 4 && !chord_tone(note_of(j, sc), root, sc); k++) j += w;
    return j;
}

/* the bass's grooves, one a board: a bar in sixteenths, R the root, F the fifth, O the octave, T the third, A a
 * step into the next chord's root, - held, . a rest (a 12-step bar takes a beat's first, third and fourth) */
static const char GROOVE[8][17] = {
    "R..R..O.R..R..FA",                                 /* DEFAULT: three, three, two, twice */
    "R.R.R.R.R.R.R.FO",                                 /* CORRIDOR: the eighths, straight on */
    "R---T---F---A---",                                 /* GARDEN: a walk, a quarter each */
    "R.RRR.RRR.RRF.FO",                                 /* GAUNTLET: a gallop */
    "R.O.R.O.R.O.F.O.",                                 /* SPINNER: octaves, round and round */
    "R..O.OR...R.F..A",                                 /* MAZE: around the corners */
    "R-----..O-..F-A-",                                 /* (a rolled or the user's board, by its tiles) dub: room */
    "R-------F-----.A",                                 /* half time */
};
int rs_band_groove(const struct rs_state *s)
{
    if (s->board_id < RS_NBOARDS) return s->board_id;
    uint32_t h = 0;
    for (int i = 0; i < RS_ROWS * RS_COLS; i++) h = h * 31u + s->board.t[i].type * 4u + s->board.t[i].dir;
    return (int)(mix32(h, 0x6A00u, 7u) & 7u);
}
/* the board's touch on the synths (D36a), a little: the tiles on each bot's side of the board (Scout-7's the
 * lower left, Echo-3's the upper right, the diagonal both) shape its voice. Zap rails narrow its pulse, belts a
 * little too; rotators swing the pulse slowly (belts faster); crates and pulse pads mix a triangle in, darker; pulse
 * pads let it ring longer, zap rails pluck it shorter. The bass rings with pulse pads, plucks among crates. */
void rs_band_patch(const struct rs_board *b, struct rs_patch *p)
{
    int n[2][7] = {{0}}, all[7] = {0};
    for (int y = 0; y < RS_ROWS; y++)
        for (int x = 0; x < RS_COLS; x++) {
            int t = b->t[y * RS_COLS + x].type % 7, deg = x + (RS_ROWS - 1 - y);
            all[t]++;
            if (deg <= 4) n[0][t]++;
            if (deg >= 4) n[1][t]++;
        }
    for (int i = 0; i < 2; i++) {
        int z = n[i][RS_T_ZAP], r = n[i][RS_T_ROTL] + n[i][RS_T_ROTR], bl = n[i][RS_T_BELT], pu = n[i][RS_T_PULSE];
        int k = n[i][RS_T_BLOCK], pw = 32768 - 1300 * z - 500 * bl, sw = 1100 * r, so = 3200 * k + 900 * pu;
        if (pw < 22000) pw = 22000;
        if (sw > 5000) sw = 5000;
        if (pw - sw < 18000) sw = pw - 18000;
        p->pw[i] = (uint16_t)pw, p->pwm[i] = (uint16_t)sw, p->soft[i] = (uint16_t)(so > 11500 ? 11500 : so);
        p->env[i] = (uint8_t)(pu >= 2 && pu > z ? 2 : z >= 3 ? 1 : 0);
    }
    int rate = 200 + 120 * all[RS_T_BELT];
    p->rate = (uint16_t)(rate > 1200 ? 1200 : rate);
    p->env[2] = (uint8_t)(all[RS_T_PULSE] >= 3 ? 2 : all[RS_T_BLOCK] >= 3 ? 1 : 0);
}

static int slot(int p, int bar) { return bar == 12 ? p / 3 * 4 + (p % 3 ? p % 3 + 1 : 0) : p; }

void rs_score_band(struct rs_state *s, struct rs_tick_out *o)
{
    if (s->step < 0) return;
    const struct rs_bassmod_step *ms = mod_step(s);
    uint8_t sc = scale_of(s);
    int n = SCALE_LEN[sc], root = chord_root(s, ms, sc), fifth = fifth_of(root, sc);
    int bar = bar_len(s), beat = beat_len(s), p = s->step % bar, on_beat = p % beat == 0, sec_b = rs_band_section(s);
    int left = s->loop_len - s->step, end = p + (bar - p < left ? bar - p : left);   /* the bar's end (or the loop's) */
    int tpm = s->tpm ? s->tpm : 1;
    uint32_t q = rs_step_q8(s), step = q * (uint32_t)tpm;   /* a tick, a move */
    uint32_t phrase = rs_band_phrase(s), in = s->cycle % phrase;
    struct range bass = {31, 52}, bass_hi = {31, 64};
    /* the intensity (D36b): every other phrase's B is the big one (Scout-7's ping voice runs an arpeggio of the
     * chord over the tune, the bass pumps, the hats fill in, all louder), and the bots' heat (zap rails, the other's
     * dash or ping into it; the HTML's) makes them louder as they run hot and, at 4, strikes a third above Scout-7's
     * notes */
    int peak = sec_b && (s->cycle / phrase & 1u), ping_used = 0;
    int h0 = s->bot[0].heat < 0 ? 0 : s->bot[0].heat > 6 ? 6 : s->bot[0].heat;
    int h1 = s->bot[1].heat < 0 ? 0 : s->bot[1].heat > 6 ? 6 : s->bot[1].heat;
    uint32_t warm0 = F(1.0) + (uint32_t)h0 * F(0.05), warm1 = F(1.0) + (uint32_t)h1 * F(0.05);
    int fill = in == phrase - 1u && left <= bar - p && p >= end - 4;   /* (the drummer's fill: the arpeggio rests) */
    int one = o->loop_reset || (s->cycle == 0 && s->step == 0 && s->move_tick == 1);   /* (and the very first tick) */
    drop_lines(o);
    tune_sfx(o, root, sc);
    if (!o->move_tick && !one) {                        /* a ghost tick: Scout-7's line glints on the and, softly */
        if (s->band_lead && o->ghost_phase == tpm / 2 && !(s->bot[0].dying)) {
            int i = snap_idx(idx_of(s->band_lead, sc) + n, root, sc);
            note(o, RS_V_A_PING, note_of(keep_in(i, sc, 60, 88), sc), q / 2u, 0, F(0.16));
        }
        band_drums(s, o, bar, beat);
        return;
    }
    if (p == 0) {                                       /* a bar: the tunes start again */
        s->band_cell[0] = s->band_cell[1] = -1, s->band_at[0] = s->band_at[1] = 0, s->band_cad = 0, s->band_end = 0;
        s->band_first = 255, s->band_ans = -1;
        for (int i = 0; i < 4; i++) s->band_call[i] = -1;
    }
    /* the bass: the board's groove on the chord's root. Echo-3 plays into it: its dash an octave up, its ping a
     * double, its wait a rest off the beat. */
    {
        const char *g = GROOVE[rs_band_groove(s)];
        char c = g[slot(p, bar)];
        int eact = o->acted[1] && !s->bot[1].dying ? o->played[1].action : -1, pump = 0;
        if ((c == '.' || c == '-') && peak && p % 2 == 0 && !fill)   /* (the big one: it pumps, the chord's note it holds) */
            pump = 1, c = s->band_bass && chord_tone(s->band_bass, root, sc) ? 'P' : 'R';
        if (fill) c = 'W';                              /* the phrase's end: under the drummer's fill, a run into the next */
        if (c != '.' && c != '-' && !(eact == RS_WAIT && !on_beat && c != 'W')) {
            int k = 1, r = fold(48 + root, bass), b = r;
            while (!pump && !fill && p + k < end && g[slot(p + k, bar)] == '-') k++;
            if (c == 'P') b = s->band_bass;              /* (the note it holds, again) */
            else if (c == 'W') {                        /* a scale run to the next chord's root, up (Echo-3 facing north or
                                                         * east as it starts) or down onto it, a step each sixteenth */
                if (p == end - 4 || !s->band_run) s->band_run = s->bot[1].dir == RS_N || s->bot[1].dir == RS_E ? 1 : -1;
                int thr = s->vm_freq4 * s->loop_len / 4, nroot = root, up = s->band_run > 0;
                if (!s->vm_manual && thr >= 1 && thr - s->vm_count <= end - p) {
                    const struct rs_bassmod_step *nx = rs_next_mod_step(s);   /* (the next round's, when it changes) */
                    nroot = chord_root(s, nx, nx->scale < RS_NSCALES ? nx->scale : RS_SC_MAJOR);
                }
                int ti = idx_of(fold(48 + nroot, bass), sc);
                b = note_of(ti + (up ? -(end - p) : end - p), sc);
                b = b < 28 ? b + 12 : b > 60 ? b - 12 : b;
                eact = -1;
            }
            else if (c == 'O') b = fold(r + 12, bass_hi);
            else if (c == 'F') b = fifth > 0 ? fold(r + fifth, bass) : r;
            else if (c == 'T') b = third_of(root, sc) > 0 ? fold(r + third_of(root, sc), bass) : r;
            else if (c == 'A') {                        /* a scale step into the next chord's root (this one's if it stays) */
                int thr = s->vm_freq4 * s->loop_len / 4, nroot = root;
                if (!s->vm_manual && thr >= 1 && thr - s->vm_count <= end - p) {
                    const struct rs_bassmod_step *nx = rs_next_mod_step(s);   /* (the next round's, when it changes) */
                    nroot = chord_root(s, nx, nx->scale < RS_NSCALES ? nx->scale : RS_SC_MAJOR);
                }
                int to = fold(48 + nroot, bass), last = s->band_bass ? s->band_bass : r;
                int c0 = fold(rs_scale_shift(to, sc, -1), bass), c1 = fold(rs_scale_shift(to, sc, 1), bass);
                b = iabs(c0 - last) <= iabs(c1 - last) ? c0 : c1;
            }
            if (eact == RS_DASH && c != 'O') b = fold(b + 12, bass_hi);
            uint32_t vel = pump ? F(0.4) : c == 'W' ? F(0.4) + (uint32_t)(4 - (end - p)) * F(0.05)
                         : p == 0 ? F(0.62) : on_beat ? F(0.54) : p % 2 == 0 ? F(0.48) : F(0.42);
            if (peak) vel = mulq(vel, F(1.08));
            note(o, RS_V_B_SUB, b, mulq(step * (uint32_t)k, k > 1 ? F(0.95) : F(0.8)), 0, vel);
            if (eact == RS_PING) note(o, RS_V_B_SUB, b, mulq(step / 2u, F(0.8)), step / 2u, mulq(vel, F(0.7)));
            s->band_bass = (int8_t)b;
        }
    }
    /* Scout-7: the tune. A note where it gets to a new cell (a ping strikes it again), held while it turns or
     * pushes at a wall, to its register's next wait or the next note. On the beats the chord's own tones, the nearest
     * the way the tune goes; a new cell is always a new note; a note off the beat still sounding on one resolves
     * there, a step to the chord. In a phrase's first half (A) its notes start on the eighths (and on a dash) and
     * the bar's last beat is Echo-3's answer; in B it sings the whole bar, on every step. The bar half way through
     * the phrase ends on the chord's fifth, its last bar on its root: from half the bar on, the first note is that,
     * held, and the tune rests after it. */
    int cad = in == phrase - 1u ? 2 : in == phrase / 2u - 1u ? 1 : 0;
    int zone = cad && left <= bar - p && 2 * p >= end;   /* (the loop's last bar) */
    int answer = !sec_b && cad != 2 && end - beat >= beat;   /* A: the bar's last beat is Echo-3's */
    int calls = answer ? end - beat : end;              /* (Scout-7's notes end before it) */
    int lead_on = 0, lead_idx = 0;
    if (o->move_tick && o->acted[0] && !s->bot[0].dying) {
        const struct rs_bot *a = &s->bot[0];
        const struct rs_step *st = &o->played[0];
        int d = cell_deg(a), act = st->action, land = a->stepped_tile;
        int fresh = (d != s->band_cell[0] || act == RS_PING) && act != RS_WAIT;
        int may = sec_b || p % 2 == 0 || act == RS_DASH;
        if (zone ? !s->band_cad && p < calls : fresh && may && p < calls) {   /* (the cadence lands, moved or not) */
            int dir = s->band_cell[0] >= 0 ? d - s->band_cell[0] : 0, i = idx_of(64, sc) + d, k;
            uint32_t vel = on_beat ? F(0.74) : p % 2 == 0 ? F(0.62) : F(0.52);
            if (zone) {
                i = home_idx(i, cad == 2 || fifth < 0 ? root : root + fifth, sc), k = calls - p, vel = F(0.7);
                s->band_cad = 1;
            } else {
                k = hold_of(s, 0, p, calls, 8);
                if (land == RS_T_PULSE && k < 8) k = calls - p < 8 ? calls - p : 8;   /* a pulse pad: held, to the next note */
                if (on_beat) i = snap_dir(i, root, sc, dir);
            }
            int m = note_of(i, sc), prev = s->band_lead;
            if (s->band_cell[0] >= 0 && prev) {         /* in the bar, no leap past a sixth: the octave nearer */
                if (m - prev > 9 && note_of(i - n, sc) >= 52) i -= n, m = note_of(i, sc);
                else if (prev - m > 9 && note_of(i + n, sc) <= 86) i += n, m = note_of(i, sc);
            }
            if (!zone && dir && m == prev) i = step_on(i, root, sc, dir, on_beat), m = note_of(i, sc);
            if (!on_beat && clashes_but(o, m, 0)) i = snap_dir(i, root, sc, dir), m = note_of(i, sc);   /* (off the bass) */
            uint32_t dur = mulq(step * (uint32_t)k, F(0.94));
            if (act == RS_DASH) vel = mulq(vel, F(1.2));
            vel = mulq(vel, peak ? mulq(warm0, F(1.1)) : warm0);
            if (st->modifier == RS_BRIGHT) vel = mulq(vel, F(1.15));
            else if (st->modifier == RS_LOW) vel = mulq(vel, F(0.8));
            else if (st->modifier == RS_GLITCH) dur = mulq(step, F(0.4));
            if (land == RS_T_PULSE) vel = mulq(vel, F(1.1));
            if (land == RS_T_ZAP) dur = mulq(step, F(0.45)), vel = mulq(vel, F(1.15)), k = 1;   /* a zap rail: short and hard */
            if (vel > ONE) vel = ONE;
            note_fx(o, RS_V_A_MAIN, m, dur, 0, vel, zone ? RS_FX_VIB_LATE : gesture((uint8_t)land, st->modifier));
            if (act == RS_PING)                         /* the glint: a chord tone an octave up */
                note(o, RS_V_A_PING, note_of(keep_in(snap_idx(i + n, root, sc), sc, 60, 90), sc), q, q / 4u, mulq(vel, F(0.5))),
                ping_used = 1;
            if (land == RS_T_BELT) {                    /* riding a belt: an arpeggio after the note, the belt's way, */
                int ct[12], nct = arp_tones(s, root, sc, 60, ct), up = a->stepped_dir == RS_N || a->stepped_dir == RS_E;
                int nn = a->heat >= 3 ? 3 : 2, j0 = 0;       /* two notes (three hot: the HTML's), within the step */
                while (j0 < nct && ct[j0] <= m) j0++;
                if (!up) j0 -= 1;
                while (j0 >= 0 && j0 < nct && !up && ct[j0] >= m) j0--;
                for (int t = 0; t < nn; t++) {
                    int j = up ? j0 + t : j0 - t;
                    if (j < 0 || j >= nct) break;
                    note(o, RS_V_A_PING, ct[j], mulq(step / (uint32_t)(nn + 1), ARP_GATE[st->modifier]),
                         (uint32_t)(t + 1) * (step / (uint32_t)(nn + 1)), mulq(vel, F(0.5)));
                }
                ping_used = 1;
            }
            if (!ping_used && h0 >= 4 && !peak)         /* hot: the HTML's third above, a strike */
                note(o, RS_V_A_PING, note_of(keep_in(i + 2, sc, 60, 90), sc), mulq(step, F(0.2)), mulq(step, F(0.03)),
                     mulq(vel, F(0.4))), ping_used = 1;
            s->band_cell[0] = (int8_t)d, s->band_lead = (int8_t)m, s->band_at[0] = (uint8_t)p;
            s->band_end = (uint8_t)(st->modifier == RS_GLITCH || land == RS_T_ZAP ? p + 1 : p + k);
            if (s->band_first == 255) s->band_first = (uint8_t)p;
            if (p - s->band_first < 4) s->band_call[p - s->band_first] = (int8_t)i;
            lead_on = 1, lead_idx = i;
        }
    }
    int choked = 0;
    if ((o->bumped[0] & 2) && !lead_on && s->band_lead && p < s->band_end && !s->bot[0].dying) {
        note(o, RS_V_A_MAIN, s->band_lead, mulq(q, F(0.1)), 0, 0);   /* a crate: the note it held stops dead (velocity 0) */
        s->band_end = (uint8_t)p, choked = 1;
    }
    if (peak && !ping_used && !fill && !s->bot[0].dying) {
        /* the big one's arpeggio by the HTML's rules: the shape from where Scout-7 stands, the span from its heat, the
         * contour from its facing, how long each rings from the step's modifier, the rate from the tile it is on (a
         * belt twice as fast, a pulse pad half) */
        int ct[12], nct = arp_tones(s, root, sc, 67, ct), land = s->bot[0].stepped_tile;
        int per = land == RS_T_BELT ? 2 : 1, pat = dir_pattern(s->bot[0].dir);
        uint8_t mod = o->acted[0] ? o->played[0].modifier : RS_STEADY;
        uint32_t av = mulq(on_beat ? F(0.78) : p % 2 == 0 ? F(0.66) : F(0.56), warm0);
        if (nct > 1 && (land != RS_T_PULSE || p % 2 == 0))
            for (int t = 0; t < per; t++)
                note(o, RS_V_A_PING, ct[pat_index(pat, p * per + t, nct)], mulq(mulq(q, ARP_GATE[mod % RS_NMODIFIERS]), per == 2 ? F(0.5) : ONE),
                     (uint32_t)t * (q / 2u), t ? mulq(av, F(0.8)) : av);
    }
    if (peak && !lead_on && !choked && !fill && p % 2 == 0 && s->band_lead && p < calls && o->acted[0] && !s->bot[0].dying) {
        int i = snap_idx(idx_of(s->band_lead, sc), root, sc);   /* the big one: the tune's note struck again, the eighths */
        note(o, RS_V_A_MAIN, note_of(i, sc), mulq(step, F(1.6)), 0, mulq(on_beat ? F(0.6) : F(0.5), warm0));
        s->band_lead = (int8_t)note_of(i, sc), lead_on = 2, lead_idx = i;
    }
    if (on_beat && !lead_on && s->band_lead && p < s->band_end && !chord_tone(s->band_lead, root, sc)) {
        int i = snap_idx(idx_of(s->band_lead, sc), root, sc);   /* (held into the beat: it resolves) */
        note(o, RS_V_A_MAIN, note_of(i, sc), mulq(step * (uint32_t)(s->band_end - p), F(0.94)), 0, F(0.46));
        s->band_lead = (int8_t)note_of(i, sc);
    }
    /* Echo-3: in A it answers. In the bar's last beat it sings Scout-7's call again, its first notes as they went,
     * from where Echo-3 stands (the chord tone of its cell, an octave down and under Scout-7's; a wait does not stop
     * it); before that, a note on the beats where it gets to a new cell. In B it sings with Scout-7, the chord's tone
     * a third or a sixth under, as long, and on its own on the beats and in Scout-7's gaps (a note of Scout-7's held
     * two steps or more). */
    if (o->move_tick && o->acted[1] && !s->bot[1].dying) {
        const struct rs_bot *b = &s->bot[1];
        int d = cell_deg(b), act = o->played[1].action, e = 0, k = 1, j = p - calls, eland = b->stepped_tile;
        int fresh = (d != s->band_cell[1] || act == RS_PING) && act != RS_WAIT;
        uint32_t vel = F(0.5);
        if (answer && j >= 0 && j < 4 && s->band_call[0] >= 0) {
            if (s->band_ans < 0) {                      /* where the answer starts: Echo-3's cell, the chord's */
                int i = keep_in(snap_idx(idx_of(52, sc) + d, root, sc), sc, 45, 64);
                while (s->band_lead && note_of(i, sc) > s->band_lead - 3 && note_of(i - n, sc) >= 43) i -= n;
                s->band_ans = (int8_t)i;
            }
            if (s->band_call[j] >= 0) {
                e = note_of(s->band_ans + s->band_call[j] - s->band_call[0], sc), vel = j ? F(0.48) : F(0.54);
                while (k + j < 4 && p + k < end && s->band_call[j + k] < 0) k++;
            }
        } else if (sec_b && lead_on && p % 2 == 0 && act != RS_WAIT) {
            int under = snap_idx(lead_idx - (b->x & 1 ? 5 : 2), root, sc);   /* (a chord tone under it) */
            if (under >= lead_idx) under = snap_idx(under - 2, root, sc);
            under = under_lead(under, note_of(lead_idx, sc), root, sc);
            e = note_of(keep_in(under, sc, 45, 74), sc), vel = F(0.42);
            k = zone ? end - p : hold_of(s, 0, p, end, 8);
        } else if (fresh && (on_beat || (sec_b && !lead_on && p - s->band_at[0] >= 2))) {
            int i = snap_idx(idx_of(52, sc) + d, root, sc);   /* (the chord's tones: it sounds over Scout-7's) */
            i = keep_in(i, sc, 43, 74);
            while (s->band_lead && note_of(i, sc) > s->band_lead - 3 && note_of(i - n, sc) >= 43) i -= n;
            if (p < s->band_end) i = under_lead(i, s->band_lead, root, sc);   /* (under Scout-7's note: a consonance) */
            e = note_of(i, sc), vel = on_beat ? F(0.56) : F(0.48);
            k = hold_of(s, 1, p, answer ? calls : end, 4);
            if (eland == RS_T_PULSE) k = (answer ? calls : end) - p < 6 ? (answer ? calls : end) - p : 6;   /* (held) */
            s->band_cell[1] = (int8_t)d, s->band_at[1] = (uint8_t)p;
        }
        if (!e && peak && p % 2 == 0 && s->band_echo && act != RS_WAIT && !fill)   /* the big one: its chord tone again */
            e = note_of(under_lead(snap_idx(idx_of(s->band_echo, sc), root, sc), s->band_lead, root, sc), sc), vel = F(0.46);
        if (e) {
            uint32_t edur = mulq(step * (uint32_t)k, F(0.9));
            vel = mulq(vel, peak ? mulq(warm1, F(1.08)) : warm1);
            if (eland == RS_T_ZAP) edur = mulq(step, F(0.45)), vel = mulq(vel, F(1.15));   /* a zap rail: short and hard */
            if (vel > ONE) vel = ONE;
            note_fx(o, RS_V_B_FM, e, edur, 0, vel, gesture((uint8_t)eland, o->played[1].modifier)), s->band_echo = (int8_t)e;
            if (b->stepped_tile == RS_T_BELT && !(lead_on && s->bot[0].stepped_tile == RS_T_BELT))
                note(o, RS_V_B_FM, fold(e + 12, (struct range){45, 79}), q / 2u, step / 2u, mulq(vel, F(0.6)));
        } else if ((o->bumped[1] & 2) && s->band_echo && p - s->band_at[1] < 4)
            note(o, RS_V_B_FM, s->band_echo, mulq(q, F(0.1)), 0, 0);   /* a crate: its note stops dead */
    }
    unrub(s, o, root, sc, bass);
    band_drums(s, o, bar, beat);
    {                                                   /* the bar breathes: the one and three lean in */
        uint32_t f = p == 0 ? F(1.12) : p == bar / 2 ? F(1.1) : on_beat ? F(1.04) : F(0.96);
        for (int i = 0; i < o->nnotes; i++)
            if (o->notes[i].voice <= RS_V_B_SUB) o->notes[i].vel = scale_vel(o->notes[i].vel, f);
    }
}
