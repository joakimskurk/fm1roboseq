# RoboSeq for the M-VAVE FM-1

RoboSeq is a sequencer you program like two little robots: each bot runs its own register of steps (forward,
reverse, turn, dash, ping, idle) on a 5 x 5 board of belts, zap rails, rotators, pulse pads and crates, and where
they go, what they hit and how they meet becomes the melody, the bass and the effects. It started as a browser
instrument ([reference/roboseq.html](reference/roboseq.html)), which stays the reference for sound and feel. This is
its port to the FM-1 as a firmware, built on [Dinghy](https://github.com/zvenson/dinghy) 0.4 by zvenson and, under
that, the Felucca / SLOOP platform.

On the FM-1 it is meant to be played, not only programmed: steering the bots live is part of the instrument
(ROADMAP.md, the playability rules).

> **Status: phase 0.** The repo, the identity and the build are in place; the firmware is still Dinghy's sine demo
> under RoboSeq's name. Nothing is tested on an FM-1. Every flash can brick a unit: hold OCT- while switching on to
> reach the updater.

## Build

```sh
make test       # the app on this computer: build/host/roboseq.wav and the screens as PNG (Python 3 + Pillow)
make fw         # build/roboseq.fwsc (JieLi toolchain and AC79 SDK: docs/DINGHY_README.md)
make check      # the package and Dinghy's platform tests, as CI runs them
```

## Where things are

- [ROADMAP.md](ROADMAP.md): the phases and the playability rules
- [docs/roboseq/DECISIONS.md](docs/roboseq/DECISIONS.md): why things are as they are
- [PLATFORM.md](PLATFORM.md): the Dinghy commit and what differs from it
- [reference/](reference/): the browser original and its SHA-256
- `docs/API.md`, `docs/RULES.md`: Dinghy's platform and the chip's rules

## Licence

GPL-3.0-only, as Dinghy; [LICENSING.md](LICENSING.md) says who made what.
