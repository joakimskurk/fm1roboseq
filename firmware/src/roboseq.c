/* SPDX-License-Identifier: GPL-3.0-only */
/* RoboSeq for the M-VAVE FM-1: the app. Phase 2: the HTML's default song plays, its sound as the HTML's
 * (reference/roboseq.html; ROADMAP.md). The controls, the board on the screen and the pages come in phase 3.
 *
 *   PLAY       starts the default song from its start, and stops it (the HTML's PLAY and STOP)
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

/* ---- the screen (phase 3 makes it the board): the name, what plays, the step, the ISR's load */
#define C_ACC RGB(180, 140, 255)
#define C_OFF RGB(90, 90, 90)

static void put_u(char *p, uint32_t v, uint32_t w)   /* v in w digits, right-aligned, spaces in front */
{
    uint32_t i;
    for (i = w; i-- > 0;) { p[i] = v || i == w - 1u ? (char)('0' + v % 10u) : ' '; v /= 10u; }
}

static void draw_state(void)
{
    char s[24] = "STOPPED";
    if (game.playing) {
        char t[24] = "STEP      CYCLE     ";
        put_u(t + 5, (uint32_t)(game.step < 0 ? 0 : game.step + 1), 2);
        put_u(t + 16, game.cycle + 1u, 4);
        draw_text_box(0, 120, 240, &FONT_S, t, C_WHITE, 1);
        draw_text_box(0, 90, 240, &FONT_L, "PLAYING", C_ACC, 1);
        return;
    }
    draw_text_box(0, 120, 240, &FONT_S, "PLAY starts the default song", C_OFF, 1);
    draw_text_box(0, 90, 240, &FONT_L, s, C_OFF, 1);
}

static void draw_load(uint32_t us)
{
    char s[32] = "audio      us of 5805";
    put_u(s + 6, us, 5);
    draw_text_box(0, 212, 240, &FONT_S, s, C_OFF, 1);
}

volatile uint32_t rs_boot_check;               /* rs_selfcheck() at boot: make emu reads it from RAM */
static void app_main(void)
{
    uint32_t drawn = ~0u, load_drawn = ~0u, st;
    rs_boot_check = rs_selfcheck();             /* the model on the chip: make emu compares it with the computer's */
    rs_mix_init(&mix, 0x5EEDu);
    rs_default(&game);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 18, 240, &FONT_L, APP_NAME, C_ACC, 1);
    draw_text_box(0, 52, 240, &FONT_S, "the robots' sequencer  v" APP_VER, C_OFF, 1);

    for (;;) {
        uint32_t rel, b;
        platform_poll();
        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_PLAY)) { if (game.playing) stop(); else play(); }
        schedule();
        fm1_led_key(BTN_PLAY, game.playing);
        if ((st = (uint32_t)game.playing | (uint32_t)(game.step + 1) << 1 | game.cycle << 8) != drawn) { drawn = st; draw_state(); }
        if (rs_audio_us_worst / 50u != load_drawn) { load_drawn = rs_audio_us_worst / 50u; draw_load(rs_audio_us_worst); }
    }
}
