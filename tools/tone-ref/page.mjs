// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Joakim (joakimskurk)
// reference/roboseq.html's own script (tools/trace/html.mjs's patch) on a page in headless Chromium, with Tone 14.7.77
// as the page loads it, for Tone.Offline renders of its own buildAudio(). The browser: $CHROME, else Brave, Chrome
// or Chromium where they usually are.
import { existsSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import puppeteer from "puppeteer-core";
import { roboSeqScript } from "../trace/html.mjs";

const here = dirname(fileURLToPath(import.meta.url));
export const SR = 44100;

function findBrowser() {
    const c = [
        process.env.CHROME,
        "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser",
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        "/Applications/Chromium.app/Contents/MacOS/Chromium",
        "/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser",
    ].filter(Boolean);
    const hit = c.find((p) => existsSync(p));
    if (!hit) throw new Error("no Chromium-family browser found: set CHROME=/path/to/chrome");
    return hit;
}

export async function openBrowser() {
    return puppeteer.launch({
        executablePath: findBrowser(),
        headless: true,
        args: ["--no-first-run", "--no-default-browser-check", "--autoplay-policy=no-user-gesture-required"],
    });
}

// a fresh page: Math.random seeded (the noise buffers and the reverb's impulse come from it), Tone, the HTML's script
export async function openPage(browser, seed = 12345, patch = (src) => src) {
    const page = await browser.newPage();
    page.on("console", (m) => { if (m.type() === "error" || m.type() === "warn") console.error("page:", m.text()); });
    page.on("pageerror", (e) => console.error("page error:", e.message));
    await page.setContent('<!doctype html><title>tone-ref</title><div id="app"></div>');
    await page.evaluate((s) => {
        let x = s >>> 0;                                // mulberry32: Math.random fixed, so renders repeat
        Math.random = () => { x = (x + 0x6D2B79F5) >>> 0; let t = x; t = Math.imul(t ^ (t >>> 15), t | 1);
            t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
        const store = new Map();                        // (about:blank has no localStorage: the page's state in memory)
        Object.defineProperty(window, "localStorage", { configurable: true, value: {
            getItem: (k) => (store.has(k) ? store.get(k) : null), setItem: (k, v) => store.set(k, String(v)),
            removeItem: (k) => store.delete(k) } });
    }, seed);
    await page.addScriptTag({ path: join(here, "node_modules", "tone", "build", "Tone.js") });
    await page.addScriptTag({ content: patch(roboSeqScript()) });
    return page;
}

// renders `body` (an async function's source, given the HTML's internals rs and the Offline transport) for `seconds`
// with Tone.Offline at 44.1 kHz; returns [left, right] Float32Arrays
export async function render(page, body, seconds) {
    const [l, r] = await page.evaluate(async (body, seconds, sr) => {
        const fn = new Function("rs", "transport", "return (async () => {" + body + "})();");
        const buf = await Tone.Offline(async (transport) => { await fn(globalThis.__rs, transport); }, seconds, 2, sr);
        const b64 = (a) => { const u = new Uint8Array(a.buffer); let s = ""; for (let i = 0; i < u.length; i += 0x8000) s += String.fromCharCode.apply(null, u.subarray(i, i + 0x8000)); return btoa(s); };
        return [b64(buf.getChannelData(0)), b64(buf.getChannelData(1))];
    }, body, seconds, SR);
    const f = (b) => new Float32Array(Uint8Array.from(Buffer.from(b, "base64")).buffer);
    return [f(l), f(r)];
}

export function wavFloat32(path, left, right, rate = SR) {
    const n = left.length, data = Buffer.alloc(n * 8), h = Buffer.alloc(44);
    for (let i = 0; i < n; i++) { data.writeFloatLE(left[i], i * 8); data.writeFloatLE(right[i], i * 8 + 4); }
    h.write("RIFF", 0); h.writeUInt32LE(36 + data.length, 4); h.write("WAVE", 8); h.write("fmt ", 12);
    h.writeUInt32LE(16, 16); h.writeUInt16LE(3, 20); h.writeUInt16LE(2, 22); h.writeUInt32LE(rate, 24);
    h.writeUInt32LE(rate * 8, 28); h.writeUInt16LE(8, 32); h.writeUInt16LE(32, 34); h.write("data", 36);
    h.writeUInt32LE(data.length, 40);
    writeFileSync(path, Buffer.concat([h, data]));
}

export function rms(a, from, to) {                     // seconds
    const i0 = Math.floor(from * SR), i1 = Math.min(a.length, Math.floor(to * SR));
    let s = 0;
    for (let i = i0; i < i1; i++) s += a[i] * a[i];
    return Math.sqrt(s / Math.max(1, i1 - i0));
}
export const db = (x) => (x > 0 ? 20 * Math.log10(x) : -Infinity);
