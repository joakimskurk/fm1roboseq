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
    /* the player's knobs while REC is on (D26), Q15, set by the main loop: K1 the cutoff, K4 the bite */
    volatile int32_t cut, bite;                 /* 16384: as D23; cut 0 a quarter, 32768 2.5 times; bite the fold, PWM */
    /* the arp (K3): its notes, up or down in the scale, one each step the robots take */
    volatile int8_t arp_notes[8];
    volatile uint8_t arp_n;                     /* 0 or 1: off */
    volatile uint32_t tick_at[8], tick_head;    /* the steps' times (audio frames), as the main loop schedules them */
    uint32_t tick_tail, arp_pos;
    int32_t held;                               /* the key's own note, Q8 (the arp leaves it, off it comes back) */
};

void rs_lead_init(struct rs_lead *l);
/* main loop: the note the keys hold now (gate 0: none), pressed: a key went down just now */
void rs_lead_play(struct rs_lead *l, int note, int gate, int pressed);
/* the interrupt: n frames from frame `now` on added into out (mono, Q15) */
void rs_lead_render(struct rs_lead *l, int32_t *out, uint32_t n, uint32_t now);
#endif
