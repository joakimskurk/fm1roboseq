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

## D18. No badge over a steered bot: the board stays lovely chaos (2026-11-06)

The product owner: the steps often run fast, and this is meant to be a bit of lovely chaos; an extra icon over the
robots is not needed. So a steer shows only in what the bot does on its next move, heard and seen there (C5),
not as a badge beforehand. The facing over a bot's head stays (the HTML's dirArrow).

## D19. The pages on the FM-1's buttons, every part of the HTML on one (2026-11-06)

The product owner: all of the HTML's parts come over, made for the FM-1 ("FM-1-ified"), and the page with the
whole sequence scrolls fully. The pages and their buttons: HOME the board; SEQ the sequence, both registers as one
list of up to 64 steps that glides (KNOB2 a row, OCT- / OCT+ sixteen; it follows the step playing three seconds
after the player let go); SCL Voice Mod (Seq 1); ARP Seq 2; LFO the drums; FX the mix; ENV the boards; GLO the
global settings (tempo, the loop's length, ticks per move, 1/4 speed). EDIT picks whose register is on the white
keys, the same on every page (it was SEQ before SEQ became a page). The black keys steer the bots on every page
(C3), PLAY is the transport (C1), REC stays free for recording.

## D20. The loop's length changes as the loop comes round, not by stopping (2026-11-06)

The HTML changes the loop's length only by stopping and resetting (setLoopLength calls stopPlayback). On the FM-1
the knobs never stop the music (C8), so GLO's KNOB3 picks the next length, shown beside the current one, and it
lands just after the next loop reset (rs_set_loop_len_live: setLoopLength less its stop and reset; the registers
capped, the drums re-laid when their step count changes). Stopped, it changes at once, as the HTML's does. The
HTML's scale button is left out: its notes follow Voice Mod's scales, not that one. Its 1/4 speed, in its state but
without a button, is KNOB4.

## D21. A lead to play solo over the robots, on REC (2026-11-06)

The product owner wanted to play solo on the keys over the robots with a typical SID lead, and chose REC to switch
the keys to it. Not the HTML's (it has no keyboard voice): a lead of our own (rs_lead.c), monophonic as a SID voice,
the last key held winning and a glide (~45 ms) when played legato; a pulse wave whose width sweeps 20-80 % at
0.7 Hz; a vibrato of +-1/4 semitone at 5.5 Hz coming in after 250 ms held; a resonant two-pole low-pass opening
with each note (600 Hz + the note's own pitch + up to 3 kHz, closing over ~150 ms); 4 ms attack, decay to 3/4,
200 ms release. It has its own level on the mix page (0.45, so it sits over the robots without hiding them) and
goes to the reverb as the melody does. With REC lit all 27 keys play it from F3 and OCT- / OCT+ shift it +-2
octaves; the white keys no longer edit and the black keys no longer steer until REC again (the lead is what is
played then: P1). LEAD and its octave show on every page. REC's later job, recording, can record this.

## D22. A half speed between the HTML's normal and quarter (2026-11-06)

The product owner: with 2 or 4 ticks per move it gets very fast; 1/2 works better with the ticks. The HTML has
only a playbackRate of 1 or 0.25. GLO's KNOB4 now steps 1/1, 1/2, 1/4 (the model's `quarter`: 0, 2, 1; 1 stays the
HTML's quarter, so the traces are unchanged), and every step's length follows it as it follows the HTML's quarter.

## D23. The lead, sharper and further back, with its echo (2026-11-06)

The product owner, on D21's lead: more open, a little lower in the mix, more delay, a real sharp SID lead with some
folding and PWM. So: the pulse and the saw together (the SID's combined waves, more or less), the pulse's width
sweeping 10-90 % at 1.6 Hz, folded back where it is driven past its edge (drive 1.35, more on each attack); the
filter open (1.8 kHz plus the note's pitch, up to 4.5 kHz more on the attack, little resonance); a stereo
ping-pong delay on the lead alone, three steps long (a dotted eighth of the playing tempo, halved until it fits
0.74 s), feedback 0.45 through a darker one-pole, 0.4 of it heard, gliding to a new tempo as tape does; its level
0.6, about 17 dB under full scale alone, 2 dB under D21's.

## D24. The board colours the whole sound, a little (2026-11-06)

The product owner: a parameter by which the board's layout does a tiny bit to the whole sound, so that not
everything is quite the same. rs_mix_colour counts the tiles: an open board makes the reverb's room longer (up to
1.35 times), crates shorter (down to 0.75); pulse pads send more to it (they cool), zap rails less (drier); zaps
and belts tilt the master brighter, pulses warmer (a tilt around 1 kHz, up to +-35 %); zaps and rotators drive it
a little (a soft cubic, up to 0.4). The default board's room and send are the HTML's. FX's KNOB2 sets how far of
the way it goes, 0-100 % (40 % at start); at 0 % the sound is the HTML's exactly (colour_test holds it for every
board). It changes with the board, as the board changes with the loop, gliding there.

## D25. A splash at boot, animated, after the product owner's draft (2026-11-06)

The product owner drew a splash (the RoboSeq lettering in mint with a star over it and three red strokes under,
Scout-7 and Echo-3 either side of a pulse pad and its rings (the board's own pulse tile), a floor, a row of dots, coloured corners) and asked for
it animated; then: the robots closer together with a small, simple heart, "Seq" in yellow, the red strokes gone. rs_splash.c draws it from our art
(tools/art/splash.py's lettering, the robots twice the size): the letters drop in one after the other ("Robo" mint,
"Seq" yellow), the star twinkles, the robots come in from the sides to meet in the middle and bob, a heart beats
over them, the dots fill as it starts. About 2.6 s; any key or button skips it (and is not also a press on the board).
The sound runs meanwhile.

