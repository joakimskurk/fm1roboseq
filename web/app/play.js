// SPDX-License-Identifier: GPL-3.0-only
// RoboSeq on its own page: the firmware runs in the audio thread (worklet.js, roboseq.wasm); here its screen, its
// button lights and the FM-1's controls, drawn in the page's style: the knobs (drag, scroll or the arrow keys), the
// buttons, the 27 keys (click or touch, several at once), the computer's keyboard and a MIDI keyboard as its keys.
// Nothing is loaded before Start (a browser plays sound only after a click). After FM-1 Clock's web/app/clock.js.
const $ = (id) => document.getElementById(id);
const fm1 = $('fm1');
if (fm1) {
  const lcd = $('lcd'), g = lcd.getContext('2d');
  const small = document.createElement('canvas');
  small.width = small.height = 240;
  const gs = small.getContext('2d');
  const img = gs.createImageData(240, 240);
  const px32 = new Uint32Array(img.data.buffer);
  const status = $('appstatus');
  let ac = null, node = null, gain = null, booting = false, held = 0;

  const say = (t) => { status.textContent = t; };
  const send = (m) => node && node.port.postMessage(m);

  function draw(p) {                             // RGB565 -> the canvas, twice its size, sharp
    for (let i = 0; i < 57600; i++) {
      const c = p[i];
      const r = ((c >> 11) * 527 + 23) >> 6, gg = (((c >> 5) & 63) * 259 + 33) >> 6, b = ((c & 31) * 527 + 23) >> 6;
      px32[i] = 0xFF000000 | (b << 16) | (gg << 8) | r;
    }
    gs.putImageData(img, 0, 0);
    g.imageSmoothingEnabled = false;
    g.drawImage(small, 0, 0, lcd.width, lcd.height);
  }

  // ---- the keys: F3 .. G5, black where the octave has them; the black keys' marks are the robots' (the nudges)
  const isBlack = (k) => (0x54A >> ((53 + k) % 12)) & 1;
  const NAME = ['F', 'F#', 'G', 'G#', 'A', 'A#', 'B', 'C', 'C#', 'D', 'D#', 'E'];
  const keysEl = $('keys'), keyEls = [];
  const whites = [...Array(27).keys()].filter((k) => !isBlack(k)).length;
  let wi = 0, bi = 0;
  for (let k = 0; k < 27; k++) {
    const el = document.createElement('button');
    el.type = 'button';
    el.tabIndex = -1;
    const name = NAME[k % 12] + (3 + Math.floor((k + 5) / 12));
    el.setAttribute('aria-label', name);
    if (isBlack(k)) {
      el.className = 'kb';                       // (its mark: whose nudge, or what it does on the page: D46)
      el.style.left = `calc(${(100 * wi) / whites}% - ${(100 * 0.34) / whites}%)`;
      el.style.width = `${(100 * 0.68) / whites}%`;
      bi++;
    } else {
      el.className = 'kw';
      el.style.left = `${(100 * wi) / whites}%`;
      el.style.width = `${100 / whites}%`;
      wi++;
    }
    keysEl.appendChild(el);
    keyEls.push(el);
  }
  const press = (k, on) => {                     // a key held or let go, from any of its sources
    const was = held;
    held = on ? held | (1 << k) : held & ~(1 << k);
    keyEls[k].classList.toggle('down', !!(held & (1 << k)));
    if (held !== was) send({ type: 'keys', mask: held });
  };
  const touching = new Map();                    // pointer -> key: click or touch, several fingers at once
  keysEl.addEventListener('pointerdown', (e) => {
    const k = keyEls.indexOf(e.target);
    if (k < 0) return;
    e.preventDefault();
    keysEl.setPointerCapture(e.pointerId);
    touching.set(e.pointerId, k);
    press(k, true);
  });
  const lift = (e) => { const k = touching.get(e.pointerId); if (k !== undefined) { touching.delete(e.pointerId); press(k, false); } };
  keysEl.addEventListener('pointerup', lift);
  keysEl.addEventListener('pointercancel', lift);
  keysEl.addEventListener('pointermove', (e) => {   // a finger sliding along the keys
    if (!touching.has(e.pointerId)) return;
    const el = document.elementFromPoint(e.clientX, e.clientY), k = keyEls.indexOf(el);
    const was = touching.get(e.pointerId);
    if (k >= 0 && k !== was) { press(was, false); touching.set(e.pointerId, k); press(k, true); }
  });

  // the computer's keyboard: two rows as a piano's, the white keys Z .. / and Q .. Y, the black ones above them
  const CODE = { KeyZ: 0, KeyS: 1, KeyX: 2, KeyD: 3, KeyC: 4, KeyF: 5, KeyV: 6, KeyB: 7, KeyH: 8, KeyN: 9, KeyJ: 10,
                 KeyM: 11, Comma: 12, KeyL: 13, Period: 14, Semicolon: 15, Slash: 16, Quote: 17, KeyQ: 18, KeyW: 19,
                 Digit3: 20, KeyE: 21, Digit4: 22, KeyR: 23, KeyT: 24, Digit6: 25, KeyY: 26 };
  // (only while the FM-1 is the one played: a click on it; a click elsewhere gives the keyboard back to the page)
  let active = false;
  document.addEventListener('pointerdown', (e) => { active = fm1.contains(e.target); fm1.classList.toggle('active', active && !!node); });
  document.addEventListener('keydown', (e) => {
    if (!node || !active || e.metaKey || e.ctrlKey || e.altKey) return;
    if (e.code in CODE) { e.preventDefault(); if (!e.repeat) press(CODE[e.code], true); }
    else if (e.code === 'Space') {                // PLAY (a button with the focus would be pressed by it too: not so)
      e.preventDefault();
      if (!e.repeat) send({ type: 'btn', id: 12 });
    } else if (e.code === 'Escape') send({ type: 'btn', id: 8 });   // HOME
  });
  document.addEventListener('keyup', (e) => { if (e.code in CODE) press(CODE[e.code], false); });
  window.addEventListener('blur', () => { for (let k = 0; k < 27; k++) if (held & (1 << k)) press(k, false); });

  // ---- what the keys and knobs do now (D46): the page RoboSeq is on, as the firmware tells it (web_app_state), named
  // on each key (its full text on hover) and knob, and said in a line over the keys
  const WH = [...Array(27).keys()].filter((k) => !isBlack(k)), BL = [...Array(27).keys()].filter((k) => isBlack(k));
  const NUDGE = ['↺', '↑', '↻', '»', '◎'], NUDGE_WORD = ['turn left', 'move', 'turn right', 'dash', 'ping'];
  const LAYER = ['move', 'modifier', 'chance'];
  const KEYCAP = {};                             // the computer's key for each, as printed on it
  for (const [code, k] of Object.entries(CODE))
    KEYCAP[k] = { Comma: ',', Period: '.', Slash: '/', Semicolon: ';', Quote: "'" }[code] || code.replace(/^Key|^Digit/, '');
  const noteName = (k) => NAME[k % 12] + (3 + Math.floor((k + 5) / 12));
  for (const k of keyEls.keys()) {
    const el = keyEls[k];
    el.innerHTML = `<span class="lbl"></span><span class="ck">${KEYCAP[k] || ''}</span>`;
  }

  function describe(st) {
    const pg = st & 15, lead = st >> 4 & 1, arp = st >> 5 & 1, layer = st >> 6 & 3, who = st >> 8 & 1, bank = st >> 9 & 3,
          row = st >> 11 & 3, d12 = st >> 13 & 1;
    const L = Array.from({ length: 27 }, () => ['', '', '']);   // the key's name, its colour, what it does
    const set = (k, t, c, d) => { L[k] = [t, c, d]; };
    const white = (i, t, c, d) => set(WH[i], t, c, d);
    const nudges = () => BL.forEach((k, i) => (i < 10
      ? set(k, NUDGE[i % 5], i < 5 ? 'scout' : 'echo', `${i < 5 ? 'Scout-7' : 'Echo-3'}: ${NUDGE_WORD[i % 5]}, once (its next move)`)
      : set(k, '⇄', 'swap', 'swap the two robots’ programs')));
    const robot = who ? 'Echo-3' : 'Scout-7';
    let page, help, kn = ['', '', '', '', ''], oct = '';
    if (lead) {
      for (let k = 0; k < 27; k++) set(k, noteName(k), 'note', `the lead: ${noteName(k)}`);
      page = arp ? 'ARP' : 'REC';
      help = arp ? 'Hold a chord and the lead plays it as an arpeggio, in time with the robots. K4 picks the pattern, '
                   + 'SELECT the notes per step. Press REC again to give the keys back.'
                 : 'The keys play the lead over the robots. K1 to K3 shape its sound, K4 and OCT− / OCT+ its octave. '
                   + 'Press REC again to give the keys back.';
      kn = [arp ? 'NOTES/STEP' : '', 'CUT', 'ECHO', 'BITE', arp ? 'PATTERN' : 'OCTAVE'];
      oct = 'OCTAVE';
    } else if (pg === 0) {                        // HOME
      for (let i = 0; i < 16; i++) white(i, String(bank * 16 + i + 1), 'step', `${robot}'s step ${bank * 16 + i + 1}: change its ${LAYER[layer]}`);
      nudges();
      page = 'HOME';
      help = `The white keys are ${robot}'s steps ${bank * 16 + 1} to ${bank * 16 + 16}: a press changes the step's `
           + `${LAYER[layer]} (SELECT picks move, modifier or chance; EDIT picks the robot). The black keys nudge the `
           + 'robots, red Scout-7 and orange Echo-3, and the last one swaps their programs.';
      kn = [LAYER[layer].toUpperCase(), 'LENGTH', 'ROLL', 'ROLL BOTH', ''];
      oct = '16 STEPS ←→';
    } else if (pg === 1) {                        // SEQ
      [['↺', 'turn left'], ['↑', 'move'], ['↓', 'back'], ['·', 'wait'], ['↻', 'turn right'], ['»', 'dash'], ['◎', 'ping']]
        .forEach(([t, w], k) => set(k, t, 'move', `type ${w.toUpperCase()} at the cursor, and on to the next step`));
      [[7, 'STDY', 'steady'], [9, 'BRGT', 'bright'], [11, 'LOW', 'low'], [12, 'WOBL', 'wobble'], [14, 'GLCH', 'glitch']]
        .forEach(([k, t, w]) => set(k, t, 'mod', `the cursor's modifier: ${w}`));
      [[16, '100'], [18, '75'], [19, '50']].forEach(([k, t]) => set(k, t + '%', 'prob', `the cursor's chance: ${t} %`));
      [[20, 'UNDO', 'undo'], [21, 'ROLL', `roll ${robot}'s program`], [23, 'ROLL2', 'roll both programs'],
       [24, '⇄', 'swap the two programs'], [26, 'CLR', 'clear both programs']].forEach(([k, t, w]) => set(k, t, 'tool', w));
      page = 'SEQ';
      help = `Type ${robot}'s program at the cursor (EDIT picks the robot): the first keys are the moves, laid out as `
           + 'A W S D in a game, then the modifier, the chance, and the tools: undo, roll this program, roll both, swap '
           + 'the two, clear.';
      kn = ['CURSOR', 'LENGTH', '', '', ''];
      oct = 'CURSOR ±8';
    } else if (pg === 2) {                        // GLO
      white(15, 'BATTLE', 'tool', 'open the battle: two players');
      nudges();
      page = 'GLO';
      help = 'The global settings are on the knobs: tempo, how often the robots move, the loop’s length and the speed; '
           + 'SELECT picks the engine. The last white key opens the battle. The black keys still nudge the robots.';
      kn = ['ENGINE', 'TEMPO', 'TICKS', 'LOOP', 'SPEED'];
    } else if (pg === 3 || pg === 4) {            // SCL: Voice Mod; again: Seq 2
      for (let i = 0; i < 4; i++) white(i, String(i + 1), 'step', `pick ${pg === 3 ? 'Voice Mod' : 'Seq 2'}'s step ${i + 1}`);
      const last = pg === 3 ? [['FOLW', 'FOLLOW: a new progression each round'], ['NEXT', 'on to the next step (MANUAL)'],
                               ['ROOT', 'the root transposes the robots, on / off'], ['RST', 'reset'], ['RND', 'a random progression']]
                            : [['ON', 'Seq 2 on / off'], ['RL A', 'roll Scout-7’s program each round'],
                               ['RL B', 'roll Echo-3’s program each round'], ['RST', 'reset'], ['RND', 'random']];
      last.forEach(([t, d], i) => white(11 + i, t, 'tool', d));
      nudges();
      page = pg === 3 ? 'SCL · VOICE MOD' : 'SCL · SEQ 2';
      help = pg === 3 ? 'The chords: keys 1 to 4 pick a step, and the knobs set its scale, root and shift, and when it '
                        + 'moves on. The last five keys: FOLLOW, NEXT, ROOT, reset, random. Press SCL again for Seq 2.'
                      : 'The song: keys 1 to 4 pick a step, and the knobs set its board, drums, high/low swap and chord '
                        + 'progression. The last five keys: on, roll Scout-7 or Echo-3 at each round, reset, random.';
      kn = pg === 3 ? ['', 'SCALE', 'ROOT', 'SHIFT', 'WHEN'] : ['', 'BOARD', 'DRUMS', 'HI/LO', 'CHAIN'];
    } else if (pg === 5) {                        // LFO: the drums
      const n = d12 ? 12 : 16, rowName = ['kick', 'snare', 'hihat'][row] || 'kick';
      for (let i = 0; i < n; i++) white(i, String(i + 1), 'step', `the ${rowName}'s step ${i + 1}: on / off`);
      nudges();
      page = 'LFO · DRUMS';
      help = `The white keys turn the ${rowName}'s steps on and off (SELECT picks kick, snare or hihat). K1 picks a `
           + 'pattern, K4 turns the drums on and off.';
      kn = ['ROW', 'PATTERN', '', '', 'ON/OFF'];
    } else if (pg === 6) {                        // FX: the mix
      ['MAST', 'MEL', 'BOT B', 'PERC', 'FX', 'DRUM', 'LEAD'].forEach((t, i) => white(i, t, 'tool', `pick the ${t.toLowerCase()} fader`));
      nudges();
      page = 'FX · MIX';
      help = 'A white key picks a fader: master, melody, bot B, percussion, FX, drums or the lead. K1 sets its level, K2 '
           + 'how much the board colours the sound.';
      kn = ['', 'LEVEL', 'COLOUR', '', ''];
    } else if (pg === 7) {                        // ENV again: the builder
      [[0, '←'], [1, '↑'], [2, '↓'], [4, '→']].forEach(([k, t]) => set(k, t, 'move', 'move the cursor (a belt points this way)'));
      set(3, 'ERASE', 'tool', 'clear the tile under the cursor');
      [[7, 'BELT'], [9, 'ROT↺'], [11, 'ROT↻'], [12, 'ZAP'], [14, 'PAD'], [16, 'CRATE']]
        .forEach(([k, t]) => set(k, t, 'tile', `lay a ${t.replace('ROT↺', 'rotator left').replace('ROT↻', 'rotator right').toLowerCase()} under the cursor`));
      [[19, 'ROLL', 'roll a random board'], [21, 'USER', 'load your board'], [23, 'SAVE', 'save this board as yours'],
       [24, 'CLR', 'clear your board'], [26, 'PADS', 'the robots back to their pads']].forEach(([k, t, d]) => set(k, t, 'tool', d));
      page = 'ENV · BUILDER';
      help = 'Build the board under the robots: the arrows move the cursor, the next keys lay a tile under it (a belt points '
           + 'the way you last moved), and the last ones roll a board, load or save your own, clear it, or send the robots '
           + 'to their pads.';
      kn = ['', 'PRESET', 'FULLNESS', '', ''];
    } else if (pg === 8) {                        // ENV: the boards
      ['DEF', 'CORR', 'GARD', 'GAUN', 'SPIN', 'MAZE'].forEach((t, i) => white(i, t, 'tile', `the ${t} board, as the loop comes round`));
      [['ROLL', 'roll a random board'], ['USER', 'load your board'], ['SAVE', 'save this board as yours'],
       ['CLR', 'clear your board'], ['PADS', 'the robots back to their pads']].forEach(([t, d], i) => white(6 + i, t, 'tool', d));
      nudges();
      page = 'ENV · BOARDS';
      help = 'Keys 1 to 6 pick a board (it comes in as the loop comes round), 7 rolls a random one, 8 loads yours, 9 saves '
           + 'this one as yours, 10 clears yours, 11 sends the robots to their pads. Press ENV again to build one.';
      kn = ['', 'PRESET', 'FULLNESS', '', ''];
    } else if (pg === 9) {                        // the battle
      [[0, '←'], [1, '↑'], [2, '↓'], [4, '→']].forEach(([k, t]) => set(k, t, 'scout', `Scout-7 walks ${t}`));
      set(7, 'HIT', 'scout', 'Scout-7 strikes');
      [[19, '←'], [20, '↑'], [21, '↓'], [23, '→']].forEach(([k, t]) => set(k, t, 'echo', `Echo-3 walks ${t}`));
      set(26, 'HIT', 'echo', 'Echo-3 strikes');
      set(13, 'RESET', 'tool', 'the match again from round 1');
      page = 'BATTLE';
      help = 'Two players. Scout-7 (red) walks on the left keys and strikes on C4; Echo-3 (orange) walks on the right '
           + 'keys and strikes on G5. Hold a direction to take a step on every beat. PLAY starts a match, F#4 starts it again.';
      kn = ['', 'TEMPO', 'ROUNDS', 'ARENA', 'NEXT'];
    } else {
      page = ''; help = '';
    }
    return { L, page, help, kn, oct };
  }

  let shownState = -1;
  function name(st) {                            // the labels, when the firmware's page or mode changed
    if (st === shownState) return;
    shownState = st;
    const d = describe(st);
    d.L.forEach(([t, c, w], k) => {
      const el = keyEls[k];
      el.firstChild.textContent = t;
      el.dataset.c = c;
      el.title = `${noteName(k)}${KEYCAP[k] ? ` (${KEYCAP[k]})` : ''}${w ? ': ' + w : ': nothing here'}`;
    });
    ['kf0', 'kf2', 'kf3', 'kf4', 'kf5'].forEach((id, i) => { $(id).textContent = d.kn[i] || '·'; });
    $('kf-oct').textContent = d.oct || '·';
    $('kh-page').textContent = d.page;
    $('kh-text').textContent = d.help;
  }
  name(0);

  // ---- the buttons, lit as the firmware lights them
  const btnEls = [...fm1.querySelectorAll('[data-btn]')];
  for (const b of btnEls) {                     // held as on the FM-1: a page's button held shows its key map (D46)
    let down = null;
    b.addEventListener('pointerdown', (e) => { e.preventDefault(); b.setPointerCapture(e.pointerId); down = e.pointerId;
                                               send({ type: 'btndown', id: +b.dataset.btn }); });
    const up = (e) => { if (down !== e.pointerId) return; down = null; send({ type: 'btnup', id: +b.dataset.btn }); };
    b.addEventListener('pointerup', up);
    b.addEventListener('pointercancel', up);
    b.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); send({ type: 'btn', id: +b.dataset.btn }); } });
  }

  // ---- the knobs: a detent each 14 px dragged, each arrow key, and the wheel once the knob is clicked (focused): a
  // scroll of the page over a knob scrolls the page, and a trackpad's swipe is gathered, 60 px a detent (D45: a
  // scroll over the panel turned the tempo, the loop and the speed by dozens of detents)
  const ANGLE = 18;
  for (const k of fm1.querySelectorAll('[data-enc], #vol')) {
    let turned = k.id === 'vol' ? 8 : 0, y0 = null, acc = 0, wacc = 0;
    const show = () => { k.style.setProperty('--a', `${k.id === 'vol' ? -135 + turned * 27 : turned * ANGLE}deg`); };
    const turn = (n) => {
      if (k.id === 'vol') {                       // MASTER: the page's volume, 0 .. 10
        turned = Math.max(0, Math.min(10, turned + n));
        if (gain) gain.gain.value = (turned / 10) ** 2;
        k.setAttribute('aria-valuenow', String(turned * 10));
      } else {
        turned += n;
        send({ type: 'enc', id: +k.dataset.enc, n });
      }
      show();
    };
    k.addEventListener('pointerdown', (e) => { e.preventDefault(); k.setPointerCapture(e.pointerId); y0 = e.clientY; acc = 0; k.focus(); });
    k.addEventListener('pointermove', (e) => {
      if (y0 === null) return;
      acc += y0 - e.clientY; y0 = e.clientY;
      while (Math.abs(acc) >= 14) { const n = acc > 0 ? 1 : -1; turn(n); acc -= 14 * n; }
    });
    const end = () => { y0 = null; };
    k.addEventListener('pointerup', end);
    k.addEventListener('pointercancel', end);
    k.addEventListener('wheel', (e) => {
      if (document.activeElement !== k) return;   // (not clicked: the page scrolls)
      e.preventDefault();
      wacc -= e.deltaY * (e.deltaMode === 1 ? 20 : e.deltaMode === 2 ? 400 : 1);
      while (Math.abs(wacc) >= 60) { const n = wacc > 0 ? 1 : -1; turn(n); wacc -= 60 * n; }
    }, { passive: false });
    k.addEventListener('blur', () => { wacc = 0; });
    k.addEventListener('keydown', (e) => {
      const n = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1 }[e.key];
      if (n) { e.preventDefault(); e.stopPropagation(); turn(n); }
    });
    show();
  }

  // ---- a MIDI keyboard as the keys: F3 (53) .. G5 (79)
  $('midi').addEventListener('click', async () => {
    if (!navigator.requestMIDIAccess) { say('This browser cannot use a MIDI keyboard (Chrome or Edge can).'); return; }
    try {
      const midi = await navigator.requestMIDIAccess();
      const hook = () => {
        for (const input of midi.inputs.values())
          input.onmidimessage = ({ data: [st, d1, d2] }) => {
            const k = d1 - 53, on = (st & 0xF0) === 0x90 && d2 > 0, off = (st & 0xF0) === 0x80 || ((st & 0xF0) === 0x90 && d2 === 0);
            if (k >= 0 && k < 27 && (on || off)) press(k, on);
          };
      };
      hook();
      midi.onstatechange = hook;
      $('midi').setAttribute('aria-pressed', 'true');
      say(midi.inputs.size ? 'A MIDI keyboard plays the keys, F3 to G5.' : 'No MIDI keyboard found yet: plug one in.');
    } catch (err) { say('MIDI was not allowed.'); }
  });

  // ---- the project RoboSeq keeps (its blobs 0 and 1, D43), kept in this browser
  const STORE = 'roboseq-project-';
  const loadBlobs = () => {
    const out = [];
    try {
      for (const i of [0, 1]) {
        const s = localStorage.getItem(STORE + i);
        if (s) out.push([i, Uint8Array.from(atob(s), (c) => c.charCodeAt(0))]);
      }
    } catch (err) { /* (no storage here: it starts as new) */ }
    return out;
  };
  const keepBlobs = (blobs) => {
    try {
      for (const [i, b] of blobs) {
        let s = '';
        for (let j = 0; j < b.length; j += 0x8000) s += String.fromCharCode(...b.subarray(j, j + 0x8000));
        localStorage.setItem(STORE + i, btoa(s));
      }
    } catch (err) { /* (kept for this visit only) */ }
  };

  function show(leds) {
    for (const b of btnEls) b.classList.toggle('lit', !!(leds & (1 << +b.dataset.btn)));
  }

  async function start() {
    if (booting) return;
    if (ac) {                                    // a pause, a go on
      if (ac.state === 'running') { await ac.suspend(); $('start').hidden = false; $('start').textContent = '▶ Go on'; say('Paused.'); }
      else { await ac.resume(); $('start').hidden = true; say('Running. Press PLAY.'); }
      return;
    }
    booting = true;
    say('Loading the firmware…');
    try {
      ac = new AudioContext({ sampleRate: 44100, latencyHint: 'interactive' });
      const [wasm] = await Promise.all([
        fetch('app/roboseq.wasm').then((r) => { if (!r.ok) throw new Error(`roboseq.wasm: ${r.status}`); return r.arrayBuffer(); }),
        ac.audioWorklet.addModule('app/worklet.js'),
      ]);
      node = new AudioWorkletNode(ac, 'roboseq', { numberOfInputs: 0, outputChannelCount: [2] });
      gain = ac.createGain();
      gain.gain.value = 0.64;
      node.connect(gain).connect(ac.destination);
      node.port.onmessage = (e) => {
        const m = e.data;
        if (m.type === 'frame') { if (m.lcd) draw(m.lcd); show(m.leds); name(m.app); if (m.blobs) keepBlobs(m.blobs); }
        else if (m.type === 'ready') { $('start').hidden = true; fm1.classList.add('on'); fm1.classList.toggle('active', active); say('Running. Press PLAY.'); }
        else if (m.type === 'error') say(`It did not start: ${m.message}`);
      };
      node.port.postMessage({ type: 'boot', wasm, blobs: loadBlobs() }, [wasm]);
      await ac.resume();
    } catch (err) {
      say(`It did not start here: ${err.message || err}. It needs a browser with Web Audio worklets (any recent one).`);
      ac = null;
      node = null;
    }
    booting = false;
  }
  $('start').addEventListener('click', start);
  $('pause').addEventListener('click', () => { if (ac) start(); });
  $('forget').addEventListener('click', () => {
    try { for (const i of [0, 1]) localStorage.removeItem(STORE + i); } catch (err) { /* (nothing kept) */ }
    say('The project kept in this browser is forgotten: the next visit starts as new.');
  });
}
