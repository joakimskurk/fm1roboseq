# RoboSeq's controls on the FM-1

The rules every page keeps (as SkurkyMello's G1-G8), then what each control does now. The playing model is D15: the
robots are the instrument; D16 says how the keys reach them. A rule here is a test (P4) once a page is built on it.

## The rules (C1-C8, 2026-11-06)

| | Rule |
|---|---|
| C1 | **PLAY is the transport**, on every page: start from the board's start, stop with the HTML's panic. Nothing else. |
| C2 | **The white keys are positions.** Where a page has steps (a register, Voice Mod, the drums) the 16 white keys are its steps, OCT- / OCT+ the next 16. A key turns its step to its next value, as the HTML's buttons cycle. |
| C3 | **The black keys steer the bots**, on every page while it plays (P1): the first five Scout-7's TURN L, MOVE, TURN R, DASH, PING, the next five Echo-3's, the last swaps the registers. A steer is the bot's next move, once. |
| C4 | **What a key does is on the screen** (P3): both registers down the board's sides (Scout-7 left, Echo-3 right, top to bottom = white keys left to right), the one on the white keys bright, the step playing framed; the registers swapped shows. The FM-1's keys have no lights, so the screen is the only place. |
| C5 | **The result is heard on the next step**, never later: the game runs 64 ms ahead (D12), so an edit or a steer lands on the next step the game computes. |
| C6 | **EDIT picks whose register** is on the white keys (lit: Echo-3's), on every page with a register. |
| C7 | **SELECT picks which part of a step** the white keys turn: its ACTION, MODIFIER or CHANCE (the HTML's step card's three buttons), named over the board in the picked bot's colour; the register on the keys shows that part, the other its actions. |
| C8 | **The knobs never stop the music.** KNOB1 is the picked register's length (8, 12, 16 ... up to the loop's). What the HTML does only by stopping (the loop's length) waits for a page of its own. |

## The pages (D19)

HOME the board, SEQ the sequence, SCL Voice Mod, ARP Seq 2, LFO the drums, FX the mix, ENV the boards, GLO the
global settings; the page's button is lit.

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

## SEQ (the sequence)

Both registers as one list, Scout-7 left and Echo-3 right, each step its action, modifier and chance, the step
each plays framed. The white keys, SELECT, EDIT, KNOB1 and the black keys as on HOME; the bar on the left edge marks
the 16 rows on the white keys.

| Control | Does |
|---|---|
| KNOB2 | scroll a row (it glides) |
| OCT- / OCT+ | scroll 16 rows |
| (let go 3 s) | it follows the step playing |

## GLO (the global settings)

| Control | Does |
|---|---|
| KNOB1 | the tempo, 60-168 BPM |
| KNOB2 | the bots move every 1, 2 or 4 ticks |
| KNOB3 | the loop's length (8 ... 64): at once when stopped, else as the loop comes round (D20) |
| KNOB4 | 1/4 speed (clockwise) or normal |

The white keys rest here (no register on the screen); the black keys steer as everywhere.

## SCL (Voice Mod, the HTML's Seq 1; the SEL button)

| Control | Does |
|---|---|
| white keys 1-4 | pick a step (framed) |
| KNOB1 / KNOB2 / KNOB3 | the picked step's scale / root / semitone (-12 ... +12) |
| KNOB4 | when it moves on: MANUAL, or every 1/4, 1/2, 1, 2, 4 loops |
| white key 13 | NEXT: on to its next step (MANUAL) |
| white key 14 | ROOT: the root transposes the sequence, on / off |
| white keys 15 / 16 | reset / random (random can be undone) |

## ARP (Seq 2)

| Control | Does |
|---|---|
| white keys 1-4 | pick a step (framed) |
| KNOB1 / KNOB2 | the picked step's board / drum pattern ("--": unchanged) |
| KNOB3 | the registers swapped on that step (clockwise on) |
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

