/* SPDX-License-Identifier: GPL-3.0-only */
/* RoboSeq's lead (rs_lead.c): a SID-style solo voice for the FM-1's keys, played over the robots (D21). */
#ifndef RS_LEAD_H
#define RS_LEAD_H
#include <stdint.h>

struct rs_lead {
    volatile uint32_t req;                      /* the main loop's: presses << 9 | gate << 8 | note */
    uint32_t seen;                              /* the interrupt's: the last req it acted on */
    uint8_t gate, stage;
    int32_t pitch, target;                      /* Q8 semitones: where it sounds, where it glides to */
    uint32_t phase, pwm_ph, vib_ph, age;        /* age: samples since the note began (the vibrato's delay) */
    int32_t env, step, fenv;                    /* the envelope (Q30) and its step; the filter's envelope (Q15) */
    int32_t lp, bp;                             /* the filter's state */
};

void rs_lead_init(struct rs_lead *l);
/* main loop: the note the keys hold now (gate 0: none), pressed: a key went down just now */
void rs_lead_play(struct rs_lead *l, int note, int gate, int pressed);
/* the interrupt: n frames added into out (mono, Q15) */
void rs_lead_render(struct rs_lead *l, int32_t *out, uint32_t n);
#endif
