# RoboSeq's controls on the FM-1

The rules every page keeps (as SkurkyMello's G1-G8), then what each control does now. The playing model is D15: the
robots are the instrument; D16 says how the keys reach them. A rule here is a test (P4) once a page is built on it.

## The rules (C1-C9, 2026-11-06)

| | Rule |
|---|---|
| C1 | **PLAY is the transport**, on every page: start from the board's start, stop with the HTML's panic. Nothing else. Its light flashes the beat (D29). |
| C2 | **The white keys are positions.** Where a page has steps (a register, Voice Mod, the drums) the 16 white keys are its steps, OCT- / OCT+ the next 16. A key turns its step to its next value, as the HTML's buttons cycle. |
| C3 | **The black keys steer the bots**, on every page but the two builders, SEQ and ENV (D28), while it plays (P1): the first five Scout-7's TURN L, MOVE, TURN R, DASH, PING, the next five Echo-3's, the last swaps the registers. A steer is the bot's next move, once. |
| C4 | **What a key does is on the screen** (P3); on the builders a little keyboard at the foot in each key's colour (D28): both registers down the board's sides (Scout-7 left, Echo-3 right, top to bottom = white keys left to right), the one on the white keys bright, the step playing framed; the registers swapped shows. A steer shows till its step plays: on HOME its icon over the bot's head, on the other pages in the bot's corner of the header (Scout-7 left, Echo-3 right), the swap in both. The FM-1's keys have no lights, so the screen is the only place. |
| C5 | **The result is heard on the next step**, never later: the game runs 64 ms ahead (D12), so an edit or a steer lands on the next step the game computes. |
| C6 | **EDIT picks whose register** is on the white keys (lit: Echo-3's), on every page with a register. |
| C7 | **SELECT picks which part of a step** the white keys turn: its ACTION, MODIFIER or CHANCE (the HTML's step card's three buttons), named over the board in the picked bot's colour; the register on the keys shows that part, the other its actions. |
| C9 | **REC turns the keys into the lead** (D21) on every page: all 27 play it, OCT- / OCT+ its octave, K1-K4 its cutoff, echo, bite and octave (D26, D30), shown as four faders in the header line. Only ARP (lit) arpeggiates: the chord held, K4 its kind (D27), SELECT its notes a step, 1-4 (D29, D30). REC again gives the keys and knobs back. |
| C8 | **The knobs never stop the music.** KNOB1 is the picked register's length (8, 12, 16 ... up to the loop's). What the HTML does only by stopping (the loop's length) waits for a page of its own. |

## The pages (D19)

HOME the board, SEQ the sequence, SCL Voice Mod and, pressed again, Seq 2 (D27), LFO the drums, FX the mix, ENV the boards and,
pressed again, the board builder (D32), GLO the global settings; the page's button is lit.

## HOME (the board)

| Control | Does |
|---|---|
| PLAY | start / stop (C1) |
| white keys 1-16 | the picked bot's register, steps 1-16 (OCT+ 17-32 ...): the next action, move > back > turn L > turn R > dash > ping > wait |
| black keys F#3 G#3 A#3 C#4 D#4 | Scout-7's next move: TURN L, MOVE, TURN R, DASH, PING |
| black keys F#4 G#4 A#4 C#5 D#5 | the same for Echo-3 |
| black key F#5 | swap the registers (the HTML's REG SWAP) |
| EDIT | Scout-7's or Echo-3's register on the white keys |
| SELECT | what a white key turns: ACTION (move > back > turn L > turn R > dash > ping > wait), MODIFIER (steady > bright > low > wobble > glitch), CHANCE (100 > 75 > 50 %) |
| KNOB1 | the picked register's length |
| OCT- / OCT+ | the register's 16 steps before / after (lit where there are more) |
| MASTER | the volume |

## SEQ (the sequence: the movement registers, D28)

Scout-7's and Echo-3's registers as two columns of step cards, the HTML's planner; the cursor on the picked one.

| Control | Does |
|---|---|
| SELECT | the cursor up / down (the list glides after it) |
| OCT- / OCT+ | the cursor 8 steps |
| EDIT | Scout-7's or Echo-3's register |
| A W S D (F3 F#3 G3 A3) | type TURN L, MOVE, BACK, TURN R at the cursor, and on to the next step |
| G#3 / A#3 / B3 | type WAIT / DASH / PING, and on |
| C4 D4 E4 F4 G4 | the cursor's modifier: steady, bright, low, wobble, glitch |
| A4 B4 C5 | its chance: 100, 75, 50 % |
| C#5 / D5 / E5 / F5 / G5 | undo / roll this register / roll both / swap the registers / clear them |
| KNOB1 | the picked register's length |

## GLO (the global settings)

| Control | Does |
|---|---|
| KNOB1 | the tempo, 60-168 BPM |
| KNOB2 | the bots move every 1, 2 or 4 ticks |
| KNOB3 | the loop's length (8 ... 64): at once when stopped, else as the loop comes round (D20) |
| KNOB4 | the speed: 1/1, 1/2, 1/4 (D22) |
| SELECT | the engine: BAND (RoboSeq's own, where it starts) or ORIGINAL (the HTML's score) (D33, D39; docs/roboseq/ENGINES.md) |

The white keys rest here (no register on the screen) but the last, 16, which opens BATTLE (D42); the black keys steer
as everywhere.

## BATTLE (GLO's white key 16, D42)

Two players, a robot each, on the music's beat. The keyboard is theirs: no register, no steering, no lead on REC. The
start screen draws the keyboard with their keys marked (arrows, a burst for the strike, R for reset), and in the
fight a little keyboard at the foot shows them; each key is lit while it is held.

| Control | Does |
|---|---|
| F3 F#3 G3 A3 | Scout-7: left, up, down, right. Held, a step that way on every beat (the last one pressed, of those held) |
| C4 | Scout-7's strike (a white key free between it and A3): with Echo-3 in reach (a tile on the way it holds or faces, or two with the tile between open), it strikes on the next beat; out of reach it plays the lead now, a tone of the chord where Scout-7 stands, on up the chord when pressed again on one tile, held as long as the key |
| C5 C#5 D5 E5 / G5 | the same for Echo-3 (F5 free) |
| F#4 (the middle) | RESET: the match again from round 1, the countdown first |
| PLAY | a match: the round, then 4 3 2 1 FIGHT on the beats; in a fight it stops (C1); on the winner, another match |
| KNOB1 | the tempo (the battle starts at 112 BPM when it was slower; leaving brings the old back) |
| KNOB2 | the rounds to win, 1 to 9 (before a match), a segment each in the score bars |
| KNOB3 | the arena: the presets (not mid-fight) |
| KNOB4 | each new round: the same arena, the next one, or a random roll |
| any page button | leaves the battle: the registers play again |

## SCL (Voice Mod, the HTML's Seq 1; the SEL button)

| Control | Does |
|---|---|
| white keys 1-4 | pick a step (framed) |
| KNOB1 / KNOB2 / KNOB3 | the picked step's scale / root / semitone (-12 ... +12) |
| KNOB4 | when it moves on: MANUAL, or every 1/4, 1/2, 1, 2, 4 loops |
| white key 12 | FOLLOW (lit): every round ends in a new progression, following on: the same key, or a fifth away (D38) |
| white key 13 | NEXT: on to its next step (MANUAL) |
| white key 14 | ROOT: the root transposes the sequence, on / off |
| white keys 15 / 16 | reset / random (random can be undone; in BAND one of 73 named progressions in one of six keys, D38) |

The header names the progression and its key when it has one (I V vi IV G); NEXT > the next round's, once it is
ready on the round's last step.

## Seq 2 (SCL pressed again)

| Control | Does |
|---|---|
| white keys 1-4 | pick a step (framed) |
| KNOB1 / KNOB2 | the picked step's board / drum pattern ("--": unchanged) |
| KNOB3 | the registers swapped on that step (clockwise on) |
| KNOB4 | the chain: the step's progression, "--" (as it is), RND (a new one, following on) or one of the named ones by number (its name at the foot) (D38) |
| white key 12 | Seq 2 on / off (queued while playing: it starts on Voice Mod's next wrap) |
| white keys 13 / 14 | roll Scout-7's / Echo-3's register as Seq 2 wraps |
| white keys 15 / 16 | reset / random |

## LFO (the drums)

| Control | Does |
|---|---|
| SELECT | the row on the white keys: kick, snare, hihat |
| white keys | turn that row's step on / off (16 steps, or 12 in a 12-step loop) |
| KNOB1 | the patterns: four, break, half, offbeat, triplet, minimal, then CLEAR (empty, off) |
| KNOB4 | the drums on (clockwise) / off |

## FX (the mix)

| Control | Does |
|---|---|
| white keys 1-7 | pick master, melody, bot B, percussion, FX, drums or the lead |
| KNOB1 | its level, 0-100 % (2 % a detent; it glides there, as the HTML's 50 ms ramp) |
| KNOB2 | how much the board's tiles colour the whole sound: room, send, tilt, drive (D24); 0 % the HTML's |

## ENV (the boards, D32)

Every board as a card with its tiles small: the one playing framed in mint, the one coming with the loop in yellow,
blinking. In BAND a board is also its bass groove (D35): picking one changes the groove as the loop comes round.

| Control | Does |
|---|---|
| white keys 1-6 | the presets: default, corridor, garden, gauntlet, spinner, maze (queued while playing) |
| white key 7 / 8 | a reroll / the user board |
| white keys 9 / 10 / 11 | save the board as the user's / clear the user board / the bots to their pads |
| black keys | steer the bots (C3) |
| KNOB1 | the presets, one by one |
| KNOB2 | a reroll's fullness: sparse, balanced, chaos |
| ENV | the board builder |

## ENV pressed again (the board builder, D28)

| Control | Does |
|---|---|
| A W S D (F3 F#3 G3 A3) | move the cursor (and set the way a belt will point) |
| G#3 | clear the tile |
| C4 | a belt (pointing the way the cursor last moved; on a belt: turn it) |
| D4 / E4 / F4 / G4 / A4 | rotator left / rotator right / zap / pulse pad / crate |
| C5 / D5 / E5 / F5 / G5 | reroll / the user board / save the board as the user's / clear the user board / bots to their pads |
| KNOB1 | the presets (queued while playing) |
| KNOB2 | a reroll's fullness: sparse, balanced, chaos |
