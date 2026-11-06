/* SPDX-License-Identifier: GPL-3.0-only */
/* RoboSeq's lead: a solo voice to play over the robots on the FM-1's keys (REC: D21, D23). Not the HTML's: a sharp
 * SID lead of our own. The pulse and the saw together (the SID's combined waves, more or less), the pulse's width
 * sweeping wide (PWM), folded back where it is driven past its edge (more on the attack), a vibrato that comes in
 * once a note is held, a glide between notes played legato, the last key held winning (monophonic, as the SID's
 * voices), and an open two-pole low-pass that opens further with each note (the SID's filter, a state-variable one
 * here). rs_mix.c adds its ping-pong delay.
 * Runs in the audio interrupt (rs_mix.c renders it); integers only. The main loop asks for notes through `req`
 * (one 32-bit word, one writer). */
#include "rs_lead.h"

static uint32_t lead_inc[128];                  /* a note's phase step (2^32 a cycle, 44.1 kHz) */
static uint32_t lead_frac[65];                  /* 2^(k / 64 / 12), Q30: the 64ths of a semitone */

void rs_lead_init(struct rs_lead *l)
{
    uint8_t *p = (uint8_t *)l;
    for (unsigned i = 0; i < sizeof *l; i++) p[i] = 0;
    uint64_t inc = 42852281ull;                 /* A4: 440 / 44100 x 2^32; up and down by 2^(+-1/12) */
    lead_inc[69] = (uint32_t)inc;
    for (int m = 70; m < 128; m++) inc = (inc * 1137589835ull + (1u << 29)) >> 30, lead_inc[m] = (uint32_t)inc;
    inc = 42852281ull;
    for (int m = 68; m >= 0; m--) inc = (inc * 1013477326ull + (1u << 29)) >> 30, lead_inc[m] = (uint32_t)inc;
    uint64_t f = 1u << 30;                      /* 2^(1/768) x 2^30 = 1074711849, a 64th of a semitone */
    for (int k = 0; k <= 64; k++) lead_frac[k] = (uint32_t)f, f = (f * 1074711849ull + (1u << 29)) >> 30;
    l->pitch = l->target = 69 << 8;
}

void rs_lead_play(struct rs_lead *l, int note, int gate, int pressed)   /* main loop: what the keys hold now */
{
    uint32_t count = (l->req >> 9) + (pressed ? 1u : 0u);
    l->req = (count & 0x7FFFFFu) << 9 | (gate ? 1u << 8 : 0u) | ((uint32_t)note & 127u);
}

static inline int32_t lead_blep(uint32_t t, uint32_t dt, uint32_t inv)   /* PolyBLEP at a step, Q15 (rs_voice's) */
{
    if (t < dt) {
        int32_t x = (int32_t)((uint64_t)(t >> 15) * inv >> 17);
        return 2 * x - ((x * x) >> 15) - 32768;
    }
    uint32_t u = 0u - t;
    if (u < dt) {
        int32_t x = -(int32_t)((uint64_t)(u >> 15) * inv >> 17);
        return ((x * x) >> 15) + 2 * x + 32768;
    }
    return 0;
}

static uint32_t lead_inc_of(int32_t pitch)      /* Q8 semitones -> phase step */
{
    if (pitch < 0) pitch = 0;
    if (pitch > 126 << 8) pitch = 126 << 8;
    uint32_t semi = (uint32_t)pitch >> 8, k = ((uint32_t)pitch & 255u) >> 2, r = (uint32_t)pitch & 3u;
    uint32_t f = lead_frac[k] + (uint32_t)(((uint64_t)(lead_frac[k + 1] - lead_frac[k]) * r) >> 2);
    return (uint32_t)(((uint64_t)lead_inc[semi] * f) >> 30);
}

static int32_t tri_q15(uint32_t ph)             /* a triangle, -32768 .. 32767 over 2^32 */
{
    int32_t x = (int32_t)(ph >> 15);            /* 0 .. 131071 */
    return x < 65536 ? x - 32768 : 98303 - x;
}

/* the sound: a 4 ms attack, a decay of 250 ms to 3/4, a 200 ms release (linear, Q30) */
#define LEAD_ATTACK (44100u * 4u / 1000u)
#define LEAD_DECAY (44100u * 250u / 1000u)
#define LEAD_RELEASE (44100u * 200u / 1000u)
#define LEAD_SUSTAIN (3 << 28)
#define LEAD_GLIDE 9                            /* the glide: a ninth of the way each 32 frames, ~45 ms */
#define VIB_DELAY (44100u * 250u / 1000u)       /* the vibrato comes in after 250 ms held, over 300 ms */
#define VIB_RAMP (44100u * 300u / 1000u)
#define VIB_DEPTH 64                            /* +-1/4 semitone (Q8), 5.5 Hz */
#define VIB_RATE 535637u                        /* 5.5 / 44100 x 2^32 */
#define PWM_RATE 155843u                        /* 1.6 Hz: the pulse's width sweeps 10 % .. 90 % */
enum { L_IDLE, L_ATTACK, L_DECAY, L_SUSTAIN, L_RELEASE };

void rs_lead_render(struct rs_lead *l, int32_t *out, uint32_t n)
{
    uint32_t req = l->req, i;
    if (req != l->seen) {                       /* what the keys did since the last block */
        int note = (int)(req & 127u), gate = (int)(req >> 8 & 1u), pressed = (req >> 9) != (l->seen >> 9);
        if (gate) {
            l->target = note << 8;
            if (pressed && !l->gate) {          /* a new note from silence: from its pitch, the envelopes again */
                l->pitch = l->target, l->stage = L_ATTACK, l->age = 0, l->fenv = 1 << 15;
                l->step = (int32_t)((1 << 30) - (l->env > 0 ? l->env : 0)) / (int32_t)LEAD_ATTACK;
            } else if (pressed)                 /* played legato: it glides there, the filter opens a little */
                l->fenv = l->fenv < (1 << 14) ? 1 << 14 : l->fenv;
        } else if (l->gate) {
            l->stage = L_RELEASE, l->step = -(l->env / (int32_t)LEAD_RELEASE) - 1;
        }
        l->gate = (uint8_t)gate, l->seen = req;
    }
    if (l->stage == L_IDLE) return;             /* silent: nothing to do */
    for (uint32_t f0 = 0; f0 < n; f0 += 32) {   /* a block of 32: the pitch, the width, the filter */
        uint32_t m = n - f0 < 32 ? n - f0 : 32;
        int32_t d = l->target - l->pitch;
        l->pitch += d / LEAD_GLIDE ? d / LEAD_GLIDE : d;
        int32_t vib = 0;
        if (l->age > VIB_DELAY) {
            uint32_t a = l->age - VIB_DELAY;
            int32_t depth = a >= VIB_RAMP ? VIB_DEPTH : (int32_t)(VIB_DEPTH * a / VIB_RAMP);
            vib = (tri_q15(l->vib_ph) * depth) >> 15;
        }
        l->vib_ph += VIB_RATE * m, l->pwm_ph += PWM_RATE * m, l->age += m;
        uint32_t inc = lead_inc_of(l->pitch + vib), binv = 0xFFFFFFFFu / ((inc >> 15) ? inc >> 15 : 1u);
        uint32_t pw = 0x80000000u + (uint32_t)(tri_q15(l->pwm_ph) * 52428);   /* 50 % +- 40 % */
        /* the filter, open: 1.8 kHz plus the note's own pitch and the envelope's up to +4.5 kHz; f = 2 pi fc / fs */
        int32_t fc = 1800 + ((int32_t)(inc >> 16) * 3) / 2 + ((l->fenv * 4500) >> 15);
        int32_t fq = fc * 4669 / 1000;
        if (fq > 26000) fq = 26000;
        int32_t drive = 22118 + (l->fenv >> 1);     /* the fold's drive, Q14: 1.35, more on the attack */
        l->fenv -= (l->fenv >> 6) + 1;          /* (it closes over ~150 ms) */
        if (l->fenv < 0) l->fenv = 0;
        for (i = 0; i < m; i++) {
            uint32_t ph = l->phase;
            /* the SID's combined wave, more or less: the pulse and the saw together (each its edges smoothed) */
            int32_t pul = ph < pw ? 16384 : -16384, b0 = lead_blep(ph, inc, binv);
            pul += (b0 - lead_blep(ph - pw, inc, binv)) >> 1;
            int32_t saw = (int32_t)(ph >> 17) - 16384 - (b0 >> 1);
            int32_t s = ((pul + saw) * 5) >> 3;
            s = (s * drive) >> 14;                  /* folded back where it runs past the edge: a sharper tone */
            if (s > 16384) s = 32768 - s;
            else if (s < -16384) s = -32768 - s;
            if (s > 16384) s = 16384;
            else if (s < -16384) s = -16384;
            l->phase = ph + inc;
            int32_t hp = s - l->lp - ((l->bp * 29491) >> 15);   /* damping 0.9: an open filter, a little edge */
            l->bp += (fq * hp) >> 15;
            l->lp += (fq * l->bp) >> 15;
            switch (l->stage) {                 /* the envelope */
            case L_ATTACK:
                l->env += l->step;
                if (l->env >= 1 << 30) l->env = 1 << 30, l->stage = L_DECAY, l->step = -((1 << 30) - LEAD_SUSTAIN) / (int32_t)LEAD_DECAY;
                break;
            case L_DECAY:
                l->env += l->step;
                if (l->env <= LEAD_SUSTAIN) l->env = LEAD_SUSTAIN, l->stage = L_SUSTAIN;
                break;
            case L_RELEASE:
                l->env += l->step;
                if (l->env <= 0) l->env = 0, l->stage = L_IDLE;
                break;
            default: break;
            }
            out[f0 + i] += (int32_t)(((int64_t)l->lp * l->env) >> 30);
        }
        if (l->stage == L_IDLE) break;
    }
}
