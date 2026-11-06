/* SPDX-License-Identifier: GPL-3.0-only */
/* The lead (rs_lead.c, D21) on its own: a held note at its pitch, legato gliding to the next, the vibrato only after
 * its delay, silence after the release, a new press from silence starting again. lead_test: exit 0 all pass */
#include <math.h>
#include <stdio.h>
#include "rs_lead.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL lead_test: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static struct rs_lead l;
static int32_t buf[44100];

static void run(uint32_t n) { for (uint32_t i = 0; i < n; i++) buf[i] = 0; rs_lead_render(&l, buf, n, 0); }

static double freq(uint32_t n)                  /* the pitch over n samples, by autocorrelation (the folded wave */
{                                               /* crosses zero more than once a cycle) */
    int best = 0;
    double bv = -1e300, prev = 0, cur = 0;
    for (int lag = 20; lag < 900 && lag < (int)n / 2; lag++) {
        double acc = 0;
        for (uint32_t i = 0; i + (uint32_t)lag < n; i++) acc += (double)buf[i] * buf[i + lag];
        acc /= (double)(n - (uint32_t)lag);
        if (lag > 21 && cur > prev && cur >= acc && cur > bv * 0.9 && (best == 0 || cur > bv)) bv = cur, best = lag - 1;
        prev = cur, cur = acc;
        if (best && lag > best * 1.5) break;    /* the first strong peak is the period */
    }
    if (!best) return 0;
    double y0 = 0, y1 = 0, y2 = 0;              /* a parabola through the peak: the lag between samples */
    for (int d = -1; d <= 1; d++) {
        double acc = 0;
        for (uint32_t i = 0; i + (uint32_t)(best + d) < n; i++) acc += (double)buf[i] * buf[i + best + d];
        acc /= (double)(n - (uint32_t)(best + d));
        if (d < 0) y0 = acc; else if (d == 0) y1 = acc; else y2 = acc;
    }
    double den = y0 - 2 * y1 + y2, off = den != 0 ? 0.5 * (y0 - y2) / den : 0;
    return 44100.0 / (best + off);
}

static double spread(uint32_t n, uint32_t win)  /* how much the pitch moves: max - min over windows */
{
    double lo = 1e9, hi = 0;
    static int32_t keep[44100];
    for (uint32_t i = 0; i < n; i++) keep[i] = buf[i];
    for (uint32_t w = 0; w + win <= n; w += win) {
        for (uint32_t i = 0; i < win; i++) buf[i] = keep[w + i];
        double f = freq(win);
        if (f > 0) lo = f < lo ? f : lo, hi = f > hi ? f : hi;
    }
    return hi - lo;
}

int main(void)
{
    rs_lead_init(&l);
    run(4096);
    CHECK(buf[100] == 0, "silent before a note");
    rs_lead_play(&l, 69, 1, 1);                 /* A4 */
    run(8820);                                  /* 200 ms: before the vibrato */
    double f = freq(8820);
    CHECK(fabs(f - 440) < 3, "A4 at 440 Hz (%.1f)", f);
    run(8820);
    CHECK(spread(8820, 2205) < 5, "no vibrato in the first 250 ms (the PWM moves the estimate +-1.5 Hz)");
    run(26460);                                 /* 0.6 s more: the vibrato is in */
    double sp = spread(26460, 2205);
    CHECK(sp > 8 && sp < 40, "a vibrato once held (it moves %.1f Hz)", sp);
    rs_lead_play(&l, 76, 1, 1);                 /* E5, legato: it glides */
    run(441);
    f = freq(441);
    CHECK(f > 440 && f < 640, "a glide on the way to E5 (%.1f)", f);
    run(8820);
    f = freq(8820);
    CHECK(fabs(f - 659.3) < 15, "E5 reached (%.1f)", f);
    rs_lead_play(&l, 76, 0, 0);                 /* let go */
    run(13230);                                 /* 300 ms: past the release */
    int32_t peak = 0;
    for (int i = 11000; i < 13230; i++) peak = buf[i] > peak ? buf[i] : -buf[i] > peak ? -buf[i] : peak;
    CHECK(peak == 0 && l.stage == 0, "silent after the release (%d)", peak);
    rs_lead_play(&l, 57, 1, 1);                 /* A3 from silence: no glide */
    run(4410);
    f = freq(4410);
    CHECK(fabs(f - 220) < 3, "a new note from silence starts at its pitch (%.1f)", f);
    /* the arp (D26): four notes, a step every 100 ms; each step its next note, round again */
    rs_lead_init(&l);
    static const int8_t ARP[4] = {57, 60, 64, 67};
    static const double HZ[4] = {220.0, 261.63, 329.63, 392.0};
    for (int i = 0; i < 4; i++) l.arp_notes[i] = ARP[i];
    l.arp_n = 4;
    for (int i = 0; i < 8; i++) l.tick_at[i] = 4410u * (uint32_t)(i + 1);
    l.tick_head = 8;
    rs_lead_play(&l, 57, 1, 1);
    static int32_t seg[4410];
    uint32_t now = 0;
    for (int stepn = 0; stepn < 9; stepn++, now += 4410) {
        for (uint32_t i = 0; i < 4410; i++) seg[i] = 0;
        rs_lead_render(&l, seg, 4410, now);
        if (stepn == 0) continue;               /* (the key's own note until the first step) */
        for (uint32_t i = 0; i < 4410; i++) buf[i] = seg[i];
        f = freq(3000);
        double want = HZ[(stepn - 1) % 4];
        CHECK(fabs(f - want) < want * 0.02, "the arp's step %d at %.1f Hz (%.1f)", stepn, want, f);
    }
    {                                           /* faster (D29): two notes a step, the second half way */
        uint32_t p0 = l.arp_pos;
        l.arp_rate = 2, l.arp_sub = 2205;
        l.tick_at[l.tick_head % 8u] = now + 10u, l.tick_head++;
        for (int i = 0; i < 64; i++) seg[i] = 0;
        rs_lead_render(&l, seg, 64, now);      /* the step: its first note */
        CHECK(l.arp_pos == p0 + 1u, "the step's first note (%u)", l.arp_pos - p0);
        rs_lead_render(&l, seg, 2048, now + 64);   /* not yet half way */
        CHECK(l.arp_pos == p0 + 1u, "nothing before half way");
        rs_lead_render(&l, seg, 256, now + 2112);  /* half way: the second */
        CHECK(l.arp_pos == p0 + 2u, "the second note half way through the step (%u)", l.arp_pos - p0);
        now += 4410, l.arp_rate = 1;
    }
    l.arp_random = 1;                           /* random (D27): a note drawn each step, never the same twice running */
    {
        int prev = -1, same = 0, seen[4] = {0};
        for (int stepn = 0; stepn < 64; stepn++) {
            l.tick_at[l.tick_head % 8u] = now + 10u, l.tick_head++;
            for (int i = 0; i < 32; i++) seg[i] = 0;
            rs_lead_render(&l, seg, 32, now);   /* (a block of 32 frames: the step falls in it) */
            now += 32;
            int at = (int)l.arp_last;
            if (at == prev) same++;
            seen[at % 4]++, prev = at;
        }
        CHECK(!same && seen[0] && seen[1] && seen[2] && seen[3], "random: every note, none twice running (%d)", same);
    }
    l.arp_random = 0;
    l.arp_n = 0;                                /* the arp off: back to the key */
    run(4410);
    f = freq(4410);
    CHECK(fabs(f - 220) < 4.4, "the arp off: the key's note again (%.1f)", f);
    printf("lead_test: %s\n", fails ? "FAILED" : "all pass");
    return fails != 0;
}
