/* SPDX-License-Identifier: GPL-3.0-only */
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
        if (!((kinds >> v->q[i].kind) & 1) || !before(t, v->q[i].at)) v->q[n++] = v->q[i];
    v->nq = n;
}

static void insert(struct rs_voice *v, uint32_t at, uint8_t kind, int midi, uint16_t vel)
{
    if (v->nq >= RS_VQ) return;
    uint8_t i = v->nq;
    while (i > 0 && before(at, v->q[i - 1].at)) { v->q[i] = v->q[i - 1]; i--; }   /* (same time: after the earlier) */
    v->q[i].at = at, v->q[i].kind = kind, v->q[i].midi = (int8_t)midi, v->q[i].vel = vel;
    v->nq++;
}

#define ENVK ((1 << EV_ATTACK) | (1 << EV_RELEASE))
#define SRCK (1 << EV_STOP)

void rs_voice_note(struct rs_voice *v, uint32_t at, uint32_t dur, int midi, uint16_t vel)
{
    const struct rs_vdef *d = &VDEF[v->def];
    /* triggerAttack(at) */
    cancel_after(v, at, ENVK | SRCK);
    insert(v, at, EV_ATTACK, midi, vel);
    if (d->osc != RS_OSC_WHITE && d->osc != RS_OSC_BROWN) insert(v, at, EV_FREQ, midi, vel);
    if (d->s == 0) insert(v, at + d->a + d->d, EV_STOP, 0, 0);
    /* triggerRelease(at + dur) */
    uint32_t rt = at + dur;
    cancel_after(v, rt, ENVK);
    insert(v, rt, EV_RELEASE, 0, 0);
    insert(v, rt + d->r, EV_STOP, 0, 0);
}

/* ---- the envelope */
static void lin(struct rs_voice *v, uint8_t stage, int32_t target, uint32_t n)
{
    v->stage = stage, v->target = target, v->left = n ? n : 1;
    v->step = (int32_t)((target - v->env) / (int32_t)v->left);
}

static void apply(struct rs_voice *v, const struct rs_vev *e)
{
    const struct rs_vdef *d = &VDEF[v->def];
    switch (e->kind) {
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
    const struct rs_vdef *d = &VDEF[v->def];
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

/* ---- the sources */
#define SQUARE_GAIN 27787                                       /* 0.848 x 32768: Web Audio's square, normalised */

static int32_t blep(uint32_t t, uint32_t dt, uint32_t inv)      /* the PolyBLEP correction, Q15, both sides of t = 0 */
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
    const struct rs_vdef *d = &VDEF[v->def];
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
        if (d->osc == RS_OSC_WHITE) { s = w; break; }
        /* y = (y + 0.02 w) / 1.02, out 3.5 y: y in Q15 x 2^8 for the small steps */
        v->brown = (int32_t)(((int64_t)v->brown * 1028016 + (int64_t)w * 256 * 20560) >> 20);   /* 1/1.02, 0.02/1.02, Q20 */
        s = (int32_t)(((int64_t)v->brown * 7) >> 9);           /* x 3.5, back from x 256 */
        break;
    }
    }
    return s;
}

void rs_voice_render(struct rs_voice *v, int32_t *out, uint32_t n, uint32_t now)
{
    for (uint32_t i = 0; i < n; i++, now++) {
        while (v->nq && !before(now, v->q[0].at)) {             /* the events due now (and any missed) */
            struct rs_vev e = v->q[0];
            for (uint8_t k = 1; k < v->nq; k++) v->q[k - 1] = v->q[k];
            v->nq--;
            apply(v, &e);
        }
        if (!v->running) {                                      /* the source stopped; Tone's envelope goes on */
            if (v->stage != ST_IDLE) env_step(v);
            continue;
        }
        int32_t s = source(v);
        env_step(v);
        out[i] += (int32_t)(((int64_t)s * v->env) >> 30);
    }
}
