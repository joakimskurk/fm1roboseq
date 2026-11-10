# The first RoboSeq on a real FM-1

RoboSeq has never run on a real FM-1. It is built and tested on a computer: the game is checked tick by tick
against the browser original, every page's keys are checked by a test, and the package runs in
[fm1-emu](https://github.com/joakimeriksson/fm1-emu) in its strict mode, saves included. What only a real FM-1 can
show is how it sounds through its speaker and jacks, how the keys and knobs feel, and whether the flash and the
update loader behave as the emulator says. If you have an FM-1 and half an hour, this page is for you.

## What could go wrong, and the way back

Nothing here is new to the FM-1's update path: RoboSeq is built on Dinghy (0.4, with 0.5's flash fix), whose
loader and USB rescue are in the community firmwares on the [Firmware Hub](https://thefirmwarehub.com). Still, before
you start:

- **The USB rescue.** Hold **OCT-** while you switch on: no sound, only the updater. From there any firmware
  installs again, the stock V15 or V16 included. If RoboSeq crashes twice at boot, the FM-1 goes there by itself.
- **The newer flash chip.** FM-1s sold since autumn 2026 carry a UC25HQ80 flash chip (JEDEC `0xB36014`) in place of
  the Puya P25Q80 (`0x856014`). RoboSeq knows both, in the firmware, the USB rescue and the update loader, so it saves
  and updates on either. But an older custom firmware may not know the new chip: if yours runs one, its update loader
  can refuse to install anything. Then go back to the official firmware first (M-VAVE's M-UPGRADE with V16), and
  install RoboSeq from there.
- **If even the USB rescue does not come up** (a black screen, no USB): the community's recovery tools reach the
  chip's own boot ROM over plain USB, such as [MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker), or
  with a small dongle, such as [FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter). More of them, and
  how they work, are listed in [aroum/fm1-custom-fw](https://github.com/aroum/fm1-custom-fw).
- **A crash** shows a screen with where it happened (a PC address) and restarts. Photograph that screen.
- **Your data.** RoboSeq keeps its project in sectors of its own and never reads or writes another firmware's
  projects or samples. Their data stays where it is, but RoboSeq cannot open it.
- **Going back:** install the firmware you had, from the Hub in Chrome or Edge, or with the tool below.

## What you need

- An FM-1 on M-VAVE's V15 or V16, or on any firmware with the FM-1 update loader (all Hub firmwares have it).
- A USB cable and a computer with Python 3, or Chrome / Edge for the Hub.
- `tools/fm1_install.py` from this repository (a clone, or the file alone).
- The package: `roboseq-0.1.fwsc` from the [release](https://github.com/joakimskurk/fm1roboseq/releases) (or the
  newest one's), and `SHA256SUMS` beside it. Check it: the number has to be the one in `SHA256SUMS`, and in the
  release notes.

```sh
shasum -a 256 roboseq-0.1.fwsc
```

## Install

```sh
pip3 install mido python-rtmidi
python3 tools/fm1_install.py --info               # what the FM-1 runs now: write it down
python3 tools/fm1_install.py roboseq-0.1.fwsc      # the install, about a minute
python3 tools/fm1_install.py --info               # now: FM-1_965, RoboSeq
```

The tool asks before it writes, and checks after the restart that the FM-1 runs `FM-1_965`.

## The checklist

Tick what works, and note what doesn't: what you did, what you saw or heard, what you expected.

**Boot and sound**
- [ ] The splash: the two robots walk in, meet under a heart, the name, the version in the corner (`v0.1`). Any
      key skips it.
- [ ] HOME: the board, the programs down its sides. PLAY starts it: music, and PLAY flashes on the beat.
- [ ] Every output the FM-1 has plays, and USB audio on a computer. MASTER is the volume.
- [ ] No clicks, crackle or dropouts while it plays, for five minutes at least, with the default song and on MAZE
      (ENV, white key 6), which is the busiest board.

**Playing it** (README.md's first five minutes and lessons)
- [ ] The black keys nudge: G#3 makes Scout-7's next move a step forward, its icon over its head till that step
      plays; then its program goes on.
- [ ] The white keys on HOME turn Scout-7's steps; EDIT gives them to Echo-3.
- [ ] Every page's button opens its page, lit: SEQ, SEL (and SEL again), LFO, FX, ENV (and ENV again), GLO, HOME.
- [ ] The knobs: one click is one step, both ways, on GLO's tempo and the FX faders.
- [ ] REC: all 27 keys play the lead, OCT- / OCT+ its octave, K1-K4 its sound. ARP arpeggiates a held chord.
- [ ] GLO, white key 16: BATTLE. Two players, PLAY: the countdown with its beeps, the fight on the beat, a winner.

**Saving** (D43)
- [ ] Change the tempo on GLO while it plays: SAVE lights up.
- [ ] Stop. After about three seconds, a short break in any sound still ringing, and SAVE goes out.
- [ ] Switch off and on: the tempo you set is there.
- [ ] Change it again, stopped, and press SAVE: it goes out at once. Off and on: kept.

**Leaving and coming back** (the Hub lists a firmware only when both work)
- [ ] From RoboSeq, install another firmware (the Hub, or the tool). It installs and runs.
- [ ] From that firmware, install RoboSeq again. It installs and runs, and your saved tempo is still there.

## What to send back

Open an issue at [joakimskurk/fm1roboseq](https://github.com/joakimskurk/fm1roboseq/issues) with:

- the checklist, ticked;
- what `fm1_install.py --info` said before and after;
- the flash chip your unit carries, if you know it (the Puya P25Q80, or the UC25HQ80 of units sold since autumn
  2026), and the stock firmware it came with (V14, V15, V16);
- the package's SHA-256, and the version on the splash;
- photos of anything that looked wrong, a crash screen's PC, and a recording of anything that sounded wrong
  (a phone is fine).

Thank you. With your report RoboSeq can go on the Firmware Hub, where a firmware is listed only once it has been
tested on a real FM-1.
