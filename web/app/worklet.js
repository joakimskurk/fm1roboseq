// SPDX-License-Identifier: GPL-3.0-only
// RoboSeq in a browser: the firmware (roboseq.wasm, web/app/build.sh) in the audio thread. Its sound is its time base,
// as the audio interrupt is on the FM-1: each quantum asks it for its frames, and it runs its main loop for the
// milliseconds they are worth. The screen, the button lights and a saved project go to the page. After FM-1 Clock's
// web/app/worklet.js.
const FS = 44100;                                // the firmware's rate; another context rate is resampled (linear)

class RoboSeq extends AudioWorkletProcessor {
  constructor() {
    super();
    this.x = null;
    this.rev = -1;
    this.blobRev = 0;
    this.since = 0;
    this.step = FS / sampleRate;                 // firmware frames per output frame
    this.pos = 0;                                // (resampling) where we are in buf, in its frames
    this.buf = new Float32Array(2 * 4096);       // its frames not yet behind us, L R L R
    this.have = 0;
    this.port.onmessage = (e) => this.message(e.data);
  }

  async message(m) {
    if (m.type === 'boot') {
      try {
        const { instance } = await WebAssembly.instantiate(m.wasm, {});
        const x = instance.exports;
        const size = x.web_blob_size();
        for (const [i, bytes] of m.blobs || []) {   // the project the browser kept, back before the start
          if (bytes && bytes.length === size) new Uint8Array(x.memory.buffer, x.web_blob(i), size).set(bytes);
        }
        x.web_init();
        this.blobRev = x.web_blob_rev();
        this.x = x;
        this.port.postMessage({ type: 'ready' });
      } catch (err) {
        this.port.postMessage({ type: 'error', message: String(err) });
      }
      return;
    }
    const x = this.x;
    if (!x) return;
    if (m.type === 'btn') x.web_btn(m.id);
    else if (m.type === 'enc') x.web_enc(m.id, m.n);
    else if (m.type === 'keys') x.web_keys(m.mask >>> 0);
  }

  process(_inputs, outputs) {
    const x = this.x;
    const out = outputs[0];
    if (!x) return true;
    const L = out[0], R = out[1] || out[0], n = L.length;
    if (this.step === 1) {                       // the usual case: the context runs at 44.1 kHz
      x.web_run(n);
      const o = new Int32Array(x.memory.buffer, x.web_out_ptr(), 2 * n);
      for (let i = 0; i < n; i++) {
        L[i] = o[2 * i] / 32768;
        R[i] = o[2 * i + 1] / 32768;
      }
    } else {                                     // another rate: a line between each two of its frames
      const st = this.step, b = this.buf;
      const top = Math.floor(this.pos + (n - 1) * st) + 2;   // the frames this quantum reads, from buf's start
      while (this.have < top) {
        x.web_run(128);
        const o = new Int32Array(x.memory.buffer, x.web_out_ptr(), 256);
        for (let i = 0; i < 256; i++) b[2 * this.have + i] = o[i] / 32768;
        this.have += 128;
      }
      for (let i = 0; i < n; i++) {
        const p = this.pos + i * st, j = p | 0, f = p - j;
        L[i] = b[2 * j] + (b[2 * j + 2] - b[2 * j]) * f;
        R[i] = b[2 * j + 1] + (b[2 * j + 3] - b[2 * j + 1]) * f;
      }
      this.pos += n * st;
      const done = this.pos | 0;                 // the frames behind us
      b.copyWithin(0, 2 * done, 2 * this.have);
      this.have -= done;
      this.pos -= done;
    }
    this.since += n;
    if (this.since >= sampleRate / 40) {         // the screen (when it changed), the lights, a project saved
      this.since = 0;
      const rev = x.web_lcd_rev();
      const msg = { type: 'frame', leds: x.web_leds_lo(), ms: x.web_ms() };
      const transfer = [];
      if (rev !== this.rev) {
        this.rev = rev;
        msg.lcd = new Uint16Array(x.memory.buffer, x.web_lcd(), 240 * 240).slice();
        transfer.push(msg.lcd.buffer);
      }
      const br = x.web_blob_rev();
      if (br !== this.blobRev) {                 // RoboSeq saved its project (blobs 0 and 1, D43): the page keeps them
        this.blobRev = br;
        const size = x.web_blob_size();
        msg.blobs = [0, 1].map((i) => [i, new Uint8Array(x.memory.buffer, x.web_blob(i), size).slice()]);
      }
      this.port.postMessage(msg, transfer);
    }
    return true;
  }
}

registerProcessor('roboseq', RoboSeq);
