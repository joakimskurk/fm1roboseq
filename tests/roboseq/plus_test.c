/* SPDX-License-Identifier: GPL-3.0-only */
/* PLUS, RoboSeq's own music on top of the HTML's (D33): what each part promises. CLASSIC is held by the traces. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "rs_model.c"                                   /* (its tables: SCALE_MASK) */
#include "rs_score.c"                                   /* (PLUS's chord_tone) */

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_compose(void)                          /* the Voice Mod roll: one scale, a progression of its chords */
{
    static struct rs_state s;
    int on_notes = 0, distinct = 0;
    static uint8_t seen[1u << 16];
    for (int r = 0; r < 4000; r++) {
        rs_default(&s);
        s.music = RS_MUSIC_PLUS, s.rng = 0x9E3779B9u * (uint32_t)(r + 1), s.transpose_root = 0;
        s.bassmod_len = (r & 1) ? 8 : 4;
        rs_compose_bassmod(&s);
        CHECK(s.transpose_root == 1, "roll %d: the roots move the notes", r);
        int all_on = 1;
        for (int i = 0; i < s.bassmod_len; i++) {
            uint8_t sc = s.bassmod[i].scale;
            CHECK(sc == s.bassmod[0].scale, "roll %d step %d: one scale all through (a key), not %d and %d", r, i, sc, s.bassmod[0].scale);
            CHECK(sc == RS_SC_MAJOR || sc == RS_SC_MINOR || sc == RS_SC_DORIAN || sc == RS_SC_MIXOLYDIAN || sc == RS_SC_PENTATONIC ||
                  sc == RS_SC_MINPENTA, "roll %d step %d: a scale to play in, not %d", r, i, sc);
            CHECK(s.bassmod[i].root < 12 && s.bassmod[i].semitone == 0, "roll %d step %d: root, no semitone", r, i);
            all_on &= SCALE_MASK[sc] >> s.bassmod[i].root & 1;
        }
        on_notes += all_on;
        uint32_t h = s.bassmod[0].scale;
        for (int i = 0; i < 4; i++) h = h * 31u + s.bassmod[i].root;
        if (!seen[h & 0xFFFFu]) seen[h & 0xFFFFu] = 1, distinct++;
    }
    CHECK(on_notes > 3000, "four in five: every root a note of the scale (%d of 4000)", on_notes);
    CHECK(distinct > 40, "many progressions, not a few (%d)", distinct);
    {                                                   /* undoable, as the HTML's roll */
        rs_default(&s);
        s.music = RS_MUSIC_PLUS;
        struct rs_bassmod_step before = s.bassmod[0];
        rs_undo_clear();
        rs_compose_bassmod(&s);
        rs_pop_undo(&s);
        CHECK(!memcmp(&before, &s.bassmod[0], sizeof before), "undo brings the Voice Mod back");
    }
    {                                                   /* PLUS's SCL turns every mode */
        rs_default(&s);
        s.music = RS_MUSIC_PLUS, s.bassmod[0].scale = RS_SC_LYDIAN;
        rs_cycle_bassmod(&s, 0, 0, +1);
        CHECK(s.bassmod[0].scale == RS_SC_PHRYGIAN, "PLUS: lydian -> phrygian");
        s.music = RS_MUSIC_CLASSIC, s.bassmod[0].scale = RS_SC_MAJOR;
        rs_cycle_bassmod(&s, 0, 0, +1);
        CHECK(s.bassmod[0].scale == RS_SC_MINOR, "CLASSIC: the HTML's order, major -> minor");
    }
}

static int out_of_scale(const struct rs_state *s, const struct rs_tick_out *o, int *pitched)   /* the tick's wrong notes */
{
    uint8_t sc = s->bassmod[s->vm_index % s->bassmod_len].scale;
    int bad = 0;
    for (int i = 0; i < o->nnotes; i++) {
        const struct rs_note *n = &o->notes[i];
        if (n->voice > RS_V_B_SUB && n->voice != RS_V_COIN) continue;
        if (n->midi == RS_NOISE || !n->vel) continue;
        (*pitched)++;
        bad += !(SCALE_MASK[sc] >> (((n->midi % 12) + 12) % 12) & 1);
    }
    return bad;
}

static int rubs(const struct rs_tick_out *o, int *pairs)   /* a semitone, major 7th or tritone struck together */
{
    int r = 0;
    for (int i = 0; i < o->nnotes; i++)
        for (int j = i + 1; j < o->nnotes; j++) {
            const struct rs_note *a = &o->notes[i], *b = &o->notes[j];
            if (a->voice > RS_V_B_SUB || b->voice > RS_V_B_SUB || a->at != b->at || !a->vel || !b->vel) continue;
            if (a->voice == RS_V_A_PING || b->voice == RS_V_A_PING) continue;
            int ic = ((a->midi - b->midi) % 12 + 12) % 12;
            (*pairs)++, r += ic == 1 || ic == 11 || ic == 6;
        }
    return r;
}

static void test_in_scale(void)                         /* no note outside the scale (CLASSIC's own too), no rubs in PLUS */
{
    static struct rs_state s;
    static struct rs_tick_out o;
    for (int mode = 0; mode < 2; mode++) {
        int bad = 0, pitched = 0, rub = 0, pairs = 0;
        for (int setup = 0; setup < 24; setup++) {
            rs_default(&s);
            if (setup % 3 == 1) rs_load_board(&s, RS_B_MAZE);
            if (setup % 3 == 2) rs_load_board(&s, (uint8_t)(setup % RS_NBOARDS));
            if (setup >= 2) s.music = RS_MUSIC_PLUS, s.rng = 1234u + (uint32_t)setup * 77u, s.bassmod_len = setup & 4 ? 8 : 4,
                            rs_compose_bassmod(&s);
            s.tpm = setup % 5 == 3 ? 2 : setup % 7 == 4 ? 4 : 1;
            if (setup % 6 == 5) rs_set_loop_len(&s, 12);
            s.music = (uint8_t)mode;
            rs_start(&s);
            for (int t = 0; t < 900; t++) {
                rs_tick(&s, &o);
                bad += out_of_scale(&s, &o, &pitched);
                rub += rubs(&o, &pairs);
                for (int i = 0; i < o.nnotes && mode; i++)   /* B's answer: never a tritone over Scout-7's note */
                    if (o.notes[i].voice == RS_V_A_PING && o.notes[i].at == rs_step_q8(&s) / 2u)
                        for (int j = 0; j < o.nnotes; j++)
                            if (o.notes[j].voice == RS_V_A_MAIN && o.notes[j].at == 0)
                                CHECK(((o.notes[i].midi - o.notes[j].midi) % 12 + 12) % 12 != 6, "the answer: no tritone");
            }
        }
        if (mode) CHECK(rub == 0, "PLUS: %d of %d notes struck together rub (a semitone, a 7th, a tritone)", rub, pairs);
        else printf("plus_test: CLASSIC's own rubs, for comparison: %d of %d\n", rub, pairs);
        CHECK(bad == 0, "%s: %d of %d pitched notes outside their step's scale", mode ? "PLUS" : "CLASSIC", bad, pitched);
        CHECK(pitched > 20000, "%s: enough notes checked (%d)", mode ? "PLUS" : "CLASSIC", pitched);
    }
}

static void test_harmony(void)                          /* the bots on the chord, the bass under it, the game the same */
{
    static struct rs_state c, s;
    static struct rs_tick_out oc, o;
    int ones = 0, roots = 0;
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
            int root = chord_root(&s, ms, ms->scale), p = s.step % 16;
            for (int i = 0; i < o.nnotes; i++) {
                const struct rs_note *n = &o.notes[i];
                if (p % 4 == 0 && n->at == 0 && (n->voice == RS_V_A_MAIN || n->voice == RS_V_B_FM)) {
                    int pc = ((n->midi - root) % 12 + 12) % 12;
                    CHECK(chord_tone(n->midi, root, ms->scale), "tick %d: voice %d on a beat: %d over the root is no chord tone", t, n->voice, pc);
                    ones++;
                }
                if (p == 0 && n->voice == RS_V_B_SUB) {
                    CHECK(((n->midi - root) % 12 + 12) % 12 == 0 && n->midi >= 31 && n->midi <= 52, "tick %d: the bass on the one is the root, low (%d)", t, n->midi);
                    roots++;
                }
            }
        }
    }
    CHECK(ones > 40 && roots > 40, "checked enough ones (%d notes, %d bass roots)", ones, roots);
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

static void test_lines(void)                            /* tunes that walk, a first half sparser, a groove a board */
{
    static struct rs_state c, s;
    static struct rs_tick_out oc, o;
    uint16_t groove[RS_NBOARDS];
    for (int board = 0; board < RS_NBOARDS; board++) {
        long jumps_c = 0, jumps_p = 0, nc = 0, np = 0, in_a = 0, in_b = 0, bars = 0, ones = 0, big = 0;
        int last_c = 0, last_p = 0, first = 1;
        rs_default(&c), rs_default(&s);
        rs_load_board(&c, (uint8_t)board), rs_load_board(&s, (uint8_t)board);
        s.music = RS_MUSIC_PLUS;
        rs_start(&c), rs_start(&s);
        groove[board] = 0;
        for (int t = 0; t < 16 * 32; t++) {
            rs_tick(&c, &oc), rs_tick(&s, &o);
            for (int i = 0; i < oc.nnotes; i++)
                if (oc.notes[i].voice == RS_V_A_MAIN && oc.notes[i].at == 0) {
                    if (last_c) jumps_c += abs(oc.notes[i].midi - last_c), nc++;
                    last_c = oc.notes[i].midi;
                }
            if (s.step == 0) first = 1;                 /* (a bar: the bots back at their pads, the tune again) */
            int dash = o.acted[0] && o.played[0].action == RS_DASH, bass_one = 0;
            for (int i = 0; i < o.nnotes; i++) {
                const struct rs_note *n = &o.notes[i];
                if (n->voice == RS_V_A_MAIN && n->at == 0) {
                    if (!first) {
                        jumps_p += abs(n->midi - last_p), np++;
                        big += abs(n->midi - last_p) > 12 && !dash;
                    }
                    last_p = n->midi, first = 0;
                    if (rs_plus_section(&s)) in_b++;
                    else in_a++;
                }
                if (n->voice == RS_V_B_SUB && n->at == 0) {
                    bass_one |= s.step == 0;
                    if (s.cycle == 2) groove[board] |= (uint16_t)(1u << s.step);
                }
            }
            if (s.step == 0 && (o.loop_reset || t == 0)) bars++, ones += bass_one;
        }
        printf("plus_test: board %d: Scout-7 moves %.1f semitones a note in a bar (the HTML's %.1f); groove %04x\n", board,
               (double)jumps_p / (double)np, (double)jumps_c / (double)nc, groove[board]);
        CHECK(np > 50 && jumps_p * 10 <= 35 * np, "board %d: Scout-7's tune walks (%.1f semitones a note)", board, (double)jumps_p / (double)np);
        CHECK(big == 0, "board %d: no leap past an octave in a bar but a dash's (%ld)", board, big);
        CHECK(in_a * 10 < in_b * 8, "board %d: the first half sparser (%ld notes in A, %ld in B)", board, in_a, in_b);
        CHECK(bars > 30 && ones * 10 >= bars * 9, "board %d: the bass on every bar's one (%ld of %ld)", board, ones, bars);
    }
    for (int i = 0; i < RS_NBOARDS; i++)
        for (int j = i + 1; j < RS_NBOARDS; j++)
            CHECK(groove[i] != groove[j], "boards %d and %d: a bass groove each (%04x)", i, j, groove[i]);
}

static int lead_at(const struct rs_tick_out *o, uint8_t voice)   /* the voice's note at the tick's start, or 0 */
{
    for (int i = 0; i < o->nnotes; i++)
        if (o->notes[i].voice == voice && o->notes[i].at == 0 && o->notes[i].vel) return o->notes[i].midi;
    return 0;
}

static void test_tunes(void)                            /* the tune comes back, Echo-3 answers it, the phrase comes home */
{
    static struct rs_state s;
    static struct rs_tick_out o;
    static int8_t roll[20][16], echo[20][16];
    long same = 0, notes = 0, answers = 0, imitated = 0, homes = 0, phrases = 0;
    for (int board = 0; board < RS_NBOARDS; board++) {
        rs_default(&s);
        rs_load_board(&s, (uint8_t)board);
        s.music = RS_MUSIC_PLUS;
        rs_start(&s);
        memset(roll, 0, sizeof roll), memset(echo, 0, sizeof echo);
        while (s.cycle < 20) {
            rs_tick(&s, &o);
            if (s.cycle < 20 && s.step >= 0 && s.step < 16)
                roll[s.cycle][s.step] = (int8_t)lead_at(&o, RS_V_A_MAIN), echo[s.cycle][s.step] = (int8_t)lead_at(&o, RS_V_B_FM);
        }
        for (int bar = 4; bar < 16; bar++)              /* the same bar of the next phrase: the same tune */
            for (int p = 0; p < 16; p++)
                if (roll[bar][p]) notes++, same += roll[bar + 4][p] == roll[bar][p];
        for (int bar = 0; bar < 20; bar++) {
            uint8_t sc = s.bassmod[bar % 4].scale;
            if (bar % 4 == 0) {                         /* A's first bar: Echo-3 answers with Scout-7's call */
                int f = 0;
                while (f < 12 && !roll[bar][f]) f++;
                for (int j = 1; j < 4 && f < 12; j++)
                    if (roll[bar][f + j] && echo[bar][12 + j] && echo[bar][12]) {
                        answers++;
                        imitated += idx_of(echo[bar][12 + j], sc) - idx_of(echo[bar][12], sc) ==
                                    idx_of(roll[bar][f + j], sc) - idx_of(roll[bar][f], sc);
                    }
            }
            if (bar % 4 == 3) {                         /* the phrase's last bar: its last note the chord's root */
                const struct rs_bassmod_step *ms = &s.bassmod[bar % 4];
                int last = 0, root = chord_root(&s, ms, ms->scale);
                for (int p = 0; p < 16; p++) last = roll[bar][p] ? roll[bar][p] : last;
                phrases++, homes += last && ((last - root) % 12 + 12) % 12 == 0;
            }
        }
    }
    printf("plus_test: the tune again %ld of %ld notes, answers %ld of %ld, home %ld of %ld\n", same, notes, imitated, answers, homes, phrases);
    CHECK(notes > 200 && same * 10 >= notes * 8, "the tune comes back a phrase later (%ld of %ld notes)", same, notes);
    CHECK(answers > 20 && imitated * 10 >= answers * 8, "Echo-3's answer goes as Scout-7's call (%ld of %ld)", imitated, answers);
    CHECK(homes * 10 >= phrases * 9, "a phrase ends on the chord's root (%ld of %ld)", homes, phrases);
}

int main(void)
{
    test_lines();
    test_tunes();
    test_compose();
    test_in_scale();
    test_harmony();
    test_drums();
    printf("plus_test: %s\n", fails ? "FAILED" : "all pass");
    return fails != 0;
}
