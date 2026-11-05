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
