# Roadmap

One phase at a time; the current phase is the first unchecked one. Each item lands as its own commit and test.
Built on what SkurkyMello's port learned (docs/roboseq/DECISIONS.md D5).

## Playability (P1-P4): the rules every phase is checked against

The product owner, 2026-11-05: *RoboSeq must have an element of playability, even if it is unorthodox.* On the
FM-1 RoboSeq is an instrument to play while it runs, not only a programmer to fill in and listen to.

- **P1. Something to play at every moment.** While the transport runs, the keys always do something musical or
  steer the bots, on every page. No page is only an editor.
- **P2. The bots can be steered live.** A player can reach into the board while it plays: nudge a bot, turn it,
  make it dash or ping, drop or move a tile, swap the registers. How exactly is decided in phase 3 (open below).
- **P3. Unorthodox is allowed, unclear is not.** The way of playing may be unusual (a game more than a keyboard),
  but the screen always shows what a key will do, and the result is heard at once (or on the next step, shown).
- **P4. A rule is a test.** Once decided, P1-P3 hold page by page in host tests, so a new page cannot break them.

Decided 2026-11-06 (D15): the robots are the instrument; playing is manipulating them and their registers (the
movement sequencers that decide the music, as in the HTML); a melody layer of its own may come later. Ideas to try on the host before deciding: the white keys as a live joystick
for one bot (overriding its next step, the program resumes after); "drive mode" where a bot is steered by hand and
its moves are recorded into its register; tiles placed and turned on the 5 x 5 board while it plays; hazards as
performance (trigger a zap or a pulse pad by hand); a "chase" of the two bots as a game with a score that changes the
music.

## Phase 0: the foundation (2026-11-05)

- [x] Dinghy 0.4 imported unchanged (`22acb91`), then RoboSeq's name and identity: `roboseq.c`, `ROBOSEQ`,
      `FM-1_960`, store magic `RSEQ`, the loader's screen in RoboSeq mint
- [x] `reference/roboseq.html` as the reference (SHA-256 in reference/README.md), the April handoff beside it
- [x] CLAUDE.md, README.md, PLATFORM.md, DECISIONS.md, this roadmap
- [x] Makefile (`make test`, `make host`, `make fw`), CI on every push (zvenson/fm1-build, like SkurkyMello)
- [x] The first CI run green (2026-11-05, `6b78aa4`; its package byte-identical to `make fw`), and that `.fwsc`
      (still Dinghy's sine) boots in fm1-emu 0.4.0 under FM1_STRICT: ROBOSEQ on the screen, a key plays (`make emu`)
- [ ] Check `FM-1_96N` against the Firmware Hub's list (not verifiable from its public page, 2026-11-05)
- [ ] RoboSeq's own logo for `assets/` (Dinghy's is designburg's, not under the GPL)

## Phase 1: model and simulation (host only)

The HTML's game, bit for bit, in C with host tests; no sound and no screen yet.

- [x] `firmware/src/rs_model.c`: the state, the boards and their presets, the actions, collisions, tiles, heat,
      death and respawn, the love check, the loop reset, Voice Mod, seq 2, the drum presets (2026-11-05)
- [x] `rs_chance.c`: seededChance without float (DECISIONS.md D6)
- [x] Golden traces: `tools/trace` runs the HTML's own script in Node (no browser), 33 scenarios (every board,
      24 random ones over the loop lengths, tpm 1/2/4, Voice Mod rates; seq 2; deaths), `make test` compares every
      tick: all equal, and a deliberate bug in the model fails them
- [x] The model on the chip: built into the firmware, its fingerprint at boot read back from fm1-emu equals the
      host's (`make emu`)
- [x] The randomizers (registers, board and its density, Seq 1, seq 2 and its rolls of the registers), the user
      board, the HTML's edits (loop and program lengths, the cycles of action, modifier, chance, Seq 1's fields) and
      undo (20), with the firmware's own xorshift32: tests/roboseq/edit_test.c holds the HTML's rules for each
- [ ] Later, with phase 3: the undo ring (12 KB of RAM's 96) into POOL if RAM gets short

- The state: two bots (A at 0,4 facing E; B at 4,0 facing W), programs of up to 64 steps (action, modifier,
  probability 1 / 0.75 / 0.5, repeats), loop lengths 8-64, BPM (80 default), scale and root, ticks per move
- The 5 x 5 board and its tiles: belt (N/E/S/W), zap, rotate L/R, pulse pad, crate; the board presets and the random
  board (density 0.45)
- The step actions: forward, reverse, turn L/R, dash, ping, idle; collisions, death and respawn, tile effects, the
  deterministic loop reset and `seededChance`
- The sequencers around it: Voice Mod (4/8 steps, LOOP / MANUAL, loop frequency), the bass mod, seq2, the drums
  (kick, snare, hihat; 12 or 16 steps; the presets), undo (20)
- Golden traces: the HTML's event log for fixed seeds and programs, rendered headless, compared with C step by step

## Phase 2: sound = HTML

Three layers, each checked against the HTML itself (DECISIONS.md D8). (The April handoff's "chip colour" of
bit-crush, EQ and delay is not in the HTML of 2026-11-05: its chain is the buses, bot B's low-pass, a hall reverb
and the limiter.)

### 2a. The score: which notes, when, how long, how loud (exact, as phase 1)

- [x] `tools/trace`: the HTML's script in Node with a recording `audio`: every `triggerAttackRelease` of every tick
      (voice, note, time, duration, velocity) into the golden traces (2026-11-05)
- [x] `rs_score.c`: playBoardSound (noteFromPosition, heat, unison avoidance, the arps and chords from position),
      playGhostTick (tpm 2 and 4), the event sounds (the first four events, their 10 ms offsets, the doubled reset),
      the drums, the explosion, the coin; integer time (1/256 sample) and velocity (Q15)
- [x] Every note of every scenario equal to the HTML's, within rounding: 90,391 notes in 36 scenarios (three new
      ones for tempo, 1/4 speed and the root's transpose), every voice; deliberate bugs fail them; the score is in
      the chip's fingerprint (8491cc7c on chip and host)

### 2b. The voices: how each one sounds (measured, as SkurkyMello)

- [x] `tools/tone-ref`: the HTML's own `buildAudio()` in headless Chromium (Brave here) under Tone.Offline, Tone
      14.7.77 as the page loads it: each voice alone, the buses, 8 s of the default scenario (`make reference`)
- [x] Find out what the chain really does: the cycle mutes the FX bus and the reverb in the HTML; the product owner
      chose the musical intent: the reverb's return to the master (D9). Tone's same-time restart throws and loses a
      tick: the second note is dropped (D10). Chromium's start phase inside a render quantum: not ported (D11)
- [x] The C voices (`rs_voice.c`): Tone.Synth (PolyBLEP square, triangle), NoiseSynth (white, brown),
      MembraneSynth (kick, thump), Tone's envelope and its call-order cancelling; `make parity`: every voice within
      0.15 dB of the HTML's in level, shape and timbre (the noise voices within 1.1 dB, random on both sides); the
      voices are in the chip's fingerprint (150e92e1 on chip and host)

### 2c. The mix and the load

- [x] The buses at DEFAULT_MIXER's levels, bot B's 1400 Hz low-pass (Chromium's biquad, to 4e-9), the reverb's
      send and its return to the master (D9), the limiter (`rs_mix.c`, its tables from `tools/tone-ref/mix_tables.py`);
      each voice monophonic as Tone's Synths (2026-11-06)
- [x] Each piece measured in Chromium on its own (`make probe`): the limiter is Chromium's compressor step for step
      (`compressor.py`, within 0.05 dB of Chromium; the C within 0.1 dB of it, D14), the reverb an 8-line network
      with Tone's decay and energy (D13); `make parity`: the bus chain and 8 s of the default song within what the
      HTML is to itself with another seed (the song's level within 0.02 dB, timbre 0.27 dB); deliberate faults (no
      reverb, no low-pass, no makeup gain, a wrong level, a wrong send) fail it
- [x] In the audio interrupt: the game 64 ms ahead in the main loop, a step's notes at once through a ring (D12);
      PLAY starts and stops it (the HTML's panic release); every note of all 36 scenarios played into the voices
      without a full timeline (14 of 24 at most)
- [x] The load in fm1-emu under FM1_STRICT: the worst block 1305 us of 5805 on the default song, 1828 us with all
      sixteen voices at once over it (`make emu` fails at half the budget); the voices render a run between events,
      the reverb unrolled (from 2423 us)

## Phase 3: playing it (controls and screen)

- [x] The control rules (docs/roboseq/CONTROLS.md, C1-C6) and the playing model (D15, D16), tried on the host
      (`make play`)
- [x] HOME is the board: the HTML's renderBoard() as 240 x 240 pixel art (its tile and robot art 1:1 on cells of
      40 px), the tiles an event lit, the explosion, heart and clash star for as long as the HTML shows them, the step,
      the cycle and the bots' health; drawn as the step is heard, not when the game computes it (2026-11-06)
- [x] The board's art our own (tools/art): tiles, robots and step icons at 1 px, lit from above, the symmetric
      exactly so; each bot on its own side of a tile at one size (D17), the clash star or the heart over them
      (`make scenes`)
- [x] The registers on the white keys (SEQ: whose, OCT: the next 16), the bots steered on the black keys (D16);
      SELECT: a step's action, modifier or chance; KNOB1: the register's length (C7, C8)
- [ ] P4: the rules as host tests, page by page
- [x] The pages on the buttons (D19); SEQ, the sequence: both registers as one list that glides (KNOB2, OCT)
- [x] GLO: tempo, ticks per move, the loop's length as the loop comes round (D20), 1/4 speed
- [x] SCL: Voice Mod's four steps as cards, a step's scale, root and semitone on the knobs
- [x] ARP: Seq 2's four steps as cards, a step's board, drums and swap on the knobs
- [x] LFO: the drum machine, a row on the white keys, the patterns on KNOB1
- [x] FX: the mix as six faders, the levels gliding as the HTML's rampTo
- [x] ENV: the boards, now and queued, the presets, reroll, the user board, reset (2026-11-06)
- [ ] The UI map generated from the code (`make screens`)
- [ ] Storage: projects in Dinghy's blobs and slots

## Phase 4: on the emulator, then hardware

- fm1-emu: load, stacks, the worst block under half the budget
- The first release `roboseq-0.1.fwsc`; FIRST_FLASH.md for someone with an FM-1
