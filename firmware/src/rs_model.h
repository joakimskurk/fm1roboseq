/* SPDX-License-Identifier: GPL-3.0-only */
/* RoboSeq's game: the board, the two bots, their registers and the sequencers around them, as
 * reference/roboseq.html runs them (advanceSequence and what it calls). Integers only, no platform: it runs the same
 * on the FM-1 and on a computer, where tests/roboseq/ checks it tick by tick against the HTML's golden traces.
 * Not in the audio interrupt: rs_tick() runs once a step from the main loop and leaves what should sound in
 * struct rs_tick_out. */
#ifndef RS_MODEL_H
#define RS_MODEL_H
#include <stdint.h>

#define RS_COLS 5
#define RS_ROWS 5
#define RS_MAX_STEPS 64
#define RS_BASSMOD_MAX 8
#define RS_SEQ2_STEPS 4
#define RS_MAX_EVENTS 24        /* the events of one tick (the HTML keeps them all; a tick makes at most ~14) */

enum rs_dir { RS_N, RS_E, RS_S, RS_W };
enum rs_action { RS_MOVE, RS_BACK, RS_TURNL, RS_TURNR, RS_DASH, RS_PING, RS_WAIT, RS_NACTIONS };
enum rs_modifier { RS_STEADY, RS_BRIGHT, RS_LOW, RS_WOBBLE, RS_GLITCH, RS_NMODIFIERS };
enum rs_tile { RS_T_NONE, RS_T_BELT, RS_T_ZAP, RS_T_ROTL, RS_T_ROTR, RS_T_PULSE, RS_T_BLOCK };
enum rs_event {
    RS_EV_RESET, RS_EV_COLLISION, RS_EV_MOVE, RS_EV_BELT, RS_EV_ROTATOR, RS_EV_ZAP, RS_EV_PULSE, RS_EV_TURN,
    RS_EV_WAIT, RS_EV_PING, RS_EV_DEATH, RS_EV_LOVE, RS_NEVENTS
};
enum rs_board_id { RS_B_DEFAULT, RS_B_CORRIDOR, RS_B_GARDEN, RS_B_GAUNTLET, RS_B_SPINNER, RS_B_MAZE, RS_NBOARDS,
                   RS_B_NONE = 0xFF };   /* (seq 2: "--") */
enum rs_drum_id { RS_D_FOUR, RS_D_BREAK, RS_D_HALF, RS_D_OFFBEAT, RS_D_TRIPLET, RS_D_MINIMAL, RS_NDRUMS,
                  RS_D_NONE = 0xFF };
enum rs_scale { RS_SC_CHROMATIC, RS_SC_MAJOR, RS_SC_MINOR, RS_SC_DORIAN, RS_SC_PHRYGIAN, RS_SC_LYDIAN,
                RS_SC_MIXOLYDIAN, RS_SC_PENTATONIC, RS_SC_MINPENTA, RS_SC_DIMINISHED, RS_SC_WHOLETONE,
                RS_SC_AUGMENTED, RS_NSCALES };

struct rs_step { uint8_t action, modifier, prob; };            /* prob: 100, 75 or 50 (%) */
struct rs_tile_t { uint8_t type, dir; };                        /* dir: a belt's */
struct rs_board { struct rs_tile_t t[RS_ROWS * RS_COLS]; };     /* t[y * RS_COLS + x] */

struct rs_bot {                                                 /* boardState.A / .B */
    int8_t x, y;
    uint8_t dir, dying;
    int16_t heat;                                               /* the HTML's pings and dashes add without a cap */
    uint16_t deaths;
    uint8_t stepped_tile;                                       /* the tile it landed on before a belt moved it */
};

struct rs_prog { struct rs_step s[RS_MAX_STEPS]; uint8_t len; };   /* a bot's register; len 1..64 */
struct rs_bassmod_step { uint8_t scale, root; int8_t semitone; };
struct rs_seq2_step { uint8_t board, drums, regswap; };

struct rs_state {
    /* the settings */
    uint8_t loop_len, tpm;                                      /* 8..64; ticks per move 1, 2, 4 */
    uint8_t vm_manual, vm_freq4;                                /* Voice Mod: LOOP or MANUAL; its frequency x 4 (1..16) */
    uint8_t bassmod_len;                                        /* 4 or 8 */
    struct rs_bassmod_step bassmod[RS_BASSMOD_MAX];
    uint8_t drums_on, drum_preset;
    uint8_t drum_k[RS_MAX_STEPS], drum_s[RS_MAX_STEPS], drum_h[RS_MAX_STEPS];
    uint8_t seq2_active, seq2_pending;
    int8_t seq2_index;
    struct rs_seq2_step seq2[RS_SEQ2_STEPS];
    uint8_t swapped;                                            /* the registers: A low, B high */
    struct rs_prog prog[2];
    struct rs_board board;
    uint8_t board_id, pending_board;                            /* RS_B_NONE: no change waiting for the loop */
    /* the run */
    uint8_t playing;
    int16_t step;                                               /* currentStep: -1 before the first tick */
    uint32_t cycle, move_tick;
    uint8_t drum_tick;
    uint8_t vm_index, vm_count;                                 /* voiceModIndex, voiceModStepCount */
    struct rs_bot bot[2];
    uint8_t clash_cooldown, love_steps;
};

struct rs_tick_out {
    uint8_t nevents;
    uint8_t events[RS_MAX_EVENTS];
    uint8_t move_tick;                                          /* the bots acted (or stood their reset) this tick */
    uint8_t acted[2];                                           /* bot A / B played its step (playBoardSound) */
    struct rs_step played[2];                                   /* that step */
    uint8_t died[2];                                            /* a death was processed (playEventSound "death") */
    uint8_t loop_reset;                                         /* playEventSound "reset" */
    uint8_t love;                                               /* the coin chime */
    uint8_t ghost, ghost_phase;                                 /* tpm > 1, a ghost tick (playGhostTick) and its phase */
    uint8_t drum_k, drum_s, drum_h, drum_ghost;                 /* the drum step played, or a ghost hihat */
};

extern const char *const rs_action_name[RS_NACTIONS];
extern const char *const rs_modifier_name[RS_NMODIFIERS];
extern const char *const rs_event_name[RS_NEVENTS];
extern const char *const rs_board_name[RS_NBOARDS];
extern const char *const rs_drum_name[RS_NDRUMS];
extern const char rs_dir_name[4];

void rs_default(struct rs_state *s);                            /* buildDefaultState() */
void rs_load_board(struct rs_state *s, uint8_t id);             /* applyBoardPreset(): now, or queued while playing */
void rs_set_drums(struct rs_state *s, uint8_t id);              /* a drum preset at the loop's drum length */
uint8_t rs_drum_steps(uint8_t loop_len);                        /* getDrumSteps(): 12 or 16 */
void rs_reset(struct rs_state *s);                              /* resetBoardState() */
void rs_start(struct rs_state *s);                              /* resetBoardState(), then playing */
void rs_stop(struct rs_state *s);                               /* stopPlayback()'s state */
void rs_tick(struct rs_state *s, struct rs_tick_out *o);        /* advanceSequence() */
void rs_advance_voice_mod(struct rs_state *s);                  /* advanceVoiceMod(): MANUAL's tap, LOOP's count */

/* seededChance(bot, step, cycle, repeat) > prob / 100, without float (rs_chance.c, docs/roboseq/DECISIONS.md D6) */
uint32_t rs_chance_q32(int bot, uint32_t step, uint32_t cycle, uint32_t repeat);   /* the chance, Q32 */
int rs_skips(int bot, uint32_t step, uint32_t cycle, uint8_t prob);

uint32_t rs_selfcheck(void);                                   /* a fingerprint of the model (rs_model.c) */

int rs_scale_quantize(int midi, uint8_t scale);                 /* scaleQuantize() */
int rs_scale_shift(int midi, uint8_t scale, int steps);         /* scaleIntervalShift() */
#endif
