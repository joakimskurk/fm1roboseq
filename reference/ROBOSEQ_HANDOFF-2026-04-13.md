# ROBOSEQ Handoff and Progress Log

Last updated: 2026-04-13 (final pre-send update)
Main file: roboseq.html

## Purpose
This file is a portable handoff so you can continue in another account without losing:
- What has been built
- Why certain decisions were made
- What state is persisted
- What to test next

## Current Product State
ROBOSEQ is a single-file web app with:
- Program registers for two bots
- Board simulation with hazards and deterministic loop reset behavior
- Two musical voices plus board/event FX
- Voice Mod sequencer (4/8 steps) controlling scale/root/semitone data
- Mixer panel with persistent levels
- Speed toggle including 1/4 playback mode

## Major Implemented Changes (Chronological Summary)
1. Added transport scale control and compacted transport UI.
2. Reworked belt arrows to pixel-art SVG for all directions.
3. Repaired multiple syntax regressions that caused black page load failures.
4. Stabilized layout (planner/board/event log) to prevent bouncing and shifts.
5. Replaced dropdown-heavy controls with cycle buttons in key areas.
6. Added settings/mixer and persistent gain buses (master/melody/botB/percussion/fx).
7. Added Voice Mod sequencer with 4/8 length, reset/random actions, and persistence.
8. Added root-transpose toggle behavior.
9. Improved STOP reliability with transport cancellation and panic audio release.
10. Added 1/4 speed mode tied to effective transport BPM and note duration.
11. Full sound-engine redesign toward advanced 8-bit style.
12. Bass behavior changed to: root + scale + chromatic semitone shift.
13. Bass semitone range expanded to +/-12 (not scale-step intervals).
14. Program register action/modifier/repeat buttons color-mapped for readability.
15. Theme shifted to playful green/BMO-inspired palette.

## Audio Architecture (Current)
- Tone.js based
- Audio chain includes:
  - limiter -> master gain
  - chip-style coloration (bit-crush + EQ + delay)
  - separate buses for melody, bass, percussion, FX
- Lead and bass are separate synth voices with action-aware phrase logic
- Board events trigger dedicated one-shot FX voices

## Bass Rules (Current)
Bass note generation now follows this model:
1. Build a base pitch from action/modifier motion.
2. Apply root/scale quantization.
3. Apply chromatic semitone offset from Voice Mod step.
4. Clamp semitone control to [-12, +12].

This matches your requested behavior: semitone offsets are chromatic, not interval-in-scale.

## Persisted Local State
Saved in browser localStorage under key:
- roboseq-mobile-v1

Includes:
- bpm, loopLength, scale
- playbackRate (normal or 1/4)
- transposeWithRoot
- mixer values
- bassMod sequencer settings
- bot step programs

## IMPORTANT: Moving to Another Account/Profile
This handoff file preserves context, but browser localStorage does NOT automatically move across accounts/profiles.

To preserve exact sequence/mixer state, also export localStorage value for key:
- roboseq-mobile-v1

Quick manual method:
1. Open app in browser.
2. Open DevTools -> Application (or Storage) -> Local Storage.
3. Copy value of roboseq-mobile-v1.
4. In new account/profile, open same app and set that key/value in localStorage.

## Quick Technical Anchors in roboseq.html
- Sound engine construction: buildAudio()
- Sequenced voice triggering: playActionSound()
- Board event SFX: playEventSound()
- Pitch mapping: noteFromStep()
- Hard stop logic: hardStopTransportAndAudio() and stopPlayback()
- Voice Mod UI: renderBassModSequencer()

## Recommended Next Test Pass
1. Verify STOP always halts on first press during heavy event density.
2. Verify bass semitone +/-12 clearly shifts pitch while still respecting root/scale base.
3. Verify 1/4 speed keeps animation, sequencing, and audio timing in sync.
4. Validate mixer balance after reload from localStorage.

## Notes for Future Editing
- Keep this as single-file app unless intentionally splitting CSS/JS.
- After any audio changes, run a STOP/PLAY stress test immediately.
- If adding more modulation controls, persist them in both loadState() and saveState().

## CRITICAL CURRENT STATUS (Resolved)
- **Audio fix confirmed working (2026-04-13).**
- Root cause: BitCrusher(5)/EQ3/Filter chain was the sole signal path — BitCrusher silenced all output in Tone.js 14.7.77.
- Fix: Gains now connect directly to masterGain. A parallel FeedbackDelay send provides subtle color without being in the critical path.
- Diagnostic beep on ARM confirms audio context is alive.
- Board animation, sequencing, and audio all functional.

## Latest Audio Debug Attempts Already Done
1. Replaced unsupported oscillator type strings that could crash Tone voice creation.
2. Added runtime status error messages:
  - Audio wake-up failed: <error>
  - Playback trigger error: <error>
3. Added try/catch around playActionSound() so one trigger failure does not silently kill playback.
4. Added buildAudio() fallback path with simpler, safer synth graph.
5. Reworked STOP/PLAY transport handling multiple times:
  - hardStopTransportAndAudio()
  - scheduler clear/recreate
  - transport cancel/reset
6. Removed persistent master gain zeroing from STOP.
7. Forced mixer gain restoration at PLAY start before transport start.
8. Tightened toggle-play state check to use Tone.Transport.state === started for stop path.

## Most Likely Remaining Root Cause
- Browser audio output/device policy or context output path mismatch despite Tone context starting.
- Secondary possibility: graph still builds but output chain is effectively muted in edge runtime state.

## Fastest Next-Step Debug Script (for next person)
1. In armAudio(), after Tone.start(), set:
  - state.message = "Tone state: " + Tone.context.state
2. Add one direct test beep immediately after buildAudio/ensureScheduler:
  - new Tone.Synth().toDestination().triggerAttackRelease("C4", 0.2)
3. If beep is audible but app still silent:
  - bypass full chain temporarily by connecting bot voices directly to destination
  - confirm sequence callbacks fire and voice triggers execute
4. If beep is not audible:
  - verify browser/site audio permissions and output device routing
  - test same file in a fresh browser profile

## Emergency Fallback For Delivery
If immediate reliable sound is required before further refactor, temporarily force minimal engine:
- In buildAudio(), return only simple Synth/MonoSynth voices connected directly to destination via mixer gains.
- Avoid BitCrusher/EQ/Delay and advanced FX nodes until sound baseline is confirmed.
