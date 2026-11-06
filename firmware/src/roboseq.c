/* SPDX-License-Identifier: GPL-3.0-only */
/* RoboSeq for the M-VAVE FM-1: the app. Phase 2: the HTML's default song plays, its sound as the HTML's
 * (reference/roboseq.html; ROADMAP.md). The controls, the board on the screen and the pages come in phase 3.
 *
 *   PLAY       starts the default song from its start, and stops it (the HTML's PLAY and STOP)
 *   white keys the 16 steps of a bot's register (its movement sequencer): a key turns its step to the next action,
 *              as the HTML's step cards do (the registers down the screen's sides, Scout-7 left, Echo-3 right,
 *              the one on the keys bright; the step playing framed)
 *   SELECT     what a white key turns: the step's ACTION, its MODIFIER (steady, bright, low, wobble, glitch) or its
 *              CHANCE (100, 75, 50 %), as the HTML's step card's three buttons (named over the board)
 *   KNOB1      the picked register's length: 8, 12, 16 ... up to the loop's (the HTML's program length)
 *   HOME       the board                SEQ   the sequence: both registers as one list, scrolled by KNOB2
 *   GLO        the tempo (KNOB1), how often the bots move (KNOB2), the loop's length (KNOB3: as the loop comes round,
 *              D20), 1/4 speed (KNOB4)
 *   SCL (SEL)  Voice Mod: white keys 1-4 pick a step, KNOB1-3 its scale, root, semitone, KNOB4 when it moves on;
 *              white keys 13-16 NEXT (MANUAL), ROOT transpose, reset, random
 *   EDIT       whose register is on the white keys: Scout-7 or Echo-3 (lit: Echo-3)
 *   OCT- OCT+  the register's 16 steps before / after (a register is up to 64)
 *   black keys steer the bots by hand: the next move is this one (D16). F# G# A# C# D# from F3: Scout-7's TURN L,
 *              MOVE, TURN R, DASH, PING; from F4 the same for Echo-3; the last (F#5) swaps the registers
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
#include "platform.c"
#include "rs_model.c"                         /* the game */
#include "rs_chance.c"
#include "rs_score.c"                         /* what it plays */
#include "rs_voice.c"                         /* the voices */
#include "rs_mix.c"                           /* the mix */
#include "rs_screen.c"                        /* the board */
#ifndef RING_PUBLISH                            /* (platform.c's, on the chip) */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#endif

/* ---- the score, from the main loop to the audio interrupt: a ring, the main loop writes the head, the ISR the tail */
#define QN 256u                                 /* notes (a step makes up to RS_MAX_NOTES) */
#define LOOKAHEAD (RS_SR * 64u / 1000u)         /* the game runs 64 ms ahead of the speaker (D12) */
static struct { uint32_t t0, at, dur; uint16_t vel; uint8_t voice; int8_t midi; } qn[QN];   /* t0: the step's start */
static volatile uint32_t qn_head, qn_tail;
static volatile uint32_t audio_now;             /* frames rendered since boot: the clock the notes' times are on */
static volatile uint32_t panic_req, panic_done; /* the main loop asks (PLAY to stop), the ISR releases every voice */
volatile uint32_t rs_audio_us, rs_audio_us_worst;   /* the ISR's last and worst render (make emu reads the worst) */
static struct rs_mix mix;

/* ---- the sound (runs in the audio ISR: keep it short, no flash, no screen) */
static void app_audio(int32_t *out, uint32_t n)
{
    static int32_t mono[HALF_FRAMES];
#ifndef DINGHY_HOST
    uint32_t t0 = fm1_ticks();
#endif
    uint32_t now = audio_now, tail = qn_tail, head = qn_head, i, k;
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
        rs_mix_note(&mix, qn[j].voice, qn[j].at, qn[j].dur, qn[j].midi, qn[j].vel);
    }
    qn_tail = tail;
    rs_mix_render(&mix, mono, n, now);
    for (i = 0; i < n; i++) out[2u * i] = out[2u * i + 1u] = mono[i];
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
    v->active = o ? o->active : 0;
    v->died[0] = o && o->died[0], v->died[1] = o && o->died[1], v->love = o && o->love;
    v->clash[0] = o && o->clash[0], v->clash[1] = o && o->clash[1];
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
            qn[j].vel = e->vel, qn[j].voice = e->voice, qn[j].midi = e->midi;
        }
        RING_PUBLISH();                         /* the notes before the head that shows them */
        qn_head = h;
        if (pending_loop && tick_out.loop_reset) {   /* a new loop length lands as the loop comes round (D20) */
            rs_set_loop_len_live(&game, pending_loop);
            pending_loop = 0;
        }
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
enum { PAGE_HOME, PAGE_SEQ, PAGE_GLO, PAGE_SCL }; /* the pages (D19) */
static uint8_t page, scl_cursor;
static int32_t seq_scroll, seq_target;          /* the sequence page's scroll, px, and where it is going */
static uint32_t seq_touched;                    /* when the player last scrolled it (fm1_ms): it follows the step after */

static uint32_t keys_top(void)                  /* the step on the first white key */
{
    return page == PAGE_SEQ ? (uint32_t)((seq_target + SEQ_ROW / 2) / SEQ_ROW) : bank * 16u;
}

static void seq_scroll_to(int32_t row)          /* the sequence page's target, rows, within the list */
{
    int32_t max = game.loop_len > 16 ? (int32_t)game.loop_len - 16 : 0;
    seq_target = (row < 0 ? 0 : row > max ? max : row) * SEQ_ROW;
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

static int keys(uint32_t down)                  /* what the keys pressed now do; 1 when the screen should show it */
{
    uint32_t k, len = game.prog[sel_bot].len ? game.prog[sel_bot].len : game.loop_len;
    int changed = 0;
    if (page == PAGE_SCL) {                     /* Voice Mod: 1-4 pick a step, 13 NEXT, 14 ROOT, 15 reset, 16 random */
        for (k = 0; k < 16u; k++) {
            if (!(down >> white_key[k] & 1u)) continue;
            if (k < 4u) scl_cursor = (uint8_t)k;
            else if (k == 12u && game.vm_manual) rs_advance_voice_mod(&game);
            else if (k == 13u) game.transpose_root = !game.transpose_root;
            else if (k == 14u) rs_reset_bassmod(&game);
            else if (k == 15u) rs_randomize_bassmod(&game);
            changed = 1;
        }
    }
    for (k = 0; k < 16u && page != PAGE_GLO && page != PAGE_SCL; k++)   /* (the white keys: the register, where it is shown) */
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
            else rs_swap_registers(&game);
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

static void app_main(void)
{
    static struct rs_fx fx;
    static struct rs_view last;
    static struct rs_ui ui;
    uint32_t due = 0, shown = ~0u, prev = 0;
    rs_boot_check = rs_selfcheck();             /* the model on the chip: make emu compares it with the computer's */
    rs_mix_init(&mix, 0x5EEDu);
    rs_default(&game);
    keys_init();
    for (;;) {
        uint32_t rel, b, key, notes, len, k;
        int32_t e;
        int dirty = 0;
        const struct rs_view *v;
        platform_poll();
        b = fm1_input_edges(&rel);
        notes = fm1_in.notes;
        dirty = keys(notes & ~prev);
        prev = notes;
        if (b & (1u << BTN_PLAY)) { if (game.playing) stop(); else play(); }
        if (b & (1u << BTN_HOME) && page != PAGE_HOME) page = PAGE_HOME, dirty = 1;
        if (b & (1u << BTN_SEQ) && page != PAGE_SEQ) page = PAGE_SEQ, seq_scroll_to((int32_t)keys_top()), dirty = 1;
        if (b & (1u << BTN_GLO) && page != PAGE_GLO) page = PAGE_GLO, dirty = 1;
        if (b & (1u << BTN_SEL) && page != PAGE_SCL) page = PAGE_SCL, dirty = 1;
        if (b & (1u << BTN_EDIT)) sel_bot ^= 1u, bank = 0, dirty = 1;   /* whose register is on the white keys */
        if ((e = fm1_enc_take(ENC_SELECT)) != 0)          /* SELECT: what the white keys turn */
            layer = (uint8_t)(((int32_t)layer + e % RS_NLAYERS + RS_NLAYERS) % RS_NLAYERS), dirty = 1;
        if (page == PAGE_GLO) {                 /* the knobs: tempo, moves, loop, speed */
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
            if ((e = fm1_enc_take(ENC_KNOB4)) != 0) game.quarter = e > 0, dirty = 1;   /* 1/4 speed */
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
        } else if ((e = fm1_enc_take(ENC_KNOB1)) != 0) turn_length(e), dirty = 1;
        len = game.prog[sel_bot].len ? game.prog[sel_bot].len : game.loop_len;
        if (page == PAGE_GLO || page == PAGE_SCL) {
        } else if (page == PAGE_SEQ) {                 /* KNOB2 scrolls row by row, OCT- / OCT+ by 16; then it follows */
            if ((e = fm1_enc_take(ENC_KNOB2)) != 0) seq_scroll_to(seq_target / SEQ_ROW + e), seq_touched = fm1_ms;
            if (b & (1u << BTN_OCT_DOWN)) seq_scroll_to(seq_target / SEQ_ROW - 16), seq_touched = fm1_ms;
            if (b & (1u << BTN_OCT_UP)) seq_scroll_to(seq_target / SEQ_ROW + 16), seq_touched = fm1_ms;
            if (game.playing && fm1_ms - seq_touched > 3000u && game.step >= 0) {   /* the step playing in view */
                const struct rs_view *w = view_now();
                int32_t at = w->step >= 0 ? w->step : 0, top = seq_target / SEQ_ROW;
                if (at < top + 1 || at > top + 14) seq_scroll_to(at - 1);
            }
            if (seq_scroll != seq_target) {     /* glide there: a third of the way a frame */
                int32_t d = (seq_target - seq_scroll) / 3;
                seq_scroll += d ? d : seq_target > seq_scroll ? 1 : -1;
                dirty = 1;
            }
        } else {
            (void)fm1_enc_take(ENC_KNOB2);
            if (b & (1u << BTN_OCT_DOWN) && bank > 0) bank--, dirty = 1;
            if (b & (1u << BTN_OCT_UP) && (bank + 1u) * 16u < len) bank++, dirty = 1;
            if (bank * 16u >= len) bank = 0;
        }
        schedule();
        fm1_led_key(BTN_PLAY, game.playing);
        fm1_led_key(BTN_HOME, page == PAGE_HOME);
        fm1_led_key(BTN_SEQ, page == PAGE_SEQ);
        fm1_led_key(BTN_GLO, page == PAGE_GLO);
        fm1_led_key(BTN_SEL, page == PAGE_SCL);
        fm1_led_key(BTN_EDIT, sel_bot);         /* lit: Echo-3's register */
        fm1_led_key(BTN_OCT_DOWN, page == PAGE_SEQ ? seq_target > 0 : bank > 0);
        fm1_led_key(BTN_OCT_UP, page == PAGE_SEQ ? seq_target < ((int32_t)game.loop_len - 16) * SEQ_ROW : (bank + 1u) * 16u < len);
        v = view_now();
        key = v->t0 ^ (uint32_t)v->playing << 31 ^ (uint32_t)v->step << 20;
        if (key != shown) {                     /* a new step heard: its effects start now */
            if (v->playing && shown != ~0u) screen_effects(&fx, &last, v, fm1_ms);
            last = *v, shown = key, dirty = 1;
        } else if (due && (int32_t)(fm1_ms - due) >= 0)   /* an effect ran out */
            dirty = 1;
        if (dirty) {
            ui_of(&ui, v);
            if (page == PAGE_SEQ) draw_seq(v, &ui, &game, seq_scroll, keys_top());
            else if (page == PAGE_GLO) draw_glo(&game, pending_loop);
            else if (page == PAGE_SCL) draw_scl(&game, scl_cursor);
            else draw_board(v, &fx, &ui, fm1_ms);
            due = fx_due(&fx, fm1_ms);
        }
    }
}
