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

## D12. The game runs 64 ms ahead of the speaker, a step's notes go to the voices at once (2026-11-06)

The HTML schedules with Tone's transport at a look-ahead of 0.25 s (armAudio), which keeps a browser's main thread
from making the music stumble; it also means what a player does is heard up to a quarter second late. The FM-1's
main loop stalls far less (the screen's DMA runs `app_idle`, which keeps scheduling), and steering the bots live is
the point (P2), so the main loop runs rs_tick 64 ms ahead of what the speaker plays, and a stall of a quarter
second (a flash erase) resumes from now instead of rushing. A step's notes are in the HTML's call order, not in
time (an arp's late note before an event's 10 ms one), so the audio interrupt gives a step to the voices whole, just
before it starts, as Tone has every note of a tick from its callback on. Stop is the HTML's: the queued notes go and
every voice is released at once (panicAudioOutputs).

## D13. The hall reverb: Tone's decay and energy, not its 53,361-tap convolution (2026-11-06)

Tone.Reverb({decay 1.2, preDelay .01}) is a ConvolverNode with white noise under exp(-t / 0.1488 s) (Tone's
exponentialApproachValueAtTime), normalized by Chromium to 0.00125893^2 x 53361 of energy a channel: measured
(`make probe`) to the third digit. A convolution of 1.21 s is far beyond the chip, and the impulse is random anyway
(another seed is another reverb), so the port keeps what is heard: a 10 ms pre-delay, four all-passes to make it
dense from the start, an 8-line feedback delay network whose lines lose exp(-length / tau) a pass (the same decay
at every frequency, as white noise has), scaled to the same energy. Its impulse response is held to Tone's in
`make parity`: energy within 0.25 dB, decay within 3 %, the first 25 ms within 1 dB. The HTML's reverb is stereo
(two noises); the FM-1 plays mono, the left channel.

## D14. The limiter is Chromium's compressor, makeup gain and soft knee included (2026-11-06)

Tone.Limiter(-6) is a DynamicsCompressorNode at threshold -6 dB, ratio 20, attack 3 ms, release 10 ms and Tone's
default knee of 30 dB. With that knee it hardly limits: Chromium compresses gently from -6 dB and adds its makeup
gain (+0.18 dB here), so the HTML's song peaks at +3.9 dBFS, which the browser's output then clips. Not "better":
the port runs Chromium's DynamicsCompressorKernel step for step (look-ahead 6 ms, the gain every 32 frames, the
adaptive release, the detector from 0 at the start), its curve and rates as tables, and the platform clips at full
scale as the browser does. `tools/tone-ref/compressor.py` is the kernel in float, within 0.05 dB of Chromium on a
test signal; the C is held to it within 0.15 dB over every 5 ms (the choice between attack and release flips on a
hair at a few points, in Chromium against its own model too).

## D15. The playing model: the robots are the instrument (2026-11-06)

The product owner, 2026-11-06, on how RoboSeq is played on the FM-1: maybe a simple melody with a sequencer over
it, or something like it, later; perhaps it is enough to manipulate the robots; and the robots' movement sequencers
(their registers) decide the music, as in the HTML. So phase 3 starts there: playing is reaching into the robots
and their registers while they run, the music following from where they go, as the HTML's does. A melody layer
of the FM-1's own is not ruled out but waits; it is taken up step by step, once the robots play well.

## D16. The keys on the board: registers on the white keys, steering on the black ones (2026-11-06)

The product owner chose both ways of reaching the robots. The white keys are the 16 steps of one bot's register
(SEQ picks which, OCT- / OCT+ its next 16): a key turns its step to the next action, as the HTML's step card does
(cycleAction). The black keys steer: a key makes that bot's next move this action, once (prob 100 %, its step's
modifier kept, so it sounds like the step it replaces), then its register goes on. Five per bot: TURN L, MOVE,
TURN R (left, ahead, right under the hand), DASH and PING; BACK and WAIT stay in the registers. The eleventh swaps the
registers, as the HTML's REG SWAP. The steer is the player's, not the HTML's: with none the game is the HTML's tick
for tick (the traces hold that). Both registers run down the board's sides, Scout-7's left and Echo-3's right, top to bottom as the white keys
left to right (the product owner, 2026-11-06: one row alone confused); the one on the keys bright. The step framed
is the one the register plays (step mod its
length); the HTML frames the step's number, which is off the card when the register is shorter than the loop.
docs/roboseq/CONTROLS.md has the rules (C1-C6).

## D17. Each bot keeps its side of a tile, at one size (2026-11-06)

The product owner, on two bots meeting on a tile: let them keep their size the whole way, Scout-7 always on the
left of its tile and Echo-3 always on the right, which makes more sense overall. So both are drawn small (18 x 18
at 1 px, the HTML's build), at the foot of the tile, each in its own half, its facing over its head, a steer over it.
Two on one tile is then nothing special: both are there, a dash's clash star over the one hit, the love heart over
the two. (The HTML draws a bot large in its cell and two as minis side by side.)

