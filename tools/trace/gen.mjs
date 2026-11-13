// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Joakim (joakimskurk)
// The golden traces: reference/roboseq.html's game (tools/trace/html.mjs) run tick by tick for a set of scenarios,
// written to tests/golden/traces/*.trace. tests/roboseq/trace_test.c runs the same scenarios through the C model and
// compares line by line. node tools/trace/gen.mjs  (make traces)
//
// A .trace file: the scenario as "key value" lines, then one "T" line per tick:
//   T cycle step moveTick drumTick vmIndex vmCount seq2Index seq2Active swapped board
//     | A x y dir heat deaths dying | B x y dir heat deaths dying | clash love | drums k s h | events
// and after it one "N" line per note the HTML's audio played in that tick, in call order:
//   N voice midi duration offset velocity      (midi -1 for noise; seconds, the offset from the tick's time)
import { mkdirSync, writeFileSync, readFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { loadRoboSeq, HTML } from "./html.mjs";

const OUT = new URL("../../tests/golden/traces/", import.meta.url);

const g = (x) => Number(x.toPrecision(9)).toString();
function midiOf(name) {                               // "m57" (Tone.Frequency in html.mjs) or "G2" (C4 = 60)
    if (name[0] === "m") return Number(name.slice(1));
    const m = /^([A-G])(#|b)?(-?\d+)$/.exec(name);
    if (!m) throw new Error("note " + name);
    return 12 * (Number(m[3]) + 1) + { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 }[m[1]] + (m[2] === "#" ? 1 : m[2] === "b" ? -1 : 0);
}

function lcg(seed) {                                  // the scenarios' own randomness: fixed, so the traces are too
    let s = seed >>> 0;
    return () => ((s = (Math.imul(s, 1664525) + 1013904223) >>> 0) / 4294967296);
}

function run(rs, sc) {
    const st = rs.state;
    st.playing = false;
    rs.applyBoardPreset(sc.board);
    st.loopLength = sc.loop;
    st.bpm = sc.bpm;
    st.playbackRate = sc.quarter ? 0.25 : 1;
    st.transposeWithRoot = sc.transpose;
    st.ticksPerMove = sc.tpm;
    st.voiceModTrigger = sc.vmTrigger;
    st.voiceModLoopFreq = sc.vmFreq;
    st.bassMod.length = sc.bassModLen;
    if (st.registersSwapped) rs.swapRegisters();       // (the ranges are the page's own variables: swap them back too)
    st.drums.on = sc.drumsOn;
    st.drums.presetId = sc.drumPreset;
    st.drums.pattern = rs.DRUM_PRESETS.find((p) => p.id === sc.drumPreset).pattern(rs.getDrumSteps(sc.loop));
    st.seq2.active = sc.seq2.active;
    st.seq2.pendingActive = sc.seq2.pending;
    st.seq2.index = 0;
    st.seq2.rndBotA = false;
    st.seq2.rndBotB = false;
    st.seq2.steps = sc.seq2.steps.map((s) => ({ ...s }));
    for (const id of ["A", "B"]) {
        st.bots[id].programLength = sc.prog[id].len;
        st.bots[id].steps = sc.prog[id].steps.map((s) => ({ action: s.action, modifier: s.modifier, probability: s.probability, repeat: 1 }));
    }
    st.pendingBoard = null;
    rs.resetBoardState(false);
    st.playing = true;
    rs.events();
    take();

    const lines = [];
    lines.push(`# ${sc.name}: reference/roboseq.html run by tools/trace/gen.mjs`);
    lines.push(`board ${sc.board}`, `loop ${sc.loop}`, `bpm ${sc.bpm} ${sc.quarter ? 1 : 0} ${sc.transpose ? 1 : 0}`, `tpm ${sc.tpm}`, `vmtrigger ${sc.vmTrigger}`,
        `vmfreq ${sc.vmFreq * 4}`, `bassmodlen ${sc.bassModLen}`, `drums ${sc.drumsOn ? 1 : 0} ${sc.drumPreset}`,
        `seq2 ${sc.seq2.active ? 1 : 0} ${sc.seq2.pending ? 1 : 0}`);
    sc.seq2.steps.forEach((s, i) => lines.push(`seq2step ${i} ${s.boardId} ${s.drumPresetId} ${s.regSwap ? 1 : 0}`));
    for (const id of ["A", "B"]) {
        lines.push(`prog ${id} ${sc.prog[id].len}`);
        sc.prog[id].steps.forEach((s, i) => lines.push(`step ${id} ${i} ${s.action} ${s.modifier} ${Math.round(s.probability * 100)}`));
    }
    lines.push(`ticks ${sc.ticks}`);
    for (let t = 0; t < sc.ticks; t++) {
        const drumTick = st.drumTick, isMove = st.moveTick % (st.ticksPerMove || 1) === 0;
        const t0 = t * rs.getStepDurationSeconds();
        const clashes0 = (st.boardState.clashes || []).length;
        rs.advanceSequence(t0);
        // the tiles the tick's events lit (activeKeys, "x,y") as bits y * 5 + x, and who was dashed into
        let lit = 0;
        for (const key of st.boardState.activeKeys || []) { const [x, y] = key.split(",").map(Number); lit |= 1 << (y * 5 + x); }
        const hit = { A: 0, B: 0 };
        for (const c of (st.boardState.clashes || []).slice(clashes0)) hit[c.attacker === "A" ? "B" : "A"] = 1;
        const ev = rs.events();
        const notes = take();
        // the drum step that played this tick: the pattern at the drum tick before it advanced (a reset tick plays 0)
        let k = 0, s = 0, h = 0;
        if (st.drums.on && isMove) {
            const i = (st.currentStep === 0 && st.drumTick === 1 ? 0 : drumTick) % rs.getDrumSteps();
            const p = st.drums.pattern;
            k = p.kick[i] ? 1 : 0; s = p.snare[i] ? 1 : 0; h = p.hihat[i] ? 1 : 0;
        }
        const b = (id) => { const o = st.boardState[id]; return `${o.x} ${o.y} ${o.dir} ${o.heat} ${o.deaths} ${o.dying ? 1 : 0}`; };
        lines.push(`T ${st.cycle} ${st.currentStep} ${st.moveTick} ${st.drumTick} ${st.voiceModIndex} ${st.voiceModStepCount} ` +
            `${st.seq2.index} ${st.seq2.active ? 1 : 0} ${st.registersSwapped ? 1 : 0} ${st.activeBoardId} | A ${b("A")} | B ${b("B")} | ` +
            `${st.boardState.clashCooldown} ${st.boardState.loveSteps || 0} | ${k} ${s} ${h} | ${ev.join(",") || "-"} | ` +
            `${(lit >>> 0).toString(16)} ${hit.A}${hit.B}`);
        for (const n of notes) {
            const [voice, ...a] = n;
            const noise = typeof a[0] === "number";
            const [note, dur, time, vel] = noise ? [null, ...a] : a;
            lines.push(`N ${voice} ${noise ? -1 : midiOf(note)} ${g(dur)} ${g(time - t0)} ${g(vel)}`);
        }
    }
    return lines.join("\n") + "\n";
}

const ACTIONS = ["move", "back", "turnL", "turnR", "dash", "ping", "wait"];
const MODS = ["steady", "bright", "low", "wobble", "glitch"];
const PROBS = [1, 0.75, 0.5];
const BOARDS = ["default", "corridor", "garden", "gauntlet", "spinner", "maze"];
const DRUMS = ["four", "break", "half", "offbeat", "triplet", "minimal"];
const LOOPS = [8, 12, 16, 24, 32, 48, 64];

function base(rs, name) {
    const st = rs.buildDefaultState();
    return {
        name, board: "default", loop: 16, bpm: 80, quarter: false, transpose: true, tpm: 1, vmTrigger: "loop", vmFreq: 1, bassModLen: 4,
        drumsOn: true, drumPreset: "four",
        seq2: { active: false, pending: false, steps: st.seq2.steps.map((s) => ({ ...s })) },
        prog: {
            A: { len: 16, steps: rs.buildBotDefaultSteps("A") },
            B: { len: 16, steps: rs.buildBotDefaultSteps("B") },
        },
        ticks: 200,
    };
}

function randomProgram(r, len) {
    return {
        len,
        steps: Array.from({ length: 64 }, () => ({
            action: ACTIONS[Math.floor(r() * ACTIONS.length)],
            modifier: MODS[Math.floor(r() * MODS.length)],
            probability: PROBS[Math.floor(r() * PROBS.length)],
        })),
    };
}

const rs = loadRoboSeq();
const take = rs.record();
const scenarios = [];
scenarios.push({ ...base(rs, "default"), ticks: 400 });
for (const b of BOARDS) scenarios.push({ ...base(rs, "board-" + b), board: b, ticks: 160 });
for (let i = 0; i < 24; i++) {
    const r = lcg(1000 + i), sc = base(rs, "random-" + String(i).padStart(2, "0"));
    sc.board = BOARDS[i % BOARDS.length];
    sc.loop = LOOPS[i % LOOPS.length];
    sc.tpm = [1, 1, 2, 4][i % 4];
    sc.vmFreq = [0.25, 0.5, 1, 2, 4][i % 5];
    sc.vmTrigger = i % 6 === 5 ? "manual" : "loop";
    sc.bassModLen = 4;                                // (this HTML's Seq 1 always has 4 steps: restoreBassMod)
    sc.drumsOn = i % 7 !== 3;
    sc.drumPreset = DRUMS[i % DRUMS.length];
    sc.prog.A = randomProgram(r, 1 + Math.floor(r() * 64));
    sc.prog.B = randomProgram(r, 1 + Math.floor(r() * 64));
    sc.ticks = 600;
    scenarios.push(sc);
}
{   // seq 2: board and drum changes at the loop resets, a register swap, a pending start
    const sc = base(rs, "seq2");
    sc.vmFreq = 0.25;
    sc.seq2 = { active: false, pending: true, steps: [
        { boardId: "gauntlet", drumPresetId: "break", regSwap: true },
        { boardId: "--", drumPresetId: "triplet", regSwap: false },
        { boardId: "maze", drumPresetId: "--", regSwap: true },
        { boardId: "default", drumPresetId: "four", regSwap: false },
    ] };
    sc.ticks = 800;
    scenarios.push(sc);
}
for (const [i, [bpm, q, tr, tpm]] of [[123, false, false, 2], [97, true, true, 4], [140, false, true, 1]].entries()) {
    // tempo, 1/4 speed and the root's transpose (every duration scales with them)
    const r = lcg(2000 + i), sc = base(rs, "tempo-" + i);
    Object.assign(sc, { bpm, quarter: q, transpose: tr, tpm, board: BOARDS[(i * 2 + 1) % BOARDS.length], ticks: 400 });
    sc.prog.A = randomProgram(r, 16);
    sc.prog.B = randomProgram(r, 12);
    scenarios.push(sc);
}
{   // heat and deaths: zaps and dashes into each other on the gauntlet
    const sc = base(rs, "deaths");
    sc.board = "gauntlet";
    const steps = (acts) => Array.from({ length: 64 }, (_, i) => ({ action: acts[i % acts.length], modifier: "steady", probability: 1 }));
    sc.prog.A = { len: 6, steps: steps(["move", "dash", "back", "dash", "ping", "turnR"]) };
    sc.prog.B = { len: 5, steps: steps(["dash", "turnL", "dash", "ping", "back"]) };
    sc.ticks = 400;
    scenarios.push(sc);
}

mkdirSync(OUT, { recursive: true });
const sha = createHash("sha256").update(readFileSync(HTML)).digest("hex");
const index = [`# the golden traces from reference/roboseq.html ${sha}`];
for (const sc of scenarios) {
    writeFileSync(new URL(sc.name + ".trace", OUT), run(rs, sc));
    index.push(sc.name);
}
writeFileSync(new URL("INDEX", OUT), index.join("\n") + "\n");

// seededChance for every step of both bots over 1024 loops, as classes (0: <= 0.5, 1: <= 0.75, 2: above), 2 bits
// each, 64 steps a line in hex (step 0 in the top bits): tests/roboseq checks rs_chance against it
const chance = [`# seededChance classes from reference/roboseq.html ${sha}: bot cycle hex`];
for (const bot of ["A", "B"]) {
    for (let c = 0; c < 1024; c++) {
        let hex = "";
        for (let s = 0; s < 64; s += 2) {
            let nib = 0;
            for (const k of [s, s + 1]) {
                const x = rs.seededChance(bot, k, c, 0);
                nib = (nib << 2) | (x > 0.75 ? 2 : x > 0.5 ? 1 : 0);
            }
            hex += nib.toString(16);
        }
        chance.push(`${bot} ${c} ${hex}`);
    }
}
writeFileSync(new URL("../chance.txt", OUT), chance.join("\n") + "\n");
console.log(`${scenarios.length} traces -> tests/golden/traces (html ${sha.slice(0, 12)})`);
