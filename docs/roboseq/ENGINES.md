# RoboSeq's two engines

RoboSeq has one game and two ways to make music from it. An **engine** turns what the two robots do on the board
into notes:

- **BAND** is RoboSeq's own: the robots as a band playing songs. The app starts in it.
- **ORIGINAL** is the browser original's score (`reference/roboseq.html`), note for note.

Switch with **SELECT on GLO**; the header shows `SEL ENGINE BAND` or `ORIGINAL`. Nothing on the FM-1 is kept over a
power-off yet, so it starts in BAND again.

This page says what everything does in each engine. It is written from the code (`rs_model.c`, `rs_score.c`,
`rs_voice.c`, `rs_mix.c`); when the two disagree, the code is right and this page is out of date. The reasons are in
[DECISIONS.md](DECISIONS.md) (BAND was called PLUS and ORIGINAL CLASSIC up to D38), every control is in
[CONTROLS.md](CONTROLS.md).

## At a glance

| | ORIGINAL | BAND |
|---|---|---|
| **What it is** | the HTML's score, checked tick by tick against the HTML itself | RoboSeq's own, written anew from the same game |
| **Scout-7** | a note every step, from where it stands | sings a tune that comes back every bar |
| **Echo-3** | a harmony of Scout-7's note, every step | answers Scout-7, then sings with it |
| **The bass** | Echo-3's sub, an octave under its note | the board's own groove, which Echo-3 plays into |
| **The drums** | your pattern | your pattern, played by a drummer |
| **The form** | the loop | phrases: A, B, A, the big one; cadences; fills |
| **Chords** | Voice Mod | Voice Mod, rolled from 73 named progressions in six keys |
| **The synths** | the HTML's voices | the HTML's voices, touched by the board; vibratos and bends |
| **Checked by** | the HTML: 36 trace scenarios, renders of its audio | `band_test`: every rule below as a test |

**The same in both:**
- The game itself. BAND plays the same game tick for tick; `band_test` checks it.
- The voices, the mix, the reverb and the limiter, measured against the HTML's audio.
- The lead on REC.
- The board's colour on FX K2.
- Voice Mod, Seq 2, the drum patterns, FOLLOW and the chain.

## The game

| | What it does |
|---|---|
| MOVE / BACK | a tile forward / back; a wall or a crate stops it (a bump) |
| TURN L / R | a quarter turn on the spot |
| DASH | two tiles forward (the second even if the first was stopped); landing on the other robot hits it: +2 heat for the other |
| PING | a pulse straight ahead to the first crate or wall; reaching the other robot: +1 heat for the other |
| WAIT | stands still |
| chance | a step at 75 or 50 % may not fire this loop: the robot hesitates |
| modifier | steady, bright, low, wobble, glitch: how the step's note is coloured |
| Belt | carries the robot on a tile its arrow's way, mid-step; chains up to three |
| Rotator L / R | turns the robot a quarter left / right |
| Zap rail | +1 heat |
| Pulse pad | -1 heat (cools it) |
| Crate | blocks the way |
| heat 6 | the robot burns out on its next move and respawns in the corner farthest away |
| both on one tile | two steps running, no dash coming: love, the coin |
| the loop | at its end both robots go back to their pads and walk the same way again |

Heat comes only from zap rails and from the other robot. A robot's own pings and dashes do not heat it; dashing
into the other, or pinging it, heats the other.

The black keys steer: each robot's next move, once. The white keys edit its register.

## What each thing does

### Where a robot stands

- **ORIGINAL.**
  - Scout-7: x picks the degree of the scale from the root, and y the octave (the top rows an octave up, the bottom
    one down).
  - Echo-3: its note is a harmony of Scout-7's by Echo-3's x (unison, third, fifth, fifth, octave), its y the octave,
    its sub an octave below.
  - x and the loop also pick the arpeggios' chord.
- **BAND.**
  - The board is a keyboard: a cell is a degree of the key, x plus the rows up, from E4 (Scout-7) and E3 (Echo-3).
    A step on the board is a step in the scale, a dash a third.
  - The robots retrace their path every loop, so the tune comes back every bar.
  - On the beats the note is the chord's tone nearest the way the tune goes.
  - A passing note still sounding on a beat resolves there, a step to the chord.
  - In a bar no leap is bigger than a sixth.
  - Echo-3's notes sound with Scout-7's: chord tones on the beats, and never a semitone, a major seventh or a
    tritone struck together with it.

### Which way it faces

- **ORIGINAL.**
  - The articulation: north loud and short, east middling, south soft and long, west softest.
  - The arpeggios' contour: north up, south down, east up and down, west ping-pong.
- **BAND.**
  - Scout-7's facing is the contour of the arpeggio, by the same table.
  - Echo-3's facing when the bass's run starts picks the run's way: north or east up, south or west down.

### The actions

- **ORIGINAL.** Every step sounds, its loudness by the action (move 0.8, back 0.7, a turn 0.6, dash 1.0 and short,
  ping 0.9, wait 0.2). Every step but a wait also gets a ping a third above.
- **BAND.**
  - **The rhythm is the movement.** A note starts where a robot gets to a new cell; on the one the robots are back
    at their pads, so Scout-7 sings from there. A note holds while the robot turns or leans on a wall, to its
    register's next wait.
  - **The lead stays in front.** Scout-7's square is a pluck, so a note it still holds is struck again on each beat,
    softer. After a clipped note (a glitch, a zap rail, a crate) the tune's note comes back on the next beat (in B
    the next eighth). Scout-7 sings a little louder than Echo-3, which keeps its harmony to a beat at most:
    `band_test` holds it in front of Echo-3 by 1.7 to 4.9 dB on every board, where ORIGINAL has them about level.
  - **The actions.** WAIT is a rest. PING strikes the note again, with a glint an octave up. DASH is 20 % louder.
  - **Echo-3 also plays the bass.** Its dash pops the bass an octave up, its ping doubles the bass's note, and its
    wait leaves a gap off the beat.

### The modifiers

| | ORIGINAL | BAND |
|---|---|---|
| steady | as is | a held note grows a slow vibrato after a quarter second |
| bright | louder, shorter | 15 % louder |
| low | softer, longer | 20 % softer |
| wobble | a hair softer and shorter | a wide vibrato from the start |
| glitch | clipped to 0.4, a hair louder | clipped to 0.4, plain |

The arpeggio's notes ring by the modifier in both engines (the HTML's getArpGate): bright shorter, low longer,
glitch shortest.

### The tiles a robot meets

| Tile | ORIGINAL | BAND |
|---|---|---|
| Belt | Scout-7: an arpeggio of the chord, up for a north or east belt, down for south or west. Echo-3: a note an octave up. | Scout-7: two chord tones after its note within the step, the belt's way; three when hot (heat 3). Echo-3: an octave double. |
| Rotator | (it turns the robot: the facing changes) | the note bends in, from a semitone below on a left rotator, from above on a right one |
| Zap rail | the note half as long, "short and staccato" | the note short (0.45 of a step) and 30 % harder, with no vibrato; while the robot is on the rail its notes come short and hard: it spits |
| Pulse pad | the note 1.8 times as long, "long and sustained" | the note held to the next one, with a wide vibrato, 10 % louder |
| Crate | the bump's blip | the bump stops the robot's held note dead |
| Wall | the bump's blip | the robot leans on the edge and its note holds |

Each tile also has its own sound in both engines: a tick for a belt or a rotator, a crack for a zap rail, a chime
for a pulse pad, a blip for a bump. In BAND they are tuned to the chord.

### Heat

- **ORIGINAL.** Each point lifts the note a scale degree, up to two, and makes it 8 % louder, up to five. At 3 the
  arpeggios get a third note; at 4 they span two octaves.
- **BAND.** Each point makes the robot's notes 5 % louder, up to six. At 4, Scout-7's notes get the HTML's strike a
  third above, and its arpeggio spans two octaves. Steering one robot into the other, or over the rails, turns the
  music up.

### Burning out

Both engines: the explosion (a thump, a noise, a crackle), and the robot is silent until it respawns.

### The two robots together

- **ORIGINAL.**
  - When they are 4 or more tiles apart, Echo-3's arpeggio takes the complementary chord.
  - When they land on the same pitch class, Scout-7 moves up a third.
  - Love plays the coin.
- **BAND.**
  - When they are 4 or more tiles apart, the arpeggio takes the complementary chord.
  - In the first half of a phrase Echo-3 answers Scout-7, in the second it sings with it (below).
  - Love plays the coin, tuned.

### The loop, the bar and the phrase

- **ORIGINAL.** The loop repeats. The reset's chime plays at its start.
- **BAND.** A phrase is a Voice Mod round: its steps times the loops each step lasts. That is 4 loops by default,
  and 4 loops when Voice Mod is MANUAL or the round is shorter than two loops. A bar is 16 steps (12 in a
  triplet loop), so a 16-step loop is one bar.

| Part of the phrase | What happens |
|---|---|
| **A**, its first half | Scout-7 calls for three beats; Echo-3 answers in the fourth with Scout-7's own first notes, from where Echo-3 stands. Scout-7's notes start on the eighths (and on dashes). |
| half way | the bar ends on the chord's fifth: from half the bar on, Scout-7 lands there and holds |
| **B**, its second half | Scout-7 sings on every step; Echo-3 sings with it, the chord's tone a third or a sixth under, and fills Scout-7's gaps. The hats open up and fill in; the room opens. |
| the end | the last bar ends on the root; the drummer plays a fill and the bass runs a scale into the next chord; a crash starts the next phrase |
| **the big one**, every other phrase's B | as dense as ORIGINAL: an arpeggio on every step over the tune, the tune and Echo-3 struck again on the eighths, the bass pumping, the hats on every sixteenth, a kick on three's and, everything about 10 % louder |

The song goes A, B, A, the big one, and round again.

### The arpeggio (BAND)

The HTML's own rule: "position determines chord, direction determines contour, heat determines span, tile determines
rate".

| What | From |
|---|---|
| the chord's shape | Scout-7's x and the loop: triad, seventh, add9, power, sixth (the HTML's sus4 is a triad here, so it does not rub on the third the others sound) |
| the other shape | the robots 4 or more tiles apart |
| the span | one octave; two at heat 4 |
| the contour | Scout-7's facing: north up, south down, east up and down, west ping-pong |
| how long each note rings | the step's modifier |
| the rate | every step; twice as fast on a belt, half on a pulse pad |

It plays in the big one, on Scout-7's ping voice, and briefly after a belt.

### The bass (BAND)

The board picks the groove:

| Board | Groove | A bar of it |
|---|---|---|
| DEFAULT | three, three, two | `R..R..O.R..R..FA` |
| CORRIDOR | straight eighths | `R.R.R.R.R.R.R.FO` |
| GARDEN | a walk in quarters | `R---T---F---A---` |
| GAUNTLET | a gallop | `R.RRR.RRR.RRF.FO` |
| SPINNER | octaves | `R.O.R.O.R.O.F.O.` |
| MAZE | syncopation | `R..O.OR...R.F..A` |
| a rolled or your own board | one of those, dub (`R-----..O-..F-A-`) or half time (`R-------F-----.A`), by its tiles | |

In a bar: R is the root, F the fifth, O the octave, T the third, A a scale step into the next chord's root, - a held
note and . a rest; each character is a sixteenth.

On top of the groove:
- **Echo-3 plays into it.** A dash pops the note an octave up, a ping doubles it, a wait leaves a gap off the beat.
- **The run.** At a phrase's end, under the fill, the bass runs four sixteenths into the next chord.
- **The next chord.** The step into the next chord and the run both aim at the chord that is coming, even when FOLLOW
  or the chain is about to change it.

Changing the board on ENV changes the groove as the loop comes round.

### The synths

- **ORIGINAL.** The HTML's voices as they are: Scout-7 a square with a ping on a triangle, Echo-3 a square through a
  low-pass with a triangle sub.
- **BAND.** The same voices, with two touches.
  - **The board, a little.** Each robot is shaped by the tiles on its side of the board (Scout-7 the lower left,
    Echo-3 the upper right):
    - zap rails narrow its square to a pulse, belts a little too;
    - rotators make the pulse swing slowly, and belts make the swing faster;
    - crates and pulse pads mix a triangle in, darker;
    - pulse pads let it ring longer, zap rails pluck it.

    The bass rings among pulse pads and plucks among crates. Scout-7's pluck rings a little longer on every board
    but the zap rails' (BAND holds its notes, ORIGINAL strikes one every step).
  - **The notes' gestures.** The vibratos and the bends above, from the modifiers and the tiles.

### Chords and progressions (Voice Mod)

Both engines:
- **The chord.** Every note is quantized to the step's scale on C, so a step's chord is the scale's triad on its note
  nearest the root. The default Eb, C, F, A major sounds as Dm, C, F, Am.
- **FOLLOW** (SCL key 12, lit). Every Voice Mod round ends in a new progression, following on: the same key three
  times in four, else a fifth up or down.
- **The chain** (Seq 2, K4 on each step). It names the next round's progression: `--` as it is, `RND` a new one, or
  one of the bank by number. Seq 2 moves on as Voice Mod comes round, so each step is a round.

The random on SCL (key 16) differs:
- **ORIGINAL.** The HTML's: random scales, roots and semitones.
- **BAND.** One of 73 named progressions in one of six keys. The header names it ("I V vi IV G") and, once it is
  ready, the next one.

The six keys are the six major keys whose notes are a mode on C: Ab, Eb, Bb, F, C, G, with their relative minors.
Four chords in an eight-step Voice Mod are played twice, the second time ending home.

The bank by number, the numbers Seq 2's K4 shows:

- **Major:**
  - 1 I V vi IV
  - 2 vi IV I V
  - 3 I vi IV V
  - 4 I IV vi V
  - 5 ii V I vi
  - 6 IV V iii vi
  - 7 I iii IV V
  - 8 I IV I V
  - 9 vi iii IV I
  - 10 I V IV V
  - 11 IV I V vi
  - 12 I IV ii V
  - 13 I vi ii V
  - 14 I I IV V
  - 15 vi V IV V
  - 16 IV IV I V
  - 17 I iii vi IV
  - 18 ii IV V I
  - 19 I V ii IV
  - 20 IV vi I V
  - 21 ii I IV vi
  - 22 IV iii ii I
  - 23 CANON, of eight
  - 24 FIFTIES, of eight
  - 25 GOSPEL, of eight
- **Minor:**
  - 26 i VI III VII
  - 27 i iv v i
  - 28 i VII VI VII
  - 29 i VI VII i
  - 30 i iv VII III
  - 31 i III VII VI
  - 32 i VI iv v
  - 33 VI VII i i
  - 34 i iv VI VII
  - 35 iv i v i
  - 36 i III iv VI
  - 37 i VII VI v
  - 38 VI III VII i
  - 39 DESCENT, of eight
- **Dorian:**
  - 40 i IV i IV
  - 41 i ii IV i
  - 42 i VII IV i
  - 43 i IV VII i
  - 44 i III IV i
  - 45 i v IV i
- **Mixolydian:**
  - 46 I VII IV I
  - 47 I VII I VII
  - 48 I v VII IV
  - 49 I IV VII I
  - 50 I v IV I
  - 51 I VII vi VII
- **Lydian:**
  - 52 I II I II
  - 53 I II vi V
  - 54 I II iii II
- **Phrygian:**
  - 55 i II i II
  - 56 i II vii i
  - 57 i II III II
- **Pentatonic, in C:**
  - 58-61 PENTA 1-4
  - 62-65 MPENTA 1-4
- **Borrowing a chord, in C:**
  - 66 I bVII IV I
  - 67 I IV iv I
  - 68 I bVI bVII I
  - 69 I bIII IV I
  - 70 I II IV I
  - 71 vi IV I bVII
  - 72 I iv I V
  - 73 I bVII bVI bVII

### The drums

- **ORIGINAL.** The pattern as set on LFO.
- **BAND.** The same pattern played by a drummer:
  - dynamics, never two hits quite the same;
  - ghost snares before the backbeat;
  - a kick pushing into four every other loop;
  - busier and open hats in B, and every sixteenth in the big one;
  - a fill at a phrase's end and a crash after it.

  CLEAR, or the drums off, stays quiet.

### The board as a whole

- **Both engines.** The board colours the room (FX K2, 40 % by default; 0 % is the HTML's sound exactly):
  - an open board makes the room bigger, crates make it smaller;
  - pulse pads send more to the reverb;
  - zap rails make it drier, brighter and driven;
  - belts make it brighter, rotators drive it.

  In BAND the colour also shades with each chord and opens up in B.
- **BAND.** The board is also the bass's groove, its synths' touch, and what its tiles do to the notes (above).
  - GARDEN's pulse pads sing long.
  - GAUNTLET's zap rails spit and burn out.
  - CORRIDOR's belts arpeggiate.
  - SPINNER's rotators bend.
  - MAZE's crates choke.

## What BAND does not use (yet)

- **The facing's articulation** (north loud and short ... west soft). In BAND the facing is the contour of the
  arpeggio and the run.
- **The HTML's heat pitch-lift.** A hot robot in BAND plays louder and gets the strike; its notes do not move up.
- **The distance between the robots.** It only picks the arpeggio's other shape.

## The controls that change the music

| Page | What it does to the music |
|---|---|
| HOME, SEQ | the registers, the robots' moves: in BAND the tune and its rhythm; the black keys steer them live |
| ENV | the board: its tiles, and in BAND its groove and its synths' touch |
| SCL | Voice Mod: the chords, when they change, FOLLOW, the random (in BAND a named progression) |
| SCL again (Seq 2) | a chain of rounds: board, drums, registers swapped, progression |
| LFO | the drum pattern (CLEAR: no drums) |
| FX | the levels and K2, how much the board colours the room |
| GLO | the engine (SELECT), the tempo, how often the robots move, the loop, the speed |
| REC | the lead you play yourself over it, the same in both engines |

Every control is in [CONTROLS.md](CONTROLS.md).
