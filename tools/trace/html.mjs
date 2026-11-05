// SPDX-License-Identifier: GPL-3.0-only
// Runs reference/roboseq.html's own script in Node, without a browser: the DOM, Tone.js and localStorage are stubs,
// so nothing sounds or draws, but the game (the board, the bots, the registers, the sequencers) runs exactly as the
// page runs it. The script is changed in memory only, in two places: every event is also recorded (pushEvent), and
// the page's internals are handed out at the end instead of rendering.
//   import { loadRoboSeq } from "./html.mjs";  const rs = loadRoboSeq();  rs.state, rs.advanceSequence(0), ...
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const HERE = fileURLToPath(new URL(".", import.meta.url));
export const HTML = HERE + "../../reference/roboseq.html";

function stub(name = "stub") {
    // anything: callable, constructible, every property another stub; numbers and strings where asked
    const f = function () { return stub(name + "()"); };
    return new Proxy(f, {
        get(t, k) {
            if (k === Symbol.toPrimitive) return () => 0;
            if (k === "then") return undefined;                      // not a promise
            if (k === "length") return 0;
            if (k === "value" || k === "textContent" || k === "innerHTML" || k === "className") return "";
            if (k === "dataset" || k === "style") return {};
            if (k === "classList") return { add() {}, remove() {}, toggle() {}, contains() { return false; } };
            if (k === "querySelectorAll" || k === "getElementsByClassName") return () => [];
            if (k === "closest" || k === "querySelector" || k === "getElementById") return () => null;
            if (k === Symbol.iterator) return function* () {};
            return stub(name + "." + String(k));
        },
        apply() { return stub(name + "()"); },
        construct() { return stub("new " + name); },
        set() { return true; },
    });
}

export function loadRoboSeq() {
    const html = readFileSync(HTML, "utf8");
    const open = html.indexOf("<script>\n");
    const close = html.lastIndexOf("</script>");
    let src = html.slice(open + "<script>\n".length, close);
    const hook = "events.push({ kind, label });";
    if (!src.includes(hook)) throw new Error("pushEvent changed: " + hook);
    src = src.replace(hook, hook + " if (globalThis.__events) globalThis.__events.push(kind);");
    const end = /renderApp\(\);\s*\}\)\(\);\s*$/;
    if (!end.test(src)) throw new Error("the script's end changed");
    src = src.replace(end, `globalThis.__rs = {
        get state() { return state; }, BOARD, BOARD_PRESETS, DRUM_PRESETS, ACTIONS, MODIFIERS, SCALES,
        advanceSequence, resetBoardState, applyBoardPreset, seededChance, getDrumSteps, scaleQuantize,
        scaleIntervalShift, buildBotDefaultSteps, buildDefaultState, getStepDurationSeconds, swapRegisters,
        setAudio(a) { audio = a; },
    };
})();`);
    const store = new Map();
    let now = 1_700_000_000_000;
    const ctx = {
        console, Math, JSON, Object, Array, Number, String, Boolean, Symbol, Map, Set, Promise, Error, Proxy,
        Reflect, isNaN, parseInt, parseFloat,
        document: stub("document"),
        navigator: { userAgent: "node" },
        localStorage: {
            getItem: (k) => (store.has(k) ? store.get(k) : null),
            setItem: (k, v) => store.set(k, String(v)),
            removeItem: (k) => store.delete(k),
        },
        Tone: new Proxy({}, {
            get(t, k) {
                // Tone.Frequency(midi, "midi").toNote() -> "m<midi>" (the recorder reads it back); the rest stubs
                if (k === "Frequency") return (v, unit) => ({ toNote: () => (unit === "midi" ? "m" + v : String(v)) });
                return stub("Tone." + String(k));
            },
        }),
        setTimeout: () => 0, clearTimeout() {}, setInterval: () => 0, clearInterval() {},
        requestAnimationFrame: () => 0, cancelAnimationFrame() {},
        matchMedia: () => ({ matches: false, addEventListener() {}, addListener() {} }),
        addEventListener() {}, removeEventListener() {},
        Date: class extends Date { static now() { return now; } },
        performance: { now: () => 0 },
    };
    ctx.window = ctx;
    ctx.globalThis = ctx;
    vm.createContext(ctx);
    vm.runInContext(src, ctx, { filename: "roboseq.html" });
    const rs = ctx.__rs;
    rs.events = () => { const e = ctx.__events || []; ctx.__events = []; return e; };
    rs.record = () => {                  // a recording audio: every voice's triggerAttackRelease, in call order
        const notes = [];
        const voice = (name) => ({
            triggerAttackRelease: (...a) => notes.push([name, ...a]),
            triggerRelease() {}, releaseAll() {},
        });
        const audio = {
            botA: { main: voice("A.main"), ping: voice("A.ping"), arp: voice("A.arp") },
            botB: { fm: voice("B.fm"), sub: voice("B.sub"), arp: voice("B.arp"), noise: voice("B.noise") },
            fx: { collision: voice("fx.collision"), belt: voice("fx.belt"), rotator: voice("fx.rotator"),
                  zap: voice("fx.zap"), pulse: voice("fx.pulse") },
            drums: { kick: voice("drum.kick"), snare: voice("drum.snare"), hihat: voice("drum.hihat") },
            coin: voice("coin"),
            explosion: { thump: voice("ex.thump"), noise: voice("ex.noise"), crackle: voice("ex.crackle") },
            mixer: {},
        };
        rs.setAudio(audio);
        return () => notes.splice(0);
    };
    ctx.__events = [];
    return rs;
}
