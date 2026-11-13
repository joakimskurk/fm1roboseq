// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Joakim (joakimskurk)
// What the HTML's chain really passes: one sound on each bus through its own buildAudio() under Tone.Offline, and
// the level of each in its window, and after it (a reverb would ring there). node tools/tone-ref/probe_chain.mjs
import { openBrowser, openPage, render, rms, db, wavFloat32 } from "./page.mjs";

const probes = [
    // [name, at (s), the call]
    ["fx bus: collision (G2)", 0.2, "a.fx.collision.triggerAttackRelease('G2', 0.08, 0.2, 0.7)"],
    ["melody bus: A.main (A3)", 1.2, "a.botA.main.triggerAttackRelease('A3', 0.14, 1.2, 0.75)"],
    ["bot B bus: B.fm (C5)", 2.2, "a.botB.fm.triggerAttackRelease('C5', 0.14, 2.2, 0.5)"],
    ["percussion bus: zap noise", 3.2, "a.fx.zap.triggerAttackRelease(0.06, 3.2, 0.6)"],
    ["drum bus: kick", 4.2, "a.drums.kick.triggerAttackRelease('C1', 0.18, 4.2, 1)"],
    ["coin (fx bus)", 5.2, "a.coin.triggerAttackRelease('B5', 0.06, 5.2, 0.7)"],
];
const body = `
    const a = rs.buildAudio();
    rs.setAudio(a);
    if (globalThis.__reverbReady) await globalThis.__reverbReady;
    ${probes.map((p) => p[2]).join(";\n    ")};
`;
const browser = await openBrowser();
try {
    // UNCYCLED=1: the reverb's return to the master instead of into the FX bus (what the cycle silences)
    const uncycled = !!process.env.UNCYCLED;
    const page = await openPage(browser, 12345, (src) => (uncycled ? src.replace("hallReverb.connect(fxGain);", "hallReverb.connect(masterGain);") : src));
    const version = await page.evaluate(() => Tone.version);
    const [L, R] = await render(page, body, 6.5);
    wavFloat32(uncycled ? "build/probe_chain_uncycled.wav" : "build/probe_chain.wav", L, R);
    console.log(`Tone ${version}, the HTML's buildAudio() under Tone.Offline${uncycled ? ", the reverb to the master (UNCYCLED)" : ""}`);
    for (const [name, at] of probes) {
        const on = db(rms(L, at, at + 0.15)), tail = db(rms(L, at + 0.35, at + 0.9));
        console.log(`  ${name.padEnd(28)} ${on.toFixed(1).padStart(7)} dBFS   after it ${tail.toFixed(1).padStart(7)} dBFS`);
    }
} finally {
    await browser.close();
}
