# RoboSeq's controls on the FM-1

The rules every page keeps (as SkurkyMello's G1-G8), then what each control does now. The playing model is D15: the
robots are the instrument; D16 says how the keys reach them. A rule here is a test (P4) once a page is built on it.

## The rules (C1-C6, 2026-11-06)

| | Rule |
|---|---|
| C1 | **PLAY is the transport**, on every page: start from the board's start, stop with the HTML's panic. Nothing else. |
| C2 | **The white keys are positions.** Where a page has steps (a register, Voice Mod, the drums) the 16 white keys are its steps, OCT- / OCT+ the next 16. A key turns its step to its next value, as the HTML's buttons cycle. |
| C3 | **The black keys steer the bots**, on every page while it plays (P1): the first five Scout-7's TURN L, MOVE, TURN R, DASH, PING, the next five Echo-3's, the last swaps the registers. A steer is the bot's next move, once. |
| C4 | **What a key does is on the screen** (P3): both registers down the board's sides (Scout-7 left, Echo-3 right, top to bottom = white keys left to right), the one on the white keys bright, the step playing framed; the registers swapped shows. The FM-1's keys have no lights, so the screen is the only place. |
| C5 | **The result is heard on the next step**, never later: the game runs 64 ms ahead (D12), so an edit or a steer lands on the next step the game computes. |
| C6 | **SEQ picks whose register** is on the white keys (lit: Echo-3's). |

## HOME (the board)

| Control | Does |
|---|---|
| PLAY | start / stop (C1) |
| white keys 1-16 | the picked bot's register, steps 1-16 (OCT+ 17-32 ...): the next action, move > back > turn L > turn R > dash > ping > wait |
| black keys F#3 G#3 A#3 C#4 D#4 | Scout-7's next move: TURN L, MOVE, TURN R, DASH, PING |
| black keys F#4 G#4 A#4 C#5 D#5 | the same for Echo-3 |
| black key F#5 | swap the registers (the HTML's REG SWAP) |
| SEQ | Scout-7's or Echo-3's register on the white keys |
| OCT- / OCT+ | the register's 16 steps before / after (lit where there are more) |
| MASTER | the volume |
