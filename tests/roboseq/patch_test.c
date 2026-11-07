/* SPDX-License-Identifier: GPL-3.0-only */
/* The board's touch on the bots' synths (rs_plus_patch, rs_mix_patch, D36a): a little, each board its own, an empty
 * board and CLASSIC the HTML's voices exactly; the patched pulse in range and without a DC. patch_test: exit 0 */
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
