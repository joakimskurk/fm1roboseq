// SPDX-License-Identifier: GPL-3.0-only
// The README's picture of the browser original: reference/roboseq.html as it opens (Tone.js from unpkg, as the page
// loads it), its welcome closed, PLAY pressed (the space bar), a few seconds of the robots, then the page as a PNG.
//   node tools/tone-ref/page_shot.mjs docs/screens/reference-page.png [width height seconds]
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { openBrowser } from "./page.mjs";

const here = dirname(fileURLToPath(import.meta.url));
const [out, w = "1280", h = "820", secs = "4"] = process.argv.slice(2);
if (!out) throw new Error("usage: page_shot.mjs OUT.png [width height seconds]");
const browser = await openBrowser();
const page = await browser.newPage();
page.on("pageerror", (e) => console.error("page error:", e.message));
await page.setViewport({ width: +w, height: +h, deviceScaleFactor: 1 });
await page.goto(pathToFileURL(join(here, "..", "..", "reference", "roboseq.html")).href, { waitUntil: "networkidle0" });
const close = await page.$('[data-action="close-tutorial"]');   // (its welcome, as a first visit has it: closed)
if (close) await close.click();
await page.keyboard.press("Space");                    // (the page's own key: play / stop)
await new Promise((r) => setTimeout(r, +secs * 1000));
await page.screenshot({ path: resolve(out) });
await browser.close();
console.log("page   ->", out);
