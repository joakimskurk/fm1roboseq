# Decisions

## D1. Dinghy 0.4 as the platform (2026-11-05)

Dinghy 0.4 (`22acb91`), as fm1apsis uses it. 0.3 is the version tested on an FM-1 (SkurkyMello is on it, with
0.4's TRS MIDI IN); 0.4 is tested on the host and in fm1-emu. Chosen for one platform across the user's ports and
its newer MIDI; the hardware risk is known and stays in ROADMAP phase 4.

## D2. The identity FM-1_96N (2026-11-05)

`FM-1_960` for dev builds, `FM-1_96N` for release 0.N. Not in Dinghy's list of taken ones (900, 91N, 93N, 97N, 98N,
99N), not fm1-dock's (921) or the 95N fm1-dock found taken. SkurkyMello and APSIS both carry 970, so RoboSeq does not.
The Firmware Hub's public page shows no identities: to be checked there before the first release. (Superseded by
D31: 960 is taken.)

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

## D26. With REC on, the knobs play the lead too (2026-11-06)

The product owner: in the lead's mode K1-K4 become performance controls for it: the filter's cutoff, the delay's
intensity, an arp up or down by where the knob is turned, in time and in the scale, and one more that makes sense.
So, on every page while REC is lit: K1 the cutoff (a quarter to 2.5 times D23's, the middle as before); K2 the echo
(heard 0-0.8, fed back 0.25-0.7); K3 the arp, off in the middle, turned right it climbs, left it falls, further
more notes (2-7), in thirds of the scale Voice Mod is on (its root under it), one note each step the robots take
(the main loop hands the lead each step's time), the filter plucked on each, the key's own note again when off;
K4 the bite, the one more: how hard the wave is folded and how wide the PWM swings. While REC is on the four are
faders in the page's own header line (between the registers on the board), their names on them in white over a
muted teal fill and a dark empty part, so nothing lies over the board (the product owner: no overlapping; the LEAD
badge went with it, REC's light says it). The pages' own knobs wait while REC is on.

## D27. ARP: the keys a chord, arpeggiated in time; Seq 2 behind SCL (2026-11-06)

The product owner: with an ARP button, toggling it should put the keys in an arp mode where chords are held, a knob
turns the arp's kind, the button lit while it is on; and the arp's adjustment on K4. So ARP now toggles that mode
(lit; it brings the lead with it, REC off takes both away): the keys held, low to high (up to eight), are played a
note each step the robots take, as K4 lays them: up, down, up and down, up over two octaves, down over two, random
(never the same note twice running). Without ARP, K4 is D26's arp in the scale from one key. The knobs are now K1
cut, K2 echo, K3 bite, K4 arp. Seq 2, which had the ARP button, is SCL pressed again: SCL toggles Seq 1 (Voice
Mod) and Seq 2, the HTML's two small sequencers side by side (D19 amended).

## D28. The builders: the sequence and the board, built a game's way with WASD (2026-11-06)

The product owner: the movement sequencer more as in the HTML, scrolled up and down with SELECT; a custom board
built with the first keys as WASD and a key for each kind of tile further up, more like a game; the same way for
the movement sequencer. Thought through, as follows.

SEQ is the HTML's Program Registers: Scout-7's register and Echo-3's as two columns of step cards (the step's
number, its action as icon and word in the action's colour, its modifier, its chance in the HTML's probColor; the
card a register plays bordered in its bot's colour, as `.step-card.current`), eight on the screen. SELECT moves a
cursor on the picked register (EDIT picks it) and the list glides after it; OCT- / OCT+ move it eight. The keys type
the program in: A W S D (F3 F#3 G3 A3, the black key W over the white A S D as on a computer's keys) turn left,
move, back, turn right; G#3 wait, A#3 dash, B3 ping; each action lands at the cursor and the cursor goes on, so a
register is driven in. C4-G4 set the step's modifier, A4 B4 C5 its chance; C#5 undoes, D5 rolls the picked
register, E5 both, F5 swaps the registers, G5 clears them (the HTML's planner buttons). KNOB1 its length.

ENV builds the board: the board as it plays with a cursor, A W S D move it; G#3 (between S and D) clears the
tile; C4 a belt (it points the way the cursor last moved; on a belt it turns it), D4 E4 the rotators, F4 a zap,
G4 a pulse pad, A4 a crate; C5 rerolls, D5 the user board, E5 saves the board as the user's, F5 clears that, G5
the bots back to their pads. KNOB1 runs through the presets (queued while playing), KNOB2 a reroll's fullness. A
tile placed changes the board at once, so the robots meet it on their next step; the HTML has no tile editor, so
this is ours (rs_place_tile).

On both pages a little keyboard at the foot shows each key in the colour of what it does (P3). On these two the
keys are the builder's, so the black keys do not steer there (C3 now: everywhere else). Building while the robots
play is playing: it is heard on the next step or loop (P1).

## D29. PLAY flashes the beat; the arp goes faster on SELECT (2026-11-06)

The product owner: PLAY should flash the tempo; in the lead's mode the arp should go faster still. PLAY's light now
flashes 100 ms on every beat as it is heard (every fourth move: every 4 x ticks-per-move steps, so it follows the
tempo, the speed and the ticks), and is dark when stopped. While the lead plays SELECT has nothing else to do (the
keys are the lead's), so it sets the arp's notes a step, 1 to 4 (sixteenths, thirty-seconds, triplets of them,
sixty-fourths at the robots' tempo): the main loop hands the lead the step's length cut in that many, and after each
step the lead plays the rest as their times come. The rate shows small on the ARP fader.

## D30. Only ARP arpeggiates; K4 is the octave without it (2026-11-06)

The product owner: with D29 the arp could run without the ARP button lit, out of key, and its speed should be one of
ARP's own settings. The arp without ARP was D26's scale arp on K4: thirds of Voice Mod's scale from the key held,
which needn't be the key the player is in, and SELECT made it faster. Now only ARP arpeggiates (the chord held, its
notes the player's own), and its settings are ARP's: K4 its kind, SELECT its notes a step (1-4, shown on the fader
only while ARP is lit; outside ARP, SELECT does what the page has it do and the lead plays a note a step). Without
ARP, K4 is the lead's octave, -2 .. +2 as OCT- / OCT+ (the product owner's first wish for K4 was "octave, or an arp
up or down"), its fader from the middle; and an octave moved, by the knob or the buttons, takes the key held with it
at once, gliding there, rather than on the next key. D26's scale arp is gone.

## D31. The identity FM-1_965, one for every build (2026-11-06)

The Firmware Hub's catalogue (thefirmwarehub.com/catalog.json, read 2026-11-06) shows D2's `FM-1_960` taken by
SID-1 (putchdev) and matched by Makohai, and release 0.N's `FM-1_96N` mostly taken too (961 FOMAJAM, 964 SIDOU, 966
GR!MM, 968 RUPTURE, 969 Demake and Kitten Synths). With a shared identity the hub cannot tell which firmware an
FM-1 runs. The product owner chose `FM-1_965`: free there, and of the form `FM-1_9NN` the hub and the web installers
resume an interrupted install for. One identity for every build, dev and release, as the hub allows ("same asset
name and same identity is fine"): the version is only in the text (`--release X.Y`), so a release can never drift
onto another firmware's number. The SMK-37 packages follow: `SMK-37 Pro_965`, `SMK-37 Elite_965`, `MKE-P37_965`.

The text on the FM-1 (2026-11-07, before 0.1): RoboSeq had dropped Dinghy's version on the screen, so a release and
any other build looked the same on the device (the 0.1 package was byte for byte the dev one). The version is now in
the splash's corner by the dots, dim: `v0.1` for a release, `v0.0` for a build that is not one, `dev` on the host.
The README's screens and the Hub's card are drawn without it.

## D32. ENV shows every board; pressed again it builds one (2026-11-06)

The product owner missed the overview of all the boards (phase 3's ENV page, gone with D28's builder, which left
the presets on KNOB1 and only the playing board's name over the board: while playing, a preset turned in waited for
the loop with nothing on the screen saying which). ENV now opens the boards, as the HTML's Battle Board panel lists
them: the six presets, a reroll and the user board, each a card with its white key and its tiles small (the tile art
at a quarter, every 4th pixel), the one playing framed in mint (with its edits, * when edited), the one coming with
the loop in yellow and blinking (the HTML's pending), a reroll shown once rolled; the ninth card the keys 9 SAVE, 10
CLEAR the user board, 11 RESET (the bots to their pads), as phase 3's page had them. The black keys steer there (it
is not a builder: P1, C3). ENV pressed again is the builder, as SCL pressed again is Seq 2; KNOB1 and KNOB2 do the
same on both, and the builder's header names the board coming ("> SPINNER", yellow) while it waits. `rs_preset_board()` gives a preset's tiles to look at, without loading it (edit_test holds that they
are the ones loading gives).

## D33. PLUS: RoboSeq's own music on top of the HTML's, CLASSIC one turn away (2026-11-06)

The product owner loves what plays when the app starts (DEFAULT and MAZE most) and wants more musicality and more
variation, harmonically, in the sound and in the drums, and a much smarter random for Voice Mod, with the old system
kept as a backup. So the music has two modes, on GLO's SELECT (shown at the top right): **CLASSIC** is the HTML's,
note for note as the traces and the parity hold it (rs_default's, so every HTML test runs on it), and **PLUS**,
where the app starts, adds RoboSeq's own on top of the same game: the robots still decide the music (D15), PLUS
only changes how what they play is voiced, accented and accompanied. Each part of PLUS is its own commit and says
here what it does. The selfcheck fingerprint is CLASSIC's.

**D33a. Voice Mod's roll (SCL's 16) in PLUS: a progression in a key.** The HTML's roll draws each step's scale and
root alone. In the HTML every note is quantized to the step's scale on C (scaleQuantize works on pitch classes from
C), and the root only shifts the board's degrees before that; so a key is one scale for all the steps, and a chord is
a root on one of the scale's notes (the default song's Eb C F A major sounds Dm C F Am: ii I IV vi of C). PLUS's
roll draws a scale (major most, then minor, dorian, mixolydian and the two pentatonics) and, four times in five, one
of its progressions (major: I V vi IV, vi IV I V, I vi IV V, I IV vi V, ii V I vi, IV V iii vi, I iii IV V ...;
minor: i VI III VII, i iv v i, i VII VI VII, i VI VII i ...; the dorian and mixolydian vamps; pentatonic ones), each
root a note of the scale; the fifth time the default song's kind (one scale, the roots moving by thirds and
fourths). An 8-step Voice Mod gets a second progression after it. ROOT is switched on. SCL in PLUS also turns
lydian, phrygian and minor pentatonic. (Its first version, 9c994b2, changed the scale with every chord: see D33e.)

**D33b. The harmony, the bass and the pulse in PLUS.** (Superseded by D34: PLUS no longer rewrites the HTML's notes.) After the HTML's score of a tick, PLUS voices it on the
chord the step really sounds (D33a: its scale's triad on the scale's note nearest the root; rs_score_plus; the game
itself is untouched, plus_test holds it tick for tick): on the beats (every 4th step of a 16-step bar, 3rd of a 12)
Scout-7's and Echo-3's notes land on the chord's nearest 1, 3 or 5 (down first); in between they move as the board
has them. Echo-3's sub becomes a bass under them, G1 to E3: the root on the one (the loop's reset tick, where the
bots stand at their pads and only the drums played: now the new chord's root lands with the kick), the fifth on
three, a chord tone on the other beats, and on the step before Voice Mod changes a scale step into the next root
(the nearer one that does not rub the bots' notes, else the root itself); when Echo-3 sits out the one or three
the bass plays anyway. Nothing struck together rubs: where the HTML's harmony puts Echo-3 a semitone, a major
seventh or a tritone from Scout-7, Echo-3 moves a scale step (or two) off it, and the bass off both, on the moves
and on the ghost ticks between them. And the bar breathes: the one 1.18, three 1.14, the other beats 1.08, the
off-sixteenths 0.82 of their velocity.

**D33c. The drums in PLUS: the player's pattern, played by a drummer.** The pattern stays what the player set (a
preset, their own, or CLEAR: then PLUS adds nothing, nor with the drums off); PLUS plays it with dynamics and
variation over a phrase, one Voice Mod round (its steps times how often it moves; 4 loops when manual or shorter):
the kick leans on one and three, the snare's backbeat over its other hits, the hats strong on the beats, weaker on
the eighths, weakest between, each hit a hair different. A ghost snare before a backbeat now and then; every other
loop a kick pushing into four. In the phrase's second half (B) the sixteenth hats fill in (also when the bots run
hot) and the hat opens on four's and. The phrase's last bar ends in a fill, its last four steps without hats: a
rising snare roll, or kick-snare-kick-snare; the next phrase starts with a crash (the hat's noise, long). Each
choice is a hash of the loop and the step, so the same loop sounds the same each time, and nothing is drawn from
the game's random (the game stays the HTML's).

**D33d. The sound over the song in PLUS.** (The answer in B is D34's Echo-3 now.) While it plays, each Voice Mod step gives the board's colour (D24) its
own shade (0.84 to 1.22 of FX's amount) and section B adds 0.45 more, so the room, the send, the tilt and the drive
breathe with the harmony and open up in the phrase's second half (they glide there, as D24's do). In B Scout-7 is
answered on the and of each beat: its note a fifth up in its scale, half a step later, a third as loud, on its ping
voice. (A swing was tried in thought and left out: the lead's arp steps with the straight grid, and the robots'
tight feel is the HTML's.)

**D33e. What went wrong in PLUS's first version (2026-11-07).** The product owner heard many wrong notes and
dissonance in the first PLUS renders. The cause: D33a and D33b took Voice Mod's root as the chord's pitch, so the
bass played Eb and Bb under melodies the HTML had quantized to C major, the beats were pulled to those notes, and
the roll changed the scale (on C) with every chord. 8.3 % of PLUS's pitched notes were outside their step's scale.
Now every root, chord tone, bass note and lead-in is a note of the step's scale as the HTML quantizes it, and the
answer in B is a third where a fifth up would be a tritone. plus_test holds both the scale and the rubs, over 24
setups (boards, rolled progressions of 4 and 8, ticks per move 1, 2 and 4, a 12-step loop): 0 notes outside the
scale, and 0 of 54,223 notes struck together a semitone, a major seventh or a tritone apart (CLASSIC's own: 686).

## D34. PLUS writes its own music from the game (2026-11-07)

(Its lead, its second voice and its bass are D35's now; the rest stands.)

The product owner, on the corrected D33b: a little better, but held back by keeping the old; a new algorithm, with
the sound kept. So PLUS no longer voices the HTML's notes: for the bots' four voices (Scout-7's main and ping,
Echo-3's FM and sub) the HTML's score is dropped and rs_score_plus writes the music anew from the same game, each
bot a musician (the voices, the mix, the drums of D33c, the room of D33d and Voice Mod's roll of D33a stay):

- **Scout-7 sings the line.** Where it stands is where the line goes: x a degree from the chord's root, the rows the
  register (the top higher), in the octave nearest its last note (a cell is a scale step, a row no octave leap). A
  move or a back goes there, a dash leaps there (louder, longer), a turn is the neighbour below or above (short), a
  ping repeats the note with a glint an octave up, a wait rests. On the beats it lands on the chord's tones. In a
  phrase's first half (A) it sings on the eighths (a dash anywhere), in B on every step: verse and chorus. Riding a
  belt the chord rolls up or down after it; a pulse pad holds the note longer; BRIGHT, LOW and GLITCH colour it.
- **Echo-3 is the second voice.** While Scout-7 sings it holds the harmony on the beats, a third or a sixth under
  it (by its column); when Scout-7 rests (a wait, a hesitation, death) it answers with a line of its own, walking
  to where it stands, an octave lower; in A once a beat besides.
- **The bass walks.** The root on the one (the reset tick, with the kick), the fifth on three, on the other beats
  the root, third or fifth by Echo-3's column (up the octave when it dashes), on the eighths between, while Echo-3
  moves, a scale step towards the next; before Voice Mod changes a scale step into the next root; when Echo-3 stands
  still the beat's note holds.
- **The ghost ticks** (2 or 4 ticks a move) glint softly on the and. **The tiles' and events' pitches** (the
  rotator, the pulse pad, the reset's chime, the collision, the coin, the thump) are moved to the nearest chord tone,
  so they are in key in every scale.

plus_test holds it: the game the same as CLASSIC's tick for tick, 0 notes outside the scale and 0 rubs over 24
setups, chord tones on the beats and the root on the one, Scout-7's line 2.1 to 3.3 semitones a note on the six
boards (the HTML's 1.4 to 4.4) with no leap past an octave but a dash's, A sparser than B, a bass on the beats.

## D35. PLUS: tunes that come back, a groove a board (2026-11-07)

The product owner, on D34: a step the right way, but the bass much the same on every board, and the melody still
sounds random. The piano roll said why: Scout-7 struck a new note on nearly every sixteenth, from positions that
jump, voice-led to the nearest octave so a bar never quite came back; and every board had the same walking bass. So:

- **The board is a keyboard.** A cell is a degree of the key: x plus the rows up, from E4 (Scout-7) or E3 (Echo-3).
  A step on the board is a step in the scale, a dash a third. The bots go back to their pads every loop and walk
  the same way again, so what they sing comes back every bar: a tune.
- **Its rhythm is when they move.** A note starts where a bot gets to a new cell (a ping strikes it again); it
  holds while the bot turns or pushes at a wall, to its register's next wait (a wait is a rest), or the next note
  (the voices are monophonic). In A Scout-7's notes start on the eighths and on dashes, in B on every step.
- **On the beats, the chord.** A note on a beat is the chord's tone nearest the way the tune goes (so the contour
  stays); a new cell is always a new note; a note off the beat still sounding on one resolves there, a step to the
  chord. In the bar, no leap past a sixth (the octave nearer).
- **Call and answer.** In A, Scout-7 sings three beats and Echo-3 the last: Scout-7's first notes again, as they
  went, from Echo-3's cell (a fugue's answer); before that Echo-3 sounds on the beats where it gets to a new cell.
  In B Echo-3 sings with Scout-7, the chord's tone a third or a sixth under, and on its own in Scout-7's gaps.
- **Cadences.** The bar half way through a phrase ends on the chord's fifth, its last bar on the root: from half
  the bar on, Scout-7 lands there (moved or not) and holds; the drummer's fill plays under it.
- **A groove a board.** The bass plays the board's own bar: DEFAULT three-three-two, CORRIDOR straight eighths,
  GARDEN a walk in quarters, GAUNTLET a gallop, SPINNER octaves, MAZE syncopated round the corners; a rolled or the
  user's board one of those or dub or half time, by its tiles. Root, fifth, octave, third and a step into the next
  chord's root. Echo-3 plays into it: its dash an octave up, its ping a double, its wait a rest off the beat.
  Changing the board (ENV) changes the groove as the loop comes round.

The ghost ticks' glint, the belt's roll, the ping's glint, the tuned tiles, the drummer, the room and the composer
stay. plus_test holds it: the same bar a phrase later the same tune (417 of 423 notes), Echo-3's answers as
Scout-7's calls went (45 of 45), every phrase home on the root (30 of 30), six boards six grooves, the bass on
every bar's one, Scout-7 2.4 to 3.2 semitones a note in a bar (no leap past an octave but a dash's), A sparser
than B, and as before 0 notes outside the scale, 0 rubs, chord tones on the beats, the game CLASSIC's.

## D36. PLUS: the board in the synths, peaks, phrase ends (2026-11-07)

The product owner, on D35: much better; the board's layout should say something about the synths too, a little;
better still, and now and then the intensity the original had (PLUS sits in between); the demos should roll the
movement registers a couple of times.

**D36a. The board's touch on the synths.** In PLUS the tiles on each bot's side of the board (Scout-7's the lower
left, Echo-3's the upper right, the diagonal both: where they are, not only how many) shape its voice, a little:
zap rails narrow its square to a pulse (a duty down to a third), belts a little too; rotators swing the pulse slowly
(a triangle LFO, Echo-3's the other way; belts make it faster, 0.2 to 1.2 Hz); crates and pulse pads mix up to a
third of a triangle in, darker; pulse pads let it ring longer, zap rails pluck it shorter (a second set of the bots'
envelopes each way); the bass rings with pulse pads and plucks among crates. rs_plus_patch is a pure function of the
board; the app sets it on the mix when the board changes (as D24's colour), CLASSIC sets nothing, and an empty board
is the HTML's voices. patch_test: six boards six patches, all within those bounds, the HTML's square sample for
sample at a half's duty, the patched pulse without a DC and no more than a quarter louder.

**D36b. The big one, and the bots' heat.** PLUS sat in between; the HTML's intensity came from every step sounding
(a note, a ping a third above, Echo-3's FM and sub) and from the heat making it louder. So every
other phrase's B is the big one (rs_plus_peak): Scout-7's ping voice runs an arpeggio of the chord's tones over the
tune on every step (C5 to G6, up, down, up and down or ping-pong as Scout-7 faces: the HTML's dir_pattern), the bass
pumps the note it holds on the eighths between the groove's, Echo-3 strikes its chord tone again on the beats, the
tune is a tenth louder, the hats fill every sixteenth, a kick lands on three's and; the arpeggio rests under the
drummer's fill. The song: A, B, A, the big one. And anywhere the heat counts, as the HTML's: each point of a bot's
heat (to 6) makes its notes 5 % louder, and at 4 Scout-7's notes get the HTML's strike a third above. (The heat
comes from zap rails, +1, and from the other bot: its dash onto this one +2, its ping reaching it +1; a pulse pad
takes 1 off; at 6 the bot burns out. So steering one bot into the other, or over the rails, turns it up; a first
version of this entry said every ping and dash did, which the game does not.) plus_test: the big one 1.4 to 1.9 times the notes and the velocity of the other
Bs on every board, its arpeggio and the bass on the eighths, no arpeggio outside it.

**D36c. The bass runs into the next phrase.** Under the drummer's fill at a phrase's end the bass leaves its groove
for a run of four sixteenths, a scale step each, onto the next chord's root: up from below when Echo-3 faces north
or east as the run starts, down from above when it faces south or west (a turn is the player's say). plus_test: the
run into the next phrase in 24 of 30 phrase ends (the rest moved a step off a clash).

**D36d. The big one carries.** Rendered, D36b's big one was hardly there (the spectral flux of a render no higher
than a B's): its arpeggio was on Scout-7's soft triangle at a third of the tune's level. So in the big one the tune's
note is struck again on the eighths it holds (Scout-7's square, the HTML's density), Echo-3 strikes its chord tone on
the eighths, and the arpeggio is up to 0.78 and an octave lower (G4 to D6). Rendered, the big one's flux is the
HTML's (148 to 154 against CLASSIC's 146; a B's 102 to 120), 1 to 4 dB more between 1 and 4 kHz. Echo-3's notes and
the bass's pump keep to the chord's tones that sound with Scout-7's (a diminished chord's own tritone included).

## D37. PLUS: the tiles shape the notes, a vibrato, the HTML's arp rules (2026-11-07)

The product owner, on D36 and docs/roboseq/MUSIC.md (now ENGINES.md): quite good now; boards more distinct, a little vibrato on
longer notes, the arpeggio more varied, every rule making sense by the movement and the blocks the robots hit.

**D37a. A note's gesture.** A note can carry a pitch gesture (rs_note.fx, RS_FX_*), carried to the voice with its
frequency (packed in the timeline event's kind, so the timeline grows no bigger): a late vibrato (after a quarter
second, growing over 0.37 s to +-18 cents at 5.5 Hz), a wide one (at once, +-40 cents) and a bend into the note from a
semitone below or above (back in 46 ms). The square and triangle voices only; per block of at most 32 samples, the
phase step and its PolyBLEP inverse recomputed (a 32-bit division, as the note's own). No gesture is the HTML's voice
sample for sample, and CLASSIC never sets one. patch_test: the late vibrato waits, then about +-1 %; the wide one at
once; the bend starts 106 samples a cycle for A4's 100 and is home in 50 ms. fm1-emu: the worst block still 28 %.

**D37b. What the robots meet shapes the notes.** By the HTML's help (a zap rail's notes "short and staccato", a
pulse pad's "longer and sustained") and its tiles:

- **Zap rail:** the note struck on it is short (0.45 of a step) and harder (+15 %), plain (no vibrato).
- **Pulse pad:** the note on it is held to the next note (the call's end at most, 8 steps) with the wide vibrato;
  Echo-3's up to 6 steps.
- **Belt:** Scout-7's note is followed, within the step, by an arpeggio of two chord tones (three at heat 3, the
  HTML's), up for a north or east belt and down for a south or west one, from the note on; Echo-3 keeps its octave.
- **Rotator:** the note bends in, from below on a left rotator and from above on a right one.
- **Crate:** a bump stops the note the robot holds dead (a note of velocity 0 on its voice). A wall does not: the
  robot leans on the board's edge and its note holds, as in D35. The model tells the score which it was (rs_tick_out
  .bumped: 1 a wall, 2 a crate; the game is unchanged).
- **Modifiers:** WOBBLE sings with the wide vibrato, what its name says; GLITCH stays plain; every other note grows
  the late vibrato, heard only on a note held past a quarter second.

So the boards sound apart by what they are made of: GARDEN's pulse pads sing long, GAUNTLET's zap rails spit, CORRIDOR's
belts arpeggiate, SPINNER's rotators bend, MAZE's crates choke. plus_test (6 boards x 3 register rolls): every zap
rail note short and plain, every pulse pad note held and singing, every rotator note bent its way, every belt's
arpeggio its way, chokes only after crates.

**D37c. The arpeggio by the HTML's own rules.** "Position determines chord, direction determines contour, heat
determines span, tile determines rate":

- **Position:** the shape is the HTML's getChordShape, Scout-7's x and the loop: triad, seventh, add9, power, sixth.
  Its sus4 is the triad here, since its fourth would rub on the third Echo-3 and the bass sound.
- **Two robots:** when they are 4 tiles or more apart, the other shape (getComplementShape).
- **Heat:** the span is two octaves at heat 4 (getArpOctaves), else one.
- **Direction:** the contour is the facing, as before.
- **Modifier:** how long each note rings is getArpGate (steady 0.9, bright 0.7, low 1.2, wobble 1.0, glitch 0.5).
- **Tile:** the rate is twice as fast on a belt and half on a pulse pad.

The big one's arpeggio and the belt's both use it. plus_test: five or more shapes across the positions and loops,
two octaves hot, the other shape far apart, every tone in the scale.

## D38. Progressions: 73 named ones in six keys, FOLLOW and the chain (2026-11-07)

The product owner: look over the progressions' random, so there are many variations; and pick other progressions in
a chain, or follow.

**The bank.** D33a drew from about 30 four-chord progressions, all on C, and a fifth of the time from random roots
moving by thirds. But the C modes are six keys, since every note is quantized to the step's scale on C:
- C major is C major's and A minor's notes;
- C lydian is G major's, C mixolydian F major's, C dorian Bb major's;
- C minor is Eb major's, C phrygian Ab major's.

So a progression is now its chords as degrees of its own mode, and a key is one of those six. The bank holds 73
named progressions:

| Kind | Count | Examples |
|---|---|---|
| Major | 22 | I V vi IV, ii V I vi, I IV ii V, IV iii ii I |
| Major, eight chords | 3 | CANON, FIFTIES, GOSPEL |
| Minor | 13, and DESCENT of eight | i VI III VII, i iv v i, VI VII i i |
| Dorian | 6 | |
| Mixolydian | 6 | |
| Lydian | 3 | |
| Phrygian | 3 | |
| Pentatonic | 4 of each | |
| Borrowing a chord in C | 8 | I bVII IV I, I IV iv I, I bVI bVII I, I II IV I |

Four chords in an eight-step Voice Mod are said twice, the second time ending home (on V when they ended home).

**The roll** (SCL 16, PLUS) draws a family (major most, then minor, the modes, the pentatonics, the borrowing ones),
one of its progressions (never the one playing) and a key. The default song is ii I IV vi in C, as before.

**FOLLOW** (SCL 12, lit; either engine) gives every round a new progression, following on: the same key three times
in four, else a fifth up or down round the circle (Ab Eb Bb F C G).

**The chain** (Seq 2's K4, a fourth field on its steps: "--", RND or a progression by number) names the next round's
progression for that step, in the key the music is in; "--" leaves it, or lets FOLLOW have it.

The next round's progression is made on the round's last step (rs_state.bm_next), so the bass's step and run into the
next chord (D35, D36c) aim at the chord that will come (rs_next_mod_step); it goes in as the round wraps, before Seq 2
moves on.

The SCL header names what plays (I V vi IV G); NEXT > names what comes. Undo keeps the name with the steps; editing a
step by hand forgets it. CLASSIC's roll is still the HTML's randomizeBassMod; FOLLOW and the chain use the bank in
both engines.

plus_test:
- 672 different Voice Mods in 4000 rolls; every progression and every key rolled; borrowing ones 194 times.
- Every root a note of its step's scale, one scale all through but where a chord is borrowed.
- Four chords in eight steps home the second time.
- I V vi IV in G is G D E C, and a minor one in C's notes is in A minor.
- FOLLOW: a new progression each of 20 rounds, each in the key or a fifth away, the bass knowing the next round's
  first chord every time.
- The chain: every round as its step says.

## D39. The engines: BAND and ORIGINAL; ENGINES.md (2026-11-07)

The product owner: the engine, PLUS or CLASSIC, as a setting in the firmware, under GLO; and a proper overview on
GitHub of what everything does in both engines, under the names that fit best.

**The setting.** It was there since D33, on GLO's SELECT ("SEL MUSIC"). Now the header says what it is, `SEL ENGINE`
and the engine's name. It is not kept over a power-off: nothing is yet (ROADMAP's storage), and the app starts in BAND.

**The names.** PLUS is **BAND**: the robots as a band, a singer and an answering voice, a bass player with a groove
per board, a drummer, songs with phrases, cadences and a big chorus. CLASSIC is **ORIGINAL**: the browser
original's score, note for note. In the code: rs_state.engine (RS_ENGINE_ORIGINAL, RS_ENGINE_BAND), rs_score_band,
rs_band_*, band_test. The entries before this one keep the old names.

**The overview.** docs/roboseq/ENGINES.md (it was MUSIC.md) has:
- the two engines side by side, and the game's rules;
- every element (where a robot stands, its facing, the actions, the modifiers, the tiles, heat, burning out, the two
  robots, the loop and the phrase, the arpeggio, the bass and its grooves, the synths, the chords and the whole bank
  of progressions by number, the drums, the board), in each engine;
- what BAND does not use yet, and the controls that change the music.

The README's sound section is the short form, and links it.

## D40. BAND: Scout-7 in front, Echo-3 under it (2026-11-07)

The product owner: quite good, but the first robot is often low and the second a little too high. Measured both ways:

| | Pitch | Level through the mix |
|---|---|---|
| ORIGINAL | Scout-7 61-71, Echo-3 61-67 (MIDI, by velocity) | about level, -0.7 to +0.8 dB |
| BAND, before | Scout-7 65-72, Echo-3 59-65 | Scout-7 up to 3.8 dB under Echo-3 |

So the pitch was right and the level was not. In BAND, Scout-7's voice was near silent 19 to 38 % of the time, where
ORIGINAL's is 7 %. Its square is a pluck: it falls to 6 % in 0.14 s, which suits ORIGINAL's note every step, but in
BAND's held notes it left quiet tails. Echo-3's square keeps 40 % and was held as long as Scout-7's note. So in BAND:

- **The one.** The robots are back at their pads, a new cell, so Scout-7 sings from there: the chord's tone, with
  the bass's root and the kick, one that sounds with the bass.
- **Held notes.** A note Scout-7 still holds is struck again on each beat, softer (0.56); a passing one resolves
  there as before.
- **Clipped notes.** After a glitch's, a zap rail's or a crate's note, the tune's note comes back on the next beat
  (in B the next eighth). On a zap rail it comes short and 30 % hard, so the rail spits rather than falls silent.
  Off the beat a crate still chokes.
- **Levels.** Scout-7's notes are 12 % louder (0.84, 0.72, 0.6 on the beat, the eighth, the sixteenth), Echo-3's 15 %
  softer, and its harmony in B lasts a beat at most.
- **The pluck.** Scout-7 has the longer pluck of D36a's patch on every board but the zap rails' (sustain 0.12, decay
  0.24 s), as BAND holds its notes.

band_test (each robot's voice alone through the mix, 12 loops a board):
- Scout-7 1.7 to 4.9 dB in front of Echo-3 on every board (ORIGINAL -0.7 to +0.8).
- Quiet 11 to 28 % of the time, against 20 to 38 % before; the rest is the answer beat and the zap rails' staccato.
- The tune, the answers, A sparser than B, the scale and the rubs as before.

## D41. BAND's kit: punch, and the board and the moves in the drums (2026-11-07)

The product owner: more punch in the drums by design, and some of their parameters changing with the board and the
movement. The HTML's kit is a sine kick at C1, 33 Hz, gliding from 196 Hz, most of it under what a small speaker
plays, and a snare and a hihat of plain white noise, the hihat dying in 40 ms whatever its length (so an open hat or
a crash was a closed one). ORIGINAL keeps it bit for bit: its hits carry no tone.

BAND's hits carry their tone in the note: a snare's or a hihat's midi, which the HTML's noise does not use, and a
kick's pitch and fx.

**The kit (rs_voice.c):**
- **Kick:** driven (1.5u - 0.5u^3 of twice the sine: a rounder square at the same peak), a 3.6 ms click of falling
  noise (the beater), and a long and a tight decay beside the HTML's.
- **Snare:** noise through a one-pole high-pass (0.6 to 3.2 kHz: the crack) over a sine body falling from half again
  above in 6 ms and dying in 26 ms, at 147 to 262 Hz. Short, mid and long decays.
- **Hihat:** noise high-passed at 3 to 10 kHz, closed, half, open (0.25 s) and a crash (0.9 s).
- **Crash:** BAND's crash rides the crackle's voice (an explosion's, a death's), so the next hat does not cut it.

All integers in the interrupt, no 64-bit division. fm1-emu: the worst block 31 % (28 before).

**The hits (rs_score.c's band_kit, after the drummer):**
- **Kick:**
  - tuned to the chord's root in 41 to 78 Hz, and driven;
  - it clicks on the one, and when a robot dashes, bumps a crate or runs hot (heat 4);
  - long among three pulse pads or more, tight among three crates or four zap rails.
- **Snare:**
  - its body is as high as Echo-3 stands;
  - its crack is bright with zap rails, a little brighter with belts, dark among pulse pads, a step harder when
    Echo-3 dashes;
  - short among crates and for a ghost, long among pulse pads.
- **Hihat:**
  - as bright as Scout-7 stands: belts and zap rails a step brighter, pulse pads a step darker;
  - as long as the drummer plays it.

Measured (the drums alone through the mix, BAND against ORIGINAL, DEFAULT):
- the kick 7 dB more between 150 and 400 Hz and 18 dB more between 0.4 and 2 kHz, 3 dB less under 60 Hz, its peak
  2 dB higher;
- the snare 14 dB more body, its crack as the HTML's;
- the hats 30 dB less under 400 Hz;
- GARDEN's kick the longest, MAZE's the tightest.

band_test:
- every kick tuned to its root and driven, every one's kick clicked;
- GARDEN long, MAZE tight;
- every snare as high as Echo-3, every hat as long as played, the crashes on their own voice;
- ORIGINAL's drums the HTML's.

The model's fingerprint stays 150e92e1: the selfcheck plays the HTML's hits.

## D42. The battle mode: two players on the beat (2026-11-07)

The product owner: a last mode, and it must work well: a two-player battle. Each side of the keyboard is WASD and an
action to strike; held, the way you go is a step on the beat (moving on the beat as Crypt of the NecroDancer has
it); rounds, then "<robot> wins" with the winning robot's picture. Along the way, in their words:
- not best of three: the rounds set before the match, a score bar of segments down each side, where HOME shows the
  registers;
- the arena changed or rolled between rounds;
- a hit should drive them apart, or they meet on a tile and trade blows to the end;
- a 4 3 2 1 countdown with sound before the fight;
- the action key one white key to the right, and out of reach it plays the lead, in tune with the music.

**The game.** rs_battle.c, inside the model and with the game's own moves and tiles:
- **The beat.** On every beat (4 steps; 3 in a triplet loop) each robot does what its player holds. A way held: it
  faces that way and steps there. The fifth key held or tapped, with the other in reach (a tile on, or two with the
  tile between open): it strikes instead.
- **Steps, both at once.** A wall, a crate, the same tile, through each other, or onto the other where it stays: a
  bump.
- **A hit.** +2 heat. It drives the two apart: the other two tiles back, +1 for each a wall or a crate stops, and
  the striker one tile the other way. The one hit reels: it cannot be hit on the next beat.
- **The tiles, as in the game.** Belts carry, rotators turn, zap rails +1, pulse pads -1.
- **The round.** Heat 6 burns a robot out and the other takes the round. Both at once: the more hits in the round
  wins it, else the one nearer the middle, else it is a draw, played again, and a second draw running goes to a
  coin.
- **The overheat.** After 64 beats no pad cools, the arena's edge glows (+1 a beat), and hits are +3. After 96
  beats all but the middle tile glow. So a round ends.
- **Between rounds.** The robots go back to their pads, cool. The arena stays, is the next preset, or is a fresh
  roll (the pads kept open), as K4 set it.
- **The match** is the first to its rounds (1 to 9, K2, before it starts).
- **The countdown** is the round's name, then 4 3 2 1 on the beats with a beep each (the coin voice, tuned to the
  chord in BAND), and FIGHT on the fifth with a long high one; the robots go from the beat after.

**The music runs on.** The engine plays the battle as it plays the registers: the robots' moves are their notes.
- **The voices.** A strike is heard as a dash: BAND's kick clicks, the snare cracks. The heat makes them louder.
- **Burning out** is the explosion.
- **Between the beats** the robots stand, so their tunes are on the beat.
- **The loop** no longer puts the robots back on their pads; only a new round does.

**The keys** (a cluster each at the keyboard's ends, WASD and F):
- Scout-7: F3 F#3 G3 A3 and B3. Echo-3: C5 C#5 D5 E5 and F5.
- The last way pressed of those held wins.
- **The fifth key out of reach** plays the lead now: the chord's tone where the robot stands on the board's keyboard
  (Scout-7 an octave over Echo-3), on up the chord when pressed again on one tile, held as long as the key
  (rs_battle_lead_note).
- **Two exceptions to the control rules.** PLAY on the winner is the next match (C1's transport otherwise), and the
  black keys are the players', not the steering (C3).

**The screens** (rs_battle_screen.c):
- **The start** shows the keys, the rounds, the arena and what each round brings, with the keyboard map.
- **The fight** shows the names with the round between them, the score bars down the sides, the arena whose frame
  flashes on the beat, a strike's flash the way it struck (white on a hit), the star on the one hit, and the
  overheat's glowing tiles. Each robot's life is at the foot.
- **Later (2026-11-08), in the board's own art:** the product owner found the white bars (the beat's frame, the
  strike) out of the design. On the beat the frame lights mint and the splash's corners in each robot's colour
  (Scout-7's side red, Echo-3's orange; dim between the beats); a strike is three chevrons the way it struck, in the
  striker's colour, outlined and lit as the step icons, brighter to the front, the front one white on a hit.
- **The banners:** the countdown, the round taken (or the draw, or the coin).
- **The winner:** its robot, 128 px, "<NAME> WINS!", the score and "PLAY: AGAIN".

**battle_test:**
- the countdown;
- a step a beat and none between;
- the bumps;
- the strikes: the hit, the push apart, the slams, the lunge, the trade, the tap, the turn and strike;
- the tiles;
- the rounds, the match, a draw and the coin;
- the arenas between rounds, the overheat;
- that the music plays on in both engines, and that the registers play the same game after a battle.

**The playtest.** Two scripted players, one greedy and one a little different, each turning when it bumps, play a
match on each of the six arenas. Every match ends: 24 rounds of 46 beats on average, 12 hits of 15 strikes, and no
one hit two beats running.

**D42, later the same day: the keys.** The product owner, on the screens: a white key free between WASD and the
strike, and the keys lit on the keyboard. So Scout-7 strikes on C4 (B3 free) and Echo-3 on G5 (F5 free). The little
keyboard is now at the foot of the fight too: each player's keys dim in its colour (the strike in the robot's
accent), lit while held. The FM-1's keys have no lights, so the screen is where they light. The lives moved into the
header beside the names. The start screen draws the keyboard big with no note names: each player's keys marked with arrows and a burst
for the strike, lit while held, which says enough. The key in the middle between the two, F#4, is RESET: the match
again from round 1.

## D43. The project kept over a power-off (2026-11-07)

ROADMAP phase 3's storage. The HTML keeps its project in the browser (`saveState()`, 300 ms after any change; the
user board apart, on SAVE) and takes it back at load (`loadState()`). The FM-1 does the same with Dinghy's blobs.

**What is kept** (`struct rs_project`, rs_model.c):
- **The HTML's `saveState()` payload:** the tempo, the loop, the scale, ROOT, the speed, the mixer, Voice Mod (its
  steps, when it moves on), Seq 2 (its steps, on, where it is, its rolls), the density, both registers with their
  lengths, the drums (a preset made again at the loop's length, a pattern by hand as it was), ticks per move and
  the registers swapped.
- **The user board**, kept with it.
- **The port's own:** the engine, Voice Mod's progression and its key, FOLLOW; and in the app, the mix's seven
  faders, the board's colour, the lead's three knobs, the arp's kind and rate.

**What is not kept** is what the HTML does not keep either: the board playing (it starts on the default), the
robots, the step, the undo.

**Taken back as the HTML's `loadState()` does.** Each field only if the HTML would take it (`clampNumber`,
`LOOP_OPTIONS.includes`, `typeof`), else the default's; a register no longer than the loop. A version number comes
first: another version is not taken at all. edit_test checks a round trip byte for byte, and that 300 projects of
random bytes come out valid states that play.

**When it is saved.** A save erases a 4 KB sector, about 50 ms with the interrupts off, and the sound stops for that
long. So the project is saved only when nothing plays: stopped 2 s, unchanged 3 s, no key held. While it plays, a
change waits for the stop.
- **SAVE is lit** while there are changes not kept yet.
- **Pressed while stopped**, SAVE keeps them at once.
- **In the battle** nothing is saved: its tempo is the fight's.

**Where.** Blobs 0 and 1 by turns, each save numbered. At boot the newer valid one is taken, so a save cut short by
a power-off leaves the one before (Dinghy writes the header last: a cut save is invalid). Two sectors also halve the
wear. A blob is 4 KB; the project with the app's part is 732 bytes.

**RAM.** .data + .bss went from 70 200 to 75 768 bytes of 98 304. Dinghy's sector buffer is 4 KB of that, and two
copies of the project. The undo ring (12 KB) stays in RAM: 22 KB are still free (ROADMAP phase 1's item).

**Tried in fm1-emu** (`fm1-live --flash FILE`, FM1_STRICT, three runs on one flash):
1. Tempo to 100 while playing, then stop: saved once, 2.25 s after the stop.
2. Boots at 100; tempo to 93 and SAVE: saved at once.
3. Boots at 93.

**D39's "the app starts in BAND"** now holds for a first boot. After that, the engine is the one kept.

Later, if wanted: more projects in the other blobs and slots (SAVE and a key, say).


## D44. The page: hear it, install it from the browser (2026-11-08)

The product owner: a page as elaborate as VaveHero's, FM-1 Clock's and Star Distroyer's, and where the README or the
page links to the original, the link goes to it on jhf.no (`https://jhf.no/skurky/roboseq.html`, byte for byte
`reference/roboseq.html`), not to the file in the repository.

**What it is.** `https://joakimskurk.github.io/fm1roboseq/`, made by `web/make_site.py` and published by CI from
`main` after the build and its tests (the site job, as the three siblings'):
- **The installer** is SLOOP's, as on the siblings' pages: `web/fm1pkg.js` and `web/fm1ota.js` unchanged (the same
  bytes as theirs), the package this run built and tested, its SHA-256 checked before it is offered, a box to tick
  that it has not been tried on a real FM-1. The package must be `FM-1_965` with its update loader in it.
- **The demos** are the app itself on the host (`web/render_demos.sh`: `tests/roboseq/demo-band.txt` and
  `demo-original.txt`), the default song in BAND and in ORIGINAL, 33 s each, as the FM-1 sends its sound over USB;
  AAC, each with its spectrum as its seek bar (Star Distroyer's player).
- **The pictures** are the firmware's own: its screens (`make screens`), the film, the README's art drawn by its code
  (`assets/readme/`), the FM-1 in BATTLE in fm1-emu (`make panels`), the link preview (`web/make_og.py`).
- **The words** say what the README says: the programs decide, the black keys nudge, the battle is steered.

Measured with it: the default song's render has 0.42 % of its samples at full scale (ORIGINAL 0.37 %), the HTML's own
render of it 0.48 % (its peak 1.64): the clipping is the HTML's, as D9-D14 port its gain staging, and stays.
