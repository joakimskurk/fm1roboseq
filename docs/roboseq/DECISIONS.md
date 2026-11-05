# Decisions

## D1. Dinghy 0.4 as the platform (2026-11-05)

Dinghy 0.4 (`22acb91`), as fm1apsis uses it. 0.3 is the version tested on an FM-1 (SkurkyMello is on it, with
0.4's TRS MIDI IN); 0.4 is tested on the host and in fm1-emu. Chosen for one platform across the user's ports and
its newer MIDI; the hardware risk is known and stays in ROADMAP phase 4.

## D2. The identity FM-1_96N (2026-11-05)

`FM-1_960` for dev builds, `FM-1_96N` for release 0.N. Not in Dinghy's list of taken ones (900, 91N, 93N, 97N, 98N,
99N), not fm1-dock's (921) or the 95N fm1-dock found taken. SkurkyMello and APSIS both carry 970, so RoboSeq does not.
The Firmware Hub's public page shows no identities: to be checked there before the first release.

## D3. The HTML is the reference (2026-11-05)

`reference/roboseq.html` (SHA-256 in reference/README.md) wins over the C code unless a decision here says
otherwise.

## D4. Playability is a requirement (2026-11-05)

The product owner: RoboSeq on the FM-1 must have an element of playability, even if unorthodox. ROADMAP.md holds it
as rules P1-P4; the playing model is chosen in phase 3, after trying it on the host.

## D5. What SkurkyMello taught (2026-11-05)

From joakimskurk/fm1mello and its October 2026 review: keep the repo plain source (no packed layers, one CI
workflow); check the sound against Tone.js renders of the HTML from the first voice instead of tuning by ear (Tone's
envelopes are exponential, its filter Q is in dB, its effects are always in the chain); write the control rules
before the pages; generate the UI map from the code.

## D6. seededChance without float (2026-11-05)

The HTML decides whether a step with probability 0.75 or 0.5 plays with `|sin(seed) * 43758.5453| % 1`, a double
hash, and the FM-1 has no FPU. `rs_chance.c` keeps the hash: the seed as an angle in turns (Q64, each constant / 2 pi,
wrapping uint64 arithmetic is the reduction mod 2 pi, exact for any loop count), |sin| from Taylor series in Q61, the
product with 43758.5453 in 128 bits. Measured against the HTML: 0 of 384,000 decisions differ (both bots, 64 steps,
3000 loops), the largest difference in the chance itself 2.4e-6, which is the HTML's own double noise (ulp(seed) x
43758). Beyond ~10^6 loops (months of play) that noise reaches 1e-3 and a step close to a threshold may go the other
way; accepted. `tests/golden/chance.txt` holds the HTML's classes for 1024 loops.

## D7. What the model keeps of the HTML's board state (2026-11-05)

`rs_model.c` keeps everything that changes what plays: positions, headings, heat, deaths, dying, the loop and step
counters, Voice Mod and seq 2, the drums, the clash cooldown and the love count, the events of each tick in their
order. It leaves out what only draws or explains: the status strings, the event labels, `activeKeys`, the trails,
explosions, hearts and clashes lists, `hiddenUntil` (a 600 ms fade after a respawn, from `Date.now()`); phase 3 draws
those from the events. The HTML's quirks are kept: a dash moves twice even when the first move was stopped, a zap
caps heat at 6 (and so lowers heat a dash pushed above it), pings and dashes add heat without a cap, every seq 2 step
sets the register swap, the reset tick of a tpm > 1 loop also plays a ghost tick (phase 0). Seq 1 has 4 steps in this HTML
(`restoreBassMod` always makes 4; the April handoff's 4/8 is older); the model keeps room for 8 but nothing sets it.
In the score: the reset sound plays twice on a loop reset (at once, and again among the events), a death keeps its
place among the first four events without sounding there, and bot B's board sound computes a chord it never plays
(its complement shape shows only in the tpm-4 ghost ticks).

## D8. Sound in three layers, each against the HTML (2026-11-05)

What the HTML plays is mostly logic: where the bots stand, their heat, the tile and the step decide the notes, the
arps, the ghost ticks and the event sounds. That part is exact and is checked like phase 1, note by note against the
HTML's own script run in Node with a recording `audio` (2a). How each note sounds (Tone's oscillators, envelopes,
noise, membrane, filter, reverb, limiter) is measured against Tone.Offline renders of the HTML's own `buildAudio()`
in a headless Chromium, with tolerances (2b, 2c), as SkurkyMello learned to. So a difference in the music can't
hide behind a difference in timbre, and the reverse.

## D9. The FX bus and the reverb sound, as they were meant to (2026-11-06)

In the HTML the reverb's return goes into the FX bus, and the FX bus sends into the reverb: fxGain -> reverb send ->
reverb -> fxGain, a cycle without a DelayNode, which Web Audio silences. Measured with the HTML's own buildAudio()
under Tone.Offline (tools/tone-ref/probe_chain.mjs): the collision, rotator, pulse, the reset jingle and the coin
give no sound at all, and no bus has any reverb. With the return to the master instead, they sound (collision
-24 dBFS, coin -23 dBFS) and the melody gets its tail. The product owner, 2026-11-06: make it as it should have been,
musically. So the port's reference is the HTML with that one connection changed (`hallReverb.connect(masterGain)`,
UNCYCLED in tools/tone-ref); everything else stays the HTML's.

## D10. A voice started twice at one time: the second note goes, the tick goes on (2026-11-06)

Tone.js throws when a source is started again at the very time it already starts at ("Start time must be strictly
greater than previous start time", Source.start), and in the HTML that exception ends advanceSequence for that
tick. It happens on every loop reset (playEventSound("reset") runs at once and again among the events: the second
jingle throws, so the page plays the jingle once) and when both bots burn out in one tick (the second explosion
throws, and the page then loses the rest of the tick: the bots' steps, the drums, even moveTick's count). The
first is what the page plays and sounds right; the second is a crash. So, as D9, as meant: a voice's second start
at a time it already starts at in that tick is dropped (rs_score.c `note()`, the recorder in tools/trace, the
renders in tools/tone-ref), and the tick always runs to its end.

## D11. A note starts its oscillator at phase 0 (2026-11-06)

Web Audio's oscillators start at phase 0, and so does a Tone voice on every attack (a new OscillatorNode). In
Chromium a start inside a 128-frame render quantum begins at a phase that depends on where in the quantum and on the
frequency before it (the first note's: Tone's default 440 Hz); on a quantum's edge it is 0. That is Chromium, not
Tone or the HTML, and Firefox differs. The C voices start at phase 0; tools/tone-ref puts its voice stimuli's
starts on quantum edges, so the golden renders show the voices without the artefact (compare.py snaps the same way).
Whole songs (2c) keep the HTML's own times and with them the artefact, which their tolerances take in.
