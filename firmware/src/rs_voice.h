/* SPDX-License-Identifier: GPL-3.0-only */
/* RoboSeq's voices: the HTML's sixteen Tone.js instruments (buildAudio) as fixed-point voices for the audio
 * interrupt. Each voice is monophonic as Tone's Synth, NoiseSynth and MembraneSynth are: notes arrive in the HTML's
 * call order (rs_score) and edit the voice's timeline the way Tone edits its envelope, frequency and source
 * (rs_voice.c says how); the interrupt renders it. Integers only: Q30 envelopes, Q15 samples, 32-bit phases. */
#ifndef RS_VOICE_H
#define RS_VOICE_H
#include <stdint.h>
#include "rs_model.h"

#define RS_VQ 24                /* a voice's timeline: pending attacks, releases, stops, frequencies */

enum rs_osc { RS_OSC_SQUARE, RS_OSC_TRIANGLE, RS_OSC_MEMBRANE, RS_OSC_WHITE, RS_OSC_BROWN };

struct rs_vev { uint32_t at; uint8_t kind; int8_t midi; uint16_t vel; };   /* at: the sample (absolute) */

struct rs_voice {
    uint8_t def;                                                /* RS_V_* */
    /* the timeline, in time order */
    uint8_t nq;
    struct rs_vev q[RS_VQ];
    /* the source */
    uint8_t running;
    uint32_t phase, inc, binv;                                  /* the oscillator (2^32 a cycle); 2^47 / inc */
    uint32_t inc_end, glide_k;                                  /* the membrane's glide: to inc_end, x glide_k (Q30) a sample */
    uint32_t glide_left;
    uint32_t rng;                                               /* the noise */
    int32_t brown;                                              /* the brown noise's integrator, Q15 */
    /* the envelope (Tone's Envelope on an AudioParam): Q30 */
    uint8_t stage;
    int32_t env, target, step;                                  /* a linear stage's step a sample, or a curve's coefficient */
    uint32_t left;                                              /* samples left in the stage */
    int32_t peak;                                               /* the attack's velocity (decay goes to sustain x it) */
};

void rs_voice_init(struct rs_voice *v, uint8_t def, uint32_t seed);
/* triggerAttackRelease(midi, duration, time, velocity): start and length in samples (absolute), velocity Q15 */
void rs_voice_note(struct rs_voice *v, uint32_t at, uint32_t dur, int midi, uint16_t vel);
/* adds n samples from sample `now` on into out (Q15 in int32), the voice's own level (Tone's output, 1 = 32768) */
void rs_voice_render(struct rs_voice *v, int32_t *out, uint32_t n, uint32_t now);
#endif
