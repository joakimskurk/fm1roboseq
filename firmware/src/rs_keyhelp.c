/* SPDX-License-Identifier: GPL-3.0-only */
/* What the keys and knobs do, on the FM-1 itself (D46): its keys have no lights, so the screen says it, two ways.
 *   the key map   while a page's own button is held (HOME on HOME, SEQ on SEQ ...), the screen shows the keys in
 *                 colours, a line a colour saying what those keys do, and the knobs; let go, the page again
 *   a key's name  each key pressed says in the header, for a second, what it did ("D5: ROLL SCOUT-7")
 * Main loop only. Included in roboseq.c after the pages, the keys (keys_init) and the app's state it reads. */

static uint8_t help_on;                         /* the key map on the screen */
static const char *const KH_NOTE[12] = {"F", "F#", "G", "G#", "A", "A#", "B", "C", "C#", "D", "D#", "E"};
static const char *const KH_ACT[RS_NACTIONS] = {"MOVE", "BACK", "TURN L", "TURN R", "DASH", "PING", "WAIT"};
static const char *const KH_MOD[RS_NMODIFIERS] = {"STEADY", "BRIGHT", "LOW", "WOBBLE", "GLITCH"};
static const char *const KH_NUDGE[5] = {"TURN L", "MOVE", "TURN R", "DASH", "PING"};

static uint32_t kh_w(uint32_t from, uint32_t to)   /* the white keys from .. to (1 = F3, 16 = G5): their bits */
{
    uint32_t m = 0;
    for (uint32_t i = from; i <= to && i <= 16u; i++) m |= 1u << white_key[i - 1u];
    return m;
}
static uint32_t kh_b(uint32_t from, uint32_t to)   /* the black keys from .. to (1 = F#3, 11 = F#5) */
{
    uint32_t m = 0;
    for (uint32_t i = from; i <= to && i <= 11u; i++) m |= 1u << black_key[i - 1u];
    return m;
}

static int kh_page_button(void)                 /* the button of the page on (the one that shows its map), -1 none */
{
    switch (page) {
    case PAGE_HOME: return BTN_HOME;
    case PAGE_SEQ: return BTN_SEQ;
    case PAGE_GLO: return BTN_GLO;
    case PAGE_SCL: case PAGE_ARP: return BTN_SEL;
    case PAGE_LFO: return BTN_LFO;
    case PAGE_FX: return BTN_FX;
    case PAGE_ENV: case PAGE_BOARDS: return BTN_ENV;
    default: return -1;                         /* (the battle shows its keys itself) */
    }
}

static int keyhelp_poll(void)                   /* the main loop's, a pass: the map on while the page's button is held
                                                   400 ms, off when it is let go; 1 when the screen changes */
{
    static uint32_t since;
    int own = kh_page_button(), held = own >= 0 && (fm1_in.buttons >> own & 1u);
    if (!held) {
        since = 0;
        if (help_on) { help_on = 0; return 1; }
        return 0;
    }
    if (!since) since = fm1_ms | 1u;
    else if (!help_on && fm1_ms - since >= 400u) { help_on = 1; return 1; }
    return 0;
}

/* ---- the key map: a line a colour, the keys of that colour */
struct kh_line { uint32_t keys; uint16_t c; const char *t; };

static void kh_nudges(struct kh_line *L, uint32_t *n)
{
    L[(*n)++] = (struct kh_line){kh_b(1, 5), C_BOT_A, "F#3-D#4: NUDGE SCOUT-7"};
    L[(*n)++] = (struct kh_line){kh_b(6, 10), C_BOT_B, "F#4-D#5: NUDGE ECHO-3"};
    L[(*n)++] = (struct kh_line){kh_b(11, 11), C_BELT, "F#5: SWAP THE PROGRAMS"};
}

static uint32_t kh_lines(struct kh_line *L, const char **title, const char **k1, const char **k2)
{
    uint32_t n = 0;
    *k2 = "";
    if (lead_on) {
        *title = arp_mode ? "ARP: WHAT THE KEYS DO" : "REC: WHAT THE KEYS DO";
        L[n++] = (struct kh_line){0x7FFFFFFu, C_ACCENT, arp_mode ? "HOLD A CHORD: AN ARPEGGIO" : "EVERY KEY: THE LEAD"};
        L[n++] = (struct kh_line){0, C_MUTED, "REC AGAIN: THE KEYS BACK"};
        *k1 = "K1 CUT  K2 ECHO  K3 BITE";
        *k2 = arp_mode ? "K4 PATTERN  SELECT SPEED" : "K4 + OCT-/OCT+: OCTAVE";
        return n;
    }
    switch (page) {
    case PAGE_HOME:
        *title = "HOME: WHAT THE KEYS DO";
        L[n++] = (struct kh_line){kh_w(1, 16), C_ACCENT, sel_bot ? "WHITE: ECHO-3'S STEPS" : "WHITE: SCOUT-7'S STEPS"};
        kh_nudges(L, &n);
        *k1 = "K1 LENGTH K2 ROLL K3 BOTH";
        *k2 = "SELECT: MOVE/MOD/CHANCE";
        break;
    case PAGE_SEQ:
        *title = "SEQ: WHAT THE KEYS DO";
        L[n++] = (struct kh_line){0x7Fu, C_PULSE, "F3-B3: TYPE MOVES (WASD)"};
        L[n++] = (struct kh_line){1u << 7 | 1u << 9 | 1u << 11 | 1u << 12 | 1u << 14, C_ACCENT, "C4-G4: THE MODIFIER"};
        L[n++] = (struct kh_line){1u << 16 | 1u << 18 | 1u << 19, C_BELT, "A4-C5: CHANCE 100 75 50"};
        L[n++] = (struct kh_line){1u << SK_UNDO, C_TEXT, "C#5: UNDO"};
        L[n++] = (struct kh_line){1u << SK_RND | 1u << SK_RNDALL, C_WARN, "D5 ROLL  E5 ROLL BOTH"};
        L[n++] = (struct kh_line){1u << SK_SWAP | 1u << SK_CLEAR, C_DANGER, "F5 SWAP  G5 CLEAR"};
        *k1 = "K1 LENGTH  SELECT CURSOR";
        *k2 = "OCT-/OCT+: 8 STEPS";
        break;
    case PAGE_GLO:
        *title = "GLO: WHAT THE KEYS DO";
        L[n++] = (struct kh_line){kh_w(16, 16), C_WARN, "G5: THE BATTLE"};
        kh_nudges(L, &n);
        *k1 = "K1 TEMPO  K2 TICKS";
        *k2 = "K3 LOOP K4 SPEED SEL ENGINE";
        break;
    case PAGE_SCL:
    case PAGE_ARP:
        *title = page == PAGE_SCL ? "VOICE MOD: THE KEYS" : "SEQ 2: THE KEYS";
        L[n++] = (struct kh_line){kh_w(1, 4), C_ACCENT, "WHITE 1-4: PICK A STEP"};
        L[n++] = (struct kh_line){kh_w(12, 16), C_WARN, page == PAGE_SCL ? "12 FOLLOW 13 NEXT 14 ROOT" : "12 ON 13 ROLL A 14 ROLL B"};
        L[n++] = (struct kh_line){0, C_WARN, "15 RESET  16 RANDOM"};
        kh_nudges(L, &n);
        *k1 = page == PAGE_SCL ? "K1 SCALE K2 ROOT K3 SHIFT" : "K1 BOARD  K2 DRUMS";
        *k2 = page == PAGE_SCL ? "K4 WHEN IT MOVES ON" : "K3 HIGH/LOW  K4 CHAIN";
        break;
    case PAGE_LFO:
        *title = "DRUMS: WHAT THE KEYS DO";
        L[n++] = (struct kh_line){kh_w(1, rs_drum_steps(game.loop_len)), C_ACCENT,
                                  drum_row == 1 ? "WHITE: THE SNARE'S STEPS" : drum_row == 2 ? "WHITE: THE HIHAT'S STEPS" : "WHITE: THE KICK'S STEPS"};
        kh_nudges(L, &n);
        *k1 = "SELECT: KICK SNARE HIHAT";
        *k2 = "K1 PATTERN  K4 ON/OFF";
        break;
    case PAGE_FX:
        *title = "MIX: WHAT THE KEYS DO";
        L[n++] = (struct kh_line){kh_w(1, 7), C_ACCENT, "WHITE 1-7: PICK A FADER"};
        kh_nudges(L, &n);
        *k1 = "K1 LEVEL";
        *k2 = "K2 HOW THE BOARD COLOURS";
        break;
    case PAGE_BOARDS:
        *title = "BOARDS: THE KEYS";
        L[n++] = (struct kh_line){kh_w(1, 6), C_ROT, "WHITE 1-6: THE BOARDS"};
        L[n++] = (struct kh_line){kh_w(7, 11), C_ACCENT, "7 ROLL 8 YOURS 9 SAVE"};
        L[n++] = (struct kh_line){0, C_ACCENT, "10 CLEAR  11 TO THE PADS"};
        kh_nudges(L, &n);
        *k1 = "K1 PRESET  K2 FULLNESS";
        *k2 = "ENV AGAIN: THE BUILDER";
        break;
    case PAGE_ENV:
        *title = "BUILDER: THE KEYS";
        L[n++] = (struct kh_line){1u << 0 | 1u << 1 | 1u << 2 | 1u << 4, C_PULSE, "F3 F#3 G3 A3: CURSOR"};
        L[n++] = (struct kh_line){1u << EK_ERASE, C_DANGER, "G#3: ERASE THE TILE"};
        L[n++] = (struct kh_line){1u << 7 | 1u << 9 | 1u << 11 | 1u << 12 | 1u << 14 | 1u << 16, C_ROT, "C4-A4: LAY A TILE"};
        L[n++] = (struct kh_line){1u << EK_ROLL | 1u << EK_USER | 1u << EK_SAVE | 1u << EK_CLEAR | 1u << EK_RESET, C_ACCENT,
                                  "C5 ROLL D5 YOURS E5 SAVE"};
        L[n++] = (struct kh_line){0, C_ACCENT, "F5 CLEAR  G5 TO THE PADS"};
        *k1 = "K1 PRESET  K2 FULLNESS";
        break;
    default:
        *title = "";
        *k1 = "";
    }
    return n;
}

static void draw_keyhelp(void)                  /* the key map, the whole screen */
{
    struct kh_line L[8];
    const char *title, *k1, *k2;
    uint32_t n = kh_lines(L, &title, &k1, &k2), i, k;
    uint16_t col[27];
    int32_t y = 46;
    for (k = 0; k < 27u; k++) col[k] = 0;
    for (i = 0; i < n; i++)
        for (k = 0; k < 27u; k++)
            if (L[i].keys >> k & 1u) col[k] = L[i].c;
    cv_begin(240, 240, C_BG);
    cv_text(4, 0, &FONT_S, title, C_ACCENT);
    cv_rect(0, 16, 240, 1, mix565(C_ACCENT, C_BG, 90));
    draw_keymap(22, col);
    for (i = 0; i < n; i++, y += 18) {          /* a line a colour (a line with no keys of its own: the one above's) */
        if (L[i].keys) cv_rect(4, y + 4, 8, 8, L[i].c);
        cv_text(16, y, &FONT_S, L[i].t, C_TEXT);
    }
    cv_rect(0, 197, 240, 1, mix565(C_MUTED, C_BG, 90));
    cv_text(4, 202, &FONT_S, k1, C_WARN);
    cv_text(4, 220, &FONT_S, k2, C_WARN);
    badge_steer[0] = badge_steer[1] = 0, badge_swap = 0, lead_badge = -99, toast_until = 0;   /* (the map alone) */
    screen_out();
}

/* ---- a key's name, in the header for a second after it is pressed */
static void kh_put(char **p, const char *s) { while (*s && *p < toast + sizeof toast - 1) *(*p)++ = *s++; **p = 0; }

static void keyhelp_say(uint32_t down)          /* the first key pressed now: its name and what it did */
{
    uint32_t k, i;
    char num[4], *p = toast;
    const char *w = 0, *w2 = 0;
    if (!down || page == PAGE_BATTLE || lead_on) return;   /* (the battle draws its keys; the lead's keys are notes) */
    for (k = 0; !(down >> k & 1u); k++) {}
    int black = (0x54Au >> ((53u + k) % 12u)) & 1u, wi = -1, bi = -1;
    for (i = 0; i < 16u; i++) if (white_key[i] == k) wi = (int)i;
    for (i = 0; i < 11u; i++) if (black_key[i] == k) bi = (int)i;
    kh_put(&p, KH_NOTE[k % 12u]);
    num[0] = (char)('3' + (k + 5u) / 12u), num[1] = 0;
    kh_put(&p, num), kh_put(&p, ": ");
    if (black && page != PAGE_SEQ && page != PAGE_ENV) {   /* the steering pages' black keys */
        if (bi == 10) w = "SWAP PROGRAMS";
        else w = bi < 5 ? "SCOUT-7 " : "ECHO-3 ", w2 = KH_NUDGE[bi % 5];
    } else if (page == PAGE_HOME && wi >= 0) {   /* the step and what it is now */
        uint32_t s = keys_top() + (uint32_t)wi, len = game.prog[sel_bot].len ? game.prog[sel_bot].len : game.loop_len;
        static char t[12];
        if (s >= len) w = "PAST THE LENGTH";
        else {
            const struct rs_step *st = &game.prog[sel_bot].s[s & 63u];
            t[0] = 'S', t[1] = 'T', t[2] = 'E', t[3] = 'P', t[4] = ' ';
            int n = 5;
            if (s + 1u >= 10u) t[n++] = (char)('0' + (s + 1u) / 10u);
            t[n++] = (char)('0' + (s + 1u) % 10u), t[n++] = ' ', t[n] = 0;
            w = t;
            if (layer == RS_LAYER_MODIFIER) w2 = KH_MOD[st->modifier % RS_NMODIFIERS];
            else if (layer == RS_LAYER_CHANCE) w2 = st->prob == 100 ? "100%" : st->prob == 75 ? "75%" : "50%";
            else w2 = KH_ACT[st->action % RS_NACTIONS];
        }
    } else if (page == PAGE_SEQ) {
        if (SEQ_KEY_ACT[k] >= 0) w = KH_ACT[SEQ_KEY_ACT[k]];
        else if (SEQ_KEY_MOD[k] >= 0) w = KH_MOD[SEQ_KEY_MOD[k]];
        else if (SEQ_KEY_PROB[k]) w = SEQ_KEY_PROB[k] == 100 ? "CHANCE 100%" : SEQ_KEY_PROB[k] == 75 ? "CHANCE 75%" : "CHANCE 50%";
        else if (k == SK_UNDO) w = "UNDO";
        else if (k == SK_RND) w = sel_bot ? "ROLL ECHO-3" : "ROLL SCOUT-7";
        else if (k == SK_RNDALL) w = "ROLL BOTH";
        else if (k == SK_SWAP) w = "SWAP PROGRAMS";
        else if (k == SK_CLEAR) w = "CLEAR BOTH";
    } else if (page == PAGE_ENV) {
        static const char *const TILE[RS_T_BLOCK + 1] = {"", "BELT", "ZAP RAIL", "ROTATOR L", "ROTATOR R", "PULSE PAD", "CRATE"};
        if (k == EK_LEFT || k == EK_UP || k == EK_DOWN || k == EK_RIGHT) w = "CURSOR";
        else if (k == EK_ERASE) w = "ERASE";
        else if (ENV_KEY_TILE[k]) w = TILE[ENV_KEY_TILE[k]];
        else if (k == EK_ROLL) w = "ROLL A BOARD";
        else if (k == EK_USER) w = "YOUR BOARD";
        else if (k == EK_SAVE) w = "SAVE AS YOURS";
        else if (k == EK_CLEAR) w = "CLEAR YOURS";
        else if (k == EK_RESET) w = "TO THE PADS";
    } else if (wi >= 0) {
        static const char *const SCL[5] = {"FOLLOW", "NEXT", "ROOT", "RESET", "RANDOM"};
        static const char *const ARP[5] = {"SEQ 2 ON/OFF", "ROLL A EACH ROUND", "ROLL B EACH ROUND", "RESET", "RANDOM"};
        static const char *const FADER[7] = {"MASTER", "MELODY", "BOT B", "PERCUSSION", "FX", "DRUMS", "LEAD"};
        static const char *const BOARD[11] = {"DEFAULT", "CORRIDOR", "GARDEN", "GAUNTLET", "SPINNER", "MAZE", "ROLL A BOARD",
                                              "YOUR BOARD", "SAVE AS YOURS", "CLEAR YOURS", "TO THE PADS"};
        static char t[10];
        if ((page == PAGE_SCL || page == PAGE_ARP) && wi < 4) t[0] = 'S', t[1] = 'T', t[2] = 'E', t[3] = 'P', t[4] = ' ',
                                                               t[5] = (char)('1' + wi), t[6] = 0, w = t;
        else if (page == PAGE_SCL && wi >= 11) w = SCL[wi - 11];
        else if (page == PAGE_ARP && wi >= 11) w = ARP[wi - 11];
        else if (page == PAGE_FX && wi < 7) w = "FADER ", w2 = FADER[wi];
        else if (page == PAGE_BOARDS && wi < 11) w = BOARD[wi];
        else if (page == PAGE_LFO && (uint32_t)wi < rs_drum_steps(game.loop_len))
            w = drum_row == 1 ? "SNARE " : drum_row == 2 ? "HIHAT " : "KICK ",
            t[0] = (char)('0' + (wi + 1) / 10), t[1] = (char)('0' + (wi + 1) % 10), t[2] = 0, w2 = wi + 1 >= 10 ? t : t + 1;
    }
    kh_put(&p, w ? w : "NOT USED HERE");
    if (w2) kh_put(&p, w2);
    toast_until = (fm1_ms + 1000u) | 1u;
}
