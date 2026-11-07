/* SPDX-License-Identifier: GPL-3.0-only */
/* BAND, RoboSeq's own music on top of the HTML's (D33): what each part promises. ORIGINAL is held by the traces. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "rs_model.c"                                   /* (its tables: SCALE_MASK) */
#include "rs_score.c"                                   /* (BAND's chord_tone) */
#include "rs_mix.c"                                     /* (the robots' levels through the mix) */
#include <math.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int c_mode(uint8_t sc)                           /* a scale the progressions use: a C mode or a C pentatonic */
{
    return sc == RS_SC_MAJOR || sc == RS_SC_MINOR || sc == RS_SC_DORIAN || sc == RS_SC_MIXOLYDIAN || sc == RS_SC_LYDIAN ||
           sc == RS_SC_PHRYGIAN || sc == RS_SC_PENTATONIC || sc == RS_SC_MINPENTA;
}

static void test_compose(void)                          /* the Voice Mod roll (D38): a named progression in a key */
{
    static struct rs_state s;
    int distinct = 0, keys[RS_NKEYS] = {0}, ids_seen = 0, borrowed = 0, homes = 0, eights = 0;
    static uint8_t seen[1u << 16], id_seen[256];
    for (int r = 0; r < 4000; r++) {
        rs_default(&s);
        s.engine = RS_ENGINE_BAND, s.rng = 0x9E3779B9u * (uint32_t)(r + 1), s.transpose_root = 0;
        s.bassmod_len = (r & 1) ? 8 : 4;
        rs_compose_bassmod(&s);
        CHECK(s.transpose_root == 1, "roll %d: the roots move the notes", r);
        CHECK(s.prog_id >= 1 && s.prog_id <= rs_nprogs(), "roll %d: a named progression (%d)", r, s.prog_id);
        int one_key = 1;
        for (int i = 0; i < s.bassmod_len; i++) {
            uint8_t sc = s.bassmod[i].scale;
            CHECK(c_mode(sc), "roll %d step %d: a scale to play in, not %d", r, i, sc);
            CHECK(s.bassmod[i].root < 12 && s.bassmod[i].semitone == 0, "roll %d step %d: root, no semitone", r, i);
            CHECK(SCALE_MASK[sc] >> s.bassmod[i].root & 1, "roll %d step %d: the root a note of its scale", r, i);
            one_key &= sc == s.bassmod[0].scale;
        }
        int bor = PROG[s.prog_id - 1].mode == M_BOR;
        CHECK(one_key || bor, "roll %d: one key all through, unless it borrows (%s)", r, rs_prog_name(s.prog_id));
        borrowed += bor && !one_key;
        keys[s.prog_key % RS_NKEYS]++;
        if (!id_seen[s.prog_id]) id_seen[s.prog_id] = 1, ids_seen++;
        if (s.bassmod_len == 8 && PROG[s.prog_id - 1].n == 4) {   /* said twice: the second time home (or on V) */
            const struct prog_def *pd = &PROG[s.prog_id - 1];
            struct rs_bassmod_step want = prog_chord(pd, s.prog_key, pd->d[3] == 0 ? 4 : 0, 0);
            eights++, homes += s.bassmod[7].root == want.root && s.bassmod[7].scale == want.scale;
        }
        uint32_t h = s.bassmod[0].scale;
        for (int i = 0; i < s.bassmod_len; i++) h = h * 31u + s.bassmod[i].root * 13u + s.bassmod[i].scale;
        if (!seen[h & 0xFFFFu]) seen[h & 0xFFFFu] = 1, distinct++;
    }
    printf("band_test: the roll: %d different Voice Mods in 4000, %d of %u progressions, keys %d %d %d %d %d %d, %d borrowing\n",
           distinct, ids_seen, rs_nprogs(), keys[0], keys[1], keys[2], keys[3], keys[4], keys[5], borrowed);
    CHECK(distinct > 600, "many progressions, not a few (%d)", distinct);
    CHECK(ids_seen == (int)rs_nprogs(), "every named progression rolls (%d of %u)", ids_seen, rs_nprogs());
    for (int k = 0; k < RS_NKEYS; k++) CHECK(keys[k] > 200, "every key (%d: %d)", k, keys[k]);
    CHECK(borrowed > 50, "some borrow a chord (%d)", borrowed);
    CHECK(eights > 500 && homes == eights, "four chords in eight steps: the second time home (%d of %d)", homes, eights);
    {                                                   /* every progression in every key: its chords where they belong */
        static const int MAJ[7] = {0, 2, 4, 5, 7, 9, 11};
        rs_default(&s);
        s.bassmod_len = 4;
        rs_apply_prog(&s, 1, 5);                        /* I V vi IV in G (C lydian's notes): G D E C */
        CHECK(s.bassmod[0].root == 7 && s.bassmod[1].root == 2 && s.bassmod[2].root == 4 && s.bassmod[3].root == 0 &&
              s.bassmod[0].scale == RS_SC_LYDIAN, "I V vi IV in G: G D E C on G major's notes");
        char kn[4];
        rs_prog_key_name(1, 5, kn);
        CHECK(!strcmp(kn, "G"), "its key: G (%s)", kn);
        for (uint8_t id = 1; id <= rs_nprogs(); id++)
            if (PROG[id - 1].mode == M_AEO) {
                rs_apply_prog(&s, id, KEY_C);           /* a minor one in C's notes: A minor */
                rs_prog_key_name(id, KEY_C, kn);
                CHECK(!strcmp(kn, "Am"), "%s in C's notes is in A minor (%s)", rs_prog_name(id), kn);
                CHECK(s.bassmod[0].root == MAJ[(5 + PROG[id - 1].d[0]) % 7], "%s: its first chord", rs_prog_name(id));
                break;
            }
        rs_apply_prog(&s, 1, 4);
        const struct rs_bassmod_step *nx = rs_next_mod_step(&s);
        CHECK(nx == &s.bassmod[1], "the next chord is the next step's");
    }
    {                                                   /* undoable, as the HTML's roll */
        rs_default(&s);
        s.engine = RS_ENGINE_BAND;
        struct rs_bassmod_step before = s.bassmod[0];
        rs_undo_clear();
        rs_compose_bassmod(&s);
        rs_pop_undo(&s);
        CHECK(!memcmp(&before, &s.bassmod[0], sizeof before), "undo brings the Voice Mod back");
    }
    {                                                   /* BAND's SCL turns every mode */
        rs_default(&s);
        s.engine = RS_ENGINE_BAND, s.bassmod[0].scale = RS_SC_LYDIAN;
        rs_cycle_bassmod(&s, 0, 0, +1);
        CHECK(s.bassmod[0].scale == RS_SC_PHRYGIAN, "BAND: lydian -> phrygian");
        s.engine = RS_ENGINE_ORIGINAL, s.bassmod[0].scale = RS_SC_MAJOR;
        rs_cycle_bassmod(&s, 0, 0, +1);
        CHECK(s.bassmod[0].scale == RS_SC_MINOR, "ORIGINAL: the HTML's order, major -> minor");
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

static void test_in_scale(void)                         /* no note outside the scale (ORIGINAL's own too), no rubs in BAND */
{
    static struct rs_state s;
    static struct rs_tick_out o;
    for (int mode = 0; mode < 2; mode++) {
        int bad = 0, pitched = 0, rub = 0, pairs = 0;
        for (int setup = 0; setup < 24; setup++) {
            rs_default(&s);
            if (setup % 3 == 1) rs_load_board(&s, RS_B_MAZE);
            if (setup % 3 == 2) rs_load_board(&s, (uint8_t)(setup % RS_NBOARDS));
            if (setup >= 2) s.engine = RS_ENGINE_BAND, s.rng = 1234u + (uint32_t)setup * 77u, s.bassmod_len = setup & 4 ? 8 : 4,
                            rs_compose_bassmod(&s);
            s.tpm = setup % 5 == 3 ? 2 : setup % 7 == 4 ? 4 : 1;
            if (setup % 6 == 5) rs_set_loop_len(&s, 12);
            s.engine = (uint8_t)mode;
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
        if (mode) CHECK(rub == 0, "BAND: %d of %d notes struck together rub (a semitone, a 7th, a tritone)", rub, pairs);
        else printf("band_test: ORIGINAL's own rubs, for comparison: %d of %d\n", rub, pairs);
        CHECK(bad == 0, "%s: %d of %d pitched notes outside their step's scale", mode ? "BAND" : "ORIGINAL", bad, pitched);
        CHECK(pitched > 20000, "%s: enough notes checked (%d)", mode ? "BAND" : "ORIGINAL", pitched);
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
            s.engine = RS_ENGINE_BAND, s.rng = 77, rs_compose_bassmod(&s), s.engine = 0;
            c.bassmod_len = s.bassmod_len, memcpy(c.bassmod, s.bassmod, sizeof c.bassmod), c.transpose_root = 1;
            rs_load_board(&c, RS_B_MAZE), rs_load_board(&s, RS_B_MAZE);
        }
        s.engine = RS_ENGINE_BAND;
        rs_start(&c), rs_start(&s);
        for (int t = 0; t < 600; t++) {
            rs_tick(&c, &oc), rs_tick(&s, &o);
            CHECK(!memcmp(c.bot, s.bot, sizeof c.bot) && c.step == s.step && c.vm_index == s.vm_index,
                  "tick %d: BAND plays the same game", t);
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
    s.engine = t.engine = RS_ENGINE_BAND;
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
        for (int i = 0; i < o.nnotes; i++)              /* (BAND's crash rides the crackle's voice, D41) */
            if (dp == 0 && s.cycle % 4 == 0 && s.cycle > 0 && o.notes[i].at == 0 && o.notes[i].dur >= SEC(0.5) &&
                (o.notes[i].voice == RS_V_HIHAT || o.notes[i].voice == RS_V_EX_CRACKLE)) crashes++;
        if (sn && sn->vel < 6000) ghosts++;
        if (hat && hat->dur > SEC(0.1) && hat->dur < SEC(0.3)) opens++;
    }
    CHECK(quiet_hats_ok, "no hats under a fill");
    CHECK(fills >= 9, "a fill ends each phrase, its last snare loudest (%d of 10)", fills);
    CHECK(crashes >= 9, "a crash starts each phrase (%d of 9)", crashes);
    CHECK(ghosts > 5 && opens > 5, "ghost snares (%d) and open hats (%d)", ghosts, opens);
    {                                                   /* CLEAR, and the drums off: nothing */
        int hits = 0;
        rs_default(&s), s.engine = RS_ENGINE_BAND, rs_drum_clear(&s), rs_start(&s);
        for (int k = 0; k < 16 * 8; k++) {
            rs_tick(&s, &o);
            for (int i = 0; i < o.nnotes; i++) hits += o.notes[i].voice >= RS_V_KICK && o.notes[i].voice <= RS_V_HIHAT;
        }
        rs_default(&s), s.engine = RS_ENGINE_BAND, s.drums_on = 0, rs_start(&s);
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
        s.engine = RS_ENGINE_BAND;
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
                    if (rs_band_section(&s)) in_b++;
                    else in_a++;
                }
                if (n->voice == RS_V_B_SUB && n->at == 0) {
                    bass_one |= s.step == 0;
                    if (s.cycle == 2) groove[board] |= (uint16_t)(1u << s.step);
                }
            }
            if (s.step == 0 && (o.loop_reset || t == 0)) bars++, ones += bass_one;
        }
        printf("band_test: board %d: Scout-7 moves %.1f semitones a note in a bar (the HTML's %.1f); groove %04x\n", board,
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
    static int8_t roll[20][16], echo[20][16], low[20][16];
    long same = 0, notes = 0, answers = 0, imitated = 0, homes = 0, phrases = 0, runs = 0;
    for (int board = 0; board < RS_NBOARDS; board++) {
        rs_default(&s);
        rs_load_board(&s, (uint8_t)board);
        s.engine = RS_ENGINE_BAND;
        rs_start(&s);
        memset(roll, 0, sizeof roll), memset(echo, 0, sizeof echo), memset(low, 0, sizeof low);
        while (s.cycle < 20) {
            rs_tick(&s, &o);
            if (s.cycle < 20 && s.step >= 0 && s.step < 16)
                roll[s.cycle][s.step] = (int8_t)lead_at(&o, RS_V_A_MAIN), echo[s.cycle][s.step] = (int8_t)lead_at(&o, RS_V_B_FM),
                low[s.cycle][s.step] = (int8_t)lead_at(&o, RS_V_B_SUB);
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
                if (bar + 1 < 20) {                     /* and the bass runs into the next phrase, a scale step a sixteenth */
                    int ok = 1;
                    for (int p = 12; p < 16; p++) {
                        int to = p < 15 ? low[bar][p + 1] : low[bar + 1][0];
                        int d = low[bar][p] && to ? idx_of(to, sc) - idx_of(low[bar][p], sc) : 9;
                        ok &= d == 1 || d == -1;
                    }
                    runs += ok;
                }
            }
        }
    }
    printf("band_test: the tune again %ld of %ld notes, answers %ld of %ld, home %ld of %ld, runs %ld\n", same, notes, imitated, answers, homes, phrases, runs);
    CHECK(runs * 10 >= phrases * 8, "the bass runs into the next phrase (%ld of %ld)", runs, phrases);
    CHECK(notes > 200 && same * 10 >= notes * 8, "the tune comes back a phrase later (%ld of %ld notes)", same, notes);
    CHECK(answers > 20 && imitated * 10 >= answers * 8, "Echo-3's answer goes as Scout-7's call (%ld of %ld)", imitated, answers);
    CHECK(homes * 10 >= phrases * 9, "a phrase ends on the chord's root (%ld of %ld)", homes, phrases);
}

static void test_peaks(void)                            /* every other phrase's B the big one: denser, louder, an arpeggio */
{
    static struct rs_state s;
    static struct rs_tick_out o;
    for (int board = 0; board < RS_NBOARDS; board++) {
        long notes[2] = {0}, vel[2] = {0}, bars[2] = {0}, arps[2] = {0}, evens[2] = {0}, bassed[2] = {0}, a_arps = 0;
        rs_default(&s);
        rs_load_board(&s, (uint8_t)board);
        s.engine = RS_ENGINE_BAND;
        rs_start(&s);
        while (s.cycle < 16) {
            rs_tick(&s, &o);
            if ((!o.move_tick && !o.loop_reset) || s.step < 0) continue;
            int b = rs_band_section(&s), pk = rs_band_peak(&s), fill = s.cycle % 4 == 3 && s.step >= 12, ping = 0, sub = 0;
            CHECK(!pk || b, "board %d: a peak only in a B", board);
            if (s.step == 0) bars[pk] += b;
            for (int i = 0; i < o.nnotes; i++) {
                const struct rs_note *n = &o.notes[i];
                if (n->voice > RS_V_B_SUB || !n->vel) continue;
                if (b) notes[pk]++, vel[pk] += n->vel;
                ping |= n->voice == RS_V_A_PING && n->at == 0;
                sub |= n->voice == RS_V_B_SUB && n->at == 0;
            }
            if (!b) a_arps += ping && o.acted[0] && o.played[0].action != RS_PING && s.bot[0].stepped_tile != RS_T_BELT && s.bot[0].heat < 4;
            if (b && s.step % 2 == 0 && !fill) evens[pk]++, arps[pk] += ping, bassed[pk] += sub;
        }
        printf("band_test: board %d: B %ld notes a bar, the big one %ld (%.1fx the velocity), its arpeggio on %ld of %ld eighths, bass %ld\n",
               board, notes[0] / bars[0], notes[1] / bars[1], (double)vel[1] * (double)bars[0] / ((double)vel[0] * (double)bars[1]),
               arps[1], evens[1], bassed[1]);
        CHECK(notes[1] * 10 >= notes[0] * 13, "board %d: the big one denser (%ld, %ld notes a bar)", board,
              notes[1] / bars[1], notes[0] / bars[0]);
        CHECK(vel[1] * 10 / bars[1] >= vel[0] * 13 / bars[0], "board %d: and louder, all told (%ld, %ld a bar)", board, vel[1] / bars[1], vel[0] / bars[0]);
        CHECK(arps[1] * 10 >= evens[1] * 8 && bassed[1] * 10 >= evens[1] * 9, "board %d: its arpeggio (%ld) and bass (%ld) on the eighths (%ld)",
              board, arps[1], bassed[1], evens[1]);
        CHECK(arps[0] * 10 < evens[0] * 3 && a_arps == 0, "board %d: none in the other Bs (%ld of %ld) or an A (%ld)", board, arps[0], evens[0], a_arps);
    }
}

static void test_tiles(void)                            /* what the robots meet shapes the notes (D37a, D37b) */
{
    static struct rs_state s;
    static struct rs_tick_out o;
    long zap = 0, zap_ok = 0, pulse = 0, pulse_ok = 0, rot = 0, rot_ok = 0, belt = 0, belt_ok = 0, chokes = 0, plain = 0, plain_ok = 0;
    for (int board = 0; board < RS_NBOARDS; board++)
        for (int roll = 0; roll < 3; roll++) {
            rs_default(&s);
            rs_load_board(&s, (uint8_t)board);
            s.engine = RS_ENGINE_BAND, s.rng = 991u + (uint32_t)(board * 7 + roll);
            if (roll) rs_randomize_all(&s);
            rs_start(&s);
            uint32_t step = rs_step_q8(&s);
            while (s.cycle < 12) {
                rs_tick(&s, &o);
                if (!o.move_tick || s.step < 0) continue;
                const struct rs_note *lead = 0;
                int pings_up = 0, pings_down = 0, last_ping = 0, choke = 0;
                for (int i = 0; i < o.nnotes; i++) {
                    const struct rs_note *n = &o.notes[i];
                    if (n->voice == RS_V_A_MAIN && n->at == 0) {
                        if (n->vel) lead = n;
                        else choke = 1;
                    }
                    if (n->voice == RS_V_A_PING && n->at > 0) {
                        if (last_ping) pings_up += n->midi > last_ping, pings_down += n->midi < last_ping;
                        last_ping = n->midi;
                    }
                }
                chokes += choke;
                if (choke) CHECK(o.bumped[0] & 2, "board %d: a choke only after a crate", board);
                int pp = s.step % 16, in = (int)(s.cycle % 4), cadence = (in == 1 || in == 3) && pp >= 8;
                if (!lead || !o.acted[0] || rs_band_peak(&s) || s.band_at[0] != pp || cadence) continue;   /* (its struck note) */
                int land = s.bot[0].stepped_tile, mod = o.played[0].modifier;
                if (land == RS_T_ZAP) zap++, zap_ok += lead->dur <= step / 2u && lead->fx == 0;
                else if (mod == RS_GLITCH) continue;      /* (GLITCH: clipped and plain wherever it is) */
                else if (land == RS_T_PULSE) {
                    uint32_t calls = rs_band_section(&s) ? 16u : 12u, want = (calls - (uint32_t)pp) * step * 9u / 10u;
                    pulse++, pulse_ok += (lead->fx & RS_FX_VIB_WIDE) && lead->dur >= (want < step * 3u / 2u ? want : step * 3u / 2u);
                } else if (land == RS_T_ROTL || land == RS_T_ROTR)
                    rot++, rot_ok += (lead->fx & (land == RS_T_ROTL ? RS_FX_BEND_UP : RS_FX_BEND_DOWN)) != 0;
                else if (land == RS_T_BELT) {
                    int up = s.bot[0].stepped_dir == RS_N || s.bot[0].stepped_dir == RS_E;
                    belt++, belt_ok += up ? !pings_down : !pings_up;
                } else if (mod != RS_GLITCH && mod != RS_WOBBLE) plain++, plain_ok += (lead->fx & RS_FX_VIB_LATE) != 0;
            }
        }
    printf("band_test: zap rails %ld (short %ld), pulse pads %ld (held, singing %ld), rotators %ld (bent %ld), belts %ld (the belt's way %ld), "
           "chokes %ld, the rest %ld (a late vibrato %ld)\n", zap, zap_ok, pulse, pulse_ok, rot, rot_ok, belt, belt_ok, chokes, plain, plain_ok);
    CHECK(zap > 10 && zap_ok == zap, "a zap rail's note short and plain (%ld of %ld)", zap_ok, zap);
    CHECK(pulse > 10 && pulse_ok * 10 >= pulse * 9, "a pulse pad's note held, with a wide vibrato (%ld of %ld)", pulse_ok, pulse);
    CHECK(rot > 10 && rot_ok == rot, "a rotator bends the note in, its way (%ld of %ld)", rot_ok, rot);
    CHECK(belt > 10 && belt_ok == belt, "a belt's arpeggio goes its way (%ld of %ld)", belt_ok, belt);
    CHECK(chokes > 5, "a crate chokes a held note (%ld)", chokes);
    CHECK(plain > 50 && plain_ok == plain, "every other note may sing late (%ld of %ld)", plain_ok, plain);
}

static void test_arp_rules(void)                        /* the arpeggio by the HTML's rules (D37c) */
{
    static struct rs_state s;
    int t[12], shapes = 0;
    static char seen[4096];
    rs_default(&s);
    s.engine = RS_ENGINE_BAND;
    for (int x = 0; x < 5; x++)
        for (uint32_t cyc = 0; cyc < 18; cyc += 3) {
            s.bot[0].x = (int8_t)x, s.bot[0].y = 2, s.bot[1].x = (int8_t)x, s.bot[1].y = 2, s.cycle = cyc, s.bot[0].heat = 0;
            int n = arp_tones(&s, 0, RS_SC_MAJOR, 67, t), sig = 0;
            CHECK(n >= 3 && t[n - 1] - t[0] <= 12, "x %d: an octave of tones (%d, %d..%d)", x, n, t[0], t[n - 1]);
            for (int i = 0; i < n; i++) {
                sig |= 1 << ((t[i] - t[0]) % 12);
                CHECK(SCALE_MASK[RS_SC_MAJOR] >> (t[i] % 12) & 1, "x %d: in the scale (%d)", x, t[i]);
            }
            if (!seen[sig]) seen[sig] = 1, shapes++;
        }
    CHECK(shapes >= 4, "where Scout-7 stands picks the chord's shape (%d shapes)", shapes);
    s.bot[0].x = 1, s.bot[1].x = 1, s.cycle = 0, s.bot[0].heat = 4;
    int n = arp_tones(&s, 0, RS_SC_MAJOR, 67, t);
    CHECK(t[n - 1] - t[0] > 12, "hot (4): two octaves (%d..%d)", t[0], t[n - 1]);
    s.bot[0].heat = 0;
    int near = arp_tones(&s, 0, RS_SC_MAJOR, 67, t), a0[12];
    memcpy(a0, t, sizeof a0);
    s.bot[1].x = 4, s.bot[1].y = 0;                     /* (far apart: 3 + 2 tiles) */
    int far = arp_tones(&s, 0, RS_SC_MAJOR, 67, t), diff = far != near;
    for (int i = 0; i < near && i < far; i++) diff |= a0[i] != t[i];
    CHECK(diff, "far apart, the other shape");
}

static void test_follow_chain(void)                     /* FOLLOW and the chain (D38) */
{
    static struct rs_state s;
    static struct rs_tick_out o;
    int rounds = 0, changed = 0, near_key = 0, ahead = 0;
    rs_default(&s);
    s.engine = RS_ENGINE_BAND, s.rng = 4242;
    rs_compose_bassmod(&s);
    rs_set_follow(&s, 1);
    rs_start(&s);
    uint8_t last_id = s.prog_id, last_key = s.prog_key;
    struct rs_bassmod_step promised = {0, 0, 0};
    int promised_ok = 0;
    while (s.cycle < 4 * 20) {
        uint8_t vm = s.vm_index;
        rs_tick(&s, &o);
        if (s.vm_index + 1u == s.bassmod_len && s.next_ready && !promised_ok)   /* the last step: the next one ready */
            promised = *rs_next_mod_step(&s), promised_ok = 1;
        if (s.vm_index == 0 && vm != 0) {
            rounds++, changed += s.prog_id != last_id;
            int d = (s.prog_key - last_key + RS_NKEYS) % RS_NKEYS;
            near_key += d == 0 || d == 1 || d == RS_NKEYS - 1;
            ahead += promised_ok && promised.root == s.bassmod[0].root && promised.scale == s.bassmod[0].scale;
            last_id = s.prog_id, last_key = s.prog_key, promised_ok = 0;
        }
    }
    printf("band_test: FOLLOW: %d rounds, %d new, %d in the key or a fifth off, %d as the last step said\n", rounds, changed, near_key, ahead);
    CHECK(rounds >= 19 && changed == rounds, "FOLLOW: a new progression every round (%d of %d)", changed, rounds);
    CHECK(near_key == rounds, "FOLLOW: the same key or a fifth away (%d of %d)", near_key, rounds);
    CHECK(ahead == rounds, "the bass knew the next round's first chord (%d of %d)", ahead, rounds);
    {                                                   /* the chain: Seq 2's steps name the next round's progression */
        static const uint8_t CHAIN[RS_SEQ2_STEPS] = {3, 0, 10, RS_P_RND};
        int ok = 0, n = 0;
        rs_default(&s);
        s.engine = RS_ENGINE_BAND, s.rng = 77;
        rs_apply_prog(&s, 1, 2);                        /* I V vi IV in Bb */
        for (int i = 0; i < RS_SEQ2_STEPS; i++) s.seq2[i].prog = CHAIN[i];
        rs_seq2_toggle(&s);                             /* (stopped: on at once) */
        rs_start(&s);
        uint8_t before = s.prog_id;
        while (s.cycle < 4 * 9) {
            uint8_t vm = s.vm_index;
            rs_tick(&s, &o);
            if (s.vm_index == 0 && vm != 0) {
                uint8_t want = s.seq2[s.seq2_index].prog;
                n++;
                if (want == 0) ok += s.prog_id == before;   /* "--": as it was */
                else if (want == RS_P_RND) ok += s.prog_id != before;
                else ok += s.prog_id == want && s.prog_key == 2;   /* (in the key it was in) */
                before = s.prog_id;
            }
        }
        CHECK(n >= 8 && ok == n, "the chain: every round as its Seq 2 step says (%d of %d)", ok, n);
    }
}

static double level(int board, int engine, uint32_t mask, int *quiet_pct)   /* a robot's voices alone through the mix */
{
    static struct rs_mix m;
    static struct rs_state s;
    static struct rs_tick_out o;
    static int32_t buf[512];
    rs_default(&s);
    rs_load_board(&s, (uint8_t)board);
    s.engine = (uint8_t)engine;
    rs_mix_init(&m, 1);
    struct rs_patch pt;
    memset(&pt, 0, sizeof pt);
    if (engine) rs_band_patch(&s.board, &pt);
    rs_mix_patch(&m, &pt);
    rs_start(&s);
    uint64_t t_q8 = 0, now = 0;
    double e = 0, win = 0;
    long n = 0, wins = 0, quiet = 0, k = 0;
    while (s.cycle < 12) {
        rs_tick(&s, &o);
        for (int i = 0; i < o.nnotes; i++)
            if (mask >> o.notes[i].voice & 1)
                rs_mix_note_fx(&m, o.notes[i].voice, (uint32_t)((t_q8 + o.notes[i].at + 128) >> 8), (o.notes[i].dur + 128) >> 8,
                               o.notes[i].midi, o.notes[i].vel, o.notes[i].fx);
        t_q8 += rs_step_q8(&s);
        while ((now + 256) * 256 <= t_q8) {
            memset(buf, 0, sizeof buf);
            rs_mix_render(&m, buf, 256, (uint32_t)now);
            for (int i = 0; i < 512; i++) e += (double)buf[i] * buf[i], win += (double)buf[i] * buf[i], n++;
            if (++k == 8) wins++, quiet += win / 4096.0 < 1073741824.0 * 3.2e-5, win = 0, k = 0;   /* (46 ms under -45 dB) */
            now += 256;
        }
    }
    *quiet_pct = (int)(100 * quiet / (wins ? wins : 1));
    return 10 * log10(e / (double)n / 1073741824.0 + 1e-12);
}

static void test_balance(void)                          /* Scout-7 sings in front, Echo-3 under it (D40) */
{
    const uint32_t MAIN = 1u << RS_V_A_MAIN, ECHO = 1u << RS_V_B_FM;
    int qsum = 0;
    for (int board = 0; board < RS_NBOARDS; board++) {
        int qs, qe, qo, qoe;
        double ls = level(board, 1, MAIN, &qs), le = level(board, 1, ECHO, &qe);
        double os = level(board, 0, MAIN, &qo), oe = level(board, 0, ECHO, &qoe);
        printf("band_test: board %d: Scout-7 %+.1f dB over Echo-3 (ORIGINAL's %+.1f), quiet %d%% of the time (ORIGINAL's %d%%)\n",
               board, ls - le, os - oe, qs, qo);
        CHECK(ls >= le + 0.5, "board %d: Scout-7 in front of Echo-3 (%.1f, %.1f dB)", board, ls, le);
        CHECK(qs <= 35, "board %d: Scout-7 not often quiet (%d%%)", board, qs);
        qsum += qs;
    }
    CHECK(qsum <= 24 * RS_NBOARDS, "Scout-7 quiet a fifth of the time or less, on the boards together (%d%%)", qsum / RS_NBOARDS);
}

static void test_kit(void)                             /* BAND's kit (D41): the hits' tone from the board and the moves */
{
    static struct rs_state s;
    static struct rs_tick_out o;
    long kicks = 0, tuned = 0, ones = 0, clicked = 0, snares = 0, snare_ok = 0, hats = 0, hat_ok = 0, crashes = 0;
    int long_garden = 0, tight_maze = 0, orig_plain = 1;
    for (int board = 0; board < RS_NBOARDS; board++)
        for (int eng = 0; eng < 2; eng++) {
            rs_default(&s);
            rs_load_board(&s, (uint8_t)board);
            s.engine = (uint8_t)eng;
            rs_start(&s);
            while (s.cycle < 8) {
                rs_tick(&s, &o);
                if (s.step < 0) continue;
                const struct rs_bassmod_step *ms = &s.bassmod[s.vm_index % s.bassmod_len];
                int root = chord_root(&s, ms, ms->scale);
                for (int i = 0; i < o.nnotes; i++) {
                    const struct rs_note *n = &o.notes[i];
                    if (!eng) {
                        if (n->voice == RS_V_KICK) orig_plain &= n->midi == 24 && n->fx == 0;
                        if (n->voice == RS_V_SNARE || n->voice == RS_V_HIHAT) orig_plain &= n->midi == RS_NOISE;
                        continue;
                    }
                    if (n->voice == RS_V_KICK) {
                        kicks++, tuned += ((n->midi - root) % 12 + 12) % 12 == 0 && n->midi >= 28 && n->midi <= 39 && (n->fx & RS_KICK_DRIVE);
                        if (s.step == 0) ones++, clicked += (n->fx & RS_KICK_CLICK) != 0;
                        if (board == RS_B_GARDEN) long_garden |= (n->fx & RS_KICK_LONG) != 0;
                        if (board == RS_B_MAZE) tight_maze |= (n->fx & RS_KICK_TIGHT) != 0;
                    } else if (n->voice == RS_V_SNARE && n->vel) {
                        int want = (RS_ROWS - 1 - s.bot[1].y) * 7 / (RS_ROWS - 1);
                        snares++, snare_ok += n->midi >= 0 && (n->midi & 7) == want;
                    } else if ((n->voice == RS_V_HIHAT || n->voice == RS_V_EX_CRACKLE) && n->vel && n->midi >= 0) {
                        int len = n->midi >> 3 & 3;
                        hats++, hat_ok += len == (n->dur >= SEC(0.4) ? 3 : n->dur >= SEC(0.1) ? 2 : n->dur >= SEC(0.05) ? 1 : 0);
                        crashes += n->voice == RS_V_EX_CRACKLE && len == 3;
                    }
                }
            }
        }
    printf("band_test: the kit: %ld kicks (%ld tuned to the root, driven), %ld of %ld ones clicked, %ld snares (%ld as high as Echo-3), %ld hats (%ld as long as played), %ld crashes\n",
           kicks, tuned, clicked, ones, snares, snare_ok, hats, hat_ok, crashes);
    CHECK(orig_plain, "ORIGINAL's drums are the HTML's: C1, the noises");
    CHECK(kicks > 100 && tuned == kicks, "every kick tuned to the chord's root and driven (%ld of %ld)", tuned, kicks);
    CHECK(ones > 20 && clicked == ones, "the one's kick clicks (%ld of %ld)", clicked, ones);
    CHECK(long_garden && tight_maze, "GARDEN's kick booms long, MAZE's is tight");
    CHECK(snares > 50 && snare_ok == snares, "the snare's body as high as Echo-3 stands (%ld of %ld)", snare_ok, snares);
    CHECK(hats > 100 && hat_ok == hats && crashes > 5, "the hats as long as the drummer plays them, crashes on their own voice (%ld of %ld, %ld)", hat_ok, hats, crashes);
}

int main(void)
{
    test_balance();
    test_lines();
    test_tunes();
    test_peaks();
    test_tiles();
    test_arp_rules();
    test_compose();
    test_follow_chain();
    test_in_scale();
    test_harmony();
    test_drums();
    test_kit();
    printf("band_test: %s\n", fails ? "FAILED" : "all pass");
    return fails != 0;
}
