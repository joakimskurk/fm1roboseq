/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Joakim (joakimskurk) */
/* RoboSeq's voices (rs_voice.h), after Tone.js 14.7.77 as the HTML's buildAudio() makes them:
 *
 *   triggerAttackRelease(note, d, t, vel) = triggerAttack(t) then triggerRelease(t + d), in that order.
 *   triggerAttack: the envelope is cancelled and held at t (later envelope events go), then ramps linearly from
 *     where it is to vel over attack x (1 - value) (MembraneSynth: an exponential approach), then decays toward
 *     sustain x vel; the frequency is set at t (never cancelled); the source (re)starts at t, phase 0, and with a
 *     sustain of 0 it stops at t + attack + decay (later source events go).
 *   triggerRelease: cancelled and held at t + d, then toward 0 over release; the source stops at t + d + release.
 *   Tone's exponential approach (exponentialApproachValueAtTime): setTargetAtTime with tau = ln(T + 1) / ln(200)
 *     for 90 % of T, then a linear ramp to the target at T.
 *
 * The oscillators: Web Audio's square is band-limited and normalised to a peak of 1 (its plateau 0.848): a PolyBLEP
 * square x 0.848. Its triangle peaks at 1 and its harmonics fall as 1/n^2: a plain triangle. MembraneSynth: a sine
 * from octaves x f gliding exponentially to f over pitchDecay. Noise: Tone loops a 5 s buffer of Math.random; here
 * xorshift, and its brown filter (y = (y + 0.02 w) / 1.02, x 3.5).
 * Runs in the audio interrupt: no flash (the sine table and the voice table are in RAM), no 64-bit division, every
 * loop bounded. rs_voice_note() runs where the score is made (the main loop) and uses 32-bit division. */
#include "rs_voice.h"

enum { EV_ATTACK, EV_RELEASE, EV_STOP, EV_FREQ };
enum { ST_IDLE, ST_ATTACK_LIN, ST_ATTACK_EXP, ST_ATTACK_EXP_END, ST_ATTACK_END, ST_DECAY_EXP, ST_DECAY_END, ST_SUSTAIN, ST_REL_EXP, ST_REL_END };

#define SR 44100u
#define Q30 (1 << 30)
#define MS(x) ((uint32_t)((x) * 44.1 + 0.5))                  /* ms -> samples */

struct rs_vdef {
    uint8_t osc;
    uint8_t attack_exp;                                         /* MembraneSynth's attackCurve "exponential" */
    uint32_t a, d, r;                                           /* samples */
    int32_t s;                                                  /* sustain, Q30 */
    int32_t kd, kr, ka;                                         /* the approach coefficients 1 - e^(-1 / (tau SR)), Q30 */
    uint8_t octaves;                                            /* MembraneSynth */
    uint32_t pitch_decay;                                       /* samples */
    uint32_t glide_k;                                           /* (1 / octaves)^(1 / pitch_decay), Q30 */
};

/* the coefficient of Tone's approach over T seconds: 1 - exp(-ln 200 / (ln(T + 1) SR)), computed at compile time
 * from a series (the compiler folds it; no float at run time) */
#define LN200 5.298317366548036
#define K_OF(T) ((int32_t)((1.0 - EXPN(-(LN200 / (LN1P(T) * 44100.0)))) * 1073741824.0 + 0.5))
#define LN1P(T) ((T) - (T) * (T) / 2 + (T) * (T) * (T) / 3 - (T) * (T) * (T) * (T) / 4 + (T) * (T) * (T) * (T) * (T) / 5 - \
                 (T) * (T) * (T) * (T) * (T) * (T) / 6 + (T) * (T) * (T) * (T) * (T) * (T) * (T) / 7)
#define EXPN(x) (1.0 + (x) + (x) * (x) / 2 + (x) * (x) * (x) / 6 + (x) * (x) * (x) * (x) / 24)

#define VD(o, A, D, S, R) {o, 0, MS((A) * 1000), MS((D) * 1000), MS((R) * 1000), (int32_t)((S) * 1073741824.0), \
                           K_OF(D), K_OF(R), K_OF(A), 0, 0, 0}
/* the membrane's glide coefficient (1/oct)^(1/n), as exp(-ln(oct) / n) */
#define MEM(D, Rl, oct, LNOCT, pd) {RS_OSC_MEMBRANE, 1, MS(1.0), MS((D) * 1000), MS((Rl) * 1000), 0, K_OF(D), K_OF(Rl), \
                                    K_OF(0.001), oct, MS((pd) * 1000), \
                                    (uint32_t)(EXPN(-(LNOCT) / ((pd) * 44100.0)) * 1073741824.0 + 0.5)}

/* buildAudio(), voice by voice (rs_model.h's RS_V_*) */
static const struct rs_vdef VDEF[RS_NVOICES] = {
    VD(RS_OSC_SQUARE, 0.003, 0.14, 0.06, 0.08),                 /* A.main */
    VD(RS_OSC_TRIANGLE, 0.003, 0.06, 0, 0.04),                  /* A.ping */
    VD(RS_OSC_SQUARE, 0.005, 0.3, 0.4, 0.15),                   /* B.fm */
    VD(RS_OSC_TRIANGLE, 0.008, 0.4, 0.5, 0.2),                  /* B.sub */
    VD(RS_OSC_SQUARE, 0.001, 0.06, 0, 0.03),                    /* fx.collision */
    VD(RS_OSC_WHITE, 0.001, 0.04, 0, 0.02),                     /* fx.belt */
    VD(RS_OSC_TRIANGLE, 0.001, 0.05, 0, 0.02),                  /* fx.rotator */
    VD(RS_OSC_WHITE, 0.001, 0.06, 0, 0.02),                     /* fx.zap */
    VD(RS_OSC_SQUARE, 0.001, 0.05, 0, 0.02),                    /* fx.pulse */
    MEM(0.3, 0.06, 6, 1.791759469228055, 0.04),                 /* drum.kick: pitchDecay 0.04, octaves 6 */
    VD(RS_OSC_WHITE, 0.001, 0.12, 0, 0.04),                     /* drum.snare */
    VD(RS_OSC_WHITE, 0.001, 0.04, 0, 0.01),                     /* drum.hihat */
    VD(RS_OSC_SQUARE, 0.001, 0.08, 0, 0.04),                    /* coin */
    MEM(0.4, 0.1, 4, 1.386294361119891, 0.08),                  /* ex.thump: pitchDecay 0.08, octaves 4 */
    VD(RS_OSC_BROWN, 0.002, 0.35, 0, 0.08),                     /* ex.noise */
    VD(RS_OSC_WHITE, 0.001, 0.18, 0, 0.04),                     /* ex.crackle */
};

/* the bots' four voices shorter and longer (the board's patch, D36a: a pulse pad's board rings on, a zap rail's
 * plucks) */
static const struct rs_vdef VDEF_VAR[2][4] = {
    {VD(RS_OSC_SQUARE, 0.003, 0.09, 0.03, 0.05), VD(RS_OSC_TRIANGLE, 0.003, 0.04, 0, 0.03),
     VD(RS_OSC_SQUARE, 0.005, 0.18, 0.3, 0.09), VD(RS_OSC_TRIANGLE, 0.006, 0.22, 0.35, 0.1)},
    {VD(RS_OSC_SQUARE, 0.003, 0.24, 0.12, 0.16), VD(RS_OSC_TRIANGLE, 0.003, 0.1, 0, 0.07),
     VD(RS_OSC_SQUARE, 0.005, 0.45, 0.5, 0.26), VD(RS_OSC_TRIANGLE, 0.008, 0.6, 0.6, 0.32)},
};
/* BAND's kit (D41): the kick long and tight, the snare short, mid and long, the hihat closed, half, open and a crash */
static const struct rs_vdef KIT_KICK[2] = {MEM(0.5, 0.12, 6, 1.791759469228055, 0.05), MEM(0.17, 0.04, 6, 1.791759469228055, 0.03)};
static const struct rs_vdef KIT_SNARE[3] = {VD(RS_OSC_WHITE, 0.001, 0.08, 0, 0.03), VD(RS_OSC_WHITE, 0.001, 0.14, 0, 0.05),
                                            VD(RS_OSC_WHITE, 0.001, 0.22, 0, 0.08)};
static const struct rs_vdef KIT_HAT[4] = {VD(RS_OSC_WHITE, 0.001, 0.035, 0, 0.01), VD(RS_OSC_WHITE, 0.001, 0.09, 0, 0.03),
                                          VD(RS_OSC_WHITE, 0.001, 0.25, 0, 0.08), VD(RS_OSC_WHITE, 0.001, 0.9, 0, 0.3)};
static inline const struct rs_vdef *vdef(const struct rs_voice *v)
{
    uint8_t e = v->shape;
    if (v->kit && v->def == RS_V_SNARE) { uint32_t l = (uint32_t)(v->kit - 1) >> 5 & 3u; return &KIT_SNARE[l > 2 ? 2 : l]; }
    if (v->kit && (v->def == RS_V_HIHAT || v->def == RS_V_EX_CRACKLE)) return &KIT_HAT[(uint32_t)(v->kit - 1) >> 3 & 3u];
    if (v->def == RS_V_KICK && (v->fx & (RS_KICK_LONG | RS_KICK_TIGHT))) return &KIT_KICK[v->fx & RS_KICK_LONG ? 0 : 1];
    return e && e <= 2 && v->def < 4 ? &VDEF_VAR[e - 1][v->def] : &VDEF[v->def];
}

/* the phase step of each midi note: 440 x 2^((m - 69) / 12) / 44100 x 2^32, from A4's and the twelfth root */
static uint32_t note_inc[128];
static int16_t sine_tab[1025];

static void tables_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    /* 2^(1/12) in Q30 and A4 = 440 Hz */
    const uint64_t SEMI = 1137589835ull;                        /* 2^(1/12) x 2^30 */
    uint64_t inc = (uint64_t)42852281ull;                       /* 440 / 44100 x 2^32 */
    note_inc[69] = (uint32_t)inc;
    for (int m = 70; m < 128; m++) inc = (inc * SEMI + (1u << 29)) >> 30, note_inc[m] = (uint32_t)inc;
    const uint64_t ISEMI = 1013477326ull;                       /* 2^(-1/12) x 2^30 (no 64-bit division) */
    inc = 42852281ull;
    for (int m = 68; m >= 0; m--) inc = (inc * ISEMI + (1u << 29)) >> 30, note_inc[m] = (uint32_t)inc;
    /* a quarter-wave free sine by its recurrence: s(n+1) = 2 cos(w) s(n) - s(n-1), w = 2 pi / 4096 (init only) */
    int64_t c = 2147481121ll;                                   /* cos(2 pi / 4096) x 2^31 */
    int64_t s0 = 0, s1 = 3294197ll;                             /* sin(2 pi / 4096) x 2^31 */
    for (int i = 0; i <= 1024; i++) {
        sine_tab[i] = (int16_t)((s0 + (1 << 15)) >> 16);
        int64_t s2 = ((c * s1) >> 30) - s0;
        s0 = s1, s1 = s2;
    }
    sine_tab[1024] = 32767;
}

static int32_t sine_q15(uint32_t ph)                            /* a full cycle over 2^32, from the quarter table */
{
    uint32_t q = ph >> 30, i = (ph >> 20) & 1023u, f = (ph >> 4) & 0xFFFFu;
    int32_t a, b;
    if (q & 1) a = sine_tab[1024 - i], b = sine_tab[1023 - i];
    else a = sine_tab[i], b = sine_tab[i + 1];
    int32_t s = a + (int32_t)(((int64_t)(b - a) * f) >> 16);
    return q & 2 ? -s : s;
}

void rs_voice_init(struct rs_voice *v, uint8_t def, uint32_t seed)
{
    uint8_t *p = (uint8_t *)v;
    for (unsigned i = 0; i < sizeof *v; i++) p[i] = 0;
    v->def = def;
    v->rng = seed ? seed : 0x9E3779B9u;
    tables_init();
}

/* ---- the timeline */
static int before(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }

static void cancel_after(struct rs_voice *v, uint32_t t, int kinds)   /* bit k: EV_k events after t go */
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < v->nq; i++)
        if (!((kinds >> (v->q[i].kind & 15)) & 1) || !before(t, v->q[i].at)) v->q[n++] = v->q[i];
    v->nq = n;
}

uint32_t rs_voice_dropped;                                     /* events a full timeline lost (the tests want 0) */
static void insert(struct rs_voice *v, uint32_t at, uint8_t kind, int midi, uint16_t vel)
{
    if (v->nq >= RS_VQ) { rs_voice_dropped++; return; }
    uint8_t i = v->nq;
    while (i > 0 && before(at, v->q[i - 1].at)) { v->q[i] = v->q[i - 1]; i--; }   /* (same time: after the earlier) */
    v->q[i].at = at, v->q[i].kind = kind, v->q[i].midi = (int8_t)midi, v->q[i].vel = vel;
    v->nq++;
}

#define ENVK ((1 << EV_ATTACK) | (1 << EV_RELEASE))
#define SRCK (1 << EV_STOP)

void rs_voice_note(struct rs_voice *v, uint32_t at, uint32_t dur, int midi, uint16_t vel)
{
    rs_voice_note_fx(v, at, dur, midi, vel, 0);
}

void rs_voice_note_fx(struct rs_voice *v, uint32_t at, uint32_t dur, int midi, uint16_t vel, uint8_t fx)
{
    const struct rs_vdef *d = vdef(v);
    /* triggerAttack(at) */
    cancel_after(v, at, ENVK | SRCK);
    insert(v, at, EV_ATTACK, midi, vel);
    if (d->osc != RS_OSC_WHITE && d->osc != RS_OSC_BROWN) insert(v, at, (uint8_t)(EV_FREQ | (fx & 15) << 4), midi, vel);
    if (d->s == 0) insert(v, at + d->a + d->d, EV_STOP, 0, 0);
    /* triggerRelease(at + dur) */
    uint32_t rt = at + dur;
    cancel_after(v, rt, ENVK);
    insert(v, rt, EV_RELEASE, 0, 0);
    insert(v, rt + d->r, EV_STOP, 0, 0);
}

void rs_voice_release(struct rs_voice *v, uint32_t at)  /* triggerRelease(at): the panic's */
{
    cancel_after(v, at, ENVK | SRCK);
    insert(v, at, EV_RELEASE, 0, 0);
    insert(v, at + vdef(v)->r, EV_STOP, 0, 0);
}

/* ---- the envelope */
static void lin(struct rs_voice *v, uint8_t stage, int32_t target, uint32_t n)
{
    v->stage = stage, v->target = target, v->left = n ? n : 1;
    v->step = (int32_t)((target - v->env) / (int32_t)v->left);
}

static void kit_strike(struct rs_voice *v, int midi)    /* a snare's or a hihat's hit: the HTML's noise, or BAND's tone */
{
    v->kit = midi >= 0 ? (uint8_t)(midi + 1) : 0, v->kit_age = 0, v->hp_x = v->hp_y = 0;
    if (v->kit && v->def == RS_V_SNARE) {               /* the body: 147-262 Hz, from half again above, falling */
        static const uint8_t BODY[8] = {50, 51, 53, 54, 56, 57, 59, 60};
        v->body_end = note_inc[BODY[midi & 7]], v->body_inc = v->body_end + v->body_end / 2u, v->body_ph = 0, v->body_amp = 26000;
    }
}

static void apply(struct rs_voice *v, const struct rs_vev *e)
{
    if ((e->kind & 15) == EV_ATTACK && (v->def == RS_V_SNARE || v->def == RS_V_HIHAT || v->def == RS_V_EX_CRACKLE))
        kit_strike(v, e->midi);                         /* (BAND's crash rides the crackle's voice: its own, to ring) */
    const struct rs_vdef *d = vdef(v);
    switch (e->kind & 15) {
    case EV_ATTACK: {
        int32_t vel = (int32_t)e->vel << 15;                    /* Q15 -> Q30 */
        v->peak = vel;
        if (!v->running) v->running = 1;
        v->phase = 0;                                           /* a new source: phase 0 */
        if (d->attack_exp) {                                    /* targetRampTo(vel, attack) */
            v->stage = ST_ATTACK_EXP, v->target = vel, v->step = d->ka;
            v->left = d->a * 9 / 10;
        } else {                                                /* linear, the remaining part of the attack */
            int32_t cur = v->env > 0 ? v->env : 0;
            uint32_t n = cur >= Q30 ? 0 : (uint32_t)(((int64_t)d->a * (Q30 - cur)) >> 30);
            if (n == 0) { v->env = vel; v->stage = ST_ATTACK_END; v->left = 0; }
            else lin(v, ST_ATTACK_LIN, vel, n);
        }
        break;
    }
    case EV_RELEASE:
        if (v->env > 0) { v->stage = ST_REL_EXP, v->target = 0, v->step = d->kr, v->left = d->r * 9 / 10; }
        break;
    case EV_STOP: v->running = 0; break;
    case EV_FREQ:
        v->fx = (uint8_t)(e->kind >> 4), v->fx_age = 0, v->kit_age = 0;   /* (its gesture starts with it) */
        v->inc = note_inc[(uint8_t)e->midi & 127];
        v->binv = 0xFFFFFFFFu / (v->inc >> 15 ? v->inc >> 15 : 1u);   /* (32-bit, once a note) */
        if (d->osc == RS_OSC_MEMBRANE) {                        /* from octaves x f down to f over pitchDecay */
            v->inc_end = v->inc;
            v->inc *= d->octaves;
            v->glide_k = d->glide_k, v->glide_left = d->pitch_decay;
        }
        break;
    }
}

static void env_step(struct rs_voice *v)
{
    const struct rs_vdef *d = vdef(v);
    switch (v->stage) {
    case ST_ATTACK_LIN:
        v->env += v->step;
        if (--v->left == 0) { v->env = v->target; v->stage = ST_ATTACK_END; }
        break;
    case ST_ATTACK_EXP:
        v->env += (int32_t)(((int64_t)(v->target - v->env) * v->step) >> 30);
        if (v->left == 0 || --v->left == 0) lin(v, ST_ATTACK_EXP_END, v->target, d->a - d->a * 9 / 10);
        break;
    case ST_ATTACK_EXP_END:                                   /* the attack's last 10 %, linear */
        v->env += v->step;
        if (--v->left == 0) { v->env = v->target; v->stage = ST_ATTACK_END; }
        break;
    case ST_ATTACK_END: {                                       /* the decay toward sustain x vel */
        int32_t sus = (int32_t)(((int64_t)d->s * v->peak) >> 30);
        v->stage = ST_DECAY_EXP, v->target = sus, v->step = d->kd, v->left = d->d * 9 / 10;
        break;
    }
    case ST_DECAY_EXP:
        v->env += (int32_t)(((int64_t)(v->target - v->env) * v->step) >> 30);
        if (--v->left == 0) lin(v, ST_DECAY_END, v->target, d->d - d->d * 9 / 10);
        break;
    case ST_DECAY_END:
        v->env += v->step;
        if (--v->left == 0) { v->env = v->target; v->stage = ST_SUSTAIN; }
        break;
    case ST_REL_EXP:
        v->env += (int32_t)(((int64_t)(0 - v->env) * v->step) >> 30);
        if (--v->left == 0) lin(v, ST_REL_END, 0, d->r - d->r * 9 / 10);
        break;
    case ST_REL_END:
        v->env += v->step;
        if (--v->left == 0) { v->env = 0; v->stage = ST_IDLE; }
        break;
    default: break;
    }
}

/* env_step k times, each sample's level into e: a stage at a time, the same arithmetic as env_step */
static void env_fill(struct rs_voice *v, int32_t *e, uint32_t k)
{
    const struct rs_vdef *d = vdef(v);
    uint32_t j = 0, c, u;
    while (j < k) {
        int32_t env = v->env;
        switch (v->stage) {
        case ST_ATTACK_LIN:
        case ST_ATTACK_EXP_END:
        case ST_DECAY_END:
        case ST_REL_END: {                                      /* linear: env += step, the last one lands on target */
            int32_t st = v->step;
            c = v->left < k - j ? v->left : k - j;
            for (u = 0; u < c; u++) env += st, e[j++] = env;
            v->env = env, v->left -= c;
            if (v->left == 0) {
                if (v->stage == ST_DECAY_END) v->env = v->target, v->stage = ST_SUSTAIN;
                else if (v->stage == ST_REL_END) v->env = 0, v->stage = ST_IDLE;
                else v->env = v->target, v->stage = ST_ATTACK_END;
                e[j - 1] = v->env;
            }
            break;
        }
        case ST_ATTACK_EXP:
        case ST_DECAY_EXP:
        case ST_REL_EXP: {                                      /* toward target, x step (Q30) a sample */
            int32_t tg = v->target, st = v->step;
            c = v->left == 0 ? 1 : v->left < k - j ? v->left : k - j;   /* (an attack of no length: one step) */
            for (u = 0; u < c; u++) env += (int32_t)(((int64_t)(tg - env) * st) >> 30), e[j++] = env;
            v->env = env;
            if (v->left) v->left -= c;
            if (v->left == 0) {
                if (v->stage == ST_ATTACK_EXP) lin(v, ST_ATTACK_EXP_END, tg, d->a - d->a * 9 / 10);
                else if (v->stage == ST_DECAY_EXP) lin(v, ST_DECAY_END, tg, d->d - d->d * 9 / 10);
                else lin(v, ST_REL_END, 0, d->r - d->r * 9 / 10);
            }
            break;
        }
        case ST_ATTACK_END:                                     /* a sample to turn to the decay */
            env_step(v);
            e[j++] = v->env;
            break;
        default:                                                /* sustain, idle: level */
            while (j < k) e[j++] = env;
            break;
        }
    }
}

/* ---- the sources */
#define SQUARE_GAIN 27787                                       /* 0.848 x 32768: Web Audio's square, normalised */

static inline int32_t blep(uint32_t t, uint32_t dt, uint32_t inv)      /* the PolyBLEP correction, Q15, both sides of t = 0 */
{                                                               /* inv: 2^47 / dt, so x = t / dt in Q15 */
    if (t < dt) {
        int32_t x = (int32_t)((uint64_t)(t >> 15) * inv >> 17);
        return 2 * x - ((x * x) >> 15) - 32768;
    }
    uint32_t u = 0u - t;                                        /* the distance before the edge */
    if (u < dt) {
        int32_t x = -(int32_t)((uint64_t)(u >> 15) * inv >> 17);
        return ((x * x) >> 15) + 2 * x + 32768;
    }
    return 0;
}

static int32_t source(struct rs_voice *v)                       /* one sample, Q15 */
{
    const struct rs_vdef *d = vdef(v);
    int32_t s = 0;
    switch (d->osc) {
    case RS_OSC_SQUARE: {
        uint32_t t = v->phase, dt = v->inc;
        s = t < 0x80000000u ? 32767 : -32767;
        s += blep(t, dt, v->binv);                              /* the rising edge at 0 */
        s -= blep(t ^ 0x80000000u, dt, v->binv);                /* the falling edge at 1/2 */
        s = (s * SQUARE_GAIN) >> 15;
        v->phase += v->inc;
        break;
    }
    case RS_OSC_TRIANGLE: {
        uint32_t t = v->phase;                                  /* 0 rising to 1 at 1/4, -1 at 3/4 */
        int32_t x = (int32_t)(t >> 15);                         /* 0..131071 */
        s = x < 32768 ? x : x < 98304 ? 65536 - x : x - 131072;
        v->phase += v->inc;
        break;
    }
    case RS_OSC_MEMBRANE:
        s = sine_q15(v->phase);
        v->phase += v->inc;
        if (v->glide_left) {
            v->inc = (uint32_t)(((uint64_t)v->inc * v->glide_k) >> 30);
            if (--v->glide_left == 0) v->inc = v->inc_end;
        }
        break;
    case RS_OSC_WHITE:
    case RS_OSC_BROWN: {
        uint32_t x = v->rng;
        x ^= x << 13, x ^= x >> 17, x ^= x << 5;
        v->rng = x;
        int32_t w = (int32_t)(x >> 16) - 32768;                 /* -1..1, Q15 */
        if (d->osc == RS_OSC_WHITE && v->kit) {             /* BAND's kit (D41): the noise high-passed; a snare's body */
            static const int16_t HP_SNARE[4] = {30081, 27623, 24642, 20775};   /* 0.6, 1.2, 2, 3.2 kHz: exp(-2 pi fc / fs), Q15 */
            static const int16_t HP_HAT[8] = {21365, 18547, 16057, 13926, 12091, 10486, 9109, 7897};   /* 3 .. 10 kHz */
            uint32_t k = (uint32_t)(v->kit - 1);
            int snare = v->def == RS_V_SNARE;
            int32_t a = snare ? HP_SNARE[k >> 3 & 3u] : HP_HAT[k & 7u];
            int32_t y = (int32_t)(((int64_t)a * (v->hp_y + w - v->hp_x)) >> 15);
            v->hp_x = w, v->hp_y = y;
            if (snare) {                                    /* the crack and a body falling from half again above */
                int32_t body = (int32_t)(((int64_t)sine_q15(v->body_ph) * v->body_amp) >> 15);
                v->body_ph += v->body_inc;
                v->body_inc = v->body_end + (uint32_t)(((uint64_t)(v->body_inc - v->body_end) * 32650u) >> 15);   /* (6 ms) */
                v->body_amp = (v->body_amp * 32740) >> 15;  /* (26 ms) */
                s = y + (y >> 1) + body;
            } else
                s = y * 2 + (y >> 1);                       /* (the high-passed noise up to the HTML's hat's level) */
            break;
        }
        if (d->osc == RS_OSC_WHITE) { s = w; break; }
        /* y = (y + 0.02 w) / 1.02, out 3.5 y: y in Q15 x 2^8 for the small steps */
        v->brown = (int32_t)(((int64_t)v->brown * 1028016 + (int64_t)w * 256 * 20560) >> 20);   /* 1/1.02, 0.02/1.02, Q20 */
        s = (int32_t)(((int64_t)v->brown * 7) >> 9);           /* x 3.5, back from x 256 */
        break;
    }
    }
    return s;
}

/* a note's gesture (D37a): the phase step for the next k samples, off the note's own by a vibrato and a bend. Per
 * block of at most 32 samples, integers; the vibrato a 5.5 Hz sine, the bend a straight line back in 46 ms */
#define VIB_INC 535700u                                         /* 5.5 Hz: 5.5 / 44100 x 2^32 */
#define VIB_LATE 11200000                                       /* 2^(18/1200) - 1, Q30 */
#define VIB_WIDE 25100000                                       /* 2^(40/1200) - 1, Q30 */
#define BEND_FROM_BELOW (-60200000)                             /* 2^(-1/12) - 1, Q30 */
#define BEND_FROM_ABOVE 63900000                                /* 2^(1/12) - 1, Q30 */
static uint32_t fx_inc(struct rs_voice *v, uint32_t k)
{
    int32_t dm = 0;
    uint32_t age = v->fx_age;
    if (v->fx & (RS_FX_VIB_LATE | RS_FX_VIB_WIDE)) {
        int wide = v->fx & RS_FX_VIB_WIDE;
        uint32_t delay = wide ? 0u : 11025u, a = age > delay ? age - delay : 0u;
        if (a) {                                                /* it grows over 16384 samples (0.37 s) */
            int32_t full = wide ? VIB_WIDE : VIB_LATE, depth = a >= 16384u ? full : (int32_t)(((int64_t)full * a) >> 14);
            v->vib_ph += VIB_INC * k;
            dm += (int32_t)(((int64_t)depth * sine_q15(v->vib_ph)) >> 15);
        }
    }
    if ((v->fx & (RS_FX_BEND_UP | RS_FX_BEND_DOWN)) && age < 2048u)
        dm += (int32_t)(((int64_t)(v->fx & RS_FX_BEND_UP ? BEND_FROM_BELOW : BEND_FROM_ABOVE) * (int32_t)(2048u - age)) >> 11);
    v->fx_age = age + k;
    return (uint32_t)((int64_t)v->inc + (((int64_t)v->inc * dm) >> 30));
}

/* the source times the envelope into out, k samples without an event: a loop for each kind of source */
static inline int32_t vlevel(int32_t s, int32_t env) { return (int32_t)(((int64_t)(s * 4) * env) >> 32); }   /* s env / 2^30 */
static void voice_play(struct rs_voice *v, int32_t *out, const int32_t *e, uint32_t k)
{
    const struct rs_vdef *d = vdef(v);
    uint32_t j, ph = v->phase, inc = v->inc, inv = v->binv;
    if (v->fx && (d->osc == RS_OSC_SQUARE || d->osc == RS_OSC_TRIANGLE))   /* (a gesture: the pitch moves a little) */
        inc = fx_inc(v, k), inv = 0xFFFFFFFFu / (inc >> 15 ? inc >> 15 : 1u);
    switch (d->osc) {
    case RS_OSC_SQUARE: {
        uint32_t pw = v->pw;
        int32_t soft = v->soft;
        if ((pw == 0 || pw == 0x80000000u) && !soft) {
            for (j = 0; j < k; j++, ph += inc) {
                int32_t s = ph < 0x80000000u ? 32767 : -32767;
                s += blep(ph, inc, inv);                        /* the rising edge at 0 */
                s -= blep(ph ^ 0x80000000u, inc, inv);          /* the falling edge at 1/2 */
                out[j] += vlevel((s * SQUARE_GAIN) >> 15, e[j]);
            }
            break;
        }
        if (pw == 0) pw = 0x80000000u;                      /* the board's patch: a pulse, a triangle in it */
        int32_t dc = (int32_t)(pw >> 16) - 32768;           /* (the pulse's mean, Q15, taken out) */
        for (j = 0; j < k; j++, ph += inc) {
            int32_t s = ph < pw ? 32767 : -32767;
            s += blep(ph, inc, inv);
            s -= blep(ph - pw, inc, inv);                       /* the falling edge at pw */
            s = ((s - dc) * SQUARE_GAIN) >> 15;
            if (soft) {
                int32_t x = (int32_t)(ph >> 15), tri = x < 32768 ? x : x < 98304 ? 65536 - x : x - 131072;
                s += ((tri - s) * soft) >> 15;
            }
            out[j] += vlevel(s, e[j]);
        }
        break;
    }
    case RS_OSC_TRIANGLE:
        for (j = 0; j < k; j++, ph += inc) {                    /* 0 rising to 1 at 1/4, -1 at 3/4 */
            int32_t x = (int32_t)(ph >> 15);
            out[j] += vlevel(x < 32768 ? x : x < 98304 ? 65536 - x : x - 131072, e[j]);
        }
        break;
    case RS_OSC_MEMBRANE:
        if (v->def == RS_V_KICK && (v->fx & (RS_KICK_CLICK | RS_KICK_DRIVE))) {   /* BAND's kick (D41): driven, a click */
            for (j = 0; j < k; j++) {
                int32_t x = sine_q15(ph);
                if (v->fx & RS_KICK_DRIVE) {                /* 1.5 u - 0.5 u^3 of u = 2 x: a rounder square, the same peak */
                    int32_t u = x * 2;
                    u = u > 32767 ? 32767 : u < -32767 ? -32767 : u;
                    int32_t u3 = (int32_t)(((((int64_t)u * u) >> 15) * u) >> 15);
                    x = (3 * u - u3) >> 1;
                }
                if ((v->fx & RS_KICK_CLICK) && v->kit_age < 160u) {   /* 3.6 ms of noise, falling: the beater */
                    uint32_t r = v->rng;
                    r ^= r << 13, r ^= r >> 17, r ^= r << 5;
                    v->rng = r;
                    x += (((int32_t)(r >> 16) - 32768) * (int32_t)(160u - v->kit_age)) >> 9;   /* (no 64-bit division) */
                }
                v->kit_age++;
                out[j] += vlevel(x, e[j]);
                ph += inc;
                if (v->glide_left) {
                    inc = (uint32_t)(((uint64_t)inc * v->glide_k) >> 30);
                    if (--v->glide_left == 0) inc = v->inc_end;
                }
            }
            v->inc = inc;
            break;
        }
        for (j = 0; j < k; j++) {
            out[j] += vlevel(sine_q15(ph), e[j]);
            ph += inc;
            if (v->glide_left) {
                inc = (uint32_t)(((uint64_t)inc * v->glide_k) >> 30);
                if (--v->glide_left == 0) inc = v->inc_end;
            }
        }
        v->inc = inc;
        break;
    default:
        for (j = 0; j < k; j++) out[j] += vlevel(source(v), e[j]);   /* the noises */
        return;
    }
    v->phase = ph;
}

void rs_voice_render(struct rs_voice *v, int32_t *out, uint32_t n, uint32_t now)
{
    int32_t e[32];
    uint32_t i = 0;
    if (!v->running && v->stage == ST_IDLE && (!v->nq || !before(v->q[0].at, now + n)))
        return;                                                 /* silent, and nothing due in this block */
    while (i < n) {
        uint32_t t = now + i, k = n - i < 32u ? n - i : 32u;
        while (v->nq && !before(t, v->q[0].at)) {               /* the events due now (and any missed) */
            struct rs_vev ev = v->q[0];
            for (uint8_t q = 1; q < v->nq; q++) v->q[q - 1] = v->q[q];
            v->nq--;
            apply(v, &ev);
        }
        if (v->nq && v->q[0].at - t < k) k = v->q[0].at - t;   /* up to the next event */
        if (v->running) env_fill(v, e, k), voice_play(v, out + i, e, k);
        else if (v->stage != ST_IDLE) env_fill(v, e, k);        /* the source stopped; Tone's envelope goes on */
        i += k;
    }
}
