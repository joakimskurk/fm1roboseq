/* SPDX-License-Identifier: GPL-3.0-only */
/* RoboSeq for the M-VAVE FM-1: the app (reference/roboseq.html on the FM-1; README.md is the manual, ROADMAP.md the
 * plan, docs/roboseq/CONTROLS.md every control). The controls, as this file wires them:
 *
 *   PLAY       starts the default song from its start, and stops it (the HTML's PLAY and STOP)
 *   white keys the 16 steps of a bot's register (its movement sequencer): a key turns its step to the next action,
 *              as the HTML's step cards do (the registers down the screen's sides, Scout-7 left, Echo-3 right,
 *              the one on the keys bright; the step playing framed)
 *   SELECT     what a white key turns: the step's ACTION, its MODIFIER (steady, bright, low, wobble, glitch) or its
 *              CHANCE (100, 75, 50 %), as the HTML's step card's three buttons (named over the board)
 *   KNOB1      the picked register's length: 8, 12, 16 ... up to the loop's (the HTML's program length)
 *   HOME       the board                SEQ   the sequence: the registers as step cards, programmed with WASD (D28)
 *   GLO        the tempo (KNOB1), how often the bots move (KNOB2), the loop's length (KNOB3: as the loop comes round,
 *              D20), the speed 1/1, 1/2, 1/4 (KNOB4)
 *   SCL (SEL)  Seq 1, pressed again Seq 2 (D27). Voice Mod (Seq 1): white keys 1-4 pick a step, KNOB1-3 its scale, root, semitone, KNOB4 when it moves on;
 *              white keys 13-16 NEXT (MANUAL), ROOT transpose, reset, random
 *   ...        Seq 2: white keys 1-4 pick a step, KNOB1-3 its board, drums, the registers swapped; white keys 12-16
 *              on / off (queued while playing), roll A's / B's register on its wrap, reset, random
 *   LFO        the drums: SELECT picks kick, snare or hihat, a white key turns its step; KNOB1 the patterns (then
 *              CLEAR), KNOB4 on / off
 *   FX         the mix: white keys 1-7 pick master, a bus or the lead, KNOB1 its level; KNOB2 how much the board's
 *              tiles colour the whole sound (its room, send, tilt, drive: D24; 0 % is the HTML's)
 *   ENV        the boards (D32): all of them at a glance, white keys 1-6 the presets, 7 a reroll, 8 the user board,
 *              9 save it, 10 clear it, 11 the bots to their pads (queued while playing, the coming one blinking);
 *              pressed again the board builder (D28): A W S D a cursor, a key a tile, reroll, the user board;
 *              on both KNOB1 the presets, KNOB2 a reroll's fullness
 *   EDIT       whose register is on the white keys: Scout-7 or Echo-3 (lit: Echo-3)
 *   OCT- OCT+  the register's 16 steps before / after (a register is up to 64)
 *   black keys steer the bots by hand: the next move is this one (D16). F# G# A# C# D# from F3: Scout-7's TURN L,
 *              MOVE, TURN R, DASH, PING; from F4 the same for Echo-3; the last (F#5) swaps the registers
 *   REC        the keys play the lead instead (D21): a SID-style solo over the robots, all 27 keys from F3, the last
 *              held winning, legato gliding; OCT- / OCT+ its octave (-2 .. +2); REC lit and LEAD on the screen;
 *              the knobs play it too (D26): K1 its cutoff, K2 its echo, K3 its bite (the fold and the PWM), K4 its
 *              octave as OCT- / OCT+ (D30); the four as faders in the page's header while the lead plays
 *   ARP        the keys a chord arpeggiated, a note each step (D27; lit, and the lead on with it): hold a chord;
 *              K4 its kind: up, down, up and down, up two octaves, down two octaves, random; SELECT its notes a
 *              step, 1-4 (D29). Only ARP arpeggiates (D30)
 *   MASTER     the volume (platform.c)
 *
 * How the sound runs: the main loop plays the game (rs_tick, a step at a time, LOOKAHEAD ahead of the speaker, as
 * the HTML's transport does) and queues each step's notes; the audio interrupt takes the notes due and renders the
 * sixteen voices through the mix (rs_mix.c: the buses, bot B's low-pass, the reverb, the limiter).
 *
 * Built on Dinghy (platform.c: updates, the USB rescue, MIDI, USB audio, keys, knobs, LEDs, the screen, flash).
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division.
 * tests/host.c runs this file on a computer: the screen as a PNG, the sound as a WAV.
 * What platform.c offers: docs/API.md. The chip's rules: docs/RULES.md. */
#define CV_MAX (240u * 240u)                    /* the board: the whole screen in one canvas (in the pool) */
#define APP_NAME "ROBOSEQ"                      /* the rescue and the crash screen say this */
#define STORE_MAGIC 0x51455352u                 /* "RSEQ": RoboSeq's settings and blobs, never another fork's */
static void app_idle(void);
#define LCD_IDLE app_idle                       /* while the screen's DMA runs: the game keeps ahead (platform.c) */
#define RS_POOL __attribute__((section(".pool")))   /* the lead's delay (rs_mix.c): big, in the pool */
#include "platform.c"
#include "rs_model.c"                         /* the game */
#include "rs_chance.c"
#include "rs_score.c"                         /* what it plays */
#include "rs_voice.c"                         /* the voices */
#include "rs_mix.c"                           /* the mix */
#include "rs_screen.c"                        /* the board */
#include "rs_splash.c"                        /* the splash at boot */
#include "rs_battle_screen.c"                 /* the battle mode's screens (D42) */
#ifndef RING_PUBLISH                            /* (platform.c's, on the chip) */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#endif

/* ---- the score, from the main loop to the audio interrupt: a ring, the main loop writes the head, the ISR the tail */
#define QN 256u                                 /* notes (a step makes up to RS_MAX_NOTES) */
#define LOOKAHEAD (RS_SR * 64u / 1000u)         /* the game runs 64 ms ahead of the speaker (D12) */
static struct { uint32_t t0, at, dur; uint16_t vel; uint8_t voice, fx; int8_t midi; } qn[QN];   /* t0: the step's start */
static volatile uint32_t qn_head, qn_tail;
static volatile uint32_t audio_now;             /* frames rendered since boot: the clock the notes' times are on */
static volatile uint32_t panic_req, panic_done; /* the main loop asks (PLAY to stop), the ISR releases every voice */
volatile uint32_t rs_audio_us, rs_audio_us_worst;   /* the ISR's last and worst render (make emu reads the worst) */
static struct rs_mix mix;

/* ---- the sound (runs in the audio ISR: keep it short, no flash, no screen) */
static void app_audio(int32_t *out, uint32_t n)
{
#ifndef DINGHY_HOST
    uint32_t t0 = fm1_ticks();
#endif
    uint32_t now = audio_now, tail = qn_tail, head = qn_head, k;
    if (panic_req != panic_done) {              /* stopPlayback(): what is queued goes, every voice lets go now */
        tail = head;
        for (k = 0; k < RS_NVOICES; k++) rs_voice_release(&mix.v[k], now);
        panic_done = panic_req;
    }
    RING_PUBLISH();
    for (k = 0; k < QN && tail != head; k++, tail++) {   /* the steps that start before the end of the next block, */
        uint32_t j = tail % QN;                         /* all their notes at once, as Tone has them (a step's notes
                                                           are in the HTML's call order, not in time) */
        if ((int32_t)(qn[j].t0 - (now + 2u * n)) >= 0) break;
        rs_mix_note_fx(&mix, qn[j].voice, qn[j].at, qn[j].dur, qn[j].midi, qn[j].vel, qn[j].fx);
    }
    qn_tail = tail;
    rs_mix_render(&mix, out, n, now);           /* stereo, straight into the platform's buffer */
    audio_now = now + n;
#ifndef DINGHY_HOST
    rs_audio_us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
    if (rs_audio_us > rs_audio_us_worst) rs_audio_us_worst = rs_audio_us;
#endif
}

/* ---- the game (main loop): the HTML's transport, advanceSequence() every step, ahead of the speaker */
static struct rs_state game;
static struct rs_tick_out tick_out;
static uint64_t next_q8;                        /* the next step's start on audio_now's clock, 1/256 frame */
static uint32_t steps_played;
static uint8_t pending_loop;                    /* GLO's next loop length, for when the loop comes round (D20) */
#define NVIEW 16u                               /* the board after each step, for when it is heard */
static struct rs_view views[NVIEW];
static uint32_t view_head;

static void view_of(struct rs_view *v, const struct rs_tick_out *o, uint32_t t0)
{
    v->t0 = t0, v->step = game.step, v->loop_len = game.loop_len, v->board_id = game.board_id;
    v->playing = game.playing, v->cycle = game.cycle, v->bot[0] = game.bot[0], v->bot[1] = game.bot[1];
    v->board = game.board;
    v->drum_tick = game.drum_tick;
    v->active = o ? o->active : 0;
    v->n = steps_played;                        /* (the step's count: the beat is every fourth move) */
    v->tpm = game.tpm ? game.tpm : 1;
    v->died[0] = o && o->died[0], v->died[1] = o && o->died[1], v->love = o && o->love;
    v->clash[0] = o && o->clash[0], v->clash[1] = o && o->clash[1];
    v->battle = game.battle;
}

static void schedule(void)
{
    uint32_t k;
    for (k = 0; k < 8u && game.playing; k++) { /* (a few steps a call at most) */
        uint32_t now = audio_now, t = (uint32_t)(next_q8 >> 8), h = qn_head, i;
        if ((int32_t)(t - (now + LOOKAHEAD)) >= 0) return;   /* far enough ahead */
        if ((int32_t)(t + RS_SR / 4u - now) < 0)             /* stalled a quarter second (a flash erase): */
            next_q8 = (uint64_t)now << 8;                    /* go on from now, not in a rush */
        if (QN - (h - qn_tail) < RS_MAX_NOTES) return;       /* the ring is full: the ISR takes some first */
        rs_tick(&game, &tick_out);
        for (i = 0; i < tick_out.nnotes; i++, h++) {
            const struct rs_note *e = &tick_out.notes[i];
            uint32_t j = h % QN;
            qn[j].t0 = t;
            qn[j].at = (uint32_t)((next_q8 + e->at + 128u) >> 8);
            qn[j].dur = (e->dur + 128u) >> 8;
            qn[j].vel = e->vel, qn[j].voice = e->voice, qn[j].midi = e->midi, qn[j].fx = e->fx;
        }
        RING_PUBLISH();                         /* the notes before the head that shows them */
        qn_head = h;
        if (pending_loop && tick_out.loop_reset) {   /* a new loop length lands as the loop comes round (D20) */
            rs_set_loop_len_live(&game, pending_loop);
            pending_loop = 0;
        }
        mix.lead.tick_at[mix.lead.tick_head % 8u] = t;   /* the step's time to the lead: its arp steps with the robots */
        RING_PUBLISH();
        mix.lead.tick_head++;
        view_of(&views[view_head % NVIEW], &tick_out, t);
        view_head++;
        next_q8 += rs_step_q8(&game);
        steps_played++;
    }
}

static void play(void)                          /* startPlayback(): the board from its start, the first step soon */
{
    rs_start(&game);
    next_q8 = (uint64_t)(audio_now + HALF_FRAMES) << 8;
    steps_played = 0;
}
static void stop(void)                          /* stopPlayback() */
{
    rs_stop(&game);
    panic_req++;
}

static void app_idle(void) { schedule(); }

/* ---- the screen: the board as it is heard (the newest step whose time has come) */
static const struct rs_view *view_now(void)
{
    static struct rs_view idle;
    uint32_t k, now = audio_now;
    if (game.playing)
        for (k = 1; k <= NVIEW && k <= view_head; k++) {
            const struct rs_view *v = &views[(view_head - k) % NVIEW];
            if ((int32_t)(v->t0 - now) <= 0) return v;
        }
    view_of(&idle, 0, 0);                       /* stopped (or before the first step sounds): the game as it stands */
    return &idle;
}

volatile uint32_t rs_boot_check;               /* rs_selfcheck() at boot: make emu reads it from RAM */
/* ---- the keys (D16) */
static const uint8_t STEER[5] = {RS_TURNL, RS_MOVE, RS_TURNR, RS_DASH, RS_PING};   /* a bot's five black keys */
static uint8_t white_key[16], black_key[11];    /* key index (0 = F3) of the k-th white / black key */
static uint8_t sel_bot, bank, layer;
static uint8_t lead_on, held_n, held[27];       /* REC: the keys play the lead (D21); the keys held, in the order pressed */
static int8_t lead_oct;                         /* its octave, -2 .. +2 (OCT- / OCT+ while it is on) */
static int8_t lead_knob[3] = {10, 10, 8};       /* K1 cut, K2 echo, K3 bite: 0 .. 20 (10 the middle); K4 the octave (D30) */
static uint32_t lead_knob_ms;                   /* when one was last turned */
static uint8_t arp_mode, arp_kind;              /* ARP (D27): the keys' chord arpeggiated; K4 its kind */
static uint8_t arp_rate = 1;                    /* its notes a step, 1 .. 4: SELECT while ARP is on (D29, D30) */
enum { ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_UP2, ARP_DOWN2, ARP_RANDOM, ARP_KINDS };

static void lead_arp(void)                      /* the arp's notes: the chord held, while ARP is on (else none: D30) */
{
    int8_t *out = (int8_t *)mix.lead.arp_notes;
    uint32_t n = 0, i, j;
    if (arp_mode) {                             /* the keys held, low to high (8 at most), as the kind lays them */
        int8_t c[8];
        uint32_t m = 0;
        for (i = 0; i < held_n && m < 8u; i++) {
            int8_t note = (int8_t)(53 + 12 * lead_oct + held[i]);
            for (j = m; j > 0 && c[j - 1] > note; j--) c[j] = c[j - 1];
            c[j] = note, m++;
        }
        if (m) switch (arp_kind) {
            case ARP_DOWN: for (i = 0; i < m; i++) out[n++] = c[m - 1u - i]; break;
            case ARP_UPDOWN:                    /* up, then down without the ends twice */
                for (i = 0; i < m; i++) out[n++] = c[i];
                for (i = m >= 2u ? m - 2u : 0u; i > 0; i--) out[n++] = c[i];
                break;
            case ARP_UP2: for (i = 0; i < 2u * m; i++) out[n++] = (int8_t)(c[i % m] + (i >= m ? 12 : 0)); break;
            case ARP_DOWN2: for (i = 0; i < 2u * m; i++) out[n++] = (int8_t)(c[m - 1u - i % m] + (i < m ? 12 : 0)); break;
            default: for (i = 0; i < m; i++) out[n++] = c[i]; break;   /* up, and random draws from them */
            }
        mix.lead.arp_random = arp_kind == ARP_RANDOM;
        mix.lead.arp_n = (uint8_t)(n ? n : 1u);     /* (one key: that note; none: it lets go anyway) */
        return;
    }
    mix.lead.arp_random = 0, mix.lead.arp_n = 0;   /* ARP off: the key alone, never an arp (D30) */
}

static void lead_knobs(void)                    /* while the lead plays, K1-K4 play it (D26, D27) */
{
    int32_t e;
    uint32_t k;
    for (k = 0; k < 4u; k++)
        if ((e = fm1_enc_take(ENC_KNOB1 + k)) != 0) {
            if (k == 3u && arp_mode) {          /* K4 with ARP on: the arp's kind */
                int32_t v = (int32_t)arp_kind + e;
                arp_kind = (uint8_t)(v < 0 ? 0 : v >= ARP_KINDS ? ARP_KINDS - 1 : v);
            } else if (k == 3u) {               /* K4 with ARP off: the octave, as OCT- / OCT+ (D30) */
                int32_t v = lead_oct + e;
                lead_oct = (int8_t)(v < -2 ? -2 : v > 2 ? 2 : v);
            } else {
                int32_t v = lead_knob[k] + e;
                lead_knob[k] = (int8_t)(v < 0 ? 0 : v > 20 ? 20 : v);
            }
            lead_knob_ms = fm1_ms;
        }
    mix.lead.arp_rate = arp_mode ? arp_rate : 1u;   /* (and how fast: the step cut in arp_rate, ARP's alone) */
    mix.lead.arp_sub = (rs_step_q8(&game) >> 8) / arp_rate;
    mix.lead.cut = lead_knob[0] * 32768 / 20;
    mix.echo = lead_knob[1] * 32768 / 20;
    mix.lead.bite = lead_knob[2] * 32768 / 20;
    lead_arp();                                 /* (the keys, the scale and the knob may all have moved) */
}

static uint8_t colour_amt = 40;                 /* how much the board colours the sound (D24), % (FX's KNOB2) */
static uint32_t colour_sig = ~0u;

static void board_colour(void)                  /* the board's tiles colour the sound (D24), when they or the amount change */
{
    uint32_t n[7] = {0}, i, sig, amt = colour_amt;
    if (game.engine && game.playing) {           /* BAND (D33d): each chord its own shade of the room, B more of it */
        static const uint8_t SHADE[8] = {100, 118, 88, 110, 94, 122, 84, 106};
        amt = amt * (SHADE[game.vm_index & 7u] + (rs_band_section(&game) ? 45u : 0u)) / 100u;
        if (amt > 100u) amt = 100u;
    }
    for (i = 0; i < RS_ROWS * RS_COLS; i++) n[game.board.t[i].type % 7u]++;
    sig = n[RS_T_BELT] | n[RS_T_ZAP] << 5 | (n[RS_T_ROTL] + n[RS_T_ROTR]) << 10 | n[RS_T_PULSE] << 15 |
          n[RS_T_BLOCK] << 20 | amt << 25;
    if (sig != colour_sig) colour_sig = sig, rs_mix_colour(&mix, &game.board, amt);
}

static uint32_t patch_sig = ~0u;
static void board_patch(void)                   /* BAND: the board's touch on the bots' synths (D36a); ORIGINAL: none */
{
    uint32_t sig = 2166136261u;
    for (uint32_t i = 0; game.engine && i < RS_ROWS * RS_COLS; i++)
        sig = (sig ^ (game.board.t[i].type * 4u + game.board.t[i].dir + 1u)) * 16777619u;
    if (sig == patch_sig) return;
    static const struct rs_patch none;
    struct rs_patch p = none;
    if (game.engine) rs_band_patch(&game.board, &p);
    patch_sig = sig, rs_mix_patch(&mix, &p);
}

static void lead_keys(uint32_t down, uint32_t up)   /* the last key held wins; let go, the one held before it again */
{
    uint32_t k, i;
    int pressed = 0;
    for (k = 0; k < 27u; k++) {
        if (up >> k & 1u)
            for (i = 0; i < held_n; i++)
                if (held[i] == k) { for (; i + 1u < held_n; i++) held[i] = held[i + 1u]; held_n--; break; }
        if (down >> k & 1u && held_n < 27u) held[held_n++] = (uint8_t)k, pressed = 1;
    }
    if (held_n) rs_lead_play(&mix.lead, 53 + 12 * lead_oct + held[held_n - 1u], 1, pressed);   /* (ARP: its notes over it) */
    else rs_lead_play(&mix.lead, 53 + 12 * lead_oct, 0, 0);
}
enum { PAGE_HOME, PAGE_SEQ, PAGE_GLO, PAGE_SCL, PAGE_ARP, PAGE_LFO, PAGE_FX, PAGE_ENV, PAGE_BOARDS, PAGE_BATTLE };   /* (D19, D32, D42) */
static uint8_t page, scl_cursor, arp_cursor, drum_row, mix_cursor;
static int32_t seq_scroll, seq_target;          /* the sequence page's scroll, px, and where it is going */

static uint32_t keys_top(void) { return bank * 16u; }   /* the board's: the step on the first white key */
static uint32_t seq_cursor;                     /* SEQ: the step the keys type into (D28) */
static int32_t env_cx = 2, env_cy = 2;          /* ENV: the cursor on the board, the way it last moved, edited */
static uint8_t env_dir = RS_E, env_edited;

static void seq_scroll_to(int32_t row)          /* the sequence page's target, rows, within the list */
{
    int32_t max = game.loop_len > SEQ_ROWS ? (int32_t)game.loop_len - SEQ_ROWS : 0;
    seq_target = (row < 0 ? 0 : row > max ? max : row) * SEQ_ROW;
}

static void seq_cursor_to(int32_t c)            /* the cursor, within the picked register; the list keeps it in view */
{
    int32_t len = game.prog[sel_bot].len ? game.prog[sel_bot].len : game.loop_len, top = seq_target / SEQ_ROW;
    c = c < 0 ? 0 : c >= len ? len - 1 : c;
    seq_cursor = (uint32_t)c;
    if (c < top + 1) seq_scroll_to(c - 1);
    else if (c > top + SEQ_ROWS - 2) seq_scroll_to(c - SEQ_ROWS + 2);
}

static int keys_seq(uint32_t down)              /* SEQ: the keys program the picked register, a game's way (D28) */
{
    uint32_t k, len = game.prog[sel_bot].len ? game.prog[sel_bot].len : game.loop_len;
    int changed = 0;
    for (k = 0; k < 27u; k++) {
        if (!(down >> k & 1u)) continue;
        changed = 1;
        if (SEQ_KEY_ACT[k] >= 0) {              /* an action at the cursor, and on to the next step: typed in */
            rs_set_step(&game, sel_bot, (int)seq_cursor, SEQ_KEY_ACT[k], -1, -1);
            seq_cursor_to((int32_t)((seq_cursor + 1u) % len));
        } else if (SEQ_KEY_MOD[k] >= 0) rs_set_step(&game, sel_bot, (int)seq_cursor, -1, SEQ_KEY_MOD[k], -1);
        else if (SEQ_KEY_PROB[k]) rs_set_step(&game, sel_bot, (int)seq_cursor, -1, -1, SEQ_KEY_PROB[k]);
        else if (k == SK_UNDO) rs_pop_undo(&game);
        else if (k == SK_RND) rs_randomize_steps(&game, sel_bot);
        else if (k == SK_RNDALL) rs_randomize_all(&game);
        else if (k == SK_SWAP) rs_swap_programs(&game);   /* (their programs: D45; who plays high is Seq 2's) */
        else if (k == SK_CLEAR) rs_clear_steps(&game);
        else changed = 0;
    }
    return changed;
}

static int keys_env(uint32_t down)              /* ENV: build the board, a game's way (D28) */
{
    static const int8_t MX[5] = {-1, 0, 0, 0, 1}, MY[5] = {0, -1, 1, 0, 0};
    static const uint8_t MD[5] = {RS_W, RS_N, RS_S, 0, RS_E};
    uint32_t k;
    int changed = 0;
    for (k = 0; k < 27u; k++) {
        if (!(down >> k & 1u)) continue;
        changed = 1;
        if (k <= EK_RIGHT && k != EK_ERASE) {  /* A W S D: the cursor, and the way a belt will point */
            int32_t x = env_cx + MX[k], y = env_cy + MY[k];
            env_cx = x < 0 ? 0 : x >= RS_COLS ? RS_COLS - 1 : x, env_cy = y < 0 ? 0 : y >= RS_ROWS ? RS_ROWS - 1 : y;
            env_dir = MD[k];
        } else if (k == EK_ERASE) rs_place_tile(&game, env_cx, env_cy, RS_T_NONE, 0), env_edited = 1;
        else if (ENV_KEY_TILE[k]) {
            const struct rs_tile_t *t = &game.board.t[env_cy * RS_COLS + env_cx];
            uint8_t dir = ENV_KEY_TILE[k] == RS_T_BELT && t->type == RS_T_BELT ? (uint8_t)((t->dir + 1) & 3) : env_dir;
            rs_place_tile(&game, env_cx, env_cy, ENV_KEY_TILE[k], dir), env_edited = 1;   /* (a belt on a belt turns) */
        } else if (k == EK_ROLL) rs_push_undo(&game), rs_randomize_board(&game), env_edited = 0;
        else if (k == EK_USER) rs_push_undo(&game), rs_load_user_board(&game), env_edited = 0;
        else if (k == EK_SAVE) rs_push_undo(&game), rs_save_user_board(&game), env_edited = 0;
        else if (k == EK_CLEAR) rs_clear_user_board(&game);
        else if (k == EK_RESET) rs_reset(&game);
        else changed = 0;
    }
    return changed;
}
static const uint8_t LENGTHS[7] = {8, 12, 16, 24, 32, 48, 64};   /* LOOP_OPTIONS: a register's lengths, up to the loop's */

static void turn_length(int32_t e)              /* KNOB1: the picked register's length, through the HTML's choices */
{
    uint32_t len = game.prog[sel_bot].len ? game.prog[sel_bot].len : game.loop_len, k;
    for (; e > 0; e--)
        for (k = 0; k < 7u; k++)
            if (LENGTHS[k] > len && LENGTHS[k] <= game.loop_len) { len = LENGTHS[k]; break; }
    for (; e < 0; e++)
        for (k = 7u; k-- > 0;)
            if (LENGTHS[k] < len) { len = LENGTHS[k]; break; }
    rs_set_prog_len(&game, sel_bot, (uint8_t)len);
}

static void keys_init(void)
{
    uint32_t k, w = 0, b = 0;
    for (k = 0; k < 27u; k++) {
        if ((0x54Au >> ((53u + k) % 12u)) & 1u) black_key[b++] = (uint8_t)k;
        else white_key[w++] = (uint8_t)k;
    }
}

/* ---- the battle mode (D42): two players on the keyboard, a cluster each at its ends, as WASD, a white key free, and
 * the strike: Scout-7's F3 F#3 G3 A3 (left, up, down, right) and C4, Echo-3's C5 C#5 D5 E5 and G5. A way held is a step on every
 * beat (the last one pressed, of those held); the strike held or tapped strikes on the next beat with the other in
 * reach, and out of reach it plays the lead: a tone of the chord where the robot stands, on up the chord as it is
 * pressed again on one tile (rs_battle_lead_note), held as long as the key is. */
static uint8_t bt_to_win = 3, bt_arena, bt_last[2];   /* the rounds to win, the arena between rounds: set before (D42) */
static uint8_t bt_note_k[2], bt_note_on[2];     /* the lead from the strike key: presses on one tile, a note held */
static int8_t bt_note_at[2] = {-1, -1};
static int bt_note[2];
static uint16_t bt_bpm_was;
static uint32_t swap_until;                     /* the swap by hand shown in the header till then (fm1_ms, 0: not) */

static int battle_keys(uint32_t held, uint32_t down)
{
    static uint32_t was;
    int changed = held != was;                          /* (any key down or up: the keyboard on the screen lights it) */
    was = held;
    for (int p = 0; p < 2; p++) {
        uint8_t hold = 0;
        for (int k = 0; k < 4; k++)
            if (down >> BT_KEY[p][k] & 1u) bt_last[p] = (uint8_t)(BT_DIR[k] + 1), changed = 1;
        for (int k = 0; k < 4; k++)                     /* the last pressed while it is held, else any held */
            if (held >> BT_KEY[p][k] & 1u && (BT_DIR[k] + 1 == bt_last[p] || !hold)) hold = (uint8_t)(BT_DIR[k] + 1);
        if (hold && hold != bt_last[p]) {
            int last_held = 0;
            for (int k = 0; k < 4; k++) last_held |= held >> BT_KEY[p][k] & 1u && BT_DIR[k] + 1 == bt_last[p];
            if (last_held) hold = bt_last[p];
        }
        uint8_t act = held >> BT_KEY[p][4] & 1u, tap = down >> BT_KEY[p][4] & 1u;
        if (p == 0 && down >> BT_RESET & 1u && game.playing && game.battle.phase != RS_BT_OFF)   /* RESET: the match again */
            rs_battle_start(&game, bt_to_win, bt_arena), changed = 1;
        rs_battle_keys(&game, p, hold, act, tap);
        if (tap) changed = 1;
        if (tap && game.playing && game.battle.phase != RS_BT_OFF && !rs_battle_in_reach(&game, p)) {   /* out of reach: play */
            int8_t at = (int8_t)(game.bot[p].y * RS_COLS + game.bot[p].x);
            if (at != bt_note_at[p]) bt_note_at[p] = at, bt_note_k[p] = 0;
            bt_note[p] = rs_battle_lead_note(&game, p, bt_note_k[p]++), bt_note_on[p] = 1;
            rs_lead_play(&mix.lead, bt_note[p], 1, 1);
        } else if (!act && bt_note_on[p]) {             /* let go: the note ends, unless the other holds one */
            bt_note_on[p] = 0;
            if (bt_note_on[!p]) rs_lead_play(&mix.lead, bt_note[!p], 1, 0);
            else rs_lead_play(&mix.lead, bt_note[p], 0, 0);
        }
    }
    return changed;
}

static void battle_enter(void)                  /* GLO's key 16: the battle page; PLAY starts a match */
{
    if (page == PAGE_BATTLE) return;
    if (game.playing) stop();
    lead_on = 0, arp_mode = 0, held_n = 0;
    page = PAGE_BATTLE, bt_bpm_was = game.bpm;
    if (game.bpm < 100u) game.bpm = 112;              /* (a fight's tempo; K1 turns it, and leaving brings the old back) */
    rs_reset(&game);
}

static void battle_leave(void)
{
    rs_battle_stop(&game);
    if (bt_bpm_was) game.bpm = bt_bpm_was, bt_bpm_was = 0;
}

static int keys(uint32_t down)                  /* what the keys pressed now do; 1 when the screen should show it */
{
    if (page == PAGE_GLO && down >> white_key[15] & 1u) { battle_enter(); return 1; }   /* (D42) */
    if (page == PAGE_SEQ) return keys_seq(down);   /* (the builders' keys are their own: no steering there) */
    if (page == PAGE_ENV) return keys_env(down);
    uint32_t k, len = game.prog[sel_bot].len ? game.prog[sel_bot].len : game.loop_len;
    int changed = 0;
    if (page == PAGE_SCL) {                     /* Voice Mod: 1-4 pick a step, 12 FOLLOW, 13 NEXT, 14 ROOT, 15 reset, 16 random */
        for (k = 0; k < 16u; k++) {
            if (!(down >> white_key[k] & 1u)) continue;
            if (k < 4u) scl_cursor = (uint8_t)k;
            else if (k == 11u) rs_set_follow(&game, !game.vm_follow);   /* FOLLOW (D38) */
            else if (k == 12u && game.vm_manual) rs_advance_voice_mod(&game);
            else if (k == 13u) game.transpose_root = !game.transpose_root;
            else if (k == 14u) rs_reset_bassmod(&game);
            else if (k == 15u) { if (game.engine) rs_compose_bassmod(&game); else rs_randomize_bassmod(&game); }   /* (BAND: a progression, D33) */
            changed = 1;
        }
    }
    if (page == PAGE_ARP) {                     /* Seq 2: 1-4 pick a step, 12 on, 13 / 14 roll A / B, 15 reset, 16 random */
        for (k = 0; k < 16u; k++) {
            if (!(down >> white_key[k] & 1u)) continue;
            if (k < 4u) arp_cursor = (uint8_t)k;
            else if (k == 11u) rs_seq2_toggle(&game);
            else if (k == 12u || k == 13u) game.seq2_rnd[k - 12u] = !game.seq2_rnd[k - 12u];
            else if (k == 14u) rs_reset_seq2(&game);
            else if (k == 15u) rs_randomize_seq2(&game);
            changed = 1;
        }
    }
    if (page == PAGE_BOARDS)                    /* the boards (D32): 1-6 the presets, 7 roll, 8 user, 9 save, 10 clear, 11 reset */
        for (k = 0; k < 11u; k++) {
            if (!(down >> white_key[k] & 1u)) continue;
            if (k < RS_NBOARDS) rs_push_undo(&game), rs_load_board(&game, (uint8_t)k), env_edited = 0;
            else if (k == 6u) rs_push_undo(&game), rs_randomize_board(&game), env_edited = 0;
            else if (k == 7u) rs_push_undo(&game), rs_load_user_board(&game), env_edited = 0;
            else if (k == 8u) rs_push_undo(&game), rs_save_user_board(&game), env_edited = 0;
            else if (k == 9u) rs_clear_user_board(&game);
            else rs_reset(&game);
            changed = 1;
        }
    if (page == PAGE_FX)                        /* the mix: white keys 1-6 pick a fader */
        for (k = 0; k < RS_NLEVEL; k++)
            if (down >> white_key[k] & 1u) mix_cursor = (uint8_t)k, changed = 1;
    if (page == PAGE_LFO)                       /* the drums: a white key turns its step of the row SELECT picked */
        for (k = 0; k < 16u; k++)
            if (down >> white_key[k] & 1u && k < rs_drum_steps(game.loop_len)) rs_drum_toggle(&game, drum_row, (int)k), changed = 1;
    for (k = 0; k < 16u && page == PAGE_HOME; k++)   /* (the white keys: the register, where it is shown) */
        if (down >> white_key[k] & 1u && keys_top() + k < len) {
            int i = (int)(keys_top() + k);
            if (layer == RS_LAYER_MODIFIER) rs_cycle_modifier(&game, sel_bot, i);
            else if (layer == RS_LAYER_CHANCE) rs_cycle_prob(&game, sel_bot, i);
            else rs_cycle_action(&game, sel_bot, i);
            changed = 1;
        }
    for (k = 0; k < 11u; k++)
        if (down >> black_key[k] & 1u) {
            if (k < 10u) rs_steer(&game, k < 5u ? 0 : 1, STEER[k % 5u]);
            else rs_swap_programs(&game), swap_until = (fm1_ms + 500u) | 1u;   /* F#5: their programs swapped (D45) */
            changed = 1;
        }
    return changed;
}

static void ui_of(struct rs_ui *u, const struct rs_view *v)
{
    uint32_t k, b;
    u->sel = sel_bot, u->bank = bank, u->swapped = game.swapped, u->layer = layer;
    for (b = 0; b < 2; b++) {
        uint32_t len = game.prog[b].len ? game.prog[b].len : game.loop_len;
        u->len[b] = (uint8_t)len;
        u->playing_at[b] = v->playing && v->step >= 0 ? (int16_t)(v->step % (int16_t)len) : -1;
        u->steer[b] = game.steer[b];
        for (k = 0; k < 16u; k++) u->steps[b][k] = game.prog[b].s[(bank * 16u + k) & 63u];
    }
}

static void draw_page(const struct rs_view *v, struct rs_fx *fx, struct rs_ui *ui, uint32_t notes)   /* the page, drawn */
{
    int steering = page != PAGE_SEQ && page != PAGE_ENV && page != PAGE_BATTLE;   /* (the black keys steer here) */
    ui_of(ui, v);
    badge_steer[0] = steering ? game.steer[0] : 0, badge_steer[1] = steering ? game.steer[1] : 0;
    badge_swap = steering && swap_until;
    if (page == PAGE_SEQ) draw_seq(v, ui, &game, seq_scroll, seq_cursor);
    else if (page == PAGE_GLO) draw_glo(&game, pending_loop);
    else if (page == PAGE_SCL) draw_scl(&game, scl_cursor);
    else if (page == PAGE_ARP) draw_arp(&game, arp_cursor);
    else if (page == PAGE_LFO) draw_drums(v, &game, drum_row);
    else if (page == PAGE_FX) draw_mix(mix.level, mix_cursor, colour_amt);
    else if (page == PAGE_ENV) draw_editor(v, &game, env_cx, env_cy, (fm1_ms / 300u) & 1u, env_edited);
    else if (page == PAGE_BOARDS) draw_boards(&game, (fm1_ms / 300u) & 1u, env_edited);
    else if (page == PAGE_BATTLE) draw_battle(v, fx, fm1_ms, bt_to_win, bt_arena, notes);
    else draw_board(v, fx, ui, fm1_ms);
}

/* ---- the project kept over a power-off (D43): the HTML's saveState() (rs_project) and the port's mix, board colour
 * and lead, in blobs 0 and 1 by turns, so a save cut short leaves the one before. A save erases a sector (~50 ms
 * with the sound stopped), so it waits till nothing plays: stopped 2 s, the project unchanged 3 s, no key held. SAVE
 * lit: changes not kept yet; pressed while stopped it keeps them at once. Not in the battle (its tempo is the fight's). */
struct app_save {
    struct rs_project p;
    uint32_t seq;                               /* the saves so far: the newer blob holds the higher */
    int32_t level[RS_NLEVEL];
    uint8_t colour, lead_knob[3], arp_kind, arp_rate, pad[2];
};
static struct app_save saved, now_save;         /* what flash holds; the project now (and the other blob at boot) */
static uint8_t unsaved;                         /* the project is not what flash holds: SAVE lit */

static void save_of(struct app_save *a)
{
    memset(a, 0, sizeof *a);
    rs_project_put(&game, &a->p);
    a->seq = saved.seq;
    for (uint32_t k = 0; k < RS_NLEVEL; k++) a->level[k] = mix.level[k];
    a->colour = colour_amt, a->arp_kind = arp_kind, a->arp_rate = arp_rate;
    for (uint32_t k = 0; k < 3u; k++) a->lead_knob[k] = (uint8_t)lead_knob[k];
}

static void project_load(void)                  /* at boot, on the default: the newer of the blobs that hold one */
{
    struct app_save *a0 = &saved, *a1 = &now_save;
    int ok0 = blob_load(0, a0, sizeof *a0) == 0 && a0->p.ver == RS_PROJECT_VER;
    int ok1 = blob_load(1, a1, sizeof *a1) == 0 && a1->p.ver == RS_PROJECT_VER;
    const struct app_save *f = ok0 && ok1 ? (a1->seq > a0->seq ? a1 : a0) : ok1 ? a1 : ok0 ? a0 : 0;
    uint32_t seq = f ? f->seq : 0;
    if (f && rs_project_take(&game, &f->p)) {   /* (each field as the model takes them: the HTML's, else the default) */
        for (uint32_t k = 0; k < RS_NLEVEL; k++)
            if (f->level[k] >= 0 && f->level[k] <= 32768) mix.level[k] = f->level[k];
        if (f->colour <= 100u) colour_amt = f->colour;
        if (f->arp_kind < ARP_KINDS) arp_kind = f->arp_kind;
        if (f->arp_rate >= 1u && f->arp_rate <= 4u) arp_rate = f->arp_rate;
        for (uint32_t k = 0; k < 3u; k++)
            if (f->lead_knob[k] <= 20u) lead_knob[k] = (int8_t)f->lead_knob[k];
    }
    save_of(&saved);                            /* (what flash holds, as taken: nothing to save till it changes) */
    saved.seq = seq;
}

static uint32_t save_hash(const struct app_save *a)   /* FNV-1a: has the project changed since the last look */
{
    const uint8_t *q = (const uint8_t *)a;
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < sizeof *a; i++) h = (h ^ q[i]) * 16777619u;
    return h;
}

static void project_keep(uint32_t held, int now)   /* the main loop's; now: SAVE pressed */
{
    struct app_save *a = &now_save;
    static uint32_t checked, changed_ms, playing_ms, seen;
    if (game.playing) playing_ms = fm1_ms;
    if (page == PAGE_BATTLE || (!now && fm1_ms - checked < 250u)) return;
    checked = fm1_ms;
    save_of(a);
    uint32_t h = save_hash(a);
    if (h != seen) seen = h, changed_ms = fm1_ms;
    unsaved = memcmp(a, &saved, sizeof *a) != 0;
    if (!unsaved || game.playing || held) return;
    if (!now && (fm1_ms - changed_ms < 3000u || fm1_ms - playing_ms < 2000u)) return;
    a->seq = saved.seq + 1u;
    blob_save(a->seq & 1u, a, sizeof *a);       /* (one that fails is not tried again till the next change) */
    saved = *a, unsaved = 0;
}

static struct {                                 /* the main loop's state, kept from pass to pass */
    struct rs_fx fx;
    struct rs_view last;
    struct rs_ui ui;
    uint32_t due, shown, prev, beat_ms;
} ml = {.shown = ~0u};

#ifdef RS_P4_TEST
#include "../../tests/roboseq/p4_test.c"        /* the playability rules, page by page (P4), on the host */
#endif

static void app_start(void)                     /* RoboSeq's start, before the splash */
{
    rs_boot_check = rs_selfcheck();             /* the model on the chip: make emu compares it with the computer's */
    rs_mix_init(&mix, 0x5EEDu);
    rs_default(&game);
    game.engine = RS_ENGINE_BAND;               /* the engine: RoboSeq's own BAND; ORIGINAL (the HTML's) on GLO's SELECT (D33, D39) */
    project_load();                             /* (the project kept, its engine with it: D43) */
    keys_init();
}

static int splash_pass(void)                    /* a pass of the splash: ~2.6 s, a frame every 33 ms; a key or a button
                                                 * skips it. 0: it is over */
{
    static uint32_t t0, drawn = ~0u;
    static int on;
    uint32_t rel;
    if (!on) on = 1, t0 = fm1_ms;
    if (fm1_ms - t0 >= SPLASH_MS) { ml.prev = fm1_in.notes; return 0; }
    platform_poll();
    schedule();
    if (fm1_input_edges(&rel) || fm1_in.notes) {
        ml.prev = fm1_in.notes;                 /* (a key that skipped it is not also a press on the board) */
        return 0;
    }
    if (drawn == ~0u || fm1_ms - drawn >= 33u) drawn = fm1_ms, draw_splash(fm1_ms - t0);
    return 1;
}

static void app_pass(void)                      /* a pass of the main loop: a millisecond's time, the keys, the screen */
{
    uint32_t rel, b, key, notes, len, k;
    int32_t e;
    int dirty = 0;
    const struct rs_view *v;
    platform_poll();
    b = fm1_input_edges(&rel);
    notes = fm1_in.notes;
    if (page == PAGE_BATTLE) dirty = battle_keys(notes, notes & ~ml.prev);   /* (D42: the players') */
    else if (lead_on) lead_keys(notes & ~ml.prev, ml.prev & ~notes);   /* REC: the keys are the lead's */
    else dirty = keys(notes & ~ml.prev);
    ml.prev = notes;
    if (page == PAGE_BATTLE) b &= ~(1u << BTN_REC | 1u << BTN_ARP | 1u << BTN_EDIT);   /* (the keys are the players') */
    if (b & (1u << BTN_REC)) {             /* the lead on / off; off, what it holds lets go */
        lead_on = !lead_on, held_n = 0, dirty = 1;
        if (!lead_on) arp_mode = 0;         /* (the lead off, its arp with it) */
        rs_lead_play(&mix.lead, 53 + 12 * lead_oct, 0, 0);
    }
    int8_t oct_was = lead_oct;              /* (OCT- / OCT+ or K4 may move it: the note held goes with it) */
    if (lead_on && b & (1u << BTN_OCT_DOWN) && lead_oct > -2) lead_oct--, dirty = 1;
    if (lead_on && b & (1u << BTN_OCT_UP) && lead_oct < 2) lead_oct++, dirty = 1;
    if (lead_on) b &= ~(1u << BTN_OCT_DOWN | 1u << BTN_OCT_UP);   /* (OCT is the lead's while it plays) */
    if (lead_on) {                          /* (and the knobs: D26) */
        uint32_t was = lead_knob_ms;
        lead_knobs();
        if (lead_knob_ms != was) dirty = 1;
        if (lead_oct != oct_was && held_n)  /* the key held, in its new octave at once (a glide there) */
            rs_lead_play(&mix.lead, 53 + 12 * lead_oct + held[held_n - 1u], 1, 0);
    }
    if (b & (1u << BTN_PLAY)) {
        if (page == PAGE_BATTLE && game.playing && game.battle.phase == RS_BT_MATCH)
            rs_battle_start(&game, bt_to_win, bt_arena);   /* the winner ml.shown: PLAY is another match (D42) */
        else if (game.playing) stop();
        else {
            play();
            if (page == PAGE_BATTLE) rs_battle_start(&game, bt_to_win, bt_arena);   /* a match (D42) */
        }
    }
    if (b & (1u << BTN_HOME) && page != PAGE_HOME) page = PAGE_HOME, dirty = 1;
    if (b & (1u << BTN_SEQ) && page != PAGE_SEQ) page = PAGE_SEQ, seq_cursor_to((int32_t)seq_cursor), dirty = 1;
    if (b & (1u << BTN_GLO) && page != PAGE_GLO) page = PAGE_GLO, dirty = 1;
    if (b & (1u << BTN_SEL))                /* SCL: Seq 1 (Voice Mod), pressed again Seq 2 (D27) */
        page = page == PAGE_SCL ? PAGE_ARP : PAGE_SCL, dirty = 1;
    if (b & (1u << BTN_ARP)) {              /* ARP: the keys a chord arpeggiated (D27); it brings the lead with it */
        arp_mode = !arp_mode, dirty = 1;
        if (arp_mode && !lead_on) lead_on = 1, held_n = 0;
    }
    if (b & (1u << BTN_LFO) && page != PAGE_LFO) page = PAGE_LFO, dirty = 1;
    if (b & (1u << BTN_FX) && page != PAGE_FX) page = PAGE_FX, dirty = 1;
    if (b & (1u << BTN_ENV))                /* ENV: the boards, pressed again the builder (D32) */
        page = page == PAGE_BOARDS ? PAGE_ENV : PAGE_BOARDS, dirty = 1;
    if (b & (1u << BTN_EDIT)) {             /* whose register is on the white keys (and SEQ's cursor) */
        sel_bot ^= 1u, bank = 0, dirty = 1;
        if (page == PAGE_SEQ) seq_cursor_to((int32_t)seq_cursor);
    }
    {                                       /* the battle's page left: the registers again (D42) */
        static uint8_t was;
        if (was == PAGE_BATTLE && page != PAGE_BATTLE) battle_leave(), dirty = 1;
        was = page;
    }
    if ((e = fm1_enc_take(ENC_SELECT)) != 0) {        /* SELECT: what the white keys turn */
        if (lead_on && arp_mode) {          /* ARP's: its notes a step, 1 .. 4 (D29, D30) */
            int32_t r = arp_rate + e;
            arp_rate = (uint8_t)(r < 1 ? 1 : r > 4 ? 4 : r);
        } else if (page == PAGE_LFO) drum_row = (uint8_t)(((int32_t)drum_row + e % 3 + 3) % 3);
        else if (page == PAGE_SEQ) seq_cursor_to((int32_t)seq_cursor + e);   /* SEQ: the cursor up and down (D28) */
        else if (page == PAGE_GLO) game.engine = e > 0 ? RS_ENGINE_BAND : RS_ENGINE_ORIGINAL;   /* GLO: the engine (D33, D39) */
        else layer = (uint8_t)(((int32_t)layer + e % RS_NLAYERS + RS_NLAYERS) % RS_NLAYERS);
        dirty = 1;
    }
    if (lead_on) {                          /* (the knobs are the lead's: lead_knobs above) */
    } else if (page == PAGE_BATTLE) {       /* the battle: K1 the tempo, K2 the rounds to win (1-9), K3 the arena */
        if ((e = fm1_enc_take(ENC_KNOB1)) != 0) {
            int32_t bpm = (int32_t)game.bpm + e;
            game.bpm = (uint16_t)(bpm < 60 ? 60 : bpm > 168 ? 168 : bpm), dirty = 1;
        }
        if ((e = fm1_enc_take(ENC_KNOB2)) != 0 && (!game.playing || game.battle.phase == RS_BT_MATCH)) {   /* (before a match) */
            int32_t w = (int32_t)bt_to_win + (e > 0 ? 1 : -1);
            bt_to_win = (uint8_t)(w < 1 ? 1 : w > 9 ? 9 : w), dirty = 1;
        }
        if ((e = fm1_enc_take(ENC_KNOB3)) != 0 && game.battle.phase != RS_BT_FIGHT) {   /* (not mid-fight) */
            int32_t id = game.board_id < RS_NBOARDS ? (int32_t)game.board_id : 0;
            id = ((id + (e > 0 ? 1 : -1)) % RS_NBOARDS + RS_NBOARDS) % RS_NBOARDS;
            rs_load_board(&game, (uint8_t)id), dirty = 1;
        }
        if ((e = fm1_enc_take(ENC_KNOB4)) != 0 && (!game.playing || game.battle.phase == RS_BT_MATCH))   /* the arena */
            bt_arena = (uint8_t)(((int32_t)bt_arena + (e > 0 ? 1 : -1) + RS_NARENAS) % RS_NARENAS), dirty = 1;   /* between rounds */
    } else if (page == PAGE_GLO) {          /* the knobs: tempo, moves, loop, speed */
        if ((e = fm1_enc_take(ENC_KNOB1)) != 0) {
            int32_t bpm = (int32_t)game.bpm + e;
            game.bpm = (uint16_t)(bpm < 60 ? 60 : bpm > 168 ? 168 : bpm), dirty = 1;
        }
        if ((e = fm1_enc_take(ENC_KNOB2)) != 0) {   /* 1, 2, 4 (the HTML resets the move count) */
            uint8_t t = game.tpm ? game.tpm : 1;
            t = e > 0 ? (t == 1 ? 2 : 4) : (t == 4 ? 2 : 1);
            if (t != game.tpm) game.tpm = t, game.move_tick = 0;
            dirty = 1;
        }
        if ((e = fm1_enc_take(ENC_KNOB3)) != 0) {   /* the loop: at once when stopped (the HTML's), else as it comes round */
            uint32_t cur = pending_loop ? pending_loop : game.loop_len, k2;
            for (; e > 0; e--) for (k2 = 0; k2 < 7u; k2++) if (LENGTHS[k2] > cur) { cur = LENGTHS[k2]; break; }
            for (; e < 0; e++) for (k2 = 7u; k2-- > 0;) if (LENGTHS[k2] < cur) { cur = LENGTHS[k2]; break; }
            if (!game.playing) rs_set_loop_len(&game, (uint8_t)cur), pending_loop = 0;
            else pending_loop = cur == game.loop_len ? 0 : (uint8_t)cur;
            dirty = 1;
        }
        if ((e = fm1_enc_take(ENC_KNOB4)) != 0) {   /* the speed: 1/1, 1/2, 1/4 (the model: 0, 2, 1) */
            static const uint8_t SPEED[3] = {0, 2, 1};
            int32_t at = game.quarter == 2 ? 1 : game.quarter == 1 ? 2 : 0;
            at += e, game.quarter = SPEED[at < 0 ? 0 : at > 2 ? 2 : at], dirty = 1;
        }
    } else if (page == PAGE_SCL) {          /* the picked step's scale, root, semitone; when it moves on */
        for (k = 0; k < 3u; k++)
            if ((e = fm1_enc_take(ENC_KNOB1 + k)) != 0)
                for (; e; e += e > 0 ? -1 : 1) rs_cycle_bassmod(&game, scl_cursor, (int)k, e > 0 ? 1 : -1);
        if ((e = fm1_enc_take(ENC_KNOB4)) != 0) {   /* MANUAL, then every 1/4, 1/2, 1, 2, 4 loops */
            static const uint8_t F4[6] = {0, 1, 2, 4, 8, 16};
            int32_t at = game.vm_manual ? 0 : game.vm_freq4 >= 16 ? 5 : game.vm_freq4 >= 8 ? 4 : game.vm_freq4 >= 4 ? 3 : game.vm_freq4 >= 2 ? 2 : 1;
            at += e, at = at < 0 ? 0 : at > 5 ? 5 : at;
            rs_set_voice_mod(&game, at == 0, F4[at]);
        }
        dirty = 1;
    } else if (page == PAGE_ARP) {          /* the picked step's board, drums, swap */
        for (k = 0; k < 3u; k++)
            if ((e = fm1_enc_take(ENC_KNOB1 + k)) != 0) {
                if (k == 2u) { if ((e > 0) != (game.seq2[arp_cursor].regswap != 0)) rs_seq2_cycle(&game, arp_cursor, 2, 1); }
                else for (; e; e += e > 0 ? -1 : 1) rs_seq2_cycle(&game, arp_cursor, (int)k, e > 0 ? 1 : -1);
                dirty = 1;
            }
        if ((e = fm1_enc_take(ENC_KNOB4)) != 0) {   /* the step's progression (D38): "--", RND, the named ones */
            for (; e; e += e > 0 ? -1 : 1) rs_seq2_cycle(&game, arp_cursor, 3, e > 0 ? 1 : -1);
            dirty = 1;
        }
    } else if (page == PAGE_LFO) {          /* KNOB1 the patterns, then CLEAR; KNOB4 on / off */
        if ((e = fm1_enc_take(ENC_KNOB1)) != 0) {
            int32_t at = game.drum_preset < RS_NDRUMS ? game.drum_preset : -1;
            at += e;
            if (at >= RS_NDRUMS) rs_drum_clear(&game);
            else rs_drum_preset(&game, (uint8_t)(at < 0 ? 0 : at));
            dirty = 1;
        }
        if ((e = fm1_enc_take(ENC_KNOB4)) != 0) game.drums_on = e > 0, dirty = 1;
        (void)fm1_enc_take(ENC_KNOB2), (void)fm1_enc_take(ENC_KNOB3);
    } else if (page == PAGE_ENV || page == PAGE_BOARDS) {   /* KNOB1 the presets (queued while playing), KNOB2 a reroll's fullness */
        if ((e = fm1_enc_take(ENC_KNOB1)) != 0) {
            int32_t at = game.pending_board != RS_B_NONE && game.pending_board < RS_NBOARDS ? game.pending_board :
                         game.board_id < RS_NBOARDS ? game.board_id : 0;
            at += e, at = at < 0 ? 0 : at >= RS_NBOARDS ? RS_NBOARDS - 1 : at;
            rs_push_undo(&game), rs_load_board(&game, (uint8_t)at), env_edited = 0, dirty = 1;
        }
        if ((e = fm1_enc_take(ENC_KNOB2)) != 0) {
            static const uint8_t D[3] = {30, 45, 60};
            int32_t at = game.density >= 60 ? 2 : game.density >= 45 ? 1 : 0;
            at += e, game.density = D[at < 0 ? 0 : at > 2 ? 2 : at], dirty = 1;
        }
        (void)fm1_enc_take(ENC_KNOB3), (void)fm1_enc_take(ENC_KNOB4);
    } else if (page == PAGE_FX) {           /* KNOB1 the picked level, 2 % a detent (the HTML's 0..1) */
        if ((e = fm1_enc_take(ENC_KNOB1)) != 0) {
            int32_t l = mix.level[mix_cursor] + e * 655;
            mix.level[mix_cursor] = l < 0 ? 0 : l > 32768 ? 32768 : l;
            dirty = 1;
        }
        if ((e = fm1_enc_take(ENC_KNOB2)) != 0) {   /* how much the board colours the sound, 5 % a detent */
            int32_t c = colour_amt + 5 * e;
            colour_amt = (uint8_t)(c < 0 ? 0 : c > 100 ? 100 : c), dirty = 1;
        }
        (void)fm1_enc_take(ENC_KNOB3), (void)fm1_enc_take(ENC_KNOB4);
    } else if ((e = fm1_enc_take(ENC_KNOB1)) != 0) turn_length(e), dirty = 1;
    len = game.prog[sel_bot].len ? game.prog[sel_bot].len : game.loop_len;
    if (page != PAGE_HOME && page != PAGE_SEQ) {
    } else if (page == PAGE_SEQ) {          /* OCT- / OCT+ the cursor 8 steps; the list glides after it */
        if (b & (1u << BTN_OCT_DOWN)) seq_cursor_to((int32_t)seq_cursor - SEQ_ROWS), dirty = 1;
        if (b & (1u << BTN_OCT_UP)) seq_cursor_to((int32_t)seq_cursor + SEQ_ROWS), dirty = 1;
        (void)fm1_enc_take(ENC_KNOB2);
        if (seq_scroll != seq_target) {     /* glide there: a third of the way a frame */
            int32_t d = (seq_target - seq_scroll) / 3;
            seq_scroll += d ? d : seq_target > seq_scroll ? 1 : -1;
            dirty = 1;
        }
    } else {                                /* HOME: K2 a new program for the picked robot, K3 new ones for both, a roll
                                             * a turn (D45; each one undone on SEQ) */
        if (fm1_enc_take(ENC_KNOB2)) rs_randomize_steps(&game, (int)sel_bot), dirty = 1;
        if (fm1_enc_take(ENC_KNOB3)) rs_randomize_all(&game), dirty = 1;
        if (b & (1u << BTN_OCT_DOWN) && bank > 0) bank--, dirty = 1;
        if (b & (1u << BTN_OCT_UP) && (bank + 1u) * 16u < len) bank++, dirty = 1;
        if (bank * 16u >= len) bank = 0;
    }
    for (k = 0; k < 4u; k++) (void)fm1_enc_take(ENC_KNOB1 + k);   /* the knobs this page did not read: let go, not kept */
    (void)fm1_enc_take(ENC_ALGORITHM), (void)fm1_enc_take(ENC_PRESETS);   /* for the next (K3 on HOME was GLO's loop, D45) */
    schedule();
    board_colour();
    board_patch();
    {                                       /* the lead's delay: three steps (a dotted eighth), halved till it fits */
        uint32_t d = rs_step_q8(&game) * 3u >> 8;
        while (d >= DLY_MAX) d >>= 1;
        mix.dly_target = (int32_t)d;
    }
    fm1_led_key(BTN_PLAY, game.playing && fm1_ms - ml.beat_ms < 100u);   /* PLAY flashes the beat (D29) */
    fm1_led_key(BTN_HOME, page == PAGE_HOME);
    fm1_led_key(BTN_SEQ, page == PAGE_SEQ);
    fm1_led_key(BTN_GLO, page == PAGE_GLO);
    fm1_led_key(BTN_SEL, page == PAGE_SCL || page == PAGE_ARP);
    fm1_led_key(BTN_ARP, arp_mode);
    fm1_led_key(BTN_LFO, page == PAGE_LFO);
    fm1_led_key(BTN_FX, page == PAGE_FX);
    fm1_led_key(BTN_ENV, page == PAGE_ENV || page == PAGE_BOARDS);
    fm1_led_key(BTN_EDIT, sel_bot);         /* lit: Echo-3's register */
    fm1_led_key(BTN_REC, lead_on);
    project_keep(notes, (b >> BTN_SAVE & 1u) != 0);
    fm1_led_key(BTN_SAVE, unsaved);         /* lit: changes not kept yet (D43) */
    if (lead_on) fm1_led_key(BTN_OCT_DOWN, lead_oct < 0), fm1_led_key(BTN_OCT_UP, lead_oct > 0);
    else {
        fm1_led_key(BTN_OCT_DOWN, page == PAGE_SEQ ? seq_cursor > 0 : bank > 0);
        fm1_led_key(BTN_OCT_UP, page == PAGE_SEQ ? seq_cursor + 1u < len : (bank + 1u) * 16u < len);
    }
    {                                       /* ENV's cursor blinks */
        static uint32_t blink_was;
        if ((page == PAGE_ENV || (page == PAGE_BOARDS && game.pending_board != RS_B_NONE)) && fm1_ms / 300u != blink_was)
            blink_was = fm1_ms / 300u, dirty = 1;   /* (the builder's cursor; the board coming, on the boards) */
    }
    v = view_now();
    key = v->t0 ^ (uint32_t)v->playing << 31 ^ (uint32_t)v->step << 20;
    if (key != ml.shown && v->playing && v->n % (4u * v->tpm) == 0) ml.beat_ms = fm1_ms;   /* a beat heard */
    if (key != ml.shown) {                     /* a new step heard: its effects start now */
        if (v->playing && ml.shown != ~0u) screen_effects(&ml.fx, &ml.last, v, fm1_ms);
        ml.last = *v, ml.shown = key, dirty = 1;
    } else if (ml.due && (int32_t)(fm1_ms - ml.due) >= 0)   /* an effect ran out */
        dirty = 1;
    if (swap_until && (int32_t)(fm1_ms - swap_until) >= 0) swap_until = 0, dirty = 1;
    if (dirty) {
        lead_badge = lead_on ? lead_oct : -99;
        lead_panel_v[0] = lead_knob[0], lead_panel_v[1] = lead_knob[1], lead_panel_v[2] = lead_knob[2], lead_panel_v[3] = lead_oct;
        lead_panel_arp = arp_mode ? (int8_t)arp_kind : -1;
        lead_panel_rate = arp_mode ? arp_rate : 1u;
        draw_page(v, &ml.fx, &ml.ui, notes);
        ml.due = fx_due(&ml.fx, fm1_ms);
    }
}

static void app_main(void)                      /* the FM-1 and the host: the start, the splash, the loop for ever. The
                                                 * page (DINGHY_WEB, web/app/web_host.c): a pass a call */
{
#ifdef RS_P4_TEST
    exit(p4_main());
#endif
#ifdef DINGHY_WEB
    static int phase;
    if (phase == 0) app_start(), phase = 1;
    if (phase == 1) { if (!splash_pass()) phase = 2; return; }
    app_pass();
#else
    app_start();
    while (splash_pass()) {}
    for (;;) app_pass();
#endif
}
