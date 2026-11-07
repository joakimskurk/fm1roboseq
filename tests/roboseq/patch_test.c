/* SPDX-License-Identifier: GPL-3.0-only */
/* The board's touch on the bots' synths (rs_plus_patch, rs_mix_patch, D36a): a little, each board its own, an empty
 * board and CLASSIC the HTML's voices exactly; the patched pulse in range and without a DC. A note's gesture (D37a):
 * the vibratos and the bends as they should be, none the HTML's. patch_test: exit 0 */
#include <stdio.h>
#include <string.h>
#include "rs_mix.c"
#include "rs_model.c"
#include "rs_score.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL patch_test: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static long render(uint32_t pw, int32_t soft, uint8_t shape, int32_t *peak, long *sum)   /* a held A4 on Scout-7's voice */
{
    static struct rs_voice v;
    static int32_t buf[4096];
    rs_voice_init(&v, RS_V_A_MAIN, 1);
    v.pw = pw, v.soft = soft, v.shape = shape;
    rs_voice_note(&v, 0, 44100u * 256u, 69, 32767);
    long energy = 0;
    *peak = 0, *sum = 0;
    for (uint32_t now = 0; now < 44100u; now += 4096u) {
        memset(buf, 0, sizeof buf);
        rs_voice_render(&v, buf, 4096u, now);
        for (int i = 0; i < 4096; i++) {
            int32_t a = buf[i] < 0 ? -buf[i] : buf[i];
            if (a > *peak) *peak = a;
            *sum += buf[i], energy += (long)buf[i] * buf[i] >> 8;
        }
    }
    return energy;
}

static int periods(uint8_t fx, int32_t *per, int max)   /* a held A4 (100.2 samples a cycle) with a gesture: its cycles */
{
    static struct rs_voice v;
    static int32_t buf[44100];
    rs_voice_init(&v, RS_V_A_MAIN, 1);
    rs_voice_note_fx(&v, 0, 44100u * 256u, 69, 32767, fx);
    memset(buf, 0, sizeof buf);
    for (uint32_t now = 0; now < 44100u; now += 32u) rs_voice_render(&v, buf + now, 32u, now);
    int n = 0, last = -1;
    for (int i = 1; i < 44100 && n < max; i++)
        if (buf[i - 1] < 0 && buf[i] >= 0) {            /* a rising edge: a cycle */
            if (last >= 0) per[n++] = i - last;
            last = i;
        }
    return n;
}

int main(void)
{
    static struct rs_patch p[RS_NBOARDS];
    struct rs_board empty;
    memset(&empty, 0, sizeof empty);
    for (uint8_t b = 0; b < RS_NBOARDS; b++) {
        struct rs_board bd;
        rs_preset_board(&bd, b);
        rs_plus_patch(&bd, &p[b]);
        for (int i = 0; i < 2; i++) {
            CHECK(p[b].pw[i] >= 22000 && p[b].pw[i] <= 32768, "board %d bot %d: the duty a little off a half (%u)", b, i, p[b].pw[i]);
            CHECK(p[b].pwm[i] <= 5000 && p[b].pw[i] - p[b].pwm[i] >= 18000, "board %d bot %d: a small swing (%u)", b, i, p[b].pwm[i]);
            CHECK(p[b].soft[i] <= 11500, "board %d bot %d: a little triangle (%u)", b, i, p[b].soft[i]);
        }
        printf("patch_test: board %d: duty %u/%u swing %u/%u at %u mHz soft %u/%u env %u %u %u\n", b, p[b].pw[0], p[b].pw[1],
               p[b].pwm[0], p[b].pwm[1], p[b].rate, p[b].soft[0], p[b].soft[1], p[b].env[0], p[b].env[1], p[b].env[2]);
        for (uint8_t a = 0; a < b; a++) CHECK(memcmp(&p[a], &p[b], sizeof p[a]), "boards %d and %d: a patch each", a, b);
    }
    {                                                   /* an empty board: the HTML's voices */
        struct rs_patch e;
        rs_plus_patch(&empty, &e);
        CHECK(e.pw[0] == 32768 && e.pw[1] == 32768 && !e.pwm[0] && !e.pwm[1] && !e.soft[0] && !e.soft[1] &&
              !e.env[0] && !e.env[1] && !e.env[2], "an empty board: no touch");
    }
    {                                                   /* the HTML's square three ways the same; the patched one sound */
        int32_t pk0, pk1, pk2, pk;
        long s0, s1, s2, s;
        long e0 = render(0, 0, 0, &pk0, &s0), e1 = render(0x80000000u, 0, 0, &pk1, &s1);
        CHECK(e0 == e1 && s0 == s1 && pk0 == pk1, "a half's duty is the HTML's square, sample for sample");
        long e2 = render(22000u << 16, 11500, 2, &pk2, &s2);
        CHECK(e2 != e0, "the patch is heard");
        CHECK(pk2 <= pk0 + pk0 / 4, "the patched pulse no louder than a quarter over (%d, %d)", pk2, pk0);
        CHECK((s2 < 0 ? -s2 : s2) < 44100L * 400, "and no DC (%ld)", s2);
        long e3 = render(0, 0, 1, &pk, &s);
        CHECK(e3 < e0 && e0 < render(0, 0, 2, &pk, &s), "shorter is less, longer more");
    }
    {                                                   /* a note's gesture (D37a) */
        static int32_t p0[600], p1[600], p2[600], p3[600];
        int n0 = periods(0, p0, 600), n1 = periods(RS_FX_VIB_LATE, p1, 600), n2 = periods(RS_FX_VIB_WIDE, p2, 600);
        int n3 = periods(RS_FX_BEND_UP, p3, 600);
        int early_same = 1, lo1 = 1000, hi1 = 0, lo2 = 1000, hi2 = 0, lo0 = 1000, hi0 = 0;
        for (int i = 0; i < 100 && i < n1; i++) early_same &= p1[i] == p0[i];   /* (the first 0.23 s) */
        for (int i = 300; i < n0 && i < n1 && i < 430; i++) {
            lo0 = p0[i] < lo0 ? p0[i] : lo0, hi0 = p0[i] > hi0 ? p0[i] : hi0;
            lo1 = p1[i] < lo1 ? p1[i] : lo1, hi1 = p1[i] > hi1 ? p1[i] : hi1;
        }
        for (int i = 20; i < 120 && i < n2; i++) lo2 = p2[i] < lo2 ? p2[i] : lo2, hi2 = p2[i] > hi2 ? p2[i] : hi2;
        printf("patch_test: cycles steady %d-%d, late vibrato %d-%d, wide %d-%d, bend's first %d then %d\n", lo0, hi0, lo1, hi1,
               lo2, hi2, p3[0], p3[n3 - 1]);
        CHECK(n0 > 400 && hi0 - lo0 <= 1, "no gesture: a steady A4");
        CHECK(early_same, "the late vibrato waits a quarter second");
        CHECK(hi1 - lo1 >= 2 && hi1 - lo1 <= 5, "then it sings, about +-1 %% (%d-%d)", lo1, hi1);
        CHECK(hi2 - lo2 >= 4 && hi2 - lo2 <= 8, "a wobble at once, wider (%d-%d)", lo2, hi2);
        CHECK(p3[0] >= 104 && p3[25] <= 101 && p3[25] >= 99, "a bend from a semitone below, home in 50 ms (%d, %d)", p3[0], p3[25]);
        int32_t pk, pk2;
        long s0, s1;
        long e0 = render(0, 0, 0, &pk, &s0);
        static struct rs_voice v;
        static int32_t buf[4096];
        long e1 = 0;
        rs_voice_init(&v, RS_V_A_MAIN, 1);
        rs_voice_note_fx(&v, 0, 44100u * 256u, 69, 32767, 0);
        pk2 = 0, s1 = 0;
        for (uint32_t now = 0; now < 44100u; now += 4096u) {
            memset(buf, 0, sizeof buf);
            rs_voice_render(&v, buf, 4096u, now);
            for (int i = 0; i < 4096; i++) s1 += buf[i], e1 += (long)buf[i] * buf[i] >> 8;
        }
        CHECK(e0 == e1 && s0 == s1, "rs_voice_note is rs_voice_note_fx with none, sample for sample");
        (void)pk2;
    }
    {                                                   /* CLASSIC: rs_mix_patch with nothing leaves the voices as they were */
        static struct rs_mix m;
        struct rs_patch z;
        memset(&z, 0, sizeof z);
        rs_mix_init(&m, 1);
        rs_mix_patch(&m, &p[RS_B_SPINNER]);
        rs_mix_patch(&m, &z);
        int clean = !m.pw_swing[0] && !m.pw_swing[1];
        for (int k = 0; k < RS_NVOICES; k++) clean &= !m.v[k].pw && !m.v[k].soft && !m.v[k].shape;
        CHECK(clean, "a patch of nothing: the HTML's voices again");
    }
    printf("patch_test: %s\n", fails ? "FAILED" : "all pass");
    return fails != 0;
}
