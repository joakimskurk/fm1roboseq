// SPDX-License-Identifier: GPL-3.0-only
// The HTML's mix pieces on their own, as Tone 14.7.77 builds them in Chromium (for 2c: the C port's models of them):
//   limiter   Tone.Limiter(-6) (Chromium's DynamicsCompressor) fed a test signal: a 440 Hz sine in level steps,
//             then bursts; build/probe/limiter_in.wav and limiter_out.wav
//   reverb    Tone.Reverb({ decay: 1.2, preDelay: 0.01, wet: 1 })'s impulse response: build/probe/reverb_ir.wav
//   lowpass   Tone.Filter({ type: "lowpass", frequency: 1400, rolloff: -12 })'s impulse response
// node tools/tone-ref/probe_mix.mjs
import { mkdirSync } from "node:fs";
import { openBrowser, openPage, render, wavFloat32, SR } from "./page.mjs";

// the limiter's test signal, made the same way here and in the page
const SIGNAL = `
    const sr = ${SR}, n = Math.round(14 * sr), x = new Float32Array(n);
    let i = 0;
    for (let db = -36; db <= 12; db += 3) {                 // steps of 0.35 s, 3 dB apart
        const a = Math.pow(10, db / 20);
        for (let k = 0; k < 0.35 * sr; k++, i++) x[i] = a * Math.sin(2 * Math.PI * 440 * i / sr);
    }
    for (let b = 0; b < 8; b++) {                           // bursts: 0.03..0.3 s at +6 dB, then quiet -24 dB
        const len = Math.round((0.03 + 0.04 * b) * sr);
        for (let k = 0; k < len && i < n; k++, i++) x[i] = 2 * Math.sin(2 * Math.PI * 220 * i / sr);
        for (let k = 0; k < 0.4 * sr && i < n; k++, i++) x[i] = 0.063 * Math.sin(2 * Math.PI * 220 * i / sr);
    }
`;

const bodies = {
    limiter: `${SIGNAL}
        const ctx = Tone.getContext().rawContext;
        const buf = ctx.createBuffer(1, n, sr);
        buf.copyToChannel(x, 0);
        const src = ctx.createBufferSource();
        src.buffer = buf;
        const lim = new Tone.Limiter(-6).toDestination();
        Tone.connect(src, lim);
        src.start(0);`,
    reverb: `
        const ctx = Tone.getContext().rawContext;
        const buf = ctx.createBuffer(1, 1, ${SR});
        buf.getChannelData(0)[0] = 1;
        const src = ctx.createBufferSource();
        src.buffer = buf;
        const rev = new Tone.Reverb({ decay: 1.2, wet: 1.0, preDelay: 0.01 });
        await rev.ready;
        rev.toDestination();
        Tone.connect(src, rev);
        src.start(0);`,
    lowpass: `
        const ctx = Tone.getContext().rawContext;
        const buf = ctx.createBuffer(1, 1, ${SR});
        buf.getChannelData(0)[0] = 1;
        const src = ctx.createBufferSource();
        src.buffer = buf;
        const lp = new Tone.Filter({ type: "lowpass", frequency: 1400, rolloff: -12 }).toDestination();
        Tone.connect(src, lp);
        src.start(0);`,
};

mkdirSync("build/probe", { recursive: true });
const browser = await openBrowser();
try {
    for (const [name, body] of Object.entries(bodies)) {
        const page = await openPage(browser, 12345);
        const [L, R] = await render(page, body, name === "limiter" ? 14 : 1.5);
        wavFloat32(`build/probe/${name === "limiter" ? "limiter_out" : name === "reverb" ? "reverb_ir" : "lowpass_ir"}.wav`, L, R);
        if (name === "limiter") {
            const x = await page.evaluate(`(() => { ${SIGNAL}; return Array.from(x); })()`);
            wavFloat32("build/probe/limiter_in.wav", Float32Array.from(x), Float32Array.from(x));
        }
        console.log(name, "done");
        await page.close();
    }
} finally {
    await browser.close();
}
