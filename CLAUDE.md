# Working on RoboSeq for the FM-1

- Bare-metal FM-1 firmware on Dinghy 0.4 (PLATFORM.md). Read README.md and ROADMAP.md first; the current phase is the
  first unchecked one in ROADMAP.md. Decisions go in docs/roboseq/DECISIONS.md.
- `reference/roboseq.html` is the reference for sound, behaviour and feel. When the C code and the HTML disagree,
  the HTML wins unless DECISIONS.md says otherwise. Its SHA-256 is in reference/README.md: a new HTML is a new
  reference and a commit of its own.
- **Playability is a requirement, not a nice-to-have.** RoboSeq on the FM-1 must have an element of play, of
  steering the bots and the music live, even if the way it is done is unorthodox. A page or feature that only
  edits and never plays is not done. Check every UI change against the playability rules in ROADMAP.md.
- Learned on SkurkyMello (joakimskurk/fm1mello, docs/review-2026-10): the gap to the HTML sits in the **sound** and
  the **controls**, not in the feature list. So: the sound is checked against Tone.js renders of the HTML from the
  first voice (exponential envelopes, the HTML's gain staging and effects as they are, not "better" ones); the
  control rules are written before the pages; the UI map is generated from the code.
- The user has no physical FM-1. Builds are tested on the host and in fm1-emu. Hand every build meant for testing
  over as the `.fwsc` file itself, with its commit and SHA-256.
- After every change: `make test`. Keep a change small enough to bisect: one fix per commit.
- Audio (the app's render in the audio interrupt): no flash, no screen, no unbounded loops, no float, no 64-bit
  division; fixed point stays in range. Never two RNG draws in one argument list (C leaves the order open).
- Dinghy's own files (`platform.c`, `gfx.c`, `lcd.c`, `hal/`, `loader/`, `tools/`, `tests/host.c`) stay as upstream;
  PLATFORM.md lists the few differences. Change them only when the task needs it, and say so in the commit.
- Never install a package on an FM-1, push, merge to main or tag a release without asking.
