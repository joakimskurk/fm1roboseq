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

Open, for phase 3: the playing model. Ideas to try on the host before deciding: the white keys as a live joystick
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
- [ ] The first CI run green; the first `.fwsc` (still Dinghy's sine) boots in fm1-emu with RoboSeq's identity
- [ ] Check `FM-1_96N` against the Firmware Hub's list (not verifiable from its public page, 2026-11-05)
- [ ] RoboSeq's own logo for `assets/` (Dinghy's is designburg's, not under the GPL)

## Phase 1: model and simulation (host only)

The HTML's game, bit for bit, in C with host tests; no sound and no screen yet.

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

- `tools/tone-ref`: render the HTML's voices with Tone.Offline (as SkurkyMello does) into `tests/golden/html`
- The voices: lead and bass (Tone.Synth), the event one-shots (NoiseSynth, MembraneSynth), the drums; the chain:
  the chip colour (bit-crush, EQ, delay), the buses (melody, bot B, percussion, FX, drums), reverb, limiter, master
- Arp and chords from position (`noteFromPosition`, `buildArpChord`, the arp patterns), ghost ticks
- `make parity`: the C engine against the renders, with tolerances in the tests; the audio's load measured

## Phase 3: playing it (controls and screen)

- First the control rules (as SkurkyMello's G1-G8) and the playing model (P2 above), tried on the host
- HOME is the board: 240 x 240 pixel art, the bots, the tiles, the step playing; the registers on the white keys
- The pages for programs, Voice Mod, drums, mix; the UI map generated from the code (`make screens`)
- Storage: projects in Dinghy's blobs and slots

## Phase 4: on the emulator, then hardware

- fm1-emu: load, stacks, the worst block under half the budget
- The first release `roboseq-0.1.fwsc`; FIRST_FLASH.md for someone with an FM-1
