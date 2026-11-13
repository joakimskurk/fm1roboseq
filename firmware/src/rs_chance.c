/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Joakim (joakimskurk) */
/* seededChance() without float. The HTML:
 *     seed = (step + 1) * 12.9898 + (cycle + 1) * 78.233 + botCode * 31.7 + (repeat + 1) * 19.19
 *     chance = |sin(seed) * 43758.5453| % 1
 * Here the seed is an angle in turns, Q64: each term's constant / 2 pi (exact to 2^-64), multiplied and added with
 * uint64 wrapping, which is the reduction mod 2 pi for free and exact for any cycle. |sin| comes from Taylor series
 * in Q61 on the octant, ~1e-17; times 43758.5453 in 128 bits, the fraction is the chance in Q32. The HTML's own
 * doubles carry ~ulp(seed) * 43758 of noise (5e-9 at the thousandth loop), so a step whose chance lies that close to
 * 0.75 or 0.5 can land on the other side; tests/roboseq checks every step for thousands of loops (DECISIONS.md D6).
 * Called once per bot and step from the main loop; 64-bit multiplications only, no division. */
#include "rs_model.h"

#define RS_TURN_STEP 0x11408758AEC7768Cull     /* 12.9898 / 2 pi, of a turn, Q64 */
#define RS_TURN_CYCLE 0x737FCA1C2BC4DBA0ull    /* 78.233 / 2 pi */
#define RS_TURN_BOT 0x0B92FE63C5DB8E20ull      /* 31.7 / 2 pi */
#define RS_TURN_REP 0x0DDEF5E64FCD06D0ull      /* 19.19 / 2 pi */
#define PI_2_Q61 0x3243F6A8885A308Dull         /* pi / 2, Q61 */
#define K_Q32 0xAAEE8B98C7E3ull                /* 43758.5453, Q32 */

struct u128 { uint64_t hi, lo; };

static struct u128 mul64(uint64_t a, uint64_t b)      /* the full product, from 32 x 32 -> 64 parts */
{
    uint64_t al = (uint32_t)a, ah = a >> 32, bl = (uint32_t)b, bh = b >> 32;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = (ll >> 32) + (uint32_t)lh + (uint32_t)hl;
    struct u128 r;
    r.lo = (mid << 32) | (uint32_t)ll;
    r.hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    return r;
}

static uint64_t mulq61(uint64_t a, uint64_t b)        /* a * b >> 61, both < 2^62 */
{
    struct u128 p = mul64(a, b);
    return (p.hi << 3) | (p.lo >> 61);
}

/* 1 / n!, Q61, n = 2..17 */
static const uint64_t inv_fact[18] = {
    0, 0, 0x1000000000000000ull, 0x0555555555555555ull, 0x0155555555555555ull, 0x0044444444444444ull,
    0x000B60B60B60B60Bull, 0x0001A01A01A01A02ull, 0x0000340340340340ull, 0x000005C778E955B2ull,
    0x00000093F27DBBC5ull, 0x0000000D7322B3FBull, 0x000000011EED8F00ull, 0x0000000016124614ull,
    0x000000000193974Bull, 0x00000000001AE7F4ull, 0x000000000001AE7Full, 0x0000000000001953ull,
};

static uint64_t sin_q61(uint64_t x)                   /* sin x, x in [0, pi/4], Q61 */
{
    uint64_t x2 = mulq61(x, x), p = x, sum = x;
    for (int n = 3; n <= 15; n += 2) {
        p = mulq61(p, x2);
        uint64_t term = mulq61(p, inv_fact[n]);
        sum = ((n >> 1) & 1) ? sum - term : sum + term;
    }
    return sum;
}

static uint64_t cos_q61(uint64_t x)                   /* cos x, x in [0, pi/4], Q61 */
{
    uint64_t x2 = mulq61(x, x), p = (uint64_t)1 << 61, sum = p;
    for (int n = 2; n <= 16; n += 2) {
        p = mulq61(p, x2);
        uint64_t term = mulq61(p, inv_fact[n]);
        sum = ((n >> 1) & 1) ? sum - term : sum + term;
    }
    return sum;
}

static uint64_t abs_sin_turn(uint64_t u)              /* |sin(2 pi u / 2^64)|, Q61 */
{
    uint64_t q = u >> 62, f = u << 2;                 /* the quadrant, the place in it (Q64) */
    int odd = (int)(q & 1);                           /* |sin| on an odd quadrant is |cos| of the place in it */
    if (f >> 63) { f = ~f + 1; odd ^= 1; }            /* past its middle: the other function, from the far end */
    uint64_t x = mulq61(f >> 3, PI_2_Q61);            /* the place as radians: f (Q61) * pi / 2 -> Q61, <= pi / 4 */
    return odd ? cos_q61(x) : sin_q61(x);
}

uint32_t rs_chance_q32(int bot, uint32_t step, uint32_t cycle, uint32_t repeat)
{
    uint64_t u = (uint64_t)(step + 1) * RS_TURN_STEP + (uint64_t)(cycle + 1) * RS_TURN_CYCLE +
                 (uint64_t)(bot ? 'B' : 'A') * RS_TURN_BOT + (uint64_t)(repeat + 1) * RS_TURN_REP;
    struct u128 p = mul64(abs_sin_turn(u), K_Q32);    /* Q61 * Q32 = Q93: the chance is bits 92..61 */
    return (uint32_t)((p.hi << 3) | (p.lo >> 61));
}

int rs_skips(int bot, uint32_t step, uint32_t cycle, uint8_t prob)
{
    if (prob >= 100) return 0;                        /* chance < 1 always */
    /* the HTML's are 0.75 and 0.5, exact; another percentage to within 2^-32 (no division) */
    uint32_t limit = prob == 75 ? 0xC0000000u : prob == 50 ? 0x80000000u : (uint32_t)prob * 42949673u;
    return rs_chance_q32(bot, step, cycle, 0) > limit;
}
