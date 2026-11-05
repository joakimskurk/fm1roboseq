/* SPDX-License-Identifier: GPL-3.0-only */
/* The C model (firmware/src/rs_model.c) against reference/roboseq.html, tick by tick: every scenario in
 * tests/golden/traces (made by tools/trace/gen.mjs from the HTML's own script) is set up the same way, run, and each
 * tick's line is compared with the HTML's. Then rs_chance against the HTML's seededChance for 1024 loops.
 *   trace_test [tests/golden]     exit 0: all equal */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rs_model.h"

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
}

static int run_trace(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { printf("%s: cannot open\n", path); return 1; }
    static struct rs_state s;
    rs_default(&s);
    char line[1024], want[1024], got[1024];
    long tick = 0, ticks = -1, lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        line[strcspn(line, "\n")] = 0;
        char a[32] = "", b[32] = "", c[32] = "", d[32] = "", e[32] = "";
        if (line[0] == '#' || !line[0]) continue;
        if (line[0] != 'T') {
            sscanf(line, "%31s %31s %31s %31s %31s", a, b, c, d, e);
            if (!strcmp(a, "board")) { s.playing = 0; rs_load_board(&s, (uint8_t)board_id(b)); }
            else if (!strcmp(a, "loop")) s.loop_len = (uint8_t)atoi(b);
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
        struct rs_tick_out o;
        rs_tick(&s, &o);
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
    const char *dir = argc > 1 ? argv[1] : "tests/golden";
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
    printf("traces: %d scenarios, %d differ from the HTML\n", n, failed);
    snprintf(path, sizeof path, "%s/chance.txt", dir);
    failed += chance_test(path);
    printf("selfcheck: %08x (the firmware computes the same at boot: make emu)\n", (unsigned)rs_selfcheck());
    return failed != 0;
}
