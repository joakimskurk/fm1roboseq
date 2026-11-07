/* SPDX-License-Identifier: GPL-3.0-only */
/* RoboSeq's mix (rs_mix.h): buildAudio()'s buses, low-pass, hall reverb and limiter, in integers.
 *
 *   voices -> bus x level ---------------------------------------------+-> master x level -> limiter -> out L R
 *             bot B: x level -> low-pass ------------------------------+
 *             melody, percussion, FX: x 0.45 -> reverb, L and R -------+   (D9: the return to the master)
 *
 * In stereo as the HTML is: everything dry in the middle, the reverb's two channels apart (Tone's impulse is two
 * noises), the limiter on the louder of the two for both (Chromium's).
 *
 * The reverb is not Tone's convolution (53361 taps a sample) but what it sounds like: white noise under the same
 * exponential decay with the same energy, from four all-passes and an 8-line feedback delay network
 * (mix_tables.py). The limiter is Chromium's DynamicsCompressorKernel step for step (tools/tone-ref/compressor.py),
 * its curve and rates as tables. */
#include "rs_mix.h"
#include "rs_lead.c"

const uint8_t rs_voice_bus[RS_NVOICES] = {      /* where buildAudio() connects each voice */
    [RS_V_A_MAIN] = RS_BUS_MELODY, [RS_V_A_PING] = RS_BUS_MELODY,
    [RS_V_B_FM] = RS_BUS_BOTB, [RS_V_B_SUB] = RS_BUS_BOTB,
    [RS_V_BELT] = RS_BUS_PERC, [RS_V_ZAP] = RS_BUS_PERC, [RS_V_EX_THUMP] = RS_BUS_PERC,
    [RS_V_EX_NOISE] = RS_BUS_PERC, [RS_V_EX_CRACKLE] = RS_BUS_PERC,
    [RS_V_COLLISION] = RS_BUS_FX, [RS_V_ROTATOR] = RS_BUS_FX, [RS_V_PULSE] = RS_BUS_FX, [RS_V_COIN] = RS_BUS_FX,
    [RS_V_KICK] = RS_BUS_DRUMS, [RS_V_SNARE] = RS_BUS_DRUMS, [RS_V_HIHAT] = RS_BUS_DRUMS,
};
/* DEFAULT_MIXER: master .9, melody .9, bot B .8, percussion .55, FX .6, drums .7 (Q15); then the lead's */
static const int32_t DEFAULT_LEVEL[RS_NLEVEL] = {29491, 29491, 26214, 18022, 19661, 22938, 19661};   /* (lead .6: ours, ~17 dB under full scale alone) */
#define SEND 14746                              /* the reverb send, 0.45 */
#ifndef RS_POOL
#define RS_POOL                                 /* (the firmware puts the delay in the pool: roboseq.c) */
#endif
#define DLY_MAX 32768                           /* the lead's delay: up to 0.74 s a side */
static int16_t lead_dly[2][DLY_MAX] RS_POOL;    /* left, right: a ping-pong */
/* K2 (D26): the echo's intensity, m->echo Q15; at 16384 about D23's: heard 0.4, fed back 0.47 */
#define DLY_DAMP 9830                           /* the repeats darker: a one-pole at ~2.5 kHz, Q15 */
#define M_ONE (1 << 30)
#define M_NONE (-65536)                           /* max_att: none yet (Chromium's -1 dB) */

static inline int32_t mulq15(int32_t x, int32_t g) { return (int32_t)(((int64_t)x * g) >> 15); }
static inline int32_t mulq30(int32_t x, int32_t g) { return (int32_t)(((int64_t)x * g) >> 30); }
static inline int32_t lerp16(const int32_t *t, uint32_t i, uint32_t w)   /* t[i] + (t[i+1] - t[i]) w / 65536 */
{
    return t[i] + (int32_t)(((int64_t)(t[i + 1] - t[i]) * w) >> 16);
}

static uint32_t msb(uint32_t x)                 /* the top set bit (x > 0); no clz: no libgcc */
{
    uint32_t e = 0;
    if (x >> 16) e += 16, x >>= 16;
    if (x >> 8) e += 8, x >>= 8;
    if (x >> 4) e += 4, x >>= 4;
    if (x >> 2) e += 2, x >>= 2;
    if (x >> 1) e += 1;
    return e;
}

static uint32_t isqrt(uint32_t x)               /* floor(sqrt(x)): 16 rounds at most */
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) x -= r + b, r = (r >> 1) + b;
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

static int32_t log2_q16(uint32_t v)             /* log2(v), Q16 (v > 0) */
{
    uint32_t e = msb(v), x = v << (31 - e);         /* the top bit at bit 31, the ones under it next */
    uint32_t i = (x >> 25) & 63u, w = (x >> 9) & 0xFFFFu;
    return (int32_t)(e << 16) + lerp16(LOG2_FRAC, i, w);
}

void rs_mix_init(struct rs_mix *m, uint32_t seed)
{
    uint8_t *p = (uint8_t *)m;
    for (unsigned i = 0; i < sizeof *m; i++) p[i] = 0;
    for (uint32_t k = 0; k < RS_NVOICES; k++) rs_voice_init(&m->v[k], (uint8_t)k, seed + k * 0x9E3779B9u);
    for (uint32_t k = 0; k < RS_NLEVEL; k++) m->level[k] = m->now_level[k] = DEFAULT_LEVEL[k];
    m->det = 0;                                 /* Chromium's Reset(): the detector at 0, the gain at 1 */
    m->gain = M_ONE;
    m->max_att = M_NONE;
    rs_lead_init(&m->lead);
    m->dly_len = m->dly_target = 24806;         /* three 16ths at 80 BPM */
    for (uint32_t i = 0; i < 8; i++) m->rev_k[i] = REV_KT[8 + i];   /* the HTML's room, no colour */
    m->send_t = m->send_n = SEND;
    m->echo = 16384;
    for (uint32_t i = 0; i < DLY_MAX; i++) lead_dly[0][i] = lead_dly[1][i] = 0;
}

static void lead_delay(struct rs_mix *m, int32_t in, int32_t *l, int32_t *r)   /* the lead's echo, a stereo ping-pong */
{
    uint32_t rd = (m->dly_w - (uint32_t)m->dly_len) & (DLY_MAX - 1);
    int32_t yl = lead_dly[0][rd], yr = lead_dly[1][rd];
    m->dly_lp += mulq15(yr - m->dly_lp, DLY_DAMP);  /* what comes back round, darker */
    int32_t e = m->echo, fb = 8192 + mulq15(e, 14746), wet = mulq15(e, 26214);   /* fed back 0.25 .. 0.7, heard 0 .. 0.8 */
    int32_t w = in + mulq15(m->dly_lp, fb);
    lead_dly[0][m->dly_w] = (int16_t)(w > 32767 ? 32767 : w < -32768 ? -32768 : w);
    lead_dly[1][m->dly_w] = (int16_t)yl;            /* the left's repeat goes on to the right */
    m->dly_w = (m->dly_w + 1) & (DLY_MAX - 1);
    if ((m->dly_w & 7u) == 0 && m->dly_len != m->dly_target)   /* a new tempo: it glides there, as tape */
        m->dly_len += m->dly_target > m->dly_len ? 1 : -1;
    *l = mulq15(yl, wet), *r = mulq15(yr, wet);
}

void rs_mix_colour(struct rs_mix *m, const struct rs_board *bd, uint32_t amount)   /* the board's colour (D24) */
{
    int32_t n[7] = {0}, a = amount > 100u ? 100 : (int32_t)amount, i;
    for (i = 0; i < RS_ROWS * RS_COLS; i++) n[bd->t[i].type % 7u]++;
    int32_t rot = n[RS_T_ROTL] + n[RS_T_ROTR], nb = n[RS_T_BELT], nz = n[RS_T_ZAP], np = n[RS_T_PULSE];
    int32_t nk = n[RS_T_BLOCK], ne = n[RS_T_NONE];
    /* the room, -1 .. 1 (x 1000): an open board a bigger room, crates a smaller one; the default board's is 0 */
    int32_t room = (ne - 13) * 250 - (nk - 1) * 333;
    room = (room < -1000 ? -1000 : room > 1000 ? 1000 : room) * a / 100;
    for (i = 0; i < 8; i++) {                   /* between the room 0.75, 1 and 1.35 times as long */
        int32_t k0 = REV_KT[8 + i], k1 = room < 0 ? REV_KT[i] : REV_KT[16 + i], w = room < 0 ? -room : room;
        m->rev_k[i] = k0 + (int32_t)(((int64_t)(k1 - k0) * w) / 1000);   /* (main loop: a division is fine) */
    }
    /* the send: pulse pads cool it (more room), zap rails dry it; the tilt: zaps and belts brighter, pulses warmer;
     * the drive: zaps and rotators. Each times the amount: 0 % is the HTML's sound */
    int32_t send = 1000 + 120 * (np - nz), tilt = 60 * nz + 30 * nb - 60 * np, drive = 40 * nz + 20 * rot;
    send = send < 500 ? 500 : send > 1500 ? 1500 : send;
    tilt = tilt < -350 ? -350 : tilt > 350 ? 350 : tilt;
    drive = drive > 400 ? 400 : drive;
    m->send_t = SEND + (SEND * (send - 1000) / 1000) * a / 100;
    m->tilt_t = 32768 * tilt / 1000 * a / 100;
    m->drive_t = 32768 * drive / 1000 * a / 100;
}

void rs_mix_patch(struct rs_mix *m, const struct rs_patch *p)   /* the board's patch (D36a) */
{
    static const uint8_t V[2] = {RS_V_A_MAIN, RS_V_B_FM};
    for (int i = 0; i < 2; i++) {
        m->pw_base[i] = (uint32_t)p->pw[i] << 16, m->pw_swing[i] = (uint32_t)p->pwm[i] << 16;
        m->v[V[i]].soft = p->soft[i], m->v[V[i]].shape = p->env[i];
        m->v[V[i] + 1].shape = i ? p->env[2] : p->env[0];   /* (Scout-7's ping as its main, the bass its own) */
        if (!p->pwm[i]) m->v[V[i]].pw = m->pw_base[i];
    }
    m->lfo_inc = (uint32_t)p->rate * 3117u;             /* mHz -> a block's step: 2^32 x 32 / 44100000 */
}

void rs_mix_note(struct rs_mix *m, uint8_t voice, uint32_t at, uint32_t dur, int midi, uint16_t vel)
{
    if (voice < RS_NVOICES) rs_voice_note(&m->v[voice], at, dur, midi, vel);
}

/* ---- the reverb: a frame, Q15 in, Q24 out. Unrolled, its multiplies the high word of a 64-bit product (one
 * instruction on the chip) */
static inline int32_t mulh(int32_t x, int32_t k) { return (int32_t)(((int64_t)x * k) >> 32); }
#define AP(k)                                                                  \
    do {                                                                       \
        int32_t *c = &m->ap[AP_O##k + m->ap_i[k]], v = *c, w = x + v - mulh(v, AP_G4);   \
        *c = w;                                                                \
        x = v - (w - mulh(w, AP_G4));                                          \
        if (++m->ap_i[k] == AP_L##k) m->ap_i[k] = 0;                           \
    } while (0)
#define TAP(i) int32_t r##i = (int32_t)m->line[REV_O##i + m->line_i[i]] * 256
#define FEED(i, h)                                                             \
    do {                                                                       \
        int32_t v = (mulh(h, m->rev_k[i]) + x + 128) >> 8;                     \
        if (v > 32767) v = 32767, m->clips++;                                  \
        else if (v < -32768) v = -32768, m->clips++;                           \
        m->line[REV_O##i + m->line_i[i]] = (int16_t)v;                         \
        if (++m->line_i[i] == REV_L##i) m->line_i[i] = 0;                      \
    } while (0)

static int32_t reverb(struct rs_mix *m, int32_t send, int32_t *right)   /* the left out; the right into *right */
{
    int32_t s = (int32_t)(((int64_t)send * REV_IN) >> 15);                  /* Q24 */
    int32_t x = m->pre[m->pre_i];
    m->pre[m->pre_i] = s;
    if (++m->pre_i == REV_PRE) m->pre_i = 0;
    AP(0); AP(1); AP(2);                                                    /* the diffusers */
    int32_t xr = x;                                                         /* the right: its own last all-pass */
    {
        int32_t *c = &m->ap_r[m->ap_ri], v = *c, w = xr + v - mulh(v, AP_G4);
        *c = w;
        xr = v - (w - mulh(w, AP_G4));
        if (++m->ap_ri == AP_LR) m->ap_ri = 0;
        int32_t late = m->dr[m->dr_i];
        m->dr[m->dr_i] = xr, xr = late;
        if (++m->dr_i == REV_DR) m->dr_i = 0;
    }
    AP(3);
    TAP(0); TAP(1); TAP(2); TAP(3); TAP(4); TAP(5); TAP(6); TAP(7);
    int32_t y = REV_TAPS + mulq15(x, REV_DIRECT);
    *right = REV_TAPS_R + mulq15(xr, REV_DIRECT);  /* Tone's reverb is two noises: the right taps the lines otherwise */
    /* the Walsh-Hadamard transform of the eight */
    int32_t a0 = r0 + r1, a1 = r0 - r1, a2 = r2 + r3, a3 = r2 - r3, a4 = r4 + r5, a5 = r4 - r5, a6 = r6 + r7, a7 = r6 - r7;
    int32_t b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3, b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;
    FEED(0, b0 + b4); FEED(1, b1 + b5); FEED(2, b2 + b6); FEED(3, b3 + b7);
    FEED(4, b0 - b4); FEED(5, b1 - b5); FEED(6, b2 - b6); FEED(7, b3 - b7);
    return y;
}

/* ---- the limiter: Chromium's per-division part (every 32 frames), then a frame */
static void lim_division(struct rs_mix *m)
{
    uint32_t u = (uint32_t)(M_ONE - m->det), s = isqrt(u);                /* Q15: sqrt(1 - detector) */
    int32_t scaled = s >= 32768u ? LIM_ASIN[256] : lerp16(LIM_ASIN, s >> 7, (s & 127u) << 9);
    int32_t diff;                                                       /* 1/65536 dB */
    int finite = 1;
    m->scaled = scaled;
    m->releasing = scaled > m->gain;
    if (scaled <= 0) finite = 0;
    else if (m->gain <= 0) diff = -1000 * 65536;
    else diff = (int32_t)(((int64_t)(log2_q16((uint32_t)m->gain) - log2_q16((uint32_t)scaled)) * 394566) >> 16);
    if (m->releasing) {
        m->max_att = M_NONE;
        if (!finite) diff = -65536;
        if (diff < -12 * 65536) diff = -12 * 65536;
        if (diff > 0) diff = 0;
        uint32_t pos = (uint32_t)(diff + 12 * 65536) * 8u;
        m->rate = (pos >> 16) >= 96u ? LIM_REL[96] : lerp16(LIM_REL, pos >> 16, pos & 0xFFFFu);
    } else {
        if (!finite) diff = 65536;
        if (m->max_att == M_NONE || m->max_att < diff) m->max_att = diff;
        int32_t eff = m->max_att < 32768 ? 32768 : m->max_att;
        uint32_t pos = (uint32_t)(eff - 32768) * 8u;
        m->rate = (pos >> 16) >= 255u ? LIM_ATK[255] : lerp16(LIM_ATK, pos >> 16, pos & 0xFFFFu);
    }
}

static void limiter(struct rs_mix *m, int32_t x, int32_t xr, int32_t *out)   /* a stereo frame in, out[0], out[1] */
{
    m->la[(m->la_i + LIM_PRE) & (RS_LIM_BUF - 1)] = x;
    m->la_r[(m->la_i + LIM_PRE) & (RS_LIM_BUF - 1)] = xr;
    int32_t xd = m->la[m->la_i], xdr = m->la_r[m->la_i];
    m->la_i = (uint16_t)((m->la_i + 1) & (RS_LIM_BUF - 1));
    uint32_t a = (uint32_t)(x < 0 ? -x : x), ar = (uint32_t)(xr < 0 ? -xr : xr);
    if (ar > a) a = ar;                                 /* Chromium: the loudest channel drives the gain of both */
    int32_t att, satr;
    if (a < LIM_THRESH) att = M_ONE, satr = LIM_R0;
    else {
        uint32_t e = msb(a), i, w;
        if (e >= 20) i = 384, w = 0;
        else i = (e - 14) * 64 + ((a >> (e - 6)) & 63u), w = (a & ((1u << (e - 6)) - 1u)) << (22 - e);
        att = i >= 384 ? LIM_ATT[384] : lerp16(LIM_ATT, i, w);
        satr = i >= 384 ? LIM_SATR[384] : lerp16(LIM_SATR, i, w);
    }
    if (att > m->det) m->det += mulq30(att - m->det, satr);
    else m->det = att;
    if (m->det > M_ONE) m->det = M_ONE;
    if (m->releasing) {
        m->gain = mulq30(m->gain, m->rate);
        if (m->gain > M_ONE) m->gain = M_ONE;
    } else
        m->gain += mulq30(m->scaled - m->gain, m->rate);
    uint32_t g = (uint32_t)(m->gain < 0 ? 0 : m->gain);
    int32_t warp = (g >> 22) >= 256u ? LIM_SIN[256] : lerp16(LIM_SIN, g >> 22, (g >> 6) & 0xFFFFu), tg = mulq30(LIM_MAKEUP, warp);
    out[0] = (int32_t)(((int64_t)xd * tg) >> 30);
    out[1] = (int32_t)(((int64_t)xdr * tg) >> 30);
}

void rs_mix_render(struct rs_mix *m, int32_t *out, uint32_t n, uint32_t now)
{
    int32_t lv[RS_NLEVEL];
    for (uint32_t f0 = 0; f0 < n; f0 += RS_MIX_SUB, now += RS_MIX_SUB) {
        for (uint32_t k = 0; k < RS_NLEVEL; k++) {      /* a level glides to a new one, a sixth of the way each 32 frames */
            int32_t d = m->level[k] - m->now_level[k];  /* (~20 ms, as the HTML's rampTo 0.05) */
            m->now_level[k] += d / 6 ? d / 6 : d;
            lv[k] = m->now_level[k];
        }
        for (uint32_t b = 0; b < RS_NBUS; b++)
            for (uint32_t i = 0; i < RS_MIX_SUB; i++) m->bus[b][i] = 0;
        if (m->pw_swing[0] | m->pw_swing[1]) {          /* the pulses swing (D36a): a triangle, Echo-3's the other way */
            m->lfo += m->lfo_inc;
            int32_t tri = (int32_t)(m->lfo >> 16), w = (tri < 32768 ? tri : 65535 - tri) * 2 - 32768;   /* Q15 */
            m->v[RS_V_A_MAIN].pw = m->pw_base[0] + (uint32_t)(((int64_t)(int32_t)(m->pw_swing[0] >> 1) * w) >> 14);
            m->v[RS_V_B_FM].pw = m->pw_base[1] - (uint32_t)(((int64_t)(int32_t)(m->pw_swing[1] >> 1) * w) >> 14);
        }
        for (uint32_t k = 0; k < RS_NVOICES; k++)
            rs_voice_render(&m->v[k], m->bus[rs_voice_bus[k]], RS_MIX_SUB, now);
        for (uint32_t i = 0; i < RS_MIX_SUB; i++) m->lead_bus[i] = 0;
        rs_lead_render(&m->lead, m->lead_bus, RS_MIX_SUB, now);
        lim_division(m);
        {                                               /* the board's colour glides to its new one (D24) */
            int32_t d = m->send_t - m->send_n;
            m->send_n += d / 8 ? d / 8 : d;
            d = m->tilt_t - m->tilt_n, m->tilt_n += d / 8 ? d / 8 : d;
            d = m->drive_t - m->drive_n, m->drive_n += d / 8 ? d / 8 : d;
        }
        int32_t drive3 = m->drive_n / 3;                /* (the drive's d / 3, once a block: no 64-bit division) */
        for (uint32_t i = 0; i < RS_MIX_SUB; i++) {
            int32_t mel = mulq15(m->bus[RS_BUS_MELODY][i], lv[RS_L_MELODY]);
            int32_t perc = mulq15(m->bus[RS_BUS_PERC][i], lv[RS_L_PERC]);
            int32_t fx = mulq15(m->bus[RS_BUS_FX][i], lv[RS_L_FX]);
            int32_t dr = mulq15(m->bus[RS_BUS_DRUMS][i], lv[RS_L_DRUMS]);
            int32_t bx = mulq15(m->bus[RS_BUS_BOTB][i], lv[RS_L_BOTB]);
            int64_t acc = ((int64_t)LP_B0 * (bx + m->lp_x2) + (int64_t)LP_B1 * m->lp_x1) * 256
                        - (int64_t)LP_A1 * m->lp_y1 - (int64_t)LP_A2 * m->lp_y2;
            int32_t y = (int32_t)(acc >> 30);                           /* Q23 */
            m->lp_x2 = m->lp_x1, m->lp_x1 = bx, m->lp_y2 = m->lp_y1, m->lp_y1 = y;
            int32_t ld = mulq15(m->lead_bus[i], lv[RS_L_LEAD]);   /* the lead: to the reverb as the melody, and out */
            int32_t el = 0, er = 0;
            if (ld) m->dly_hold = 8u * RS_SR;  /* the echoes ring out for 8 s after the lead's last sound, then rest */
            if (m->dly_hold) m->dly_hold--, lead_delay(m, ld, &el, &er);
            int32_t rev_r, rev = (reverb(m, mulq15(mel + perc + fx + ld, m->send_n), &rev_r) + 256) >> 9;
            int32_t dry = mel + ((y + 128) >> 8) + perc + fx + dr + ld;
            int32_t mo[2] = {mulq15(dry + el + rev, lv[RS_L_MASTER]), mulq15(dry + er + ((rev_r + 256) >> 9), lv[RS_L_MASTER])};
            if (m->tilt_n || m->drive_n)                /* the board's colour (D24): a tilt, then a soft drive */
                for (uint32_t c = 0; c < 2; c++) {
                    int32_t x = mo[c];
                    m->tlp[c] += mulq15(x - m->tlp[c], 4357);   /* a one-pole at ~1 kHz: above it is tilted */
                    x += mulq15(x - m->tlp[c], m->tilt_n);
                    if (m->drive_n) {                   /* x - d x^3 / 3, within +-1.3 of full scale */
                        int32_t xc = x > 42598 ? 42598 : x < -42598 ? -42598 : x;
                        int32_t x3 = (int32_t)(((((int64_t)xc * xc) >> 15) * xc) >> 15);
                        x = xc - (int32_t)(((int64_t)x3 * drive3) >> 15);
                    }
                    mo[c] = x;
                }
            limiter(m, mo[0], mo[1], out + 2u * (f0 + i));
        }
    }
}
