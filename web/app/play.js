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
      el.className = 'kb ' + (bi < 5 ? 'scout' : bi < 10 ? 'echo' : 'swap');
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

  // ---- the buttons, lit as the firmware lights them
  const btnEls = [...fm1.querySelectorAll('[data-btn]')];
  for (const b of btnEls) b.addEventListener('click', () => send({ type: 'btn', id: +b.dataset.btn }));

  // ---- the knobs: a detent each 14 px dragged, each notch of a wheel, each arrow key; their mark turns with them
  const ANGLE = 18;
  for (const k of fm1.querySelectorAll('[data-enc], #vol')) {
    let turned = k.id === 'vol' ? 8 : 0, y0 = null, acc = 0;
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
    k.addEventListener('wheel', (e) => { e.preventDefault(); turn(e.deltaY < 0 ? 1 : -1); }, { passive: false });
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
        if (m.type === 'frame') { if (m.lcd) draw(m.lcd); show(m.leds); if (m.blobs) keepBlobs(m.blobs); }
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
