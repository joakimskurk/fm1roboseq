/* SPDX-License-Identifier: GPL-3.0-only */
/* The C voices (rs_voice.c) play tools/tone-ref's stimuli, as tools/tone-ref/compare.py writes them out
 * (build/parity/stimuli.txt), into build/parity/<name>.wav (32-bit float, mono) for compare.py to hold against the
 * HTML's renders in tests/golden/html.
 *   render build/parity/stimuli.txt build/parity
 * A stimulus: "S name voice seconds", then its notes "n midi duration time velocity" (midi -1: noise). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rs_voice.h"

static void wav(const char *path, const float *x, uint32_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    uint32_t data = n * 4, v32;
    uint16_t v16;
    fwrite("RIFF", 1, 4, f), v32 = 36 + data, fwrite(&v32, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f);
    v32 = 16, fwrite(&v32, 4, 1, f), v16 = 3, fwrite(&v16, 2, 1, f), v16 = 1, fwrite(&v16, 2, 1, f);
    v32 = RS_SR, fwrite(&v32, 4, 1, f), v32 = RS_SR * 4, fwrite(&v32, 4, 1, f), v16 = 4, fwrite(&v16, 2, 1, f);
    v16 = 32, fwrite(&v16, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
    fwrite(x, 4, n, f);
    fclose(f);
}

static struct { char name[64]; int voice; double seconds; int n; int midi[64]; double dur[64], at[64], vel[64]; } st;

static void render_one(const char *dir)
{
    if (!st.name[0]) return;
    uint32_t n = (uint32_t)(st.seconds * RS_SR + 0.5);
    int32_t *acc = calloc(n, sizeof *acc);
    float *out = malloc(n * sizeof *out);
    static struct rs_voice v;
    rs_voice_init(&v, (uint8_t)st.voice, 12345);
    int next = 0;                                       /* the notes go in shortly before they start, as the score's do */
    for (uint32_t i = 0; i < n; i += 128) {
        while (next < st.n && st.at[next] * RS_SR < i + 4096.0) {
            rs_voice_note(&v, (uint32_t)(st.at[next] * RS_SR + 0.5), (uint32_t)(st.dur[next] * RS_SR + 0.5),
                          st.midi[next], (uint16_t)(st.vel[next] * 32768 + 0.5));
            next++;
        }
        rs_voice_render(&v, acc + i, n - i < 128 ? n - i : 128, i);
    }
    for (uint32_t i = 0; i < n; i++) out[i] = (float)acc[i] / 32768.0f;
    char path[512];
    snprintf(path, sizeof path, "%s/%s.wav", dir, st.name);
    wav(path, out, n);
    free(acc), free(out);
    st.name[0] = 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "render stimuli.txt outdir\n"); return 2; }
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 1; }
    char line[512], name[64], voice[64];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == 'S') {
            render_one(argv[2]);
            double sec;
            if (sscanf(line, "S %63s %63s %lf", name, voice, &sec) != 3) continue;
            int id = -1;
            for (int k = 0; k < RS_NVOICES; k++)
                if (!strcmp(rs_voice_name[k], voice)) id = k;
            if (id < 0) { fprintf(stderr, "voice %s?\n", voice); return 1; }
            snprintf(st.name, sizeof st.name, "%s", name);
            st.voice = id, st.seconds = sec, st.n = 0;
        } else if (line[0] == 'n' && st.n < 64) {
            sscanf(line, "n %d %lf %lf %lf", &st.midi[st.n], &st.dur[st.n], &st.at[st.n], &st.vel[st.n]);
            st.n++;
        }
    }
    render_one(argv[2]);
    fclose(f);
    return 0;
}
