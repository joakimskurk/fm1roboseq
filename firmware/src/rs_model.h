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
                   RS_B_RANDOM = RS_NBOARDS, RS_B_USER, RS_B_NONE = 0xFF };   /* (none: seq 2's "--") */
enum rs_drum_id { RS_D_FOUR, RS_D_BREAK, RS_D_HALF, RS_D_OFFBEAT, RS_D_TRIPLET, RS_D_MINIMAL, RS_NDRUMS,
                  RS_D_CUSTOM = 0xFE, RS_D_NONE = 0xFF };   /* custom: the pattern edited by hand */
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
    uint8_t stepped_tile, stepped_dir;                          /* the tile it landed on before a belt moved it (and a belt's way) */
};

struct rs_prog { struct rs_step s[RS_MAX_STEPS]; uint8_t len; };   /* a bot's register; len 1..64 */
struct rs_bassmod_step { uint8_t scale, root; int8_t semitone; };
struct rs_seq2_step { uint8_t board, drums, regswap; };

struct rs_state {
    /* the settings */
    uint16_t bpm;                                               /* 80 */
    uint8_t quarter, scale, transpose_root;                     /* 1: 1/4 speed (the HTML's), 2: 1/2 (D22); the scale (unused by the HTML's notes) */
    uint8_t loop_len, tpm;                                      /* 8..64; ticks per move 1, 2, 4 */
    uint8_t vm_manual, vm_freq4;                                /* Voice Mod: LOOP or MANUAL; its frequency x 4 (1..16) */
    uint8_t bassmod_len;                                        /* 4 or 8 */
    struct rs_bassmod_step bassmod[RS_BASSMOD_MAX];
    uint8_t drums_on, drum_preset;
    uint8_t drum_k[RS_MAX_STEPS], drum_s[RS_MAX_STEPS], drum_h[RS_MAX_STEPS];
    uint8_t seq2_active, seq2_pending;
    int8_t seq2_index;
    struct rs_seq2_step seq2[RS_SEQ2_STEPS];
    uint8_t seq2_rnd[2];                                        /* roll bot A's / B's register when seq 2 wraps */
    uint8_t swapped;                                            /* the registers: A low, B high */
    struct rs_prog prog[2];
    struct rs_board board, pending_tiles, user_board;
    uint8_t board_id, pending_board;                            /* RS_B_NONE: no change waiting for the loop */
    uint8_t density;                                            /* the random board's: 30, 45 or 60 (%) */
    uint32_t rng;                                               /* the randomizers' (xorshift32; the HTML's Math.random) */
    uint8_t rnd_due;                                            /* bit 0 / 1: seq 2 wrapped, roll that register after the tick */
    /* the run */
    uint8_t playing;
    int16_t step;                                               /* currentStep: -1 before the first tick */
    uint32_t cycle, move_tick;
    uint8_t drum_tick;
    uint8_t vm_index, vm_count;                                 /* voiceModIndex, voiceModStepCount */
    struct rs_bot bot[2];
    uint8_t clash_cooldown, love_steps;
    /* the player's (D16): not the HTML's, 0 there */
    uint8_t steer[2];                                           /* bot A / B's next move is this action + 1 (0: its register's) */
};

/* a note, as the HTML's triggerAttackRelease(note, duration, time, velocity) on one of its voices */
enum rs_voice_id {
    RS_V_A_MAIN, RS_V_A_PING, RS_V_B_FM, RS_V_B_SUB, RS_V_COLLISION, RS_V_BELT, RS_V_ROTATOR, RS_V_ZAP, RS_V_PULSE,
    RS_V_KICK, RS_V_SNARE, RS_V_HIHAT, RS_V_COIN, RS_V_EX_THUMP, RS_V_EX_NOISE, RS_V_EX_CRACKLE, RS_NVOICES
};
#define RS_MAX_NOTES 48         /* the notes of one tick (a busy tpm-4 belt tick makes ~30) */
#define RS_NOISE (-1)           /* the midi of a noise voice's note */
#define RS_SR 44100u
struct rs_note {
    uint8_t voice;
    int8_t midi;                                                /* RS_NOISE for the noise voices */
    uint16_t vel;                                               /* Q15, 32768 = 1 */
    uint32_t at, dur;                                           /* from the tick's start, and how long: 1/256 sample */
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
    uint32_t active;                                            /* bit y * RS_COLS + x: the tiles an event lit (activeKeys) */
    uint8_t clash[2];                                           /* bot A / B was dashed into (the clash star) */
    uint8_t nnotes;
    struct rs_note notes[RS_MAX_NOTES];                         /* the score of this tick, in the HTML's call order */
};

extern const char *const rs_action_name[RS_NACTIONS];
extern const char *const rs_modifier_name[RS_NMODIFIERS];
extern const char *const rs_event_name[RS_NEVENTS];
extern const char *const rs_board_name[RS_NBOARDS + 2];   /* the presets, then random, user */
extern const char *const rs_drum_name[RS_NDRUMS];
extern const char rs_dir_name[4];
extern const char *const rs_voice_name[RS_NVOICES];

void rs_default(struct rs_state *s);                            /* buildDefaultState() */
void rs_load_board(struct rs_state *s, uint8_t id);             /* applyBoardPreset(): now, or queued while playing */
void rs_set_board(struct rs_state *s, const struct rs_board *b, uint8_t id);   /* any board, the same way */
void rs_set_drums(struct rs_state *s, uint8_t id);              /* a drum preset at the loop's drum length */
uint8_t rs_drum_steps(uint8_t loop_len);                        /* getDrumSteps(): 12 or 16 */
void rs_reset(struct rs_state *s);                              /* resetBoardState() */
void rs_start(struct rs_state *s);                              /* resetBoardState(), then playing */
void rs_stop(struct rs_state *s);                               /* stopPlayback()'s state */
void rs_tick(struct rs_state *s, struct rs_tick_out *o);        /* advanceSequence() */
uint32_t rs_step_q8(const struct rs_state *s);                  /* getStepDurationSeconds(), in 1/256 samples */

/* the score (rs_score.c): what the HTML's audio plays at each point of advanceSequence */
void rs_score_board(const struct rs_state *s, int b, const struct rs_step *st, struct rs_tick_out *o);   /* playBoardSound() */
void rs_score_ghost(const struct rs_state *s, int phase, struct rs_tick_out *o);   /* playGhostTick() */
void rs_score_event(const struct rs_state *s, uint8_t kind, uint32_t at, struct rs_tick_out *o);   /* playEventSound() */
void rs_score_drums(const struct rs_state *s, uint8_t i, struct rs_tick_out *o);   /* playDrumStep() */
void rs_score_ghost_hat(const struct rs_state *s, int phase, struct rs_tick_out *o);
void rs_score_coin(const struct rs_state *s, struct rs_tick_out *o);   /* the love check's chime */
void rs_advance_voice_mod(struct rs_state *s);                  /* advanceVoiceMod(): MANUAL's tap, LOOP's count */

/* editing, as the HTML's controls do it */
void rs_set_loop_len(struct rs_state *s, uint8_t len);          /* setLoopLength(): caps the programs, stops */
void rs_set_loop_len_live(struct rs_state *s, uint8_t len);      /* the loop's length without a stop (D20) */
void rs_set_prog_len(struct rs_state *s, int bot, uint8_t len); /* setProgramLength(): at most the loop's */
void rs_cycle_action(struct rs_state *s, int bot, int i);       /* cycleAction() */
void rs_steer(struct rs_state *s, int bot, uint8_t action);    /* the bot's next move does this, once (D16) */
void rs_swap_registers(struct rs_state *s);                     /* swapRegisters() */
void rs_cycle_modifier(struct rs_state *s, int bot, int i);     /* cycleModifier() */
void rs_cycle_prob(struct rs_state *s, int bot, int i);         /* cycleProbability(): 100 -> 75 -> 50 */
void rs_cycle_bassmod(struct rs_state *s, int i, int field, int dir);   /* cycleBassModStepValue(): 0 scale, 1 root, 2 semitone */
void rs_save_user_board(struct rs_state *s);                    /* saveUserBoard() */
void rs_load_user_board(struct rs_state *s);                    /* loadUserBoard() */
void rs_clear_user_board(struct rs_state *s);                   /* clear-user-board */
void rs_cycle_density(struct rs_state *s);                      /* cycleBoardDensity(): 30 -> 45 -> 60 */

/* the randomizers (Math.random in the HTML: the same rules, the firmware's own numbers), each undoable */
void rs_randomize_steps(struct rs_state *s, int bot);           /* randomizeSteps() */
void rs_randomize_all(struct rs_state *s);                      /* randomizeAllSteps() */
void rs_clear_steps(struct rs_state *s);                        /* resetAllSteps() */
void rs_randomize_board(struct rs_state *s);                    /* randomizeBoard() (not undoable, as the HTML) */
void rs_randomize_bassmod(struct rs_state *s);                  /* randomizeBassMod() */
void rs_set_voice_mod(struct rs_state *s, uint8_t manual, uint8_t freq4);   /* its trigger, its loop frequency x 4 */
void rs_reset_bassmod(struct rs_state *s);                      /* resetBassMod() */
void rs_drum_preset(struct rs_state *s, uint8_t id);            /* applyDrumPreset() */
void rs_drum_toggle(struct rs_state *s, int voice, int step);   /* toggleDrumCell(): 0 kick, 1 snare, 2 hihat */
void rs_drum_clear(struct rs_state *s);                         /* clearDrumPattern(): empty, custom, off */
void rs_seq2_toggle(struct rs_state *s);                        /* Seq 2 on (queued while playing) / off */
void rs_seq2_cycle(struct rs_state *s, int i, int field, int dir);   /* step i's 0 board, 1 drums (dir), 2 regswap */
void rs_reset_seq2(struct rs_state *s);                         /* resetSeq2() */
void rs_randomize_seq2(struct rs_state *s);                     /* randomizeSeq2() */
uint32_t rs_random(struct rs_state *s, uint32_t n);             /* 0..n-1 */

/* undo (UNDO_MAX 20): the registers, Seq 1, the tempo, the loop, the scale, Voice Mod's trigger, the drums */
#define RS_UNDO_MAX 20
void rs_push_undo(const struct rs_state *s);                    /* pushUndo() */
int rs_pop_undo(struct rs_state *s);                            /* popUndo(): 0 when there was nothing */
int rs_undo_depth(void);
void rs_undo_clear(void);

/* seededChance(bot, step, cycle, repeat) > prob / 100, without float (rs_chance.c, docs/roboseq/DECISIONS.md D6) */
uint32_t rs_chance_q32(int bot, uint32_t step, uint32_t cycle, uint32_t repeat);   /* the chance, Q32 */
int rs_skips(int bot, uint32_t step, uint32_t cycle, uint8_t prob);

uint32_t rs_selfcheck(void);                                   /* a fingerprint of the model (rs_model.c) */

int rs_scale_quantize(int midi, uint8_t scale);                 /* scaleQuantize() */
int rs_scale_shift(int midi, uint8_t scale, int steps);         /* scaleIntervalShift() */
#endif
