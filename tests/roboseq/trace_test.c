/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Joakim (joakimskurk) */
/* The C model (firmware/src/rs_model.c) against reference/roboseq.html, tick by tick: every scenario in
 * tests/golden/traces (made by tools/trace/gen.mjs from the HTML's own script) is set up the same way, run, and each
 * tick's line is compared with the HTML's. Then rs_chance against the HTML's seededChance for 1024 loops.
 * With --voices, every tick's notes also go into the voices (rs_voice.c) at the tick's start, as the firmware gives
 * them, and the voices play the tick: no voice's timeline may run full.
 *   trace_test [tests/golden] [--voices]     exit 0: all equal */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rs_model.h"
#include "rs_voice.h"

static int find(const char *const *names, int n, const char *w)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(names[i], w)) return i;
    return -1;
}
static int board_id(const char *w) { return !strcmp(w, "--") ? RS_B_NONE : find(rs_board_name, RS_NBOARDS, w); }
static int drum_id(const char *w) { return !strcmp(w, "--") ? RS_D_NONE : find(rs_drum_name, RS_NDRUMS, w); }

static void line_of(const struct rs_state *s, const struct rs_tick_out *o, char *buf, size_t n)
{
    int k = snprintf(buf, n, "T %u %d %u %u %u %u %d %u %u %s", (unsigned)s->cycle, s->step, (unsigned)s->move_tick,
                     s->drum_tick, s->vm_index, s->vm_count, s->seq2_index, s->seq2_active, s->swapped,
                     rs_board_name[s->board_id]);
    for (int b = 0; b < 2; b++) {
        const struct rs_bot *t = &s->bot[b];
        k += snprintf(buf + k, n - k, " | %c %d %d %c %d %u %u", 'A' + b, t->x, t->y, rs_dir_name[t->dir], t->heat,
                      t->deaths, t->dying);
    }
    k += snprintf(buf + k, n - k, " | %u %u | %u %u %u | ", s->clash_cooldown, s->love_steps, o->drum_k, o->drum_s,
                  o->drum_h);
    if (!o->nevents) k += snprintf(buf + k, n - k, "-");
    for (int i = 0; i < o->nevents; i++) k += snprintf(buf + k, n - k, "%s%s", i ? "," : "", rs_event_name[o->events[i]]);
    k += snprintf(buf + k, n - k, " | %x %u%u", (unsigned)o->active, o->clash[0], o->clash[1]);   /* what lit up, who was hit */
}

static long notes_checked;

/* an "N voice midi duration offset velocity" line against the C score's next note (host only: float here is fine) */
static int check_note(const char *path, long tick, const char *line, const struct rs_tick_out *o, int *ni)
{
    char voice[32];
    int midi;
    double dur, at, vel;
    if (sscanf(line, "N %31s %d %lf %lf %lf", voice, &midi, &dur, &at, &vel) != 5) { printf("%s: bad line %s\n", path, line); return 1; }
    if (*ni >= o->nnotes) { printf("%s: tick %ld: the HTML plays more notes, the next: %s\n", path, tick, line); return 1; }
    const struct rs_note *n = &o->notes[(*ni)++];
    double cd = n->dur / (256.0 * RS_SR), ca = n->at / (256.0 * RS_SR), cv = n->vel / 32768.0;
    /* within rounding: 1.5 samples, a velocity step of 1e-4 */
    if (strcmp(voice, rs_voice_name[n->voice]) || midi != n->midi || fabs(cd - dur) > 3.4e-5 || fabs(ca - at) > 3.4e-5 ||
        fabs(cv - vel) > 1e-4) {
        printf("%s: tick %ld, note %d differs\n  html %s\n  c    N %s %d %.9g %.9g %.6g\n", path, tick, *ni - 1, line,
               rs_voice_name[n->voice], n->midi, cd, ca, cv);
        return 1;
    }
    notes_checked++;
    return 0;
}

static int with_voices, voice_peak;
static struct rs_voice voices[RS_NVOICES];
static void play_tick(const struct rs_state *s, const struct rs_tick_out *o, uint64_t *t_q8)
{
    static int32_t out[256];
    for (int k = 0; k < o->nnotes; k++) {
        const struct rs_note *e = &o->notes[k];
        rs_voice_note(&voices[e->voice], (uint32_t)((*t_q8 + e->at + 128) >> 8), (e->dur + 128) >> 8, e->midi, e->vel);
    }
    for (int k = 0; k < RS_NVOICES; k++)
        if (voices[k].nq > voice_peak) voice_peak = voices[k].nq;
    uint32_t a = (uint32_t)(*t_q8 >> 8), b = (uint32_t)((*t_q8 += rs_step_q8(s)) >> 8);
    for (uint32_t t = a; t < b; t += 256)
        for (int k = 0; k < RS_NVOICES; k++) rs_voice_render(&voices[k], out, b - t < 256 ? b - t : 256, t);
}

static int run_trace(const char *path)
{
    uint64_t t_q8 = 0;
    for (int k = 0; k < RS_NVOICES; k++) rs_voice_init(&voices[k], (uint8_t)k, 1u + (uint32_t)k);
    FILE *f = fopen(path, "r");
    if (!f) { printf("%s: cannot open\n", path); return 1; }
    static struct rs_state s;
    rs_default(&s);
    char line[1024], want[1024], got[1024];
    long tick = 0, ticks = -1, lineno = 0;
    static struct rs_tick_out o;
    int ni = 0, have = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        line[strcspn(line, "\n")] = 0;
        char a[32] = "", b[32] = "", c[32] = "", d[32] = "", e[32] = "";
        if (line[0] == '#' || !line[0]) continue;
        if (line[0] == 'N') {
            if (!have || check_note(path, tick - 1, line, &o, &ni)) { fclose(f); return 1; }
            continue;
        }
        if (line[0] == 'T' && have && ni != o.nnotes) {
            printf("%s: tick %ld: C plays %d notes, the HTML %d; the first extra: %s %d\n", path, tick - 1, o.nnotes, ni,
                   rs_voice_name[o.notes[ni].voice], o.notes[ni].midi);
            fclose(f);
            return 1;
        }
        if (line[0] != 'T') {
            sscanf(line, "%31s %31s %31s %31s %31s", a, b, c, d, e);
            if (!strcmp(a, "board")) { s.playing = 0; rs_load_board(&s, (uint8_t)board_id(b)); }
            else if (!strcmp(a, "loop")) s.loop_len = (uint8_t)atoi(b);
            else if (!strcmp(a, "bpm")) { s.bpm = (uint16_t)atoi(b); s.quarter = (uint8_t)atoi(c); s.transpose_root = (uint8_t)atoi(d); }
            else if (!strcmp(a, "tpm")) s.tpm = (uint8_t)atoi(b);
            else if (!strcmp(a, "vmtrigger")) s.vm_manual = !strcmp(b, "manual");
            else if (!strcmp(a, "vmfreq")) s.vm_freq4 = (uint8_t)atoi(b);
            else if (!strcmp(a, "bassmodlen")) s.bassmod_len = (uint8_t)atoi(b);
            else if (!strcmp(a, "drums")) { s.swapped = 0; s.drums_on = (uint8_t)atoi(b); rs_set_drums(&s, (uint8_t)drum_id(c)); }
            else if (!strcmp(a, "seq2")) { s.seq2_active = (uint8_t)atoi(b); s.seq2_pending = (uint8_t)atoi(c); s.seq2_index = 0; }
            else if (!strcmp(a, "seq2step")) {
                struct rs_seq2_step *st = &s.seq2[atoi(b) & 3];
                st->board = (uint8_t)board_id(c), st->drums = (uint8_t)drum_id(d), st->regswap = (uint8_t)atoi(e);
            } else if (!strcmp(a, "prog")) s.prog[a[0] && b[0] == 'B'].len = (uint8_t)atoi(c);
            else if (!strcmp(a, "step")) {
                char f2[32] = "";
                sscanf(line, "%*s %*s %*s %*s %*s %31s", f2);
                struct rs_step *st = &s.prog[b[0] == 'B'].s[atoi(c) & 63];
                st->action = (uint8_t)find(rs_action_name, RS_NACTIONS, d);
                st->modifier = (uint8_t)find(rs_modifier_name, RS_NMODIFIERS, e);
                st->prob = (uint8_t)atoi(f2);
            } else if (!strcmp(a, "ticks")) {
                ticks = atol(b);
                s.pending_board = RS_B_NONE;
                rs_start(&s);
            } else { printf("%s:%ld: what is \"%s\"\n", path, lineno, a); fclose(f); return 1; }
            continue;
        }
        rs_tick(&s, &o);
        if (with_voices) play_tick(&s, &o, &t_q8);
        have = 1, ni = 0;
        line_of(&s, &o, got, sizeof got);
        snprintf(want, sizeof want, "%s", line);
        if (strcmp(got, want)) {
            printf("%s: tick %ld differs\n  html %s\n  c    %s\n", path, tick, want, got);
            fclose(f);
            return 1;
        }
        tick++;
    }
    fclose(f);
    if (have && ni != o.nnotes) { printf("%s: the last tick: C plays %d notes, the HTML %d\n", path, o.nnotes, ni); return 1; }
    if (tick != ticks) { printf("%s: %ld ticks of %ld\n", path, tick, ticks); return 1; }
    return 0;
}

static int chance_test(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { printf("%s: cannot open\n", path); return 1; }
    char line[256], bot;
    unsigned cycle, rows = 0, bad = 0;
    char hex[80];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        if (sscanf(line, " %c %u %79s", &bot, &cycle, hex) != 3) continue;
        for (unsigned s = 0; s < 64; s++) {
            char h = hex[s / 2];
            unsigned nib = (unsigned)(h <= '9' ? h - '0' : h - 'a' + 10);
            unsigned want = (s & 1) ? nib & 3 : nib >> 2;
            uint32_t q = rs_chance_q32(bot == 'B', s, cycle, 0);
            unsigned got = q > 0xC0000000u ? 2 : q > 0x80000000u ? 1 : 0;
            if (got != want && bad++ < 5) printf("chance %c step %u cycle %u: html %u, c %u (%08x)\n", bot, s, cycle, want, got, q);
        }
        rows++;
    }
    fclose(f);
    printf("chance: %u steps x loops, %u differ\n", rows * 64, bad);
    return bad != 0 || rows == 0;
}

int main(int argc, char **argv)
{
    const char *dir = "tests/golden";
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--voices")) with_voices = 1;
        else dir = argv[i];
    char path[512], name[128];
    snprintf(path, sizeof path, "%s/traces/INDEX", dir);
    FILE *idx = fopen(path, "r");
    if (!idx) { printf("%s: cannot open\n", path); return 1; }
    int n = 0, failed = 0;
    while (fgets(name, sizeof name, idx)) {
        name[strcspn(name, "\n")] = 0;
        if (name[0] == '#' || !name[0]) continue;
        snprintf(path, sizeof path, "%s/traces/%s.trace", dir, name);
        failed += run_trace(path);
        n++;
    }
    fclose(idx);
    printf("traces: %d scenarios, %d differ from the HTML (%ld notes checked)\n", n, failed, notes_checked);
    if (with_voices) {
        printf("voices: every note played, the fullest timeline %d of %d, %u events lost\n", voice_peak, RS_VQ,
               (unsigned)rs_voice_dropped);
        failed += rs_voice_dropped != 0;
    }
    snprintf(path, sizeof path, "%s/chance.txt", dir);
    failed += chance_test(path);
    printf("selfcheck: %08x (the firmware computes the same at boot: make emu)\n", (unsigned)rs_selfcheck());
    return failed != 0;
}
