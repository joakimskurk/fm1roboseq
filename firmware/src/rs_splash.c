/* SPDX-License-Identifier: GPL-3.0-only */
/* RoboSeq's splash at boot, after the product owner's draft (2026-11-06), animated: the letters drop in one by one,
 * the star over them twinkles, the robots come in from the sides to meet in the middle and bob, a heart beats
 * over them, the dots fill as it starts. About 2.6 s; a key or a button skips it. Main loop (rs_screen.c's canvas);
 * the art from tools/art (splash.py: the lettering; robots.py: the robots, drawn twice the size). */
#include "rs_splash_art.h"

#define SPLASH_MS 2600u

static int32_t ease_out(int32_t t, int32_t len, int32_t from, int32_t to)   /* from -> to over len ms, slowing */
{
    if (t <= 0) return from;
    if (t >= len) return to;
    int32_t u = len - t;                        /* (1 - p)^2, in len^2 */
    return to + (int32_t)((int64_t)(from - to) * u * u / ((int64_t)len * len));
}

static void big_bot(int32_t x, int32_t y, int b)   /* a robot twice the size: the 32 x 32 art, 2 x 2 a pixel */
{
    uint16_t main = b ? C_BOT_B : C_BOT_A, dark = b ? C_BOT_B2 : C_BOT_A2, acc = b ? C_WARN : C_ACCENT;
    int32_t gx, gy;
    for (gy = 0; gy < 32; gy++)
        for (gx = 0; gx < 32; gx++) {
            uint16_t c;
            switch ((b ? ART_BOT_B : ART_BOT_A)[gy][gx]) {
            case 'o': c = RGB(0x05, 0x05, 0x0c); break;
            case 'm': c = main; break;
            case 'M': c = mix565(C_WHITE, main, 90); break;
            case 'n': c = mix565(main, dark, 128); break;
            case 'd': c = dark; break;
            case 'a': c = acc; break;
            case 'v': c = RGB(0x0a, 0x0a, 0x1a); break;
            case 'w': c = C_WHITE; break;
            case 't': c = mix565(acc, C_BG, 154); break;
            default: continue;
            }
            cv_rect(x + 2 * gx, y + 2 * gy, 2, 2, c);
        }
}

static void corner(int32_t x, int32_t y, int32_t dx, int32_t dy, uint16_t c)   /* an L at a corner, its arms dx, dy */
{
    cv_rect(dx > 0 ? x : x - 9, y, 10, 2, c);
    cv_rect(x, dy > 0 ? y : y - 9, 2, 10, c);
}

static void draw_splash(uint32_t t)
{
    uint16_t bg = RGB(0x0b, 0x23, 0x24), grid = RGB(0x10, 0x2f, 0x2f), line = RGB(0x2f, 0x6a, 0x63);
    int32_t i, k, y;
    cv_begin(240, 240, bg);
    for (i = 6; i < 240; i += 12) cv_rect(i, 6, 1, 228, grid), cv_rect(6, i, 228, 1, grid);   /* a faint grid */
    frame(5, 5, 230, 230, 1, line);
    corner(3, 3, 1, 1, C_WARN), corner(236, 3, -1, 1, C_DANGER), corner(3, 236, 1, -1, C_WARN), corner(236, 236, -1, -1, C_DANGER);
    /* the star: from 300 ms, its rays long and short in turn */
    if (t > 300u) draw_clash(110, 14, 2);
    if (t > 300u && ((t / 180u) & 1u))          /* (its long rays, every other beat) */
        cv_rect(119, 9, 2, 4, C_WHITE), cv_rect(119, 35, 2, 4, C_WHITE), cv_rect(105, 23, 4, 2, C_WHITE), cv_rect(131, 23, 4, 2, C_WHITE);
    /* the letters: each drops in 80 ms after the last, lit on top, a shadow under */
    for (i = 0; i < 7; i++) {
        int32_t lx = 37 + i * 24, ly = ease_out((int32_t)t - 80 * i, 260, -40, 48);
        for (y = 0; y < 11; y++)
            for (k = 0; k < 7; k++) {
                char ch = LOGO[i][y][k];
                if (ch == '.') continue;
                cv_rect(lx + 3 * k + 3, ly + 3 * y + 3, 3, 3, RGB(0x05, 0x14, 0x14));
            }
        for (y = 0; y < 11; y++)
            for (k = 0; k < 7; k++) {
                char ch = LOGO[i][y][k];
                if (ch == '.') continue;
                if (i < 4)                      /* "Robo" mint, "Seq" yellow */
                    cv_rect(lx + 3 * k, ly + 3 * y, 3, 3, ch == 'h' ? RGB(0xd6, 0xff, 0xf0) : ch == 'n' ? RGB(0x3c, 0xb8, 0x96) : C_ACCENT);
                else
                    cv_rect(lx + 3 * k, ly + 3 * y, 3, 3, ch == 'h' ? RGB(0xff, 0xf0, 0xb0) : ch == 'n' ? RGB(0xc9, 0x9a, 0x00) : C_WARN);
            }
    }
    /* the floor */
    cv_rect(14, 196, 212, 2, line);
    for (k = 0; k < 9; k++) cv_rect(20 + k * 24, 202, 12, 1, grid), cv_rect(26 + k * 24, 207, 8, 1, grid);
    /* the robots: in from the sides to meet in the middle, then bobbing to a beat, each the other way; a heart
     * over them once they are together, beating */
    {
        int32_t xa = ease_out((int32_t)t - 400, 600, -70, 50), xb = ease_out((int32_t)t - 400, 600, 240, 126);
        int32_t bob = (int32_t)((t / 190u) & 1u) * 2;
        big_bot(xa, 132 - bob, 0);
        big_bot(xb, 130 + bob, 1);
        if (t > 1000u) {
            int beat = (t - 1000u) % 600u < 120u;   /* a beat every 0.6 s: a little bigger */
            draw_heart(120 - 15, 104, 2, beat);  /* 30 x 26, in the middle over them; on the beat a glow round it */
        }
    }
    /* the dots: as it starts */
    for (k = 0; k < 6; k++) {
        int lit = (int32_t)t * 7 / (int32_t)SPLASH_MS > k;
        cv_rect(85 + k * 12, 216, 8, 8, lit ? C_ACCENT : RGB(0x1a, 0x3a, 0x38));
    }
    screen_out();
}
