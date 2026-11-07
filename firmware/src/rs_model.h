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
struct rs_seq2_step { uint8_t board, drums, regswap, prog; };   /* prog (D38, the port's): 0 "--", 1.. a progression, RS_P_RND */
#define RS_P_RND 0xFFu                                          /* a Seq 2 step's progression: a new one, following on */

enum { RS_BT_OFF, RS_BT_READY, RS_BT_FIGHT, RS_BT_ROUND, RS_BT_MATCH };   /* the battle's phase (D42) */
enum { RS_BT_WAIT, RS_BT_MOVE, RS_BT_STRIKE };                 /* what a robot did on a beat */
enum { RS_ARENA_SAME, RS_ARENA_NEXT, RS_ARENA_RANDOM, RS_NARENAS };   /* the arena between rounds */
#define RS_BT_OVERHEAT 64                                       /* a round's beats before the arena's edge glows (+1 a beat) */
#define RS_BT_OVERHEAT2 96                                      /* and before all but its middle tile does */
struct rs_battle {                                              /* the battle mode (D42): two players, the robots on the beat */
    uint8_t phase, to_win, wins[2], round, winner;              /* the round's or the match's winner: 0, 1, 2 a draw */
    uint8_t arena;                                              /* between rounds: RS_ARENA_SAME, _NEXT (preset), _RANDOM (rolled) */
    uint8_t count;                                              /* beats left of READY's countdown or ROUND's pause */
    uint8_t hold[2], act[2], press[2];                          /* the players: the way held (RS_N .. + 1, 0 none), action held, pressed */
    uint8_t did[2], hit[2], sdir[2];                            /* this beat: what each did (RS_BT_*), it landed a hit, the way it struck */
    uint8_t guard[2];                                           /* just hit: it cannot be hit on the next beat */
    uint8_t rhits[2];                                           /* the round's hits each landed (a burnout together: the more wins) */
    uint8_t draws, coin;                                        /* draws running; this round a coin decided it */
    uint16_t beats;                                             /* the round's beats so far (from RS_BT_OVERHEAT on: +1 heat a bar) */
};

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
    uint8_t engine;                                             /* RS_ENGINE_ORIGINAL (the HTML's, rs_default) or _BAND (D33, D39) */
    int8_t band_lead, band_echo, band_bass;                     /* BAND's lines (D34): their last notes (0: none yet) */
    int8_t band_cell[2];                                        /* BAND's tunes (D35): the cell each bot's last note is from (-1) */
    uint8_t band_at[2], band_cad, band_end;                     /* the step of that note; the bar's cadence has sounded; Scout-7's note ends */
    int8_t band_call[4], band_ans;                              /* Scout-7's call: its bar's first notes (scale steps, -1); Echo-3's answer's */
    uint8_t band_first;                                         /* the step of the call's first note (255: none yet) */
    int8_t band_run;                                            /* the bass's run into the next phrase: 1 up, -1 down (D36c) */
    /* the progressions (D38, the port's): Voice Mod's, by name and key (0: made by hand); the next round's, ready on
     * its last step when FOLLOW or the chain changes it */
    uint8_t vm_follow, prog_id, prog_key, next_id, next_key, next_ready;
    struct rs_bassmod_step bm_next[RS_BASSMOD_MAX];
    struct rs_battle battle;                                    /* the battle mode (D42); phase RS_BT_OFF: the registers play */
};

enum { RS_ENGINE_ORIGINAL, RS_ENGINE_BAND };                    /* the engine: the HTML's score, or RoboSeq's own band (D33, D39) */

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
    uint8_t fx;                                                 /* its pitch gesture (BAND, D37a: RS_FX_*; 0 none) */
};
/* BAND's kit (D41): a hit's tone, in the note (ORIGINAL's hits are the HTML's: the noises' midi RS_NOISE, a kick's
 * fx 0). A snare's or a hihat's midi: its tone; a kick's midi its pitch and its fx: RS_KICK_* */
#define RS_SNARE_KIT(pitch, snap, len) (((pitch) & 7) | ((snap) & 3) << 3 | ((len) & 3) << 5)   /* body 0..7 (147-262 Hz), crack 0..3, 0 short 1 mid 2 long */
#define RS_HAT_KIT(bright, len) (((bright) & 7) | ((len) & 3) << 3)    /* 0..7 (3-10 kHz up), 0 closed 1 half 2 open 3 a crash */
enum { RS_KICK_CLICK = 1, RS_KICK_DRIVE = 2, RS_KICK_LONG = 4, RS_KICK_TIGHT = 8 };
enum {                                                          /* a note's pitch gesture: a vibrato | a bend */
    RS_FX_VIB_LATE = 1,                                         /* a held note's: after a quarter second, growing, +-18 cents */
    RS_FX_VIB_WIDE = 2,                                         /* a wobble's: at once, +-40 cents */
    RS_FX_BEND_UP = 4,                                          /* into the note from a semitone below (a left rotator) */
    RS_FX_BEND_DOWN = 8,                                        /* from a semitone above (a right rotator) */
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
    uint8_t bumped[2];                                          /* bot A / B bumped this tick: 1 into a wall, 2 into a crate (D37b) */
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
void rs_preset_board(struct rs_board *b, uint8_t id);           /* a preset's tiles, to look at (the port's: D32) */
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
void rs_score_count(const struct rs_state *s, int n, struct rs_tick_out *o);   /* the battle's countdown: 4 .. 1, 0 FIGHT (D42) */
void rs_score_band(struct rs_state *s, struct rs_tick_out *o);  /* BAND (D34-D37): the tick's music, written anew */
uint32_t rs_band_phrase(const struct rs_state *s);              /* BAND: a phrase, in loops (a Voice Mod round) */
int rs_band_section(const struct rs_state *s);                  /* BAND: 1 in a phrase's second half (B) */
int rs_band_peak(const struct rs_state *s);                     /* BAND: 1 in every other phrase's B, the big one (D36b) */
int rs_band_groove(const struct rs_state *s);                   /* BAND: the bass's groove, 0..7 (the board's, D35) */
struct rs_patch {                                               /* BAND: the board's touch on the bots' synths (D36a) */
    uint16_t pw[2];                                             /* Scout-7's and Echo-3's square: its duty, Q16 (32768 a half, the HTML's) */
    uint16_t pwm[2];                                            /* its slow swing around that, Q16 */
    uint16_t rate;                                              /* the swing's rate, mHz */
    uint16_t soft[2];                                           /* a triangle into the square, Q15 (0: the HTML's) */
    uint8_t env[3];                                             /* Scout-7's, Echo-3's, the bass's envelope: 0 the HTML's, 1 shorter, 2 longer */
};
void rs_band_patch(const struct rs_board *b, struct rs_patch *p);   /* the patch of a board (ORIGINAL: all 0) */
void rs_advance_voice_mod(struct rs_state *s);                  /* advanceVoiceMod(): MANUAL's tap, LOOP's count */

/* editing, as the HTML's controls do it */
void rs_set_loop_len(struct rs_state *s, uint8_t len);          /* setLoopLength(): caps the programs, stops */
void rs_set_loop_len_live(struct rs_state *s, uint8_t len);      /* the loop's length without a stop (D20) */
void rs_set_prog_len(struct rs_state *s, int bot, uint8_t len); /* setProgramLength(): at most the loop's */
void rs_cycle_action(struct rs_state *s, int bot, int i);       /* cycleAction() */
void rs_set_step(struct rs_state *s, int bot, int i, int action, int modifier, int prob);   /* updateStep(), -1 keeps */
void rs_place_tile(struct rs_state *s, int x, int y, uint8_t type, uint8_t dir);   /* a tile by hand (D28) */
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
void rs_compose_bassmod(struct rs_state *s);                    /* BAND's Voice Mod roll: a progression in a key (D33, D38) */
#define RS_NKEYS 6                                              /* the keys the C modes give: Ab Eb Bb F C G major (and their minors) */
uint32_t rs_nprogs(void);                                       /* the named progressions: ids 1..rs_nprogs() */
const char *rs_prog_name(uint8_t id);                           /* "I V vi IV" ("--" for 0, "RND" for RS_P_RND) */
void rs_prog_key_name(uint8_t id, uint8_t key, char *out);      /* its key, "G", "Am", "C" (4 bytes) */
void rs_apply_prog(struct rs_state *s, uint8_t id, uint8_t key);   /* a named progression into Voice Mod (undoable) */
void rs_set_follow(struct rs_state *s, uint8_t on);             /* FOLLOW: every round a new progression, following on */
/* the battle mode (D42, rs_battle.c): two players steer the robots, a step on every beat, and strike */
void rs_battle_start(struct rs_state *s, uint8_t to_win, uint8_t arena);   /* a match: round 1, the robots at their pads */
void rs_battle_stop(struct rs_state *s);                        /* back to the registers */
void rs_battle_keys(struct rs_state *s, int player, uint8_t hold, uint8_t act, uint8_t pressed);   /* hold: RS_N .. + 1, or 0 */
int rs_battle_beat_steps(const struct rs_state *s);             /* the steps of a beat: 4, or 3 in a 12-step loop */
int rs_battle_hot(const struct rs_battle *t, int x, int y);     /* that tile glows now (the overheat) */
int rs_battle_in_reach(const struct rs_state *s, int player);   /* the other within a strike, the way it holds or faces */
int rs_battle_lead_note(const struct rs_state *s, int player, int k);   /* out of reach the strike plays the lead: this note */
const struct rs_bassmod_step *rs_next_mod_step(const struct rs_state *s);   /* the chord after this one (the next round's) */
void rs_randomize_bassmod(struct rs_state *s);                  /* randomizeBassMod() */
void rs_set_voice_mod(struct rs_state *s, uint8_t manual, uint8_t freq4);   /* its trigger, its loop frequency x 4 */
void rs_reset_bassmod(struct rs_state *s);                      /* resetBassMod() */
void rs_drum_preset(struct rs_state *s, uint8_t id);            /* applyDrumPreset() */
void rs_drum_toggle(struct rs_state *s, int voice, int step);   /* toggleDrumCell(): 0 kick, 1 snare, 2 hihat */
void rs_drum_clear(struct rs_state *s);                         /* clearDrumPattern(): empty, custom, off */
void rs_seq2_toggle(struct rs_state *s);                        /* Seq 2 on (queued while playing) / off */
void rs_seq2_cycle(struct rs_state *s, int i, int field, int dir);   /* step i's 0 board, 1 drums (dir), 2 regswap, 3 prog (D38) */
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
