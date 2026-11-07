# What the game does to the music

Every rule from the robots to the sound, in both modes: CLASSIC (the HTML's, `reference/roboseq.html`, note for note)
and PLUS (RoboSeq's own, D33-D36). The HTML states its own principle in playBoardSound: *position determines chord,
direction determines contour, heat determines span, tile determines rate*. Its help says of the tiles: a zap rail's
notes go short and staccato, a pulse pad's long and sustained. The last part lists where PLUS does not follow the
robots yet. Written 2026-11-07 from the code (rs_model.c, rs_score.c, rs_voice.c, rs_mix.c); when this and the code
disagree, the code is right and this is out of date.

## The game

| | What it does in the game |
|---|---|
| MOVE / BACK | a tile forward / back; a wall or a crate stops it (a bump) |
| TURN L / R | a quarter turn on the spot |
| DASH | two tiles forward (the second even if the first was stopped); landing on the other robot hits it: +2 heat for the other |
| PING | a pulse straight ahead to the first crate or wall; reaching the other robot: +1 heat for the other |
| WAIT | stands still |
| chance | a step at 75 or 50 % may not fire this loop (the robot hesitates) |
| Belt | carries the robot on one tile in its arrow's way, mid-step; chains up to 3 |
| Rotator L / R | turns the robot a quarter left / right |
| Zap rail | +1 heat |
| Pulse pad | -1 heat (cools it) |
| Crate | blocks the way |
| heat 6 | the robot burns out on its next move and respawns in the corner farthest away |
| both on one tile | two steps running, no dash coming: love, the coin |
| the loop | at its end both robots go back to their pads and walk the same way again |

Heat comes only from zap rails and from the other robot (its dash onto, its ping into this one). A robot's own pings
and dashes do not heat it.

## The map

### Where a robot stands (x, y)

- **CLASSIC.** Scout-7: x picks the degree of the scale from the root, y the octave (the top rows an octave up, the
  bottom one down). Echo-3: its note is a harmony of Scout-7's by Echo-3's x (unison, third, fifth, fifth, octave),
  its y the octave; its sub an octave below. x and the loop also pick the arpeggios' chord (triad, seventh, sus4, add9,
  power, sixth, turning every three loops).
- **PLUS.** The board is a keyboard: a cell is a degree of the key, x plus the rows up, from E4 (Scout-7) and E3
  (Echo-3); a step on the board is a step in the scale, a dash a third. Because the robots retrace their path every
  loop, the tune comes back every bar. On the beats the note is the chord's tone nearest the way the tune goes.

### Which way it faces

- **CLASSIC.** The articulation: north loud and short, east middling, south soft and long, west softest. The
  arpeggios' contour on the ghost ticks: north up, south down, east up and down, west ping-pong.
- **PLUS.** Scout-7's facing is the contour of the big one's arpeggio, by the HTML's same table. Echo-3's facing when
  the bass's run starts at a phrase's end picks the run's direction: north or east up, south or west down. The
  articulation by facing is not used.

### The actions

- **CLASSIC.** Each step sounds, louder or softer by the action: move 0.8, back 0.7, a turn 0.6, dash 1.0 and short,
  ping 0.9, wait 0.2 (a tick). Every step but a wait also gets a ping a third above, short.
- **PLUS.**
  - The rhythm comes from the movement: a note starts where a robot gets to a new cell, and it holds while the robot
    turns or pushes at a wall or a crate.
  - WAIT is a rest.
  - PING strikes the note again, with a glint an octave up.
  - DASH is a fifth louder, and it may start a note between the eighths in A.
  - Echo-3's actions also play into the bass: a dash pops the bass an octave up, a ping doubles its note, and a wait
    leaves a gap off the beat.

### The modifiers

- **CLASSIC.** They colour the note's loudness and length: steady as is, bright louder and shorter, low softer and
  longer, wobble a hair softer and shorter, glitch clipped to 0.4. The arpeggio's last note rings by the same: bright
  0.8, low 1.3, wobble 1.1, glitch 0.6.
- **PLUS.** Bright makes the note 15 % louder, low 20 % softer, and glitch clips it to 0.4 of a step. Steady and
  wobble do nothing.

### The chance

- **Both modes.** A hesitating robot plays nothing that step. In PLUS the path changes from the hesitation on, so the
  tune varies there.

### The tiles a robot meets

| Tile | CLASSIC | PLUS |
|---|---|---|
| Belt | Scout-7: an arpeggio of the chord, up for a north or east belt, down for south or west; its own note shortened. Echo-3: a note an octave up (not while Scout-7 arpeggiates). A noise tick. | Scout-7: one chord tone rolls up or down after its note. Echo-3: an octave double. The tick. |
| Rotator | A tick at A5. (It turns the robot, so the next notes' facing changes.) | The tick, tuned to the chord; the facing changes the big one's contour. |
| Zap rail | The note half as long ("short and staccato"); +1 heat; a noise crack. | +1 heat (louder, see heat); the crack. The note itself is not shortened. |
| Pulse pad | The note 1.8 times as long ("long and sustained"); -1 heat; a chime at C6. | The note 10 % louder; -1 heat; the chime tuned to the chord. |
| Crate, wall | The robot bumps: a blip at G2. | The note holds while the robot pushes; the blip tuned to the chord. |

### Heat

- **CLASSIC.**
  - Each point lifts the note a scale degree, up to two.
  - Each point makes the note 8 % louder, up to five.
  - At 3 heat the arpeggios get a third note; at 4 they span two octaves.
- **PLUS.**
  - Each point makes the robot's notes 5 % louder, up to six.
  - At 4 heat Scout-7's notes get the HTML's strike a third above.

### Burning out

- **Both modes.** The explosion: a thump, a noise and a crackle. The robot stays silent until it respawns.

### Two robots

- **CLASSIC.** Bots 4 or more tiles apart: Echo-3's arpeggio takes the complementary chord. Same pitch class:
  Scout-7 moves up a third. Love: the coin.
- **PLUS.** The coin, tuned to the chord. The distance does nothing. Echo-3's notes are chosen to sound with
  Scout-7's: no semitone, tritone or major seventh struck together.

### The loop and the bar

- **CLASSIC.** The reset's chime, E5, A5 and C6.
- **PLUS.** The chime, tuned to the chord. The phrase is a Voice Mod round, 4 loops by default:
  - **A, the first half.** Scout-7 calls for three beats, and Echo-3 answers in the fourth with Scout-7's motif from
    its own cell.
  - **The middle.** The tune lands on the chord's fifth.
  - **B, the second half.** Echo-3 sings a third or a sixth under.
  - **The end.** The tune lands on the root while the bass runs into the next chord under the drummer's fill.
  - **The big one.** Every other phrase's B: an arpeggio on every step over the tune, the tune and Echo-3 struck on
    the eighths, the bass pumping, the hats on every sixteenth and a kick on three's and.

### Voice Mod (Seq 1)

- **Both modes.** The scale and the root of each loop. Every note is quantized to the scale on C, so the chord is
  the scale's triad nearest the root; the default Eb, C, F, A major sounds as Dm, C, F, Am.
- **PLUS.** SCL's random writes a progression in a key.

### The drums

- **CLASSIC.** The pattern as set.
- **PLUS.** The pattern played by a drummer: dynamics, ghost snares, a kick pushing into four every other loop,
  busier and open hats in B, a fill at a phrase's end and a crash after it.

### The board as a whole (its layout)

- **Both modes (the FM-1's, D24, FX K2, 40 %).** The board colours the room:
  - An open board makes it bigger, crates smaller.
  - Pulse pads send more to the reverb.
  - Zap rails make it drier, brighter and driven.
  - Belts make it brighter, rotators drive it.

  In PLUS the colour also shades with each chord and opens up in B.
- **PLUS.**
  - **The bass groove.** DEFAULT three-three-two, CORRIDOR straight eighths, GARDEN a walk, GAUNTLET a gallop,
    SPINNER octaves, MAZE syncopation; a rolled or the user's board gets one of eight by its tiles.
  - **The synth patch, a little.** Each robot is shaped by the tiles on its side of the board (Scout-7 lower left,
    Echo-3 upper right):
    - zap rails narrow its square to a pulse, belts a little too;
    - rotators swing the pulse slowly, and belts make the swing faster;
    - crates and pulse pads mix a triangle in;
    - pulse pads ring longer, zap rails pluck.

    The bass rings with pulse pads and plucks among crates.

## Where PLUS does not follow the robots yet

1. **Zap rail and pulse pad.** They do not shape the note struck on them, as the HTML's help promises (short and
   staccato; long and sustained). PLUS only knows them through the board patch, which does not care where the robots
   go.
2. **Wobble does nothing in PLUS.** Its name promises a wobble; the HTML gives it a hair less, nothing more.
3. **The facing's articulation** (north loud and short ... west soft) is not used in PLUS.
4. **The arpeggio is always the triad over about an octave and a half.** The HTML's own arp rules are unused: the
   chord from the position, the span from the heat, the ring from the modifier, and Echo-3's complementary chord when
   they are far apart.
5. **Rotators and crates do little to the notes.** A rotator only changes the facing; a crate only lets the note
   hold.
6. **The distance between the robots** does nothing.

## Proposed next (not built, 2026-11-07)

The product owner's three wishes, each from a rule above:

- **More distinct boards, from what the robots meet.**
  - Zap rail: the note struck on it is short and hard (the HTML's half).
  - Pulse pad: the note on it is held long (the HTML's 1.8, to the next note) and sings with vibrato.
  - Belt: a real arpeggio along it, two or three chord tones the belt's way (the HTML's belt arp) instead of one roll.
  - Rotator: the note bends into place from below (left) or above (right), the turn heard.
  - Crate or wall: a bump chokes the held note to a rest.

  Then GARDEN (five pulse pads) sings long, GAUNTLET (seven zap rails) spits staccato and burns out, CORRIDOR (seven
  belts) arpeggiates, SPINNER (eight rotators) bends, and MAZE (four crates) is choppy, on top of their grooves and
  patches.
- **Vibrato on longer notes.** A note held past about a beat starts a slow vibrato, about 5.5 Hz and a few cents,
  growing. WOBBLE makes it wider and starts it at once, finally what its name says. A pulse pad's note gets it too;
  GLITCH never does. CLASSIC keeps none (the HTML has none).
- **A more varied arpeggio, by the HTML's own arp rules.**
  - The chord's shape comes from where Scout-7 stands: x and the loop pick triad, seventh, sus4, add9, power or sixth.
  - Its span comes from the heat: two octaves at 4.
  - Its contour comes from the facing, as now.
  - How long its notes ring comes from the modifier.
  - Its rate comes from the tile: twice as fast riding a belt, half on a pulse pad.
  - When the robots are 4 or more tiles apart, Echo-3 takes the complementary chord.
