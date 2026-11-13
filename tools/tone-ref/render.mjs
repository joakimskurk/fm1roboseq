// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Joakim (joakimskurk)
// The golden renders: every stimulus in stimuli.json through reference/roboseq.html's own buildAudio() (D9: the
// reverb's return to the master) under Tone.Offline in headless Chromium, as WAVs in
// tests/golden/html/ (voices 16-bit mono, chains float stereo). A fresh page per stimulus, Math.random seeded, so they repeat.
//   cd tools/tone-ref && npm ci && npm run render [-- name ...]      (make reference)
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { createHash } from "node:crypto";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { openBrowser, openPage, render, SR } from "./page.mjs";
import { HTML } from "../trace/html.mjs";

const here = dirname(fileURLToPath(import.meta.url));
const out = process.env.RS_OUT || join(here, "..", "..", "tests", "golden", "html");   // RS_OUT, RS_SEED: another take
const seed = Number(process.env.RS_SEED || 12345);
const spec = JSON.parse(readFileSync(join(here, "stimuli.json"), "utf8"));
const only = process.argv.slice(2);
const D9 = (src) => src.replace("hallReverb.connect(fxGain);", "hallReverb.connect(masterGain);");

function wav16(path, x) {                              // 16-bit PCM, mono: a voice (its peaks are under 0 dBFS)
    const n = x.length, data = Buffer.alloc(n * 2), h = Buffer.alloc(44);
    for (let i = 0; i < n; i++) data.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(x[i] * 32768))), i * 2);
    h.write("RIFF", 0); h.writeUInt32LE(36 + data.length, 4); h.write("WAVE", 8); h.write("fmt ", 12);
    h.writeUInt32LE(16, 16); h.writeUInt16LE(1, 20); h.writeUInt16LE(1, 22); h.writeUInt32LE(SR, 24);
    h.writeUInt32LE(SR * 2, 28); h.writeUInt16LE(2, 32); h.writeUInt16LE(16, 34); h.write("data", 36);
    h.writeUInt32LE(data.length, 40);
    writeFileSync(path, Buffer.concat([h, data]));
}

function wav(path, chans) {                            // 32-bit float, 1 or 2 channels
    const n = chans[0].length, c = chans.length, data = Buffer.alloc(n * 4 * c), h = Buffer.alloc(44);
    for (let i = 0; i < n; i++) for (let k = 0; k < c; k++) data.writeFloatLE(chans[k][i], (i * c + k) * 4);
    h.write("RIFF", 0); h.writeUInt32LE(36 + data.length, 4); h.write("WAVE", 8); h.write("fmt ", 12);
    h.writeUInt32LE(16, 16); h.writeUInt16LE(3, 20); h.writeUInt16LE(c, 22); h.writeUInt32LE(SR, 24);
    h.writeUInt32LE(SR * 4 * c, 28); h.writeUInt16LE(4 * c, 32); h.writeUInt16LE(32, 34); h.write("data", 36);
    h.writeUInt32LE(data.length, 40);
    writeFileSync(path, Buffer.concat([h, data]));
}

const prelude = `
    const a = rs.buildAudio();
    rs.setAudio(a);
    if (globalThis.__reverbReady) await globalThis.__reverbReady;
    // D10: a voice started again at the time of its last start is skipped (Tone would throw and lose the tick)
    for (const group of Object.values(a)) for (const v of Object.values(group)) {
        if (!v || typeof v.triggerAttackRelease !== "function") continue;
        const tar = v.triggerAttackRelease.bind(v);
        const starts = new Set();
        v.triggerAttackRelease = (...args) => {
            const time = typeof args[0] === "number" ? args[1] : args[2];
            if (starts.has(time)) return v;
            starts.add(time);
            return tar(...args);
        };
    }
`;

// A voice stimulus's start times on Chromium's 128-frame render quanta: an oscillator started inside a quantum
// begins at a phase that depends on where in it (and on the frequency before: Chromium, not Tone, not the spec),
// one started on its edge at 0, as the C voices always do (DECISIONS.md D11). compare.py snaps the same way.
export const snap = (t) => (Math.round((t * SR) / 128) * 128) / SR;

function voiceBody(st) {
    const path = st.voice.split(".").map((k) => `[${JSON.stringify(k)}]`).join("");
    const notes = [];
    const [times, every] = st.repeat || [1, 0];
    for (let r = 0; r < times; r++) for (const [n, d, t, v] of st.notes) notes.push([n, d, t + r * every, v]);
    const calls = notes.map(([note, dur, time0, vel]) => [note, dur, snap(time0), vel]).map(([note, dur, time, vel]) =>
        note === null ? `v.triggerAttackRelease(${dur}, ${time}, ${vel});` : `v.triggerAttackRelease(${JSON.stringify(note)}, ${dur}, ${time}, ${vel});`);
    return `${prelude}
    const v = a${path};
    v.disconnect();
    v.toDestination();
    ${calls.join("\n    ")}`;
}

function chainBody(st) {
    if (!st.song) return prelude + st.calls.join(";\n    ") + ";";
    return `${prelude}
    const s = rs.state;
    rs.resetBoardState(false);
    s.playing = true;
    const transport = Tone.getContext().transport;     // (inside Tone.Offline: the offline context's)
    transport.bpm.value = s.bpm * (s.playbackRate || 1) * (s.ticksPerMove || 1);   // getPlaybackBpm()
    transport.scheduleRepeat((time) => rs.advanceSequence(time), "16n");            // ensureScheduler()
    transport.start(0);`;
}

if (process.argv[1] && process.argv[1].endsWith("render.mjs")) await main();

async function main() {
const browser = await openBrowser();
const html = createHash("sha256").update(readFileSync(HTML)).digest("hex");
const made = [];
try {
    mkdirSync(out, { recursive: true });
    let version = "";
    for (const st of [...spec.voices, ...spec.chains]) {
        if (only.length && !only.includes(st.name)) continue;
        const page = await openPage(browser, seed, D9);
        version = await page.evaluate(() => Tone.version);
        const isVoice = !!st.voice;
        const [L, R] = await render(page, isVoice ? voiceBody(st) : chainBody(st), st.seconds);
        if (isVoice) wav16(join(out, st.name + ".wav"), L);
        else wav(join(out, st.name + ".wav"), [L, R]);  // (a chain's peaks can pass 0 dBFS: float)
        let peak = 0;
        for (let i = 0; i < L.length; i++) peak = Math.max(peak, Math.abs(L[i]));
        console.log(`${st.name.padEnd(14)} ${st.seconds.toFixed(1)} s  peak ${(20 * Math.log10(peak || 1e-12)).toFixed(1)} dBFS`);
        made.push(st.name);
        await page.close();
    }
    if (!only.length && !process.env.RS_OUT)
        writeFileSync(join(out, "VERSION"), `Tone ${version}, reference/roboseq.html ${html}, D9 (the reverb to the master)\n`);
} finally {
    await browser.close();
}
}
