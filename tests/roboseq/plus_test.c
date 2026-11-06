/* SPDX-License-Identifier: GPL-3.0-only */
/* PLUS, RoboSeq's own music on top of the HTML's (D33): what each part promises. CLASSIC is held by the traces. */
#include <stdio.h>
#include <string.h>
#include "rs_model.c"                                   /* (its tables: SCALE_MASK) */
#include "rs_score.c"                                   /* (PLUS's chord_tone) */

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint16_t pcs(const struct rs_bassmod_step *b)    /* the pitch classes a step plays: its scale on its root */
{
    uint16_t m = SCALE_MASK[b->scale], r = 0;
    for (int k = 0; k < 12; k++)
        if (m >> k & 1) r |= (uint16_t)(1u << ((k + b->root) % 12));
    return r;
}

static void test_compose(void)                          /* the Voice Mod roll: a progression in a key */
{
    static struct rs_state s;
    int keyed = 0, mediant = 0, distinct = 0;
    static uint8_t seen[1u << 16];
    for (int r = 0; r < 4000; r++) {
        rs_default(&s);
        s.music = RS_MUSIC_PLUS, s.rng = 0x9E3779B9u * (uint32_t)(r + 1), s.transpose_root = 0;
        s.bassmod_len = (r & 1) ? 8 : 4;
        rs_compose_bassmod(&s);
        CHECK(s.transpose_root == 1, "roll %d: the roots move the notes", r);
        int in_key = 0;
        for (int k = 0; k < 12 && !in_key; k++) {       /* every step inside one major key? */
            uint16_t key = 0;
            for (int d = 0; d < 7; d++) key |= (uint16_t)(1u << ((k + KEY_PC[d]) % 12));
            int all = 1;
            for (int i = 0; i < s.bassmod_len; i++) all &= (pcs(&s.bassmod[i]) & ~key) == 0;
            in_key = all;
        }
        int same_kind = 1;                              /* else the mediants: one quality (or two in turn), no other */
        for (int i = 0; i < s.bassmod_len; i++) {
            uint8_t sc = s.bassmod[i].scale;
            same_kind &= sc == RS_SC_MAJOR || sc == RS_SC_MINOR || sc == RS_SC_PENTATONIC || sc == RS_SC_MINPENTA;
            CHECK(sc != RS_SC_CHROMATIC && sc != RS_SC_DIMINISHED && sc != RS_SC_WHOLETONE && sc != RS_SC_AUGMENTED,
                  "roll %d step %d: a scale of the key, not %d", r, i, sc);
            CHECK(s.bassmod[i].root < 12 && s.bassmod[i].semitone == 0, "roll %d step %d: root, no semitone", r, i);
        }
        keyed += in_key, mediant += !in_key;
        CHECK(in_key || same_kind, "roll %d: in a key, or the mediants' one quality", r);
        uint32_t h = 0;
        for (int i = 0; i < 4; i++) h = h * 31u + (uint32_t)((s.bassmod[i].root - s.bassmod[0].root + 12) % 12) * 16u + s.bassmod[i].scale;
        if (!seen[h & 0xFFFFu]) seen[h & 0xFFFFu] = 1, distinct++;
    }
    CHECK(keyed > 2600 && mediant > 500, "the families: %d in a key, %d mediants", keyed, mediant);
    CHECK(distinct > 60, "many progressions, not a few (%d)", distinct);
    {                                                   /* undoable, as the HTML's roll */
        rs_default(&s);
        s.music = RS_MUSIC_PLUS;
        struct rs_bassmod_step before = s.bassmod[0];
        rs_undo_clear();
        rs_compose_bassmod(&s);
        rs_pop_undo(&s);
        CHECK(!memcmp(&before, &s.bassmod[0], sizeof before), "undo brings the Voice Mod back");
    }
    {                                                   /* PLUS's SCL turns every mode the roll uses */
        rs_default(&s);
        s.music = RS_MUSIC_PLUS, s.bassmod[0].scale = RS_SC_LYDIAN;
        rs_cycle_bassmod(&s, 0, 0, +1);
        CHECK(s.bassmod[0].scale == RS_SC_PHRYGIAN, "PLUS: lydian -> phrygian");
        s.music = RS_MUSIC_CLASSIC, s.bassmod[0].scale = RS_SC_MAJOR;
        rs_cycle_bassmod(&s, 0, 0, +1);
        CHECK(s.bassmod[0].scale == RS_SC_MINOR, "CLASSIC: the HTML's order, major -> minor");
    }
}

static void test_harmony(void)                          /* the bots on the chord, the bass under it, the game the same */
{
    static struct rs_state c, s;
    static struct rs_tick_out oc, o;
    int ones = 0, roots = 0, louder = 0, quieter = 0;
    for (int run = 0; run < 2; run++) {
        rs_default(&c), rs_default(&s);
        if (run) {                                      /* and a rolled progression, MAZE */
            s.music = RS_MUSIC_PLUS, s.rng = 77, rs_compose_bassmod(&s), s.music = 0;
            c.bassmod_len = s.bassmod_len, memcpy(c.bassmod, s.bassmod, sizeof c.bassmod), c.transpose_root = 1;
            rs_load_board(&c, RS_B_MAZE), rs_load_board(&s, RS_B_MAZE);
        }
        s.music = RS_MUSIC_PLUS;
        rs_start(&c), rs_start(&s);
        for (int t = 0; t < 600; t++) {
            rs_tick(&c, &oc), rs_tick(&s, &o);
            CHECK(!memcmp(c.bot, s.bot, sizeof c.bot) && c.step == s.step && c.vm_index == s.vm_index,
                  "tick %d: PLUS plays the same game", t);
            if ((!o.move_tick && !o.loop_reset) || s.step < 0) continue;
            const struct rs_bassmod_step *ms = &s.bassmod[s.vm_index % s.bassmod_len];
            int root = ms->root % 12, p = s.step % 16;
            for (int i = 0; i < o.nnotes; i++) {
                const struct rs_note *n = &o.notes[i];
                if (p % 4 == 0 && n->at == 0 && (n->voice == RS_V_A_MAIN || n->voice == RS_V_B_FM)) {
                    int pc = ((n->midi - root) % 12 + 12) % 12;
                    CHECK(chord_tone(pc, ms->scale), "tick %d: voice %d on a beat: %d over the root is no chord tone", t, n->voice, pc);
                    ones++;
                }
                if (p == 0 && n->voice == RS_V_B_SUB) {
                    CHECK((n->midi - root) % 12 == 0 && n->midi >= 31 && n->midi <= 52, "tick %d: the bass on the one is the root, low (%d)", t, n->midi);
                    roots++;
                }
            }
            for (int i = 0; i < o.nnotes && i < oc.nnotes; i++)   /* the pulse: the one louder, the off-16ths softer */
                if (o.notes[i].voice == RS_V_A_MAIN && oc.notes[i].voice == RS_V_A_MAIN && o.notes[i].at == 0) {
                    if (p % 4 == 0) louder += o.notes[i].vel > oc.notes[i].vel || o.notes[i].vel == 32767;
                    if (p & 1) quieter += o.notes[i].vel < oc.notes[i].vel;
                }
        }
    }
    CHECK(ones > 40 && roots > 40, "checked enough ones (%d notes, %d bass roots)", ones, roots);
    CHECK(louder > 20 && quieter > 100, "the pulse: %d beats louder, %d off-16ths softer", louder, quieter);
}

static void test_drums(void)                            /* the fills, the crashes, quiet when cleared, the same each time */
{
    static struct rs_state s, t;
    static struct rs_tick_out o, o2;
    int fills = 0, crashes = 0, ghosts = 0, opens = 0, quiet_hats_ok = 1;
    rs_default(&s), rs_default(&t);
    s.music = t.music = RS_MUSIC_PLUS;
    rs_start(&s), rs_start(&t);
    for (int k = 0; k < 16 * 40; k++) {
        rs_tick(&s, &o), rs_tick(&t, &o2);
        CHECK(o.nnotes == o2.nnotes && !memcmp(o.notes, o2.notes, sizeof o.notes[0] * (size_t)o.nnotes), "tick %d: the same each time", k);
        int dp = (s.drum_tick + 15) % 16, last = s.cycle % 4 == 3;
        const struct rs_note *hat = 0, *sn = 0;
        for (int i = 0; i < o.nnotes; i++) {
            if (o.notes[i].at) continue;
            if (o.notes[i].voice == RS_V_HIHAT) hat = &o.notes[i];
            if (o.notes[i].voice == RS_V_SNARE) sn = &o.notes[i];
        }
        if (last && dp >= 12) {
            if (hat && hat->vel) quiet_hats_ok = 0;
            if (dp == 15) fills += sn && sn->vel > 20000;
        }
        if (dp == 0 && s.cycle % 4 == 0 && s.cycle > 0 && hat) crashes += hat->dur >= SEC(0.5);
        if (sn && sn->vel < 6000) ghosts++;
        if (hat && hat->dur > SEC(0.1) && hat->dur < SEC(0.3)) opens++;
    }
    CHECK(quiet_hats_ok, "no hats under a fill");
    CHECK(fills >= 9, "a fill ends each phrase, its last snare loudest (%d of 10)", fills);
    CHECK(crashes >= 9, "a crash starts each phrase (%d of 9)", crashes);
    CHECK(ghosts > 5 && opens > 5, "ghost snares (%d) and open hats (%d)", ghosts, opens);
    {                                                   /* CLEAR, and the drums off: nothing */
        int hits = 0;
        rs_default(&s), s.music = RS_MUSIC_PLUS, rs_drum_clear(&s), rs_start(&s);
        for (int k = 0; k < 16 * 8; k++) {
            rs_tick(&s, &o);
            for (int i = 0; i < o.nnotes; i++) hits += o.notes[i].voice >= RS_V_KICK && o.notes[i].voice <= RS_V_HIHAT;
        }
        rs_default(&s), s.music = RS_MUSIC_PLUS, s.drums_on = 0, rs_start(&s);
        for (int k = 0; k < 16 * 8; k++) {
            rs_tick(&s, &o);
            for (int i = 0; i < o.nnotes; i++) hits += o.notes[i].voice >= RS_V_KICK && o.notes[i].voice <= RS_V_HIHAT;
        }
        CHECK(hits == 0, "cleared or off, the drums stay quiet (%d hits)", hits);
    }
}

int main(void)
{
    test_compose();
    test_harmony();
    test_drums();
    printf("plus_test: %s\n", fails ? "FAILED" : "all pass");
    return fails != 0;
}
