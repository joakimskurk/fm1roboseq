/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Joakim (joakimskurk) */
/* RoboSeq's mix: the HTML's buildAudio() after the voices, for the audio interrupt. The sixteen voices (rs_voice)
 * on their five buses at DEFAULT_MIXER's levels; bot B's bus through its 1400 Hz low-pass; the melody, percussion
 * and FX buses sent at 0.45 into the hall reverb, whose return goes to the master (DECISIONS.md D9); the master
 * through the limiter (Tone.Limiter(-6): Chromium's compressor). Integers only; the tables come from
 * tools/tone-ref/mix_tables.py. */
#ifndef RS_MIX_H
#define RS_MIX_H
#include <stdint.h>
#include "rs_voice.h"
#include "rs_mix_tables.h"
#include "rs_lead.h"

enum rs_bus { RS_BUS_MELODY, RS_BUS_BOTB, RS_BUS_PERC, RS_BUS_FX, RS_BUS_DRUMS, RS_NBUS };
enum rs_level { RS_L_MASTER, RS_L_MELODY, RS_L_BOTB, RS_L_PERC, RS_L_FX, RS_L_DRUMS, RS_L_LEAD, RS_NLEVEL };

#define RS_MIX_SUB 32           /* frames the voices render at a time (the limiter's division) */
#define RS_LIM_BUF 512          /* the limiter's look-ahead (LIM_PRE frames), a power of two */

struct rs_mix {
    struct rs_voice v[RS_NVOICES];
    struct rs_lead lead;                        /* the player's solo voice (D21), on a bus of its own */
    int32_t lead_bus[RS_MIX_SUB];
    volatile int32_t rev_k[8];                  /* the reverb's line gains, Q32 (the board's room: D24) */
    volatile int32_t send_t, tilt_t, drive_t;   /* the board's colour (D24), Q15: the send, a tilt (+ brighter), a drive */
    int32_t send_n, tilt_n, drive_n, tlp[2];    /* where they are on their way; the tilt's low-pass, L and R */
    volatile int32_t dly_target, echo;          /* the lead's delay, samples (three steps); K2's intensity, Q15 (D26) */                /* the lead's delay, samples: three steps (the main loop sets it) */
    int32_t dly_len, dly_lp;
    uint32_t dly_w, dly_hold;
    volatile int32_t level[RS_NLEVEL];          /* Q15: DEFAULT_MIXER; the mix page sets them (one writer) */
    int32_t now_level[RS_NLEVEL];               /* where each is on its way there (applyMixer's 50 ms rampTo) */
    int32_t bus[RS_NBUS][RS_MIX_SUB];
    int32_t lp_x1, lp_x2, lp_y1, lp_y2;         /* bot B's low-pass: x Q15, y Q23 */
    /* the reverb: Q24 */
    int32_t pre[REV_PRE];
    int32_t ap[REV_APS_TOTAL], ap_r[AP_LR], dr[REV_DR];
    int16_t line[REV_LINES_TOTAL];              /* Q16 */
    uint16_t pre_i, ap_i[4], ap_ri, dr_i, line_i[8];
    /* the limiter: Q30 */
    int32_t la[RS_LIM_BUF], la_r[RS_LIM_BUF];   /* Q15, left and right */
    uint16_t la_i;
    int32_t det, gain, scaled, rate, max_att;   /* max_att: 1/65536 dB, -1 none */
    uint8_t releasing;
    uint32_t clips;                             /* a reverb line was full (tests count them) */
    /* the board's patch (D36a): the bots' pulses swing around their duty, Echo-3's the other way */
    volatile uint32_t pw_base[2], pw_swing[2], lfo_inc;   /* Q32 of a cycle; the swing's step a block */
    uint32_t lfo;
};

void rs_mix_init(struct rs_mix *m, uint32_t seed);
/* the board colours the whole sound (D24): its tiles into the reverb's room and send, a tilt and a drive, amount %
 * of the way (0: the HTML's sound); main loop, when the board or the amount changes */
void rs_mix_colour(struct rs_mix *m, const struct rs_board *bd, uint32_t amount);
/* the board's touch on the bots' synths (BAND, D36a; all 0: the HTML's voices); main loop, when the board changes */
void rs_mix_patch(struct rs_mix *m, const struct rs_patch *p);
/* triggerAttackRelease on voice `voice` (RS_V_*): rs_voice_note's arguments */
void rs_mix_note(struct rs_mix *m, uint8_t voice, uint32_t at, uint32_t dur, int midi, uint16_t vel);
void rs_mix_note_fx(struct rs_mix *m, uint8_t voice, uint32_t at, uint32_t dur, int midi, uint16_t vel, uint8_t fx);
/* n frames (a multiple of RS_MIX_SUB) from sample `now` on into out, stereo (L R L R), Q15 in int32 (the limiter's
 * output can pass full scale as the HTML's does: the platform clips) */
void rs_mix_render(struct rs_mix *m, int32_t *out, uint32_t n, uint32_t now);
extern const uint8_t rs_voice_bus[RS_NVOICES];
#endif
