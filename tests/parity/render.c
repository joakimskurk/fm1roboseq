/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Joakim (joakimskurk) */
/* The C sound (rs_voice.c, rs_mix.c) plays tools/tone-ref's stimuli, as tools/tone-ref/compare.py writes them out
 * (build/parity/stimuli.txt), into build/parity/<name>.wav (32-bit float, mono) for compare.py to hold against the
 * HTML's renders in tests/golden/html.
 *   render build/parity/stimuli.txt build/parity
 * The stimuli:
 *   S name voice seconds    one voice, straight out; then its notes "n midi duration time velocity" (midi -1: noise)
 *   M name seconds          the whole mix in stereo (buses, reverb, limiter); then "m voice midi duration time velocity"
 *   G name seconds          the default song: rs_tick from rs_default(), the score into the mix, as the HTML's
 *                           transport plays advanceSequence() every 16th
 *   L name seconds          the limiter alone on compressor.py's test signal
 *   R name seconds          the reverb's impulse response alone */
#define _DEFAULT_SOURCE                         /* M_PI */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rs_mix.c"

static void wav(const char *path, const float *x, uint32_t n, uint16_t ch)   /* n frames of ch interleaved */
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    uint32_t data = n * 4 * ch, v32;
    uint16_t v16;
    fwrite("RIFF", 1, 4, f), v32 = 36 + data, fwrite(&v32, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f);
    v32 = 16, fwrite(&v32, 4, 1, f), v16 = 3, fwrite(&v16, 2, 1, f), fwrite(&ch, 2, 1, f);
    v32 = RS_SR, fwrite(&v32, 4, 1, f), v32 = RS_SR * 4 * ch, fwrite(&v32, 4, 1, f), v16 = (uint16_t)(4 * ch), fwrite(&v16, 2, 1, f);
    v16 = 32, fwrite(&v16, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
    fwrite(x, 4, (size_t)n * ch, f);
    fclose(f);
}

#define MAXN 64
static struct {
    char kind, name[64];
    int voice;
    double seconds;
    int n;
    int v[MAXN], midi[MAXN];
    double dur[MAXN], at[MAXN], vel[MAXN];
} st;
static struct rs_mix mix;

static void feed(int k)
{
    uint32_t at = (uint32_t)(st.at[k] * RS_SR + 0.5), dur = (uint32_t)(st.dur[k] * RS_SR + 0.5);
    uint16_t vel = (uint16_t)(st.vel[k] * 32768 + 0.5);
    if (st.kind == 'S') rs_voice_note(&mix.v[0], at, dur, st.midi[k], vel);
    else rs_mix_note(&mix, (uint8_t)st.v[k], at, dur, st.midi[k], vel);
}

/* compressor.py's test signal (probe_mix.mjs's): 440 Hz in 3 dB steps, then bursts */
static double test_signal(uint32_t i)
{
    uint32_t step = (uint32_t)(0.35 * RS_SR);
    if (i < 17 * step) return pow(10, (-36 + 3.0 * (i / step)) / 20) * sin(2 * M_PI * 440 * i / RS_SR);
    uint32_t j = i - 17 * step;
    for (int b = 0; b < 8; b++) {
        uint32_t len = (uint32_t)lround((0.03 + 0.04 * b) * RS_SR), quiet = (uint32_t)(0.4 * RS_SR);
        if (j < len) return 2 * sin(2 * M_PI * 220 * i / RS_SR);
        j -= len;
        if (j < quiet) return 0.063 * sin(2 * M_PI * 220 * i / RS_SR);
        j -= quiet;
    }
    return 0;
}

static void render_one(const char *dir)
{
    if (!st.name[0]) return;
    uint32_t n = (uint32_t)(st.seconds * RS_SR + 0.5);
    n = (n + RS_MIX_SUB - 1) / RS_MIX_SUB * RS_MIX_SUB;
    uint16_t ch = st.kind == 'S' || st.kind == 'L' ? 1 : 2;   /* the mix and the reverb in stereo */
    int32_t *acc = calloc((size_t)n * ch, sizeof *acc);
    float *out = malloc((size_t)n * ch * sizeof *out);
    rs_mix_init(&mix, 12345);
    if (st.kind == 'S') rs_voice_init(&mix.v[0], (uint8_t)st.voice, 12345);
    static struct rs_state s;
    static struct rs_tick_out o;
    uint64_t tick_q8 = 0;
    if (st.kind == 'G') rs_default(&s), rs_start(&s);
    int next = 0;
    for (uint32_t i = 0; i < n; i += 128) {             /* the notes go in shortly before they start, as the score's do */
        uint32_t len = n - i < 128 ? n - i : 128;
        if (st.kind == 'S' || st.kind == 'M')
            while (next < st.n && st.at[next] * RS_SR < i + 4096.0) feed(next++);
        if (st.kind == 'G')
            while (tick_q8 < (uint64_t)(i + 4096) << 8) {
                rs_tick(&s, &o);
                for (int k = 0; k < o.nnotes; k++) {
                    const struct rs_note *e = &o.notes[k];
                    rs_mix_note(&mix, e->voice, (uint32_t)((tick_q8 + e->at + 128) >> 8), (e->dur + 128) >> 8, e->midi, e->vel);
                }
                tick_q8 += rs_step_q8(&s);
            }
        if (st.kind == 'S') rs_voice_render(&mix.v[0], acc + i, len, i);
        else if (st.kind == 'M' || st.kind == 'G') rs_mix_render(&mix, acc + 2u * i, len, i);
        else if (st.kind == 'L')
            for (uint32_t f = 0; f < len; f += RS_MIX_SUB) {
                lim_division(&mix);
                for (uint32_t k = f; k < f + RS_MIX_SUB; k++) {
                    int32_t x = (int32_t)lrint(test_signal(i + k) * 32768), o2[2];
                    limiter(&mix, x, x, o2);
                    acc[i + k] = o2[0];
                }
            }
        else if (st.kind == 'R')                        /* 1.0 in, Q24 out, both channels */
            for (uint32_t k = 0; k < len; k++) acc[2u * (i + k)] = reverb(&mix, i + k == 0 ? 32768 : 0, &acc[2u * (i + k) + 1u]);
    }
    for (uint32_t i = 0; i < n * ch; i++) out[i] = (float)acc[i] / (st.kind == 'R' ? 16777216.0f : 32768.0f);
    char path[512];
    snprintf(path, sizeof path, "%s/%s.wav", dir, st.name);
    wav(path, out, n, ch);
    if (mix.clips) fprintf(stderr, "%s: the reverb's lines were full %u times\n", st.name, mix.clips);
    if (rs_voice_dropped) fprintf(stderr, "%s: the voices' timelines dropped %u events\n", st.name, rs_voice_dropped);
    rs_voice_dropped = 0;
    free(acc), free(out);
    st.name[0] = 0;
}

static int voice_id(const char *name)
{
    for (int k = 0; k < RS_NVOICES; k++)
        if (!strcmp(rs_voice_name[k], name)) return k;
    fprintf(stderr, "voice %s?\n", name);
    exit(1);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "render stimuli.txt outdir\n"); return 2; }
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 1; }
    char line[512], name[64], voice[64];
    while (fgets(line, sizeof line, f)) {
        double sec;
        if (line[0] == 'S') {
            render_one(argv[2]);
            if (sscanf(line, "S %63s %63s %lf", name, voice, &sec) != 3) continue;
            st.voice = voice_id(voice);
        } else if (line[0] && strchr("MGLR", line[0])) {
            render_one(argv[2]);
            if (sscanf(line + 1, " %63s %lf", name, &sec) != 2) continue;
        } else if (line[0] == 'n' && st.n < MAXN) {
            sscanf(line, "n %d %lf %lf %lf", &st.midi[st.n], &st.dur[st.n], &st.at[st.n], &st.vel[st.n]);
            st.n++;
            continue;
        } else if (line[0] == 'm' && st.n < MAXN) {
            sscanf(line, "m %63s %d %lf %lf %lf", voice, &st.midi[st.n], &st.dur[st.n], &st.at[st.n], &st.vel[st.n]);
            st.v[st.n++] = voice_id(voice);
            continue;
        } else continue;
        st.kind = line[0];
        snprintf(st.name, sizeof st.name, "%s", name);
        st.seconds = sec, st.n = 0;
    }
    render_one(argv[2]);
    fclose(f);
    return 0;
}
